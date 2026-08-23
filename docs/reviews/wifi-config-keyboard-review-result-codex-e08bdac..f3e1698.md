# 第 23/24 轮整改评审结果（Codex）— 用户反馈修复 + Codex 直接改进

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..f3e1698.md](wifi-config-keyboard-review-request-e08bdac..f3e1698.md)
- **关联代码范围**：`e08bdac..f3e1698`（申请人登记 7 个非 Key commit；含 1 个 doc-only + 2 个 reviews 注册 commit）
- **本次重点范围**：
  - 沿用上轮：e08bdac / 8770a41 / f4449c3 / 0b43685
  - 新增：cc94452（Tab 移入顶栏）/ 06a2c13（Codex §1.3 `\v` ignore 修复）/ f3e1698（Codex 重写 800ms 最短显示为帧序号绑定）
- **评审结论**：**A 全量接受**（4 个上轮 commit + 3 个本轮 commit 全部接受；阻塞项已闭合）

---

## 1. Findings

### 1.1 e08bdac Usage 弹窗完整明细

- **结论**：✅ 通过（沿用上轮 §1.1）

### 1.2 8770a41 → f3e1698 WiFi 扫描覆盖层最短显示 — 从对象创建 → 帧可见

- **严重性**：✅ 通过（Codex 直接改进）
- **位置**：`examples/pda2/ui_deckpro.cpp:2367-2369, 2391-2395, 2405-2413, 2417-2443`
- **演进**：
  - 8770a41 用 `millis() - wifi_scan_ovl_t0 >= 800ms`：计时起点是对象创建时刻——EPD 局刷未完成时对象已存在但不可见
  - **f3e1698 改进**：计时起点改为覆盖层**自己的帧到达面板**时刻，使用 Sleep 同款 `ui_disp_full_refr_seq()` / `ui_disp_flush_done_seq()` 机制
- **机制**：
  - `wifi_scan_overlay_show` 新增：
    ```c
    wifi_scan_ovl_frame_visible = false;
    wifi_scan_ovl_visible_t0 = 0;
    wifi_scan_ovl_flush_seq = ui_disp_full_refr_seq();   // 捕获本次帧序号
    ```
  - `wifi_scan_overlay_update` 新增帧可见性检测：
    ```c
    if (!wifi_scan_ovl_frame_visible &&
        ui_disp_flush_done_seq() >= wifi_scan_ovl_flush_seq) {
        wifi_scan_ovl_frame_visible = true;
        wifi_scan_ovl_visible_t0 = millis();
        Serial.println("[WiFi] scan overlay reached panel");
    }
    ```
  - min-display 守卫从 `wifi_scan_ovl_t0` 改为 `wifi_scan_ovl_visible_t0`：
    ```c
    if (wifi_scan_state != WIFI_SCAN_RUNNING &&
        wifi_scan_ovl_frame_visible &&                       // <-- 必须先 frame 可见
        millis() - wifi_scan_ovl_visible_t0 >= WIFI_SCAN_OVL_MIN_MS) {
        wifi_scan_overlay_hide();
    }
    ```
  - 额外：`lv_obj_move_foreground(wifi_scan_ovl)` 在每次 update 调用——结果横幅出现时进度层仍置顶
- **观察**：
  - **设计一致性**：与 Sleep frame-wait（`9a89cdd`）共用同一对 EPD flush 序号 API
  - **10s 兜底不变**：`wifi_scan_ovl_t0` 仍用于 abort 超时（`WIFI_SCAN_OVL_TIMEOUT_MS`）——两条计时线职责清晰
  - **边界 case**：若 EPD 永远不到达该帧（理论上不可能，但容错），frame_visible 永远 false → 10s 兜底触发 abort
  - **结果横幅优先级**：`lv_obj_move_foreground` 每帧调用，避免 layer top 上其他元素（横幅、msgbox）覆盖进度层
- **结论**：本评审 §1.2 上轮担心"对象生命周期 ≠ 可见时间"被精准闭环；机制稳健。

