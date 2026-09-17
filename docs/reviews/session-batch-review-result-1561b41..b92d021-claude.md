# 评审结果：v1.2 修复轮核销 + WiFi 全链路修复批次 v1.3–v1.12（1561b41..b92d021，claude）

- **评审日期**：2026-09-18
- **申请文件**：[session-batch-review-request-1561b41..b92d021.md](session-batch-review-request-1561b41..b92d021.md)
- **评审提交**：`1561b41..b92d021`（首末含两端，14 commit）
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）。
- **评审范围**：§2 核销表按 §0 原则 8"验证修复而非重复发现"逐条重读原文构造反例；
  §1 WiFi 全链路新代码全量走查，重点扑向申请 §3 自评"最没把握"的五处，尝试
  用具体调用序列击穿（对称三格③）；14 个文件里 8 个"灰改黑机械清扫"文件抽查
  2 个确认改动性质与描述一致，未逐行过一遍（低风险纯样式改动，§7.3 口径）。
- **评审结论**：**A 全量接受**。§2 核销表 4 条（P1 whoami cfg-epoch、P2 OTA NTP、
  P2-5 idle-sleep re-arm、P2-6 契约登记）本轮独立复核**全部真实闭合**——尤其
  P2-5 是上一轮（`095e41a..301c571`）被我证伪过一次的"假核销"，这次真的改了
  代码。对 §3 自评"最没把握"的五处逐一构造攻击，**均未能击穿**（详见对称三格
  ③）；未发现新的 P0/P1/P2。两处 Nit（非阻断）。

---

## 复核方法

这轮申请结构清楚地分成三块：(a) 上轮三方评审（Claude/GPT/Grok）遗留发现的核销
表（§2）；(b) v1.3–v1.12 九个版本的 WiFi 全链路重写（§1，本轮实际的评审重点）；
(c) 申请方自己点名的五处"最没把握"，明确要求评审员来构造反例攻击（§3）。我按
这个结构走：核销表逐条重读代码（不信任转述），WiFi 状态机挑高风险的交互点
（autoconn 管理器 × 4_1/4_2 两套扫描生命周期的 hold/reconnect 配对）追踪到具体
函数，自评五处逐一尝试构造可复现的破坏序列。

---

## §2 核销表复核（构造反例，未能击穿）

### P1（whoami cfg-epoch）—— 成立

`wa_msg_t`/`wa_req_t` 首字段改为 `uint32_t cfg_gen`（符合
`async_ipc_contract.md` 规则 2"gen 放第一字段"）；`wa_cfg_save_cb` 保存成功后
`s_wa_cfg_gen++`；`wa_consume()` 在处理 `WA_REQ_PROFILE` 结果的**最前面**
（早于任何渲染/`wa_cache_save()` 调用）比对 `m->cfg_gen != s_wa_cfg_gen`，
不匹配直接 `delete m; continue;`。本轮独立读了这四处，判定顺序正确——不存在
"先渲染/先缓存、后来才发现代次不对"的窗口。反例真机测（Refresh A 账号在途时
立即 Save B 账号）仍是 `CLAIM_ONLY`（申请 §3 已自陈⏸），但代码逻辑本身经得起
纸面推演。

### P2（OTA NTP 与 Trust 开关解耦）—— 成立

两处 HTTPS 分支的 NTP 门控从 `strncmp(...) == 0 && http_get_tls_mode() !=
HTTP_TLS_INSECURE && !http_ensure_time(...)` 改为 `strncmp(...) == 0 &&
!http_ensure_time(...)`——去掉了对共享 `s_tls_mode` 的读取，HTTPS 场景下
NTP 校时永远强制执行，不再随 AI 页的 Trust 开关变化。本轮独立核对
`ota_update.cpp` 两处（manifest 请求 + 固件下载）均已同步修改，未遗漏第三处。

### P2-5（idle-sleep 打断 OTA 覆盖层）—— 这次真的修了

