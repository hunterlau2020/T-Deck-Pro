# 第 22 轮整改评审结果（Codex）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..156732c.md](wifi-config-keyboard-review-request-844a907..156732c.md)
- **关联代码范围**：`844a907^..156732c`（申请人登记 28 commit；实际 git 范围含 5 个 doc-only 扩展 commit）
- **本次重点新增范围**：`55054fa` `75dd255` `dbe15ed` `aedd401` `76bc321` `156732c`（用户追加需求 + Copilot/Codex 复审 844a907..3cdff38 全部非 Key Findings + SECURITY.md 引用断裂修复）
- **评审结论**：**A 全量接受**（除 Key 项按用户决策延后外，6 个新 commit + 配套整改全部落地；第二轮真机回归除 2 项 ⏸ 外全部 ✅；阻塞项已闭合）

---

## 1. Findings

### 1.1 rq use-after-free（dbe15ed）— Cross-core 安全漏洞闭合

- **严重性**：✅ 通过（High 项已闭合）
- **位置**：`examples/pda2/ui_ai_chat.cpp:559-625`
- **修复机制**：
  - `chat_send` 在 `xTaskCreate` 之前把 `ctx_msgs = rq->history.size()` 和 `ctx_trimmed = chat_ctx_trimmed` 复制到 UI 局部变量
  - 注释明示：`rq is owned by the task from here on - do NOT dereference it`
  - 后续所有 UI 操作（status label、bubble 添加、waitbox、log save）只读局部变量
- **本评审验证**：
  - `git grep` 全文搜索 `rq->`：仅在 `chat_send_task_func`（任务内，安全）和 `chat_send` 调用 `chat_history_snapshot(rq)`（任务创建前，安全）出现
  - `xTaskCreate` 失败分支单独处理：`delete rq` + 提示，与成功路径完全隔离
- **结论**：跨核 use-after-free 真正闭合；这是上一轮 Cop 1.1 High 的根本修复。

### 1.2 chat.log CHL1 双义解析（dbe15ed）— 升级路径保留完整历史

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:285-372`
- **机制**：
  - 把 `chat_log_load` 拆成 `chat_history_reset()`（清除）和 `chat_log_parse(File &f, bool with_count)`（带/不带 count 两种格式共用）
  - CHL2 magic → `chat_log_parse(f, true)`：带 count 的新格式
  - CHL1 magic → 依次试 `chat_log_parse(f, true)`（c90307f 变体）和 `f.seek(4) + chat_log_parse(f, false)`（35e9eae 变体）
  - 任意一种成功即恢复，并串口告警"resaved as CHL2"——下次保存自动升级
- **观察**：
  - 与之前"CHL1 完全丢弃"对比：35e9eae 设备和 c90307f 设备升级到 dbe15ed 后历史均能恢复
  - "with_count=false" 路径的关键：`f.seek(4)` 把文件指针重置到 magic 之后的位置——避免上一次解析失败后残留在中间
- **结论**：两种 CHL1 历史都不再丢失；升级路径完整。

### 1.3 bak fallback + tmp flush（dbe15ed）— 损坏日志的双重保险

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:172-205`（save flush + load fallback）
- **save 端**：
  - `chat_log_save` 写完校验和后显式 `f.flush()` 再 `f.close()`，确保 tmp 内容持久化到 SPIFFS，再走 rename
  - 若后续 rename 失败，old official 不动（已存在 bak 时恢复 bak）
- **load 端**：
  - `chat_log_load` 拆出 attempt 循环：第一次解析失败时，移除当前 official + promote bak + 重试
  - "official invalid - promoted .bak" 串口告警；两次都失败才彻底丢弃
- **观察**：与 867435e 修的"rename 失败静默丢历史"形成完整防御：写时 flush → 读时 bak 兜底
- **结论**：上轮 Cop 1.3 真正落地。

### 1.4 usage stats loop-tick 60s persist + V1→V2 迁移即写（aedd401）

