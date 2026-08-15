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
 * @return true on HTTP 200 + valid reply.
 */
bool openai_chat(const char *prompt, const char *base_url,
                 const char *model, const char *api_key, string &out);

#define AI_BASE_DEFAULT "https://openrouter.ai/api/v1/chat/completions"
#define AI_MODEL_DEFAULT "deepseek/deepseek-v4-flash-0731"

/**
 * @brief Read AI config (endpoint/model/key) from NVS namespace "ai".
 *        base defaults to AI_BASE_DEFAULT when not stored.
 */
void openai_load_config(char *base, int base_len, char *model, int model_len,
                        char *key, int key_len);

/** @brief Write AI config to NVS namespace "ai". */
void openai_save_config(const char *base, const char *model, const char *key);
