/**
 * @file      penpal_api.cpp
 * @brief     Pen-pal service API client (design: docs/penpal-design.md v3.1).
 *            Contract reference: scripts/remote_api_demo.py (live-tested).
 */
#include "Arduino.h"
#include "penpal_api.h"
#include "http_utils.h"
#include "env_secrets.h"
#include "config_keys.h"
#include <Preferences.h>
#include <cJSON.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_random.h>

static const char *PP_TAG = "[PenPal]";

/* ---- defensive-parse helpers (design R1) ---------------------------------- */

static const char *j_str(cJSON *obj, const char *field)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, field) : NULL;
    return (it && cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

static int j_int(cJSON *obj, const char *field, int def)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, field) : NULL;
    return (it && cJSON_IsNumber(it)) ? it->valueint : def;
}

static bool j_bool(cJSON *obj, const char *field, bool def)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, field) : NULL;
    return (it && cJSON_IsBool(it)) ? cJSON_IsTrue(it) : def;
}

/* sender_user_id: null for NPC letters, >=1 for mine (demo step ④). */
static bool j_mine(cJSON *obj)
{
    cJSON *it = obj ? cJSON_GetObjectItem(obj, "sender_user_id") : NULL;
    return (it && cJSON_IsNumber(it)) ? (it->valueint != 0) : false;
}

/* NULL-safe fixed-buffer copy. */
static void s_copy(char *dst, int dstlen, const char *src)
{
    if (!dst || dstlen <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstlen - 1);
    dst[dstlen - 1] = '\0';
}

/* DISPLAY copy: truncate on a UTF-8 boundary and mark with "..." (§5). */
static void s_copy_disp(char *dst, int dstlen, const char *src)
{
    if (!dst || dstlen <= 0) return;
    if (!src) src = "";
    size_t len = strlen(src);
    if (len <= (size_t)(dstlen - 1)) {
        memcpy(dst, src, len + 1);
        return;
    }
    size_t cut = (size_t)(dstlen - 4);               /* room for "..." + NUL */
    while (cut > 0 && ((src[cut] & 0xC0) == 0x80)) cut--;   /* UTF-8 seq start */
    memcpy(dst, src, cut);
    dst[cut] = '.'; dst[cut + 1] = '.'; dst[cut + 2] = '.';
    dst[cut + 3] = '\0';
}

/* Truncate a std::string on a UTF-8 boundary at ~max bytes, marking the cut
 * (single-letter 4KB rule, §5). */
static void s_trunc_mark(string &s, size_t max, const char *mark)
{
    if (s.size() <= max) return;
    size_t cut = max - strlen(mark);
    while (cut > 0 && ((s[cut] & 0xC0) == 0x80)) cut--;
    s.resize(cut);
    s += mark;
}

/* ---- URL + config preconditions ------------------------------------------- */

/* "<base>/api/v1<path>", tolerating a trailing slash in the stored base. */
static string pp_url(const char *base, const char *path)
{
    string url = base ? base : "";
    while (!url.empty() && url.back() == '/') url.pop_back();
    url += "/api/v1";
    url += path;
    return url;
}

static bool pp_cfg_ok(const char *base, const char *key, string *err)
{
    if (!base || !base[0]) {
        if (err) *err = "server URL not set (Cfg)";
        return false;
    }
    if (!key || !key[0]) {
        if (err) *err = "API key not set (Cfg)";
        return false;
    }
    return true;
}

/* ---- transport (design §3.3) ----------------------------------------------
 * Own request function: http:// -> plain WiFiClient (no NTP/TLS dependency),
 * https:// -> WiFiClientSecure per http_get_tls_mode(). http_apply_tls() /
 * http_ensure_time() are ADDITIVE http_utils exports so both transports share
 * ONE CA bundle and policy; existing http_* callers are untouched. */
typedef struct {
    int code;        /* HTTP status; <=0 = transport failure */
    bool ok;         /* 2xx */
    bool replayed;   /* response carried Idempotent-Replayed: true (§2.2) */
    string body;
    string error;
} pp_http_t;

