# 评审结果：v1.2 修复轮核销 + WiFi 全链路修复批次 v1.3–v1.12（1561b41..b92d021，ds4）

- **评审日期**：2026-09-18
- **申请文件**：[session-batch-review-request-1561b41..b92d021.md](session-batch-review-request-1561b41..b92d021.md)
- **评审提交**：`50263cf`、`eb85b09`、`f8c6b38`、`c1d1f13`、`67d2fc8`、`f757a93`、
  `cf44bf7`、`692e3a7`、`8e1e542`、`536cb30`、`794ca4b`、`cb133a1`、`74a7e72`、
  `b92d021`（14 个，与申请头部名单一致；`1561b41` 是区间下端基点、未被覆盖——
  见文末 Nit-2）
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **评审范围**：全区间 diff（19 文件，863+/68−）＋ 上轮（`095e41a..301c571`）核销表
  逐条复核（§0 原则 8：构造反例击穿"已修"声明）＋ 本批真机驱动的 WiFi 状态机
  边界序列推演（作者 §3 自评 2/3/4 处）
- **评审结论**：**C 部分接受**。v1.2 四项核销（P1 cfg-epoch / OTA NTP 解耦 /
  P2-5 idle-sleep re-arm / P2-6 契约行）**独立核实全部成立**——上轮失实项 P2-5
  确已真修；WiFi 链 v1.3–v1.12 的驱动层诊断、异步扫描对、退避管理器、三态 pick、
  灰扫完整性同样核实成立。**但本批新增的 4_2 扫描生命周期有两处状态机缺陷**
  （F1 扫描释放标志可被永久楔死 → 扫描功能直到重启不可用；F2 掉链重连被推迟
  最长 ~65s），另有两处低危（F3/F4）与三处文档对齐项（F5 + 两个 Nit）。
  **无 P0/P1**；F1/F2 均为一行级修复，**不要求推倒批次**。
- **G-合入**：**否**（F1/F2 当批修复后可过；见 Required action）

---

## 复核方法

本轮是"核销 + 新功能"混合批次，按性质分两段处理：

1. **核销段（f8c6b38，上轮三方发现的处置）**：不重开原始发现，逐条读申请引用的
   `file:line` 原文，尝试用反例击穿"✅ 已修"声明（P2-5 是上轮我判定失实的那条，
   本轮优先复核）。
2. **新代码段（v1.3–v1.12 WiFi 链）**：按 §7.2①核全区间 diff；对作者点名的三处
   "最没把握"（§3-2 标志组合状态机、§3-3 exit 重连竞争、§3-4 状态行静态变量）
   构造边界序列逐条推演；对 4_1/4_2 两套扫描生命周期与 autoconn 管理器的
   交互做五出口清点（谁 hold / 谁 release / 谁 reconnect）。
3. **独立复跑**：见"验证说明"（BUILD 已独立重跑，HW 无设备）。

---

## Findings

### F1（P2）：`s_scan_release_pending` 可被永久楔死——4_2 退出撞上"扫描已完成但未被 collect"的 ≤1s 窗口后，**扫描功能直到重启不可用**

- **位置**：`examples/pda2/ui_deckpro.cpp:3686-3690`（`exit4_2` 的 stop-and-release
  调用，本批 v1.4 新增）＋ `:3333-3358`（`wifi_scan_stop_and_release()` 协议，
  提取自 4_1 abort）＋ `:3610-3617`（1s tick 才 collect）＋
  `examples/pda2/ui_deckpro_port.cpp:513-546`（collect）
- **反例序列（SIM，可照抄推演）**：
  1. 进 4_2 → `entry4_2` 置 `wifi_scan_last_kick_ms = 0` + `lv_timer_ready` →
     首个 tick 立即 kick → `wifi_scan_async_inflight = true`；**tick 周期
     1000ms**（`:3677`），而扫描约 2–4s 完成。
  2. 扫描完成：驱动投递 SCAN_DONE → Arduino `_scanDone()` 置 DONE 位、计数
     `s_scan_done_cnt++`。此时 tick 的下一次轮询还没到——窗口 ≤1000ms。
  3. **在这个窗口内用户按 Back**（正常操作，占每周期约 5–10% 的退出时机）：
     `exit4_2` 见 `inflight == true` → `wifi_scan_stop_and_release()`：
     - `s_scan_release_target = s_scan_done_cnt`（事件已计过数）、
       `s_scan_release_pending = true`；`s_scan_done_cnt > target` 为假 →
       复位判据不成立；
     - `esp_wifi_scan_stop()`：扫描**已结束** → 不再投递新的 SCAN_DONE
       （驱动只在完成/中止时投递一次，本次已投递过）；
     - 3s 等待超时 → 打印 `[WiFi] scan abort timeout - release deferred` →
       **`s_scan_release_pending` 永久为 true**。
  4. 之后：4_2 的 tick 每拍在 `ui_wifi_scan_release_clear()`
     （`ui_deckpro_port.cpp:507`）处即 `return`（`:3620-3621`）→
     列表永不刷新、也不再 kick 新扫描；4_1 的 `wifi_cfg_scan_start()`
     （`:2611-2626`）每次 3s 等待后返回 `Scan busy - retry` → **4_1 扫描也
     起不来**。全项目扫描入口仅此两处（`grep -rn scanNetworks examples/pda2`
     = `ui_deckpro.cpp:2639/2644` + port 层），**没有第三条路径能清掉该标志**，
     即直到重启都不可恢复。
