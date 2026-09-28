# 评审结果：v1.22–v1.23 批次（`4d27849..bf0aa72`，Grok）

- **评审日期**：2026-09-27
- **评审人**：Grok（Cursor）
- **申请文件**：[session-batch-review-request-4d27849..bf0aa72.md](session-batch-review-request-4d27849..bf0aa72.md)
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**，代码口径（§2.3 A/B/C）；修复核销按 §0 原则 8（构造反例击穿，不是复读申请表）
- **工作树 HEAD**：`113fe0c`（范围外：申请合并 + changelog v1.23 + `FW_VERSION_MINOR` 22→23）。**本批代码 findings 在 `bf0aa72` 上仍存活**（`git diff bf0aa72 HEAD -- examples/pda2 scripts` 为空）。
- **评审提交**（申请 9 个代码/工具 + 2 个不评；首末含两端，与 `git rev-list --count 4d27849^..bf0aa72` = 11 一致）：

  | commit | 内容 | 本轮 |
  |---|---|---|
  | `4d27849` | OTA 清单 4KB 封顶 + Whoami busy 句柄 UI 所有 + ota-baseline | clean（qwen P3-2 / Nit-1 主反例闭合） |
  | `01ba2e0` | platformio.ini 注释乱码 | docs-only，不评 |
  | `0fd2043` | Voice AI MIC-during-TTS 崩溃 + 1s 以下筛选 + 堆日志 + 12K 栈 | clean（崩溃链 CODE+SIM 击不穿） |
  | `a37face` | Voice AI 对话区滚动 | **issue**（P2-1） |
  | `d68289e` | PenPal 草稿放弃确认 | clean |
  | `8d82043` | changelog v1.22 + 旧申请 | docs-only，不评 |
  | `67acddf` | PenPal 排查脚本 | 工具面；密钥常量见 Nit（非新泄密） |
  | `2760655` | 缓存 v2：base 绑定 + 时钟洞 | clean |
  | `77e0634` | 信件方向按本人 uid | 主路径闭合；uid=0 回退见 P3-1 |
  | `4ca5874` | uid NVS + 缓存 v3 base+key | clean（补上 v2 同域换账号盲区） |
  | `bf0aa72` | 未读线程自动刷新 | 功能可达；「后台」名实见 P3-2 |

- **评审结论**：**A 全量接受**。无 P0、无未闭合 P1。当批引入 **1×P2**（Voice AI 触摸滚动抬手会解开 TTS 的 EPD 抑制，把 2026-09-11 已修过的「朗读期 EPD 断粮」再打开）。按 §4.2，P2 是**应修**不是 G-合入阻断；**不得**写成已闭合，须进 `issue_list.md` 并绑下次 Voice AI 改动 / G-真机。P3 ×2 + 决策项不挡合入。
- **G-合入**：是（无 P0/P1）。**G-真机**：否（申请声称三台已刷 `bf0aa72`，本评审方无设备，HW 全 `CLAIM_ONLY`）。

---

## 0. 范围与归属（指南 §0 原则 5 / §7.1）

申请自校：`git rev-list --count c531d66..bf0aa72` = 11、`git diff --name-only` = 19 文件——**复算一致**。旧申请 `4d27849..d68289e.md` 已标注取代，本轮只评合并后区间。

`113fe0c` 不在文件名范围内：版本号 1.23 实际落在该 commit（`bf0aa72` 上 `FW_VERSION_MINOR` 仍为 **22**）。不退回申请，但 **v1.23 版本戳不是本评代码面的一部分**。

`git diff --check 4d27849^..bf0aa72`：通过。

### 区间文件归属