static pp_http_t pp_request(const char *method, const string &url,
                            const char *body, const char *idem_key,
                            const char *api_key, uint32_t timeout_ms)
{
    pp_http_t r = {0, false, false, "", ""};

    const bool is_https = (url.rfind("https://", 0) == 0);
    if (!is_https && url.rfind("http://", 0) != 0) {
        r.error = "URL must start with http:// or https://";
        return r;
    }
    if (is_https && http_get_tls_mode() != HTTP_TLS_INSECURE &&
        !http_ensure_time(5000)) {
        r.code = -3;
        r.error = "Time not synced - retry after NTP";
        return r;
    }

    WiFiClient plain;
    WiFiClientSecure secure;
    if (is_https) http_apply_tls(secure);

    HTTPClient http;
    http.setTimeout(timeout_ms);
    /* HTTPClient::begin takes WiFiClient& (secure derives from it); pass the
     * matching client for the URL scheme. */
    const bool begun = is_https ? http.begin(secure, url.c_str())
                                : http.begin(plain, url.c_str());
    if (!begun) {
        r.error = is_https ? "Failed to connect (TLS)" : "Failed to connect";
        return r;
    }

    /* Idempotent-Replayed must reach the SEND result (§2.2 "replayed" flag).
     * collectHeaders costs nothing on the read/compute endpoints. */
    static const char *wanted_headers[] = {"Idempotent-Replayed"};
    http.collectHeaders(wanted_headers, 1);

    http.addHeader("X-API-Key", api_key);
    if (idem_key && idem_key[0]) {
        http.addHeader("Idempotency-Key", idem_key);
    }

    if (strcmp(method, "POST") == 0) {
        http.addHeader("Content-Type", "application/json");
        r.code = http.POST(body ? body : "");
    } else {
        r.code = http.GET();
    }

    if (r.code > 0) {
        r.body = http.getString().c_str();
        r.ok = (r.code >= 200 && r.code < 300);
        if (http.header("Idempotent-Replayed") == "true") r.replayed = true;
    } else {
        r.body = http.errorToString(r.code).c_str();
        r.error = r.body;
    }
    http.end();
    return r;
}

/* Human-readable failure line: transport error, or "HTTP <code>" plus the
 * FastAPI {"detail": "..."} when present. */
static string pp_fail(const pp_http_t &r)
{
    if (r.code <= 0) return r.error.empty() ? "network error" : r.error;
    char head[24];
    snprintf(head, sizeof(head), "HTTP %d", r.code);
    string out = head;
    if (!r.body.empty()) {
        cJSON *root = cJSON_Parse(r.body.c_str());
        if (root) {
            cJSON *d = cJSON_GetObjectItem(root, "detail");
            if (cJSON_IsString(d) && d->valuestring && d->valuestring[0]) {
                char msg[80];
                s_copy(msg, sizeof(msg), d->valuestring);
                out += " - ";
                out += msg;
            }
            cJSON_Delete(root);
        }
    }
    return out;
}

/* ---- idempotency key (§2.2) ----------------------------------------------- */

void penpal_new_idem_key(char out[33])
{
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i * 2] = hex[rnd[i] >> 4];
        out[i * 2 + 1] = hex[rnd[i] & 0x0F];
    }
    out[32] = '\0';
}

/* ---- config chain (§3.4) --------------------------------------------------- */

void penpal_load_config(char *base, int base_len, char *key, int key_len)
{
    String b = "", k = "";
    Preferences p;
    p.begin("penpal", true);
    const bool b_saved = p.isKey("base");       /* saved "" wins over env */
    const bool k_saved = p.isKey("key");
    if (b_saved) b = p.getString("base", "");
    if (k_saved) k = p.getString("key", "");
    p.end();

    if (!b_saved) {
        char v[96] = "";
        if (env_get("PENPAL_BASE", v, sizeof(v))) {
            b = v;
        }
#ifdef PENPAL_BASE_DEFAULT_DEV
        else {
            b = PENPAL_BASE_DEFAULT_DEV;
        }
#endif
    }
    if (!k_saved) {
        char v[96] = "";
        if (env_get("PENPAL_KEY", v, sizeof(v))) {
            k = v;
        }
#ifdef PENPAL_KEY_DEFAULT_DEV
        else {
            k = PENPAL_KEY_DEFAULT_DEV;
        }
#endif
    }
    s_copy(base, base_len, b.c_str());
    s_copy(key, key_len, k.c_str());
}

