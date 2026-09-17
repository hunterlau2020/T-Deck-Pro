# 评审结果：v1.2 修复轮核销 + WiFi 全链路批次 v1.3–v1.12（1561b41..b92d021，gemini）

- **评审日期**：2026-09-18
- **申请文件**：[session-batch-review-request-1561b41..b92d021.md](session-batch-review-request-1561b41..b92d021.md)
- **评审提交**：`1561b41..b92d021`（首末含两端，共 14 个 commit，19 个文件 +863/−68 行）
- **评审基线**：HEAD=`12c9e7b`（本批申请落地的 commit）
- **评审依据**：`docs/review_guide.md` v1.3 代码口径（§2.3 A/B/C：**A 不得挂 P0/P1 必修项**；§0 原则 2"证据 > 票数"；§0 原则 3"独立复现优先"）。
- **评审结论**：**C 部分接受；当前不通过 G-合入**。
  - **已核销**：上轮四项发现（Whoami cfg-epoch、OTA NTP 解耦、idle_sleep OTA 守卫、IPC 契约登记）经反例验证全部真实闭环；
  - **阻断项**：WiFi 状态机存在 **2×P1 生命周期事件注册缺失**，导致常规操作路径下扫描永久锁死与重连上限失效，均须当批修复；
  - **建议项**：自评疑点推演形成 **4×P3/Nit** 优化建议（可移交后续）。
- **门禁状态**：
  - **G-合入**：**不通过**（存在 2 处当批引入的状态机 P1 阻断项，必须当批修复并补回归）。
  - **G-真机**：**暂缓**（真机目前虽验证了主路径，但冷启动直进 4_2 中途退出、以及无槽位新配网后的断链路径尚未覆盖两个 P1 反例）。

---

## 1. 上轮发现核销复核（独立反例验证）

按 `review_guide.md` §0 原则 8，对 `095e41a..301c571` 遗留的 4 项问题逐条验证：

### 1.1 P2-5：idle_sleep OTA 覆盖层守卫（上轮失实核销的二次复核）——✅ 核销真实成立
- **上轮问题**：上一轮申请声称“核实既有实现已防”，但实际 `idle_sleep_timer_cb` 未改动，被 Claude 构造反例击穿并打 C。
- **本轮复核**：走查 `examples/pda2/ui_deckpro.cpp:4987`（当前文件第 5140 行附近）：
  ```c
  if (ota_busy()) {
      s_last_activity_ms = millis();
      return;
  }
  ```
- **反例推演**：在 OTA 下载覆盖层显示时静置 5 分钟（无按键/触摸），定时器触发 `idle_sleep_timer_cb`，命中 `ota_busy()` 分支，重置 `s_last_activity_ms` 并直接 return，不再压栈 Sleep 屏。反例被有效阻断。
- **两轴**：`VERIFIED`（CODE）/ `CONFORMS`。

### 1.2 P1：Whoami 旧账号在途 profile 回写 NVS——✅ 核销成立
- **本轮复核**：走查 `examples/pda2/ui_whoami.cpp`：
  - `wa_msg_t` 与 `wa_req_t` 第一字段均对齐加入 `uint32_t cfg_gen`（契约规则 2）；
  - 保存配置成功回调 `wa_cfg_save_cb` 末尾显式执行 `s_wa_cfg_gen++;`；
  - 消费端 `wa_consume()` 校验 `if (m->kind == WA_REQ_PROFILE && m->cfg_gen != s_wa_cfg_gen) { delete m; continue; }`。
- **反例推演**：启动 Refresh A，立即切换配置输入并保存 B。A 的 HTTP 请求稍后返回，`m->cfg_gen` 与当前代次不匹配，被直接 `delete` 丢弃，旧账号数据绝不会写入 NVS 缓存。
- **两轴**：`VERIFIED`（CODE）/ `CONFORMS`。

### 1.3 P2：OTA NTP 门控解耦——✅ 核销成立
- **本轮复核**：`examples/pda2/ota_update.cpp:309, 398` 两处已移除对 `http_get_tls_mode() != HTTP_TLS_INSECURE` 的短路依赖。只要 URL 是 `https://`，一律执行 `http_ensure_time(5000)`。
- **两轴**：`VERIFIED`（CODE）/ `CONFORMS`。

### 1.4 P2-6：IPC 契约补登——✅ 核销成立
- **本轮复核**：`docs/async_ipc_contract.md` 表格第一节补齐了 `Whoami Profile/Test`、`OTA Check`、`OTA Update` 三行。

---

## 2. 阻断项（Findings — P1）

### P1-1：未先进入 4_1 时，从 4_2 扫描中途按 Back 退出会导致 4_2 扫描功能永久死锁

