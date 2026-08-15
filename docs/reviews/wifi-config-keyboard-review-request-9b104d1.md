# 评审申请书：AI Text 屏重构 + OpenRouter 请求构造（第十六次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `9b104d1` — `pda2: rework AI Text - multi-line input, Send/Clear buttons, async send`
- **历史文档**（保留不覆盖）：前十五轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户要求重构 AI Text（对话）屏并按 OpenRouter curl 范例构造请求：

| # | 需求 | 实现 |
|---|---|---|
| 1 | 文本输入框改多行，至少容纳 150 字 | 多行 textarea（64px 高，上限 200 字符，约 6 行可见），支持 Shift/Sym 层输入 |
| 2 | 增加"发送""清除"按钮 | **Send**（= 键盘 Enter 语义）/ **Clear**（清空输入框）按钮，`FLOATING` 钉在容器底部（沿用 AI Config 按钮修复的位置确定性方案），点击有串口日志 |
| 3 | 按 curl 范例构造请求 | `openai_chat` 请求体改为：`model`（AI Config）、`temperature: 0.7`、`reasoning: {exclude: true}`、`messages: [system(KET English examiner 提示), user(输入框内容)]`；Authorization Bearer + Content-Type application/json |
| 4 | url/auth/model 来自 AI Config；user content 来自输入框并转义 | 三项配置经 `openai_load_config` 读取（NVS 优先，Base/Model/Key 均有固件默认值）；**转义说明**：请求体是 JSON 而非 URL 编码，`cJSON_AddStringToObject` 自动做 JSON 字符串转义（引号/反斜杠/换行/控制字符），特殊字符不会破坏请求构造 |

**附带**：
- Key 默认值：用户之前看到的 `sk-or-v1-...` 是 NVS 存量值（编辑中被清掉并保存导致 Test 报 "Key empty"）；现按用户意图将 **`AI_KEY_DEFAULT`** 内置为固件默认（NVS 优先）。⚠️ 该 Key 随仓库提交，若仓库公开或 Key 泄露需轮换（代码注释已标注）
- **异步发送**：请求在 FreeRTOS 任务执行（15-30s 不冻结 UI，符合 allinone AI 评审 §1.5），回复经 ready 标志由键盘轮询收取应用；发送期间按键吞掉

## 2. 变更明细（`examples/pda2/`，commit `9b104d1`）

- `openai_api.h`：新增 `AI_SYSTEM_PROMPT`、`AI_KEY_DEFAULT`
- `openai_api.cpp`：`openai_chat` 请求体按 curl 范例扩展（system 消息 + temperature + reasoning.exclude）；`openai_load_config` 的 key 默认值改 `AI_KEY_DEFAULT`
- `ui_ai_chat.cpp`：多行输入框；Send/Clear 按钮（FLOATING 底部钉住 + move_foreground）；`chat_send` 拆为任务 + `chat_apply_reply`；轮询收取结果、发送中吞键；移除 hint 行（按钮自解释）

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 多行输入 ≥150 字 | ⏸ 待测 | 输入框可换行显示长文本 |
| Send/Clear 按钮 | ⏸ 待测 | 串口 `[AIChat] Send/Clear button clicked` |
| 请求构造与回复 | ⏸ 待测 | 配好 WiFi 后发送，应见 `[AIChat] reply len=N` + 分页回答 |
| Test 不再报 Key empty | ⏸ 待测 | Key 默认值生效 |

## 4. 回滚方案

```bash
git revert 9b104d1
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
