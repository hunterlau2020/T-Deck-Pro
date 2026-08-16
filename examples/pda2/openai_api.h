/**
 * @file      openai_api.h
 * @brief     Minimal OpenAI-compatible chat client (OpenRouter etc.).
 *            Text-only, reuses http_utils::http_post for TLS/HTTP.
 */
#pragma once

#include <string>

using namespace std;

/** One message of a chat transcript (multi-turn context). */
typedef struct {
    const char *role;       /* "user" or "assistant" */
    const char *content;    /* message text; must stay valid for the call */
} ai_message_t;

/**
 * @brief Multi-turn chat completion: messages = [system] + history +
 *        current prompt. openai_chat() is the single-turn wrapper
 *        (history_count = 0).
 * @param history       Prior turns in CHRONOLOGICAL order (oldest first).
 * @param history_count Number of entries in history (0 = single turn).
 * @param prompt        Current user message text.
 * @param base_url Full endpoint URL, e.g. "https://openrouter.ai/api/v1/chat/completions".
 * @param model   Model id (e.g. "deepseek/deepseek-chat", "openai/gpt-4o").
 * @param api_key API key (sent as "Authorization: Bearer <api_key>").
 * @param out     Filled with assistant reply on success.
 * @param timeout_ms HTTP timeout; covers connect + response read only.
 *        NOTE: TLS cert validation may first wait up to 5 s for NTP, so the
 *        caller's absolute deadline must be timeout_ms + 5000.
 * @return true on HTTP 200 + valid reply.
 */
bool openai_chat_multi(const ai_message_t *history, int history_count,
                       const char *prompt, const char *base_url,
                       const char *model, const char *api_key, string &out,
                       uint32_t timeout_ms = 30000);

/**
 * @brief Send one user turn to an OpenAI-compatible chat/completions endpoint.
 * @param prompt  User message text.
 * @param base_url Full endpoint URL, e.g. "https://openrouter.ai/api/v1/chat/completions".
 * @param model   Model id (e.g. "deepseek/deepseek-chat", "openai/gpt-4o").
 * @param api_key API key (sent as "Authorization: Bearer <api_key>").
 * @param out     Filled with assistant reply on success.
 * @param timeout_ms HTTP timeout; covers connect + response read only.
 *        NOTE: TLS cert validation may first wait up to 5 s for NTP, so the
 *        caller's absolute deadline must be timeout_ms + 5000.
 * @return true on HTTP 200 + valid reply.
 */
bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out,
                 uint32_t timeout_ms = 30000);

#define AI_BASE_DEFAULT "https://openrouter.ai/api/v1/chat/completions"
#define AI_MODEL_DEFAULT "deepseek/deepseek-v4-flash-0731"
/* v1: fixed system prompt (not user-configurable). Moving it into NVS
 * ("ai.system") is planned together with the cfg_version migration. */
#define AI_SYSTEM_PROMPT "You are a KET English examiner. Now you are going to talk to me with a special topic."
/* Device default API key (user-provided); NVS always takes precedence.
 * DEV-EXCEPTION (user decision, see SECURITY.md + memory/api-key-dev-exception):
 * a real key stays in source during development for fast verification.
 * Compensating control C1: the build warns on every compile while the
 * key is compiled in (flag set in [env:pda2]). */
#ifdef AI_KEY_DEFAULT_COMPILED
#warning "Dev-only API Key in source - rotate before pushing to a public remote"
#endif
#define AI_KEY_DEFAULT "REDACTED-OPENROUTER-KEY"

/**
 * @brief Read AI config (endpoint/model/key) from NVS namespace "ai".
 *        base defaults to AI_BASE_DEFAULT when not stored.
 * TODO(cfg_version): the "ai" namespace has no schema version yet. When a
 *        field is added/renamed (e.g. system prompt, temperature), add an
 *        "ai.cfg_version" key and migrate old values on load.
 */
void openai_load_config(char *base, int base_len, char *model, int model_len,
                        char *key, int key_len);

/** @brief Write AI config to NVS namespace "ai".
 *
 *  Dual-slot + single active-version key (copilot finding 1.2): all three
 *  fields are staged into the INACTIVE slot and read back for verification;
 *  the commit point is one atomic putUChar("active") flip. A failure
 *  before the flip leaves the previous slot untouched, so a failed save
 *  can never leave a mixed config observable - even across power loss.
 *
 *  @param err Optional human-readable failure reason ("NVS write failed" /
 *             "NVS commit failed"); unchanged on success.
 *  @return true when the new slot was fully staged AND committed. */
bool openai_save_config(const char *base, const char *model, const char *key,
                        const char **err = NULL);