- **影响**：C/2/有声 → **P2**。后果 C（功能失效但可重启恢复）；可达性档 2
  （需"退出时机落在扫描完成后、tick 采集前"的特定时序，但该窗口每周期都存在，
  非千分之一级的巧合）。有声：串口 `scan abort timeout - release deferred`、
  屏上 `Scan busy - retry` / 列表冻结。
- **两轴**：`VERIFIED`（CODE：两处调用点 + 协议原文 + tick 周期；SIM：上述时序）
  ／`VIOLATES`（状态机违约：本批新增调用点使"等待中的事件已过去"成为可达态；
  共享的既有 round-4 协议在 4_1 侧窗口小得多——`wifi_cfg_scan_poll` 每 loop
  跑一次，且只在 4_1 为顶层时，故 4_1 是毫秒级窗口，4_2 因 1s tick 放大到
  秒级）
- **最小修复**（二选一，都只改共享 helper，一并收掉 4_1 的理论窗口）：
  ```c
  static void wifi_scan_stop_and_release(void)
  {
      /* 扫描已终结（无在跑扫描可中止）：结果已定案，直接释放，
       * 不进入"等待已过去的事件"协议 */
      if (WiFi.scanComplete() != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); return; }
      ... 现有协议原样 ...
  }
  ```
  该早退**免竞态**：`scanComplete()` 返回终结值的前提是事件回调已跑完
  （`_scanDone()` 在结束时才置 DONE 位，`WiFiScan.cpp:109-122`），因此不存在
  "迟到的 `_scanDone` 正在填结果"的并发窗口；即便驱动语义与上述推断相反
  （仍会补投 SCAN_DONE），该修复也安全（最坏只是下一次 `scanNetworks` 时
  `scanDelete()` 回收，无 UAF——应用侧不持有结果指针）。备选：把
  `s_scan_release_target` 的发布时间从"退出时"改到"kick 时"（结构性修复，
  改动更大）。
- **须补回归**：真机 4_2 反复进出（含"扫描刚出现在列表上立刻 Back"）× 10 次，
  确认清单仍会更新、4_1 的 Alt+Enter 仍能起扫（简测：串口不出现
  `release deferred`；出现即未修好）。

### F2（P2）：`s_scan_dropped_link` 在 4_2 第二个扫描周期被覆盖 → 退出屏不 `retry_now()`，掉链重连被推迟到管理器 65s guard（最长 ~65s 显示 "Not connected"）

- **位置**：`examples/pda2/ui_deckpro_port.cpp:454`（`prepare()` 用**赋值**而非
  粘滞置位）＋ `:468-479`（`reconnect()` 只在标志为真时 `retry_now()`）＋
  `examples/pda2/ui_deckpro.cpp:2453-2487`（管理器的 65s guard/backoff）
- **反例序列（SIM）**：
  1. 已连接（GOT_IP 时管理器把下一次 guard 设为 `now + 65000`，`:2462`）。
  2. 进 4_2（`entry4_2` hold(true)）；首个 tick kick → `ui_wifi_scan_prepare()`
     主动断链（v1.9 的"统一 idle 扫描"）并把 `s_scan_dropped_link = true`。
  3. 4_2 每 10s 一个周期，**第二个周期** kick 时 `prepare()` 再跑一次——此刻
     链路已被自己断开，`WiFi.status() != WL_CONNECTED` → **`s_scan_dropped_link`
     被重赋为 false**。
  4. 用户退出（停留 >1 个周期即命中）→ `exit4_2:3695` → `ui_wifi_scan_reconnect()`
     → `hold(false)`（清掉挂起期间锁存的 DISCONNECTED 假事件）＋
     `s_scan_dropped_link == false` → **跳过 `retry_now()`**，只打印
     `scan cycle done - autoconn resumed`。
  5. 管理器恢复后唯一的重连触发是 guard 到期（`:2471-2476`）→ 实际等待
     `next_ms - now`，**最坏 ~65s**（GOT_IP 恰在进屏前发生时）；期间状态行
     显示 "Not connected"。