| 文件 | 归属 | 处置 |
|---|---|---|
| `examples/pda2/ota_update.cpp` | **评** | P3-2 封顶 |
| `examples/pda2/ui_whoami.cpp` | **评** | Nit-1 句柄；4ca5874 uid 持久化 |
| `examples/pda2/ui_voice_ai.cpp` | **评（重点）** | 崩溃修复 + 滚动（P2-1） |
| `examples/pda2/openai_api.cpp` | 评 | 堆三值日志 |
| `examples/pda2/ui_penpal.cpp` | **评（重点）** | 草稿键路由；stale 刷新；自动刷新；uid 懒加载 |
| `examples/pda2/ui_penpal.h` / `ui_penpal_write.cpp` | **评** | 确认框 |
| `examples/pda2/penpal_api.cpp` / `.h` | **评（重点）** | 缓存 v2/v3、`j_mine`、uid NVS |
| `examples/pda2/fw_version.h` | 评（低） | 区间内停在 1.22 |
| `docs/ota-baseline.md` | 评（文档） | T=60s 核算自洽；G-真机项仍开 |
| `scripts/*.py` | 评（工具） | 一次性排查；密钥见 Nit |
| `CHANGELOG.md` / `platformio.ini` / 旧申请 | 不评 | 与申请表一致 |

---

## Findings

### P2-1：Voice AI 触摸滚动抬手解开 TTS 的 EPD 抑制 → 朗读期全刷断粮

- **位置**：`examples/pda2/ui_voice_ai.cpp:124-137`（`vai_scroll_begin_cb` / `vai_scroll_end_cb`）；对照 `:644-647`（`start_tts` 置抑制）、`:275-278`（播放结束才释放）；抑制实现为**布尔量而非计数**（`examples/pda2/factory.ino:96-100` `disp_suppress_flush`）。
- **证据（SIM）**：

  1. 语音回合成功 → `tts_auto_read` → `start_tts()`：`tts_playing = true` 且 `ui_disp_suppress_flush(true)`（防止 EPD 阻塞 `audio.loop()`，2026-09-11 真机：应用内朗读断续、退出反而清晰）。
  2. 用户在对话容器上**触摸拖动**（本批 `a37face` 的一等功能，不是残留指针事件）。
  3. `SCROLL_BEGIN`：`indev != NULL` → 再写一次 `suppress(true)`（无增量，布尔已真）。
  4. `SCROLL_END`：**无条件** `ui_disp_suppress_flush(false)` + `ui_disp_full_refr()`。TTS 仍在播放，抑制被清掉。
  5. 全刷 ~0.3–1s 卡住 loopTask → `factory.ino:943` 的 `audio.loop()` 断粮 → 断续噪声。`ui_timer_cb` 只在 `!audio.isRunning()` 时才再释放抑制，**不会**在仍播放时补回去。

  键 `+/-` 路径：`lv_obj_scroll_by` 的 `indev == NULL`，回调直接 return，TTS 抑制保持。**仅触摸抬手**击穿。

  `disp_set_suppress_flush` 是赋值不是嵌套计数，TTS 与滚动无法叠放。
- **影响**：`C/2/有声 → P2`（实时路径被打断；语音回合后边听边翻记录是自然操作，不是冷僻探针）。回升：无（机#1/#2 凡能 TTS 的都可达；机#1 4G 版 TTS 已证实可用）。
- **两轴**：验证状态 `PARTIALLY_VERIFIED`（CODE+SIM；HW `CLAIM_ONLY`）+ 契约 `VIOLATES(review_guide §5.3/§5.7 EPD 不阻塞实时路径)`。根因**当批**（`a37face` 引入滚动回调，未与既有 TTS 抑制合成）。
- **最小修复**：`SCROLL_END` 若 `tts_playing`，保持抑制、**不要**全刷（或抬手后再 `suppress(true)`）。`start_voice_record` 里已有的停播逻辑可保留。回归：TTS 朗读中触摸拖动 → 音频不断续；朗读结束后抬手仍应全刷一次。
- **Required action**：应修，**不得** RISK_ACCEPTED。最小补丁可单独 follow-up；合入本批不倒逼 revert `a37face`。绑 G-真机：TTS 朗读中触摸拖动不得断续。

---

### P3-1：`j_mine` 在本人 uid 未知时仍回退「非零 = mine」

