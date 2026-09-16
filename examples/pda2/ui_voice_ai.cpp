/**
 * @file      ui_voice_ai.cpp
 * @brief     Voice AI app: MiniMax speech_to_text (ASR) + AI Config chat
 *            provider + MiniMax t2a_v2 (TTS). Google-free per user
 *            request 2026-09-11 (Gemini + connecttospeech removed -
 *            unreachable in CN networks).
 *            Keys: AI Config minimax first, /env.cfg MINIMAX_AUDIO_KEY
 *            as fallback (minimax_audio.h).
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "pdm_recorder.h"
#include "minimax_audio.h"
#include "env_secrets.h"
#include "openai_api.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/queue.h>
#include <Preferences.h>
#include "Audio.h"
#include "utilities.h"
#include <driver/i2s.h>
#include <vector>
#include <utility>

static lv_obj_t *response_label = NULL;
static lv_obj_t *input_ta = NULL;
static lv_obj_t *status_label = NULL;
static TaskHandle_t ai_task = NULL;
static bool ai_kbd_active = false;
static bool tts_playing = false;
static volatile bool tts_auto_read = false;

/* TTS enable switch (user request 2026-09-15): OFF = skip the t2a_v2
 * synthesis + playback entirely (no earphone plugged - the jack has no
 * detect pin, so the USER tells the device). ASR is unaffected: the MIC
 * path works without earphones. Persisted in NVS "ai"/tts_enabled
 * (default on), same style as AI Config's Trust switch. */
static lv_obj_t *tts_sw = NULL;
static bool tts_enabled = true;

static bool tts_enabled_load(void)
{
    Preferences pr;
    if (pr.begin("ai", true)) {           /* read-only; absent -> default on */
        bool v = pr.getUChar("tts_enabled", 1) != 0;
        pr.end();
        return v;
    }
    return true;
}

static bool tts_enabled_save(bool on)
{
    Preferences pr;
    if (!pr.begin("ai", false)) return false;
    bool ok = pr.putUChar("tts_enabled", on ? 1 : 0) == 1;
    pr.end();
    return ok;
}

static void tts_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (tts_enabled_save(on)) {
        tts_enabled = on;
        if (status_label) {
            lv_label_set_text(status_label,
                              on ? "TTS on" : "TTS off (no earphone)");
        }
        Serial.printf("[VoiceAI] tts enabled=%d\n", on ? 1 : 0);
    } else {
        /* NVS failure: revert the control to the persisted state */
        if (tts_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
        if (status_label) lv_label_set_text(status_label, "TTS save failed");
    }
}

static char *chat_history = NULL;
static char *last_response = NULL;

/* Multi-turn context (feature request 2026-09-14, mirrors AI Text round
 * 21): whole (user, assistant) turn pairs, newest kept within a byte
 * budget, sent via openai_chat_multi. Only clean ASR/chat text lands
 * here, and only after a confirmed reply - the display-only chat_history
 * buffer and its "> [Voice recording]" markers never enter the API. A
 * failed turn records nothing (the error the user saw is not context).
 * Worker-task-owned: start_voice_record/do_send admit one ai_task at a
 * time and the UI thread never touches this. */
#define VAI_CTX_MSG_MAX     1024    /* per-message cap at append time */
#define VAI_CTX_TRUNC_MARK  "(truncated)"
#define VAI_CTX_BUDGET      4096    /* history bytes sent per request */
#define VAI_CTX_MAX_MSGS    16
static vector<pair<string, string> > vai_ctx;

extern Audio audio;

/* Thread-safe UI queue */
enum { UI_MSG_APPEND = 1, UI_MSG_STATUS = 2, UI_MSG_WAITBOX = 3 };
struct ui_msg_t { int type; char *text; };
static QueueHandle_t ui_queue = NULL;
static lv_timer_t *ui_timer = NULL;

/* Pagination for response display */
#define RESPONSE_PAGE_CHARS 400
static int response_page = 0;
static int response_total_pages = 1;

