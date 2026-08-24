# Merge conflict log — `HD-V2-250915` vs `origin/HD-V2-250915`

> 生成日期：2026-08-24 · 触发命令：`git pull --no-rebase origin HD-V2-250915`
> 当前状态：merge 进行中，工作区暂留冲突以便人工决策。

## 背景

| 角色 | 提交 | 日期 | 说明 |
|---|---|---|---|
| `HEAD` (本地 / ours) | `48a6d51` | 2026-08-09 | `docs(allinone): revise design per 4 follow-up findings on cdbd383` |
| `MERGE_HEAD` (远程 / theirs) | `aecddc3` | 2026-08-22 | `document`（远程近期被强制更新） |
| merge-base | `dfcf8ed` | 2025-09-10 | `fix bug`（一年前的共同祖先） |

分叉规模：本地领先 base **123** 个提交；远程领先 base **333** 个提交。
冲突文件总数：**27**（2 个 `UU` + 25 个 `AA`）。
注：之前回合估算为 29，实际计数为 27。

## 类型说明

- **UU**（modify/modify）：base 已知，双方都修改了同一文件。可三方 diff。
- **AA**（add/add）：base 不存在（两边独立新增），无法三方合并，只能二选一或手工缝合。

## 冲突汇总表

| # | 文件 | 类型 | 本地 size | 远程 size | 远程比本地增删 | 远程侧最近提交 | 本地侧最近提交 |
|---|---|---|---:|---:|---:|---|---|
| 1 | `.gitignore` | UU | 323 | 377 | +3 / −0 | `cb98b16` pda2: AI Config provider presets + base suffix + monthly usage reset | `8a0aabb` Fix build reproducibility issues |
| 2 | `TODO.md` | AA | 2 562 | 10 784 | +136 / −22 | `aecddc3` document | `8151c5a` pda2: phase 0 pilot |
| 3 | `docs/allinone-design.md` | AA | 36 652 | 50 860 | +116 / −40 | `2f483a0` docs: MP3 cancelled on 4G variant; allinone superseded by pda2 | `48a6d51` docs(allinone): revise design |
| 4 | `docs/build-and-code-structure.md` | AA | 10 053 | 13 334 | +37 / −2 | `8578bc4` docs: consolidate CLAUDE.md build/architecture/working notes | `63a27ea` Add multi-example build support |
| 5 | `examples/pda2/README.md` | AA | 5 306 | 7 653 | +42 / −32 | `37311ab` docs: sync CLAUDE/CHANGELOG/TODO/issue_list/README with rounds 29-31 + hardware findings | `8151c5a` pda2: phase 0 pilot |
| 6 | `examples/pda2/config_keys.h.example` | AA | 1 578 | 2 199 | +11 / −0 | `b48f584` penpal: API client | `8590289` Calendarific API for calendar holidays |
| 7 | `examples/pda2/factory.h` | AA | 2 537 | 2 817 | +3 / −0 | `955a492` pda2: redraw-on-release scroll + Enter newline | `d4e9b68` Add pda2 example |
| 8 | `examples/pda2/factory.ino` | AA | 23 350 | 26 297 | +78 / −6 | `b231dd3` penpal: screen UI - mailbox/compose/thread pages | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 9 | `examples/pda2/http_utils.cpp` | AA | 7 331 | 21 312 | +289 / −39 | `b48f584` penpal: API client | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 10 | `examples/pda2/http_utils.h` | AA | 3 541 | 5 867 | +52 / −2 | `b48f584` penpal: API client | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 11 | `examples/pda2/openai_api.cpp` | AA | 2 864 | 22 376 | +489 / −17 | `950fcfe` ai-cfg: Trust self-signed TLS toggle | `8151c5a` pda2: phase 0 pilot |
| 12 | `examples/pda2/openai_api.h` | AA | 1 298 | 5 615 | +83 / −4 | `950fcfe` ai-cfg: Trust self-signed TLS toggle | `8151c5a` pda2: phase 0 pilot |
| 13 | `examples/pda2/peri_gps.cpp` | AA | 9 894 | 11 454 | +67 / −25 | `52f709e` gps: publish all fields under snapshot mux | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 14 | `examples/pda2/peri_keypad.cpp` | AA | 3 940 | 8 876 | +157 / −56 | `a2bfc46` pda2: flush hardware key FIFO on screen transitions | `8151c5a` pda2: phase 0 pilot |
| 15 | `examples/pda2/peripheral.h` | AA | 2 039 | 2 179 | +7 / −0 | `42961e5` pda2: fix keypad page boundary + key map | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 16 | `examples/pda2/src/assets.h` | AA | 922 | 949 | +1 / −0 | `5329383` penpal: menu icon + third menu page | `9878c9c` Add Voice AI app |
| 17 | `examples/pda2/ui_ai_cfg.cpp` | AA | 5 645 | 31 279 | +673 / −51 | `a58a73c` ai-cfg: spell out TLS toggle scope in status line | `8151c5a` pda2: phase 0 pilot |
| 18 | `examples/pda2/ui_ai_chat.cpp` | AA | 7 894 | 49 045 | +1105 / −144 | `955a492` pda2: redraw-on-release scroll + Enter newline | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 19 | `examples/pda2/ui_calculator.cpp` | AA | 13 230 | 13 327 | +2 / −0 | `42961e5` pda2: fix keypad page boundary + key map | `ab97954` Add dictionary app |
| 20 | `examples/pda2/ui_deckpro.cpp` | AA | 127 386 | 169 010 | +1239 / −244 | `bfa7a16` menu: one page per swipe (gesture poll edge-detect) | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 21 | `examples/pda2/ui_deckpro.h` | AA | 4 001 | 4 023 | +1 / −0 | `5329383` penpal: menu icon + third menu page | `8151c5a` pda2: phase 0 pilot |
| 22 | `examples/pda2/ui_deckpro_port.cpp` | AA | 17 282 | 18 752 | +39 / −1 | `c8f62f3` pda2: SD hint states failure, offers FAT32 | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 23 | `examples/pda2/ui_deckpro_port.h` | AA | 6 114 | 6 240 | +4 / −1 | `23030c9` pda2: Setting screen explains SD mount failures | `27ad8d5` pda2 + docs(allinone): 9 reviewer findings |
| 24 | `examples/pda2/ui_scr_mrg.c` | AA | 7 774 | 8 209 | +7 / −0 | `42961e5` pda2: fix keypad page boundary + key map | `d4e9b68` Add pda2 example |
| 25 | `examples/pda2/ui_weather.cpp` | AA | 20 354 | 27 891 | +310 / −117 | `141942d` weather: reject zero-slot forecast parse as data_valid | `2069c8a` Add weather app, fix icons |
| 26 | `platformio.ini` | UU | 4 690 | 5 367 | +14 / −0 | `e3e3e31` test_i2s_probe: audio-path probe for 4G variant | `8a0aabb` Fix build reproducibility |
| 27 | `script/set_srcdir.py` | AA | 723 | 1 241 | +9 / −0 | `6d26699` build: honor external PLATFORMIO_SRC_DIR | `8a0aabb` Fix build reproducibility |

