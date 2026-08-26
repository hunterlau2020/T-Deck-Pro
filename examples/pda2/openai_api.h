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

#define AI_BASE_DEFAULT "https://openrouter.ai/api/v1"
#define AI_MODEL_DEFAULT "deepseek/deepseek-v4-flash-0731"
/* v1: fixed system prompt (not user-configurable). Moving it into NVS
 * ("ai.system") is planned together with the cfg_version migration. */
#define AI_SYSTEM_PROMPT "You are a KET English examiner. Now you are going to talk to me with a special topic."
/* API key resolution (SECURITY.md): NVS -> device /env.cfg -> gitignored
 * config_keys.h (AI_KEY_DEFAULT_DEV) -> empty. TRACKED source carries no
 * real key - the compile-time default is empty. */
#define AI_KEY_DEFAULT ""

/**
 * @brief Read AI config (endpoint/model/key) from NVS namespace "ai".
 *        base defaults to AI_BASE_DEFAULT when not stored.
 * TODO(cfg_version): the "ai" namespace has no schema version yet. When a
 *        field is added/renamed (e.g. system prompt, temperature), add an
 *        "ai.cfg_version" key and migrate old values on load.
 */
void openai_load_config(char *base, int base_len, char *model, int model_len,
                        char *key, int key_len);

/** @brief Flush dirty usage statistics to NVS immediately.
 *  The regular accumulate path throttles commits (60 s / 20 responses);
 *  lifecycle checkpoints must call this before deep sleep, on screen
 *  destroy and on New so low-frequency sessions still persist. */
void openai_stats_flush(void);

/** @brief Time-based throttle tick; call once per loop().
 *  Enforces the 60 s persist window even when no further responses
 *  arrive (no-op when nothing is dirty or the window has not elapsed). */
void openai_stats_poll(void);

/** @brief Format the accumulated usage for display (two lines: chat and
 *  test groups). Safe to call from the UI thread. */
void openai_stats_text(char *buf, int buf_len);

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

/** @brief TLS trust mode ("trust self-signed") persisted as ONE independent
 *  NVS key "tls_insecure" in namespace "ai" — NOT part of the dual-slot
 *  config: it is a device-level transport setting applied globally via
 *  http_set_tls_mode() to every http_utils consumer.
 *  openai_tls_apply() runs once in setup(); openai_tls_set() persists +
 *  applies and returns false on NVS failure (caller should revert the UI
 *  control to the persisted state). */
bool openai_tls_insecure(void);
void openai_tls_apply(void);
bool openai_tls_set(bool insecure);

/* --------------------------------------------------------------------------
 * Provider registry shared between AI Config and PenPal.
 * A provider is a named tuple of (base_url, model).  The API key is resolved
 * at runtime from the active AI Config slot or per-provider NVS overrides,
 * so it is NOT part of the registry entry.
 * -------------------------------------------------------------------------- */

/** @brief One static provider descriptor. */
typedef struct {
    const char *name;     /**< Internal id, e.g. "openrouter". */
    const char *label;    /**< Human label, e.g. "OpenRouter". */
    const char *base_url; /**< Endpoint base, e.g. "https://openrouter.ai/api/v1". */
    const char *model;    /**< Default model id for this provider. */
} ai_provider_info_t;

/** @return Number of built-in providers. */
int ai_provider_count(void);

/**
 * @brief Copy provider descriptor at index into @p out.
 * @param idx 0 .. ai_provider_count()-1.
 * @param out Filled on success; pointers point to static strings.
 * @return true if idx is valid.
 */
bool ai_provider_enum(int idx, ai_provider_info_t *out);

/**
 * @brief Find a provider by internal name.
 * @return Its index, or -1 if not found.
 */
int ai_provider_find(const char *name);

/**
 * @brief Resolve the effective config for a named provider.
 *
 * Resolution is a two-way branch (matches design-penpal-ai-provider-link
 * §3.1; the active slot is consulted FIRST, it does not layer over the
 * registry - a matching base means the user saved this provider in AI
 * Config and the whole slot wins including its key, env.cfg is then NOT
 * consulted):
 *   A. Active AI Config slot base == registry base_url:
 *      use the slot's base/model/key verbatim (user-customised).
 *   B. Otherwise: registry base/model + key chain
 *      NVS "ai"/key.<name> -> /env.cfg <NAME>_KEY ->
 *      (openrouter only) AI_KEY_DEFAULT_DEV -> empty.
 *
 * @param name Provider name ("openrouter", "deepseek", ...).
 * @param base  Output buffer for endpoint URL.
 * @param model Output buffer for model id.
 * @param key   Output buffer for API key.
 * @return true if the provider exists; buffers are always written (possibly
 *         empty strings when no key is configured).
 */
bool ai_provider_get(const char *name, char *base, int base_len,
                     char *model, int model_len,
                     char *key, int key_len);
