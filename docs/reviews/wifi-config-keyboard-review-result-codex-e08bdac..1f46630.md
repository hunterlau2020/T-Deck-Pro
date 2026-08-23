# 第 24/25 轮整改评审结果（Codex）— 输入延迟 + 滚动延迟

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..1f46630.md](wifi-config-keyboard-review-request-e08bdac..1f46630.md)
- **关联代码范围**：`e08bdac..1f46630`（申请人登记 9 个非 Key commit；含 1 个 doc-only + 2 个 reviews 注册 commit）
- **本次重点新增范围**：
  - `7ecebcd` — 键盘突发输入合并渲染（用户反馈：输入延迟）
  - `1f46630` — 触摸滚动抬手才刷 + Enter 改换行（用户反馈）
- **评审结论**：**A 全量接受**（9 个非 Key commit + 上轮 7 commit + 历史 28 commit 全部接受；阻塞项已闭合）

---

## 1. Findings

### 1.1 沿用（上轮已闭合）

- **e08bdac** Usage 弹窗 6 行 breakdown
- **8770a41 + f3e1698** WiFi 扫描覆盖层帧序号绑定
- **f4449c3** 双 Tab 布局
- **0b43685** chat_exit 也 hide waitbox
- **cc94452** Tab 移入顶栏
- **06a2c13** Chat tab `\v` ignore