bool penpal_save_config(const char *base, const char *key)
{
    Preferences p;
    p.begin("penpal", false);
    const String nb = base ? String(base) : String("");
    const String nk = key ? String(key) : String("");
    p.putString("base", nb);
    p.putString("key", nk);
    /* single-slot + round-trip verify (§3.4; no dual-slot needed) */
    const bool ok = p.isKey("base") && p.isKey("key") &&
                    p.getString("base", "") == nb &&
                    p.getString("key", "") == nk;
    p.end();
    if (!ok) Serial.println("[PenPal] config save failed (NVS write/verify)");
    return ok;
}

/* ---- endpoints -------------------------------------------------------------- */

bool penpal_get_pals(const char *base, const char *key,
                     pp_pal_t *out, int max, int *count, string *err)
{
    if (count) *count = 0;
    if (!pp_cfg_ok(base, key, err)) return false;

    pp_http_t r = pp_request("GET", pp_url(base, "/pen-pals"), NULL, NULL,
                             key, PP_TIMEOUT_CRUD_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s pals failed: %s\n", PP_TAG, pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (pen-pals)";
        Serial.printf("%s pals: bad JSON\n", PP_TAG);
        return false;
    }
    int n = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, root) {
        if (n >= max) {
            Serial.printf("%s pals: >%d rows, extra dropped\n", PP_TAG, max);
            break;
        }
        out[n].id = j_int(it, "id", 0);
        out[n].is_npc = j_bool(it, "is_npc", false);
        s_copy(out[n].name, sizeof(out[n].name), j_str(it, "name"));
        s_copy(out[n].status, sizeof(out[n].status), j_str(it, "status"));
        n++;
    }
    cJSON_Delete(root);
    if (count) *count = n;
    return true;
}

bool penpal_get_topics(const char *base, const char *key,
                       pp_topic_t *out, int max, int *count, string *err)
{
    if (count) *count = 0;
    if (!pp_cfg_ok(base, key, err)) return false;

    pp_http_t r = pp_request("GET", pp_url(base, "/topics/suggestions"),
                             NULL, NULL, key, PP_TIMEOUT_CRUD_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s topics failed: %s\n", PP_TAG, pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (topics)";
        Serial.printf("%s topics: bad JSON\n", PP_TAG);
        return false;
    }
    int n = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, root) {
        if (n >= max) {
            Serial.printf("%s topics: >%d rows, extra dropped\n", PP_TAG, max);
            break;
        }
        out[n].id = j_int(it, "id", 0);
        s_copy_disp(out[n].title, sizeof(out[n].title), j_str(it, "title"));
        s_copy(out[n].tag, sizeof(out[n].tag), j_str(it, "exam_tag"));
        s_copy_disp(out[n].background, sizeof(out[n].background),
                    j_str(it, "background"));
        /* guiding_questions is an array; join with spaces into one block */
        string joined;
        cJSON *g = cJSON_GetObjectItem(it, "guiding_questions");
        if (g && cJSON_IsArray(g)) {
            cJSON *q = NULL;
            cJSON_ArrayForEach(q, g) {
                if (cJSON_IsString(q) && q->valuestring && q->valuestring[0]) {
                    if (!joined.empty()) joined += ' ';
                    joined += q->valuestring;
                }
            }
        }
        s_copy_disp(out[n].guiding, sizeof(out[n].guiding), joined.c_str());
        n++;
    }
    cJSON_Delete(root);
    if (count) *count = n;
    return true;
}