- **影响**：C/2/有声 → **P2**。这是 v1.11/v1.12 修掉的"离开扫描屏后一直
  not connected"的**同症状轻量回归**（自愈，非永久卡死）。可达性档 2：需
  "已连接 → 进 4_2 停留 ≥10s 再退出"（浏览 AP 列表的常见行为，比 F1 更容易命中）。
- **两轴**：`VERIFIED`（CODE + SIM）／`VIOLATES`（`s_scan_dropped_link` 的语义
  "本次扫描周期丢弃了一条健康链路"被写成"本次 prepare 是否丢弃了链路"，两者
  在 4_2 的多周期循环下不等价）
- **最小修复**（一行）：
  ```c
  if (WiFi.status() == WL_CONNECTED) s_scan_dropped_link = true;   /* 粘滞：直到 reconnect() 清除 */
  ```
  （`reconnect()` 的 `if (s_scan_dropped_link) { ...; s_scan_dropped_link = false; }`
  分支已保证清除时机，无需改动；4_1 单次扫描路径不受影响。）
- **须补回归**：真机在 4_2 停留 ≥30s（跨 ≥3 个扫描周期）后 Back → 串口应出现
  `scan done - dropped link reconnecting now` 且数秒内恢复 IP。

### F3（P3）：`cursor.show = 0` **不是**绘制门（被 LVGL 强制置 1）；真正生效的是 CURSOR part 的 `bg_opa=TRANSP`——注释与申请 §1.1-3 的机制描述错误

- **位置**：`examples/pda2/ui_deckpro.cpp:2876-2879`（`wifi_cfg_set_field` 直写
  `((lv_textarea_t *)ta)->cursor.show = 0`）＋ `:3253-3254`（`create4_1` 同款）
- **反证（CODE，本仓库 vendored LVGL）**：
  - `lib/lvgl/src/widgets/lv_textarea.c:1029-1037`：`start_cursor_blink()` 在
    `anim_time == 0` 时的分支**就是** `lv_anim_del(...); ta->cursor.show = 1;`
    ——即"无闪烁动画"与"强制显示光标"是同一个分支；
  - `:391`：`lv_textarea_set_cursor_pos()` 末尾无条件调 `start_cursor_blink()`,
    而 `add_char/del_char/set_text` 都会走到它 → 每次输入都把 `show` 打回 1；
  - `:1302`：`draw_cursor()` 的 `if (ta->cursor.show == 0) return;` 因此几乎
    不会命中。
  故 v1.10/v1.11"用 style/show 压光标"仍能看到两条静止光标（`anim_time=0`
  只去掉了闪烁，没去掉绘制），而 v1.12 之所以成功，是因为**同时**写下的
  `lv_obj_set_style_bg_opa(ta, LV_OPA_TRANSP, LV_PART_CURSOR)`——
  `draw_cursor()` 用该 dsc 调 `lv_draw_rect`（`:1311`），bg_opa=0 且光标 part
  无边框时**画不出任何像素**。这也解释了本批 v1.10→v1.12 三轮才收敛的历史。
- **影响**：E/1/静默 → **P3**（显示结果正确——用户已真机确认光标消失；错的是
  机制记录与随之而来的脆弱性：任何人日后"清理"那两行 bg_opa 或改 `anim_time`
  都会让光标回来，而现注释会让人以为 `cursor.show` 是门）
- **两轴**：`VERIFIED`（CODE）／`NO_CONTRACT`
- **最小修复**：① 注释改为"生效门 = CURSOR part `bg_opa=TRANSP` + `anim_time=0`
  （无闪烁也不重绘）；`cursor.show=0` 只省一次 `draw_cursor()` 计算，且会被
  `start_cursor_blink()` 打回 1"；② 顺带回答作者 §3-1：**该私有字段可以直接删
  （两处共 4 行），行为不变**——这样也就不再依赖 LVGL 内部字段，无需封 compat
  宏。③ 若保留直写，加一句 `/* start_cursor_blink() 会把它打回 1，勿依赖 */`。

### F4（P3）：状态行块缺 `else`——`WiFi.status()` 为 CONNECTED/DISCONNECTED 之外的值时仍写标签（用未更新的旧缓冲），串口打点会打出与链路状态无关的旧文案

