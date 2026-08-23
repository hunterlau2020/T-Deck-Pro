# Sleep 屏休眠提示评审结果（Copilot）

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-bcca4d2.md](wifi-config-keyboard-review-request-bcca4d2.md)
- **关联 commit**：`bcca4d2`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 定时器返回值未保存，返回按钮无法取消休眠

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:3822-3839`
- **触发场景**：进入 Sleep 页面后，在三秒内点击返回。
- **证据**：
  - `create11()` 调用 `lv_timer_create()` 后没有把返回值赋给 `sleep_timer`。
  - `exit11()` 和 `destroy11()` 只删除 `sleep_timer`，但该变量始终为 `NULL`。
- **影响**：返回菜单后原定时器仍会触发并进入深度休眠；重复进入和返回还会留下多个定时器。
- **最小修复**：使用 `sleep_timer = lv_timer_create(...)`；进入前先删除旧 timer，退出时删除并清空句柄。

### 1.2 定时器没有设置为一次性

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:3781-3808,3822`
- **触发场景**：`esp_deep_sleep_start()` 因配置或平台异常返回，或在桌面/测试构建中被替代为空操作。
- **证据**：LVGL timer 默认周期执行，代码没有设置 repeat count，也没有在回调开头删除 timer。
- **影响**：回调会每三秒重复执行外设断电、GPIO hold 和深睡准备。
- **最小修复**：创建后调用 `lv_timer_set_repeat_count(sleep_timer, 1)`；回调开始时清空受管句柄。

### 1.3 三秒计时从屏幕创建开始，不保证提示实际可见三秒

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:3810-3827`
- **触发场景**：EPD 全刷耗时约一至两秒。
- **证据**：
  - timer 在 `create11()` 中启动。
  - 提示画面的 `ui_disp_full_refr()` 到 `entry11()` 才执行。
  - EPD 刷新耗时被计入三秒倒计时。
- **影响**：用户实际看到提示的时间显著少于申请书承诺的三秒；刷新较慢时可能只短暂闪现。
- **最小修复**：在 `entry11()` 完成首次显示提交后启动 timer，或从显示完成回调开始计时。

## 2. 通过项

- 深睡准备从 `create11()` 移出后，提示页面具备被渲染的机会。
- 外设断电、GPIO hold、BOOT GPIO0 唤醒配置及深睡调用顺序保持不变。
- 返回按钮的触摸事件能正确执行 `scr_mgr_pop(false)`。
- 唤醒后的 hold 释放路径未被本提交改变。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

重新申请前应保存 timer 句柄、设置一次性执行，并从提示实际显示后开始三秒倒计时。
