# WiFi 配置与键盘驱动第三次评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-2e559ad-5030566.md](wifi-config-keyboard-review-request-2e559ad-5030566.md)
- **关联分支**：`HD-V2-250915`
- **关联 commit**：`2e559ad`、`5030566`
- **评审结论**：**退回修订**

## 1. 前轮 Findings 整改结论

| 前轮 Finding | 本轮结论 | 说明 |
|---|---|---|
| 2.1 全量排空丢字符 | **主体已修复** | 软件字符 FIFO 可按顺序保存并逐个交付积压字符，但缺少页面边界清理，见 Finding 2.1 |
| 2.2 溢出标志未清除 | **已修复** | 检测后按 W1C 语义写回 `OVR_FLOW_INT` |
| 2.3 `scanDelete()` 不取消扫描 | **未完整修复** | 已调用 `esp_wifi_scan_stop()`，但立即 `scanDelete()` 与异步 SCAN_DONE 回调存在竞态，见 Finding 2.2 |
| 2.4 触摸切字段丢草稿 | **主体已修复** | 字段切换会同步离场草稿且不再整体刷新文本框，但扫描完成路径仍可覆盖草稿，见 Finding 2.3 |

## 2. Findings

### 2.1 软件字符 FIFO 缺少页面边界，残留按键会注入后续页面

- **严重性**：Medium
- **位置**：`examples/pda2/peri_keypad.cpp:58-128`
- **触发场景**：
  1. 在输入页面快速按下 Backspace 和后续若干字符，使多个字符在同一轮进入软件 FIFO。
  2. Backspace 先被消费并退出当前页面，队列中仍有未消费字符。
  3. 返回页或随后打开的其他键盘页面继续调用 `keypad_get_val()`。
- **证据**：软件 FIFO 只提供入队和逐个出队，没有清空接口，也未在 `scr_mgr_pop()`、屏幕 `destroy()` 或键盘页面激活时建立新的输入代次。
- **影响**：旧页面产生的字符可能自动写入新页面；残留 Enter 或 Backspace 还可能触发提交、返回等非预期操作。
- **最小修复**：增加 `keypad_clear_chars()`，在屏幕切换完成时统一清空；更稳妥的方案是为字符记录页面/输入会话代次，只向产生该事件时的活动页面交付。

### 2.2 `esp_wifi_scan_stop()` 后立即 `scanDelete()` 与异步完成回调存在竞态

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1833-1844`
- **触发场景**：Alt+Enter 启动异步扫描后立即退出 WiFi Config。
- **证据**：
  - `esp_wifi_scan_stop()` 通过 WiFi 事件任务异步产生 SCAN_DONE。
  - Arduino ESP32 框架收到事件后会调用 `WiFiScanClass::_scanDone()`，更新扫描状态并可能分配、填充 `_scanResult`。
  - 当前主循环在 `esp_wifi_scan_stop()` 返回后立即调用 `WiFi.scanDelete()`；框架静态扫描结果没有互斥保护。
- **影响**：
  - `scanDelete()` 先执行时，随后 `_scanDone()` 仍可能重新分配结果并保留到下一次扫描。
  - 两者并发时可能发生结果指针释放与填充竞态。
  - SCAN_DONE 尚未处理前，其他页面仍可能看到 `WIFI_SCAN_RUNNING`，因此“退出后立即可扫”没有得到保证。
- **最小修复**：停止后不要立即释放结果。将扫描置为“取消等待完成”状态，等待 `scanComplete()` 不再返回 `WIFI_SCAN_RUNNING` 或收到统一扫描完成事件后，再由同一执行路径调用 `scanDelete()`。所有页面应复用同一个扫描生命周期管理器。

### 2.3 扫描进行中切换到密码框，完成结果仍会覆盖 SSID 草稿

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:1559-1605,1666-1679`
- **触发场景**：
  1. 在 SSID 字段输入草稿并按 Alt+Enter 开始扫描。
  2. 扫描完成前触摸密码框。
  3. 异步扫描随后完成。
- **证据**：
  - `wifi_cfg_set_field(1)` 只关闭当时的 `wifi_cfg_scan_mode`，没有取消进行中的扫描或标记结果已放弃。
  - `wifi_cfg_scan_poll()` 完成后不检查当前字段和本轮取消状态，无条件设置 `wifi_cfg_scan_mode = true` 并用候选 SSID 覆盖 `wifi_ssid_ta`。
- **影响**：用户已离开 SSID 字段后，后台扫描仍会改写其草稿；界面字段标记、扫描选择状态和最终保存值可能不一致。
- **最小修复**：离开 SSID 字段时取消本轮扫描或设置“忽略结果”标志；`wifi_cfg_scan_poll()` 仅在仍处于 SSID 字段且扫描请求代次有效时更新 textarea 和进入选择模式。

### 2.4 SSID 占位提示仍错误地写为 Enter 扫描

- **严重性**：Low
- **位置**：`examples/pda2/ui_deckpro.cpp:1791`
- **触发场景**：NVS 中没有 SSID，进入 WiFi Config 查看空文本框。
- **证据**：placeholder 为 `"type SSID or Enter=scan"`，实际扫描快捷键已经改为 Alt+Enter；普通 Enter 在空框只显示提示，不会扫描。
- **影响**：用户会按错误快捷键，并再次认为扫描不可用。
- **最小修复**：改为 `"type SSID or Alt+Enter=scan"`，并与页面底部提示保持一致。

## 3. 分项结论

| 功能 | 结论 | 说明 |
|---|---|---|
| 软件 FIFO 顺序交付 | **通过** | 同一输入会话内可按顺序交付，不再覆盖为最后一个字符 |
| 软件 FIFO 页面隔离 | **不通过** | 队列内容可跨页面残留 |
| TCA8418 W1C 清除 | **通过** | 前轮 High Finding 已正确修复 |
| WiFi 扫描结果释放 | **通过** | 正常完成并复制 SSID 后会释放框架结果 |
| WiFi 扫描中止 | **不通过** | 停止与异步回调的清理路径存在竞态 |
| 字段切换草稿保留 | **部分通过** | 普通切换已保留，扫描完成仍可能后台覆盖 |
| 快捷键提示一致性 | **不通过** | placeholder 仍提示 Enter 扫描 |

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

重新申请前至少应完成：

1. 为软件字符 FIFO 增加页面/输入会话边界，并验证退出页面后无残留字符被消费。
2. 将扫描停止、SCAN_DONE 和结果释放串行化，避免 `scanDelete()` 与 `_scanDone()` 并发。
3. 扫描请求增加取消或代次校验，离开 SSID 字段后不得覆盖草稿。
4. 修正 SSID placeholder。
5. 完成申请书 §3 的真机回归，并补充“扫描中切到密码框”和“带积压字符退出页面”两项测试。