- **位置**：`examples/pda2/penpal_api.cpp:83-89`；调用点 `pp_parse_thread` `:1126`（网络与缓存同一条）；Fix/Polish 使能 `ui_penpal_read.cpp:143-149` / `:207`。
- **证据（SIM）**：NVS `my_uid` 为空（全新刷机、换 key 后 `penpal_my_uid_store(0)`、从未进过 Whoami 拉 profile）时 `s_my_user_id == 0`，`j_mine` 走 `return it->valueint != 0`。真实用户笔友来信带**对方**非零 id → 全部标 `mine` → 详情全 `To:`，且 Fix/Polish 可点。这是 `77e0634` 要修的**同一条规则**；`4ca5874` 只在 Whoami 成功拉过一次之后才封住。PenPal 自己的 sync **不**拉 `/users/me/profile`。
- **申请自评第 3 条**构造的「cache-miss 不会 parse」不成立：cache-miss 走网络，worker 里 `pp_parse_thread` 仍调 `j_mine`（`:1126`）。首访无 uid 时**网络结果也会方向全错**，并写入 v3 缓存；之后即使 Whoami 补了 uid，也要等再 parse（进详情 / Sync / 未读自动刷新）才会纠正。
- **影响**：`D/2/有声 → P3`（错 From/To；Fix 点在别人信上是连带，服务器多半按 email id 拒或改错对象，不升 B——未证明写路径）。回升：把 fallback 当默认产品行为（不进 Whoami 也能长期用 PenPal）即回升 P2。
- **两轴**：`VERIFIED`（CODE）+ `NO_CONTRACT`（方向规则是产品语义，不在 async 契约表）。
- **最小修复（裁定申请第 3 条）**：**uid 未知时 `j_mine` 返回 false**（不显示为「我写的」、Fix/Polish 保持禁用），优于保留 NPC 时代回退。可选增强：PenPal 首次 sync 顺带 GET profile（UI 线程 persist），从结构上关掉窗口。
- **标签**：应修，绑 G-合入或下一修复轮；不挡在 P2-1 之后的合入。

---

### P3-2：未读「后台刷新」实际弹出 READ waitbox，且 THREAD 缓存忽略 `stale_clock`

- **位置**：
  - 自动刷新：`ui_penpal.cpp:903-911` → `pp_start(&rq, false)` → `:562-568` 必 `pp_waitbox_show("Opening thread...")`；键盘在 waitbox 上全部吞掉（`:1146-1151`）。
  - 消费：`ui_penpal.cpp:638-656` 成功时 **`pp.thr_idx = 0`**（用户若已翻到旧信会被拽回最新）。
  - 开信缓存：`:884-885` 调 `penpal_cache_load_thread(...)` **不传** `stale_clock`（默认 nullptr）；HOME 路径 `:1209-1226` 才会在 clock-unsynced 时刷新。
- **证据**：申请 §1.5 写「缓存命中渲染后……后台刷新」「无未读=纯缓存」。实现上未读开信 = 先 `pp_set_page(THREAD)`（一次全刷）再盖 waitbox，与 cache-miss 开信同属 `PP_WAIT_READ`（Close = cancel/gen++）。THREAD 文件在 NTP 前写入时，HOME 会带 `stale` 刷新，详情开信不会——读侧时钟洞在 **thread 文件**上仍开着（mailbox/pals 已封）。
- **影响**：`E/1/有声 → P3`（可用性；busy 会结束，不是永久卡死）。坐标不升 C：用户可点 Close 取消、缓存信仍在。
- **两轴**：`VERIFIED`（CODE）+ `NO_CONTRACT`。
- **最小修复**：自动刷新走「不弹 waitbox / 不吞键」的 `pp_start` 变体（busy 仍要占，避免与 Sync 双飞），或至少用更轻的状态行；消费时保留 `thr_idx`（夹到新 `letters_cnt`）；开信把 `stale_clock` 接到与 HOME 相同的刷新策略。
- **标签**：应修，可与 P3-1 同轮。

---

### 已通过项（含击穿实验）

#### 4d27849 — qwen P3-2 清单封顶

