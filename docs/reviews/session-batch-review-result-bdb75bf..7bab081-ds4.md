# 评审结果：五方评审（1561b41..b92d021）修复轮 delta 复审（bdb75bf..7bab081，ds4）

- **评审日期**：2026-09-18
- **申请文件**：`docs/reviews/session-batch-review-request-bdb75bf..7bab081.md`
- **评审依据**：`docs/review_guide.md` v1.3（§2.5 修复轮 delta 复审）
- **评审提交**（区间 `bdb75bf..7bab081`，3 个）：

  | commit | 内容 | 评审面 |
  |---|---|---|
  | `0bc0fcc` | v1.14 修复轮：P1-1 幂等注册扫描回调 / P1-2 幂等注册 autoconn 回调 / P2-1 粘滞 dropped-link / P2-2a whoami 部分保存代次 | 代码（4 文件） |
  | `ae39763` | v1.15 修复轮：F1 终态快径 / F3 cursor.show 勘误 / F4 状态行过渡态 / F5 归因勘误（台账） | 代码（3 文件） |
  | `7bab081` | docs：ds4 结果文件入库 | 纯文档 |

- **评审基线核对**：`git rev-parse HEAD` = `bb28ce2`。本区间之后 HEAD 另有 4 个**代码** commit
  （`e411e73`/`72b4cce`/`bfc27d2`/`d5755ae`，属另两份申请的 LevelTest/WordBank 功能面）
  与 3 个 docs commit。逐 hunk 核过**本区间触及的符号在 HEAD 上无漂移**：
  `git diff 7bab081 HEAD -- examples/pda2/ui_deckpro_port.cpp examples/pda2/ui_whoami.cpp` **为空**；
  `ui_deckpro.cpp` 仅有 3 处**纯新增** hunk（`:10` include、`:286` 菜单条目、`:5637-5638` 屏幕注册），
  扫描 / autoconn / 状态行三个受评区段一行未动 ⇒ delta 复审成立。
- **结论**：**C 部分接受**——2×P1 + 2×P2 + 3×P3 + 3×Nit 的核销**全部成立**（含我上轮 F1~F5 的五项处置）；但 v1.15 新引入的终态快径本身留有一处判据缺口（F1′，P3/应修），故不接受"全量"。

## Findings

### P3（应修｜绑 G-真机）：F1 终态快径把 `scanComplete()` 的**超时判负**分支误当"终态"，早退时跳过了 `esp_wifi_scan_stop()`

- **位置**：`examples/pda2/ui_deckpro.cpp:3372-3384`（快径）；判负分支在
  `~/.platformio/packages/framework-arduinoespressif32/libraries/WiFi/src/WiFiScan.cpp:142-158`
- **证据**（三层，均可复算）：

  1. 框架源码：`scanComplete()` 有**三条**返回路径，不止"运行中 / 已终结"两种——

     ```cpp
     if (_scanStarted && (millis()-_scanStarted) > _scanTimeout) {   // ④ 超时判负
         clearStatusBits(WIFI_SCANNING_BIT);        // 清 SCANNING，但 _scanStarted 保持非 0
         return WIFI_SCAN_FAILED;                   // -2：驱动扫描**仍在飞**
     }
     if (DONE bit) return _scanCount;               // 终结且有结果
     if (SCANNING bit) return WIFI_SCAN_RUNNING;
     return WIFI_SCAN_FAILED;                       // 从未启动
     ```
     `_scanTimeout = max_ms_per_chan * 20`，本项目 `WiFi.scanNetworks(true)` 用默认
     `max_ms_per_chan=300`（`WiFiScan.h:34`）⇒ **6000 ms**。
  2. 快径判据 `if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); return; }`
     在分支 ④ 同样成立，于是**提前返回、不调 `esp_wifi_scan_stop()`**，驱动侧扫描继续跑。
  3. 可达路径：4_2 的 collect 只在 1 s 周期的 tick 里做（`ui_deckpro.cpp:3660-3668`，
     定时器 `:3731`），6 s 判负后到下一次 tick 之间（≤1 s）按 Back ⇒ `exit4_2`（`:3742` 的
     `wifi_scan_async_inflight` 分支）正好落进该窗口；4_1 侧的
     `destroy4_1 → wifi_cfg_scan_abort`（`:3559`/`:3411`）同理。

