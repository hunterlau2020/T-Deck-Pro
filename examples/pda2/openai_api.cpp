/**
 * @file      openai_api.cpp
 * @brief     OpenAI-compatible chat client (OpenRouter etc.), text-only.
 */
#include "Arduino.h"
#include "openai_api.h"
#include "http_utils.h"
#include <Preferences.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* ---- dual-slot config storage (copilot finding 1.2) ---------------------
 * Fields live in slot 0/1 ("base.0", "model.0", "key.0" / "...1"); the
 * single "active" key selects the live slot. Staging into the inactive
 * slot + verifying reads, followed by ONE atomic "active" flip, makes the
 * switch commit-or-not: a failure before the flip leaves the previous
 * config fully intact - no rollback of possibly-failed writes needed.
 * NOTE: load/save must be called from a single thread (Preferences is not
 * re-entrant); all current callers run on the UI thread. */
static const char *cfg_key(const char *field, int slot)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "%s.%d", field, slot);
    return buf;
}

void openai_load_config(char *base, int base_len, char *model, int model_len,
                        char *key, int key_len)
{
    /* Fallback chain (copilot finding 1.8 + main review 1.6):
     * 1. ACTIVE SLOT, if it is initialized. A slot counts as initialized
     *    as soon as ANY of its three keys exists - an empty-but-saved Base
     *    must be read back as empty, NOT silently replaced.
     * 2. Legacy flat keys from pre-dual-slot firmware (any of base/model/
     *    key present).
     * 3. Compile-time defaults.
     * The first save always lands in the INACTIVE slot (active defaults
     * to 0, so next = 1) and flips "active"; after that the legacy branch
     * is never taken again. */
    Preferences p;
    p.begin("ai", true);
    int slot = (p.getUChar("active", 0) == 0) ? 0 : 1;
    const bool slot_init = p.isKey(cfg_key("base",  slot)) ||
                           p.isKey(cfg_key("model", slot)) ||
                           p.isKey(cfg_key("key",   slot));
    String b, m, k;
    if (slot_init) {
        b = p.getString(cfg_key("base",  slot), "");
        m = p.getString(cfg_key("model", slot), "");
        k = p.getString(cfg_key("key",   slot), "");
    } else if (p.isKey("base") || p.isKey("model") || p.isKey("key")) {
        b = p.getString("base",  "");
        m = p.getString("model", "");
        k = p.getString("key",   "");
    } else {
        b = AI_BASE_DEFAULT;
        m = AI_MODEL_DEFAULT;
        k = AI_KEY_DEFAULT;
    }
    p.end();
    strncpy(base,  b.c_str(), base_len  - 1);
    strncpy(model, m.c_str(), model_len - 1);
    strncpy(key,   k.c_str(), key_len   - 1);
    base[base_len-1] = model[model_len-1] = key[key_len-1] = '\0';
}

bool openai_save_config(const char *base, const char *model, const char *key,
                        const char **err)
{
    Preferences p;
    p.begin("ai", false);
    const int active = (p.getUChar("active", 0) == 0) ? 0 : 1;
    const int next = 1 - active;

    const String nb = base  ? String(base)  : String("");
    const String nm = model ? String(model) : String("");
    const String nk = key   ? String(key)   : String("");

    /* stage the whole config into the INACTIVE slot + verify round-trip */
    bool staged =
        p.putString(cfg_key("base",  next), nb) > 0 &&
        p.putString(cfg_key("model", next), nm) > 0 &&
        p.putString(cfg_key("key",   next), nk) > 0 &&
        p.getString(cfg_key("base",  next), "") == nb &&
        p.getString(cfg_key("model", next), "") == nm &&
        p.getString(cfg_key("key",   next), "") == nk;
    if (!staged) {
        p.end();
        if (err) *err = "NVS write failed";
        Serial.println("[AI] save aborted: slot staging failed");
        return false;
    }

    /* COMMIT: one atomic key flip switches the whole config */
    if (p.putUChar("active", (uint8_t)next) == 0) {
        p.end();
        if (err) *err = "NVS commit failed";
        Serial.println("[AI] save failed: active-slot flip failed");
        return false;
    }
    p.end();
    return true;
}

static cJSON *ai_msg_add(cJSON *msgs, const char *role, const char *content)
{
    cJSON *m = cJSON_CreateObject();
    if (!m) return NULL;
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(msgs, m);
    return m;
}

