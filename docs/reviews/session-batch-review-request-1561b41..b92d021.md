# 评审申请：v1.2 修复轮核销 + WiFi 全链路修复批次 v1.3–v1.12（1561b41..b92d021）

- **申请人**：Claude（ZCode 代理）；**申请日期**：2026-09-17
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **关联 commit**（14 个，首末含两端）：
  - `50263cf` — **v1.1**：whoami FW label/状态行 EPD 不可见（灰≥128 阈值→白）、
    wifi 扫描 CJK 空洞清表、开机 `[FW]` 串口打印、minor bump
  - `eb85b09` — v1.1 续：其余 12 处灰改黑机械清扫（OTA/AI Test/Chat/Voice/
    Weather/Calc/Dict/PenPal×2/Wifi 状态行）
  - `f8c6b38` — **v1.2 修复轮**：上轮三方评审处置（P1 whoami cfg-epoch、
    P2 OTA NTP 解耦、P2-5 idle-sleep ota_busy re-arm、P2-6 IPC 契约登记）
  - `c1d1f13` — **v1.3**：扫描 -2 定案——重连循环与扫描互斥（`ui_wifi_scan_
    prepare/reconnect`），issue_list §24
  - `67d2fc8` — **v1.4**：4_2 异步扫描（同步扫描阻塞 UI 2-3s）+ scan-pick
    三态（匹配槽/首空槽/全满拒绝），issue_list §25
  - `f757a93` — **v1.5**：有界自动重连管理器（5 次 + 指数退避 2.5s→40s，
    放弃后 STA 空闲）
  - `cf44bf7` — **v1.6**：充电中（VBUS in）自动深睡 5→10 分钟
  - `692e3a7` — **v1.7**：4_2 扫描循环全程挂起管理器（kick 撞 begin 竞争）
  - `8e1e542` — **v1.8**：Disc 断开按钮 + 扫描失败码上屏 + kick/collect 诊断
    打点 + release-pending kick 防御
  - `536cb30` — **v1.9**：已连接态异步扫描被驱动中止（collect r=-2 实证）→
    统一空闲态扫描 + 丢链立即重连
  - `794ca4b` — **v1.10**：4_1 光标修复第一轮（style 层 + 空退格 banner）
  - `cb133a1` — **v1.11**：`cursor.show` 绘制门直写 + exit4_2 无条件恢复重连
  - `74a7e72` — **v1.12**：4_1 光标全灭（">"标签=焦点）+ 状态行跟随真实链路
  - `b92d021` — docs：issue_list §26（v1.10–v1.12 三轮修复史台账）
- **背景**：上轮（`095e41a..301c571`）三方评审 = Claude C / GPT C / Grok A
  （结果 `session-batch-review-result-095e41a..301c571-{claude,gpt,grok}.md`）。
  `f8c6b38`（v1.2）为其修复轮——**本轮申请同时覆盖修复核销与 delta 复审**
  （上轮评审员的复审请求即此）。v1.3 起为真机反馈驱动的 WiFi 全链路迭代
  （9 个版本、11 次真机刷写验证）。
- **硬件**：机 `28:37:2f:91:2c:20`（V1.0/4G，COM5）**已烧录 v1.12**
  （flash_verified.py 5/5 块校验 + 整段回读 MD5 一致，用户确认全部症状修复）；
  机 `10:20:ba:34:18:5c`（COM7）按用户指令留旧版；机 `10:20:ba:34:19:ec` 未连接。

## 头部自校

- `git rev-list --count 1561b41..b92d021` = **14**（与上方列表一致）
- `git diff --name-only 1561b41..b92d021 | wc -l` = **19** = 归属表行数
- `git diff --stat 1561b41..b92d021 | tail -1` = **863 insertions / 68 deletions**

