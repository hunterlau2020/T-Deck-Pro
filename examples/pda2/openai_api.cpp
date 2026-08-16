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
 * Storage: ONE NVS blob "stats" in namespace "ai_stats" - a single
 * putBytes commit per response instead of eight separate NVS writes
 * (copilot finding 1.10: write amplification). The RAM copy is guarded
 * by a mutex because chat-send and config-Test tasks may complete
 * concurrently (copilot finding 1.9: read-modify-write race).
 * The counters are NEVER reset by the chat "New" button - they are usage
 * accounting, not conversation data. */
#define AI_STATS_MAGIC 0x53544154u  /* "STAT" */

typedef struct {
    uint32_t magic;
    uint64_t p_tok;         /* usage.prompt_tokens */
    uint64_t c_tok;         /* usage.completion_tokens */
    uint64_t tot_tok;       /* usage.total_tokens */
    double   cost;          /* usage.cost */
    uint64_t cached;        /* prompt_tokens_details.cached_tokens */
    uint64_t cwrite;        /* prompt_tokens_details.cache_write_tokens */
    uint64_t audio;         /* prompt_tokens_details.audio_tokens */
    uint64_t reasoning;     /* completion_tokens_details.reasoning_tokens */
} ai_stats_t;

static SemaphoreHandle_t s_ai_stats_mux = NULL;
static ai_stats_t s_ai_stats;
static bool s_ai_stats_loaded = false;

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

/* Call with the mutex held. */
static void ai_stats_load_locked(void)
{
    if (s_ai_stats_loaded) return;
    memset(&s_ai_stats, 0, sizeof(s_ai_stats));
    Preferences p;
    if (p.begin("ai_stats", false)) {
        size_t n = p.getBytes("stats", &s_ai_stats, sizeof(s_ai_stats));
        p.end();
        if (n != sizeof(s_ai_stats) || s_ai_stats.magic != AI_STATS_MAGIC) {
            memset(&s_ai_stats, 0, sizeof(s_ai_stats));
        }
    }
    s_ai_stats.magic = AI_STATS_MAGIC;
    s_ai_stats_loaded = true;
}

static void ai_usage_accumulate(cJSON *root)
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

    if (!s_ai_stats_mux) s_ai_stats_mux = xSemaphoreCreateMutex();
    if (!s_ai_stats_mux) return;
    if (xSemaphoreTake(s_ai_stats_mux, portMAX_DELAY) != pdTRUE) return;

    ai_stats_load_locked();
    s_ai_stats.p_tok     += p_tok;
    s_ai_stats.c_tok     += c_tok;
    s_ai_stats.tot_tok   += tot;
    s_ai_stats.cost      += cost;
    s_ai_stats.cached    += cached;
    s_ai_stats.cwrite    += cwrite;
    s_ai_stats.audio     += audio;
    s_ai_stats.reasoning += reason;

    /* ONE blob commit per response; a failed begin/write is logged and
     * the RAM totals stay correct for the next flush */
    bool saved = false;
    Preferences p;
    if (p.begin("ai_stats", false)) {
        saved = p.putBytes("stats", &s_ai_stats, sizeof(s_ai_stats)) == sizeof(s_ai_stats);
        p.end();
    }
    if (!saved) {
        Serial.println("[AI] stats persist failed - totals stay in RAM");
    }

    Serial.printf("[AI] usage +%llu/%llu tok, cost +%.8f | totals %llu/%llu, %.6f%s\n",
                  (unsigned long long)p_tok, (unsigned long long)c_tok, cost,
                  (unsigned long long)s_ai_stats.p_tok, (unsigned long long)s_ai_stats.c_tok,
                  s_ai_stats.cost, saved ? "" : " (unsaved)");
    xSemaphoreGive(s_ai_stats_mux);
}

bool openai_chat_multi(const ai_message_t *history, int history_count,
                       const char *prompt, const char *base_url,
                       const char *model, const char *api_key, string &out,
                       uint32_t timeout_ms)
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
    ai_usage_accumulate(j);             /* count usage whenever it is present */
    cJSON *choices = cJSON_GetObjectItem(j, "choices");
    cJSON *c0 = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg0 = c0 ? cJSON_GetObjectItem(c0, "message") : NULL;
    cJSON *content = msg0 ? cJSON_GetObjectItem(msg0, "content") : NULL;
    bool ok = (content != NULL) && cJSON_IsString(content) && content->valuestring != NULL;
    if (ok) out = content->valuestring;
    cJSON_Delete(j);
    return ok;
}

bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out,
                 uint32_t timeout_ms)
{
    /* single-turn wrapper (e.g. the AI Config Test ping) */
    return openai_chat_multi(NULL, 0, prompt, base_url, model, api_key,
                             out, timeout_ms);
}
