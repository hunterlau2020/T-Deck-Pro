# PDA2 Review Result — 2026-08-07 20:00

The malformed CA bundle prevents verified HTTPS from working, which breaks the newly added AI functionality and existing HTTP consumers. The patch also weakens CI coverage and leaves the GPS snapshot synchronization incomplete.

## Findings

### P1 — Replace the malformed CA certificate

**File:** `examples/pda2/http_utils.cpp:24-26`

With the new default CA-verification mode, `setCACert` receives invalid PEM: its ASN.1 header declares a 1387-byte DER certificate, while the bundled Base64 decodes to only 1331 bytes. mbedTLS cannot parse this trust anchor, so HTTPS requests fail, breaking the added OpenRouter chat and existing `http_get` consumers until a complete valid CA bundle is installed.

### P2 — Preserve CI's selected source directory

**File:** `script/set_srcdir.py:26`

When the existing GitHub Actions matrix runs, it exports `PLATFORMIO_SRC_DIR` for each example, but this replacement subsequently forces every default `T-Deck-Pro` invocation back to `examples/test_GPS`. The matrix will therefore report successful builds while no longer compiling factory, EPD, WiFi, or the other selected examples; preserve the externally supplied source directory or update the workflow to select the new environments.

### P2 — Synchronize GPS writers with the snapshot lock

**File:** `examples/pda2/peri_gps.cpp:131-143`

On the dual-core ESP32, `gps_task` can execute `displayInfo()` on the other core while this reader holds the mux, because the writes to every `gps_*` field take no matching lock. Consequently `gps_get_snapshot()` can still return mixed readings from different GPS updates, despite its atomic-snapshot contract; protect publication in `displayInfo()` with the same mux, preferably by copying a prepared local struct.

### P2 — Implement the advertised TLS bypass control

**File:** `examples/pda2/ui_ai_cfg.cpp:46-49`

For a user configuring a self-signed or privately rooted endpoint, this screen only persists base URL, model, and key, and no code calls `http_set_tls_mode`. Such requests therefore always use CA verification and fail, even though `http_utils.h` advertises an AI Config “Trust self-signed” control; persist the setting and apply it before AI requests.
