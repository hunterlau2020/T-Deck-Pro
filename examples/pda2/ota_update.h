/**
 * @file      ota_update.h
 * @brief     Signed OTA firmware update (docs/ota-update-design.md v6).
 *
 * Contract highlights (v6):
 *  - Result channel carries POINTERS only (worker new → xQueueOverwrite →
 *    ota_result_poll drains every loop tick → consumer deletes exactly
 *    once on every path - consumed / stale / no-callback / overwritten).
 *  - Single-flight s_ota_inflight (cap 1): incremented AFTER a successful
 *    xTaskCreate, decremented at the worker's SINGLE exit point (paired
 *    with the hw-lock release); a second op is refused while in flight.
 *  - Manifest verification: six-field canonical byte string (each value +
 *    '\n', UTF-8; size/seq decimal no leading zeros; sha256 lowercase hex)
 *    signed ECDSA P-256, IEEE P1363 r||s, verified with mbedtls (r/s via
 *    mbedtls_mpi + mbedtls_ecdsa_verify; read_signature forbidden).
 *  - Download: streaming HTTPClient → Update.write with incremental
 *    SHA-256; read-idle 45 s + absolute 10 min deadline (task-local);
 *    size vs free slot; Content-Length mandatory; failure → Update.abort();
 *    the signed manifest snapshot travels by ownership hand-off (no
 *    re-fetch between check and flash - "confirmed package == flashed
 *    package").
 *  - last_seq persists ONLY after Update.end(true) succeeded.
 */
#pragma once

#include <stdint.h>
#include <string>

using namespace std;

/* Device-side app version reported to the user (design §11: version
 * compare is string INEQUALITY, not ordering). Bump with each release. */
#define OTA_APP_VERSION "v2.6-260915"

/* Allow plain-http OTA sources. OFF by default: the manifest signature
 * protects integrity, but plaintext transport leaks the firmware image
 * and URL; enable only for bench testing on an isolated network. */
// #define OTA_ALLOW_PLAIN_HTTP

typedef struct {
    string version;
    string url;
    uint64_t size;
    uint8_t sha256[32];
    uint32_t seq;
    string notes;
    uint8_t sig[64];                    /* IEEE P1363 r||s */
} ota_manifest_t;

typedef enum {
    OTA_RESULT_CHECK = 0,               /* manifest check finished */
    OTA_RESULT_UPDATE,                  /* flash write finished */
} ota_result_kind_t;

/* Heap-allocated by the worker; ownership moves to the consumer, which
 * must delete it exactly once (queue carries ota_result_t* POINTERS). */
typedef struct {
    uint32_t gen;                       /* screen generation at launch */
    ota_result_kind_t kind;
    bool ok;
    /* CHECK: ok + has_update -> manifest holds the verified update;
     * ok + !has_update -> already current (up_to_date note in err);
     * !ok -> err holds the failure line. UPDATE: ok -> reboot pending;
     * !ok -> err holds the failure line. */
    bool has_update;
    ota_manifest_t manifest;
    string err;
} ota_result_t;

/** @brief Kick an async manifest check (network + verify in the worker).
 *  @param gen screen generation, stamped into the result.
 *  @return false when refused locally (op in flight / no URL). */
bool ota_check_async(uint32_t gen);

/** @brief Flash a VERIFIED manifest (ownership of snap moves to the task;
 *  caller must NOT use or free it afterwards - also on false return when
 *  the task could not start, the snapshot is deleted here).
 *  @return false when refused locally (op in flight). */
bool ota_start_async(uint32_t gen, ota_manifest_t *snap);

/** @brief Drain results every loop() tick UNCONDITIONALLY (design §3.3:
 *  drain-then-active, PenPal penpal_keyboard_poll pattern). Forwards to
 *  the registered consumer (gen-matched); deletes on every other path. */
void ota_result_poll(void);

/** @brief Consumer registration by the OTA UI screen (create registers,
 *  destroy clears). NULL/!active results are deleted inside the poll. */
void ota_set_consumer(void (*cb)(ota_result_t *));

/** @brief UI-thread prechecks WITHOUT network: OTA_URL configured,
 *  battery >= 30% (or external power; gauge-invalid falls back to
 *  get_input()). Fills err on refusal. */
bool ota_precheck_ui(char *err, int err_len);

/** @brief Any OTA operation in flight (worker running) or hw held. Used
 *  by Sleep entry / Shutdown confirm / low-voltage force-off to refuse
 *  or postpone while flash is being written. */
bool ota_busy(void);

/** @brief True while busy AND inside the 10-minute safety-valve window
 *  (design §4: low-voltage shutdown is suppressed for at most ONE
 *  deadline window - past it, shutdown is allowed through even if the
 *  worker somehow still runs). */
bool ota_shutdown_blocked(void);

/** @brief Ask the running worker to stop at the next chunk boundary
 *  (check-stage Cancel; the write path is not user-interruptible by
 *  design - tokens act at block edges only). */
void ota_request_cancel(void);

/** @brief Download progress, 0..100 (atomic poll, design §6). */
int ota_progress_percent(void);

/** @brief Device version string for display. */
const char *ota_current_version(void);
