# 评审结论：v1.2 修复轮核销 + WiFi 全链路批次（1561b41..b92d021）

- **评审人**：GPT（Codex）
- **评审日期**：2026-09-18
- **评审申请**：[session-batch-review-request-1561b41..b92d021.md](session-batch-review-request-1561b41..b92d021.md)
- **评审范围**：`1561b41..b92d021`，申请列出的 14 个提交、19 个文件。
- **评审基线**：评审时 HEAD=`12c9e7b`；申请末端后的提交仅新增本申请文件，未混入结论。
- **依据**：[review_guide.md](../review_guide.md) v1.3，代码 A/B/C 口径。

## 结论

**C（部分接受）；当前不通过 G-合入。**

v1.2 的 Whoami cfg-epoch、OTA NTP/TLS 解耦、OTA 下载期间自动休眠重臂及异步 IPC 契约登记，均已按静态反例核销。WiFi 全链路仍有两条状态机路径会永久失去预期功能：首次从 4_2 中止扫描后扫描锁存，以及首次手动配置后的无界重连。两项均须当批修复。

## 阻断项

### P1 — 未先进入 4_1 时，4_2 扫描中途退出会永久锁死后续扫描

- **证据**：`SCAN_DONE` 回调只在 4_1 的 `create4_1()` 注册；4_2 的 `entry4_2()` 不注册，却可在扫描在飞时调用 `wifi_scan_stop_and_release()`。后者将 `s_scan_release_pending=true`，只有该回调才能清除；4_2 计时器在 pending 期间拒绝再 kick。[ui_deckpro.cpp](../../examples/pda2/ui_deckpro.cpp) 第 3111–3117、3329–3353、3615–3618、3661–3691 行。
- **反例（SIM）**：冷启动后从未进入 WiFi Config（4_1）→ 进入 WiFi Scan（4_2）→ 等首轮异步扫描开始→按 Back→再次进入 WiFi Scan。退出路径等待未注册的回调并在 3 秒后保留 pending；下一次 4_2 每秒 tick 均在 `ui_wifi_scan_release_clear()` 返回 false 处退出，不能开始新扫描，直至重启。
- **影响与定级**：正常 UI 操作序列令扫描功能永久不可用，表面只停留在旧列表/提示，缺少可恢复路径。**C/2/静默 → P1**（`review_guide.md` §4.2）。
- **Required action**：提取幂等的 `wifi_scan_event_ensure_registered()`，在 4_1、4_2 以及任何可能调用 abort/release 的扫描入口启动前调用；补真机用例“首次直接进入 4_2 → 扫描中 Back → 再进 4_2 可重新扫描”。

### P1 — 首次手动配置成功后，断链重连不受 5 次上限约束

- **证据**：工厂启动只有活动槽已有 SSID 时才调用 `wifi_autoconn_start()`；事件回调只在该函数中注册。空 NVS 的首次手动连接成功后调用的是 `wifi_autoconn_restart()`，它只设置 active/next_ms，并未注册事件。失败计数只能由 `s_ac_disconnected` 回调事件递增，缺回调时 `wifi_autoconn_poll()` 每隔 65 秒直接再次 `WiFi.begin()`。[factory.ino](../../examples/pda2/factory.ino) 第 846–866 行；[ui_deckpro.cpp](../../examples/pda2/ui_deckpro.cpp) 第 2391–2409、2441–2482、2735–2762 行。
- **反例（SIM）**：NVS 无 WiFi 槽位→用户在 4_1 手动连上 AP（保存并激活）→AP 关机或离开覆盖范围。启动期从未注册 DISCONNECTED callback；每次 65 秒 guard 到期都重试，`s_autoconn_fails` 一直为 0，不会执行“5 次后 STA idle”。
- **影响与定级**：首次配置这一常规路径违反有界重连承诺，重建了会长期占用 STA 状态的后台循环，且无上限已到达提示。**C/2/静默 → P1**。
- **Required action**：提取一次性 `wifi_autoconn_event_ensure_registered()`；在 `wifi_autoconn_start()` 与 `wifi_autoconn_restart()` 均调用，并验证新设备“手动连通→断链→第五次后 idle”以及有既存槽位的启动路径。

## 已核销/通过的范围

- **Whoami cfg-epoch**：请求与结果均携带配置代次，成功保存递增代次；旧 Profile 结果在写 RAM/NVS 前被释放。[ui_whoami.cpp](../../examples/pda2/ui_whoami.cpp) 第 126–143、269–299、343–365、367–386、485–519 行。
- **OTA NTP/TLS**：两个 HTTPS 下载点均无条件确保时间，再用固定 Mozilla CA bundle，不再读取 AI “Trust self-signed” 模式。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 312–321、404–413 行。
- **OTA 自动休眠**：`ota_busy()` 时重臂闲置计时器，避免先压入 Sleep 页面、随后才拒绝深睡。[ui_deckpro.cpp](../../examples/pda2/ui_deckpro.cpp) 第 5254–5280 行。
- **异步 IPC 文档**：Whoami 的配置代次、OTA Check/Update 的指针队列和前置单飞声明已列入契约表。[async_ipc_contract.md](../async_ipc_contract.md) 第 13–21 行。
- **EPD 黑色样式、版本号与 WiFi slot/scan-pick 的静态路径**：未发现新的内存所有权、越界或凭据覆盖问题；申请中的真机显示与硬件行为主张仍不能由静态阅读替代。

## 区间文件归属

| 文件 | 结论 |
|---|---|
| `examples/pda2/ui_deckpro.cpp` | 评：两项 WiFi P1 位于此；其余 v1.2 idle-sleep 修复已核销。 |
| `examples/pda2/ui_deckpro_port.cpp`、`.h` | 评：扫描 prepare/reconnect/collect 接口与 4_2 生命周期联审。 |
| `examples/pda2/factory.ino` | 评：启动期是否注册重连回调是 P1 反例的一端。 |
| `examples/pda2/ui_whoami.cpp`、`ota_update.cpp`、`docs/async_ipc_contract.md` | 评：v1.2 四项修复核销。 |
| `examples/pda2/fw_version.h` | 评：版本宏与显示接口一致。 |
| `ui_ai_cfg.cpp`、`ui_ai_chat.cpp`、`ui_calculator.cpp`、`ui_dictionary.cpp`、`ui_penpal.cpp`、`ui_penpal_write.cpp`、`ui_voice_ai.cpp`、`ui_weather.cpp` | 评：EPD 灰→黑机械样式变更，未发现额外功能逻辑变化。 |
| `CHANGELOG.md`、`TODO.md`、`docs/issue_list.md` | 评：台账/事实与代码交叉核对；不因纯文档而豁免。 |

## 流程与验证边界

- 申请把 14 个提交放入同一评审，未满足 `review_guide.md` §1.1 的 5–7 提交分段要求；后续申请应按修复轮、WiFi 状态机、文档/样式拆分。这是 G-评审流程偏差，不替代上述代码 P1。
- `git diff --check 1561b41 b92d021`：**VERIFIED**（通过）。
- 编译、设备烧录、P1 的两条反例、扫描驱动行为、光标/EPD 显示、充电态 10 分钟窗口：**CLAIM_ONLY / PARTIALLY_VERIFIED**。本机未发现 PlatformIO 命令且无可操作设备，未独立重跑申请的构建或真机证据。
