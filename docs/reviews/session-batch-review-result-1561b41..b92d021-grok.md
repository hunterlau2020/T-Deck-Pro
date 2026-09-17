# 评审结果：v1.2 修复轮核销 + WiFi 全链路 v1.3–v1.12（`1561b41..b92d021`，Grok）

- **评审日期**：2026-09-18
- **评审人**：Grok（Cursor）
- **申请文件**：[session-batch-review-request-1561b41..b92d021.md](session-batch-review-request-1561b41..b92d021.md)
- **评审对象**：14 个 commit（父 `1561b41`；`50263cf`…`b92d021`；19 文件 +863/−68；git 区间 `1561b41..b92d021` 与申请自校一致）
- **工作树 HEAD**：`12c9e7b`（`git diff b92d021 HEAD` 仅本申请文件，**本批代码 findings 仍存活**）
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**。本对象是**代码**（修复轮 + WiFi 状态机），A/B/C 用 §2.3 **代码**口径：A 不得挂 P0/P1；P2 应修进 `issue_list.md`。§0 原则 8：已标「已修」的条目要构造反例击穿修复。§8：≥10 commit 本应分段——本结果按 **Part A 修复核销 / Part B WiFi 全链路** 两段走查，不因分段缺失退回申请。
- **对照**：上轮 `095e41a..301c571` Grok A（[`session-batch-review-result-095e41a..301c571-grok.md`](session-batch-review-result-095e41a..301c571-grok.md)；P2-5 核销被证伪、P2-6 未闭合、P2-7 Whoami 在途写回）。GPT 同批将 P2-7 升为 P1。
- **评审结论**：**A 全量接受**。无 P0、无未闭合 P1。上轮 **P1（cfg-epoch）/ P2 NTP / P2-5 / P2-6 修复成立**（主反例 SIM 击不穿）。WiFi 状态机引入 **2×P2**（4_2 多轮 kick 清掉 `dropped_link`；未走 `autoconn_start` 的首次 Connect 不注册事件 → 五次上限失效）。未独立 `pio run` → L2 BUILD 轴 `UNKNOWN`，不挡代码 A。
- **G-合入**：是（无 P0/P1）。两条新 P2 **不得**写成已闭合，须进 `issue_list.md`。**G-真机 / G-发布**：否（申请 HW 无串口摘要 → `CLAIM_ONLY`；OTA PENDING_VERIFY 矩阵仍 ⏸；COM7 未升 v1.12）。

---

## 0. 区间文件归属（§0 原则 5 / §7.2①）

`git rev-list --count 1561b41..b92d021` = **14**；`git diff --name-only` = **19**，与申请归属表一致。