`ota_check_task` 在 `getString()` 前：`mlen = http.getSize()`，`mlen <= 0 || mlen > 4096` 拒绝（`ota_update.cpp:355-362`）。chunked（`getSize()==-1`）与超大 Content-Length 都进不了分配。与固件下载侧已有 `clen` 禁 chunked（`:472` 注释）同构。主反例（恶意/异常 CL → `getString()` OOM）击不穿。合法清单无 CL 会被误拒——作者明示 10× 余量优先于兼容，接受。

#### 4d27849 — qwen Nit-1 Whoami 句柄

worker 不再写 `s_wa_task`（删除旧 `:s_wa_task = NULL`）；`wa_consume` 在取出结果后、stale 丢弃前清句柄（`ui_whoami.cpp:380-390`），stale profile 也会解锁 busy。投递改为入队成功才返回（`:297-300`），消掉「2s 超时删结果但句柄仍挂」的新窗口。单飞 + 队列永驻 + loop 无条件 `wa_consume` 下，忙等实际不会发生。

**契约漂移（Nit，不单列缺陷）**：`async_ipc_contract.md` 规则 7 仍写 Whoami =「2000ms + 超时自 delete」。实现已改成近似 `portMAX_DELAY`。建议同批或下一文档 commit 把 Whoami 登记为第三种变体（UI 清句柄 ⇒ 必须投递成功，不能超时丢结果）。LevelTest/WordBank 未改，仍走旧变体。

#### 0fd2043 — MIC-during-TTS 崩溃

`start_voice_record` / `start_tts` 在 `pdm_init`/重装 I2S 前 `audio.stopSong()`（`ui_voice_ai.cpp:531-535`、`:591-594`）。`Audio::stopSong` 清 `m_f_running` 且本地文件路径清 `m_f_localfile`（`lib/ESP32-audioI2S/src/Audio.cpp:2086-2093`）；随后 `audio.loop()` 不再进 `processLocalFile`。`factory.ino` 里 `voiceai_keyboard_poll` 在 `audio.loop()` **之前**（`:923-943`），同一 loopTask 上停播先于下一次 pump。主反例（朗读中按 MIC → 卸驱动 → `i2s_write` LoadProhibited）击不穿。过短筛选：`dur_ms < 1000` 则 `free(wav)` 且不上 ASR（`:446-461`）；waitbox 在筛选前 `ui_post(WAITBOX)`，过短路径会闪一下再被 `ai_task==NULL` 收掉——Nit，不升级。

#### d68289e — 草稿确认

`ppw_back_cb`：非空 title/body 且非 `send_lock` 才弹框；SEND 飞行直退 HOME（`ui_penpal_write.cpp:387-391`），与申请「飞行语义不变」一致。键盘：`penpal_keyboard_poll` 框在时 Enter=放弃、其它键=保留（`ui_penpal.cpp:1121-1128`）。COMPOSE 键 `\b` 仅在两框都空时离页（`:656-664`）——键盘用户不能「带着草稿误退」，确认框主要保护**顶栏返回**。`ppw_overlays_close` 删框不丢草稿（`:578-580`）。无 P0/P1/P2。

#### 2760655 / 4ca5874 — 缓存绑定与时钟洞（HOME）

v3 头 `<fetched> <base> <key>`（`penpal_api.cpp:368-401`）；缺 key 的 v1/v2 头直接 `SPIFFS.remove`（`:444-449`）；base/key 与当前 NVS 不一致即失效（`:456-460`）。读侧 NTP 前：serve + `stale_clock`（`:463-467`）；写侧 `fetched==0` 在时钟已同步后当过期（`:468-474`）。HOME entry 接了 `stale` 并 `pp_home_sync(false)`（`ui_penpal.cpp:1209-1226`）。换 key：`penpal_my_uid_store(0)`（`ui_whoami.cpp:512-516`）+ v3 失配删缓存。与 2026-09-26「同域换库 / hunter 缓存打在 carlos 上」的主反例对齐。**THREAD 文件的 stale 标志未接到开信** → 归 P3-2，不否决 HOME 修复。

#### 77e0634 / 4ca5874 — 方向主路径

