# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

LilyGo **T-Deck-Pro V1.1** firmware repository. ESP32-S3, 16MB Flash + 8MB PSRAM, GDEQ031T10 3.1" E-Paper (240×320 monochrome), TCA8418 4×10 keypad, CST226SE touch. Targets are independent example sketches under `examples/`; `pda2` is the active PDA application (built on top of `factory`). A consolidated `examples/allinone` is being designed (see `docs/allinone-design.md`) but not yet implemented.

## Critical conventions (load-bearing)

These come from prior session memory and review rounds; treat them as hard rules:

1. **Code-review workflow** (see `docs/reviews/README.md` for merge flow; full rationale in Claude memory):
   - Every change goes through a review-request file under `docs/reviews/` named `wifi-config-keyboard-review-request-<commit-id>.md`. Filename is the commit (or range) id.
   - Commits must be split per module (keypad driver / WiFi config / AI chat each in its own commit). Co-Authored-By line is `Claude <noreply@anthropic.com>`.
   - Existing review result files are **never overwritten** — superseded request files can be merged into a range request, but results stand.
2. **Real API key exception** (project-specific deviation from default hygiene guidance):
   - `examples/pda2/openai_api.h::AI_KEY_DEFAULT` currently holds a real OpenRouter key. Per user decision, **dev-phase retention is acceptable** to iterate quickly on AI features. Compensating controls (compile-time `#warning`, `SECURITY.md`, dev-tier key rotation) are recommended but **not blocking**. Before any push to a public remote, the key string must be removed from the file.
3. **Documentation vs hardware discrepancies**: `docs/issue_list.md` is the **canonical fix log** (each entry: status, committish fixing it). When you spot a doc/assumption vs HD-V2 reality mismatch, add an entry there — don't argue from first principles. Many "this can't work" comments predate commits that already resolved the issue.

## Build & dev environment

Local machine: Windows 11, PlatformIO Core 6.1.19 installed via pip, **not on PATH**. Use:

```bash
python -m platformio run -e pda2          # build pda2
python -m platformio run -e factory       # build factory
python -m platformio run -e <env>         # one env per examples/<name>
python -m platformio run -e pda2 -t upload --upload-port COM5
python -m platformio device monitor -p COM5 -b 115200
```

Each `[env:xxx]` in `platformio.ini` maps to `examples/xxx` via `script/set_srcdir.py` (env name == folder name; `T-Deck-Pro` env → `examples/test_GPS`). `default_envs = T-Deck-Pro`.

**Prerequisites**:
- `examples/pda2/config_keys.h` must exist (copy from `config_keys.h.example`; gitignored). Empty values compile fine.
- Stop any background `device monitor` before `-t upload`, otherwise COM5 is held and upload silently fails.

Full build details, including PlatformIO `src_dir` global-only trap (the reason `set_srcdir.py` exists), are in `docs/build-and-code-structure.md`. Do **not** trust the path `C:\Users\asdfo\.platformio\...\pio.exe` quoted in that file — it's from another machine.

## Architecture (the minimum to be productive)

Three load-bearing subsystems; read these files before changing anything in their area:

1. **Screen manager (`scr_mgr`)** — `examples/pda2/ui_scr_mrg.{h,c}`. Each app exposes a `scr_lifecycle_t { create, entry, exit, destroy }` registered via `scr_mgr_register(SCREEN_ID, ...)`. Navigation uses push/pop/switch. Page transitions call `keypad_clear_chars()` to drain the keypad FIFO (prevents residual Backspace across screens).
2. **Keypad driver** — `examples/pda2/peri_keypad.cpp`. TCA8418 4×10 matrix with 3 layers (lowercase / Shift uppercase / Sym locked). Modifier state (double Shift OR-ed, Alt momentary sym, Sym toggle) is maintained per-driver. Hardware FIFO → 16-deep software character FIFO consumed by `keypad_get_val()`. **`INT_STAT` is W1C** — write-to-clear, not read-to-clear (the old code got this wrong and broke modifier state on overflow; see issue_list §1.5).
3. **Async IPC contract** — `docs/async_ipc_contract.md` defines the canonical pattern for all screens that issue HTTP requests (WiFi Test / Time Sync / AI Test / AI Chat Send). Hard rules: result structs `new`-ed by the worker task and `delete`-d by the UI thread after `xQueueReceive`; busy flags are UI-thread-only and carry a generation counter; tasks own **copies** of all UI buffers (no `volatile`/`std::string` shared-state). Local-only screens (Sleep, Keys, GPS) do **not** apply this contract.

Adding a new app: see `examples/pda2/README.md` §Architecture (four-line checklist: write `ui_myapp.cpp` with `scr_lifecycle_t`, add enum to `ui_deckpro.h`, register in `ui_deckpro.cpp`, add a `menu_btn`).

## Where to find things

| Need | Look in |
|---|---|
| HD-V2-specific bug fixes / doc-vs-hardware diffs | `docs/issue_list.md` |
| Build pipeline, env mapping, PlatformIO traps | `docs/build-and-code-structure.md` |
| Async task model (queues / busy generation / ownership) | `docs/async_ipc_contract.md` |
| allinone consolidated firmware design (not yet implemented) | `docs/allinone-design.md` |
| Code review history (each round = paired request + result file under commit id) | `docs/reviews/wifi-config-keyboard-review-*` |
| Pin definitions and shared utilities | `examples/pda2/utilities.h`, `examples/factory/utilities.h` |
| Menu / button grid / screen ID enum | `examples/pda2/ui_deckpro.{h,cpp}` |
| NVS layout (AI config + chat log) | `examples/pda2/openai_api.{h,cpp}` for ai namespace; `/chat.log` (SPIFFS) ring buffer in `ui_ai_chat.cpp` |
| Vendored 3rd-party libraries | `lib/` (offline; LDF links by `#include`) |

## Working notes

- **E-paper rendering is slow**: ~0.3s partial refresh, ~1-2s full refresh. Don't animate, don't scroll live content — use pagination (`LV_OBJ_FLAG_HIDDEN` swap with Enter/Space) per the `pda2/README.md` "Use pagination, not scrolling" note. `LV_COLOR_DEPTH=1` so all images must be monochrome.
- **Touch is configured but keypad drives all navigation** in current code paths. Don't assume pointer events reliably fire after cosmetic-only changes.
- **No live tests** — the project relies on real-hardware manual regression. Each new commit is expected to document its verification gaps in the review request's §验证状态. Repeated rounds of "待用户实测 ⏸" escalate — see review results for how that's tracked.
- **Latest review rounds** the project is currently integrating (per review files): dual-slot NVS atomic save (`844a907`), WiFi scan critical-section + busy generation (`9c075c5`/`e31cd06`), Sleep countdown gated by EPD frame-complete (`7fec0e5`), AI Chat dynamic bodies + SPIFFS ring + multi-turn 8KB context (`9b376da`/`e1b2d0f`), AI Config save-failure msgbox + Test billing transparency (`e60b2e8`). Read those commit messages before extending the corresponding area.