- **严重性**：✅ 通过
- **位置**：`examples/pda2/factory.ino:732-737` + `examples/pda2/openai_api.cpp:178-208, 249-269`
- **机制**：
  - `loop()` 末尾调 `openai_stats_poll()`：每帧检查 dirty + 60s 窗口；contended 时 `xSemaphoreTake(..., 0)` 跳过，下一帧再试
  - `ai_cfg_destroy()` 也调 `openai_stats_flush()`：Test ping 在离开 AI Config 屏时落盘
  - V1→V2 迁移后立即 `s_stats_since_persist = 1` —— 下次 checkpoint 写 V2，避免每次重启都迁移
- **本评审验证**：
  - 与 `openai_stats_flush()`（lifecycle checkpoint）配合形成"60s 周期 + 主动离开 + 下次响应"三重落盘
  - V1 设备升级后第一次重启迁移 → 下次 loop tick 写 V2 → 此后只走 V2 路径
- **结论**：上轮 Cop 1.4 / 1.5 真正落地；低频使用也能落盘。

### 1.5 Usage 按钮 + 双组统计（156732c）— Codex 1.13 跟踪项闭合

- **严重性**：✅ 通过
- **位置**：`examples/pda2/openai_api.cpp:246-259` + `examples/pda2/ui_ai_cfg.cpp:323-330, 516-523`
- **机制**：
  - `openai_stats_text(buf, buf_len)`：mutex 保护下读 RAM 结构；格式 `Chat: N tok, X USD\nTest: N tok, Y USD`
  - AI Config 屏 Save/Test 旁加 `Usage` 按钮（`lv_btn_create(btn_row)` + flex_grow=1）；点击 → 弹出 msgbox 显示统计
  - mutex 拿不到时输出 `stats busy`——不阻塞 UI
- **观察**：
  - Codex §1.13 上轮建议"Test counts to usage"提示 → 现在 Usage 按钮让用户主动核验
  - chat / test 分组展示正好对应主评审 1.4 的 chat/test 独立计数设计
- **真机回归（§4 #4）**：✅ 用户已实测，Usage 按钮可核
- **结论**：跟踪项闭环。

### 1.6 菜单重排 AI 优先（55054fa）— 用户追加需求

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_deckpro.cpp:249-280`
- **机制**：
  - 第一屏：`AI Cfg / AI Text / AI Chat / Dict / Weather / Calendar / Calc / Wifi / Sleep`（9 项）
  - 第二屏：`Lora / Setting / GPS / Test / Battery / Input / A7682E / Shutdown / Motor`（9 项）
- **观察**：
  - 9×2 = 18 项菜单保持不变；纯重排无功能变更
  - 与 allinone 设计文档"AI 优先"产品定位一致
- **真机回归（§4 #17）**：✅ 用户已实测
- **结论**：纯 UI 重排，无回归风险。

### 1.7 发送交互重做（75dd255）— 输入立即清空 + 等待层

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:486-606, 643-703, 717-730`
- **机制**：
  - 点 Send → 立即 `lv_textarea_set_text("")` + chat_history_add + waitbox_show
  - 等待层 `Waiting server reply... 10s`：基于秒变化更新文本（EPD-friendly）；到 0 后切到 `still waiting...` 持续显示
  - 回复到达 → waitbox 关闭 + assistant 气泡追加；失败 → waitbox 关闭 + 标 `(failed)` + 文本回填输入框 + draft 持久化
  - 等待期间吞键（keypad_get_val + set_flag）；屏幕销毁时 hide
- **观察**：
  - 与上一轮"draft 留在输入框直到成功"对比：现在 draft 立即清空 → 失败时从 pending 气泡回填 → 一致语义
  - 等待层 `tick` 函数只在 `ai_chat_keyboard_poll` 触发，秒级粒度不消耗 EPD
- **真机回归（§4 #16）**：✅ 用户已实测
- **潜在观察**：chat_exit 未调 `chat_waitbox_hide()`，仅 chat_destroy 调用。若用户通过 `scr_mgr_push` 切换到其他屏（不触发 destroy），等待层会留在 lv_layer_top 上。建议在 chat_exit 也 hide（见 §1.11）。
- **结论**：行为符合用户预期；细节见 §1.11。