- **位置**：`examples/pda2/ui_deckpro.cpp:2915-2930`
- **反例（SIM）**：管理器 `WiFi.begin()` 到不存在的 AP 期间，`WiFi.status()` 会
  经过 `WL_IDLE_STATUS` / `WL_NO_SSID_AVAIL` / `WL_CONNECT_FAILED` 等非
  CONNECTED/DISCONNECTED 值。块内 `if/else if` 两个分支都不命中 →
  `s_shown_link` 已更新、`wifi_status` **未更新**，但紧接着
  `lv_label_set_text(wifi_status_lab, wifi_status)` 仍执行。已知一步：
  Save 后状态行是 `Slot 2 saved`，链路态转 `WL_NO_SSID_AVAIL` →
  串口输出 `[WiFi] link state -> Slot 2 saved`（误导性诊断行），标签被重写为
  同一文案（显示无变化，因为所有写标签的点都同步写了 `wifi_status`）。
- **影响**：E/1/有声 → **P3**（纯诊断/一致性；显示内容经核对无错误分支）
- **两轴**：`VERIFIED`（CODE）／`NO_CONTRACT`
- **最小修复**：把标签写入移进两个分支内（或加 `else continue;`），并在注释里
  去掉"in-progress banners preserved"的过度承诺——实测一次扫描会先把自己的
  链路断掉（DISCONNECTED）再重连（CONNECTED），两次状态跳变都会覆盖
  `Scan: N found` 这类状态行文案（横幅另存，不受影响）。

### F5（P3）：申请 §1.1-1 把"已连接态异步扫描 `-2`"归因于"驱动中途掐掉"——证据不唯一，且与观测值矛盾

- **位置**：申请 §1.1 第 1 条（"物理前提"）；反证在本仓库构建所用的
  Arduino 库与 IDF 头文件
- **反证（CODE）**：
  - `WiFiScan.cpp:142-152`：`WiFiScanClass::scanComplete()` 的**第一个**分支是
    `if (_scanStarted && (millis()-_scanStarted) > _scanTimeout) return
    WIFI_SCAN_FAILED;`，而 `_scanTimeout = max_ms_per_chan * 20`，默认
    `max_ms_per_chan = 300`（`WiFiScan.h:34`）→ **6000ms**。即：一次仍在正常
    进行、但耗时超过 6s 的扫描，被 Arduino 层判成 `-2`（本轮全部为
    20 通道，理论最长 20×300ms=6s，连接态信道切换更慢）。
  - 若真为"驱动中止"：中止同样会投递 SCAN_DONE → `_scanDone()` 置 DONE 位
    （`:109-122`）→ `scanComplete()` 应返回**AP 计数（≥0）**，与现场观测的
    `-2` 矛盾。
  - `esp_wifi.h:413`（ESP-IDF 文档）：`ESP_ERR_WIFI_STATE: wifi still connecting
    when invoke esp_wifi_scan_start`——**只支持 §1.1-2（connecting 拒绝）**，
    并未支持 §1.1-1 的"已连接会被中途掐掉"。
- **影响**：E/1 → **P3**（不推翻修复：v1.9 起"统一从 idle 扫描"已被真机
  验证有效，且 idle 扫描确实又快又稳；但前提的机制描述若被后续设计引用
  ——例如"必须断链才能扫"——会推高不必要的代价/复杂度）
- **两轴**：`CONTRADICTED`（§1.1-1 的归因）／`NO_CONTRACT`
- **最小修复**：把该条改为可验证的表述（"connecting 必拒（IDF 文档）；已连接态
  的异步扫描在真机上不稳定，现场观测 `collect r=-2 mode=0x1 status=3`【可能
  机制：Arduino `scanComplete()` 6s `_scanTimeout` 判负，或驱动状态拒绝】，
  故统一从 idle 扫描"），并登记 `issue_list.md` §24 的同一处措辞。

### Nit（≤3，不绑门禁）

- **Nit-1**：灰清扫计数。申请 §1.3 与 `docs/issue_list.md` §23 写"全项目 **14**
  处"，实测本区间 `git diff 1561b41..b92d021 | grep -c '^-.*LV_PALETTE_GREY'`
  = **15**（ui_deckpro 3 + whoami 3 + 其余 8 文件 9）；§23 自己的枚举求和也是 15。
  清扫本身的完整性已核实（`grep -rn LV_PALETTE_GREY examples/pda2/*.cpp` = 0 命中）。