static void chat_append(const char *text)
{
    if (!text || !text[0]) return;
    if (chat_history) {
        size_t old_len = strlen(chat_history);
        size_t new_len = strlen(text);
        char *buf = (char *)heap_caps_realloc(chat_history, old_len + new_len + 3,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = (char *)realloc(chat_history, old_len + new_len + 3);
        if (buf) {
            buf[old_len] = '\n';
            memcpy(buf + old_len + 1, text, new_len + 1);
            chat_history = buf;
        }
    } else {
        size_t len = strlen(text);
        chat_history = (char *)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (chat_history) memcpy(chat_history, text, len + 1);
        else chat_history = strdup(text);
    }
    /* Show last page */
    if (chat_history) {
        size_t total = strlen(chat_history);
        response_total_pages = (total + RESPONSE_PAGE_CHARS - 1) / RESPONSE_PAGE_CHARS;
        if (response_total_pages < 1) response_total_pages = 1;
        response_page = response_total_pages - 1;

        /* Display current page */
        int start = response_page * RESPONSE_PAGE_CHARS;
        int len = total - start;
        if (len > RESPONSE_PAGE_CHARS) len = RESPONSE_PAGE_CHARS;
        static char page_buf[RESPONSE_PAGE_CHARS + 1];
        memcpy(page_buf, chat_history + start, len);
        page_buf[len] = '\0';
        lv_label_set_text(response_label, page_buf);
    }
}

static void show_response_page(int pg)
{
    if (!chat_history) return;
    size_t total = strlen(chat_history);
    response_total_pages = (total + RESPONSE_PAGE_CHARS - 1) / RESPONSE_PAGE_CHARS;
    if (pg < 0) pg = 0;
    if (pg >= response_total_pages) pg = response_total_pages - 1;
    response_page = pg;

    int start = pg * RESPONSE_PAGE_CHARS;
    int len = total - start;
    if (len > RESPONSE_PAGE_CHARS) len = RESPONSE_PAGE_CHARS;
    static char page_buf[RESPONSE_PAGE_CHARS + 1];
    memcpy(page_buf, chat_history + start, len);
    page_buf[len] = '\0';
    lv_label_set_text(response_label, page_buf);
    if (status_label)
        lv_label_set_text_fmt(status_label, "Page %d/%d  W:up S:down", pg + 1, response_total_pages);
}

static void chat_show_status(const char *s)
{
    if (status_label) lv_label_set_text(status_label, s);
}

static void ui_post(int type, const char *text)
{
    if (!ui_queue) return;
    ui_msg_t msg;
    msg.type = type;
    msg.text = strdup(text);
    if (xQueueSend(ui_queue, &msg, pdMS_TO_TICKS(2000)) != pdTRUE)
        free(msg.text);
}

static void start_tts();

/* ---- send-wait overlay (user request 2026-09-11, same pattern as the
 * AI Text app): pops on Send/Voice, countdown ticks on second changes
 * only (EPD-friendly), stays with "still waiting..." until the worker
 * task ends; ui_timer_cb hides it when ai_task clears. */
static lv_obj_t *vai_waitbox = NULL;
static lv_obj_t *vai_waitbox_body = NULL;
static uint32_t vai_wait_t0 = 0;
static uint32_t vai_wait_last = 99;

static void vai_waitbox_hide(void)
{
    if (vai_waitbox) {
        lv_obj_del(vai_waitbox);
        vai_waitbox = NULL;
        vai_waitbox_body = NULL;
    }
}

static void vai_waitbox_show(void)
{
    vai_waitbox_hide();
    vai_waitbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(vai_waitbox, 220, 110);
    lv_obj_align(vai_waitbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(vai_waitbox, lv_color_white(), 0);
    lv_obj_set_style_border_width(vai_waitbox, 1, 0);
    lv_obj_set_style_border_color(vai_waitbox, lv_color_black(), 0);
    lv_obj_set_style_radius(vai_waitbox, 6, 0);
    lv_obj_set_style_pad_all(vai_waitbox, 8, 0);
    lv_obj_clear_flag(vai_waitbox, LV_OBJ_FLAG_SCROLLABLE);

    vai_waitbox_body = lv_label_create(vai_waitbox);
    lv_obj_set_width(vai_waitbox_body, lv_pct(100));
    lv_label_set_long_mode(vai_waitbox_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(vai_waitbox_body, "Waiting server reply... 15s");
    lv_obj_set_style_text_font(vai_waitbox_body, &lv_font_montserrat_14, 0);
    lv_obj_center(vai_waitbox_body);

    vai_wait_t0 = millis();
    vai_wait_last = 99;
}

static void vai_waitbox_tick(void)
{
    if (!vai_waitbox || !vai_waitbox_body) return;
    int32_t remain = 15000 - (int32_t)(millis() - vai_wait_t0);
    if (remain <= 0) {
        if (vai_wait_last != 0) {
            vai_wait_last = 0;
            lv_label_set_text(vai_waitbox_body,
                              "still waiting...\n(voice: rec+ASR+chat+TTS)");
        }
        return;
    }
    uint32_t secs = ((uint32_t)remain + 999) / 1000;
    if (secs != vai_wait_last) {
        vai_wait_last = secs;
        char buf[48];
        snprintf(buf, sizeof(buf), "Waiting server reply... %lus",
                 (unsigned long)secs);
        lv_label_set_text(vai_waitbox_body, buf);
    }
}

static void ui_timer_cb(lv_timer_t *t)
{
    ui_msg_t msg;
    while (xQueueReceive(ui_queue, &msg, 0) == pdTRUE) {
        if (msg.type == UI_MSG_APPEND) chat_append(msg.text);
        else if (msg.type == UI_MSG_STATUS) chat_show_status(msg.text);
        else if (msg.type == UI_MSG_WAITBOX) vai_waitbox_show();
        free(msg.text);
    }

    if (ai_task) {
        vai_waitbox_tick();
    } else if (vai_waitbox) {
        vai_waitbox_hide();               /* worker finished */
    }

    if (tts_auto_read && ai_task == NULL) {
        tts_auto_read = false;
        start_tts();
    }

    if (tts_playing && !audio.isRunning()) {
        tts_playing = false;
        ui_disp_suppress_flush(false);   /* release the EPD held during play */
        if (status_label) lv_label_set_text(status_label, "V:voice R:read Enter:text");
    }
}

/* Chat endpoint resolution (user rule 2026-09-11): MiniMax DIRECT,
 * 1. AI Config's minimax provider entry (base/model/key) when its key is
 *    configured, else
 * 2. /env.cfg MINIMAX_AUDIO_KEY with the international chat defaults.
 * sk-api keys only work on api.minimax.io - the CN api.minimaxi.com
 * rejects them with 401 (PC-verified 2026-09-11); configure the base
 * accordingly when filling AI Config's minimax slot. */
static bool resolve_chat_cfg(char *base, int base_len, char *model,
                             int model_len, char *key, int key_len)
{
    char b[160], m[80], k[160];
    if (ai_provider_get("minimax", b, sizeof(b), m, sizeof(m), k, sizeof(k))
        && k[0]) {
        strncpy(base, b, base_len - 1);
        base[base_len - 1] = '\0';
        strncpy(model, m, model_len - 1);
        model[model_len - 1] = '\0';
        strncpy(key, k, key_len - 1);
        key[key_len - 1] = '\0';
        return true;
    }
    if (!env_get("MINIMAX_AUDIO_KEY", k, sizeof(k)) || !k[0]) return false;
    Serial.printf("[VoiceAI] chat key: env.cfg (len %d)\n", (int)strlen(k));
    strncpy(base, "https://api.minimax.io/v1", base_len - 1);
    base[base_len - 1] = '\0';
    strncpy(model, "MiniMax-M3", model_len - 1);
    model[model_len - 1] = '\0';
    strncpy(key, k, key_len - 1);
    key[key_len - 1] = '\0';
    return true;
}

/* Append one confirmed turn; per-message cut at a UTF-8 code point
 * boundary (AI Text chat_text_trunc pattern), eviction removes whole
 * turn pairs from the FRONT so the window never starts mid-turn. */
static void vai_ctx_add_pair(const char *user, const char *assistant)
{
    string u(user ? user : ""), a(assistant ? assistant : "");
    string *parts[2] = { &u, &a };
    size_t limit = VAI_CTX_MSG_MAX - strlen(VAI_CTX_TRUNC_MARK);
    for (int i = 0; i < 2; i++) {
        if (parts[i]->length() > limit) {
            parts[i]->resize(limit);
            while (!parts[i]->empty() &&
                   ((uint8_t)parts[i]->back() & 0xC0) == 0x80)
                parts[i]->pop_back();
            *parts[i] += VAI_CTX_TRUNC_MARK;
        }
    }
    vai_ctx.push_back(make_pair(u, a));
    while (vai_ctx.size() > 2) {
        size_t bytes = 0;
        for (size_t i = 0; i < vai_ctx.size(); i++)
            bytes += vai_ctx[i].second.length();
        if (bytes <= VAI_CTX_BUDGET && vai_ctx.size() <= VAI_CTX_MAX_MSGS)
            break;
        vai_ctx.erase(vai_ctx.begin(), vai_ctx.begin() + 2);
    }
}

/* Newest-first walk within VAI_CTX_BUDGET, chronological order for the
 * API. The ai_message_t entries point INTO vai_ctx strings, which stay
 * valid for the whole call (single worker task, no mutation mid-call). */
static int vai_ctx_build(ai_message_t *msgs, int max_msgs)
{
    size_t bytes = 0;
    size_t first = vai_ctx.size();
    while (first > 0 &&
           bytes + vai_ctx[first - 1].second.length() <= VAI_CTX_BUDGET) {
        bytes += vai_ctx[first - 1].second.length();
        first--;
    }
    /* Each stored turn is ONE pair (user_text, assistant_text) but the API
     * needs TWO messages per turn: {role:"user", content:user_text} then
     * {role:"assistant", content:assistant_text}. v4/v5 assigned
     * pair.first as the ROLE - an invalid role that broke multi-turn from
     * the second exchange on (Claude/Grok session-batch P2-1). Budget is
     * counted in whole turn-pairs (VAI_CTX_BUDGET), so the 2x expansion
     * cannot exceed max_msgs when callers pass 2 * stored turns. */
    int n = 0;
    for (size_t i = first; i < vai_ctx.size(); i++) {
        if (n + 2 > max_msgs) break;
        msgs[n].role = "user";
        msgs[n].content = vai_ctx[i].first.c_str();
        n++;
        msgs[n].role = "assistant";
        msgs[n].content = vai_ctx[i].second.c_str();
        n++;
    }
    return n;
}

static void ai_text_task(void *param)
{
    char *prompt = (char *)param;
    char base[160], model[80], key[160];
    if (!resolve_chat_cfg(base, sizeof(base), model, sizeof(model),
                          key, sizeof(key))) {
        ui_post(UI_MSG_APPEND, "No AI provider configured (AI Cfg)");
        ui_post(UI_MSG_STATUS, "V:voice Enter:text");
        free(prompt);
        ai_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    Serial.printf("[VoiceAI] prompt: %s -> %s\n", prompt, model);
    ui_post(UI_MSG_STATUS, "Asking AI...");

    ai_message_t ctx[VAI_CTX_MAX_MSGS];
    int ctx_n = vai_ctx_build(ctx, VAI_CTX_MAX_MSGS);
    string reply;
    if (openai_chat_multi(ctx, ctx_n, prompt, base, model, key, reply,
                          30000)) {
        if (last_response) free(last_response);
        last_response = strdup(reply.c_str());
        vai_ctx_add_pair(prompt, reply.c_str());
        ui_post(UI_MSG_APPEND, reply.c_str());
        ui_post(UI_MSG_STATUS, "V:voice R:read Enter:text");
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Error: %.200s", reply.c_str());
        ui_post(UI_MSG_APPEND, buf);
        ui_post(UI_MSG_STATUS, "V:voice Enter:text");
    }
    free(prompt);
    ai_task = NULL;
    vTaskDelete(NULL);
}

static bool mic_released(uint32_t elapsed_ms)
{
    (void)elapsed_ms;
    return !keypad_mic_held();
}

/* param: NULL = fixed 5 s take (V key); non-NULL = hold-to-talk on the
 * MIC key - record while held (max 10 s), stop 700 ms minimum. */
static void ai_voice_task(void *param)
{
    const bool hold = (param != NULL);
    char akey[160];
    if (!minimax_audio_key(akey, sizeof(akey))) {
        ui_post(UI_MSG_APPEND,
                "No MiniMax key (AI Cfg minimax or env MINIMAX_AUDIO_KEY)");
        ai_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ui_post(UI_MSG_STATUS, hold ? "Recording... release MIC to send"
                                : "Recording 5 sec...");
    ui_post(UI_MSG_APPEND, "> [Voice recording]");

    uint8_t *wav = NULL;
    size_t wav_len = 0;
    bool ok = hold
        ? pdm_record_wav_hold(10, 16000, &wav, &wav_len, mic_released)
        : pdm_record_wav(5, 16000, &wav, &wav_len);
    pdm_restore_audio();
    /* network stages from here on - this is where the wait overlay
     * belongs (user feedback 2026-09-11: it must not cover recording) */
    ui_post(UI_MSG_WAITBOX, "");

    char text[256] = "";
    if (ok && wav && wav_len > 0) {
        ui_post(UI_MSG_STATUS, "ASR (minimax)...");
        char err[128];
        ok = minimax_asr(wav, wav_len, akey, text, sizeof(text),
                         err, sizeof(err));
        free(wav);
        if (!ok) {
            char buf[192];
            snprintf(buf, sizeof(buf), "ASR failed: %.150s", err);
            ui_post(UI_MSG_APPEND, buf);
            ui_post(UI_MSG_STATUS, "V:voice Enter:text");
            ai_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        char shown[270];
        snprintf(shown, sizeof(shown), "> %s", text);
        ui_post(UI_MSG_APPEND, shown);
    } else {
        if (wav) free(wav);
        ui_post(UI_MSG_APPEND, "Recording failed");
        ui_post(UI_MSG_STATUS, "V:voice Enter:text");
        ai_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    char base[160], model[80], ckey[160];
    if (!resolve_chat_cfg(base, sizeof(base), model, sizeof(model),
                          ckey, sizeof(ckey))) {
        ui_post(UI_MSG_APPEND, "No AI provider configured (AI Cfg)");
        ui_post(UI_MSG_STATUS, "V:voice Enter:text");
        ai_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ui_post(UI_MSG_STATUS, "Asking AI...");
    ai_message_t ctx[VAI_CTX_MAX_MSGS];
    int ctx_n = vai_ctx_build(ctx, VAI_CTX_MAX_MSGS);
    string reply;
    if (openai_chat_multi(ctx, ctx_n, text, base, model, ckey, reply,
                          30000)) {
        if (last_response) free(last_response);
        last_response = strdup(reply.c_str());
        vai_ctx_add_pair(text, reply.c_str());
        ui_post(UI_MSG_APPEND, reply.c_str());
        tts_auto_read = true;
    } else {
        char buf[256];
        snprintf(buf, sizeof(buf), "Error: %.200s", reply.c_str());
        ui_post(UI_MSG_APPEND, buf);
    }
    ui_post(UI_MSG_STATUS, "V:voice R:read Enter:text");
    ai_task = NULL;
    vTaskDelete(NULL);
}

static void start_voice_record(bool hold)
{
    if (ai_task) return;
    if (WiFi.status() != WL_CONNECTED) {
        chat_append("WiFi not connected.");
        return;
    }
    if (!hold) vai_waitbox_show();   /* hold-to-talk: waitbox only after
                                      * the take, posted by the task */
    xTaskCreatePinnedToCore(ai_voice_task, "ai_voice", 16384,
                            hold ? (void *)1 : NULL, 5, &ai_task, 0);
}

static void ensure_audio_init()
{
    Serial.println("[VoiceAI] Re-initializing audio...");

    /* The Audio lib installs its I2S_NUM_0 TX driver ONCE in its ctor and
     * never reinstalls it; PDM recording needs the same port and kills it.
     * Rebuild field-for-field like the ctor (16000 Hz, RIGHT_LEFT,
     * tx_desc_auto_clear=true, APLL off) - the previous hand-rolled config
     * (44100, no auto-clear) decoded fine but stayed SILENT. Recipe is
     * device-proven on the audio-variant board 2026-09-10, issue_list
     * §16.1 rule 2. */
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    Serial.printf("[VoiceAI] i2s_driver_install: %s\n", esp_err_to_name(err));

    i2s_pin_config_t pins = {};
    pins.bck_io_num = BOARD_I2S_BCLK;
    pins.ws_io_num = BOARD_I2S_LRC;
    pins.data_out_num = BOARD_I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    err = i2s_set_pin(I2S_NUM_0, &pins);
    Serial.printf("[VoiceAI] i2s_set_pin: %s\n", esp_err_to_name(err));

    /* Now Audio.setPinout is safe — driver is installed */
    audio.setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT);
    audio.setVolume(21);
    Serial.println("[VoiceAI] Audio re-initialized");
}

static void start_tts()
{
    Serial.println("[VoiceAI] TTS: start_tts called");

    if (!tts_enabled) {
        /* switch OFF: no earphone plugged - skip synthesis AND playback
         * (ASR/voice input is unaffected: the mic works standalone) */
        Serial.println("[VoiceAI] TTS: disabled by switch");
        if (status_label) lv_label_set_text(status_label, "TTS off (no earphone)");
        return;
    }
    if (!last_response || last_response[0] == '\0') {
        Serial.println("[VoiceAI] TTS: no response to read");
        if (status_label) lv_label_set_text(status_label, "No response to read");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[VoiceAI] TTS: no WiFi");
        if (status_label) lv_label_set_text(status_label, "WiFi needed for TTS");
        return;
    }

    char key[160];
    if (!minimax_audio_key(key, sizeof(key))) {
        if (status_label)
            lv_label_set_text(status_label,
                              "No MiniMax key (AI Cfg / env)");
        return;
    }

    if (status_label) lv_label_set_text(status_label, "Synthesizing...");
    Serial.printf("[VoiceAI] TTS: minimax t2a, %d chars\n",
                  (int)strlen(last_response));

    char err[128];
    if (!minimax_tts(last_response, key, "/tts.mp3", err, sizeof(err))) {
        Serial.printf("[VoiceAI] TTS failed: %s\n", err);
        char buf[160];
        snprintf(buf, sizeof(buf), "TTS failed: %.120s", err);
        if (status_label) lv_label_set_text(status_label, buf);
        return;
    }

    /* PDM recording killed the Audio lib's I2S0 driver - rebuild it with
     * the ctor-replica recipe before the first playback (16.1 rule 2) */
    ensure_audio_init();

    if (status_label) lv_label_set_text(status_label, "Reading aloud...");
    bool ok = audio.connecttoFS(SPIFFS, "/tts.mp3");
    Serial.printf("[VoiceAI] TTS: play /tts.mp3 = %d\n", ok ? 1 : 0);
    tts_playing = ok;
    /* any EPD flush while playing blocks audio.loop() for hundreds of ms
     * and the take plays as choppy noise (same device report) - hold all
     * flushes until the playback-end check in ui_timer_cb releases them */
    if (tts_playing) ui_disp_suppress_flush(true);
}

static void do_send()
{
    if (!input_ta || ai_task) return;
    const char *text = lv_textarea_get_text(input_ta);
    if (!text || text[0] == '\0') return;

    if (WiFi.status() != WL_CONNECTED) {
        chat_append("WiFi not connected.");
        return;
    }

    /* Show user message */
    char q[270];
    snprintf(q, sizeof(q), "> %s", text);
    chat_append(q);

    char *prompt = strdup(text);
    lv_textarea_set_text(input_ta, "");
    if (prompt) {
        vai_waitbox_show();
        xTaskCreatePinnedToCore(ai_text_task, "ai_text", 16384, prompt, 5, &ai_task, 0);
    }
}

/* Keyboard */
void voiceai_keyboard_poll()
{
    if (!ai_kbd_active || !input_ta) return;
    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\n') {
        do_send();
    } else if (c == 'r' && ai_task == NULL) {
        const char *text = lv_textarea_get_text(input_ta);
        if (!text || text[0] == '\0') {
            start_tts();
            return;
        }
        lv_textarea_add_char(input_ta, c);
        return;
    } else if (c == '\f') {
        /* dedicated MIC-key code (keymap (3,6), like '\v' for volume):
         * hold-to-talk - record while held, release to send */
        if (ai_task == NULL) start_voice_record(true);
    } else if ((c == 'v' || c == 'V') && ai_task == NULL) {
        /* V (either case) also starts a fixed 5 s take when empty */
        const char *text = lv_textarea_get_text(input_ta);
        if (!text || text[0] == '\0') {
            start_voice_record(false);
            return;
        }
        if (c == 'v') lv_textarea_add_char(input_ta, c);
        return;
    } else if (c == '\b') {
        const char *text = lv_textarea_get_text(input_ta);
        if (!text || text[0] == '\0') {
            ai_kbd_active = false;
            scr_mgr_pop(false);
        } else {
            lv_textarea_del_char(input_ta);
        }
    } else if (c >= ' ' && c <= '~') {
        lv_textarea_add_char(input_ta, c);
    }
}

static void ai_back_cb(lv_event_t *e)
{
    ai_kbd_active = false;
    scr_mgr_pop(false);
}

static void ai_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Voice AI", ai_back_cb);

    /* TTS switch, AI Config "Trust" switch pattern: label + lv_switch in
     * the top row, persists immediately (independent NVS key) */
    lv_obj_t *tts_lab = lv_label_create(parent);
    lv_label_set_text(tts_lab, "TTS");
    lv_obj_set_style_text_font(tts_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(tts_lab, LV_ALIGN_TOP_RIGHT, -50, 9);
    tts_sw = lv_switch_create(parent);
    lv_obj_set_size(tts_sw, 44, 24);
    lv_obj_align(tts_sw, LV_ALIGN_TOP_RIGHT, -2, 4);
    /* EPD state visibility (user request 2026-09-15, take 2): the style
     * must target the CHECKED selector - a default-state style is overridden
     * by the theme's checked style (that is why take 1 changed nothing).
     * ON  -> track (LV_PART_INDICATOR | CHECKED) black: the area the knob
     *        left behind reads DARK, white knob with border slides right.
     * OFF -> plain white track with a black border, black-bordered knob. */
    lv_obj_set_style_bg_color(tts_sw, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(tts_sw, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(tts_sw, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tts_sw, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(tts_sw, lv_color_black(),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tts_sw, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_border_color(tts_sw, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_border_width(tts_sw, 1, LV_PART_KNOB);
    tts_enabled = tts_enabled_load();
    if (tts_enabled) {
        lv_obj_add_state(tts_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(tts_sw, tts_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ui_queue = xQueueCreate(8, sizeof(ui_msg_t));
    ui_timer = lv_timer_create(ui_timer_cb, 200, NULL);

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 230, 270);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Response area (top, most space) */
    response_label = lv_label_create(cont);
    lv_obj_set_width(response_label, lv_pct(100));
    lv_obj_set_flex_grow(response_label, 1);
    lv_obj_set_style_text_font(response_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(response_label, LV_LABEL_LONG_WRAP);

    lv_label_set_text(response_label,
                      "Enter: send text\nV: voice (5s record)\n"
                      "R: read last response");

    /* Status line */
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(status_label, "");

    /* Input at bottom */
    input_ta = lv_textarea_create(cont);
    lv_obj_set_width(input_ta, lv_pct(100));
    lv_obj_set_height(input_ta, 36);
    lv_textarea_set_placeholder_text(input_ta, "Ask anything...");
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_max_length(input_ta, 256);
    /* no cursor blink: each blink redraws -> a blocking EPD partial flush
     * every ~400 ms starves audio.loop() during TTS playback (device
     * report 2026-09-11: choppy noise in-app, clean audio after exit).
     * Blink time lives on the LV_PART_CURSOR style anim_time (0 = off). */
    lv_obj_set_style_anim_time(input_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_font(input_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    response_page = 0;
    response_total_pages = 1;
    ai_kbd_active = true;
}

static void ai_entry(void) { ui_disp_full_refr(); }
static void ai_exit(void)
{
    /* playback continues after exit (global audio object) - the timer
     * that releases the flush suppression dies with the screen, so do
     * it here or the EPD stays frozen app-wide */
    ui_disp_suppress_flush(false);
    ui_disp_full_refr();
}
static void ai_destroy(void)
{
    ai_kbd_active = false;
    if (ai_task) { vTaskDelete(ai_task); ai_task = NULL; }
    if (ui_timer) { lv_timer_del(ui_timer); ui_timer = NULL; }
    if (ui_queue) {
        ui_msg_t msg;
        while (xQueueReceive(ui_queue, &msg, 0) == pdTRUE) free(msg.text);
        vQueueDelete(ui_queue); ui_queue = NULL;
    }
    if (chat_history) { free(chat_history); chat_history = NULL; }
    if (last_response) { free(last_response); last_response = NULL; }
    vai_ctx.clear();
    tts_playing = false;
    tts_auto_read = false;
    response_label = input_ta = status_label = NULL;
    tts_sw = NULL;
}

scr_lifecycle_t screen_voice_ai = {
    .create = ai_create,
    .entry = ai_entry,
    .exit = ai_exit,
    .destroy = ai_destroy,
};
