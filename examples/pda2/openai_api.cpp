/**
 * @file      openai_api.cpp
 * @brief     OpenAI-compatible chat client (OpenRouter etc.), text-only.
 */
#include "Arduino.h"
#include "openai_api.h"
#include "http_utils.h"
#include <Preferences.h>
#include <cJSON.h>

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
    Preferences p;
    p.begin("ai", true);
    int slot = (p.getUChar("active", 0) == 0) ? 0 : 1;
    String b = p.getString(cfg_key("base",  slot), "");
    String m = p.getString(cfg_key("model", slot), "");
    String k = p.getString(cfg_key("key",   slot), "");
    if (b.length() == 0) {
        /* never saved (or pre-dual-slot layout): fall back to the legacy
         * flat keys from older firmware, then to the compile-time defaults */
        b = p.getString("base",  AI_BASE_DEFAULT);
        m = p.getString("model", AI_MODEL_DEFAULT);
        k = p.getString("key",   AI_KEY_DEFAULT);
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
 * The response's "usage" block is accumulated into NVS namespace
 * "ai_stats" for a future statistics screen. Fields are provider-specific
 * (user requirement 2): EVERY field is optional and read as 0 when absent,
 * and unknown extra fields are ignored. NVS keys are shortened because
 * NVS names are limited to 15 chars:
 *   prompt_tokens       -> p_tok        (usage.prompt_tokens)
 *   completion_tokens   -> c_tok        (usage.completion_tokens)
 *   total_tokens        -> tot_tok      (usage.total_tokens)
 *   cost                -> cost         (usage.cost, double)
 *   cached_tokens       -> cached       (prompt_tokens_details.cached_tokens)
 *   cache_write_tokens  -> cwrite       (prompt_tokens_details.cache_write_tokens)
 *   audio_tokens        -> audio        (prompt_tokens_details.audio_tokens)
 *   reasoning_tokens    -> reasoning    (completion_tokens_details.reasoning_tokens)
 * The counters are NEVER reset by the chat "New"/clear-history button -
 * they are usage accounting, not conversation data. */
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

static void ai_usage_accumulate(cJSON *root)
{
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (!usage) return;                 /* absent: nothing to count */

    cJSON *pd = cJSON_GetObjectItem(usage, "prompt_tokens_details");
    cJSON *cd = cJSON_GetObjectItem(usage, "completion_tokens_details");

    uint64_t p_tok  = ai_json_u64(usage, "prompt_tokens");
    uint64_t c_tok  = ai_json_u64(usage, "completion_tokens");
    uint64_t tot    = ai_json_u64(usage, "total_tokens");
    double   cost   = ai_json_dbl(usage, "cost");
    uint64_t cached = pd ? ai_json_u64(pd, "cached_tokens") : 0;
    uint64_t cwrite = pd ? ai_json_u64(pd, "cache_write_tokens") : 0;
    uint64_t audio  = pd ? ai_json_u64(pd, "audio_tokens") : 0;
    uint64_t reason = cd ? ai_json_u64(cd, "reasoning_tokens") : 0;

    Preferences p;
    p.begin("ai_stats", false);
    p.putULong64("p_tok",     p.getULong64("p_tok", 0)     + p_tok);
    p.putULong64("c_tok",     p.getULong64("c_tok", 0)     + c_tok);
    p.putULong64("tot_tok",   p.getULong64("tot_tok", 0)   + tot);
    p.putDouble("cost",       p.getDouble("cost", 0.0)     + cost);
    p.putULong64("cached",    p.getULong64("cached", 0)    + cached);
    p.putULong64("cwrite",    p.getULong64("cwrite", 0)    + cwrite);
    p.putULong64("audio",     p.getULong64("audio", 0)     + audio);
    p.putULong64("reasoning", p.getULong64("reasoning", 0) + reason);
    /* read the totals BEFORE end(): the handle is dead afterwards */
    uint64_t tot_p = p.getULong64("p_tok", 0);
    uint64_t tot_c = p.getULong64("c_tok", 0);
    double tot_cost = p.getDouble("cost", 0.0);
    p.end();

    Serial.printf("[AI] usage +%llu/%llu tok, cost +%.8f | totals %llu/%llu, %.6f\n",
                  (unsigned long long)p_tok, (unsigned long long)c_tok, cost,
                  (unsigned long long)tot_p, (unsigned long long)tot_c, tot_cost);
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
