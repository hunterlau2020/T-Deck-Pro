# 评审申请书：WiFi 配置交互重设计 + 键盘物理布局实测修正（修订版，第二次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-15（初版）；**2026-08-16 修订后重新申请**
- **关联分支**：`HD-V2-250915`
- **关联 commit**：
  - `3d98321` — `pda2: fix keypad modifiers per review`（键盘修饰键状态机 + Alt 临时符号层 + FIFO 排空/溢出恢复 + README 布局）
  - `f3f2a58` — `pda2: rework WiFi config screen per review`（编辑/扫描双模式 + 异步扫描 + 错误码区分 + 触摸焦点同步）
  - `eafa672` — `pda2: improve AI chat input UX`（发送后清空输入框 + 查看模式打字回输入）
- **关联改动**：3 个 commit，共 4 个文件（+297 / −158 行）
- **关联文档**：[`examples/pda2/README.md`](../../examples/pda2/README.md)（键盘布局图已同步更新）
- **关联代码**：`examples/pda2/peri_keypad.cpp`、`examples/pda2/ui_deckpro.cpp`、`examples/pda2/ui_ai_chat.cpp`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）
- **评审结论前次**：[`wifi-config-keyboard-review-result.md`](wifi-config-keyboard-review-result.md)（退回修订，Findings 1.1–1.5）

---

## 0. 修订记录（2026-08-16，对照前次 Findings）

| Finding | 修订内容 |
|---|---|
| **1.1** `+`/`-` 无条件占用 | 引入显式 **手动编辑 / 扫描选择** 双模式（`wifi_cfg_scan_mode`）。仅扫描选择模式拦截 `+`/`-`；手动编辑模式下**所有可见字符**（含 `+ -`）都写入文本框；扫描模式中按任意可见字符 = 退出扫描模式并追加该字符 |
| **1.2** 已有 SSID 时扫描入口不可达 | 扫描入口改为独立组合键 **Alt+Enter**（`keypad_loop` 在 Alt 按住时把 Enter 译为 `'\t'`），与文本框内容无关；Enter 仅负责提交/切字段。空框按 Enter 提示 "Type SSID or Alt+Enter:scan"，不再触发扫描 |
| **1.3** 三 Shift 共用布尔 | 三个修饰键分别记录（`alt_pressed` / `shift_l_press` / `shift_r_press`），大写层状态取两个 Shift 的 **逻辑 OR**；释放其一不影响另一个 |
| **1.4** 同步扫描丢事件 | ① WiFi 扫描改为**异步**（`WiFi.scanNetworks(true)` + `scanComplete()` 轮询，主循环持续排空键盘 FIFO）；② `keypad_loop` 每轮 **while 排空全部 FIFO 事件**；③ 每轮读 `INT_STAT` 检测 `OVR_FLOW_INT`，溢出时**重置全部修饰键状态**（防丢失释放事件卡层） |
| **1.5** 错误码合并 | 区分三态：`n == 0` → "Scan: none found"；`n < 0` → "Scan failed (code)"；异步启动失败 → "Scan start fail (code)"。**未**添加无条件 `WiFi.disconnect()`（评审明确不接受） |
| §5.1 Alt 语义 | 产品决策：**B 方案已实施**——Alt 为按住生效的临时符号层（与 Sym 锁同层不同锁），Shift 仅管大写 |
| 评审要求回归项 | 见 §6 验证状态，真机回归待用户配合完成 |

其余部分（§1 申请事由、§2 实测布局、§4.3 焦点同步、§4.4 AI 聊天、§5 待评审重点中已批复项）保持初版不变，下文为完整申请书。

---

## 1. 申请事由

固件烧录真机后，用户报告 4 个问题：

