# 评审申请书：WiFi 配置与键盘驱动复审整改（第三次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `2e559ad` — `pda2: fix keypad char delivery per review round 2`（Findings 2.1、2.2）
  - `5030566` — `pda2: fix wifi scan lifecycle and draft preservation per review round 2`（Findings 2.3、2.4）
- **历史文档**（保留不覆盖）：
  - 初版/修订版申请：[`wifi-config-keyboard-review-request.md`](wifi-config-keyboard-review-request.md)
  - 复审结果：[`wifi-config-keyboard-review-result.md`](wifi-config-keyboard-review-result.md)
- **前轮关联 commit**：`3d98321`（键盘修饰键）、`f3f2a58`（WiFi 配置）、`eafa672`（AI 聊天）——前轮 1.1/1.2/1.3/1.5 已通过，本轮未改动其实现
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

针对复审结论（2026-08-16，退回修订，Findings 2.1–2.4），完成全部四项整改并重新申请。本轮仅触碰两个文件的两个 commit，前轮已通过的实现保持不变。

## 2. 整改明细（对照 Findings）

### 2.1 全量排空丢字符（High）→ 软件字符 FIFO

**问题**：`while (keypad.available() > 0)` 一轮排空硬件 FIFO，但所有字符写入同一个全局槽位 + 单一布尔标志，应用层每轮只能取到最后一个字符。

**修复**（`peri_keypad.cpp`，commit `2e559ad`）：

```cpp
#define KBD_CHAR_FIFO_LEN 16
static char kbd_char_fifo[KBD_CHAR_FIFO_LEN];
static int kbd_char_cnt  = 0;
static int kbd_char_head = 0;   /* next pop position */
static int kbd_char_tail = 0;   /* next push position */
```

- `keypad_loop()`：排空硬件 FIFO 时逐字符**入队**；队列满时丢弃新字符并打印 `[KBD] char fifo full - drop`
- `keypad_get_val()`：每次调用**出队一个字符**，无则返回 0；`keypad_set_flag()` 退化为空操作（出队即消费）
- 交付模型不变：主循环每个 poll 每轮取一个字符，多轮间顺序完整交付；修饰键状态仍在 `keypad_loop` 内部逐事件维护，不受字符队列影响

### 2.2 溢出标志未清除（High）→ W1C 写回

**问题**：把 `INT_STAT` 当作 read-to-clear，实际需 W1C；溢出后标志常驻，每轮循环重置修饰键 → Alt/Shift/Sym 持续失效。

**修复**（`peri_keypad.cpp`，commit `2e559ad`）：

```cpp
uint8_t int_stat = keypad.readRegister(TCA8418_REG_INT_STAT);
if (int_stat & TCA8418_REG_STAT_OVR_FLOW_INT) {
    keypad_modifiers_recover();
    keypad.writeRegister(TCA8418_REG_INT_STAT,
                         TCA8418_REG_STAT_OVR_FLOW_INT);   /* W1C */
}
```

### 2.3 `scanDelete()` 不取消进行中扫描（Medium）→ `esp_wifi_scan_stop()`

**问题**：框架 `WiFiScanClass::scanDelete()` 仅释放结果内存、清 `WIFI_SCAN_DONE_BIT`，不停止扫描；退出屏幕后扫描仍在后台，其他页面 `scanNetworks()` 直接返回 `WIFI_SCAN_RUNNING`。

**修复**（`ui_deckpro.cpp`，commit `5030566`）：
- `destroy4_1()` 退出屏幕且扫描进行中时：先 `esp_wifi_scan_stop()`（真正中止扫描，框架 SCAN_DONE 事件随后清除 `WIFI_SCANNING_BIT`），再 `WiFi.scanDelete()`
- `wifi_cfg_scan_poll()` 拷贝完 SSID 列表后立即 `WiFi.scanDelete()` 释放框架结果内存（此前依赖下次扫描隐式释放）
- 补充说明：`esp_wifi_scan_stop()` 后框架事件回调仍会触发 `_scanDone()`，可能分配一次空结果集；下次 `scanNetworks()` 开头会 `scanDelete()` 回收，无泄漏累积

### 2.4 触摸切字段丢草稿（Medium）→ 草稿同步 + 刷新拆分

**问题**：焦点回调切字段时调用 `wifi_cfg_refresh()`，用旧缓存覆盖两个 textarea，未提交的 SSID/密码草稿被无提示回滚。

**修复**（`ui_deckpro.cpp`，commit `5030566`）：
- `wifi_cfg_refresh()` 拆分为：
  - `wifi_cfg_refresh_labels()`——仅更新 ">" 字段标记与状态栏
  - `wifi_cfg_sync_draft()`——把当前字段 textarea 内容同步进草稿缓存（`wifi_ssid`/`wifi_pass`）
- `wifi_cfg_set_field(f)`：切换前 `sync_draft()` 保存离场字段草稿 → 切字段 → 移动光标（`LV_EVENT_FOCUSED`）→ 仅刷新标签，**不重写任何输入框**
- 触摸焦点回调与键盘切换共用 `wifi_cfg_set_field()`；扫描选择中触摸切到密码框 = 当前候选自动落入草稿（不再丢弃）
- 从 NVS 重载文本只发生在 `create4_1()` 屏幕创建时；仅"退出页面（空框 `\b`）或显式取消扫描选择"丢弃草稿，符合评审 §5.5 意见

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 评审要求回归 1：一次积压 2–10 字符顺序交付 | ⏸ 待测 | 快速连打一串字母，输入框应按序完整显示 |
| 评审要求回归 2：溢出后修饰键可用 | ⏸ 待测 | 溢出发生后（串口 `[KBD] FIFO overflow - modifiers reset`）Alt/双 Shift/Sym 应继续可用且日志不重复刷 |
| 评审要求回归 3：扫描中退出后其他页面可扫 | ⏸ 待测 | Alt+Enter 开始扫描后立即 ⌫ 退出，进入 4.2 Wifi Scan 页应立即可扫 |
| 评审要求回归 4：触摸切字段草稿保留 | ⏸ 待测 | SSID 框打字 → 点触密码框 → 点回 SSID 框，文字应仍在 |
| 评审要求回归 5：快速输入/双 Shift 交叠/扫描中退出 | ⏸ 待测 | 串口日志 `[KBD] shift(l)/shift(r)` 辅助确认 |

## 4. 回滚方案

```bash
git revert 5030566      # 仅回退 2.3/2.4（WiFi 扫描生命周期 + 草稿保留）
git revert 2e559ad      # 仅回退 2.1/2.2（字符 FIFO + 溢出清除）
```

两个 commit 均不涉及 `boards/`、`platformio.ini`、分区表、硬件配置。

## 5. 申请审批事项

- [ ] **A. 全量接受** — 两 commit 保留，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