## 重点说明

- 远程在 `48a6d51..aecddc3` 区间新增了 `PenPal` 模块（mailbox / compose / thread 三页 UI + API 客户端 + 异步任务框架），并叠加了 30+ 轮 review 修复（issue_list §3-§12）。
- 本地仅停留在 `48a6d51`（`docs(allinone): revise design`）—— 后续没跟上远程的迭代。
- 本地并未跑过 `git filter-repo`（历史里仍可能包含已轮换前的旧提交指纹，详见 `SECURITY.md`）。**不要把本地 `.git` 上传或 cherry-pick 到远程分支**。
- `examples/pda2/peripheral.h`、`.gitignore` 两个文件本地有 `docs/`/`SECURITY.md`/`CHANGELOG.md` 等新文档的引用，远程版本可能缺这些文档路径引用——合成时需校对。

## 决策建议

> 用户先前表态：**"以远程为主（丢弃本地）"**——但本次任务中止了 `git checkout --theirs .` 的全量覆写，改为先记录冲突再决策。下面是按"决策成本"分级的建议清单：

### 必读：`UU` 类（可三方对比，2 个）

1. **`.gitignore`**（3 行差异）
   - 本地：`8a0aabb` 8 月 8 日加入 `script/set_srcdir.py` 跟踪
   - 远程：`cb98b16` 后续追加 AI Config / pda2 产物忽略规则
   - **建议**：合成两端（`.gitignore` 行数都不大，可手工 3 行追加）