- **位置**：`examples/pda2/ui_deckpro.cpp:3114-3117`（事件注册仅在 `create4_1`）、`3329-3353`（`wifi_scan_stop_and_release`）、`3616-3617`（4_2 轮询前置守卫）
- **代码事实**：
  1. `ARDUINO_EVENT_WIFI_SCAN_DONE` 事件回调 `wifi_scan_done_cb` **仅在 `create4_1()` 内部被条件注册**（`:3114` `if (!s_scan_event_registered)`）；
  2. 4_2 界面（`create4_2` / `entry4_2`）**从未注册过该回调**；
  3. 当 4_2 正在进行异步扫描时（`wifi_scan_async_inflight == true`），若用户按 Back 键退出，`exit4_2()` 会调用 `wifi_scan_stop_and_release()`；
  4. `wifi_scan_stop_and_release()` 将 `s_scan_release_pending` 置为 `true`，并尝试 `while (scan_release_is_pending() && millis() - t0 < 3000)` 等待回调将其清为 `false`；
  5. 只有 `wifi_scan_done_cb` 才会清除 `s_scan_release_pending`。在未注册回调的情况下，等待 3000ms 超时后打印 `"scan abort timeout - release deferred"`，`s_scan_release_pending` **永久保持为 `true`**！
- **反例攻击（SIM）**：
  1. 设备开机后，用户直接从主菜单进入 `WiFi Scan`（4_2），**全程从未打开过 `WiFi Config`（4_1）**；
  2. 4_2 启动异步扫描（`wifi_scan_async_inflight = true`）；
  3. 用户在扫描完成前按下 Back 键退出 4_2；
  4. `exit4_2()` 调用 `wifi_scan_stop_and_release()`，由于没有事件回调，3000ms 超时，`s_scan_release_pending` 永久锁定为 `true`；
  5. 用户再次进入 4_2，每秒定时器触发 `wifi_scan_timer_event()`：
     ```c
     if (!ui_wifi_scan_release_clear())
         return;   /* a previous abort's SCAN_DONE is still pending - next tick */
     ```
     `ui_wifi_scan_release_clear()` 永远返回 `false`，4_2 从此永远无法启动扫描，一直显示静止文案，**扫描功能彻底死锁，直到整机硬复位重启**。
- **定级与影响**：
  - 后果 **C**（核心扫描功能永久卡死，重启前不可恢复）；
  - 可达性 **2**（开机直进 4_2 并在扫描中按返回，极为常见的常规操作）；
  - 静默（表面无 panic，串口仅一条 deferred 超时，之后所有 tick 静默丢弃）。
  - 坐标：`C/2/静默 → P1`（按 `review_guide.md` §4.2）。
- **两轴**：`VERIFIED`（CODE 原文可唯一定位）/ `VIOLATES`（状态机释放语义未闭环）。
- **最小修复**：
  提取幂等的事件注册函数：
  ```c
  static void wifi_scan_event_ensure_registered(void) {
      if (!s_scan_event_registered) {
          WiFi.onEvent(wifi_scan_done_cb, ARDUINO_EVENT_WIFI_SCAN_DONE);
          s_scan_event_registered = true;
      }
  }
  ```
  在 `create4_1`、`entry4_2` 以及 `ui_wifi_scan_prepare()` / `ui_wifi_scan_async_start()` 启动前无条件调用。

---

### P1-2：首次手动配置成功后，断网自动重连不受 5 次上限约束（退化为无限重连）

- **位置**：`examples/pda2/factory.ino:855-866`、`examples/pda2/ui_deckpro.cpp:2391-2409, 2441-2483`
- **代码事实**：
  1. 开机启动时（`factory.ino`），只有在活动槽已有有效 SSID 时才调用 `wifi_autoconn_start()`；
  2. `WiFi.onEvent(wifi_autoconn_event)` **仅在 `wifi_autoconn_start()` 中注册**（`ui_deckpro.cpp:2404`）；
  3. 当设备首次使用、或 NVS 中无可用 WiFi 槽位时，开机不会调用 `wifi_autoconn_start()`，事件回调从未注册；
  4. 用户进入 4_1 手动配置并点击 Connect 成功后，代码调用 `wifi_autoconn_restart()`（`:2759`）；
  5. `wifi_autoconn_restart()` 仅重置了 `s_autoconn_fails = 0`、`s_autoconn_active = true`、`s_autoconn_next_ms`，**并没有注册 `wifi_autoconn_event`**！
  6. 随后当该 AP 掉电或超出覆盖范围时，驱动产生 `DISCONNECTED` 事件，但因为没有注册回调，`s_ac_disconnected` **永远不会被置为 true**！
