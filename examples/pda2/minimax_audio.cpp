/**
 * @file      minimax_audio.cpp
 * @brief     MiniMax speech_to_text + t2a_v2 client (see minimax_audio.h).
 *
 * Memory rules: the ASR multipart body (~193 KB for a 6 s WAV) and the
 * TTS response (hex mp3, 2x the audio size) live in PSRAM - the internal
 * heap never carries more than the small JSON/error strings.
 */
#include "Arduino.h"
#include "minimax_audio.h"
#include "http_utils.h"
#include "env_secrets.h"
#include "openai_api.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <cJSON.h>
#include <esp_heap_caps.h>
#include <string.h>

#define MM_ASR_URL "https://api.minimax.io/v1/speech_to_text"
#define MM_TTS_URL "https://api.minimax.io/v1/t2a_v2"
#define MM_TTS_MODEL "speech-2.6-turbo"
#define MM_TTS_VOICE "female-shaonv"   /* system voice; swap via this define */
#define MM_TTS_MAX_TEXT 300            /* keeps the hex response ~<600 KB */

bool minimax_audio_key(char *key, int key_len)
{
    key[0] = '\0';
    char base[160], model[80], k[160];
    if (ai_provider_get("minimax", base, sizeof(base), model, sizeof(model),
                        k, sizeof(k)) && k[0]) {
        strncpy(key, k, key_len - 1);
        key[key_len - 1] = '\0';
        return true;
    }
    return env_get("MINIMAX_AUDIO_KEY", key, key_len) && key[0];
}

/* Pull base_resp.status_msg / OpenAI-style error out of a JSON body. */
static void mm_parse_error(const char *body, char *err, int err_len,
                           int http_code)
{
    const char *msg = NULL;
    cJSON *root = cJSON_Parse(body);
    if (root) {
        cJSON *br = cJSON_GetObjectItem(root, "base_resp");
        cJSON *sm = br ? cJSON_GetObjectItem(br, "status_msg") : NULL;
        if (sm && cJSON_IsString(sm) && sm->valuestring[0]) msg = sm->valuestring;
        if (!msg) {
            cJSON *e = cJSON_GetObjectItem(root, "error");
            cJSON *m2 = e ? cJSON_GetObjectItem(e, "message") : NULL;
            if (m2 && cJSON_IsString(m2) && m2->valuestring[0])
                msg = m2->valuestring;
        }
    }
    snprintf(err, err_len, "HTTP %d: %.120s", http_code,
             msg ? msg : body);
    cJSON_Delete(root);
}