`idle_sleep_timer_cb` 在 `audio.isRunning()` 判断之后新增：
```c
if (ota_busy()) {
    s_last_activity_ms = millis();
    return;
}
```
`ota_busy()` 的声明通过文件中部 `#include "ota_update.h"`（:1061）在此处已经
可见，编译无问题（本轮独立确认没有漏 extern 声明——上一轮我没有具体检查过这个
细节，这次顺带核对了）。这条是上一轮（`095e41a..301c571`）被我明确证伪过的
"假核销"——那次申请把 `sleep_do_enter()`（拒绝真正进深睡）误当成了对
`idle_sleep_timer_cb`（拒绝 push Sleep 屏）的核实证据，这次是真的改了正确的
函数。CHANGELOG 里登记了这次教训，值得肯定——按 §0 原则 8 的方法论，这正是
"验证修复"该抓的那类问题，抓到了就该如实记录抓到了。

### P2-6（IPC 契约登记）—— 成立

`docs/async_ipc_contract.md` 表格新增 Whoami / OTA Check / OTA Update 三行，
含配置代次语义、指针通道覆盖回收纪律、单飞前置位规则的简述——本轮独立
`grep` 确认命中，内容与 `ota_update.cpp`/`ui_whoami.cpp` 的实际实现描述一致
（不是空登记）。

---

## §1 WiFi 全链路新代码：核心状态机走查

### autoconn 管理器（`ui_deckpro.cpp:2375-2483`）

有界重试（5 次，2.5s→40s 指数退避，封顶 60s）+ 事件驱动（`GOT_IP`/
`DISCONNECTED` 只置 volatile 标志，实际状态转换全部在 `wifi_autoconn_poll()`
里做，不在 WiFi 事件回调里做重活）——这个"事件回调只置标志位、轮询函数做决策"
的分工是稳妥的模式，避免了在 WiFi 驱动的事件上下文里做可能阻塞的操作。

重点核对了一个容易漏的边界：`ui_wifi_scan_prepare()` 主动断开连接
（`WiFi.disconnect(false, false)`）本身也会触发 `ARDUINO_EVENT_WIFI_STA_
DISCONNECTED`，如果这个"自己制造的"断线事件被 `wifi_autoconn_poll()` 当成
真实断线去计入 `s_autoconn_fails`，会污染重试计数。核对 `wifi_autoconn_hold
(false)`（`:2412-2420`）：
```c
void wifi_autoconn_hold(bool on) {
    if (on) { s_autoconn_held = true; }
    else {
        s_ac_disconnected = false;   /* drop the event our own prepare caused */
        s_autoconn_held = false;
    }
}
```
在恢复非 hold 状态时**先清空**了 `s_ac_disconnected`，且 `wifi_autoconn_poll()`
本身在 `s_autoconn_held` 为真时直接 `return`（不消费任何事件标志）——两者结合
确保了扫描期间产生的断线事件不会被管理器当作重试失败次数消耗掉。这个边界在
代码里被显式注释和处理了，不是侥幸。

### 4_1/4_2 扫描生命周期与 autoconn hold 配对

针对申请自评②"4_1 扫描中直接 push 4_2 再退出"，本轮具体追踪了这条路径：

1. 用户在 4_1 内点 Scan → `wifi_cfg_scan_start()` → `ui_wifi_scan_prepare()`
   （`wifi_autoconn_hold(true)`）→ `wifi_scan_state = WIFI_SCAN_RUNNING`，
   同时 `wifi_scan_overlay_show()` 挂一个 `LV_OBJ_FLAG_CLICKABLE` 的全屏
   覆盖层吞掉触摸。
2. 若此时用户通过物理 Back 键退出 4_1（`scr_mgr_pop`）——`destroy4_1()`
   （`:3497-3505`）无条件调用 `wifi_cfg_scan_abort()`，后者内部
   `wifi_scan_stop_and_release()`（停止扫描 + 等 `SCAN_DONE` 事件，最多阻塞
   3s）之后**必定**调用 `ui_wifi_scan_reconnect()`（`:3361`），恢复
   `autoconn_hold(false)` 并按需 `retry_now()`。
3. 随后 push 4_2 → `entry4_2()` 再次 `wifi_autoconn_hold(true)`——此时管理器
   已经是"未 hold"状态，重新 hold 是幂等操作，不会因为"上一次 hold 没有被
   正确释放"而出现状态错乱。
