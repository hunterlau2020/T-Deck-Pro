# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

LilyGo **T-Deck-Pro V1.1** firmware repository. ESP32-S3, 16MB Flash + 8MB PSRAM, GDEQ031T10 3.1" E-Paper (240×320 monochrome), TCA8418 4×10 keypad, CST226SE touch. Targets are independent example sketches under `examples/`; `pda2` is the active PDA application (built on top of `factory`). The consolidated `examples/allinone` was **cancelled by user decision (2026-08-19)** — pda2 is the final integrated firmware (2-page 18-entry menu); `docs/allinone-design.md` is archived as pda2-evolution reference. **Hardware gotchas (issue_list §3)**: the 4G (A7682E) board variant has NO PCM5102A DAC (MP3 playback impossible, verified by `test_i2s_probe`); SD cards must be FAT16/FAT32 (exFAT shows 0MB, Setting screen now hints this).

## Critical conventions (load-bearing)

These come from prior session memory and review rounds; treat them as hard rules:

1. **Code-review workflow** (see `docs/reviews/README.md` for merge flow; full rationale in Claude memory):
   - Every change goes through a review-request file under `docs/reviews/` named `wifi-config-keyboard-review-request-<commit-id>.md`. Filename is the commit (or range) id.
   - Commits must be split per module (keypad driver / WiFi config / AI chat each in its own commit). Co-Authored-By line is `Claude <noreply@anthropic.com>`.
   - Existing review result files are **never overwritten** — superseded request files can be merged into a range request, but results stand.
2. **Secrets chain** (no real key in tracked source since 2026-08-17):
   - Lookup order: NVS → SPIFFS `/env.cfg` (parsed by `examples/pda2/env_secrets.cpp`) → gitignored `config_keys.h` → empty default. `env.cfg.example` is tracked; the real `/env.cfg` never is.
   - OLDER COMMITS still contain a real OpenRouter key string in `openai_api.h` history — treat it as compromised; `git filter-repo` before any public push (GitHub push protection also blocks plain pushes). See `SECURITY.md`.
3. **Documentation vs hardware discrepancies**: `docs/issue_list.md` is the **canonical fix log** (each entry: status, committish fixing it). When you spot a doc/assumption vs HD-V2 reality mismatch, add an entry there — don't argue from first principles. Many "this can't work" comments predate commits that already resolved the issue.

## Where to find things

| Need | Look in |
|---|---|
| HD-V2-specific bug fixes / doc-vs-hardware diffs | `docs/issue_list.md` |
| Build/dev environment, env mapping, PlatformIO traps, architecture minimum | `docs/build-and-code-structure.md` |
| Async task model (queues / busy generation / ownership) | `docs/async_ipc_contract.md` |
| allinone consolidated firmware design (not yet implemented) | `docs/allinone-design.md` |
| Working notes (EPD/touch/manual regression/latest review integration) | `docs/allinone-design.md` §11 |
| Code review history (each round = paired request + result file under commit id) | `docs/reviews/wifi-config-keyboard-review-*` |
| Pin definitions and shared utilities | `examples/pda2/utilities.h`, `examples/factory/utilities.h` |
| Menu / button grid / screen ID enum | `examples/pda2/ui_deckpro.{h,cpp}` |
| NVS/SPIFFS storage layout | NVS `ai` (dual-slot `base.0/1`, `model.0/1`, `key.0/1` + `active` flip) and `ai_stats` (single usage blob) in `examples/pda2/openai_api.cpp`; NVS `wifi` (ssid/pass); SPIFFS `/chat.log` (+`.tmp`) and `/chat.draft` in `examples/pda2/ui_ai_chat.cpp` |
| Vendored 3rd-party libraries | `lib/` (offline; LDF links by `#include`) |

## Working notes

- **E-paper rendering is slow**: ~0.3s partial refresh, ~1-2s full refresh. Don't animate, don't scroll live content — use pagination (`LV_OBJ_FLAG_HIDDEN` swap with Enter/Space) per the `pda2/README.md` "Use pagination, not scrolling" note. `LV_COLOR_DEPTH=1` so all images must be monochrome.
- **Touch is configured but keypad drives all navigation** in current code paths. Don't assume pointer events reliably fire after cosmetic-only changes.
- **No live tests** — the project relies on real-hardware manual regression. Each new commit is expected to document its verification gaps in the review request's §验证状态. Repeated rounds of "待用户实测 ⏸" escalate — see review results for how that's tracked.
- **Latest review rounds** (all Codex **A 全量接受**): round 29 `d22007d..4c3c9b1` (native dropdown provider + per-provider keys + OPENROUTER_KEY rename + custom clears), round 30 `a2ecd7b` (key.custom dead store + KEEP-IN-SYNC note), round 31 `764e7bf..980b6df` (CST-8 local month boundary + scan overlay hidden on screen cover + test_keypad mirror note), pending `a924c4e` (SD mount failure hints). Earlier frontier (rounds 22-28) covered: dual-slot NVS atomic save, WiFi scan critical-section + busy generation, Sleep countdown gated by the screen's OWN EPD flush sequence, AI Chat dynamic bodies + SPIFFS atomic log + turn-paired 8KB multi-turn context + New confirmation, usage stats as a mutex-guarded single NVS blob (V3 monthly reset), AI Config save-failure msgbox + Test billing transparency, secrets chain + API-key compensating controls. Read those commit messages before extending the corresponding area.
- **Device-side reminders**: usage-stats month boundary is now local time (TZ=CST-8 set in setup); `localtime()` users (Calendar/Sleep timestamps) follow it. Review-request filenames = first..last covered commit (INCLUSIVE, not git range notation), accepted commits are out-listed.