- **影响**：① 紧随其后的 kick 结果未定义——IDF 只文档化"still connecting"一种
  `ESP_ERR_WIFI_STATE`（`esp_wifi.h:413`），"扫描在飞"未文档化；失败则一轮扫描报废，
  4_2 按 10s 节奏自愈。② 仍在飞的扫描迟早投递 SCAN_DONE（自然完成或重连时被驱动中止），
  `s_scan_done_cnt++`（`:2282`）——该计数协议**不带代次**，无法区分来源，故它可能提前清掉
  **后续**一次 abort 刚登记的 `s_scan_release_pending`（`:2283`）。
  **不构成内存不安全**：`_scanStarted != 0` 期间框架的 `_scanResult` 必为 NULL
  （`scanNetworks()` 入口先 `scanDelete()`，`WiFiScan.cpp:68`），早退的 `scanDelete()`
  无对象可释放，也不可能与 `_scanDone()` 的分配竞态——申请 §"请评审重点 3" 的免竞态论证
  只对"已终结"分支成立，建议把注释里的结论按此**收窄措辞**。
  **坐标**：多前提（需扫描 >6s 的异常环境 + 需判负窗口内再发生一次 abort）⇒ 可达性 3；
  症状可观测（失败码上屏 + `[WiFi] ... kick failed` 串口行）⇒ 有声表 → `B/3/有声 → P3`。
- **两轴**：`PARTIALLY_VERIFIED`（CODE 静态推导 + 框架源码，无设备复现）｜
  `VIOLATES`（本项目自定 release 协议：abort 必须 stop + 等 SCAN_DONE）
- **最小修复**：快径内补一行 `esp_wifi_scan_stop();`（无扫描在跑时是 no-op，原路径本来就无条件调），
  或把判据收紧到"已终态且结果已定案"（`scanComplete() >= 0`，配合 DONE 位判断）。注释按上条收窄。
  **须补回归（HW）**：扫描 >6s 的慢环境下"扫描中 Back ×10 + 再进 4_2 应立即重新出列表"，
  串口不应出现 `release deferred`，也不应出现连续两轮 `kick failed`。

## 已通过项（核销逐条成立）

- **P1-1（Gemini+GPT）闭合，且入口覆盖穷尽**：`wifi_scan_event_ensure_registered()`
  （`:2246-2251`）在 `create4_1`（`:3159`）与 `entry4_2`（`:3719`）各调一次。
  我按 `ui_scr_mrg.c` 的**状态机**独立核了"是否存在能 abort/release 却没有注册回调的入口"：
  `scr_mgr_active()`（`:57-69`）对 DESTROYED 走 `create`+`entry`、对 CREATED/INACTIVE 走 `entry`；
  `scr_mgr_remove()`（`:78-88`）只在 `st > INACTIVE`（即 ACTIVE）时调 `exit()`。
  即 **exit 必然有前置 entry，destroy 可能无 entry**。而能进入 `wifi_scan_stop_and_release()`
  的三条路径——`exit4_2`（需 entry4_2）、`destroy4_1 → wifi_cfg_scan_abort`、覆盖层倒计时
  `wifi_cfg_scan_abort`（后两条都要求 `wifi_scan_state == WIFI_SCAN_RUNNING`，而扫描只能由
  4_1 自己的 UI 发起 ⇒ create4_1 必已跑过）——**全部落在回调已注册之后**。闭合。
- **P1-2（Gemini+GPT / Grok P2-2b）闭合**：`wifi_autoconn_event_ensure_registered()`
  （`:2427-2434`）被 `wifi_autoconn_start()`（`:2443`）与 `wifi_autoconn_restart()`（`:2482`）调用；
  `s_autoconn_active = true` 只在游标这两处出现，而 `wifi_cfg_connect` 成功路径确实调
  `wifi_autoconn_restart()`（`:2799`）⇒ 空槽首连→断链→5 次上限闭合。
  `wifi_autoconn_retry_now()` 对未激活的管理器直接 return（`:2470`），不存在"绕过 restart 激活"的第二入口。
- **F1（我上轮 P2，wedge）闭合**：快径确实命中"SCAN_DONE 已投递并计数、1s tick 未 collect"
  的窗口（4_2 判据是 `wifi_scan_async_inflight`，该标志只在 tick 里清），wedge 不再产生；
  F1′ 只是同一处的判据**过宽**，不是原缺陷复发。