### 区间文件归属表（19 文件）

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/ui_deckpro.cpp` | **评（重点）** | WiFi 全链路主体：slot 管理、扫描状态机、autoconn 管理器、4_1/4_2 生命周期、光标/状态行、Disc/按钮 |
| `examples/pda2/ui_deckpro_port.cpp` | **评（重点）** | port 层：`ui_wifi_scan_prepare/reconnect/async_start/collect`、last_ret、release_clear |
| `examples/pda2/ui_deckpro_port.h` | **评** | 上述接口声明 + 语义注释 |
| `examples/pda2/factory.ino` | **评** | v1.2 `[FW]` 打印；v1.5 开机 autoconn_start 替换无限 autoReconnect；v1.6 loop 挂 autoconn_poll |
| `examples/pda2/fw_version.h` | **评** | v1.1→v1.12 版本链（策略注释） |
| `examples/pda2/ota_update.cpp` | **评** | v1.2 P2：NTP 门控解耦（两处） |
| `examples/pda2/ui_whoami.cpp` | **评** | v1.2 P1：cfg-epoch 守卫 + FW label 位置/配色 |
| `examples/pda2/ui_ai_cfg.cpp` / `ui_ai_chat.cpp` / `ui_calculator.cpp` / `ui_dictionary.cpp` / `ui_penpal.cpp` / `ui_penpal_write.cpp` / `ui_voice_ai.cpp` / `ui_weather.cpp`（8 文件） | **评（低风险）** | v1.1 灰改黑机械清扫（每文件 1-4 行样式改动）；ui_voice_ai 另含 v1.2 无改动（上轮已评部分不在本轮 diff 内重述） |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` / `docs/async_ipc_contract.md` | 不评（纯文档） | 台账/契约行登记（§24–§26）；P2-6 闭合的文档面 |

## §1 变更明细（WiFi 全链路，v1.3–v1.12）

### §1.1 驱动层事实（真机实证，评审的物理前提）

1. ESP32 WiFi STA 三态可扫性：**已连接（异步扫描会被驱动中途掐掉，
   `collect r=-2 mode=0x1 status=3`）/ idle（100% 可靠）/ connecting
   （`esp_wifi_scan_start` 拒绝）**——v1.9 起所有扫描统一从 idle 执行。
2. 开机 `setAutoReconnect(true)` 在目标 AP 不在场时每 2.4s 无限重连，
   STA 永久 connecting → 两条扫描路径全部 -2（v1.3 定案）。
3. 本项目 LVGL 构建（lib/lvgl ~8.3）的 textarea **无 DEFOCUSED 光标
   处理**：FOCUSED 一发 blink 即启动永不停止；`draw_cursor()` 首行
   `cursor.show==0 → return` 是唯一可靠绘制门（v1.10–v1.12 三轮收敛）。
4. `ui_scr_mrg.c`：push 时旧栈屏 `exit()` 执行、新栈屏 `create()+entry()`
   每次 push 重跑（v1.12 读源码核实，此前靠注释推断踩坑）。

### §1.2 WiFi 状态机（核心评审对象）

**autoconn 管理器**（ui_deckpro.cpp，factory loop 每 tick poll）：
- 5 次尝试上限 + 指数退避 2.5s→40s，放弃后 STA 空闲直到显式连接/重启
- 事件驱动（GOT_IP 重置 / DISCONNECTED 计数），65s 无事件 guard 自愈；
  已连接态 guard 到期只重臂不重连
- 接口：`start/hold/stop/retry_now/poll` + WiFi 事件回调仅置 volatile 标志

**port 层扫描对**（ui_deckpro_port.cpp）：
- `prepare()`：hold(true) + `s_scan_dropped_link`（WL_CONNECTED 时置）+
  disconnect + delay(100) ——**含已连接链路**（v1.9 统一 idle 扫描）
- `reconnect()`：hold(false) + dropped_link 时 `retry_now()` 立即重连
- `async_start()`：kick 被拒重试一次（disconnect + 200ms）；失败打
  `r/mode/status` 诊断
- `collect()`：结果拷贝（CJK/隐藏网压缩）+ scanDelete；**不 reconnect**
  （4_2 循环语义，v1.7）；失败打点
- 4_2：1s tick 轮询 + 10s kick 节奏 + release-pending 守卫；进屏
  hold(true)、出屏无条件 reconnect（v1.11）
- 4_1：单次扫描（pending-release 3s 等待协议，上轮评审 1.2/1.3 机制
  保留），终态三出口 reconnect

**4_1 UI**：pick 三态（SSID 精确匹配槽保留密码 / 首空槽 / 全满 banner
拒绝）；光标全灭（">"标签=焦点，`cursor.show=0` + CURSOR part TRANSP +
不再发 FOCUSED）；状态行跟踪真实链路状态（仅变化时更新，串口
`[WiFi] link state ->` 打点）；空退格跳字段 banner 明示；Disc/Connect/
Save/Clear 四按钮。

### §1.3 其他

