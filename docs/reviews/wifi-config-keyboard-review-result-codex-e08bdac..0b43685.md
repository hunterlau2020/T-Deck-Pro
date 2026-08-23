# 第 23 轮整改评审结果（Codex）— 用户反馈修复

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..0b43685.md](wifi-config-keyboard-review-request-e08bdac..0b43685.md)
- **关联代码范围**：`e08bdac..0b43685`（申请人登记 4 个非 Key commit；含 1 个 doc-only + 2 个 reviews 注册 commit）
- **本次重点整改范围**：用户反馈的 4 项 UX 改进 + 1 项 Codex 跟踪项顺手修复
- **评审结论**：**C 部分接受**（3 个 commit 接受；`f4449c3` 双 Tab 布局发现 `\v` 在 Chat tab 落入 "switch + type char" 兜底分支——与 commit message 不符，建议加一行 ignore 后再接受）

---

## 1. Findings

### 1.1 Usage 弹窗完整明细（e08bdac）— 6 行 breakdown 完整展示

- **严重性**：✅ 通过
- **位置**：`examples/pda2/openai_api.cpp:255-275` + `examples/pda2/ui_ai_cfg.cpp:106, 326-329`
- **机制**：
  - `openai_stats_text()` 新格式（mutex 保护读 RAM 结构）：
    ```
    Chat: <tot_tok>
      cached <cached>, write <cwrite>
      audio <audio>, rsn <reasoning>
      cost <cost> USD
    Test: <t_tot_tok>, <t_cost> USD
    ```
  - `ai_msgbox_show()` 增加 `int height = 160` 默认参数；Usage 路径传 205 容纳 6 行
  - 调用点 buffer 升至 192 字节
- **观察**：
  - 默认参数 `int height = 160` 是 C++ 合法写法；若未来被改写为 `.c` 文件需手动展开
  - breakdown 完整对应 `ai_stats_t` 的所有 chat 字段——cached / cwrite / audio / reasoning 都用户可读
  - 用户原意是"显示 cached_tokens"——本次实现连同 cache_write_tokens / audio_tokens / reasoning_tokens 都展示，比原要求更充分
- **结论**：实现完整；不阻塞合并。

### 1.2 WiFi 扫描覆盖层最短显示 800ms（8770a41）— 解决"扫描太快看不见提示"

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_deckpro.cpp:2402-2416`
- **机制**：
  - 新增 `#define WIFI_SCAN_OVL_MIN_MS 800`
  - `wifi_scan_overlay_update()` 增加 `millis() - wifi_scan_ovl_t0 >= WIFI_SCAN_OVL_MIN_MS` 守卫
  - 扫描结束/失败后，覆盖层继续显示至满 800ms 再隐藏
- **观察**：
  - `wifi_scan_ovl_t0` 在 `wifi_scan_overlay_show()` 设置，是覆盖层创建时刻；`millis() - t0` 反映"覆盖层已存在多久"
  - 与原 10s 超时逻辑（`WIFI_SCAN_OVL_TIMEOUT_MS`）兼容：扫描慢时仍走 10s 兜底 → 中途超时调用 `wifi_cfg_scan_abort()`
  - 若用户快速扫描（<1s）后立刻导航离开 WiFi Cfg，覆盖层仍在 lv_layer_top 上（**预存在**，非本 commit 引入）
- **结论**：解决"1-2s 扫描看不见提示"的核心问题；不阻塞合并。

### 1.3 AI Text 双 Tab 布局（f4449c3）— 设计方向正确但 Chat tab 的 `\v` 处理与 spec 不符

- **严重性**：**Medium**（行为偏差）
- **位置**：`examples/pda2/ui_ai_chat.cpp:546-578, 800-840, 967-1100`
- **正向观察**：
  - **布局**：container 232×288（↑14px），tab 条 30px + 状态行 + flex_grow 双页面
  - **Chat tab**：历史占满全屏；`+`/`-` 滚屏；`\b` 退回菜单；`\n` 跳 Input；任意可见字符 → 跳 Input 并追加
  - **Input tab**：176×full textarea（max 200 字符）+ 48×flex_grow Send/Clear/New 三按钮
  - **Send 后自动跳回 Chat tab**（`chat_set_tab(false)` 在 `chat_waitbox_show` 之后）
  - **重试草稿恢复**时 `chat_set_tab(draft.length() > 0)`：Input tab 自动打开——良好的草稿 UX
  - **Tab 切换**：触摸按钮（`chat_chat_tab_cb` / `chat_input_tab_cb`）和键盘 `\t`（Alt+Enter）双通道
  - **Input tab 下 `\v`（volume）= New chat confirm**：替代旧 Alt+Enter 路径
  - **空框 Backspace 回 Chat tab**：`if (txt[0] == '\0') chat_set_tab(false)`
