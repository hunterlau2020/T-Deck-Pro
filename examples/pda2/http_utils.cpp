/**
 * @file      http_utils.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     Shared HTTPS request utilities implementation.
 */
#include "http_utils.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

static http_tls_mode_t s_tls_mode = HTTP_TLS_CA_VERIFY;

/* Built-in CA bundle (reviewer #2 fix). These root certs cover the default
 * OpenRouter endpoint (Let's Encrypt R10 / ISRG Root X1) and most public
 * HTTPS endpoints. PEM strings may be chunked back-to-back; WiFiClientSecure
 * accepts concatenated PEMs in a single setCACert call. */
static const char *CA_BUNDLE =
    /* ISRG Root X1 (Let's Encrypt) */
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

void http_set_tls_mode(http_tls_mode_t mode) { s_tls_mode = mode; }
http_tls_mode_t http_get_tls_mode(void) { return s_tls_mode; }

/* Configure a WiFiClientSecure instance per the current TLS mode. */
static void apply_tls(WiFiClientSecure &client)
{
    if (s_tls_mode == HTTP_TLS_INSECURE) {
        client.setInsecure();
    } else {
        client.setCACert(CA_BUNDLE);
    }
}

bool http_require_wifi(const char *feature_name)
{
    return WiFi.status() == WL_CONNECTED;
}

http_response_t http_get(const char *url, uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false};
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        return resp;
    }

    resp.status_code = http.GET();
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
    }
    http.end();
    return resp;
}

http_response_t http_post(const char *url, const string &body,
                          const char *content_type,
                          const char *auth_header,
                          uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false};
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        return resp;
    }

    http.addHeader("Content-Type", content_type);
    if (auth_header && auth_header[0] != '\0') {
        http.addHeader("Authorization", auth_header);
    }

    resp.status_code = http.POST(body.c_str());
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
    }
    http.end();
    return resp;
}

http_response_t http_post_large(const char *url, const uint8_t *data, size_t data_len,
                                const char *content_type, uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false};
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        return resp;
    }

    http.addHeader("Content-Type", content_type);

    resp.status_code = http.sendRequest("POST", (uint8_t *)data, data_len);
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
    }
    http.end();
    return resp;
}

char *base64_encode_psram(const uint8_t *data, size_t len, size_t *out_len)
{
    size_t encoded_len = 0;
    mbedtls_base64_encode(NULL, 0, &encoded_len, data, len);

    char *encoded = (char *)heap_caps_malloc(encoded_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!encoded) {
        *out_len = 0;
        return NULL;
    }

    mbedtls_base64_encode((unsigned char *)encoded, encoded_len + 1, &encoded_len, data, len);
    encoded[encoded_len] = '\0';
    *out_len = encoded_len;
    return encoded;
}

#else
// Desktop stubs
void http_set_tls_mode(http_tls_mode_t mode) {}
http_tls_mode_t http_get_tls_mode(void) { return HTTP_TLS_INSECURE; }
bool http_require_wifi(const char *feature_name) { return false; }
http_response_t http_get(const char *url, uint32_t timeout_ms) { return {0, "Not supported on desktop", false}; }
http_response_t http_post(const char *url, const string &body, const char *content_type, const char *auth_header, uint32_t timeout_ms) { return {0, "Not supported on desktop", false}; }
http_response_t http_post_large(const char *url, const uint8_t *data, size_t data_len, const char *content_type, uint32_t timeout_ms) { return {0, "Not supported on desktop", false}; }
char *base64_encode_psram(const uint8_t *data, size_t len, size_t *out_len) { *out_len = 0; return NULL; }
#endif