| 文件 | 归属 | 处置 |
|---|---|---|
| `examples/pda2/ui_deckpro.cpp` | 评（重点） | autoconn / 4_1 / 4_2 / 光标 / 状态行 / idle-sleep |
| `examples/pda2/ui_deckpro_port.cpp` | 评（重点） | prepare / reconnect / async_start / collect |
| `examples/pda2/ui_deckpro_port.h` | 评 | 声明；注释滞后见 Nit |
| `examples/pda2/factory.ino` | 评 | `[FW]` 打印；`autoconn_start/poll` 接线 |
| `examples/pda2/fw_version.h` | 评 | minor 1.12 |
| `examples/pda2/ota_update.cpp` | 评 | NTP 解耦 |
| `examples/pda2/ui_whoami.cpp` | 评 | cfg-epoch + 灰改黑 |
| 8× UI 灰改黑 | 评（低风险） | 每文件 1–4 行 `lv_color_black()`；开关 CHECKED 选择器未误改 |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` / `docs/async_ipc_contract.md` | 不评（纯文档） | 台账 §23–§26；P2-6 闭合的文档面已核契约表三行 |

HEAD 之后落入、**不评**：本申请文件本身（`12c9e7b`）。

**流程（不占缺陷）**：14 commit 未按 §8 拆成 5–7 一段；申请缺 §7.1 验证状态表 / 回滚方案。自校数字正确，按组给 findings，不退回。

---

## 1. 上轮核销（击穿实验，不是复读申请表）

| 上轮 | 申请处置 | 本轮裁定 | 证据 |
|---|---|---|---|
| **P1 / P2-7** Whoami 在途 PROFILE 写回 NVS | ✅ `s_wa_cfg_gen` | **主反例闭合** | `ui_whoami.cpp:132-134` 结果首字段 `cfg_gen`；`:344` 启动快照；`:273` worker 抄回；`:375-380` 不匹配则 `delete`、**不** `wa_cache_save`；`:513` 双成功 Save 才 `++`。反例「Refresh A → Save B（双成功）→ A 到达」击不穿。残余见 **P2-2**。HW 反例仍 ⏸。 |
| **P2** OTA NTP 跟 Trust 开关 | ✅ HTTPS 无条件 `http_ensure_time` | **闭合** | `ota_update.cpp:316`、`:408`：两处已去掉 `http_get_tls_mode()!=INSECURE` 门。Trust ON + 冷启动仍会对时。 |
| **P2-5** idle 无条件 push Sleep | ✅ `ota_busy()` re-arm | **闭合**（上轮核销 CONTRADICTED 已真修） | `ui_deckpro.cpp:5269-5277`：`ota_busy()` 时打时间戳 return，**无** `scr_mgr_push`。原反例（下载满 5 分钟压栈 Sleep）击不穿。`sleep_do_enter` `:5093` 仍是第二道电源轨门。HW 下载满窗 ⏸。 |
| **P2-6** OTA/Whoami 未入契约 | ✅ 表补三行 | **闭合（文档面）** | `docs/async_ipc_contract.md:19-21` Whoami 配置代次 + OTA 指针通道 / inflight 前置位。 |

---

## Findings

### P2-1：4_2 第二轮 kick 把 `s_scan_dropped_link` 覆写成 false——exit「立即重连」只对首轮扫描成立

- **位置**：`ui_deckpro_port.cpp:454`（赋值而非粘性置位）× `:489-491`（每次 `async_start` 都 `prepare`）× `:473-478`（仅 `dropped_link` 才 `retry_now`）× `ui_deckpro.cpp:3615-3618`（10s 再 kick）× `:3691`（exit 无条件 `reconnect`，但不保证 `retry_now`）。
- **证据 / 反例（SIM）**：
  1. 已连接、`s_autoconn_active=true`、上次 GOT_IP 把 `next_ms` 设为 now+65s。进 4_2：`hold(true)`。
  2. 首轮 `async_start`→`prepare`：`dropped_link=true`，`disconnect`。
  3. ≥10s 后第二轮 kick 再 `prepare`：STA 已 idle，`dropped_link = (status==WL_CONNECTED)` **写成 false**（上一轮的 true 被清掉）。
  4. 点 SSID 或 Back → `exit4_2` → `reconnect`：`hold(false)` **顺手清掉** `s_ac_disconnected`（`:2417`），`dropped_link==false` 故 **不** `retry_now`。
  5. `autoconn_poll` 要等到旧的 65s guard 到期才 `WiFi.begin(active slot)`。停留刚过 10s 时剩余可到 ~55s。不是永久卡死（guard 到期会连；停留 >65s 反而立刻 begin）。
- **与 v1.11 声明的关系**：CHANGELOG / issue_list §26 把 v1.11 写成「exit 无条件 reconnect 修复了 dropped_link 悬挂、链路永不重连」。exit **确实**每次调用 reconnect；但 reconnect 的「立即重连」仍绑在会被 4_2 循环清掉的标志上。v1.12 状态行会在最终 GOT_IP 时刷新，所以真机长留扫描再进 Config 可能只表现为「Not connected 一会儿」——申请靶向 A（点选后立刻有 IP）覆盖的是**首轮 10s 内 tap**（`dropped_link` 仍真），击不穿这条。
- **影响**：`C/2/有声 → P2`（须在 4_2 等到第二次 kick）。不升 P1：会重连，不断电、不 busy 死。回升：若产品把「扫完立刻回到原 AP」当承重且扫描页常驻 >10s。
- **两轴**：`VERIFIED`（CODE/SIM）/ `CONFORMS` 契约（非 HTTP worker）；**违反**申请 §1.2「dropped_link 时 `retry_now` 立即重连」在 4_2 循环下的全称。
- **最小修复**：`prepare` 只允许 `true` 粘性置位，仅 `reconnect` 清零：
  ```c
  if (WiFi.status() == WL_CONNECTED) s_scan_dropped_link = true;
  ```
  不要 `= (status == WL_CONNECTED)`。SIM：两轮 kick 后 exit 仍打 `[WiFi] scan done - dropped link reconnecting now`。HW：已连接 → 4_2 等 >10s → Back，串口应立即 `WiFi.begin`，不应干等 guard。

### P2-2（上轮 P2-7 残余）：Cfg 部分保存不 `++cfg_gen`、不清缓存；首次 Connect 未注册 autoconn 事件

分两支，同属「修复声明的等价绕过」，各不够单独升 P1。

**2a. `server_ok && !prov_ok`**（`ui_whoami.cpp:510-521`）

- Save 双成功才 `s_wa_cfg_gen++` + `wa_cache_clear`。`penpal_save_config` 已把新 key 写入 NVS 后，provider 保存失败走 `:520-521`：缓存仍是旧用户，在途 PROFILE 的 `cfg_gen` 仍匹配 → 仍可 `wa_cache_save`。
- 上轮主反例（双成功 Save）已闭合；本支需 NVS provider 写失败，可达性 2。`D/2/静默 → P2`。最小修复：`server_ok` 即 `++gen` 并清缓存（上轮 Grok P2-7 最小修复③），不要等 `prov_ok`。

**2b. 开机无 SSID 时 `wifi_autoconn_start` 整段跳过，`WiFi.onEvent` 从未注册**（`factory.ino:858-866` × `ui_deckpro.cpp:2397-2404` × `:2441-2446`）

- `start()` 是唯一 `onEvent(wifi_autoconn_event)` 点；空槽开机不调用。用户随后 Connect 成功只走 `wifi_autoconn_restart()`（置 `active`、65s guard），**不**注册事件。
- 之后 AP 消失：无 `s_ac_disconnected`，`fails` 永不 ++，**五次放弃永不触发**。poll 每 65s 见 `!WL_CONNECTED` 就 `begin()`，弱化为无界重连（周期 65s 而非 2.4s）。
- 开机已有槽位（`start()` 已跑）再 Disc→Connect：**事件仍在**，本支不可达。
- `B/2/有声 → P2`（新机 / 清槽后的首次连接；扫描可能周期性 -2）。最小修复：事件回调一次性注册（`start`/`restart` 共用，防重复订阅导致 DISCONNECTED 双计）。SIM：空 NVS → Connect OK → 拔 AP → 65s×6 仍 `active==true`。

---

## 2. 申请 §3 自评五问（对称三格③）

| # | 自评 | 本轮 |
|---|---|---|
| 1 | `cursor.show` 直写是否稳 | **可接受，但不是真正的绘制门**。本树 LVGL 8.3 **没有** public hide API。`start_cursor_blink`（`lib/lvgl/src/widgets/lv_textarea.c:1033-1035`）在 `anim_time==0` 时 **强制 `show=1`**；FOCUSED（触摸仍会发，`:2841` 同框 tap **不**走 `set_field`）会把它打回去。真正挡住像素的是 `LV_PART_CURSOR` + `LV_OPA_TRANSP`（`:2872-2873`）。直写是加固，升级脆弱。更稳：①继续只靠 TRANSP + `border_width=0`；或 ②`wifi_ssid_focus_cb`/`pass` **每次** FOCUSED 末尾再 `show=0`（含同框 tap）；或 ③项目内 `wifi_ta_hide_cursor()` 包一层。不占缺陷（v1.12 真机已验全灭）。 |
| 2 | 标志组合卡死 | **无永久卡 `held`**：4_2 push/pop 都跑 `exit4_2`→`reconnect`→`hold(false)`；4_1 扫描销毁路径 `destroy4_1`→`scan_abort`→`reconnect`。Disc 后扫描：`active==false`，exit 不 `retry_now`——符合「放弃/手断直到显式 Connect」。**非永久、但延迟**：见 P2-1。放弃后 pick→Save：**Save 本来就不连**，须 Connect；Connect 成功会 `restart()`。 |
| 3 | exit4_2 立即重连 vs 用户 Connect | **会短连旧槽**，功能上后一次 `begin` 获胜。10s 内 tap（`retry_now`）时 poll 可能在用户打密码期间 `begin(active)`；NTP 突发只发生在 `wifi_cfg_connect` 成功路径（`:2762`），旧槽 GOT_IP 不会走 `wifi_time_sync`。状态行可能闪。`E/2 → P3`，不升 P2。P2-1 下延迟重连反而减轻这条竞争。 |
| 4 | `s_shown_link` 跨屏 | **进屏不跳过首帧**：`create4_1` `wifi_cfg_load` + `refresh_labels` 必写一次。静态变量只抑制 poll 里的重复刷新。**会打脏进行中横幅**：自身 `prepare` 把 STA 打成 `WL_DISCONNECTED` 后，poll 把 `wifi_status` 写成 `"Not connected"`，覆盖 `"Scanning..."`（`:2916-2920`）。与 CHANGELOG「不覆盖 Scan 横幅」字面不符。overlay 仍在，`E/1 → P3`。 |
| 5 | 充电 10min I2C | `isVbusIn()` 失败退化为 5min（申请已写）。误报 VBUS 只会拉长窗口。观察项，不占缺陷。HW 插 USB 静置 ⏸。 |

---

## 已通过项

- **v1.5 管理器主路径**（开机有槽）：`setAutoReconnect(false)`；GOT_IP 清 fails + 65s 再臂；已连接 guard 到期只再臂不 `begin`（`:2474-2476`）；放弃后 `active=false`；`hold` 期间不计失败；Connect 成功 `restart`。`factory.ino:906-907` 每 tick poll。替换无限 `setAutoReconnect(true)` 的目标在**已注册事件**的设备上成立。
- **v1.9 idle 扫描**：`prepare` 对已连接也 `disconnect`；4_1 单次 prepare/reconnect 对上 `dropped_link`（P2-1 只打 4_2 循环）。
- **v1.4 pick 三态**：匹配槽保留密码 / 首空槽 / 全满 banner（`:3271-3283`）。切槽 outgoing save 不再被空密码冲掉。
- **v1.8 Disc**：`stop` + `disconnect`，NVS 保留。
- **v1.6 idle 窗口**：VBUS ? 10min : 5min，OTA/音频仍 re-arm。
- **灰改黑**：GREY 0x9E ≥128 → 白的 EPD 阈值叙述与 `fw_lab` 配色一致；开关仍绑 `LV_PART_INDICATOR \| LV_STATE_CHECKED`。
- **CJK 压缩**：`collect` 写指针独立于读指针（`:535-544`），破「空行即停」截断。返回值仍是原始 `r`，渲染靠空 name 停——自洽。
- **单飞 Whoami**：`s_wa_task` 拒绝第二请求；契约表已写配置代次语义。

---

## Nit / 疑问（≤3，不占缺陷）

1. **`ui_deckpro_port.h:114-122` 注释过期**：仍写「已连接原地扫」「collect 结束恢复槽位」。v1.7/v1.9 已改成 idle 扫、collect **不** reconnect。读头文件会按旧协议实现。
2. **`ota_update.h:8-11` 仍写 inflight「create 成功后再递增」**——上轮已与代码相反，本批未改头。沿用。
3. **4_2 `name[16]`**：SSID 最长 32，列表截 15 字符；4_1 扫描缓冲是 `[33]`。长 SSID pick 可能对不上槽、连不上。根因是既有结构体，本批只做了 NUL-safe。

疑问（不升）：`wifi_autoconn_start` 每次 `onEvent` 无去重——当前只从 setup 调一次，安全；若将来热调用 `start()`，GOT_IP/DISCONNECT 会双计。与 P2-2b 的一次性注册是同一补丁。

---

## 验证说明

- **环境**：开发机静态走查；**无** PlatformIO 于 PATH → BUILD `UNKNOWN`；**无**本机设备 → HW 不能独立复验。
- **申请 HW**：机 `28:37:2f:91:2c:20` 已烧 v1.12 + `flash_verified.py` 5/5 + 用户确认症状修复。无串口摘要、无操作逐步记录 → 指南 §3.5 记 **`CLAIM_ONLY`**（属主签字可用，评审方不标 VERIFIED）。COM7 留旧版，双机一致不适用。
- **SIM**：核销四条 + P2-1/P2-2 逐步推演；未写可执行脚本。

---

## 对称三格（强制）

① **全区间 diff**：`git diff --name-only 1561b41..b92d021` 19 文件均归属；不只申请靶向清单。灰改黑 8 文件抽查为机械配色。  
② **快照**：未独立 `pio run`（无 pio）。未 checkout 到 `b92d021` 干净树编译。HEAD 仅多申请文档。  
③ **最没把握**：五问均构造反例；#2/#3/#4 见上；#1 给出 LVGL `anim_time==0` 强制 `show=1` 的代码事实。

---

## 自审清单（§9 代码档）

- [x] 1–6 异步：本批新 HTTP 路径仅 Whoami 代次（已核）；autoconn 在 WiFi 事件任务只写 volatile 标志，LVGL 仍在 UI。
- [x] 7–8 memset / 肥聚合：`collect` memset 的 `ui_wifi_scan_info_t` 为 POD。
- [x] 10–11 秘密：本批未改 key 缓冲；无新 tracked 密钥。
- [x] 12 双机：申请单机 CLAIM_ONLY，未外推 COM7。
- [x] 15 每条 P2 有 file:line + 序列。
- [x] 16 真机通过 = CLAIM_ONLY。
- [x] 18 `wifi_autoconn_*` / `ui_wifi_scan_*` 均有生产调用方（factory + 4_1/4_2）。
- [x] 19 反例库：离页/hold/Disc/放弃/扫描中 push 已推演。

---

## 审批意见

- [x] **A 全量接受**
- [ ] B 退回修订
- [ ] C 部分接受

**合入条件（不挡 A，挡把 P2 写成已修）**：`issue_list.md` 增收 P2-1（4_2 `dropped_link` 粘性）与 P2-2（部分 Save / 首次 Connect 事件注册）。建议下一 WiFi 小提交一并修，不必开新设计版。