- **Nit-2**：文件名口径。申请头写"首末含两端"，但 `1561b41` 是下端**基点**
  （`git log --oneline 1561b41..b92d021` = 14 个不含它，`git log -1 1561b41` =
  上轮结果归档 commit），即现行文件名是 git 区间记法，与
  `review_guide.md` §7.1"非 git 区间记法"不符（上一轮 `095e41a..301c571` 同款，
  建议二选一：改文件名或改 §7.1 措辞，别每轮都重提）。
- **Nit-3**：`s_shown_link`（§3-4 处）建议加 KEEP-IN-SYNC 注释：`wifi_cfg_load()`
  （`:2496-2500`）与状态行块（`:2920-2924`）各自独立地把链路态映射成文案，
  两处必须同款（当前一致，故本轮不记缺陷）。

### 入库轮义务核对（§7.2）

上轮（`095e41a..301c571`）结果里我方唯一的 Required action（P2-5：
`idle_sleep_timer_cb` 补 `ota_busy()` 门控）本轮**确已修复**（证据见上），
但它在 `docs/issue_list.md` 里**没有台账条目**——
`grep -n "P2-5\|idle_sleep\|ota_busy" docs/issue_list.md` = 0 命中，
只有 `CHANGELOG.md:234`（时点记录，非台账）。按 §7.2"结果入库轮须把上一轮全部
'应修'逐条落台账或标已修——台账是应修不阻断的唯一承接面，漏收即丢发现"，
请在下批补一条（一条 §27 即可：写 `f8c6b38` 修复 + 上轮核销失实的教训）。
本轮新增的 F1/F2 同理需要登记后才能绑 G-合入。

---

## 已核实成立（独立复核，反例未能击穿）

### P2-5（上轮我方判定失实项）——**已真修**

`ui_deckpro.cpp:5258-5275` 区间 `idle_sleep_timer_cb` 内新增
`if (ota_busy()) { s_last_activity_ms = millis(); return; }`，位置在
`audio.isRunning()` 分支之后、`scr_mgr_push(SCREEN11_ID, false)` **之前**——
正是上轮最小修复要求的一行判断 + 重新打时间戳（不放 AB 计时窗口），
与既有 `audio.isRunning()` 分支同构。上轮反例（OTA 下载覆盖层期间 5min 到期
无条件压 Sleep 屏）本轮**不可复现**。

### P1 配置代次守卫（whoami）——成立（含 busy 泄漏反例核对）

- 结果/请求结构首字段 `cfg_gen`（`ui_whoami.cpp:133-134`、`:273`、`:344`）、
  保存成功 `s_wa_cfg_gen++`（`:513`）、消费端丢弃（`:375-381`）三件套齐备，
  与 `async_ipc_contract.md` §1 新增行一致。
- **我构造的反例**：stale 丢弃路径会不会泄漏 busy？——不会：任务句柄由
  worker 自己在 `vTaskDelete` 前清空（`:297`），busy 的释放与结果消费解耦；
  应用侧只丢消息对象（`delete m`，`:379`），无双重释放。
- **残余**：`WA_REQ_TEST` 的迟到结果未纳入代次丢弃（只丢 PROFILE）。影响是
  "换配置后 Cfg 页可能闪过旧服务器的 Test 结论"（E 级、不写 NVS、不渲染资料），
  本轮**不记为缺陷**，登记为可选后续。

### OTA NTP 与 AI Trust 开关解耦——成立

两处（`ota_update.cpp:313`、`:402` 附近）都改成 `https://` 即无条件
`http_ensure_time(5000)`；`http_ensure_time` 本身（`http_utils.cpp:54-67`）
是纯 SNTP 等待，**不读** `s_tls_mode`（`http_get_tls_mode()` 只被
`http_apply_tls`/`http_get*` 使用），故解耦彻底；配合 P2-4 的
`setCACertBundle(CA_BUNDLE_MOZILLA)`，"HTTPs 下载必然校验证书 → 必然需要
时钟"的自洽性成立。

### P2-6 契约行——成立

`async_ipc_contract.md` §1 表新增 Whoami / OTA Check / OTA Update 三行，
其中 Whoami 行明确标注"**配置代次**，非页面代次"（与 P1 修复的关键区别），
OTA 两行标注指针通道 + 覆盖前回收 + `s_ota_inflight` 前置位，与代码一致。

### WiFi 链主体——核实成立的部分

- **驱动层事实核对**：§1.1-2 的 connecting 拒绝有 IDF 文档支持
  （`esp_wifi.h:413`）；§1.1-4 的 `ui_scr_mrg` 语义与我读到的源码一致
  （`ui_scr_mrg.c:77` push → 旧屏 `exit()`、`:63` pop → `exit()+destroy()`），
  且 push 不 destroy 这一点解释得通"4_1 被 Sleep 屏压栈时 `destroy4_1`
  不跑"的时序（本轮未发现该窗口的可用症状，不记缺陷）。
