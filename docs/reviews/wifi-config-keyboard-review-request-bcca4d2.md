# 评审申请书：Sleep 屏休眠提示（第十七次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `bcca4d2` — `pda2: show a sleep prompt before entering deep sleep`
- **历史文档**（保留不覆盖）：前十六轮及合并申请见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户反馈：点击 Sleep 菜单后**无任何提示直接休眠**（屏幕无反应，实际已进入深度休眠）。排查根因：`create11()` 把外设断电 + `esp_deep_sleep_start()` 全部放在**屏幕创建阶段**——屏幕尚未渲染就进入休眠，UI 永远没有机会显示（创建函数末尾的返回按钮也是死代码）。

## 2. 变更明细（`examples/pda2/ui_deckpro.cpp`，commit `bcca4d2`）

- 休眠准备逻辑（`lora_sleep` / `SerialGPS.end` / gpio reset / 外设 EN 拉低 / gpio_hold / ext1 唤醒源配置 / `esp_deep_sleep_start()`）整体移入 `sleep_timer_event` 定时器回调
- `create11()` 只做 UI：居中提示文案 `"Entering sleep...\n\nWake: press BOOT key."` + 返回按钮 + 创建 **3 秒一次性定时器**
- 3 秒窗口内点返回 = **取消休眠**（`exit11` 删除定时器；`destroy11` 同样兜底删除）——顺带修复了原代码返回按钮不可达的问题
- 唤醒方式不变：BOOT 键（`ext1` 低电平唤醒 GPIO0），唤醒后 `setup()` 的 `gpio_hold_dis`/`gpio_deep_sleep_hold_dis` 释放保持引脚

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 休眠提示显示 3s 后休眠 | ⏸ 待测 | 点 Sleep → 显示提示屏 → 3s 后休眠 |
| 窗口内点返回取消休眠 | ⏸ 待测 | 3s 内点返回 → 回菜单不休眠 |
| BOOT 键唤醒 | ⏸ 待测 | 休眠后按 BOOT 键开机 |

## 4. 回滚方案

```bash
git revert bcca4d2
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