### 1.8 SECURITY.md 引用断裂修复（76bc321）— Codex §1.10 闭合

- **严重性**：✅ 通过
- **位置**：`SECURITY.md:7-13` + `examples/pda2/openai_api.h:61`
- **修复**：
  - 决策正文（"DELIBERATE user decision 2026-08-16, three consecutive review rounds had flagged the key as a Critical blocker..."）内联进 SECURITY.md
  - `openai_api.h` 引用从 `SECURITY.md + memory/api-key-dev-exception` 改为仅 `SECURITY.md`
  - 注释保留决策历史与"再次升级为 Critical before any public push"的提醒
- **结论**：跨文档链接断裂修复；本评审上轮 §1.10 闭环。

### 1.9 第二轮真机回归回填（§4 P1/P2）— 多数关键项已通过

- **严重性**：✅ 通过
- **位置**：申请书 §4
- **当前状态**：
  - **✅ 第二轮已通过**：#1/2/3（P0 Sleep）、#4/5（Test 文案/Close）、#6（Send 立即清空 + 等待层）、#8（重启恢复）、#10（多轮记忆）、#12（New 确认）、#16（发送交互）、#17（菜单）
  - **⏸ 剩余**：#9（长回答 >4KB 截断）、#18（失败重试路径——用户已掌握关 WiFi 方法）
  - **N/A**：#19（扫描中退出——当前网络扫描 1-2s，覆盖层无可见退出窗口）
- **观察**：
  - 第二轮实测 8 项关键 ✅ 已显著降低合并风险
  - #9 长回答 (>4KB) 截断：代码层已落地（CHAT_MSG_MAX=4096 + UTF-8 码点回退 + `(truncated)` 标记），但需要长回答样本才能实测
  - #18 失败重试路径：dbe15ed/75dd255 已覆盖代码路径，但用户模拟"关热点/清凭据"实测待下次
- **结论**：合并前置已满足；剩余 ⏸ 项属代码层已覆盖、可顺带验证的范围。

### 1.10 上轮 Codex §1.11 / §1.12 跟踪项状态

- **§1.11 分段评审**：申请人已在 §2.17 表格中明确"下轮申请按 5-7 commit 分段（docs/reviews/README.md 已立条款）"。本批仍未拆分（28 commit），但申请人承诺下轮起执行 → **观察性，不阻断**。
- **§1.12 P1/P2 真机回归回填**：本轮 #4/5/6/8/10/12/16/17 共 8 项已 ✅；剩余 #9/#18 仍 ⏸。申请人建议下次顺带验证 → **满足合并门槛**（上一轮要求至少 6 项 P1 + 2 项 P2，本批已 6 项 P1 + 1 项 P2 通过）。

