# 评审申请书：键盘页面边界、WiFi 扫描生命周期与可用性整改（第四次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `6a9ab00` — `pda2: fix keypad page boundary and volume/mic key map per review round 3`（Finding 2.1 + 用户键位问题）
  - `6c51964` — `pda2: fix wifi scan lifecycle, add Save/Clear buttons per review round 3`（Findings 2.2/2.3/2.4 + 用户可用性问题）
- **历史文档**（保留不覆盖）：
  - 初版/修订版申请：[`wifi-config-keyboard-review-request.md`](wifi-config-keyboard-review-request.md)
  - 第二轮申请：[`wifi-config-keyboard-review-request-2e559ad-5030566.md`](wifi-config-keyboard-review-request-2e559ad-5030566.md)
  - 第二轮结果（原位更新）：[`wifi-config-keyboard-review-result.md`](wifi-config-keyboard-review-result.md)
  - 第三轮结果：[`wifi-config-keyboard-review-result-2e559ad-5030566.md`](wifi-config-keyboard-review-result-2e559ad-5030566.md)
- **前轮关联 commit**：`3d98321`、`f3f2a58`、`eafa672`、`2e559ad`、`5030566`（均已通过对应轮次评审项，本轮未改动其实现）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

针对第三轮评审结论（退回修订，Findings 2.1–2.4）与用户在真机测试中报告的 5 个问题，完成全部整改并重新申请。

**用户报告问题与整改对照**：

| # | 用户报告 | 整改 |
|---|---|---|
| a | WiFi config 没有保存按钮，填完如何生效 | 新增 **Save** 触摸按钮（= 密码框 Enter：同步草稿 → 存 NVS → 连接）；提示文案同步说明 |
| b | 音量键 Sym 层应为音量，实际输入框出现 `0` | Sym 层音量键 (2,8) 改发专用码 `'\v'`（0x0B），不再污染输入框；文本输入屏统一忽略 `'\v'` |
| c | 麦克风键 Sym 层应为 `0`，实际无反应 | Sym 层麦克风键 (3,6) 映射为 `'0'` |
| 4 | SSID 框为空时按 Enter 不再扫描 | 空框 Enter 恢复触发扫描（Alt+Enter 仍随时可扫，保留独立入口，不违反 Finding 1.2） |
| 5 | 建议增加清除按钮 | 新增 **Clear** 触摸按钮：清空两个输入框、草稿缓存与 NVS 记录（下次开机不再自动连接旧凭据） |
| 3 | 页面切换后积压删除键仍生效，导致无法重新进入 WiFi config | `keypad_clear_chars()` + `scr_mgr` 切换/push/pop 时清空字符缓冲（即 Finding 2.1 整改） |

## 2. 整改明细（对照第三轮 Findings）

### 2.1 软件 FIFO 页面边界（Medium）→ `keypad_clear_chars()`

- `peri_keypad.cpp` 新增 `extern "C" void keypad_clear_chars(void)`：清空字符队列并打印 `[KBD] char fifo cleared (screen switch)`
- `ui_scr_mrg.c` 在 `scr_mgr_switch()` / `scr_mgr_push()` / `scr_mgr_pop()` 三处调用——页面切换即丢弃旧页面产生的积压按键，残留 `\b`/`\n` 不再注入新页面或重复弹栈
- 注：因 `ui_scr_mrg.c` 为 C 编译单元，`keypad_clear_chars` 采用 C 链接（`peripheral.h` 声明 + `extern "C"` 定义），链接已验证

### 2.2 扫描中止与 SCAN_DONE 竞态（High）→ 中止后同步等待再释放

