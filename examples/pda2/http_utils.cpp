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
    "-----END CERTIFICATE-----\n"
    /* ISRG Root YR (Let's Encrypt 2026 hierarchy; cross-signed by ISRG Root X1.
     * Sites on the YR1 intermediate (e.g. ifconfig.me) fail with only X1. */
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n"
    /* DigiCert Global Root G2 (Cloudflare Universal SSL chains) */
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n"
    /* GlobalSign Root CA - R3 (misc commercial endpoints) */
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDXzCCAkegAwIBAgILBAAAAAABIVhTCKIwDQYJKoZIhvcNAQELBQAwTDEgMB4G\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjMxEzARBgNVBAoTCkdsb2JhbFNp\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDkwMzE4MTAwMDAwWhcNMjkwMzE4\n"
    "MTAwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMzETMBEG\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAMwldpB5BngiFvXAg7aEyiie/QV2EcWtiHL8\n"
    "RgJDx7KKnQRfJMsuS+FggkbhUqsMgUdwbN1k0ev1LKMPgj0MK66X17YUhhB5uzsT\n"
    "gHeMCOFJ0mpiLx9e+pZo34knlTifBtc+ycsmWQ1z3rDI6SYOgxXG71uL0gRgykmm\n"
    "KPZpO/bLyCiR5Z2KYVc3rHQU3HTgOu5yLy6c+9C7v/U9AOEGM+iCK65TpjoWc4zd\n"
    "QQ4gOsC0p6Hpsk+QLjJg6VfLuQSSaGjlOCZgdbKfd/+RFO+uIEn8rUAVSNECMWEZ\n"
    "XriX7613t2Saer9fwRPvm2L7DWzgVGkWqQPabumDk3F2xmmFghcCAwEAAaNCMEAw\n"
    "DgYDVR0PAQH/BAQDAgEGMA8GA1UdEwEB/wQFMAMBAf8wHQYDVR0OBBYEFI/wS3+o\n"
    "LkUkrk1Q+mOai97i3Ru8MA0GCSqGSIb3DQEBCwUAA4IBAQBLQNvAUKr+yAzv95ZU\n"
    "RUm7lgAJQayzE4aGKAczymvmdLm6AC2upArT9fHxD4q/c2dKg8dEe3jgr25sbwMp\n"
    "jjM5RcOO5LlXbKr8EpbsU8Yt5CRsuZRj+9xTaGdWPoO4zzUhw8lo/s7awlOqzJCK\n"
    "6fBdRoyV3XpYKBovHd7NADdBj+1EbddTKJd+82cEHhXXipa0095MJ6RMG3NzdvQX\n"
    "mcIfeg7jLQitChws/zyrVQ4PkX4268NXSb7hLi18YIvDQVETI53O9zJrlAGomecs\n"
    "Mx86OyXShkDOOyyGeMlhLxS67ttVb9+E7gUJTb0o2HLO02JQZR7rkpeDMdmztcpH\n"
    "WD9f\n"
    "-----END CERTIFICATE-----\n"
    /* GTS Root R4 (Google Trust Services; openrouter.ai chains via WE1). */
    "-----BEGIN CERTIFICATE-----\n"
    "MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYDVQQGEwJVUzEi\n"
    "MCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQw\n"
    "HhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZ\n"
    "R29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjO\n"
    "PQIBBgUrgQQAIgNiAATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzu\n"
    "hXyiQHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvRHYqjQjBA\n"
    "MA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQWBBSATNbrdP9JNqPV2Py1\n"
    "PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/C\n"
    "r8deVl5c1RxYIigL9zC2L7F8AjEA8GE8p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh\n"
    "4rsUecrNIdSUtUlD\n"
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

/* TLS cert validation needs a sane system clock; pool.ntp.org is often
 * unreachable in CN networks, so prefer cn.pool.ntp.org and fail requests
 * with a clear message instead of a generic TLS verify error. */
static bool http_ensure_time(uint32_t max_wait_ms)
{
    if (time(nullptr) > 1700000000) {           /* after 2023-11-14 */
        return true;
    }
    Serial.println("[HTTP] system time not synced - requesting NTP");
    configTzTime("CST-8", "cn.pool.ntp.org", "pool.ntp.org", "time.nist.gov");
    uint32_t t0 = millis();
    while (time(nullptr) <= 1700000000 && millis() - t0 < max_wait_ms) {
        delay(100);
    }
    bool ok = (time(nullptr) > 1700000000);
    Serial.printf("[HTTP] NTP retry %s (epoch=%ld)\n", ok ? "ok" : "failed", (long)time(nullptr));
    return ok;
}