bool minimax_asr(const uint8_t *wav, size_t wav_len, const char *key,
                 char *text, int text_len, char *err, int err_len)
{
    text[0] = '\0';
    err[0] = '\0';
    if (!wav || wav_len == 0) {
        snprintf(err, err_len, "no audio");
        return false;
    }

    /* Request shape verified from the PC against the live API (2026-09-11):
     * dashed boundary like curl -F, model + file parts only (an extra
     * response_format field returned HTTP 400 "Error when parsing
     * request"); json is the default response format anyway. */
    const char *bnd = "----pda2minimax7f3a1c";
    std::string head;
    head += "--" + std::string(bnd) + "\r\n";
    head += "Content-Disposition: form-data; name=\"model\"\r\n\r\nasr-1.0\r\n";
    head += "--" + std::string(bnd) + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"take.wav\"\r\n";
    head += "Content-Type: audio/wav\r\n\r\n";
    std::string tail = "\r\n--" + std::string(bnd) + "--\r\n";

    size_t total = head.size() + wav_len + tail.size();
    uint8_t *buf = (uint8_t *)heap_caps_malloc(total,
                                               MALLOC_CAP_SPIRAM |
                                               MALLOC_CAP_8BIT);
    if (!buf) {
        snprintf(err, err_len, "no PSRAM for %u B body", (unsigned)total);
        return false;
    }
    memcpy(buf, head.data(), head.size());
    memcpy(buf + head.size(), wav, wav_len);
    memcpy(buf + head.size() + wav_len, tail.data(), tail.size());

    if (!http_ensure_time(5000)) {
        free(buf);
        snprintf(err, err_len, "time not synced (NTP)");
        return false;
    }
    WiFiClientSecure secure;
    http_apply_tls(secure);
    HTTPClient http;
    http.setTimeout(30000);
    http.setReuse(false);
    if (!http.begin(secure, MM_ASR_URL)) {
        free(buf);
        snprintf(err, err_len, "connect failed");
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + key);
    http.addHeader("Content-Type",
                   String("multipart/form-data; boundary=") + bnd);
    int code = http.POST(buf, total);
    free(buf);
    if (code != 200) {
        mm_parse_error(http.getString().c_str(), err, err_len, code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    cJSON *root = cJSON_Parse(body.c_str());
    if (!root) {
        snprintf(err, err_len, "bad JSON");
        return false;
    }
    cJSON *t = cJSON_GetObjectItem(root, "text");
    bool ok = t && cJSON_IsString(t) && t->valuestring;
    if (ok) {
        strncpy(text, t->valuestring, text_len - 1);
        text[text_len - 1] = '\0';
    } else {
        snprintf(err, err_len, "no text in response");
    }
    cJSON_Delete(root);
    Serial.printf("[Minimax] asr: %s \"%s\"\n", ok ? "ok" : "fail", text);
    return ok;
}

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool minimax_tts(const char *text, const char *key, const char *spath,
                 char *err, int err_len)
{
    err[0] = '\0';
    if (!text || !text[0]) {
        snprintf(err, err_len, "no text");
        return false;
    }
    char t[MM_TTS_MAX_TEXT + 1];
    strncpy(t, text, MM_TTS_MAX_TEXT);
    t[MM_TTS_MAX_TEXT] = '\0';
    for (int i = 0; t[i]; i++)
        if (t[i] == '\n' || t[i] == '\r') t[i] = ' ';

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", MM_TTS_MODEL);
    cJSON_AddStringToObject(root, "text", t);
    cJSON_AddBoolToObject(root, "stream", 0);
    cJSON_AddStringToObject(root, "output_format", "hex");
    cJSON_AddStringToObject(root, "language_boost", "auto");
    cJSON *vs = cJSON_AddObjectToObject(root, "voice_setting");
    cJSON_AddStringToObject(vs, "voice_id", MM_TTS_VOICE);
    cJSON_AddNumberToObject(vs, "speed", 1.0);
    cJSON_AddNumberToObject(vs, "vol", 2.0);
    cJSON_AddNumberToObject(vs, "pitch", 0);
    cJSON *as = cJSON_AddObjectToObject(root, "audio_setting");
    cJSON_AddStringToObject(as, "format", "mp3");
    cJSON_AddNumberToObject(as, "sample_rate", 24000);
    cJSON_AddNumberToObject(as, "bitrate", 64000);
    cJSON_AddNumberToObject(as, "channel", 1);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        snprintf(err, err_len, "json build failed");
        return false;
    }

    if (!http_ensure_time(5000)) {
        cJSON_free(body);
        snprintf(err, err_len, "time not synced (NTP)");
        return false;
    }
    WiFiClientSecure secure;
    http_apply_tls(secure);
    HTTPClient http;
    http.setTimeout(45000);               /* synthesis takes a while */
    http.setReuse(false);
    if (!http.begin(secure, MM_TTS_URL)) {
        cJSON_free(body);
        snprintf(err, err_len, "connect failed");
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + key);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t *)body, strlen(body));
    cJSON_free(body);
    if (code != 200) {
        mm_parse_error(http.getString().c_str(), err, err_len, code);
        http.end();
        return false;
    }

    /* the body is a hex-encoded mp3 inside JSON - it can be several
     * hundred KB, so stream it into PSRAM instead of getString() */
    int size = http.getSize();            /* -1 when chunked */
    size_t cap = 1024 * 1024;
    if (size > (int)cap) {
        http.end();
        snprintf(err, err_len, "response too big (%d)", size);
        return false;
    }
    if (size <= 0) size = (int)cap;
    char *rbuf = (char *)heap_caps_malloc((size_t)size + 1,
                                          MALLOC_CAP_SPIRAM |
                                          MALLOC_CAP_8BIT);
    if (!rbuf) {
        http.end();
        snprintf(err, err_len, "no PSRAM for response");
        return false;
    }
    Stream *s = http.getStreamPtr();
    size_t got = 0;
    uint32_t idle0 = millis();
    while (got < (size_t)size) {
        int avail = s->available();
        if (avail > 0) {
            size_t want = (size_t)size - got;
            if (want > (size_t)avail) want = (size_t)avail;
            got += s->readBytes(rbuf + got, want);
            idle0 = millis();
        } else {
            if (millis() - idle0 > 15000) break;
            delay(2);
        }
    }
    rbuf[got] = '\0';
    http.end();

    cJSON *jr = cJSON_Parse(rbuf);
    free(rbuf);
    if (!jr) {
        snprintf(err, err_len, "bad JSON (%u B)", (unsigned)got);
        return false;
    }
    cJSON *br = cJSON_GetObjectItem(jr, "base_resp");
    cJSON *sc = br ? cJSON_GetObjectItem(br, "status_code") : NULL;
    if (sc && cJSON_IsNumber(sc) && sc->valueint != 0) {
        cJSON *sm = br ? cJSON_GetObjectItem(br, "status_msg") : NULL;
        snprintf(err, err_len, "api %d: %.120s", sc->valueint,
                 sm && cJSON_IsString(sm) ? sm->valuestring : "?");
        cJSON_Delete(jr);
        return false;
    }
    cJSON *data = cJSON_GetObjectItem(jr, "data");
    cJSON *hex = data ? cJSON_GetObjectItem(data, "audio") : NULL;
    bool ok = hex && cJSON_IsString(hex) && hex->valuestring;
    if (!ok) {
        snprintf(err, err_len, "no audio in response");
        cJSON_Delete(jr);
        return false;
    }

    /* hex -> mp3, decoded straight into the SPIFFS file in 2 KB chunks */
    const char *h = hex->valuestring;
    size_t hexlen = strlen(h);
    if (hexlen % 2) {
        snprintf(err, err_len, "odd hex length");
        cJSON_Delete(jr);
        return false;
    }
    File f = SPIFFS.open(spath, FILE_WRITE);
    if (!f) {
        snprintf(err, err_len, "cannot write %s", spath);
        cJSON_Delete(jr);
        return false;
    }
    size_t written = 0;
    uint8_t tmp[2048];
    for (size_t i = 0; i + 1 < hexlen; i += 2048 * 2) {
        size_t n = 0;
        for (; n < 2048 && i + 2 * n + 1 < hexlen; n++) {
            int hi = hex_val(h[i + 2 * n]);
            int lo = hex_val(h[i + 2 * n + 1]);
            if (hi < 0 || lo < 0) break;
            tmp[n] = (uint8_t)((hi << 4) | lo);
        }
        if (n == 0) break;
        written += f.write(tmp, n);
    }
    f.close();
    cJSON_Delete(jr);
    Serial.printf("[Minimax] tts: %u B mp3 -> %s\n", (unsigned)written,
                  spath);
    if (written == 0) {
        snprintf(err, err_len, "hex decode empty");
        return false;
    }
    return true;
}