`j_mine`：uid 已知时 `sender_user_id == s_my_user_id`（`:87`）。Whoami 成功 profile → `penpal_my_uid_store`（`ui_whoami.cpp:399`）；PenPal `entry` 先 `penpal_my_uid_load` 再 cache parse（`ui_penpal.cpp:1194-1213`）。worker `penpal_get_profile` 写 RAM uid（`:582-586`）注释正确（Preferences 不进 worker）。carlos 机「hello = From: hunter」属 HW `CLAIM_ONLY`，CODE 与声明一致。残留窗口见 P3-1。

#### 其它

- 堆日志：`openai_api.cpp:608-612` 三值，无秘密打印。
- 滚动显示帽 `VAI_DISP_MAX 4000` + UTF-8 续字节对齐（`ui_voice_ai.cpp:163-165`），全文仍在 PSRAM `chat_history`。符合 §15 池教训。
- `docs/ota-baseline.md`：T=60s = 2×T_min(30s)，最长不可喂狗 = EPD `_busy_timeout` 10s，内部自洽（DOC `VERIFIED`）；双机仪器化与自毁回滚仍开（申请 §3）。

---

## 作者「最没把握」三条（对称三格 ③）

| # | 自评 | 本轮裁定 |
|---|---|---|
| 1 | 12K 栈未打水印 | **不升缺陷**。同链路 PenPal 8K 先例；本批语音任务堆上 ASR/hex，栈型声明合理。无 watermark = 观察项，HW 长跑才有证据。 |
| 2 | 缓存 v3 头带 16 字符 key 明文 | **决策项：接受**。SPIFFS 已有 `/env.cfg` 同 key；威胁模型要物理转储。哈希多一层对「换账号失效」无额外安全边界。不必改。若将来轮换测试 key（`TODO.md` 已列），头会自然失效。 |
| 3 | uid 未知时的 `j_mine` 回退 | **不接受为长期语义** → **P3-1**。请改为 uid=0 时不当 mine。 |

---

## 疑问清单（不占缺陷）

- `penpal_my_uid_load` 在 `s_my_uid_loaded` 后仍用 NVS **覆盖** RAM（`:64`）。Whoami worker 已写入非零 RAM、consume 尚未 `store` 时进 PenPal，会被 NVS 的 0 踩掉。窗口窄（loop 会很快 consume），折叠进 P3-1，修复 uid=0 不当 mine 后无害。
- HOME `stale` 指针先给 mailbox 再给 pals；`pp_cache_read` 入口把 `*stale_clock = false`，**后一次调用覆盖前一次**。两文件同拍同时钟下同结果；mailbox 预 NTP、pals 已同步的拼盘几乎不可达。
- 过短录音仍先 `UI_MSG_WAITBOX` 再本地丢弃，可能闪框。
- `penpal_api.h:74` 仍写 `sender_user_id != null -> letter I wrote`，与 `j_mine` 新语义不符。
- 脚本 `inspect_mailbox.py` / `mb_check.py` / `whoami_check.py` 硬编码 `89rg35eua2` 与 gate-pin。二者**早已**在 `docs/penpal-design.md` / `TODO.md`（「已入 git，待轮换」）和 `penpal_api.cpp:210`。**不是本批新 P0**。建议脚本改读环境变量，轮换走既有 TODO，勿再复制新副本。

---

## 验证说明

- **评审方环境**：无 PlatformIO（`python3 -c "import platformio"` 失败，PATH 无 `pio`）→ BUILD **UNKNOWN**，不挡代码结论。无设备 → 全部 HW `CLAIM_ONLY`。
- **已做**：全区间 `git log` / `git diff`（含申请未展开的 `penpal_api.cpp` 缓存与 `j_mine`、`ui_voice_ai.cpp` 滚动与 TTS 抑制、`factory.ino` 抑制布尔）；`git diff --check`；对照 `async_ipc_contract.md` 规则 3/7、`review_guide` §5.1/§5.3/§5.7；MIC 崩溃链对照 Audio 库 `stopSong`/`loop`；qwen P3-2/Nit-1 原文对照实现。
- **未做**：`pio run -e pda2`；真机 MIC-during-TTS / 触摸+TTS / 未读自动刷新 / carlos 方向。