| # | 用户报告 | 定位结果 |
|---|---|---|
| 1 | 删除键变成"返回"键，不删输入框文字 | WiFi 配置页 SSID 字段为下拉框，`\b` 无条件退出页面（无删字逻辑）；AI 聊天发送后输入框残留已发文字，查看模式按 `\b` 翻页/清屏，观感如"删除失灵" |
| 2 | Alt 键变成 Shift（按 Alt 出大写），Shift 键无反应 | 固件只映射了 Z 行最左键 (2,0) 为 Shift；HD-V2 硬件该键丝印为 **Alt**，而丝印为 Shift 的两个键（底行 (3,5)/(3,9)）**未映射** |
| 3 | Sym 键用途不明 | 功能正常（符号层锁定开关），属文档问题，已在 README 与本文档说明 |
| 4 | WiFi 下拉框不能输入 SSID、不显示扫描结果 | 下拉框本身不可编辑；且**触摸点开下拉框后再按 Enter 会把占位文字 "(Enter: scan)" 当作 SSID 选中**，扫描流程永远走不到 |

> ⚠️ **流程偏差说明**：本次为真机联调，采用"实测解码 → 直接改码 → 事后评审"顺序，未先评审设计。改动集中在单一功能（WiFi 配置）+ 单一驱动（键盘矩阵），且已通过编译与烧录验证，风险可控；若贵方工作流要求先评审，可按 §7 回滚。

---

## 2. 键盘物理布局实测（问题 #2 的证据基础）

烧录 `examples/test_keypad` 原始矩阵示例，用户按 12 个指定键，串口日志解码如下（固件坐标 = 原始坐标列镜像，`col = 9 - raw_col`）：

| 用户按键 | 原始坐标 | 固件坐标 | 改动前固件行为 |
|---|---|---|---|
| Alt（Z 行最左） | R2 C9 | (2,0) | 当作 Shift → 大写（用户所见"Alt 变大写"） |
| Shift（底行左） | R3 C4 | (3,5) | **未映射 → 无反应** |
| Shift（底行右） | R3 C0 | (3,9) | **未映射 → 无反应** |
| Sym | R3 C1 | (3,8) | 符号层锁定 ✓ 正确 |
| Space / Mic | R3 C2 / R3 C3 | (3,7) / (3,6) | Space ✓，Mic 无功能 |
| Q / Z / M / ♪ / ⏎ / ⌫ | — | — | 与固件映射全部一致 ✓ |

实测物理布局（**无 Ctrl 键**，与旧注释中 "LCtrl/RCtrl" 的猜测不符）：

```
Q   W   E   R   T   Y   U   I   O   P
A   S   D   F   G   H   J   K   L   ⌫
Alt Z   X   C   V   B   N   M   ♪   ⏎
⇧   Mic Space Sym ⇧
```

---

## 3. 代码变更总览

| # | 文件 | +/− | 关键变更摘要 |
|---|---|---|---|
| 1 | `examples/pda2/peri_keypad.cpp` | +17 / −12 | 新增两个 Shift 键 (3,5)/(3,9) 触发 shift 层；(2,0) 保留为 shift（Alt 充当第三个 Shift）；修正 row 3 注释为实测布局 |
| 2 | `examples/pda2/ui_deckpro.cpp` | +112 / −103 | WiFi 配置屏（screen 4.1）SSID 字段**下拉框 → 文本框**；扫描结果存内部数组，`+`/`-` 循环选择；删除 `wifi_dd_value_cb` 触摸回调；**触摸焦点 ↔ 键盘字段状态同步**（§4.3，真机 bug 修复） |
| 3 | `examples/pda2/ui_ai_chat.cpp` | +8 / −1 | `chat_send()` 成功后清空输入框；查看回复模式按任意可见字符 → 自动回输入模式并追加该字符（原为静默丢弃） |
| 4 | `examples/pda2/README.md` | +30 / −30 | 键盘布局图改为实测 HD-V2 布局；说明三键均可大写 |

无新增库依赖、无 `boards/` / `platformio.ini` / 分区表变更。

---

## 4. 变更明细

### 4.1 键盘修饰键状态机（`peri_keypad.cpp`，修订版）