- **F2 / Grok P2-1 闭合**：`s_scan_dropped_link` 改为粘滞置位（`ui_deckpro_port.cpp:459`），
  仅 `ui_wifi_scan_reconnect()` 清零（`:481`），4_2 第二轮 kick 不再误清 ⇒ 退出即 `retry_now()`，
  不等 65s guard。
- **F3 闭合（结论正确且注释已按事实改写）**：4 处 `cursor.show = 0` 直写已删；`ui_deckpro.cpp:3216/3217`
  与 `:3230/3231` 确认真正生效的是 CURSOR part 的 `anim_time = 0` + `bg_opa = TRANSP` 组合。
- **F4 闭合**：状态行只在 CONNECTED / DISCONNECTED 两种状态写标签（`:2955-2969`，`write`
  指针为 NULL 时不写），过渡态只记 `s_shown_link`——不再有"用旧缓冲重写标签"的残余路径
  （`:2557`/`:2659`/`:2685`/`:2693`/`:2712`/`:2743`/`:2749`/`:2779`/`:2783`/`:2819` 等其它写入点
  都在各自事件路径上，属设计内）。
- **P2-2a 闭合**：`server_ok` 即 `s_wa_cfg_gen++` + `wa_cache_clear()` + `pp_notify_cfg_changed()`
  （`ui_whoami.cpp:531-536`）。**双调用无害**已核：`pp_notify_cfg_changed()` 函数体只有
  `s_pp_autosynced = false;`（`ui_penpal.cpp:334-337`），幂等。
- **上轮 3 个 Nit 均有处置**：Nit-1 灰色计数 14→15（§23 勘误）、Nit-2 文件名区间记法
  （`docs/reviews/README.md` 命名补充 + §27 流程项）、Nit-3 `s_shown_link` 进屏重置
  （`:2229` 提为文件级 + `:3305` 重置 + 注释）。
- **§7.2 入库轮义务已履行**：`docs/issue_list.md:864`（§27）确含上轮 P2-5
  （`idle_sleep_timer_cb` + `ota_busy()` 门控，`f8c6b38`）补录条目 + 五方发现处置表，
  且"核销声明必须核代码，不是核提交说明"的方法论教训已入库。这是我上轮点名的欠账，已闭合。
- **F5 勘误成立**：本结果 F1′ 的框架源码核查**独立复现**了该勘误的机制（`_scanTimeout`
  判负同样产出 -2），"driver aborted the connected scan"确非唯一归因；§27 处置表已写明"未定论"。

## 验证说明

- **评审方环境**：本机装有 PlatformIO（`python -m platformio`）；**无设备**（COM5 不在本会话可达范围）。
- **独立复跑（BUILD）**：`git worktree add <tmp> bb28ce2` + 拷入 gitignored `config_keys.h` +
  `python -m platformio run -e pda2` → **SUCCESS，44.70 s**；`RAM 57.4% (188140/327680)`、
  `Flash 44.2% (2896249/6553600)`。相对上轮基线（`b92d021`：RAM 55.8% / Flash 36.4%）的
  +1.6pp / +7.8pp 与 Font_Hanzi_16（~488 KB）+ LevelTest + WordBank 三个模块的体量相符（本轮为超集，
  非单一 delta 的涨幅归因）。
- **未独立复现**：申请 §"请评审重点 5" 的三条真机反例（P1-1 / F1 / F2）**按申请声明一律视为
  CLAIM_ONLY**，本结果不引用其结论；F1′ 的第 4 条反例同样待真机。
  `0bc0fcc`/`ae39763` 各自声称的 `pio run` SUCCESS 与 `flash_verified.py` 分块校验为
  `CLAIM_ONLY`（我未复跑那两个 SHA 的独立构建，理由：`bb28ce2` 是其代码超集且构建通过）。
- **`git diff --check bdb75bf 7bab081`**：我复核通过（无空白错）。

## 对称三格

