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

bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out,
                 uint32_t timeout_ms)
{
    if (!prompt || !base_url || !model || !api_key) return false;
    if (!http_require_wifi("AI")) return false;

    /* Request shape mirrors the OpenRouter curl reference:
     * {"model":..., "temperature":0.7, "reasoning":{"exclude":true},
     *  "messages":[{"role":"system",...},{"role":"user","content":<prompt>}]}
     * cJSON_AddStringToObject performs proper JSON string escaping, so
     * quotes/backslashes/newlines in the user prompt cannot break the body. */
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
    cJSON *sys = cJSON_CreateObject();
    if (!sys) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddStringToObject(sys, "role", "system");
    cJSON_AddStringToObject(sys, "content", AI_SYSTEM_PROMPT);
    cJSON_AddItemToArray(msgs, sys);

    cJSON *msg = cJSON_CreateObject();
    if (!msg) {
        cJSON_Delete(root);
        return false;
    }
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", prompt);
    cJSON_AddItemToArray(msgs, msg);
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
    cJSON *choices = cJSON_GetObjectItem(j, "choices");
    cJSON *c0 = choices ? cJSON_GetArrayItem(choices, 0) : NULL;
    cJSON *msg0 = c0 ? cJSON_GetObjectItem(c0, "message") : NULL;
    cJSON *content = msg0 ? cJSON_GetObjectItem(msg0, "content") : NULL;
    bool ok = (content != NULL) && cJSON_IsString(content) && content->valuestring != NULL;
    if (ok) out = content->valuestring;
    cJSON_Delete(j);
    return ok;
}