- **异步扫描对**（`ui_deckpro_port.cpp:489-546`）：压缩写指针
  `w <= i < r <= list_len` 无越界；`memset` 先行保证 `list[w..]` 全零，
  与 `show_wifi_scan()`"遇全零行 break"（`:3554`）配套；隐藏网络与
  16 字符 SSID 的 NUL 安全已修；`scanDelete()` 只在结果定案
  （`r >= 0`）时调用，`RUNNING` 早退不污染 `last_ret`。
- **退避管理器**（`:2395-2487`）：5 次上限、2.5s→40s（`2500UL << (fails-1)`
  带 60s 夹紧）、放弃后 STA 闲置；时间戳一律按 `(int32_t)(now - next) < 0`
  比较 → **`millis()` 回绕安全**；GOT_IP/DISCONNECTED 仅置 `volatile bool`
  标志（事件任务 → loopTask，单字节读写），与契约注记一致；`hold(false)`
  主动丢弃"自己 prepare 造成的" DISCONNECTED，避免误计失败。
- **三态 scan-pick**（`entry4_1`，`:3261-3315`）：先精确匹配槽→否则首空槽→
  全满 banner 拒绝并 `wifi_scan_gen++` 丢弃在飞扫描；`wifi_cfg_set_slot()`
  的 masked-aware 出站保存保证"不点 Save 也丢密码"的老问题不再发生；
  `existing` 分支不再清密码框（保留该槽真密码）。
- **4_1 扫描五出口成对**：kick 被拒（`:2654`）/ 终态成功（`:2670`）/
  终态失败 / 结果被丢弃（`:2670` 同点）/ 中止（`wifi_cfg_scan_abort:3360-3366`）
  / 销毁（`destroy4_1:3508`）——每条都回到 `ui_wifi_scan_reconnect()`。
  `exit4_2` 的无条件 reconnect（`:3695`）确认存在（v1.11）。
- **灰扫完整性**：`examples/pda2/*.cpp` 已无 `LV_PALETTE_GREY`（0 命中）；
  8 个机械文件逐行核对，均为纯样式等值替换，无副作用。
- **v1.6 睡眠窗口**：`ui_battery_25896_is_vbus_in()` = `PPM.isVbusIn()`，
  I2C 失败返回 false → 退化为 5min，注释属实；调用点在 5s 周期的
  `idle_sleep_timer_cb`（`:5519`），单次寄存器读，不构成实时路径阻塞。

---

## 对作者 §3 自评五处的答复

1. **`cursor.show` 直写**：见 F3——它不是门，真门是 `bg_opa`；该字段可**直接
   删掉**（更稳，且免去你们担心的"依赖 LVGL 内部"）。
2. **标志组合状态机边界**：本轮构造的两条反例即 F1（4_2 退出撞采集窗口）与
   F2（多周期 prepare 覆盖 dropped_link）。点名的另外三条我推演为**安全**：
   ①"4_1 扫描中直接 push 4_2"不可达（4_1 顶层时唯一能压栈的是 Sleep 屏；
   4_2 只能从菜单 4 push，那时 4_1 已 pop/destroy → `wifi_cfg_scan_abort()`
   已跑）；②"Disc 后立即扫描"——`wifi_autoconn_stop()` 置
   `active=false`，`reconnect()` 的 `retry_now()` 首行即 `if (!active) return`，
   Disc 生效；③"管理器放弃后 pick→Save"——`wifi_cfg_connect()` 成功会
   `wifi_autoconn_restart()` 重新激活，失败则维持放弃态，无卡态。
3. **exit 重连与用户 Connect 竞争**：无实质副作用——管理器**不触发 NTP**
   （`wifi_time_sync()` 只在 `wifi_cfg_connect()` 成功路径调），旧槽 begin 与
   用户 begin 之间以后写者胜出（驱动会中止在飞的连接），用户视角只是状态行
   可能多刷一次。
4. **`s_shown_link` 静态变量**：**无缺陷**。重进 4_1 时 `wifi_cfg_load()`
   （`:2496-2500`）已按实时链路态重写 `wifi_status` 并在
   `wifi_cfg_refresh_labels()` 落地，与状态行块的映射逐字一致；跨屏残留只会
   造成"多写一次相同文案 + 一行串口"，无显示错误。建议加 KEEP-IN-SYNC
   注释（Nit-3）。