### 1.2 7ecebcd 键盘突发输入合并渲染 — 解决"每字符一次 EPD 全刷"

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_cfg.cpp:391-426` + `examples/pda2/ui_ai_chat.cpp:789-880` + `examples/pda2/ui_deckpro.cpp:2114-2194`
- **机制**：
  - 三个文本输入 poll（AI Chat / AI Config / WiFi Cfg）整体包入 `for (int guard = 0; guard < 32; guard++)` 循环
  - 每轮迭代：`keypad_get_val(&c)` → 若无键 `break`；否则 `keypad_set_flag()`（legacy no-op）+ 分派
  - 循环上限 32 键/帧：FIFO 顺序分派控制键，连打整串合并为单次 EPD 渲染
- **观察**：
  - **`break` vs `continue` 语义保留正确**：
    - `break` 用于"终态"：save 成功后、scr_mgr_pop 后、waitbox 打开后、confirm 接受/取消后
    - `continue` 用于"按键已处理但仍可继续处理后续键"：Alt+Enter toggle tab、`\v` no-op 后
  - **`keypad_set_flag` 现在循环内调用**：原 legacy API 是 no-op，新位置不影响语义
  - **跨 poll 协作**：本 pass 排空后下 pass 从空 FIFO 开始，遗留键（>32）下帧再处理
  - **与等待层交互**：`chat_waitbox != NULL` 检查仍在 burst loop 之外——等待中吞键（一次一帧，不影响）
  - **AI Cfg save 后 break**：save 不开 msgbox（成功路径），但 break 防连按 '\n' 触发重复 save（无害但浪费 NVS 写）
- **影响**：
  - 用户打 "hello world" 整串 → 一次 EPD flush 而非 11 次
  - 在 EPD 0.3-1s/flush 的成本下，延迟从 11×0.5s = 5.5s 降到 0.5s
- **结论**：根治用户反馈；机制稳健。

### 1.3 1f46630 触摸滚动抬手才刷 + Enter 改换行 — 解决"滚一步刷一次 + 多行写作"

- **严重性**：✅ 通过
- **位置**：
  - `examples/pda2/factory.h:69` + `examples/pda2/factory.ino:69-78, 247-254, 294-300`
  - `examples/pda2/ui_deckpro_port.{cpp,h}:51-53, 44`
  - `examples/pda2/ui_ai_chat.cpp:584-597, 859-863, 1070-1071, 1161-1163`
- **机制 A：redraw-on-release 滚动**
  - 新增 `disp_suppress_flush` (volatile bool) + `disp_set_suppress_flush(bool)` public API
  - `ui_disp_suppress_flush(bool)` 屏幕层 wrapper
  - `flush_timer_cb`：suppress=true 时 `disp_refr_mode = PART; lv_timer_pause(flush_timer); return;`（不再每帧 lv_timer 触发 EPD flush）
  - `disp_flush`：suppress=true 时跳过 `flush_epd_bitmap(area)` 调用；pixels 仍通过 `convert_lvgl_buf_to_epd_bitmap` 累积到 `decodebuffer`
  - `chat_scroll_begin_cb` / `chat_scroll_end_cb`：`LV_EVENT_SCROLL_BEGIN`/`SCROLL_END` 绑定到 `chat_hist_cont`；仅 `lv_event_get_indev(e) != NULL`（即触摸，非程序）触发 suppress
  - 抬手时 `chat_scroll_end_cb` → `ui_disp_suppress_flush(false)` + `ui_disp_full_refr()` → 下次 `dips_render_start_cb` resume flush_timer → `flush_timer_cb` 看到 FULL mode → 全区刷一次
- **机制 B：Enter 改换行**
  - Input tab 的 `'\n'` 不再调 `chat_send()`，改为 `lv_textarea_add_char(chat_input_ta, '\n')` 插入换行
  - 发送只走 Send 按钮（touch）
  - 头部 keypad map 注释同步更新
- **观察**：
  - **`decodebuffer` 累积机制正确**：`convert_lvgl_buf_to_epd_bitmap` 总是执行（先于 suppress 检查），所以滚动期间 decodebuffer 持续更新；suppress 仅阻止 push 到 EPD
  - **程序化滚动不受影响**：`lv_obj_scroll_to_y(..., LV_COORD_MAX, ...)`（chat_history_render 末尾）+ `lv_obj_scroll_by(...)`（键盘 +/-）的 SCROLL_BEGIN 事件 indev=NULL → suppress 不被设置 → 正常刷新
  - **10s 全刷 ghosting 计数兼容**：`flush_timer_cb` 的 part_count 仅在 suppress=false 且 mode=PART 时递增；suppress 期间 timer 被 pause → part_count 不增长 → 全刷决策不受影响
  - **离屏清理（chat_exit）**：`ui_disp_suppress_flush(false)` 在 waitbox_hide 之前调用 → 即便用户触摸滚动中按 Back，suppress 立即解除、waitbox 隐藏、exit 触发全刷
  - **仅 chat_hist_cont 绑定**：其他 scrollable（菜单、weather、calendar 等）仍正常逐帧刷——chat history 是最大且最慢的滚动对象
  - **Enter 改换行 → 多行 writing**：textarea max 200 chars 仍生效；中文/emoji 仍 UTF-8 码点正确；Send 按钮与触摸 Send 路径一致
  - **状态机正确**：7ecebcd burst loop + 1f46630 Enter=newline 配合 → 用户在 Input tab 连按 Enter 输入多行，burst loop 一次性处理完所有换行字符
- **潜在观察**（Low，不阻断）：
  - **`disp_suppress_flush` + `lv_timer_pause(flush_timer)` 解除依赖**：`chat_scroll_end_cb` 清 suppress 后必须依赖下一次 LVGL render 触发 `dips_render_start_cb` resume timer。若 LVGL 无后续 invalidate（极端静止态），flush_timer 会保持 pause 直到下次任何动画/状态变化。在实际 UI 上几乎不会发生（cursor blink / status update 都会触发 invalidate），但理论最坏延迟 30ms 量级。
- **影响**：
  - 用户滚动整页（10 步）→ 1 次全刷 而非 10 次
  - 多行 writing（KET writing 题场景）→ Enter 自然换行
- **结论**：精准解决用户反馈的两个 UX 问题；机制稳健。

---

## 2. 已通过项汇总（沿用 + 本轮新增）

### 沿用（前两轮）
- **e08bdac** Usage 弹窗 6 行 breakdown
- **8770a41** WiFi 扫描 800ms 最短显示（原始版）
- **f4449c3** AI Text 双 Tab 布局
- **0b43685** chat_exit 也 hide waitbox
- **cc94452** Tab 按钮移入顶栏
- **06a2c13** Chat tab `\v` ignore
- **f3e1698** 扫描覆盖层帧序号绑定（Codex 改进）

### 本轮新增
- **7ecebcd** 三个文本输入 poll 突发按键合并渲染（≤32 键/帧）
- **1f46630** 触摸滚动抬手才刷（disp_suppress_flush 机制）+ Input tab Enter 改换行

### 历史（本批次之前）
- 844a907..156732c 28 commit（Codex 全量接受）

---

## 3. 已接受但未消除的安全风险

- 真实 API Key 仍在源码与 Git 历史中。按 `api-key-dev-exception` 用户决策延后；C1/C2 已落地。
- 推公网 / 重大 release 前必须按 `SECURITY.md` 4 步处理。
- 本评审**不视为阻塞项**。

## 4. 跟踪项（继承 + 本批新观察）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| Codex §1.11 chat_exit hide waitbox | 上上轮 §1.11 | ✅ 0b43685 闭合 |
| Codex §1.3 Chat tab `\v` ignore | 上轮 §1.3 | ✅ 06a2c13 闭合 |
| 扫描覆盖层计时 ≠ 可见时间 | 上轮 §1.2 观察 | ✅ f3e1698 闭合 |
| WiFi scan overlay 跨 push 屏残留 | 预存在 | 本批未触及；建议下轮在 `exit4_1` 也 hide |
| chat 重试草稿恢复在 create 而非 entry | 预存在 | 行为可接受 |
| **suppress_flush + pause timer 解除依赖 LVGL 后续 invalidate** | **本批 §1.3 观察** | **实际不可见（cursor blink / status update 触发）；理论最坏 30ms** |
| SPIFFS /chat.log append+compact | 主评审 §1.2 | TODO |
| CJK 8KB 预算裁剪 UI 提示 | 主评审 §1.3 | 部分实施 |
| system prompt NVS 化 | TODO | 阶段 1 |
| 长回答 >4KB 实测 | §4 #9 | 代码层已覆盖 |
| 失败重试路径实测 | §4 #18 | 代码层已覆盖 |

## 5. 验证说明

- `python scripts/test_nvs_atomic_save.py` → 11 项 PASS（沿用）
- 当前环境无 `pio` 可执行，未独立复现固件编译
- 申请人自测：`pio run -e pda2` SUCCESS；COM5 烧录 + Hash verified
- 真机回归：申请 §3 列 5 项均为 ⏸（待用户本批烧录后实测）
- 本评审**仅静态复核**：
  - `git show 7ecebcd` 逐字符追踪三个 poll 的 burst loop + break/continue 位置
  - `git show 1f46630` 追踪 disp_suppress_flush 流程：suppress 期间 `convert_lvgl_buf_to_epd_bitmap` 仍执行（累积到 decodebuffer）+ `flush_epd_bitmap` 跳过；释放时 `dips_render_start_cb` resume timer → FULL flush
  - 验证 `chat_scroll_begin_cb` 的 `lv_event_get_indev(e) != NULL` 守卫生效：程序化 `lv_obj_scroll_to_y` 不触发 suppress
  - 验证 `chat_exit` 的 `ui_disp_suppress_flush(false)` 防"中滚离屏冻结"
- 结果文档未包含 API Key 正文

## 6. 审批意见

- [x] **A. 全量接受** — 保留全部 9 个非 Key commit，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受范围**：本批 9 commit 中 8 commit 全量接受（除 Key 项按用户决策延后外）；7ecebcd 输入延迟与 1f46630 滚动延迟两个用户反馈问题精准闭合；与 f3e1698（帧序号绑定）共享同一架构原则（"对象生命周期 ≠ 面板可见时间"）。

**遗留项**：
- Key 项按 `api-key-dev-exception` 决策延后；跟踪至推公网 / 重大 release 前
- SPIFFS 整文件重写（主评审 §1.2）已登记 `TODO.md`
- wifi_scan_overlay 跨 push 屏残留：建议下轮顺手在 `exit4_1` 也 hide

---

**评审人**：Codex（第三方静态复核视角，已交叉核对 `git show e08bdac 8770a41 f4449c3 0b43685 cc94452 06a2c13 f3e1698 7ecebcd 1f46630` 的实际 diff；burst loop 的 break/continue 语义逐位置校验；redraw-on-release 的 flush_timer pause/resume 链与 dispatch_render_start_cb 的联动确认）。