/* ---- usage statistics (round 21) ----------------------------------------
 * The response's "usage" block is accumulated for a future statistics
 * screen. Fields are provider-specific (user requirement 2): EVERY field
 * is optional and read as 0 when absent, unknown extra fields ignored.
 * CHAT and TEST usage are kept SEPARATE (main review 1.4: Test pings
 * also bill the account, users must not see them mixed into chat totals).
 * Storage: ONE NVS blob "stats" in namespace "ai_stats". The RAM copy is
 * guarded by a mutex because chat-send and config-Test tasks may complete
 * concurrently (copilot finding 1.9: read-modify-write race). The mutex
 * is created STATICALLY at load time - a lazily created handle has its
 * own first-use race when two tasks both see NULL (copilot finding 1.3).
 * Persistence is THROTTLED (main review 1.3): at most one blob commit
 * per 60 s or per 20 responses, so a burst of Test pings does not wear
 * NVS; up to the throttle window of deltas may be lost on power loss,
 * RAM totals are always current.
 * The counters are NEVER reset by the chat "New" button - they are usage
 * accounting, not conversation data. */
#define AI_STATS_MAGIC_V1 0x53544154u  /* "STAT": 74c24ff single-group blob */
#define AI_STATS_MAGIC_V2 0x53544156u  /* "STAV": dual-group blob (0328cd2+) */

typedef struct {
    uint32_t magic;
    /* chat */
    uint64_t p_tok, c_tok, tot_tok, cached, cwrite, audio, reasoning;
    double   cost;
    /* test (config screen pings) */
    uint64_t t_p_tok, t_c_tok, t_tot_tok, t_cached, t_cwrite, t_audio, t_reasoning;
    double   t_cost;
} ai_stats_t;

/* pre-split layout written by 74c24ff (same magic family, no test group) */
typedef struct {
    uint32_t magic;
    uint64_t p_tok, c_tok, tot_tok;
    double   cost;
    uint64_t cached, cwrite, audio, reasoning;
} ai_stats_v1_t;

static StaticSemaphore_t s_ai_stats_mux_buf;
static SemaphoreHandle_t s_ai_stats_mux =
    xSemaphoreCreateMutexStatic(&s_ai_stats_mux_buf);  /* static init: no lazy-creation race */
static ai_stats_t s_ai_stats;
static bool s_ai_stats_loaded = false;
static uint32_t s_stats_since_persist = 0;      /* responses since last blob commit */
static uint32_t s_stats_last_persist_ms = 0;

static uint64_t ai_json_u64(cJSON *obj, const char *key)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    return (it && cJSON_IsNumber(it)) ? (uint64_t)it->valuedouble : 0;
}

static double ai_json_dbl(cJSON *obj, const char *key)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    return (it && cJSON_IsNumber(it)) ? it->valuedouble : 0.0;
}

/* Call with the mutex held. Migrates the pre-split V1 blob into the
 * chat group instead of silently zeroing it (copilot finding 1.4). */
static void ai_stats_load_locked(void)
{
    if (s_ai_stats_loaded) return;
    memset(&s_ai_stats, 0, sizeof(s_ai_stats));
    Preferences p;
    if (p.begin("ai_stats", false)) {
        size_t n = p.getBytes("stats", &s_ai_stats, sizeof(s_ai_stats));
        p.end();
        if (n == sizeof(s_ai_stats) && s_ai_stats.magic == AI_STATS_MAGIC_V2) {
            /* current format - nothing to do */
        } else if (n == sizeof(ai_stats_v1_t)) {
            ai_stats_v1_t v1;
            if (p.begin("ai_stats", false)) {
                n = p.getBytes("stats", &v1, sizeof(v1));
                p.end();
            }
            if (n == sizeof(v1) && v1.magic == AI_STATS_MAGIC_V1) {
                /* migrate: the old single group becomes the chat group */
                s_ai_stats.p_tok = v1.p_tok;
                s_ai_stats.c_tok = v1.c_tok;
                s_ai_stats.tot_tok = v1.tot_tok;
                s_ai_stats.cost = v1.cost;
                s_ai_stats.cached = v1.cached;
                s_ai_stats.cwrite = v1.cwrite;
                s_ai_stats.audio = v1.audio;
                s_ai_stats.reasoning = v1.reasoning;
                s_stats_since_persist = 1;      /* dirty: the next flush commits
                                                 * the V2 schema (copilot 1.5) */
                Serial.println("[AI] stats blob migrated from V1 to V2");
            }
        } else {
            Serial.println("[AI] stats blob unrecognized - starting fresh");
        }
    }
    s_ai_stats.magic = AI_STATS_MAGIC_V2;
    s_ai_stats_loaded = true;
}

/* Call with the mutex held. Returns true when the blob reached NVS. */
static bool ai_stats_persist_locked(void)
{
    bool saved = false;
    Preferences p;
    if (p.begin("ai_stats", false)) {
        saved = p.putBytes("stats", &s_ai_stats, sizeof(s_ai_stats)) == sizeof(s_ai_stats);
        p.end();
    }
    if (!saved) {
        Serial.println("[AI] stats persist failed - totals stay in RAM");
    }
    return saved;
}