2. **`platformio.ini`**（14 行差异）
   - 本地：`8a0aabb` 处理 build reproducibility
   - 远程：`e3e3e31` 加入 `test_i2s_probe` 环境配置
   - **建议**：以远程为主（远端包含 `test_i2s_probe` 等新环境）；如本地有 `script/set_srcdir.py` 相关的 `extra_scripts` 段，需手动补回

### 大体量代码（diff > 100 行，建议单独 review）

| 文件 | 远程增删 | 远程侧关键提交 |
|---|---:|---|
| `examples/pda2/ui_deckpro.cpp` | +1239 / −244 | `bfa7a16` menu 翻页 edge-detect |
| `examples/pda2/ui_ai_chat.cpp` | +1105 / −144 | `955a492` redraw-on-release + Enter newline |
| `examples/pda2/ui_ai_cfg.cpp` | +673 / −51 | `a58a73c` TLS toggle status line |
| `examples/pda2/openai_api.cpp` | +489 / −17 | `950fcfe` Trust self-signed TLS |
| `examples/pda2/ui_weather.cpp` | +310 / −117 | `141942d` zero-slot forecast 拒绝 |
| `examples/pda2/http_utils.cpp` | +289 / −39 | `b48f584` PenPal API client |
| `examples/pda2/peri_keypad.cpp` | +157 / −56 | `a2bfc46` key FIFO flush |
| `examples/pda2/peri_gps.cpp` | +67 / −25 | `52f709e` snapshot mux |
| `examples/pda2/factory.ino` | +78 / −6 | `b231dd3` PenPal screen UI 注册 |

**建议**：直接采用远程版（与用户"以远程为主"意图一致），但要核对：
- PenPal 注册逻辑是否依赖本地尚未引入的头文件（如 `ui_penpal.h`）—— 远程同时新增 `examples/pda2/ui_penpal.{cpp,h,read,write}` 等文件，这些不在冲突列表（remote-only 新增已被 clean merge 加入）。
- 双槽 NVS、WiFi scan busy generation、Sleep countdown 触发器等 CLAUDE.md §3 描述的关键 fix 都在远程侧。

### 小体量 / 文档类（可直接采纳远程）

- `TODO.md`、`docs/allinone-design.md`、`docs/build-and-code-structure.md`、`examples/pda2/README.md`、`examples/pda2/factory.h`、`examples/pda2/peripheral.h`、`examples/pda2/ui_scr_mrg.c`、`examples/pda2/src/assets.h`、`examples/pda2/ui_deckpro.h`、`examples/pda2/ui_deckpro_port.{cpp,h}`、`examples/pda2/ui_calculator.cpp`、`examples/pda2/config_keys.h.example`、`script/set_srcdir.py`
- 这些文件的远程版本都补到了后续 review 轮的更新，本地基本停在 8 月 8-9 日

## 操作清单

```bash
# 1. 回到冲突状态（如果之前已 checkout --theirs）
git checkout -m .  # 把冲突标记重新生成到工作区
# 或：手动 `git checkout -m <file>` 逐文件

# 2. 按上表逐个处理（推荐先 UU 后 AA）
#    UU 双方合成；AA 默认采用 theirs，但核对依赖

# 3. 处理完所有冲突后：
git add -A
git commit -m "merge origin/HD-V2-250915: drop local (pre-rewrite history), adopt remote"

# 4. 如果决定整体放弃 merge：
git merge --abort
git reset --hard origin/HD-V2-250915   # 与远程完全一致，丢弃本地 123 个独有提交
```

## 参考

- 远程 review 历史：`docs/reviews/wifi-config-keyboard-review-*.md`（Codex/Kimi/Copilot 多轮）
- 已知 fix 索引：`docs/issue_list.md` §3-§12
- 远程合并基线身份核实：`SECURITY.md` + `.git/filter-repo/commit-map`（远程已在 8 月 21 日完成 filter-repo；本地 `48a6d51` 未做，请勿把本地对象推回 origin）