---

## 对称三格（强制）

1. **是否核了全区间 diff**：是。不限于申请靶向清单。补核了 `a37face` 滚动回调与 TTS 抑制的合成、`pp_start(false)` 对「后台刷新」的实际 waitbox、`j_mine` 网络路径、thread 缓存是否接 `stale_clock`、Whoami 投递循环与契约规则 7 文本。`01ba2e0`/`8d82043` 按申请不评。`113fe0c` 排除出代码面并在头部声明。
2. **快照是否独立复跑**：未独立 BUILD。CODE/SIM 在当前工作树（= `bf0aa72` 代码 + 范围外文档）上进行。HW 未复现。
3. **申请「最没把握」是否构造反例**：三条均回应；第 3 条反例成立（网络 parse 仍走 fallback）→ P3-1；第 2 条接受；第 1 条不升。

---

## 声明矩阵（§4.4 适用行）

| # | 声明 | 状态 |
|---|---|---|
| ① 编译 SUCCESS / RAM 57.7% / Flash 44.2% | `CLAIM_ONLY`（作者；本方 BUILD UNKNOWN） |
| ② 三台已刷 `bf0aa72` / carlos 方向与 v3 迁移真机过 | `CLAIM_ONLY` |
| ③ busy 释放 | Whoami 句柄 UI 清：CODE `VERIFIED`；PenPal 自动刷新仍走既有 `pp_release_busy`：未新引入泄漏 |
| ⑥ 秘密不入 tracked | 本批无新 sk-api；PenPal 测试 key 为**既有** git 副本（TODO 轮换），非新 P0 |
| ⑦ gen 门控 | 自动刷新带 `s_pp_gen`；Close waitbox 仍 gen++。THREAD 消费不因自动刷新改 gen 语义 |
| ⑩ EPD 不阻塞实时路径 | **CONTRADICTED**（P2-1，触摸+TTS） |

---

## 每 commit 一行

```
4d27849  clean     OTA 4KB cap + Whoami UI-owned handle
01ba2e0  docs-only platformio.ini comments
0fd2043  clean     MIC-during-TTS stopSong; <1s drop; heap log; 12K stack
a37face  issue     P2-1 scroll_end vs TTS suppress
d68289e  clean     draft abandon confirm
8d82043  docs-only changelog + superseded request
67acddf  clean     triage scripts (known test key copies)
2760655  clean     cache v2 base + clock holes (HOME)
77e0634  issue     direction-by-uid; leftover fallback = P3-1
4ca5874  clean     NVS uid + cache v3
bf0aa72  issue     unread auto-refresh; waitbox/stale = P3-2
```

---

## §9 自审摘录

异步 1–6：Whoami 句柄改 UI 写；PenPal 自动刷新仍 UI `pp_start`。内存 7–8：本批无新 memset/`= T()` 肥聚合。秘密 10–11：无新长 key 路径；脚本常量见疑问。硬件/实时 13：**未过**（P2-1）。方法论 15–19：P2 给了操作序列；HW 标 CLAIM_ONLY；反例库「实时路径 / EPD 期间音频泵」本轮命中。

---

## 入库义务

- P2-1 / P3-1 / P3-2 写入 `issue_list.md`（建议新 §32）绑门禁：P2-1 → G-真机（Voice AI 朗读+触摸）；P3 → 下一 PenPal 修复轮。P2-1 **禁止** RISK_ACCEPTED。
- 决策项「缓存头明文 key」登记为 RISK_ACCEPTED（属主=用户，范围=SPIFFS 物理接触，与 `/env.cfg` 同分区）。
- 契约规则 7 的 Whoami 变体建议随文档 commit 改一句话（UI 清句柄 ⇒ 投递必须成功）。

---

## 审批意见

- [x] **A 全量接受**
- [ ] B 退回修订
- [ ] C 部分接受

P2-1 为应修台账项，不挡本批合入；下一次改 `ui_voice_ai.cpp` 或做 Voice AI G-真机前必须闭合。