5. **充电 10min 窗口**：注释属实（失败 → false → 5min）；长期 I2C 抖动属
   §3 待真测，与本轮结论无关。

---

## 验证说明（评审方环境）

- **设备**：无（未连接任何 COM 口）。**pio**：可用（PlatformIO Core 6.1.19，
  `python -m platformio`）。
- **BUILD 独立复跑（本轮实测，非沿用申请）**：
  ```
  git worktree add <tmp> b92d021          # 受评末端 commit，含两端 14 个提交的最终态
  cp examples/pda2/config_keys.h <tmp>/examples/pda2/   # gitignored 构建前置（空模板）
  cd <tmp> && python -m platformio run -e pda2
  → [SUCCESS] Took 51.16 s
    RAM:   55.8% (182812/327680)
    Flash: 36.4% (2382489/6553600)
  ```
  工作树已 `git worktree remove` 清理；**未改动主工作树**。
- **HW / PROBE**：无设备，一律**沿用申请证据链**并标
  `PARTIALLY_VERIFIED`（申请给了 `flash_verified.py` 5/5 块校验 + 整段回读
  MD5 + 用户确认症状修复，属可复核的具体输出，非空泛"测过了"）。
  F1 的"驱动不补投 SCAN_DONE"与 F2 的 65s 等待时长，本轮为 **SIM** 结论，
  未真机复现——但两处最小修复在两种驱动语义下都安全（见各条论证）。
- **未独立复现而标 UNKNOWN 的**：§2 表"烧录 ✅ 开机冒烟 ✅"（无设备）；
  `[FW]` 打印、Disc 按钮、双光标消失的实际观感（仅代码核对）。
- **本轮独立读取的仓库源码**：`ui_deckpro.cpp`（2372-2487 管理器 /
  2489-2719 扫描与 cfg 加载 / 2736-2886 connect+set_field /
  2888-3038 键盘 poll / 3261-3366 4_1 入口+abort /
  3436-3516 overlay+destroy4_1 / 3547-3696 4_2 全生命周期 /
  5239-5277 睡眠）、`ui_deckpro_port.cpp`（443-546 扫描对 / 571 vbus）、
  `ui_whoami.cpp`（123-140/270-300/341-414/510-513/603-640）、
  `http_utils.cpp:28-67`、`ui_scr_mrg.c`（全部 push/pop/active/remove）、
  `lib/lvgl/src/widgets/lv_textarea.c`（55/391/810-945/1029-1045/1290-1345）、
  构建所用 `WiFiScan.cpp:57-172`、`WiFiScan.h:34`、`esp_wifi.h:406-420`。

---

## 对称三格（§7.2）

1. **全区间**：19 文件逐文件归属核对完毕（与申请头部自校表一致：
   `git rev-list --count` = 14、`git diff --name-only | wc -l` = 19、
   `863 insertions / 68 deletions`，本轮独立重跑三项数字全对）。
   8 个"机械清扫"文件逐行看过（非抽样）；4 个文档文件抽查 §23–§26 与
   CHANGELOG/契约行，除 Nit-1 的计数外与实际一致。
2. **独立复跑**：BUILD 已独立重跑（见上，`b92d021` 干净工作树 + 复制
   gitignored 前置）；HW 无设备 → 如实标 `PARTIALLY_VERIFIED`/`UNKNOWN`，
   未把"无法复现"写成"证伪"（F1/F5 的推论均标了证据类型与替代解释）。
3. **申请"最没把握"处反例**：① cursor 直写 → 反例成立（F3，给出机制反证）；
   ② 标志组合 → 构造出 2 条可达反例（F1/F2），另 3 条给出安全论证；
   ③ exit 竞争 → 反例不成立（无 NTP 副作用，给出理由）；
   ④ `s_shown_link` → 反例不成立（entry 重写保证一致，给出一致性论证）；
   ⑤ I2C 抖动 → 归 §3 待真测，不升级。**5/5 全部有答复，非转述申请**。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 1 非 UI 线程调 LVGL：本批新增的 `wifi_autoconn_event`（WiFi 事件任务）
      仅置 `volatile bool`，未触碰 LVGL；`wifi_scan_done_cb` 同理（临界区内
      不打印）。未见新违例。
- [x] 2 busy/标志释放：**F1 即本条**（`s_scan_release_pending` 无 stale-drop
      兜底 → 永久卡死）；`s_ota_inflight`/`s_wa_task` 两条本轮单独复核，释放
      路径与 gen 匹配规则成立。
- [x] 3 worker `new` → UI `delete`：whoami 陈旧结果走 `delete m`（`:379`）、
      OTA 覆盖前回收（P2-3 已核实）；未见泄漏/双重释放。
