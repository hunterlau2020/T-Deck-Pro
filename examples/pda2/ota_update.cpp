/**
 * @file      ota_update.cpp
 * @brief     Signed OTA implementation - see ota_update.h / design v6.
 */
#include "Arduino.h"
#include "ota_update.h"
#include "ota_trust_anchor.h"
#include "http_utils.h"
#include "env_secrets.h"
#include "ui_deckpro_port.h"            /* battery getters (precheck) */

#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <cJSON.h>
#include <ctype.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <freertos/queue.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

#define OTA_MANIFEST_TIMEOUT_MS 20000
#define OTA_IDLE_TIMEOUT_MS     45000     /* read-idle during download */
#define OTA_DEADLINE_MS         (10UL * 60UL * 1000UL)
#define OTA_TASK_STACK          (1024 * 16)   /* design §6 exception: 16KB */
#define OTA_BATT_MIN_PERCENT    30

/* ---- single-flight + hw lock (design §3.3) ------------------------------ */
static volatile int s_ota_inflight = 0;
static volatile bool s_ota_hw_lock = false;      /* flash in use */
static volatile bool s_ota_cancel = false;       /* token, chunk edges */
static volatile int s_ota_progress = 0;
static uint32_t s_ota_start_ms = 0;              /* safety-valve window */

/* ---- pointer result channel (design §3.1) ------------------------------- */
static QueueHandle_t s_ota_q = NULL;             /* depth 1, POINTERS */
static void (*s_consumer)(ota_result_t *) = NULL;

void ota_set_consumer(void (*cb)(ota_result_t *)) { s_consumer = cb; }

bool ota_busy(void)
{
    return s_ota_inflight > 0 || s_ota_hw_lock;
}

bool ota_shutdown_blocked(void)
{
    /* suppress shutdown only inside ONE deadline window (design §4) */
    return ota_busy() && (millis() - s_ota_start_ms) < OTA_DEADLINE_MS;
}

void ota_request_cancel(void) { s_ota_cancel = true; }

int ota_progress_percent(void) { return s_ota_progress; }

const char *ota_current_version(void) { return OTA_APP_VERSION; }

/* ---- helpers ------------------------------------------------------------- */

static bool ota_url_scheme_ok(const char *url, char *err, int err_len)
{
    const bool https = (strncmp(url, "https://", 8) == 0);
    const bool http = (strncmp(url, "http://", 7) == 0);
    if (https) return true;
#ifdef OTA_ALLOW_PLAIN_HTTP
    if (http) return true;
#endif
    (void)http;
    snprintf(err, err_len, "URL must be https://");
    return false;
}

/* Six-field canonical byte string (design §2.1 pinned encoding; the
 * publisher builds the identical bytes in scripts/ota_sign.py). */
static string ota_signed_bytes(const ota_manifest_t *m)
{
    char size_dec[24], seq_dec[16], sha_hex[65];
    snprintf(size_dec, sizeof(size_dec), "%llu",
             (unsigned long long)m->size);
    snprintf(seq_dec, sizeof(seq_dec), "%lu", (unsigned long)m->seq);
    for (int i = 0; i < 32; i++)
        snprintf(sha_hex + 2 * i, 3, "%02x", m->sha256[i]);
    string b;
    b.reserve(200 + m->version.length() + m->url.length() + m->notes.length());
    b += m->version; b += '\n';
    b += m->url;     b += '\n';
    b += size_dec;   b += '\n';
    b += sha_hex;    b += '\n';
    b += seq_dec;    b += '\n';
    b += m->notes;   b += '\n';
    return b;
}

/* ECDSA P-256 verify over the canonical bytes with the flash-resident
 * trust anchor. r/s from the 64-byte P1363 blob via mbedtls_mpi (design
 * §2.1 - read_signature is forbidden). */