```cpp
#define KEY_ALT_ROW     2
#define KEY_ALT_COL     0   /* Z-row left key, silkscreened "Alt" */
#define KEY_SHIFT_L_ROW 3
#define KEY_SHIFT_L_COL 5   /* bottom-row left Shift */
#define KEY_SHIFT_R_ROW 3
#define KEY_SHIFT_R_COL 9   /* bottom-row right Shift */
#define KEY_SYM_ROW     3
#define KEY_SYM_COL     8

static bool alt_pressed   = false;   /* Alt (2,0): momentary sym layer */
static bool shift_l_press = false;   /* Shift (3,5): uppercase layer */
static bool shift_r_press = false;   /* Shift (3,9): uppercase layer */
static bool sym_lock      = false;   /* Sym (3,8): locked sym layer */
```

- **语义（产品决策 B）**：Alt = 按住生效的**临时符号层**（与 Sym 锁同层不同锁）；Shift（两个）= 大写层；Sym = 符号层锁定。取值优先级：`sym_lock || alt_pressed` → sym 层；否则 `shift_l || shift_r` → 大写层；否则普通层
- **独立状态**（finding 1.3）：三个修饰键各记各的按下/释放，大写层 = 两个 Shift 的**逻辑 OR**，交叠按放不错乱
- **FIFO 排空**（finding 1.4）：`keypad_loop` 每轮 `while (keypad.available() > 0)` 一次性消费全部事件
- **溢出恢复**（finding 1.4）：每轮读 `INT_STAT`（read-to-clear），`OVR_FLOW_INT` 置位时重置全部修饰键并打印 `[KBD] FIFO overflow - modifiers reset`
- **Alt+Enter 组合键**：Alt 按住时按 Enter 译为 `'\t'`（0x09），供 WiFi 配置屏作扫描快捷键；普通 Enter 仍为 `'\n'`

### 4.2 WiFi 配置交互重设计（`ui_deckpro.cpp`，修订版）

**双模式状态机**（`wifi_cfg_scan_mode` + `wifi_scan_state`）：

| 按键 | 手动编辑模式（默认） | 扫描选择模式（有结果后自动进入） |
|---|---|---|
| 可见字符（含 `+ -`） | 追加到文本框（finding 1.1） | 退出扫描模式，回编辑模式并追加该字符 |
| Alt+Enter（`'\t'`） | **开始异步扫描**（与框内容无关，finding 1.2） | 重新扫描 |
| Enter（`'\n'`） | 框非空 → 提交并跳到密码框；框空 → 状态栏提示 | 选中当前候选 → 提交并跳到密码框 |
| `+` / `-` | 追加到文本框（finding 1.1） | 循环切换扫描候选 |
| `\b` | 有字删字；为空退出本屏 | 取消选择，恢复扫描前框内容，回编辑模式 |
| 扫描进行中 | 按键忽略（状态栏 "Scanning..."；修饰键仍由 keypad_loop 维护） | — |

**异步扫描**（finding 1.4）：`wifi_cfg_scan_start()` 调 `WiFi.scanNetworks(true)`（立即返回 `WIFI_SCAN_RUNNING`）；`wifi_cfg_scan_poll()` 挂在 `wifi_cfg_keyboard_poll()` 入口、**每个 loop 周期**查 `WiFi.scanComplete()`。扫描期间主循环持续排空键盘 FIFO，无阻塞丢事件风险。

**错误码区分**（finding 1.5）：`scanComplete() < 0` → "Scan failed (code)"；结果为 0 → "Scan: none found"（保持编辑模式）；异步启动失败 → "Scan start fail (code)"。按评审意见**未**添加无条件 `WiFi.disconnect()`。

**触摸一致性**：触摸点按 pass 框即放弃扫描选择（回编辑模式）；`destroy4_1` 退出屏幕时 `WiFi.scanDelete()` 取消未完成的异步扫描。

**屏上提示**（已更新）："Alt+Enter:scan  +/-:pick / Enter:next/save  Backspace:del/back"