static void http_capture_error(http_response_t &resp, HTTPClient &http,
                               WiFiClientSecure &client)
{
    char err[128] = {0};
    client.lastError(err, sizeof(err));
    if (err[0] != '\0') {
        resp.error = err;
    } else {
        resp.error = http.errorToString(resp.status_code).c_str();
    }
}

http_response_t http_get(const char *url, uint32_t timeout_ms)
{
    return http_get_ua(url, NULL, timeout_ms);
}

http_response_t http_get_ua(const char *url, const char *user_agent, uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false, ""};
    if (s_tls_mode != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        resp.status_code = -3;
        resp.error = "Time not synced - retry after NTP";
        resp.body = resp.error;
        return resp;
    }
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        char err[128] = {0};
        client.lastError(err, sizeof(err));
        resp.error = err[0] ? err : resp.body;
        return resp;
    }

    if (user_agent && user_agent[0] != '\0') {
        http.addHeader("User-Agent", user_agent);
    }

    resp.status_code = http.GET();
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
        http_capture_error(resp, http, client);
    }
    http.end();
    return resp;
}

http_response_t http_get_auth(const char *url, const char *auth_header, uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false, ""};
    if (s_tls_mode != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        resp.status_code = -3;
        resp.error = "Time not synced - retry after NTP";
        resp.body = resp.error;
        return resp;
    }
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        char err[128] = {0};
        client.lastError(err, sizeof(err));
        resp.error = err[0] ? err : resp.body;
        return resp;
    }

    if (auth_header && auth_header[0] != '\0') {
        http.addHeader("Authorization", auth_header);
    }

    resp.status_code = http.GET();
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
        http_capture_error(resp, http, client);
    }
    http.end();
    return resp;
}

http_response_t http_post(const char *url, const string &body,
                          const char *content_type,
                          const char *auth_header,
                          uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false, ""};
    if (s_tls_mode != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        resp.status_code = -3;
        resp.error = "Time not synced - retry after NTP";
        resp.body = resp.error;
        return resp;
    }
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        char err[128] = {0};
        client.lastError(err, sizeof(err));
        resp.error = err[0] ? err : resp.body;
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
        http_capture_error(resp, http, client);
    }
    http.end();
    return resp;
}

http_response_t http_post_large(const char *url, const uint8_t *data, size_t data_len,
                                const char *content_type, uint32_t timeout_ms)
{
    http_response_t resp = {0, "", false, ""};
    if (s_tls_mode != HTTP_TLS_INSECURE && !http_ensure_time(5000)) {
        resp.status_code = -3;
        resp.error = "Time not synced - retry after NTP";
        resp.body = resp.error;
        return resp;
    }
    WiFiClientSecure client;
    apply_tls(client);
    HTTPClient http;

    http.setTimeout(timeout_ms);
    if (!http.begin(client, url)) {
        resp.body = (s_tls_mode == HTTP_TLS_INSECURE) ? "Failed to connect" : "Failed to connect (TLS)";
        char err[128] = {0};
        client.lastError(err, sizeof(err));
        resp.error = err[0] ? err : resp.body;
        return resp;
    }

    http.addHeader("Content-Type", content_type);

    resp.status_code = http.sendRequest("POST", (uint8_t *)data, data_len);
    if (resp.status_code > 0) {
        resp.body = http.getString().c_str();
        resp.success = (resp.status_code >= 200 && resp.status_code < 300);
    } else {
        resp.body = http.errorToString(resp.status_code).c_str();
        http_capture_error(resp, http, client);
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
http_response_t http_get(const char *url, uint32_t timeout_ms) { return {0, "Not supported on desktop", false, ""}; }
http_response_t http_get_ua(const char *url, const char *user_agent, uint32_t timeout_ms) { return {0, "Not supported on desktop", false, ""}; }
http_response_t http_get_auth(const char *url, const char *auth_header, uint32_t timeout_ms) { return {0, "Not supported on desktop", false, ""}; }
http_response_t http_post(const char *url, const string &body, const char *content_type, const char *auth_header, uint32_t timeout_ms) { return {0, "Not supported on desktop", false, ""}; }
http_response_t http_post_large(const char *url, const uint8_t *data, size_t data_len, const char *content_type, uint32_t timeout_ms) { return {0, "Not supported on desktop", false, ""}; }
char *base64_encode_psram(const uint8_t *data, size_t len, size_t *out_len) { *out_len = 0; return NULL; }
#endif