4. 退出 4_2 → `exit4_2()` 无条件 `ui_wifi_scan_reconnect()`（v1.11 从"仅
   inflight 时才 reconnect"改成无条件——`:3687-3691` 注释里写明了改动原因：
   从扫描行点击进 4_1 常常落在两次扫描的间隙，按 inflight 条件曾经漏掉过
   resume，导致断链后台永远连不回去，这条修复本身也在这次申请范围内）。

这条链路上 hold/reconnect 的配对是**由 `destroy4_1`/`exit4_2` 无条件兜底**的，
不依赖"扫描到底有没有跑完"这个易碎的条件——本轮没能构造出让 `s_autoconn_held`
卡死为 `true`（或 `s_scan_dropped_link` 卡死为 `true` 导致重连丢失）的具体
序列。**这条反例没有打穿**，判定为已过（VERIFIED via SIM，非真机压测）。

### scan-pick 三态（`entry4_1:3260-3300` 一带）

精确匹配槽保留密码 / 首空槽 / 全满拒绝——三个分支逐一读了一遍，`target` 变量
在循环里先查精确匹配（一旦命中立即 `break`，优先级高于"记录首个空槽"），
逻辑与申请描述一致，未发现槽位选择偏差。

---

## 申请 §3 自评"最没把握"五处 —— 逐一尝试反例，均未能击穿

1. **`cursor.show` 直写内部字段**：本轮确认这不是孤立的一次性 hack——
   `create4_1`（`:3255-3256`）和 `wifi_cfg_set_field`（`:2874-2875`，每次
   切换输入焦点字段都会重新执行）两处都写，说明经过 v1.10→v1.12 三轮迭代后
   已经确认 LVGL 会在 FOCUSED 事件路径上把这个字段重新置位，需要在每个可能
   重新触发 FOCUSED 的地方都显式清零，不是漏了某处的临时补丁。**接受这个
   实现，但认可申请自己提的担忧是合理的**：这是对 `lv_textarea_t` 内部布局
   的直接依赖，虽然字段在公开头文件里，但访问方式本身没有走 `lv_textarea_
   set_cursor_click_pos`/`lv_obj_add_flag` 等公开 API 的语义保证。**建议
   （Nit，不阻断）**：包一个 `wifi_cursor_force_off(lv_obj_t *ta)` 内联
   helper，把这两处直写和 `LV_PART_CURSOR` 的 `bg_opa` 设置收在一起，并在
   helper 旁边写清楚"依赖 lib/lvgl 当前版本的 `cursor.show` 布局，升级 LVGL
   前需要重新核对"——这样以后升级 lvgl 库时只有一个位置要检查，而不是散在
   两个调用点各自记不记得改。
2. **WiFi 标志组合状态机边界序列**：见上文"4_1/4_2 扫描生命周期"——具体
   构造了"扫描中退出 4_1 再进 4_2 再退出"这条路径，`destroy4_1`/`exit4_2`
   的无条件收尾让状态机在这条路径上没有卡死。另外尝试了"Disc 后立即扫描"：
   `wifi_disc_btn_cb` 调 `wifi_autoconn_stop()`（`s_autoconn_active=false`）,
   之后进 4_2 扫描时 `wifi_autoconn_hold(true)` 只是把 `s_autoconn_held`
   置真，`wifi_autoconn_poll()` 本身已经因 `s_autoconn_active=false` 直接
   短路，两个标志不冲突；退出 4_2 时 `ui_wifi_scan_reconnect()` 调用
   `wifi_autoconn_retry_now()`，但该函数首行 `if (!s_autoconn_active)
   return;`——Disc 之后管理器保持停用状态，扫描不会意外把它重新唤醒。这条
   也没有打穿。