- v1.6：`idle_sleep_timer_cb` 窗口 = `ui_battery_25896_is_vbus_in() ? 10min
  : 5min`（I2C 失败安全退化为 5min）。
- v1.1 灰色清扫：14 处状态行/边框 `lv_color_black()`（EPD 二值阈值下
  0x9E 灰=白=不可见，issue_list §23）。
- v1.2 修复轮明细见 §2 核销表。

## §2 修复轮核销表（095e41a..301c571 三方发现 → f8c6b38 处置）

| 发现（三方） | 处置 | 证据 |
|---|---|---|
| **P1**（GPT/Grok）Whoami 旧账号在途 profile 回写 NVS | ✅ 已修：请求/结果携带配置代次 `s_wa_cfg_gen`（Cfg 保存成功 ++），迟到结果代次不匹配即丢弃 | `ui_whoami.cpp` wa_msg/wa_req 首字段 gen；`async_ipc_contract.md` §1 补行。反例真测（Refresh A→Save B→重启）⏸ 仍未做（§3） |
| **P2**（GPT）OTA NTP 与 AI "Trust self-signed" 开关耦合 | ✅ 已修：HTTPS 分支无条件 `http_ensure_time(5000)` | `ota_update.cpp` 两处（313/402 附近） |
| **P2-5**（Claude/Grok 证伪上轮核销）idle_sleep 无条件 push Sleep 屏打断 OTA 覆盖层 | ✅ 真修：`idle_sleep_timer_cb` 加 `ota_busy()` re-arm | 上轮"核实既有实现"实为只核 sleep_do_enter——**核销失实教训已入 CHANGELOG** |
| **P2-6**（GPT/Grok）OTA/Whoami 异步任务未登记 IPC 契约 | ✅ 闭合：契约 §1 表补三行（含配置代次语义、指针通道、单飞前置位） | `grep whoami docs/async_ipc_contract.md` 命中 |

## §3 遗留与自评（评审重点请求）

**自评最没把握的五处（请重点看）**：

1. **`((lv_textarea_t *)ta)->cursor.show` 直写**（cb133a1/74a7e72）：字段
   在公开头文件但语义属 LVGL 内部——版本升级脆弱；是否有更稳的等效
   手段（或应在项目内封一个compat宏）？
2. **WiFi 标志组合状态机**：`s_autoconn_active/held/fails/next_ms` ×
   `s_scan_dropped_link` × 4_1/4_2 两套扫描生命周期的交互——请检查边界
   序列（如 4_1 扫描中直接 push 4_2 再退出、Disc 后立即扫描、管理器
   放弃后 pick→Save 连接等）是否存在标志泄漏/永久卡态。
3. **exit4_2 无条件 reconnect 与用户立即 Connect 的竞争**：旧槽 begin
   可能先于用户 begin 执行——功能上用户最终生效，但中间的旧 AP 短连
   是否有副作用（如 NTP 突发/状态行抖动）？
4. **状态行 `static wl_status_t s_shown_link`**（74a7e72）：跨屏残留的
   初值跳过风险（重进 4_1 时恰好状态相同则不刷新——entry 的
   wifi_cfg_load 已写过一次，理论一致，未穷举）。
5. **充电 10 分钟深睡**（cf44bf7）：依赖 BQ25896 VBUS 轮询读取，I2C
   长期抖动场景未测。

**待真测清单（不影响本轮结论，登记备查）**：
- OTA 回滚矩阵（PENDING_VERIFY 状态链 + P2-5 反例）——连续三轮评审
  遗留，建议排期
- P1 cfg-epoch 反例真测（Refresh A→Save B→重启）
- COM7 机升级 v1.12 + 双机一致性
- 充电态 10 分钟窗口实测（插 USB 静置）

**靶向建议**（代码口径 §2.3）：
- **A（真机已验）**：扫描两路径出列表、pick 三态、重连恢复、光标全灭、
  状态行刷新、Disc、[FW]/诊断打点——用户 2026-09-17 确认修复
- **B（推演请评审员反例攻击）**：§3 自评 2/3/4
- **C（无需机）**：§1.1 事实链核对、核销表证据核对

## §4 一致性声明

- 本申请由申请人撰写；`b92d021` 前全部 14 个提交已 push 至
  `origin/HD-V2-250915`。
- 评审产物命名建议：`session-batch-review-result-1561b41..b92d021-<评审员>.md`。