### 4.3 触摸焦点与键盘字段状态同步（`ui_deckpro.cpp`，2026-08-16 补录）

**真机测试发现的 bug**：WiFi 配置屏上，触摸点击 pass 输入框（LVGL 光标移入 pass 框）后按 `\b`，删除的却是 SSID 框的文字。

**根因**：LVGL 8.3 的 `lv_indev.c::indev_click_focus()` 在指针点按可点击对象时**独立于业务逻辑**发送 `LV_EVENT_FOCUSED`（无需 group），文本框光标跟随触摸移动；而键盘 poll 只认内部 `wifi_cfg_field` 状态（仍为 0 = SSID），两条输入路径脱节。

**修复**：
- 两个文本框各挂 `LV_EVENT_FOCUSED` 回调（`wifi_ssid_focus_cb` / `wifi_pass_focus_cb`），触摸移焦时同步 `wifi_cfg_field`；回调带字段守卫——点按**已激活**的框不触发 refresh，保护未提交的手动输入不被 `wifi_cfg_refresh()` 重置
- 新增 `wifi_cfg_set_field(int)`：键盘切换字段时发送 `LV_EVENT_FOCUSED` 使光标跟随 ">" 标记（`LV_EVENT_FOCUSED` 会重启 textarea 光标闪烁动画），替换 poll 中两处直接赋值

**请评审**：§5.5 的"未提交即丢失"语义在触摸路径下同样成立（点按另一框会重置未提交文字），是否需要改为"切换字段时把文本框内容先暂存"？

### 4.4 AI 聊天输入框体验（`ui_ai_chat.cpp`）

- `chat_send()` 成功发送后 `lv_textarea_set_text(chat_ta, "")` —— 输入框不再残留已发文字；此后查看模式下 `\b` 翻页/返回语义无歧义（用户问题 #1 的另一触发场景）
- `ai_chat_keyboard_poll()` 查看模式新增 `else if (c >= ' ')` 分支：按任意可见字符自动退出查看、清空回复区并追加到输入框（原实现静默丢弃，用户可能误以为键盘失灵后连按 `\b` 导致退出）

---

## 5. 待评审重点（请专家逐项确认）

### 5.1 Alt 键语义（High）——**已决，产品决策 B**

评审建议 B、产品确认 B：Alt = **按住生效的临时符号层**（§4.1 已实施）。同时保留 Alt+Enter = 扫描组合键。若后续产品改回 A，仅需一行改动并更新文档。

### 5.2 `\b` 空框即返回的约定（Medium）

所有输入屏维持"有字删字、无字返回（上一步/退出）"约定。用户问题 #1 表明该约定可感知性不足。**请确认**：是否需要在每个屏提示行明确写 "Backspace=del/back"，或改为 `\b` 永不退出、仅靠屏幕右上角触摸返回按钮退出（键盘用户将无返回路径，需评估）。

### 5.3 扫描与自动连接的互斥（High）——**按评审意见修订，待真机验证**

`factory.ino::setup()` 末尾会读取 NVS 并用旧凭据 `WiFi.begin()` 自动连接。修订版：**不**无条件 `disconnect()`；错误码已区分（finding 1.5），若真机回归发现"连接中扫描必失败"，再在配置页生命周期内暂停自动重连、退出时恢复（评审认可的路径）。

### 5.4 阻塞扫描的响应性（Medium）——**已修订**

改为**异步扫描**（`scanNetworks(true)` + 每 loop 轮询 `scanComplete()`），主循环不再阻塞；`keypad_loop` 每轮全量排空 FIFO；`OVR_FLOW_INT` 溢出时重置全部修饰键（finding 1.4）。评审申请的"排队不丢失"结论已撤回，以评审意见为准。

### 5.5 文本框与 NVS 缓存的同步语义（Low）