static bool ota_verify_signature(const ota_manifest_t *m)
{
    mbedtls_mpi r, s;
    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    unsigned char hash[32];
    size_t hlen = 0;
    bool ok = false;

    mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);

    do {
        if (mbedtls_mpi_read_binary(&r, m->sig, 32) != 0) break;
        if (mbedtls_mpi_read_binary(&s, m->sig + 32, 32) != 0) break;
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) break;
        if (mbedtls_ecp_point_read_binary(
                &grp, &Q, OTA_TRUST_ANCHOR_SEC1, 65) != 0) break;

        const string msg = ota_signed_bytes(m);
        const mbedtls_md_info_t *md = mbedtls_md_info_from_type(
            MBEDTLS_MD_SHA256);
        if (!md) break;
        if (mbedtls_md(md, (const unsigned char *)msg.c_str(),
                       msg.length(), hash) != 0) break;
        hlen = mbedtls_md_get_size(md);
        if (mbedtls_ecdsa_verify(&grp, hash, hlen, &Q, &r, &s) != 0) {
            Serial.println("[OTA] signature INVALID");
            break;
        }
        ok = true;
    } while (0);

    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s);
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    return ok;
}

/* Parse the manifest JSON into m (fields + sig). Encoding is validated
 * against the §2.1 pins (decimal digits, lowercase hex, no newlines in
 * notes) so the canonical rebuild cannot drift from the publisher. */
static bool ota_parse_manifest(const char *body, ota_manifest_t *m,
                               string *err)
{
    cJSON *root = cJSON_Parse(body);
    if (!root) { *err = "bad JSON (manifest)"; return false; }
    const char *version = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "version"));
    const char *url = cJSON_GetStringValue(cJSON_GetObjectItem(root, "url"));
    const char *size_s = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "size"));
    const char *sha_s = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "sha256"));
    const char *seq_s = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "seq"));
    const char *notes = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "notes"));
    const char *sig_b64 = cJSON_GetStringValue(
        cJSON_GetObjectItem(root, "sig"));
    if (!version || !url || !size_s || !sha_s || !seq_s || !notes ||
        !sig_b64 || !version[0] || !url[0] || !size_s[0] || !sha_s[0] ||
        !seq_s[0] || !sig_b64[0]) {
        cJSON_Delete(root);
        *err = "manifest field missing";
        return false;
    }
    m->version = version;
    m->url = url;
    m->notes = notes;
    if (strchr(notes, '\n') || strchr(notes, '\r')) {
        cJSON_Delete(root);
        *err = "notes contains newline";
        return false;
    }
    /* decimal, unsigned, no leading zeros (§2.1) */
    for (const char *p = size_s; *p; p++) {
        if (!isdigit((int)*p) || (p == size_s && *p == '0')) {
            cJSON_Delete(root);
            *err = "bad size encoding";
            return false;
        }
    }
    for (const char *p = seq_s; *p; p++) {
        if (!isdigit((int)*p) || (p == seq_s && *p == '0')) {
            cJSON_Delete(root);
            *err = "bad seq encoding";
            return false;
        }
    }
    m->size = strtoull(size_s, NULL, 10);
    m->seq = (uint32_t)strtoul(seq_s, NULL, 10);
    if (strlen(sha_s) != 64) {
        cJSON_Delete(root);
        *err = "sha256 must be 64 hex";
        return false;
    }
    for (int i = 0; i < 64; i++) {
        char c = sha_s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            cJSON_Delete(root);
            *err = "sha256 must be lowercase hex";
            return false;
        }
    }
    for (int i = 0; i < 32; i++) {
        char pair[3] = { sha_s[2 * i], sha_s[2 * i + 1], 0 };
        m->sha256[i] = (uint8_t)strtoul(pair, NULL, 16);
    }
    /* base64 -> 64 raw bytes */
    {
        static const char *b64 =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        uint8_t out[64];
        int oi = 0;
        int acc = 0, nbits = 0;
        for (const char *p = sig_b64; *p && oi < 64; p++) {
            const char *q = strchr(b64, *p);
            if (!q || q - b64 >= 64) { if (*p == '=') continue; else break; }
            acc = (acc << 6) | (int)(q - b64);
            nbits += 6;
            if (nbits >= 8) {
                nbits -= 8;
                out[oi++] = (uint8_t)((acc >> nbits) & 0xFF);
            }
        }
        if (oi != 64) {
            cJSON_Delete(root);
            *err = "sig must decode to 64 bytes";
            return false;
        }
        memcpy(m->sig, out, 64);
    }
    cJSON_Delete(root);
    return true;
}