bool penpal_get_mailbox(const char *base, const char *key,
                        pp_thread_row_t *out, int max, int *count,
                        bool *truncated, string *err)
{
    if (count) *count = 0;
    if (truncated) *truncated = false;
    if (!pp_cfg_ok(base, key, err)) return false;

    pp_http_t r = pp_request("GET", pp_url(base, "/emails/mailbox"),
                             NULL, NULL, key, PP_TIMEOUT_CRUD_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s mailbox failed: %s\n", PP_TAG, pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (mailbox)";
        Serial.printf("%s mailbox: bad JSON\n", PP_TAG);
        return false;
    }
    int n = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, root) {
        if (n >= max) {
            if (truncated) *truncated = true;
            Serial.printf("%s mailbox: >%d rows, extra dropped\n", PP_TAG, max);
            break;
        }
        /* pen_pal_id may be null on deleted-pal leftovers: keep the 0
         * sentinel; HOME shows those rows and THREAD opens them read-only
         * via the residual channel (§5, R9 live since 2026-08-22). */
        out[n].root_id = j_int(it, "thread_root_id", 0);
        out[n].pal_id = j_int(it, "pen_pal_id", 0);
        s_copy_disp(out[n].subject, sizeof(out[n].subject), j_str(it, "subject"));
        s_copy(out[n].from, sizeof(out[n].from), j_str(it, "counterpart"));
        s_copy(out[n].last_sender, sizeof(out[n].last_sender),
               j_str(it, "last_sender"));
        s_copy(out[n].state, sizeof(out[n].state), j_str(it, "state"));
        out[n].unread = j_int(it, "unread", 0);
        out[n].count = j_int(it, "count", 0);
        s_copy(out[n].last_at, sizeof(out[n].last_at), j_str(it, "last_at"));
        n++;
    }
    cJSON_Delete(root);
    if (count) *count = n;
    return true;
}

bool penpal_get_thread(const char *base, const char *key,
                       int pen_pal_id, int thread_root_id,
                       pp_letter_t *out, int max, int *count, int *dropped,
                       string *err)
{
    if (count) *count = 0;
    if (dropped) *dropped = 0;
    if (!pp_cfg_ok(base, key, err)) return false;

    /* pen_pal_id optional since server R9 (2026-08-22, live-verified): when
     * omitted the thread is read participant-authorized by thread_root_id
     * alone - the residual channel for deleted-pal leftovers (pal_id 0
     * sentinel). Both params missing is still a 400. */
    char path[64];
    if (pen_pal_id > 0)
        snprintf(path, sizeof(path), "/emails?pen_pal_id=%d&thread_root_id=%d",
                 pen_pal_id, thread_root_id);
    else
        snprintf(path, sizeof(path), "/emails?thread_root_id=%d", thread_root_id);
    pp_http_t r = pp_request("GET", pp_url(base, path), NULL, NULL,
                             key, PP_TIMEOUT_CRUD_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s thread %d failed: %s\n", PP_TAG, thread_root_id,
                      pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    cJSON *emails = root ? cJSON_GetObjectItem(root, "emails") : NULL;
    if (!emails || !cJSON_IsArray(emails)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (thread)";
        Serial.printf("%s thread %d: bad JSON\n", PP_TAG, thread_root_id);
        return false;
    }
    int n = 0;
    size_t total = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, emails) {              /* ascending: oldest first */
        if (n >= max) {
            Serial.printf("%s thread %d: >%d letters, extra dropped\n",
                          PP_TAG, thread_root_id, max);
            break;
        }
        out[n].id = j_int(it, "id", 0);
        out[n].mine = j_mine(it);
        s_copy(out[n].sender, sizeof(out[n].sender), j_str(it, "sender_name"));
        s_copy(out[n].time, sizeof(out[n].time), j_str(it, "created_at"));
        const char *c = j_str(it, "content");
        out[n].content = c ? c : "";
        s_trunc_mark(out[n].content, PP_LETTER_MAX, " (truncated)");
        total += out[n].content.size();
        n++;
    }
    cJSON_Delete(root);

    /* 16KB thread budget: drop OLDEST letters until it fits (§5). The newest
     * letter is never dropped. std::string members -> move, not memmove. */
    int k = 0;
    while (k < n - 1 && total > PP_THREAD_BUDGET) {
        total -= out[k].content.size();
        k++;
    }
    if (k > 0) {
        for (int i = 0; i < n - k; i++) out[i] = std::move(out[i + k]);
        n -= k;
        if (dropped) *dropped = k;
        Serial.printf("%s thread %d: %d oldest dropped (size limit)\n",
                      PP_TAG, thread_root_id, k);
    }
    if (count) *count = n;
    return true;
}