- **⚠️ Bug：Chat tab 下 `\v` 落入 "else" 分支**

  Chat tab 兜底分支：
  ```c
  } else {
      /* typing jumps to the input page and appends the character */
      chat_set_tab(true);
      lv_textarea_add_char(chat_input_ta, c);   // <-- 捕获了 \v
  }
  ```
  
  Commit message 明示：`'\v' (volume): New chat confirm`（Input tab only）。但 Chat tab 下没有 `\v` 的专门 ignore 分支，导致：
  
  1. 用户在 Chat tab 按 volume key → `chat_set_tab(true)` 切到 Input tab
  2. 同时 `lv_textarea_add_char(chat_input_ta, '\v')` —— `\v`（ASCII 0x0B）作为单字节进入 textarea buffer
  3. 视觉上无变化（控制字符不可见），但 buffer 已污染
  4. 若用户随后 Send，prompt 含 `\v` 字节被发送给 OpenAI（无害但 in-band）
  
  对比旧代码：
  ```c
  // 旧 ui_ai_chat.cpp
  if (c == '\v') return;                      /* volume key */
  ```
  旧版本明确忽略 `\v`。

  **影响**：
  - 用户预期 volume key 在 Chat tab 是 no-op（沿用旧习惯）
  - 实际触发 tab 切换 + 不可见字节入 buffer
  - 与 commit message "'\v' (volume): New chat confirm（Input tab only）" 不一致
  
  **最小修复**：
  ```c
  if (!chat_tab_input) {
      /* --- Chat tab --- */
      if (c == '+' || c == '-') { ... }
      else if (c == '\b') { ... }
      else if (c == '\n') { ... }
      else if (c == '\v') {
          /* volume key: no-op on the Chat tab (New confirm is Input-tab only) */
          return;
      }
      else {
          chat_set_tab(true);
          lv_textarea_add_char(chat_input_ta, c);
      }
  }
  ```
  
  实质只是把 `\v` 的 ignore 移到 Chat tab 分支顶部（保持旧行为）。

- **观察（次要）**：
  - 双 Tab 隐藏通过 `LV_OBJ_FLAG_HIDDEN` 切换；create 后 chat_set_tab 在末尾调用，单帧内可能短暂双页面可见——用户不可见
  - 重试草稿恢复逻辑仍在 `chat_create` 而非 `chat_entry`：若屏幕未被 destroy（push 而非 pop），草稿恢复逻辑不重跑——这是**预存在**行为，本批不涉及
  - Status line 在 tab_row 下方、独立于两个 page，无论哪个 tab 都可见——设计正确

- **结论**：建议加 1 行 `\v` ignore 后接受；其他设计正确。

### 1.4 等待层离屏清理（0b43685）— Codex §1.11 顺手修复

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:1126-1127`
- **修复**：
  ```c
  static void chat_exit(void) {
      ui_disp_full_refr();
      chat_waitbox_hide();                        /* push-away leaves no waitbox on
                                                   * other screens (codex 1.11) */
      /* ... existing draft sync ... */
  }
  ```
- **观察**：
  - `chat_destroy` 已有 `chat_waitbox_hide()`；现在 `chat_exit` 同步调用 → push 与 pop 都清理
  - 本评审上一轮 §1.11 跟踪项闭合
  - 注意：隐藏 waitbox 不取消在飞请求——reply 仍在 `s_chat_q`，用户回到 AI Chat 后由 `ai_chat_keyboard_poll` 消费（gen 校验仍生效）
- **结论**：单行精准修复；不阻塞合并。

---

## 2. 已通过项汇总

- **e08bdac** Usage 弹窗完整明细（cached / cwrite / audio / rsn 全展示 + 205px 高度变体）
- **8770a41** WiFi 扫描覆盖层最短显示 800ms
- **f4449c3** AI Text 双 Tab 布局（**待 §1.3 修复后接受**）
- **0b43685** chat_exit 也 hide waitbox（Codex §1.11 闭合）

## 3. 跟踪项（已登记或继承）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| WiFi scan overlay 跨 push 屏残留 | 预存在（类似 waitbox 修复前） | 本批不涉及；建议下轮顺手在 `exit4_1` 也 hide |
| chat 重试草稿恢复在 create 而非 entry | 预存在 | 行为可接受；下次重构时考虑迁移 |
| SPIFFS /chat.log append+compact | 主评审 §1.2 | TODO |
| CJK 8KB 预算裁剪 UI 提示 | 主评审 §1.3 | 部分实施 |
| system prompt NVS 化 | TODO | 阶段 1 |
| 长回答 >4KB 实测 | §4 #9 | 代码层已覆盖 |
| 失败重试路径实测 | §4 #18 | 代码层已覆盖 |

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → 11 项 PASS（沿用，本批未触及）
- 当前环境无 `pio` 可执行，未独立复现固件编译
- 申请人自测：`pio run -e pda2` SUCCESS；COM5 烧录 + Hash verified
- 真机回归：申请 §3 列 5 项均为 ⏸（待用户本批烧录后实测）
- 本评审**仅静态复核**：`git show e08bdac 8770a41 f4449c3 0b43685` 逐 commit diff 检查
- 结果文档未包含 API Key 正文

## 5. 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

**接受范围**：
- ✅ `e08bdac` Usage 弹窗完整明细
- ✅ `8770a41` WiFi 扫描 800ms 最短显示
- ⏸ `f4449c3` 双 Tab 布局（**待 §1.3 加一行 `\v` ignore 后可接受**）
- ✅ `0b43685` chat_exit 也 hide waitbox

**前置条件（合并到 master 前必须满足）**：
- §1.3 在 `f4449c3` 加 1 行 `else if (c == '\v') { return; }`（Chat tab 顶部）——或 `else if (c == '\v')` 走 else-return

**遗留项**：
- Key 项按 `api-key-dev-exception` 决策延后
- SPIFFS 整文件重写（主评审 §1.2）已登记 `TODO.md`
- wifi_scan_overlay 跨 push 屏残留：建议下轮顺手在 `exit4_1` 也 hide

---

**评审人**：Codex（第三方静态复核视角，已交叉核对 `git show e08bdac 8770a41 f4449c3 0b43685` 的实际 diff，并对 `f4449c3` 的 keyboard poll 分支做了 `'\v'` 行为分析——发现 Chat tab 下 `\v` 落入 "switch tab + type char" 兜底，与 commit message 不一致）。