- **① 是否核了全区间 diff**：是。8 文件全部归属并逐个看过；两个 docs 文件（`CHANGELOG.md`/
  `docs/issue_list.md`）按"不评代码面"处理，但**其断言逐条与代码对齐核过**（F5 勘误、§23 计数、
  §27 处置表）；评审产物文件（ds4 结果）只核"未覆盖既有结果"。
  区间外顺带核了 `ui_deckpro.h`/`factory.ino`/`penpal_api.*`（无本区间改动）。
- **② 快照是否独立复跑**：是（见上，命令 + 输出摘要）。**无设备 ⇒ 未标 VERIFIED**，
  真机面一律 `CLAIM_ONLY`/`UNKNOWN`，不以"编译过"外推。
- **③ 申请"最没把握"各处是否构造了反例**：申请未列 §6 最没把握清单（修复轮），
  改为 §"请评审重点"5 条 + CLAIM_ONLY 声明；我逐条给了结论：1（入口穷尽）=已核 ✓、
  2（首连路径）=已核 ✓、3（免竞态论证）=**反例成立，见 F1′**、4（状态行残余）=已核无残余 ✓、
  5（反例声明）=尊重声明、未采信 ✓。

## §9 评审自审清单（承重档）

```text
【异步/契约】1 非 UI 线程调 LVGL？无新增跨线程 UI（本区间只动 WiFi 事件回调与 UI 线程代码）；
  2 busy 是否只被 gen 匹配结果释放？本区间未改 busy/gen 结构，P2-2a 只扩了"哪个分支才 ++gen"；
  3 worker new 的结果是否 delete？本区间未动队列/所有权代码；
  6 结果第一字段 gen / 迟到丢弃：未改（上轮已核）。
【内存/栈】7 无 memset std::string；8 无 >400B 聚合赋值（本区间无新增聚合）；9 无 lv_conf 改动。
【秘密】10/11 本区间不涉 key 缓冲；git grep 真实 key 仍 0 hits（未复跑，沿用既有证据）。
【硬件/实时】12 变体：4G 板无 DAC 与本次无关；13 EPD：状态行/扫描提示层写入次数未增；
  14 原子写：不涉。
【方法论】15 每条发现给了 file:line + 反例序列 ✓；16 作者编译/刷写声明按 CLAIM_ONLY 处理并
  独立重跑 BUILD ✓；17 复跑作用域 = 干净 worktree @ bb28ce2（非脏工作树，见下"评审环境提示"）✓；
  18 本轮核心函数（两个 ensure_registered）确有生产调用方（create4_1/entry4_2/start/restart）✓；
  19 反例库：扫描生命周期/事件计数协议/状态机页面切换三项逐一推演 ✓；20 与作者结论相反处
  （F1′）已核对框架源码而非比票数 ✓。
```

**评审环境提示（非缺陷，供台账）**：评审时工作树**非干净**——`examples/pda2/` 下
`ui_newdict.cpp`/`penpal_api.*`/`ui_deckpro.cpp`/`fw_version.h` 有未提交的 **v1.20 在改内容**
（NewDict 搜索优先 + Home/List 标签页 + exam books，菜单条也把 NewDict 挪到第 1 屏）。
本结果与另两份分段结果**一律以 commit SHA 为准**（`git show <sha>:<path>`），不采信工作树。
建议入库 v1.20 时**另开申请**，不要把未评审内容混进本轮结果的范围。

## 审批意见：[ ] A 全量接受  [ ] B 退回修订  [x] C 部分接受

- **接受**：本区间 3 个 commit 的全部核销主张（P1-1 / P1-2 / P2-1 / P2-2a / F1 / F3 / F4 / F5
  + 3×Nit + §7.2 入库义务）**成立**，可继续沿 v1.14/v1.15 代码推进。
- **保留**：**F1′（P3，应修）**——终态快径的判据需收窄（或补 `esp_wifi_scan_stop()`），
  绑 **G-真机**（下一次 WiFi 链路真机回归时一并修正 + 复验）；同时把申请 §"请评审重点 3"
  的免竞态结论按 F1′ 收窄措辞。
- **入库动作（作者）**：F1′ 登记 `docs/issue_list.md`（severity=P3 / first_commit=`ae39763` /
  evidence=`ui_deckpro.cpp:3372-3384` + `WiFiScan.cpp:142-158` / gate=G-真机），
  并把本条真机回归项追加到 §"真机反例待验"清单（现为 3 条 → 4 条）。