/* Explicit flush for lifecycle checkpoints (copilot finding 1.1): the
 * throttle only commits on the next response, so low-frequency use
 * would otherwise never hit NVS. Call before deep sleep, on chat screen
 * destroy and on New. */
void openai_stats_flush(void)
{
    if (xSemaphoreTake(s_ai_stats_mux, portMAX_DELAY) != pdTRUE) return;
    ai_stats_load_locked();
    if (s_stats_since_persist > 0) {
        if (ai_stats_persist_locked()) {
            s_stats_since_persist = 0;
            s_stats_last_persist_ms = millis();
        }
    }
    xSemaphoreGive(s_ai_stats_mux);
}

void openai_stats_text(char *buf, int buf_len)
{
    if (!buf || buf_len <= 0) return;
    if (xSemaphoreTake(s_ai_stats_mux, portMAX_DELAY) != pdTRUE) {
        snprintf(buf, buf_len, "stats busy");
        return;
    }
    ai_stats_load_locked();
    /* show the full breakdown, including the cached-token details the
     * user asked to surface (usage.prompt_tokens_details.cached_tokens) */
    snprintf(buf, buf_len,
             "Chat: %llu tok\n"
             "  cached %llu, write %llu\n"
             "  audio %llu, rsn %llu\n"
             "  cost %.6f USD\n"
             "Test: %llu tok, %.6f USD",
             (unsigned long long)s_ai_stats.tot_tok,
             (unsigned long long)s_ai_stats.cached,
             (unsigned long long)s_ai_stats.cwrite,
             (unsigned long long)s_ai_stats.audio,
             (unsigned long long)s_ai_stats.reasoning,
             s_ai_stats.cost,
             (unsigned long long)s_ai_stats.t_tot_tok,
             s_ai_stats.t_cost);
    xSemaphoreGive(s_ai_stats_mux);
}

/* Time-based throttle from the main loop (copilot finding 1.4): the
 * 60 s window is now enforced by an actual periodic check, not only by
 * the NEXT response. Call once per loop() from factory.ino - it is a
 * no-op while nothing is dirty or the window has not elapsed. */
void openai_stats_poll(void)
{
    if (xSemaphoreTake(s_ai_stats_mux, 0) != pdTRUE) return;  /* contended: skip */
    ai_stats_load_locked();
    if (s_stats_since_persist > 0 &&
        millis() - s_stats_last_persist_ms >= 60000) {
        if (ai_stats_persist_locked()) {
            s_stats_since_persist = 0;
            s_stats_last_persist_ms = millis();
        } else {
            /* finite backoff, same as the accumulate path */
            s_stats_last_persist_ms = millis() - 60000 + 10000;
        }
    }
    xSemaphoreGive(s_ai_stats_mux);
}

static void ai_usage_accumulate(cJSON *root, bool is_test)
{
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (!usage) return;                 /* absent: nothing to count */

    cJSON *pd = cJSON_GetObjectItem(usage, "prompt_tokens_details");
    cJSON *cd = cJSON_GetObjectItem(usage, "completion_tokens_details");

    const uint64_t p_tok  = ai_json_u64(usage, "prompt_tokens");
    const uint64_t c_tok  = ai_json_u64(usage, "completion_tokens");
    const uint64_t tot    = ai_json_u64(usage, "total_tokens");
    const double   cost   = ai_json_dbl(usage, "cost");
    const uint64_t cached = pd ? ai_json_u64(pd, "cached_tokens") : 0;
    const uint64_t cwrite = pd ? ai_json_u64(pd, "cache_write_tokens") : 0;
    const uint64_t audio  = pd ? ai_json_u64(pd, "audio_tokens") : 0;
    const uint64_t reason = cd ? ai_json_u64(cd, "reasoning_tokens") : 0;

    if (xSemaphoreTake(s_ai_stats_mux, portMAX_DELAY) != pdTRUE) return;

    ai_stats_load_locked();
    if (is_test) {
        s_ai_stats.t_p_tok     += p_tok;
        s_ai_stats.t_c_tok     += c_tok;
        s_ai_stats.t_tot_tok   += tot;
        s_ai_stats.t_cost      += cost;
        s_ai_stats.t_cached    += cached;
        s_ai_stats.t_cwrite    += cwrite;
        s_ai_stats.t_audio     += audio;
        s_ai_stats.t_reasoning += reason;
    } else {
        s_ai_stats.p_tok     += p_tok;
        s_ai_stats.c_tok     += c_tok;
        s_ai_stats.tot_tok   += tot;
        s_ai_stats.cost      += cost;
        s_ai_stats.cached    += cached;
        s_ai_stats.cwrite    += cwrite;
        s_ai_stats.audio     += audio;
        s_ai_stats.reasoning += reason;
    }

    /* THROTTLED blob commit (main review 1.3): every 20 responses or
     * every 60 s, not on every response - a Test-ping burst must not
     * wear the NVS. RAM totals are always current. On a failed commit
     * the dirty counters are KEPT and retried with a 10 s backoff
     * (copilot finding 1.2). */
    s_stats_since_persist++;
    const uint32_t now = millis();
    const bool due = (s_stats_since_persist >= 20) ||
                     (now - s_stats_last_persist_ms >= 60000);
    bool saved = false;
    if (due) {
        saved = ai_stats_persist_locked();
        if (saved) {
            s_stats_since_persist = 0;
            s_stats_last_persist_ms = now;
        } else {
            /* finite backoff: the next response retries after ~10 s */
            s_stats_last_persist_ms = now - 60000 + 10000;
        }
    }

    Serial.printf("[AI] usage%s +%llu/%llu tok, cost +%.8f | chat %llu/%llu %.6f | test %llu/%llu %.6f%s\n",
                  is_test ? "(test)" : "",
                  (unsigned long long)p_tok, (unsigned long long)c_tok, cost,
                  (unsigned long long)s_ai_stats.p_tok, (unsigned long long)s_ai_stats.c_tok,
                  s_ai_stats.cost,
                  (unsigned long long)s_ai_stats.t_p_tok, (unsigned long long)s_ai_stats.t_c_tok,
                  s_ai_stats.t_cost,
                  saved ? "" : (due ? " (unsaved)" : " (ram)"));
    xSemaphoreGive(s_ai_stats_mux);
}

