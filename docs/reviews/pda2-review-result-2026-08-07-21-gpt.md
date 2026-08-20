# PDA2 Review Result — 2026-08-07 onward (GPT)

## Scope

Reviewed the 178 commits on `HD-V2-250915` dated from 2026-08-07 onward.

## Findings

### P2 — Do not cache a partial weather refresh as successful

**File:** `examples/pda2/ui_weather.cpp:428`

If the current-weather request fails while an older cache keeps `data_valid` true, or if the forecast request fails after the current-weather request succeeds, the code still updates `last_fetch_time` and saves the mixed old/new state. The application then reports success and will not automatically retry for one hour. Track the two request outcomes separately; only advance the freshness timestamp after a complete refresh, and permit an earlier retry after a partial refresh.

### P2 — Trigger CI when the source-directory selector changes

**Files:** `script/set_srcdir.py:26`, `.github/workflows/platformio.yml:7`

`script/set_srcdir.py` determines which example the PlatformIO matrix actually builds, but the workflow path filter does not include `script/**`. A change to this critical selector alone therefore skips CI and can again make the matrix silently build the wrong source directory. Include `script/set_srcdir.py` or `script/**` in the workflow's `on.push.paths` list.

### P2 — Correct the TLS initializer declaration

**File:** `examples/pda2/factory.ino:757`

`openai_tls_apply` is declared here as returning `bool`, while its header and implementation return `void`. The current call discards the return value and will commonly link, but the declarations are incompatible across translation units. Include `openai_api.h` or change the local declaration to `extern void openai_tls_apply(void);`.

## Verification

- CA bundle check passed: all 6 root certificates parsed successfully.
- NVS dual-slot atomic-save state-machine test passed: all 11 cases.
- PlatformIO firmware build was not run because `pio` is unavailable in the review environment.