bool penpal_send_email(const char *base, const char *key,
                       const pp_send_req_t *req, const char *idem_key,
                       int *email_id, int *thread_root_id, bool *reply_pending,
                       bool *replayed, string *err)
{
    if (email_id) *email_id = 0;
    if (thread_root_id) *thread_root_id = 0;
    if (reply_pending) *reply_pending = false;
    if (replayed) *replayed = false;
    if (!pp_cfg_ok(base, key, err)) return false;

    cJSON *b = cJSON_CreateObject();
    cJSON_AddNumberToObject(b, "pen_pal_id", req->pen_pal_id);
    cJSON_AddStringToObject(b, "subject", req->subject.c_str());
    if (req->has_topic) cJSON_AddNumberToObject(b, "topic_id", req->topic_id);
    else cJSON_AddNullToObject(b, "topic_id");
    if (req->has_thread_root) {
        cJSON_AddNumberToObject(b, "thread_root_id", req->thread_root_id);
    }
    cJSON_AddStringToObject(b, "content", req->content.c_str());
    char *body = cJSON_PrintUnformatted(b);
    cJSON_Delete(b);
    if (!body) {
        if (err) *err = "payload build failed";
        return false;
    }

    pp_http_t r = pp_request("POST", pp_url(base, "/emails"), body, idem_key,
                             key, PP_TIMEOUT_CRUD_MS);
    free(body);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s send failed: %s\n", PP_TAG, pp_fail(r).c_str());
        return false;
    }
    if (replayed) *replayed = r.replayed;

    /* {"email": {"id", "thread_root_id", ...}, "reply_pending": bool};
     * 201 first delivery / 200 replay both land here (§2.2). */
    cJSON *root = cJSON_Parse(r.body.c_str());
    cJSON *email = root ? cJSON_GetObjectItem(root, "email") : NULL;
    if (!email) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (send)";
        Serial.printf("%s send: bad JSON\n", PP_TAG);
        return false;
    }
    if (email_id) *email_id = j_int(email, "id", 0);
    if (thread_root_id) *thread_root_id = j_int(email, "thread_root_id", 0);
    if (reply_pending) *reply_pending = j_bool(root, "reply_pending", false);
    cJSON_Delete(root);
    Serial.printf("%s sent email %d root %d replayed=%d\n", PP_TAG,
                  email_id ? *email_id : 0,
                  thread_root_id ? *thread_root_id : 0, r.replayed);
    return true;
}

