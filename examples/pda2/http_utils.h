/**
 * @file      http_utils.h
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Shared HTTPS request utilities for network-connected features.
 */
#pragma once

#include <stdint.h>
#include <string>

using namespace std;

class WiFiClientSecure;   /* reference params below only */

typedef enum {
    HTTP_TLS_CA_VERIFY = 0,   /* validate chain against built-in CA bundle */
    HTTP_TLS_INSECURE  = 1,   /* skip verification (user opted in) */
} http_tls_mode_t;

typedef struct {
    int status_code;
    string body;
    bool success;
    string error;       /* human-readable failure detail (TLS/time/etc.) */
} http_response_t;

/**
 * @brief Set TLS verification mode applied to all subsequent http_get/post calls.
 *        Default is HTTP_TLS_CA_VERIFY. On CA auth failure, response success=false
 *        is returned (no fallback to insecure).
 *
 *        Built-in CA bundle (reviewer #2 fix):
 *          - ISRG Root X1 (Let's Encrypt) — covers openrouter.ai, *.openrouter.ai
 *          - DigiCert Global Root G2       — covers many commercial CDNs
 *          - GlobalSign Root R1            — covers some EU/AS CN endpoints
 *        If the user's custom endpoint is signed by an unknown CA, they must
 *        either point end-point to a known CA or set HTTP_TLS_INSECURE via
 *        the AI Cfg screen "Trust self-signed" toggle.
 */
void http_set_tls_mode(http_tls_mode_t mode);
http_tls_mode_t http_get_tls_mode(void);

/**
 * @brief Check WiFi connectivity (STA connected).
 *        Does NOT show any UI by itself: the CALLER is responsible for
 *        showing user feedback when this returns false.
 * @param feature_name Name of the feature requesting WiFi (log context).
 * @return true if WiFi is connected.
 */
bool http_require_wifi(const char *feature_name);

/**
 * @brief Configure a WiFiClientSecure per the current TLS mode (built-in CA
 *        bundle, or insecure when the user opted in).
 *        Exported for penpal_api's own https transport (design §3.3) so both
 *        paths share ONE CA bundle and policy instead of a drifting copy.
 *        ADDITIVE export: every existing http_* caller is unchanged.
 */
void http_apply_tls(WiFiClientSecure &client);

/**
 * @brief Wait (bounded) for NTP time sync - TLS cert validation needs a sane
 *        clock. Exported for penpal_api's https path, same single-policy
 *        rationale as http_apply_tls(). ADDITIVE export.
 * @param max_wait_ms Upper bound on the wait.
 * @return true when system time is past the 2023-11 sanity threshold.
 */
bool http_ensure_time(uint32_t max_wait_ms);

/**
 * @brief Perform an HTTPS GET request.
 * @param url Full URL to fetch.
 * @param timeout_ms Request timeout in milliseconds (default 10000).
 * @return http_response_t with status_code, body, and success flag.
 */
http_response_t http_get(const char *url, uint32_t timeout_ms = 10000);

/**
 * @brief Perform an HTTPS GET with an explicit User-Agent header.
 *        Some endpoints (e.g. ifconfig.me) return HTML to unknown/browser
 *        agents and plain text to curl-like agents.
 *
 *        UA policy: a User-Agent is ONLY injected through this function,
 *        for endpoints known to switch on it. All other http_* calls send
 *        the HTTPClient default headers - there is no global curl UA.
 * @param url Full URL to fetch.
 * @param user_agent User-Agent value (NULL/empty = default).
 * @param timeout_ms Request timeout in milliseconds (default 10000).
 * @return http_response_t with status_code, body, success and error.
 */
http_response_t http_get_ua(const char *url, const char *user_agent,
                            uint32_t timeout_ms = 10000);

/**
 * @brief Perform an HTTPS GET with an Authorization header
 *        (e.g. "Bearer <key>" for API endpoints).
 * @param url Full URL to fetch.
 * @param auth_header Authorization header value (NULL/empty = skip).
 * @param timeout_ms Request timeout in milliseconds (default 10000).
 * @return http_response_t with status_code, body, success and error.
 */
http_response_t http_get_auth(const char *url, const char *auth_header,
                              uint32_t timeout_ms = 10000);

/**
 * @brief Perform an HTTPS POST request.
 * @param url Full URL to post to.
 * @param body Request body content.
 * @param content_type Content-Type header value.
 * @param auth_header Optional Authorization header value (empty string to skip).
 * @param timeout_ms Request timeout in milliseconds (default 15000).
 * @return http_response_t with status_code, body, and success flag.
 */
http_response_t http_post(const char *url, const string &body,
                          const char *content_type = "application/json",
                          const char *auth_header = "",
                          uint32_t timeout_ms = 15000);

/**
 * @brief Perform an HTTPS POST with raw binary/large body using streaming write.
 * @param url Full URL to post to.
 * @param data Pointer to raw data.
 * @param data_len Length of data.
 * @param content_type Content-Type header value.
 * @param timeout_ms Request timeout in milliseconds.
 * @return http_response_t with status_code, body, and success flag.
 */
http_response_t http_post_large(const char *url, const uint8_t *data, size_t data_len,
                                const char *content_type = "application/json",
                                uint32_t timeout_ms = 30000);

/**
 * @brief Base64 encode data using mbedTLS (PSRAM-allocated output).
 * @param data Input data pointer.
 * @param len Input data length.
 * @param out_len Output: length of encoded string.
 * @return PSRAM-allocated base64 string (caller must free with free()).
 */
char *base64_encode_psram(const uint8_t *data, size_t len, size_t *out_len);