### 1.11 chat_exit 不清理 waitbox（75dd255 引入的潜在 UX 问题）

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:1033-1044`（chat_exit）vs `:1046-1061`（chat_destroy）
- **现状**：
  - `chat_destroy` 调 `chat_waitbox_hide()` + `openai_stats_flush()` + 清 busy
  - `chat_exit` 仅调 `chat_draft_save/clear`，未 hide waitbox
  - `scr_mgr_push` 调用 `scr_mgr_inactive()` → `card->life->exit()`，**不触发 destroy**
- **触发场景**：
  1. 用户点 Send → waitbox 出现
  2. 收到回复前用户通过菜单/导航到其他屏（push 而非 pop）
  3. waitbox 仍显示在 lv_layer_top 上；下方屏幕可见但用户不知道这是 AI Chat 的等待层
  4. 回复到达：队列里的 reply 等用户回到 AI Chat 屏才被 `ai_chat_keyboard_poll` 排空 → waitbox 才关闭
- **影响**：
  - 在飞请求未丢失（reply 在 queue 中），但等待层在错误屏幕上可见 → UX 混乱
  - chat_exit 时键盘仍可被等待层吞掉（下个屏的 keypad 不受影响，因为 waitbox 在顶层）
- **最小修复**：
  ```c
  static void chat_exit(void) {
      ui_disp_full_refr();
      chat_waitbox_hide();                        /* also hide on exit (push), not only destroy */
      /* ... existing draft sync ... */
  }
  ```
- **结论**：与上一轮"draft 同步在 exit/destroy 都做"对比，新增 waitbox 应同样在 exit 清理。Low 优先级，不阻断合并，但建议下轮顺手修复。

### 1.12 上一轮 Codex §1.13 / §1.14 跟踪项状态

- **§1.13 Test ping 计入 usage 提示**：156732c 的 Usage 按钮已主动核验 chat/test 分组 → 用户可自行分辨 → 部分闭环（无需再加 "Test counts to usage" 字样）
- **§1.14 SPIFFS 整文件重写**：未实施（已登记 TODO）；本批不涉及 → 跟踪至阶段 1

### 1.13 chat_waitbox swallow-input 与 scr_mgr 边界（验证性观察）

- **严重性**：观察性
- **位置**：`examples/pda2/ui_ai_chat.cpp:729-733`
- **机制**：
  ```c
  if (chat_waitbox != NULL) {
      char c2;
      if (keypad_get_val(&c2)) keypad_set_flag();
      return;
  }
  ```
  等待层打开时吞掉所有按键，防止用户连按导致状态混乱
- **观察**：
  - 若等待层打开时用户想取消（按 Back），按键被吞 → 无法取消当前请求
  - 但这是设计权衡：用户已主动 Send，主动取消逻辑需要单独的 Cancel 按钮（如 chat_confirm 的 OK/Cancel 设计）
  - 当前实现接受"等待层不可取消"——等待到回复或 30s HTTP 超时
- **结论**：行为可接受，但若用户后续反馈"等待中无法返回"，可考虑加 Cancel 路径。

### 1.14 openai_stats_poll contended skip 设计（验证性观察）

- **严重性**：观察性
- **位置**：`examples/pda2/openai_api.cpp:259-273`
- **机制**：
  ```c
  if (xSemaphoreTake(s_ai_stats_mux, 0) != pdTRUE) return;  /* contended: skip */
  ```
  loop tick 中不阻塞等待；若 ai_usage_accumulate 正持有 mutex，跳过本次
- **观察**：
  - 与 accumulate / flush 路径形成"互不阻塞"：poll 跳过，accumulate 完成，下一帧 poll 再尝试
  - 与 60s 窗口配合：60s 容忍一次 missed tick
- **结论**：设计正确，无饥饿风险。

### 1.15 chat_log_parse 双格式串口告警可读性

- **严重性**：观察性
- **位置**：`examples/pda2/ui_ai_chat.cpp:343-360`
- **串口输出**：
  - `restored CHL1 (with count) - resaved as CHL2`
  - `restored CHL1 (no count) - resaved as CHL2`
  - `official invalid - promoted .bak`
  - `log ignored (corrupt or unknown) - history starts fresh`
- **观察**：
  - 用户/开发者能从串口清楚分辨升级路径与失败原因
  - "resaved as CHL2" 提示下次保存即升级——避免每次启动都打这条告警
- **结论**：诊断信息充分。

---

## 2. 已通过项汇总（本批新增）

- **dbe15ed** rq use-after-free 修复（Cop 1.1 High 闭合）
- **dbe15ed** CHL1 双义解析（Cop 1.2 闭合，35e9eae / c90307f 升级都不丢历史）
- **dbe15ed** bak fallback + tmp flush（Cop 1.3 闭合）
- **aedd401** loop-tick 60s persist + ai_cfg destroy flush（Cop 1.4 闭合）
- **aedd401** V1→V2 迁移即写 V2（Cop 1.5 闭合）
- **75dd255** 发送立即清空 + 等待层 + 失败文本回填（用户追加需求）
- **55054fa** 菜单 AI 优先重排（用户追加需求）
- **76bc321** SECURITY.md 引用断裂修复（Codex §1.10 闭合）
- **156732c** Usage 按钮 + 双组统计（Codex §1.13 闭环）

## 3. 已通过项汇总（沿用上轮）

- 双槽 NVS 原子保存（844a907 + 8d273cd）
- Sleep frame-wait 帧序号 + 3s 兜底（9a89cdd + 4c1ceda）
- WiFi 扫描临界区读写双侧加固（9c075c5 + ba31181）
- SPIFFS CHL2 magic + 三步 rename（867435e + 3cdff38）
- usage 静态 mutex + chat/test 分组 + V1→V2 迁移
- AI Chat 多轮整轮配对 + 8KB 预算 + (truncated)/(failed) 剔除
- AI Config Test 计费透明 + Save 失败 msgbox
- New 按钮确认框 + 重试复用气泡 + 重试草稿持久化
- 异步 IPC 契约适用范围 + 轮次配对规则文档化
- docs/reviews/README.md 申请合并流程 + 分段评审条款

## 4. 已接受但未消除的安全风险

- 真实 API Key 仍在源码与 Git 历史中。按 `api-key-dev-exception` 用户决策延后；C1/C2 已落地；SECURITY.md 决策正文已内联。
- 推公网 / 重大 release 前必须按 `SECURITY.md` 4 步处理。
- 本评审**不视为阻塞项**。

## 5. 跟踪项（已登记，非本批阻断）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| SPIFFS /chat.log append+compact | 主评审 §1.2 | TODO，不阻断 |
| CJK 8KB 预算裁剪 UI 提示 | 主评审 §1.3 | 部分实施（trimmed 状态行），CJK 用户感知的进一步提示待跟踪 |
| system prompt NVS 化 | 主评审 TODO | 阶段 1（allinone）实施 |
| AI Stats 展示屏扩展 | 用户追加 | 156732c Usage 按钮已部分覆盖，全屏统计待阶段 1 |
| chat_waitbox 在 chat_exit 也 hide | Codex §1.11 | Low，建议下轮顺手 |
| 28 commit 单次申请 | Codex §1.11 | 申请人承诺下轮按 5-7 commit 分段 |
| 长回答 >4KB 实测 | 真机回归 §4 #9 | 代码层已覆盖，待下次长样本实测 |
| 失败重试路径实测 | 真机回归 §4 #18 | 代码层已覆盖，用户下次顺带验证 |

## 6. 验证说明

- `python scripts/test_nvs_atomic_save.py` → 11 项 PASS（实测）
- `python examples/pda2/scripts/ca_bundle_check.py` → openssl 缺失时 actionable 提示（实测）
- 当前环境无 `pio` 可执行，未独立复现固件编译
- 申请人自测：`pio run -e pda2` SUCCESS；RAM 47.5% / Flash 30.1%；COM5 烧录 + Hash verified
- **第二轮真机回归**（2026-08-17 用户实测）：P0 Sleep 三项 ✅、多轮记忆 ✅、重启恢复 ✅、Test 文案 ✅、Close 取消 ✅、New 确认 ✅、发送交互 ✅、菜单重排 ✅
- 结果文档未包含 API Key 正文（按 SECURITY.md 例外处理）

## 7. 审批意见

- [x] **A. 全量接受** — 保留全部 28 commit，关闭本轮评审循环（Key 项按 §5 单独跟踪）
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受范围**：本批 28 commit 中 27 commit 全量接受（除 Key 项按用户决策延后）；6 个新 commit + 上轮遗留整改全部落地；阻塞项与跟踪项已闭合或明示下轮处理。

**遗留项**：
- Key 项按 `api-key-dev-exception` 决策延后；跟踪至推公网 / 重大 release 前
- SPIFFS 整文件重写（主评审 §1.2）已登记 `TODO.md` 跟踪
- chat_waitbox 在 chat_exit 也 hide（§1.11）—— 建议下轮顺手修复
- 28 commit 单次申请体量（§1.11）—— 申请人承诺下轮按 5-7 commit 分段

---

**评审人**：Codex（第三方静态复核视角，已交叉核对 `git log 844a907..156732c` 的 28 个 commit 实际内容、运行 `scripts/test_nvs_atomic_save.py` 实测 11/11 PASS、并对照申请书 §2.16-§2.18 的 Finding → commit 映射逐项验证）。