- `wifi_cfg_scan_abort()`：`esp_wifi_scan_stop()` 后**不再立即** `scanDelete()`；有界等待（≤1s）`WiFi.scanComplete() != WIFI_SCAN_RUNNING`——该条件成立即框架已处理完异步 SCAN_DONE（`_scanDone()` 在置位 SCAN_DONE_BIT 前完成结果分配与填充），随后在同一执行路径 `scanDelete()` 释放，消除与 `_scanDone()` 的竞态
- 有界等待仅发生在"扫描中退出屏幕"路径，且屏幕退出本身触发整屏刷新，1s 上限不影响交互
- `destroy4_1()` 先 `wifi_scan_gen++` 失效在途结果，再调用 `wifi_cfg_scan_abort()`

### 2.3 扫描完成覆盖已离开字段的草稿（Medium）→ 扫描代次校验

- 新增 `wifi_scan_gen` / `wifi_scan_pending_gen` 代次计数：
  - `wifi_cfg_scan_start()` 每次扫描 `gen++` 并记录在途代次
  - `wifi_cfg_set_field(1)`（离开 SSID 字段）时若扫描进行中则 `gen++`（忽略其结果）
  - `destroy4_1()` / Clear 按钮同样 `gen++`
- `wifi_cfg_scan_poll()` 完成时校验 `wifi_scan_pending_gen == wifi_scan_gen && wifi_cfg_field == 0`，不满足则 `scanDelete()` 丢弃结果、置 `WIFI_SCAN_FAILED`，**不触碰** textarea 与选择模式

### 2.4 快捷键提示不一致（Low）→ 恢复空框 Enter 扫描并统一提示

- 空框按 Enter 恢复触发扫描（用户要求），与 Alt+Enter 并存：Alt+Enter 任何时刻可扫，Enter 仅空框时扫、非空时提交切密码框
- placeholder `"type SSID or Enter=scan"` 在空框状态下与实际行为一致；屏底提示更新为 `"Enter:scan/next  +/-:pick / Alt+Enter:scan  Backspace:del/back"`

## 3. 键位与输入屏配套变更

- `keymap_sym[2][8]`：`'0'` → `'\v'`（音量键专用码，当前无处理器，文本输入屏显式忽略）
- `keymap_sym[3][6]`：`0` → `'0'`（麦克风键 Sym 层出数字 0）
- `ui_ai_chat.cpp` / `ui_ai_cfg.cpp` / `ui_calculator.cpp` / `wifi_cfg_keyboard_poll()`：统一忽略 `'\t'`（Alt+Enter 扫描组合键）与 `'\v'`，防止控制字符写入 textarea
- `README.md` 键盘层图与特殊键说明同步更新

## 4. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS（含 C/C++ 混编链接） |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 页面切换清缓冲 | ⏸ 待测 | WiFi config 快速连按 ⌫ 退到 WiFi 页后应立即能重新进入，无自动回弹 |
| 空框 Enter 扫描 | ⏸ 待测 | 清空 SSID 后按 Enter 应显示 Scanning... → Scan: N found |
| Alt+Enter 扫描（非空框） | ⏸ 待测 | 框内有字时 Alt+Enter 仍可扫描 |
| 扫描中切密码框 | ⏸ 待测 | 扫描完成不得改写 SSID 草稿/进入选择模式 |
| 扫描中退出再进 4.2 | ⏸ 待测 | 4.2 Wifi Scan 页应立即可扫描 |
| Save/Clear 按钮 | ⏸ 待测 | 触摸 Save = 保存+连接；Clear = 双框+缓存+NVS 清空，状态栏 "Cleared" |
| 音量键/麦克风键 | ⏸ 待测 | Sym 层：音量键不再出 0（无反应/保留），麦克风键出 0；普通层麦克风无反应 |
| 双 Shift 交叠、快速输入 | ⏸ 待测 | 串口 `[KBD]` 日志辅助确认 |

## 5. 回滚方案

```bash
git revert 6c51964      # 仅回退 WiFi 扫描生命周期 + Save/Clear 按钮
git revert 6a9ab00      # 仅回退键盘页面边界 + 键位修正
```

两个 commit 均不涉及 `boards/`、`platformio.ini`、分区表、硬件配置。

## 6. 申请审批事项

- [ ] **A. 全量接受** — 两 commit 保留，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