`wifi_cfg_refresh()` 会用 `wifi_ssid`（NVS 缓存/上次提交值）整体覆盖文本框。未提交的手动输入在 `+`/`-` 循环、字段切换后即丢失——属预期行为（未提交 = 未保存）。请确认该语义是否需要在提示行明示。

### 5.6 内存占用（Low）——**评审已接受**

评审指出 `UI_WIFI_SCAN_ITEM_MAX` 实际为 **13**，新增数组 `13 × 33 = 429` 字节，可接受。

### 5.7 提交拆分建议（流程）

当前 4 个文件改动未提交，建议拆分（若通过）：
1. `peri_keypad.cpp` + `README.md` —— 键盘布局修正（独立、可 bisect）
2. `ui_deckpro.cpp` —— WiFi 配置重设计
3. `ui_ai_chat.cpp` —— 聊天输入框体验

---

## 6. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 键盘矩阵实测解码 | ✅ 完成 | `test_keypad` 烧录 + 用户按 12 键 + 串口日志（见 §2 表格） |
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS，RAM 46.4% / Flash 29.7% |
| 烧录 | ✅ 完成 | COM5，`Wrote 1,947,920 bytes ... Hash of data verified` |
| 串口 Shift/Alt 日志 | ⏸ 待测 | 按 Shift/Alt 应打印 `shift(l)=1` / `shift(r)=1` / `alt=1` 等日志 |
| WiFi 手动输入/扫描/循环选择 | ⏸ 待测 | 需真机进入 WiFi Config 实测打字、Alt+Enter 扫描、`+`/`-` 切换 |
| 触摸焦点同步（§4.3 bug） | ⏸ 待复测 | 复现步骤：点触 pass 框 → 按 `\b` → 应删 pass 文字而非 SSID |
| **评审回归 1**：`+`/`-` SSID | ⏸ 待测 | 手动输入 `Home-5G`、`AP+Guest` 应完整写入文本框 |
| **评审回归 2**：已存 SSID 时扫描 | ⏸ 待测 | NVS 有旧 SSID 时按 **Alt+Enter** 应直接开始扫描 |
| **评审回归 3**：双 Shift 交叠 | ⏸ 待测 | 左+右 Shift 同按、不同顺序释放，期间输出保持大写（串口 `shift(l)=1 shift(r)=1`） |
| **评审回归 4**：扫描期间连按 | ⏸ 待测 | 异步扫描期间快速连按 >10 键，扫描后修饰键不卡；溢出时打印 `[KBD] FIFO overflow - modifiers reset` |
| **评审回归 5**：扫描错误区分 | ⏸ 待测 | 已连接/连接中/失败状态下扫描，状态栏文案应能区分三态 |
| WiFi 连接（真实凭据） | ⏸ 未做 | 需用户提供测试热点或自行验证 |
| 其他屏回归（LoRa 下拉框、菜单、AI 聊天） | ⏸ 未做 | `ui_deckpro.cpp` 中 LoRa 等屏仍用 dropdown，本次未触碰（已 grep 确认无残留引用） |

---

## 7. 回滚方案

改动已按 §5.7 拆分为 3 个 commit，按需回滚：

```bash
git revert eafa672          # 仅回退 AI 聊天输入框改动
git revert f3f2a58          # 仅回退 WiFi 配置重设计
git revert 3d98321          # 仅回退键盘修饰键改动
```

不涉及 `boards/`、`platformio.ini`、分区表、硬件配置，回滚后无副作用。

---

## 8. 申请审批事项

请审批人确认以下任一选项：

- [ ] **A. 全量接受** — 4 个文件改动按 §5.7 拆 3 个 commit 提交到 `HD-V2-250915`
- [ ] **B. 键盘部分先行** — 仅接受 `peri_keypad.cpp` + `README.md`（问题 #2 修复），WiFi/AI 部分退回修订
- [ ] **C. 退回修订** — 具体修订意见：________________
- [ ] **D. 按 §5 待评审重点逐项批复后再定** — 先答复 5.1–5.7 各项

**审批人**（手写或电子签名）：________________
**审批日期**：________________
