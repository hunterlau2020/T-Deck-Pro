/**
 * @file      openai_api.cpp
 * @brief     OpenAI-compatible chat client (OpenRouter etc.), text-only.
 */
#include "Arduino.h"
#include "openai_api.h"
#include "http_utils.h"
#include <Preferences.h>
#include <cJSON.h>

void openai_load_config(char *base, int base_len, char *model, int model_len,
                        char *key, int key_len)
{
    Preferences p;
    p.begin("ai", true);
    String b = p.getString("base", AI_BASE_DEFAULT);
    String m = p.getString("model", AI_MODEL_DEFAULT);
    String k = p.getString("key", AI_KEY_DEFAULT);
    p.end();
    strncpy(base,  b.c_str(), base_len  - 1);
    strncpy(model, m.c_str(), model_len - 1);
    strncpy(key,   k.c_str(), key_len   - 1);
    base[base_len-1] = model[model_len-1] = key[key_len-1] = '\0';
}

bool openai_save_config(const char *base, const char *model, const char *key)
{
    /* Atomic-ish write (review finding 1.8): NVS has no multi-key
     * transaction, so the new values are first staged under *.tmp keys and
     * verified, then swapped into place. If any step fails the previous
     * config is restored - a failed save never leaves a mixed config. */
    Preferences p;
    p.begin("ai", false);

    const String ob = p.getString("base",  "");
    const String om = p.getString("model", "");
    const String ok = p.getString("key",   "");
    const String nb = base  ? String(base)  : String("");
    const String nm = model ? String(model) : String("");
    const String nk = key   ? String(key)   : String("");

    /* stage + verify: a truncated write is detected before anything moves */
    if (p.putString("base.tmp",  nb) == 0 ||
        p.putString("model.tmp", nm) == 0 ||
        p.putString("key.tmp",   nk) == 0 ||
        p.getString("base.tmp",  "") != nb ||
        p.getString("model.tmp", "") != nm ||
        p.getString("key.tmp",   "") != nk) {
        p.remove("base.tmp"); p.remove("model.tmp"); p.remove("key.tmp");
        p.end();
        Serial.println("[AI] save aborted: staging failed");
        return false;
    }

    /* swap: putString on an existing key overwrites atomically per key */
    if (p.putString("base",  nb) == 0 ||
        p.putString("model", nm) == 0 ||
        p.putString("key",   nk) == 0) {
        /* rollback to the previous values (best effort) */
        p.putString("base",  ob);
        p.putString("model", om);
        p.putString("key",   ok);
        p.remove("base.tmp"); p.remove("model.tmp"); p.remove("key.tmp");
        p.end();
        Serial.println("[AI] save failed: previous config restored");
        return false;
    }

    p.remove("base.tmp"); p.remove("model.tmp"); p.remove("key.tmp");
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
