# 键盘页面边界、WiFi 扫描生命周期与可用性整改评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-6a9ab00-6c51964.md](wifi-config-keyboard-review-request-6a9ab00-6c51964.md)
- **关联 commit**：`6a9ab00`、`6c51964`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 页面切换只清理软件队列，硬件 FIFO 中的旧按键仍会注入新页面

- **严重性**：Medium
- **位置**：`examples/pda2/peri_keypad.cpp:136-146`、`examples/pda2/ui_scr_mrg.c:140,193,233`
- **触发场景**：
  1. 用户按键事件仍停留在 TCA8418 硬件 FIFO。
  2. 触摸操作在 `lv_task_handler()` 中先触发页面切换。
  3. `keypad_clear_chars()` 只清空软件字符 FIFO。
  4. 同一主循环稍后执行 `keypad_loop()`，把旧硬件事件重新写入软件 FIFO，并由新页面消费。
- **证据**：主循环顺序为 `lv_task_handler()` 后执行 `keypad_loop()`；清理函数没有调用 `keypad.flush()` 或其他硬件事件丢弃逻辑。
- **影响**：页面边界隔离并不完整，旧页面的 Enter、Backspace 或字符仍可能作用于新页面。
- **最小修复**：提供同时清空硬件事件和软件字符队列的接口，并明确重置 Alt/Shift 的瞬时状态。Sym 锁是否跨页面保留应作为独立产品语义处理。

### 1.2 固定等待 1 秒不能证明 Arduino SCAN_DONE 回调已经结束

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1904-1913`
- **触发场景**：退出页面时 WiFi 事件任务繁忙、回调延迟超过一秒，或 `scanComplete()` 因框架自身超时清除扫描状态。
- **证据**：
  - `esp_wifi_scan_stop()` 的 SCAN_DONE 由事件任务异步处理。
  - `WiFi.scanComplete() != WIFI_SCAN_RUNNING` 只表示扫描状态位不再运行，不保证 `_scanDone()` 已完成结果分配和填充。
  - 一秒超时后代码无条件调用 `WiFi.scanDelete()`。
- **影响**：`scanDelete()` 仍可能与 `_scanDone()` 并发，产生结果内存泄漏、并发释放或写入已释放内存的风险；前轮 High Finding 未被可靠关闭。
- **最小修复**：使用明确的 SCAN_DONE 事件同步信号，将停止、完成回调和 `scanDelete()` 串行化；超时时不能假定回调已结束并立即释放框架结果。

## 2. 通过项

- 软件字符 FIFO 在键盘触发的 `push/pop` 路径上能清除已入队字符。
- TCA8418 `OVR_FLOW_INT` 的 W1C 清除保持正确。
- 扫描代次校验可阻止已离开 SSID 字段的结果覆盖草稿。
- 空框 Enter 扫描、Alt+Enter 扫描及提示文案一致。
- Save/Clear 按钮的基本草稿与 NVS 操作逻辑一致。
- 音量键和麦克风键的 Sym 层映射与 README 一致。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

重新申请前应完成硬件 FIFO 页面边界清理，并用事件同步替代扫描中止的固定等待。