3. **exit4_2 无条件 reconnect 与用户立即 Connect 的竞争**：`exit4_2()` 触发
   的重连是通过 `wifi_autoconn_retry_now()` 让下一次 `wifi_autoconn_poll()`
   tick 去 `WiFi.begin(旧槽)`；若用户在 4_1 里几乎同时点了 Connect/Save（对
   另一个 SSID），两次 `WiFi.begin()` 会先后发出。这确实可能出现申请自己
   提到的"旧 AP 短连"，但**后一次 `WiFi.begin()` 总是最终生效**（ESP32
   WiFi 驱动对 `WiFi.begin()` 是覆盖式的，不存在两个连接请求同时生效的
   状态）。副作用范围止于"状态行短暂抖动/一次多余的 NTP 尝试"，不会导致
   永久错连或崩溃——与申请自己的评估一致，**接受为已知的、影响面有限的
   时序噪声，不升级为缺陷**，登记 `issue_list.md` 作为观察项即可，不要求
   本轮改。
4. **`static wl_status_t s_shown_link` 跨屏残留**：`wifi_cfg_keyboard_poll()`
   （`:2912`）的这个 `static` 变量在函数第一次被调用时初值是
   `WL_NO_SHIELD`，之后跨越多次 4_1 访问持续存活（因为它是 `static` 局部
   变量而不是每次 `entry4_1` 重置的状态）。申请的担忧是"重进 4_1 时若
   `WiFi.status()` 恰好和上次退出时相同，状态行不会刷新，可能显示一条过时
   文案"。本轮验证了 `entry4_1` 确实没有主动重置这个 static 变量或强制刷新
   一次状态行——但 `wifi_cfg_load()`（`:3251` 附近，`create4_1` 里调用）会
   重新加载并显示已保存的 slot 信息，**状态行本身**（`wifi_status_lab`）在
   `create4_1`/`entry4_1` 是否有独立的"进屏立即按当前 WiFi.status() 刷新
   一次"的调用，本轮未找到——也就是说，如果用户离开 4_1 时链路是
   `WL_CONNECTED`，几分钟后重进 4_1 时链路仍是 `WL_CONNECTED`（同状态），
   状态行会保留上次显示的旧 IP 文案而不重新格式化，这在多数情况下巧合地
   仍然正确（IP 通常不变），但如果两次连接之间 DHCP 换了新 IP，这条会显示
   过期地址。**这是一个真实存在、但影响很小的显示类问题**（不影响功能，
   只影响一行状态文案的时效性）——按 §4.2 判定：`E/2/静默 → P3`（可用性
   类，需要"两次进屏之间状态碰巧相同但底层 IP 变了"这种特定序列，且是
   纯文案问题）。**列为 Nit，不占正式编号**，因为申请自己已经准确预判了
   这个边界并如实标注"未穷举"，不是被隐瞒的问题；建议后续任何一轮顺手在
   `entry4_1` 里加一行强制刷新（把 `s_shown_link` 重置为一个哨兵值或直接
   调用一次状态行刷新逻辑）。
5. **充电 10 分钟深睡依赖 `PPM.isVbusIn()` 的 I2C 读取**：`XPowersPPM` 库是
   vendored 第三方库，本轮未深入其 I2C 失败路径的具体返回值语义（超出这批
   diff 的评审面，且库代码本轮未改动）。即使 I2C 读取失败返回 `false`
   （申请自评的预期），效果是回落到更短的 5 分钟窗口，方向上是"保守"而非
   "危险"（不会导致设备在插着电源却被过早强制深睡更久，只会在个别 I2C
   抖动的 tick 上判断偏早）。接受申请自己的评估，未构造出反例。

---

## Nit（≤3，不占编号）

1. `cursor.show` 直写建议包一层 helper（见上文①）。
2. `wifi_cfg_keyboard_poll` 的 `s_shown_link` 建议在 `entry4_1` 显式重置
   （见上文自评④），避免罕见的"两次进屏状态碰巧相同但 IP 已变"导致状态行
   文案过期。

---

## 验证说明

- 本环境无 `pio`、无设备：BUILD/HW 证据沿用申请方记录（flash_verified.py
  5/5 块校验 + 回读 MD5 一致、用户 2026-09-17 确认全部症状修复），标
  `PARTIALLY_VERIFIED`（证据链完整但本轮未独立重跑）。