static uint32_t ota_last_seq_load(void)
{
    Preferences pr;
    if (pr.begin("ota", true)) {
        uint32_t v = pr.getULong("last_seq", 0);
        pr.end();
        return v;
    }
    return 0;
}

static void ota_last_seq_save(uint32_t seq)
{
    Preferences pr;
    if (pr.begin("ota", false)) {
        pr.putULong("last_seq", seq);
        pr.end();
    }
}

/* ---- shared task exit path (design §3.3: SINGLE exit, paired release) --- */
static void ota_task_exit(void)
{
    s_ota_hw_lock = false;
    s_ota_inflight = 0;
    WiFi.setSleep(true);                 /* paired restore (design §4) */
    vTaskDelete(NULL);
}

static void ota_send_result(ota_result_t *r)
{
    if (s_ota_q) xQueueOverwrite(s_ota_q, &r);   /* never blocks (§3.3) */
    else delete r;
}

/* ---- CHECK worker --------------------------------------------------------- */

static void ota_check_task(void *param)
{
    ota_result_t *r = new ota_result_t;
    r->gen = (uint32_t)(uintptr_t)param;
    r->kind = OTA_RESULT_CHECK;
    r->ok = false;
    r->has_update = false;

    char url[160];
    if (!env_get("OTA_URL", url, sizeof(url)) || !url[0]) {
        r->err = "OTA_URL not set (/env.cfg)";
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    char e[96];
    if (!ota_url_scheme_ok(url, e, sizeof(e))) {
        r->err = e;
        ota_send_result(r);
        ota_task_exit();
        return;
    }

    if (strncmp(url, "https://", 8) == 0 &&
        http_get_tls_mode() != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        r->err = "time not synced (NTP)";
        ota_send_result(r);
        ota_task_exit();
        return;
    }

    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    http.setTimeout(OTA_MANIFEST_TIMEOUT_MS);
    http.setReuse(false);
    bool begun = (strncmp(url, "https://", 8) == 0)
        ? (http_apply_tls(secure), http.begin(secure, url))
        : http.begin(plain, url);
    if (!begun) {
        r->err = "connect failed (manifest)";
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    int code = http.GET();
    if (code != 200) {
        r->err = string("HTTP ") + to_string(code) + " (manifest)";
        http.end();
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    String body = http.getString();
    http.end();

    ota_manifest_t m;
    if (!ota_parse_manifest(body.c_str(), &m, &r->err)) {
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    /* dual scheme check: the SIGNED url too (design §2.1) */
    if (!ota_url_scheme_ok(m.url.c_str(), e, sizeof(e))) {
        r->err = string("signed url rejected: ") + e;
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    if (!ota_verify_signature(&m)) {
        r->err = "signature verify FAILED";
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    uint32_t last = ota_last_seq_load();
    if (m.seq <= last) {
        r->ok = true;                    /* signed fine, already installed */
        r->err = "already up to date (seq " + to_string(last) + ")";
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    r->ok = true;
    r->has_update = true;
    r->manifest = m;
    ota_send_result(r);
    ota_task_exit();
}

/* ---- UPDATE worker -------------------------------------------------------- */

static void ota_update_task(void *param)
{
    /* snap ownership moved here (launch-time hand-off, design §3.2) */
    struct snap_t {
        uint32_t gen;
        ota_manifest_t m;
    } *snap = (snap_t *)param;

    ota_result_t *r = new ota_result_t;
    r->gen = snap->gen;
    r->kind = OTA_RESULT_UPDATE;
    r->ok = false;

    const ota_manifest_t &m = snap->m;

    WiFi.setSleep(false);                /* paired restore in ota_task_exit */

    if (strncmp(m.url.c_str(), "https://", 8) == 0 &&
        http_get_tls_mode() != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        r->err = "time not synced (NTP)";
        delete snap;
        ota_send_result(r);
        ota_task_exit();
        return;
    }

    /* size vs the free OTA slot (design §10: size > slot refused) */
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(running);
    if (!next) {
        r->err = "no OTA partition";
        delete snap;
        ota_send_result(r);
        ota_task_exit();
        return;
    }
    if (m.size > (uint64_t)next->size) {
        r->err = "image " + to_string(m.size) + "B > slot " +
                 to_string(next->size) + "B";
        delete snap;
        ota_send_result(r);
        ota_task_exit();
        return;
    }

    s_ota_hw_lock = true;                /* flash writes from here */

    WiFiClientSecure secure;
    WiFiClient plain;
    HTTPClient http;
    http.setTimeout(OTA_IDLE_TIMEOUT_MS);
    http.setReuse(false);
    bool begun = (strncmp(m.url.c_str(), "https://", 8) == 0)
        ? (http_apply_tls(secure), http.begin(secure, m.url.c_str()))
        : http.begin(plain, m.url.c_str());
    if (!begun) {
        r->err = "connect failed (firmware)";
        goto fail;
    }
    {
        int code = http.GET();
        if (code != 200) {
            r->err = string("HTTP ") + to_string(code) + " (firmware)";
            http.end();
            goto fail;
        }
        int clen = http.getSize();       /* -1 = chunked: FORBIDDEN (§10) */
        if (clen <= 0 || (uint64_t)clen != m.size) {
            r->err = "Content-Length missing/mismatch";
            http.end();
            goto fail;
        }

        if (!Update.begin((size_t)m.size, U_FLASH)) {
            r->err = string("Update.begin: ") + Update.errorString();
            http.end();
            goto fail;
        }
        mbedtls_sha256_context sha;
        mbedtls_sha256_init(&sha);
        mbedtls_sha256_starts(&sha, 0);

        Stream *s = http.getStreamPtr();
        uint8_t buf[2048];
        uint64_t got = 0;
        uint32_t idle0 = millis();
        const uint32_t deadline = millis() + OTA_DEADLINE_MS;
        bool io_err = false;
        while (got < m.size) {
            if (s_ota_cancel) {
                r->err = "cancelled";
                io_err = true;
                break;
            }
            if ((int32_t)(millis() - deadline) >= 0) {
                r->err = "absolute deadline (10 min)";
                io_err = true;
                break;
            }
            int avail = s->available();
            if (avail <= 0) {
                if (millis() - idle0 > OTA_IDLE_TIMEOUT_MS) {
                    r->err = "read idle timeout (45 s)";
                    io_err = true;
                    break;
                }
                delay(2);
                continue;
            }
            size_t want = m.size - got;
            if (want > sizeof(buf)) want = sizeof(buf);
            if ((size_t)avail < want) want = (size_t)avail;
            size_t n = s->readBytes(buf, want);
            if (n == 0) {
                r->err = "read returned 0";
                io_err = true;
                break;
            }
            if (Update.write(buf, n) != n) {
                r->err = string("flash write: ") + Update.errorString();
                io_err = true;
                break;
            }
            mbedtls_sha256_update(&sha, buf, n);
            got += n;
            idle0 = millis();
            s_ota_progress = (int)((got * 100) / m.size);
        }
        http.end();

        uint8_t digest[32];
        mbedtls_sha256_finish(&sha, digest);
        mbedtls_sha256_free(&sha);

        if (io_err) goto fail_abort;
        if (memcmp(digest, m.sha256, 32) != 0) {
            r->err = "sha256 mismatch after download";
            goto fail_abort;
        }
        if (!Update.end(true)) {         /* true = set BOOT_VALID flag */
            r->err = string("Update.end: ") + Update.errorString();
            goto fail;
        }
        /* end(true) succeeded -> otadata flipped; NOW persist last_seq
         * (design §2.1: write only after end) */
        ota_last_seq_save(m.seq);
        r->ok = true;
        Serial.printf("[OTA] flashed %llu B seq %lu %s -> reboot pending\n",
                      (unsigned long long)m.size, (unsigned long)m.seq,
                      m.version.c_str());
        delete snap;
        ota_send_result(r);
        ota_task_exit();
        return;
    }

fail_abort:
    Update.abort();                      /* never end() on failure (§10) */
fail:
    delete snap;
    ota_send_result(r);
    ota_task_exit();
}

/* ---- launchers (UI thread) ------------------------------------------------ */

bool ota_check_async(uint32_t gen)
{
    if (s_ota_inflight > 0) {
        Serial.println("[OTA] check refused: previous op closing");
        return false;
    }
    if (!s_ota_q) s_ota_q = xQueueCreate(1, sizeof(ota_result_t *));
    if (!s_ota_q) return false;
    s_ota_cancel = false;
    s_ota_progress = 0;
    s_ota_start_ms = millis();
    if (xTaskCreate(ota_check_task, "ota_chk", OTA_TASK_STACK,
                    (void *)(uintptr_t)gen, 1, NULL) != pdPASS) {
        return false;                    /* not launched: no inflight bump */
    }
    s_ota_inflight = 1;                  /* AFTER successful create (§3.3) */
    return true;
}

bool ota_start_async(uint32_t gen, ota_manifest_t *snap)
{
    if (!snap) return false;
    if (s_ota_inflight > 0) {
        delete snap;                     /* refused: caller must not reuse */
        return false;
    }
    if (!s_ota_q) s_ota_q = xQueueCreate(1, sizeof(ota_result_t *));
    if (!s_ota_q) { delete snap; return false; }
    struct snap_t {
        uint32_t gen;
        ota_manifest_t m;
    } *s = new snap_t();
    s->gen = gen;
    s->m = *snap;                        /* deep copy into task-owned heap */
    delete snap;                         /* ownership hand-off complete */
    s_ota_cancel = false;
    s_ota_progress = 0;
    s_ota_start_ms = millis();
    if (xTaskCreate(ota_update_task, "ota_upd", OTA_TASK_STACK,
                    s, 1, NULL) != pdPASS) {
        delete s;
        return false;
    }
    s_ota_inflight = 1;
    return true;
}

void ota_result_poll(void)
{
    if (!s_ota_q) return;
    ota_result_t *r = NULL;
    if (xQueueReceive(s_ota_q, &r, 0) == pdTRUE && r) {
        if (s_consumer) s_consumer(r);   /* consumer deletes exactly once */
        else delete r;                   /* no screen: discard + delete */
    }
}

bool ota_precheck_ui(char *err, int err_len)
{
    char url[160];
    if (!env_get("OTA_URL", url, sizeof(url)) || !url[0]) {
        snprintf(err, err_len, "OTA_URL not set (/env.cfg)");
        return false;
    }
    if (!ota_url_scheme_ok(url, err, err_len)) return false;
    /* battery: >=30% or on external power; invalid gauge -> input pin
     * (design §4). No-network checks only - WiFi is tested in the worker. */
    if (!ui_battery_is_external_power_present()) {
        if (ui_battery_27220_is_vaild()) {
            uint16_t pct = ui_battery_27220_get_percent();
            if (pct < OTA_BATT_MIN_PERCENT) {
                snprintf(err, err_len, "battery %u%% < 30%% - charge first",
                         (unsigned)pct);
                return false;
            }
        } else if (!ui_battery_27220_get_input()) {
            snprintf(err, err_len, "battery gauge invalid - charge first");
            return false;
        }
    }
    return true;
}