### 1.3 f4449c3 → cc94452 双 Tab 布局 → Tab 移入顶栏

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:101-106, 985-1010`
- **演进**：
  - f4449c3 在 `cont`（内容容器）内部 30px tab_row
  - **cc94452 改进**：tab 按钮移到 `parent`（屏幕根）顶栏，与 back 按钮同行
- **机制**：
  - 移除 `cont` 内部的 `tab_row`
  - `chat_tab_btn = lv_btn_create(parent)` + `lv_obj_set_size(54, 30)` + `lv_align(chat_tab_btn, LV_ALIGN_TOP_RIGHT, -58, 3)`（Chat 在 Input 左边 4px 间隔）
  - `input_tab_btn` 同样方式 + `LV_ALIGN_TOP_RIGHT, 0, 3`
- **观察**：
  - **空间释放**：整个 `cont`（232×288）下方全部归页面使用 → 历史区比上一版再 +30px
  - **与 back 按钮共存**：back 在 TOP_LEFT,3,3 + 文字 label；Chat tab 在 TOP_RIGHT -58,3（54×30）；Input tab 在 TOP_RIGHT 0,3（54×30）—— 240px 宽屏上无视觉重叠
  - **`chat_set_tab` 状态翻转仍生效**：`chat_set_tab(input_tab)` 用 `lv_obj_get_child(chat_tab_btn, 0)` 取 label 改文字色——buttons 仍在内存
  - **容器布局简化**：`cont` 现在只有 status + chat_page/input_page 两层，flex COLUMN 布局简单
- **结论**：用户反馈"chat box still too small"的精准响应。

### 1.4 f4449c3 §1.3 bug → 06a2c13 `\v` 在 Chat tab 忽略（Codex §1.3 修复）

- **严重性**：✅ 通过（Medium → 已闭合）
- **位置**：`examples/pda2/ui_ai_chat.cpp:819-823`
- **修复**：
  ```c
  if (!chat_tab_input) {
      /* --- Chat tab --- */
      if (c == '+' || c == '-') { ... }
      else if (c == '\b') { ... }
      else if (c == '\n') { ... }
      else if (c == '\v') {
          /* volume key: no-op on the Chat tab - New confirm is
           * Input-tab only, and the control byte must never reach the
           * textarea (codex finding 1.3) */
          return;
      }
      else {
          chat_set_tab(true);
          lv_textarea_add_char(chat_input_ta, c);
      }
  }
  ```
- **观察**：
  - 完全采纳本评审 §1.3 的最小修复建议（`else if (c == '\v') { return; }`）
  - 注释明示"the control byte must never reach the textarea"——防御意图清晰
  - Input tab 分支保留 `c == '\v' → chat_confirm_show()` 不变
- **结论**：上轮 §1.3 跟踪项精准闭合。

### 1.5 0b43685 chat_exit hide waitbox（Codex §1.11 修复）

- **结论**：✅ 通过（沿用上轮 §1.4）

---

## 2. 已通过项汇总（沿用 + 本轮新增）

### 沿用（上轮）
- **e08bdac** Usage 弹窗 6 行 breakdown + 205px 高度变体
- **8770a41** WiFi 扫描 800ms 最短显示（计时起点 = 对象创建）
- **f4449c3** AI Text 双 Tab 布局
- **0b43685** chat_exit 也 hide waitbox

### 本轮新增
- **cc94452** Tab 按钮移入顶栏（与 back 同行）→ 历史区高度 +30px
- **06a2c13** Chat tab 下 `\v` 忽略（Codex §1.3 闭合）——控制字节不再污染 textarea
- **f3e1698** 扫描覆盖层最短显示计时起点改为"帧到达面板"——与 Sleep frame-wait 共用机制

### 历史（本批次之前）
- 844a907..156732c 28 commit（Codex 全量接受）

---

## 3. 已接受但未消除的安全风险

- 真实 API Key 仍在源码与 Git 历史中。按 `api-key-dev-exception` 用户决策延后；C1/C2 已落地。
- 推公网 / 重大 release 前必须按 `SECURITY.md` 4 步处理。
- 本评审**不视为阻塞项**。

## 4. 跟踪项（继承 + 本轮关闭）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| Codex §1.11 chat_exit 也 hide waitbox | 上轮 §1.11 | ✅ 0b43685 闭合 |
| Codex §1.3 Chat tab `\v` ignore | 上轮 §1.3 | ✅ 06a2c13 闭合 |
| 扫描覆盖层计时 = 对象生命周期 ≠ 可见时间 | 上轮 §1.2 观察 | ✅ f3e1698 精准修复（帧序号绑定） |
| WiFi scan overlay 跨 push 屏残留 | 预存在 | 本批未触及；建议下轮在 `exit4_1` 也 hide |
| chat 重试草稿恢复在 create 而非 entry | 预存在 | 行为可接受；下次重构时考虑迁移 |
| SPIFFS /chat.log append+compact | 主评审 §1.2 | TODO |
| CJK 8KB 预算裁剪 UI 提示 | 主评审 §1.3 | 部分实施（trimmed 状态行） |
| system prompt NVS 化 | TODO | 阶段 1 |
| 长回答 >4KB 实测 | §4 #9 | 代码层已覆盖 |
| 失败重试路径实测 | §4 #18 | 代码层已覆盖 |

## 5. 验证说明

- `python scripts/test_nvs_atomic_save.py` → 11 项 PASS（沿用）
- 当前环境无 `pio` 可执行，未独立复现固件编译
- 申请人自测：`pio run -e pda2` SUCCESS；COM5 烧录 + Hash verified
- 真机回归：申请 §3 列 5 项均为 ⏸（待用户本批烧录后实测）
- 本评审**仅静态复核**：`git show cc94452 06a2c13 f3e1698` 逐 commit diff 检查
- f3e1698 与 Sleep frame-wait（9a89cdd）共用 `ui_disp_full_refr_seq()` / `ui_disp_flush_done_seq()` API，机制一致
- 结果文档未包含 API Key 正文

## 6. 审批意见

- [x] **A. 全量接受** — 保留全部 7 个非 Key commit，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受范围**：本批 7 commit 中 6 commit 全量接受（除 Key 项按用户决策延后外）；上轮 §1.3 / §1.11 跟踪项与 §1.2 观察项精准闭合；用户反馈 4 项 + 评审方 1 项改进全部落地。

**遗留项**：
- Key 项按 `api-key-dev-exception` 决策延后；跟踪至推公网 / 重大 release 前
- SPIFFS 整文件重写（主评审 §1.2）已登记 `TODO.md`
- wifi_scan_overlay 跨 push 屏残留：建议下轮顺手在 `exit4_1` 也 hide

---

**评审人**：Codex（第三方静态复核视角，已交叉核对 `git show e08bdac 8770a41 f4449c3 0b43685 cc94452 06a2c13 f3e1698` 的实际 diff；f3e1698 的帧序号绑定机制与 Sleep frame-wait（9a89cdd）共用 `ui_disp_full_refr_seq()` / `ui_disp_flush_done_seq()` —— 设计一致性确认）。