- 独立读取的仓库源码位置：`ui_deckpro.cpp:2375-2483`（autoconn 管理器全量）、
  `:2860-2920`（`wifi_cfg_set_field`/`wifi_cfg_keyboard_poll` 状态行）、
  `:2595-2680`（4_1 扫描启动/轮询）、`:3260-3300`（scan-pick 三态）、
  `:3320-3510`（`wifi_scan_stop_and_release`/`destroy4_1`）、`:3600-3695`
  （4_2 async 扫描循环 + `entry4_2`/`exit4_2`）、`:5240-5275`（idle-sleep
  P2-5 + 充电窗口）；`ui_deckpro_port.cpp:440-548`（`ui_wifi_scan_prepare/
  reconnect/async_start/collect`）；`ota_update.cpp`（P2 NTP diff）；
  `ui_whoami.cpp`（P1 cfg-epoch diff）；`docs/async_ipc_contract.md`（P2-6
  grep 复核）。8 个"灰改黑"文件抽查 `ui_ai_cfg.cpp`/`ui_weather.cpp` 两个，
  确认改动仅限颜色样式调用，未引入行为变化，其余 6 个未逐行复核（低风险，
  §7.3 口径下不要求）。

---

## 对称三格（§7.2）

1. **全区间**：19 个文件归属表本轮独立用 `git diff --name-only`/`git diff
   --stat` 复算一致（14/19/863+68- 三项头部自校数字核对无误）；重点落在
   WiFi 状态机而非申请靶向清单本身，额外主动追踪了 hold/reconnect 配对
   这条申请没有直接点名、但由自评②间接指向的具体路径。
2. **独立复跑**：无 pio/无设备，SIM 级纸面推演为主。核销表四条逐条重读
   `file:line` 原文；WiFi 状态机的边界序列（扫描中退出、Disc 后扫描、
   连续 push/pop）均按调用顺序手工走了一遍代码路径，未发现能让标志位
   永久卡死或 hold/reconnect 配对错位的输入序列。
3. **申请"最没把握"五处**：逐条见上文，五处全部尝试构造反例，**无一击穿**——
   如实记录"打不穿"而不是为了显得有产出而夸大成缺陷（避免过度追求完美/
   无谓地制造 P3，参照 `docs/review_guide.md` §0 原则 11 的精神，虽然这是
   代码评审不是设计稿评审，但同样的克制原则适用）。仅④一处发现真实存在但
   影响极小的文案时效性问题，如实列为 Nit。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 2 busy/hold 释放：`s_autoconn_held`/`s_scan_dropped_link` 的所有设置点
      都能在 `destroy4_1`/`exit4_2`/`wifi_cfg_scan_abort` 任一终态路径下被
      释放，未发现遗漏分支。
- [x] 4 队列/状态机先决条件：autoconn 管理器对 `s_autoconn_active`/`held`
      的组合判断在各个入口函数（start/hold/stop/retry_now/restart/poll）
      间自洽，未见互相矛盾的赋值顺序。
- [x] 15 每条复核（含"打不穿"的自评五处）均给出具体 `file:line` + 推演路径。
- [x] 16 申请 HW 证据有具体工具输出/用户确认时间点，标 `PARTIALLY_VERIFIED`
      而非直接采信为 `VERIFIED`。
- [x] 18 `wifi_autoconn_*` 系列函数均在 `ui_deckpro.cpp`/`factory.ino` 找到
      真实调用方（boot entry、Disc 按钮、扫描 prepare/reconnect），非"实现
      但未接线"。
- [x] 20 A 全量接受的结论完全基于"未发现 P0/P1/P2"这一证据状态，不是因为
      申请方自评"用户已确认"就降低核查标准——五处自评点全部亲自走了一遍。

---

## 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

**登记 issue_list.md（Nit，不阻断，供顺手处理）**：① `cursor.show` 直写建议
包 helper；② `entry4_1` 建议显式重置状态行的 `s_shown_link` 缓存。

**遗留（申请已自陈，本轮不重复要求）**：OTA 回滚矩阵真测、P1 cfg-epoch 反例
真机测试、COM7 机升级 v1.12 一致性、充电 10 分钟窗口实测。
