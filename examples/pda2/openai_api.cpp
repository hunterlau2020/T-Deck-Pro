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
    Preferences p;
    p.begin("ai", false);
    size_t b = p.putString("base",  base  ? base  : "");
    size_t m = p.putString("model", model ? model : "");
    size_t k = p.putString("key",   key   ? key   : "");
    p.end();
    return b > 0 && m > 0 && k > 0;
}

bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out)
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
                                     "application/json", auth, 30000);
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
