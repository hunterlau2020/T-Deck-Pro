/**
 * @file      openai_api.h
 * @brief     Minimal OpenAI-compatible chat client (OpenRouter etc.).
 *            Text-only, reuses http_utils::http_post for TLS/HTTP.
 */
#pragma once

#include <string>

using namespace std;

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
 * NOTE: this key is committed to the repository - rotate it if the
 * repository becomes public or the key is compromised. */
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
 *  @return true when all fields were written (a failed write leaves the
 *          previous values in place - callers must not treat partial
 *          writes as success). */
bool openai_save_config(const char *base, const char *model, const char *key);