- **反例攻击（SIM）**：
  1. 新机/清空 NVS 后开机，初次手动在 4_1 输入密码连接 AP 成功；
  2. AP 掉线或断网，连接中断；
  3. 进入 `wifi_autoconn_poll()`：
     - 因为 `s_ac_disconnected` 永远是 `false`，`s_autoconn_fails` 永远不会自增，永远不会达到 `>= WIFI_AUTOCONN_MAX_TRIES (5)` 的停机分支！
     - 每次 65 秒 guard 窗口到期后（`now - s_autoconn_next_ms >= 0`），检测到 `status != WL_CONNECTED`，便**无休止地执行 `WiFi.begin(ssid, pass)`**！
  4. 结果：v1.5 承诺的“5 次尝试上限 + 指数退避 + 放弃后 STA idle 避免卡顿”在初次配网场景下**彻底失效**，退化回永久占用 STA 的无限重试死循环。
- **定级与影响**：
  - 后果 **C**（破坏状态机契约，STA 永久处于 connecting 震荡导致后台与扫描卡顿）；
  - 可达性 **2**（首次配网常见路径）；
  - 静默（无报错日志，静默破坏 5 次上限保证）。
  - 坐标：`C/2/静默 → P1`。
- **两轴**：`VERIFIED`（CODE 原文可唯一定位）/ `VIOLATES`。
- **最小修复**：
  提取幂等注册函数 `wifi_autoconn_event_ensure_registered()`，确保在 `wifi_autoconn_start()` 与 `wifi_autoconn_restart()` 均被执行。

---

## 3. 改进建议（Findings — P3 / Nit）

以下条目为对申请书 §3 自评疑点的推演分析，不属于阻断合入的致命缺陷：

### P3-1（可维护性）：直接强转并操作 `((lv_textarea_t *)ta)->cursor.show`
- **位置**：`ui_deckpro.cpp:2874-2875, 3255-3256`
- **分析**：代码通过强转 `(lv_textarea_t *)` 直接修改 LVGL 私有位域 `cursor.show = 0`。在当前 vendored 的 LVGL 8.3 环境下物理有效，但存在跨版本脆弱性。
- **建议**：后续可封装宏或辅助函数集中隔离。

### P3-2（交互体验）：4_2 选网跳入 4_1 触发旧 AP 短暂重连
- **位置**：`ui_deckpro.cpp:3691` 与 `3263-3311`
- **分析**：用户在 4_2 选中新 AP 选网跳入 4_1 时，`exit4_2` 会无条件调用 `ui_wifi_scan_reconnect()`。如果此前处于已连接态，后台会开始重连旧 AP。而此时用户正在 4_1 界面为新 AP 输入密码，后台若连上旧 AP 会将状态行刷新为 `"IP: ..."`，容易造成视觉误导。
- **建议**：在 `entry4_1` 检测到 `wifi_scan_pick_ssid[0]` 时显式调用一次 `wifi_autoconn_hold(true)` 挂起重连。

### P3-3（代码洁癖）：`s_shown_link` 局部静态变量作用域
- **位置**：`ui_deckpro.cpp:2912`
- **建议**：提升为文件作用域静态变量，并在 `destroy4_1` 或 `entry4_1` 中规范重置。

### P3-4（两屏一致性）：4_1 单次扫描对 CJK 的过滤
- **位置**：`ui_deckpro.cpp:2689-2699` vs `ui_deckpro_port.cpp:535-544`
- **建议**：4_1 扫描未过滤 CJK，在水墨屏轮播会遇到方块。建议统一复用 `ui_wifi_scan_collect` 的过滤逻辑。

---

## 4. 验证说明与对称三格（承自 §7.2）

| 检查项 | 结论 | 证据 / 过程 |
|---|---|---|
| **① 全区间 diff** | **通过** | 已审 `1561b41..b92d021` 全量 19 个文件，863 行增量逐行核查，涵盖所有改动。 |
| **② 快照独立复跑** | **部分（纸面推演）** | 本地环境无 `platformio` CLI（BUILD 为 `UNKNOWN`）；状态机时序与两条 P1 反例全部基于源码严格完成纸面推演（SIM）。 |
| **③ 作者最没把握处反例** | **通过** | 对申请书自评 5 处疑点全部构造反例推演，并深挖发现了两处未被注册的事件回调状态机死锁（P1-1 与 P1-2）。 |

---

## 5. 审批意见

- [ ] **A 全量接受**
- [ ] **B 退回重构**
- [x] **C 部分接受**（上轮 v1.2 修复核销通过；本轮新增代码存在 **2×P1 状态机阻断项**，须当批修复并补齐真机用例后方可通过 G-合入）

> **评审员签署**：Gemini &nbsp;&nbsp;&nbsp;&nbsp; **日期**：2026-09-18