bool penpal_correction(const char *base, const char *key, int email_id,
                       pp_fix_t *out, string *err)
{
    memset(out, 0, sizeof(*out));
    if (!pp_cfg_ok(base, key, err)) return false;

    char path[48];
    snprintf(path, sizeof(path), "/emails/%d/correction", email_id);
    pp_http_t r = pp_request("POST", pp_url(base, path), NULL, NULL,
                             key, PP_TIMEOUT_LLM_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s correction %d failed: %s\n", PP_TAG, email_id,
                      pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    cJSON *items = root ? cJSON_GetObjectItem(root, "corrections") : NULL;
    if (!items || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (correction)";
        Serial.printf("%s correction: bad JSON\n", PP_TAG);
        return false;
    }
    out->degraded = j_bool(root, "degraded", false);
    int n = 0;
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {
        if (n >= PP_FIX_MAX) {
            out->truncated = true;
            Serial.printf("%s correction: >%d items, extra dropped\n",
                          PP_TAG, PP_FIX_MAX);
            break;
        }
        s_copy(out->items[n].type, sizeof(out->items[n].type), j_str(it, "type"));
        s_copy_disp(out->items[n].from, sizeof(out->items[n].from), j_str(it, "from"));
        s_copy_disp(out->items[n].to, sizeof(out->items[n].to), j_str(it, "to"));
        s_copy_disp(out->items[n].explanation, sizeof(out->items[n].explanation),
                    j_str(it, "explanation"));
        n++;
    }
    out->count = n;
    cJSON_Delete(root);
    return true;
}

bool penpal_polish(const char *base, const char *key, int email_id,
                   pp_polish_t *out, string *err)
{
    memset(out, 0, sizeof(*out));
    if (!pp_cfg_ok(base, key, err)) return false;

    char path[48];
    snprintf(path, sizeof(path), "/emails/%d/polish", email_id);
    pp_http_t r = pp_request("POST", pp_url(base, path), NULL, NULL,
                             key, PP_TIMEOUT_LLM_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s polish %d failed: %s\n", PP_TAG, email_id,
                      pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    if (!root) {
        if (err) *err = "bad JSON (polish)";
        Serial.printf("%s polish: bad JSON\n", PP_TAG);
        return false;
    }
    out->degraded = j_bool(root, "degraded", false);
    const char *imp = j_str(root, "improved_email");
    out->improved = imp ? imp : "";
    s_trunc_mark(out->improved, PP_LETTER_MAX, " (truncated)");

    cJSON *arr = cJSON_GetObjectItem(root, "improvements");
    if (arr && cJSON_IsArray(arr)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, arr) {          /* array of plain strings */
            if (out->imp_count >= PP_POLISH_IMP_MAX) {
                Serial.printf("%s polish: >%d improvements, extra dropped\n",
                              PP_TAG, PP_POLISH_IMP_MAX);
                break;
            }
            s_copy_disp(out->improvements[out->imp_count],
                        sizeof(out->improvements[0]),
                        (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL);
            out->imp_count++;
        }
    }
    arr = cJSON_GetObjectItem(root, "topic_coverage");
    if (arr && cJSON_IsArray(arr)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, arr) {
            if (out->cov_count >= PP_COVERAGE_MAX) {
                Serial.printf("%s polish: >%d coverage rows, extra dropped\n",
                              PP_TAG, PP_COVERAGE_MAX);
                break;
            }
            s_copy_disp(out->coverage[out->cov_count].question,
                        sizeof(out->coverage[0].question), j_str(it, "question"));
            s_copy(out->coverage[out->cov_count].status,
                   sizeof(out->coverage[0].status), j_str(it, "status"));
            out->cov_count++;
        }
    }
    cJSON_Delete(root);
    return true;
}

bool penpal_tips(const char *base, const char *key, int email_id,
                 pp_tips_t *out, string *err)
{
    memset(out, 0, sizeof(*out));
    if (!pp_cfg_ok(base, key, err)) return false;

    char path[48];
    snprintf(path, sizeof(path), "/emails/%d/tips", email_id);
    pp_http_t r = pp_request("POST", pp_url(base, path), NULL, NULL,
                             key, PP_TIMEOUT_LLM_MS);
    if (!r.ok) {
        if (err) *err = pp_fail(r);
        Serial.printf("%s tips %d failed: %s\n", PP_TAG, email_id,
                      pp_fail(r).c_str());
        return false;
    }

    cJSON *root = cJSON_Parse(r.body.c_str());
    cJSON *items = root ? cJSON_GetObjectItem(root, "tips") : NULL;
    if (!items || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        if (err) *err = "bad JSON (tips)";
        Serial.printf("%s tips: bad JSON\n", PP_TAG);
        return false;
    }
    out->degraded = j_bool(root, "degraded", false);
    cJSON *it = NULL;
    cJSON_ArrayForEach(it, items) {            /* array of plain strings */
        if (out->count >= PP_TIPS_MAX) {
            out->truncated = true;
            Serial.printf("%s tips: >%d items, extra dropped\n",
                          PP_TAG, PP_TIPS_MAX);
            break;
        }
        s_copy_disp(out->tips[out->count], sizeof(out->tips[0]),
                    (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL);
        out->count++;
    }
    cJSON_Delete(root);
    return true;
}