static bool openai_chat_impl(const ai_message_t *history, int history_count,
                             const char *prompt, const char *base_url,
                             const char *model, const char *api_key, string &out,
                             uint32_t timeout_ms, bool is_test)
{
    if (!prompt || !base_url || !model || !api_key) return false;
    if (!http_require_wifi("AI")) return false;

    /* Request shape mirrors the OpenRouter curl reference:
     * {"model":..., "temperature":0.7, "reasoning":{"exclude":true},
     *  "messages":[{"role":"system",...},<history turns>,
     *              {"role":"user","content":<prompt>}]}
     * cJSON_AddStringToObject performs proper JSON string escaping, so
     * quotes/backslashes/newlines in any message cannot break the body. */
    cJSON *root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddNumberToObject(root, "temperature", 0.7);
    cJSON *reasoning = cJSON_AddObjectToObject(root, "reasoning");
    if (reasoning) cJSON_AddBoolToObject(reasoning, "exclude", true);

    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");
    if (!msgs) {
        cJSON_Delete(root);
        return false;
    }
    if (!ai_msg_add(msgs, "system", AI_SYSTEM_PROMPT)) {
        cJSON_Delete(root);
        return false;
    }
    if (history) {
        for (int i = 0; i < history_count; i++) {
            if (!history[i].role || !history[i].content) continue;
            if (!ai_msg_add(msgs, history[i].role, history[i].content)) {
                cJSON_Delete(root);
                return false;
            }
        }
    }
    if (!ai_msg_add(msgs, "user", prompt)) {
        cJSON_Delete(root);
        return false;
    }
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return false;

    char auth[160];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    http_response_t resp = http_post(base_url, string(body),
                                     "application/json", auth, timeout_ms);
    free(body);

    if (!resp.success || resp.status_code != 200) {
        Serial.printf("[AI] HTTP %d, len=%u\n", resp.status_code, (unsigned)resp.body.length());
        return false;
    }

    /* Parse choices[0].message.content */
    cJSON *j = cJSON_Parse(resp.body.c_str());
    if (!j) return false;
    ai_usage_accumulate(j, is_test);    /* count usage whenever it is present */
    cJSON *choices = cJSON_GetObjectItem(j, "choices");
    cJSON *c0 = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg0 = c0 ? cJSON_GetObjectItem(c0, "message") : NULL;
    cJSON *content = msg0 ? cJSON_GetObjectItem(msg0, "content") : NULL;
    bool ok = (content != NULL) && cJSON_IsString(content) && content->valuestring != NULL;
    if (ok) out = content->valuestring;
    cJSON_Delete(j);
    return ok;
}

bool openai_chat_multi(const ai_message_t *history, int history_count,
                       const char *prompt, const char *base_url,
                       const char *model, const char *api_key, string &out,
                       uint32_t timeout_ms)
{
    /* chat path: usage goes into the CHAT counters */
    return openai_chat_impl(history, history_count, prompt, base_url, model,
                            api_key, out, timeout_ms, false);
}

bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out,
                 uint32_t timeout_ms)
{
    /* single-turn wrapper (AI Config Test ping): usage goes into the
     * separate TEST counters (main review 1.4) */
    return openai_chat_impl(NULL, 0, prompt, base_url, model, api_key,
                            out, timeout_ms, true);
}