- [x] 4 队列先建后 busy：whoami 队列一次创建永不删除（注释与代码一致），
      `xTaskCreate` 失败回滚句柄（`:352-353`）。
- [x] 7/8 内存纪律：本批无新增 `memset`/`= T()`；`s_scan_*` 均为标量。
- [x] 12 硬件变体：本批改动均与 4G/音频变体无关（WiFi/睡眠/UI 样式）；
      申请未声称新的"双机一致"。
- [x] 13 EPD 实时路径：cursor 闪烁在本批被彻底关停（`anim_time=0`），
      对音频泵是**净改善**；睡眠窗口延长减少 USB CDC 掉线。
- [x] 15 每条 P0/P1/P2/P3 均给了 `file:line` + 可复现序列/反证；
      无证据的推测（F5 的替代机制）已明确标注为"可能机制"。
- [x] 16 申请"编译过/真机通过"声明：BUILD 我独立重跑（VERIFIED），
      HW 沿用申请证据链标 `PARTIALLY_VERIFIED`（附其工具输出摘要）。
- [x] 17 参照系一致：BUILD 在受评末端 `b92d021` 的独立工作树跑，非在
      1 个提交之后的 HEAD 上外推；单机证据未外推双机。
- [x] 18 生产调用方：`ui_wifi_scan_async_start/collect/last_ret/release_clear`
      在 4_2 tick/exit 与 `show_wifi_scan` 均有真实调用方；
      `wifi_autoconn_*` 由 factory loop（start/poll）与扫描对（hold/retry）
      调用，无空接线函数。
- [x] 19 反例库：异步/生命周期（4_2 退出竞态、迟到事件的 pending 协议）、
      边界值（`millis` 回绕、`-1/-2` 状态码与 `last_ret` 显示分支）、
      外设（I2C 失败退化）均已过；掉电/原子写本批未触及。
- [x] 20 未与票数对抗：本轮结论全部基于自读代码；对上轮"三方一致"的
      P2-5 反而做了独立复核（结论一致但证据自取）。
- [x] 23（文档对齐模式 B）申请 §2 核销表 4 行逐条核对证据是否对应：
      P1/P2/P2-5/P2-6 四行**证据全部成立**（含 P2-5 上轮失实项的整改闭环）；
      数字类声明一处不实（Nit-1），机制类声明一处不唯一（F5）。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒批次）
- [x] **C. 部分接受**

**保留（本轮核实成立，无需改动）**：`f8c6b38` 四项核销（P1 配置代次 / OTA NTP
解耦 / P2-5 idle-sleep re-arm / P2-6 契约行）；v1.3–v1.12 WiFi 全链路主体
（idle 扫描统一、异步扫描对、有界退避管理器、4_1 五出口成对恢复、
三态 scan-pick、Disc、屏显失败码、`[FW]` 打印、v1.6 睡眠窗口、
15 处灰改黑）；`flash_verified.py` 刷机纪律。

**Required action（当批修复，均为一行级，不要求推倒批次）**：

1. **F1（P2）**：`wifi_scan_stop_and_release()` 开头加"扫描已终结则
   `scanDelete()` 并直接返回"的早退（或改在 kick 时发布 release target）。
   这是**必修**：不修则 4_2 有一次"退出时机落在扫描完成与 tick 之间"就把
   扫描功能楔死到重启（约每 10 次退出命中 1 次）。
2. **F2（P2）**：`ui_wifi_scan_prepare()` 的 `s_scan_dropped_link = (...)` 改粘滞
   置位，退出扫描屏即可立即重连而不是等 65s guard。
3. **F3（P3）**：订正 `cursor.show` 相关注释（真门 = CURSOR part `bg_opa`）；
   建议顺势删掉 4 行私有字段直写（行为不变）。
4. **F4（P3）**：状态行块补 `else`（只在 CONNECTED/DISCONNECTED 两个分支内写
   标签），并订正注释里"banners preserved"的过度承诺。
5. **文档**：F5 的物理解释改写（`issue_list.md` §24 + 下批申请引用同一处）、
   Nit-1（14→15）、Nit-3（KEEP-IN-SYNC 注释）。

修复后**不必重开本轮全部内容**（§2.5.3 代码侧仍核全区间，但复审只需覆盖
F1–F4 的 delta + 上述文档行）；F1/F2 的真机回归项建议与 §3 待真测清单合并
执行（退出扫描屏 ×10、停留 30s 后退出各一遍）。

**回退项**：无（不主张回退任何单个 commit；F1/F2 是状态机补丁而非架构问题）。
