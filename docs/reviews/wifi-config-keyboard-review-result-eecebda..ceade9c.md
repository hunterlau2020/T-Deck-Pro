# AI Text 聊天界面 + 第 17/18 轮评审整改评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-eecebda..ceade9c.md](wifi-config-keyboard-review-request-eecebda..ceade9c.md)
- **关联 commit**（10 个，含 1 个 docs 提交）：`eecebda` `2875efb` `bb1819b` `6da7fe2` `84090c1` `fb2ac62` `3bc255f` `d21bd18` `e210b46` `ceade9c`
- **评审依据**：
  - [主审 Sleep 屏](wifi-config-keyboard-review-result-bcca4d2.md) + Copilot 复审
  - [主审 第 8-16 轮整改](wifi-config-keyboard-review-result-01f8eac..8b96656.md) + Copilot 复审
- **评审结论**：**部分接受**（Part 1 接受、Part 2 大部分接受、Critical API Key 仍遗留）

---

## 0. 总判定

合并申请整体质量较高：

- **Part 1（`eecebda` AI Text 聊天界面重构）**：UI 思路清晰，符合现代对话 UX，自动 UTF-8 换行 / 代次校验 / 草稿保留三件套均到位。但聊天历史**纯内存态**是显著产品体验短板，本次接受但要求下一轮添加 NVS / SPIFFS 持久化。
- **Part 2（第 17/18 轮整改 8 commit）**：上轮评审的 High 项（异步 IPC 边界、Sleep 倒计时、Sleep 竞态、Test 端点 fallback）落实到位；Critical Key 项按用户决策继续延后，但 `openai_api.h:36` 的真实 Key 字符串依然可见——下次评审必查。

---

## 1. Findings

### 1.1 真实 API Key 仍在 `openai_api.h`，按用户决策延后但仍为 Critical

- **严重性**：**Critical**（遗留项）
- **位置**：`examples/pda2/openai_api.h:36` —— `#define AI_KEY_DEFAULT "REDACTED-OPENROUTER-KEY"`
- **证据**：`grep` 复核确认 `examples/pda2/openai_api.h:36` 与 `examples/pda2/openai_api.cpp:18` 均未在本批 10 commit 中修改；申请书 §5 明示"用户决策：暂不修复"。
- **影响**：与前几轮评价一致，本评审不重复列出，**仅作为下次评审的 blocking 项跟踪**。
- **下次必查**：删除 Key 字符串（占位符或留空）、`git filter-repo` 计划或 README 通告、CI `gitleaks` hook。

### 1.2 AI Text 聊天历史纯内存态，重启后整段对话丢失

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:34-47` —— `chat_msg_t { bool from_user; char text[256]; }` `chat_history[CHAT_HIST_MAX=40]`，无 NVS / SPIFFS 写入
- **触发场景**：用户聊了 5 轮有效内容后按 Sleep / 设备掉电 / 升级固件 → 回到 AI Text 屏空白。
- **证据**：
  - `chat_history_add()` 仅写 `chat_history[]` 全局数组，无持久化调用。
  - `chat_history_render()` 仅 `lv_obj_clean(chat_hist_cont)` 重渲染。
  - 申请书 §1.1 明示"**内存态**（重启清空，未持久化——需要可后续加 NVS/文件）"。
- **影响**：
  - "对话"语义暗示跨次会话保留上下文，本设计实际是"易失对话"——与 `AI 对话屏`产品名不符。
  - 与上一轮 `allinone-design-ai-ux-flow-review-result.md §1.7`（对话无 System prompt 配置）共同构成"v1 是单轮问答"的实证。
  - 升级固件后用户看不到之前的上下文，调试 / 复现问题困难。
- **最小修复**：
  1. 在 `openai_api` 或 `ui_ai_chat.cpp` 引入 `chat_log_t` 持久化：
     - 轻量：每次写入 `Preferences` namespace `chat_log`，保存最近 N 条（含 from_user / text / 时间戳）。
     - 中等：写入 SPIFFS 文件 `/chat.log`，循环覆盖 16KB 上限。
  2. 启动时按上限（默认 40 条）回放到 `chat_history[]`。
  3. UI 提供 "Clear history" 入口（与 Send/Clear 同行第三按钮或长按）。
  4. 申请书写明计划：在下次 AI Config 屏重构中一并加入。

### 1.3 AI Config 改用 chat-completion 测试，每次 Test 消耗模型 token / 配额

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_cfg.cpp:236-240` —— Test 任务调 `openai_chat("ping", base, model, key, timeout)`
- **证据**：
  - 申请书 §2.5 "Test 改为对草稿 base/model/key 发起**最小 chat-completion**（`openai_chat("ping", ...)`），回复到达即证明三元组可用"。
  - 真实语义：从 [OpenRouter 计费](https://openrouter.ai/docs#pricing) 看，`"ping"` 仍会触发模型推理产生 token 消耗（哪怕 `:max_tokens=1` 也按最少 1 token 计费）。
  - 与上一轮的 `/models?limit=2` 方案相比，**取消 URL 推导风险**是有益，但**计费风险**是新增。
  - Test 期间 prompt = "ping" + system "You are a KET English examer..." + `temperature 0.7` + `reasoning.exclude` —— OpenRouter 会按"输入 token + 输出 token"收费。
- **影响**：
  - 配置 OpenAI 直连的企业用户在每次配错重测时被计费。
  - 用户不知情，可能在反复测错时花掉真金白银。
  - 与"v1 固定 system prompt"耦合：system 越长，每次 Test 输入 token 越多。
- **最小修复**：
  1. 在 AI Config Test 弹窗标题明示 "Test will charge ~1 token against your account"。
  2. 在 `openai_chat("ping", ...)` 调用之前**优先尝试 HTTP HEAD** 或 `GET /models`（如果与 base 同源），仅在失败回退到 chat-completion。
  3. 增加配置项 `use_minimal_test_endpoint`（bool），让用户选择"用 chat-completion 验证最准"还是"用 /models 不计费"。
  4. 在 msgbox 倒计时结束后追加 "（already charged to your account）"。
- **关联 finding**：本项取代上一轮 `01f8eac..8b96656 §1.5`（Test 端点 fallback），但引入了新风险，应作为 §1.5 的"演进版" + 风险叠加。

### 1.4 AI Config Test 输入文本固定为 "ping"，对不同 prompt 模板 / system prompt 不验证

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_cfg.cpp:240`
- **证据**：固定 `"ping"` 不能覆盖模型实际生产场景（如长输入 / 含特殊字符 / 跨语言）。
- **影响**：用户测试通过后正式问问题时仍可能因 system prompt / temperature / reasoning 触发 5xx 或节流。
- **最小修复**：在 msgbox 末尾追加 "Test only verifies network + auth. Model behavior depends on system prompt and temperature."。

### 1.5 Chat 列表 `chat_msg_t` 256 字节截断，长回答 / 长问题被静默截断

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:64` —— `char text[CHAT_MSG_MAX=256]`；`strncpy` 截断
- **证据**：
  - 用户多行输入框上限 200 字符（申请书 §1.1 L55），问题本身不会超。
  - **AI 回复** —— 申请书 L56 说 "回答区 flex_grow（自动换行，分页 8 行/页）"——但 `chat_msg_t.text[256]` 截断任何超过 256 字节的回答。
  - `d21bd18` 加 `(truncated)` 标记，但仅在 `>=255` 字节的字节缓冲中——`chat_msg_t` 截断发生在写入历史这一步，与该 truncation 标记是两套独立机制，关系未明。
- **影响**：
  - 长 AI 回复（常见 500-2000 字符）在用户视野里被硬切；`(truncated)` 标记未必能与 `chat_msg_t` 截断协同。
  - 用户追问"继续/详细说说"时上下文已不完整。
- **最小修复**：
  1. 把 `CHAT_MSG_MAX` 与对话屏 buffer 上限统一为 `CHAT_ANSWER_MAX`（当前 4096B）——尽管增加静态内存约 9×40=360KB 不可接受，需改为 `std::string` 或动态分配指针。
  2. 或者在 `chat_history_add()` 检测 `text >= CHAT_MSG_MAX` 时追加 `…(truncated)`，明确告知 UI "对话气泡只展示首 256 字节，全文可滚动回答区"。
  3. 至少把 `(truncated)` 策略与 `d21bd18` 的 truncation 用同一份代码产出，避免两套。

### 1.6 Chat UI 总高度分配：164px + 16px + 44px = 与 240×320 EPD 不符

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:228-230` —— `lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 32)`
- **证据**：
  - 申请书 §1.1 wireframe 写"历史 164px + 状态行 16px + 输入 44px + 侧栏小按钮"，合计 224px。
  - 实际 `lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 32)` 中 `cont` 应为整个 chat 屏容器——32px 是预留 back 按钮位置，但 chat 输入与状态行的实际像素分配未在 commit 中说明。
  - 申请书说"约 8 行 ASCII 容纳" vs 第 8-16 轮 wireframe 说"约 4 行 ASCII ×30 列 ≈ 120 字符"，**两轮 wireframe 估算不一致**（基数 30 列 vs 实际屏 240px ÷ 14pt ≈ 17 列 ASCII）。
- **影响**：
  - 在 240×320 EPD 上运行 chat UI，可能输入框与历史气泡相互挤压。
  - 用户实测时可能发现"看不到历史最后几条 + 输入框只显示 1 行"。
- **最小修复**：
  1. 申请书补充实际布局计算：240px 屏宽 / 320px 屏高 + 各组件 px 分配（特别是按钮行 44px 占用比例）。
  2. 把 `240 / 14pt 字体` 实际多少列写入文档（约 17 ASCII 列 / 8-9 CJK 列），与之前估算对齐。
  3. 验证表新增 "AI Text 屏在 240×320 EPD 上首帧渲染无重叠"。

### 1.7 Sleep 屏重复 `entry11` 删除旧 timer 与 commit message 表述"进入前先删旧 timer"存在竞态隐患

- **严重性**：Low
- **位置**：`examples/pda2/ui_deckpro.cpp:3800` —— entry11 中 timer 操作
- **证据**：
  - 申请书 §2.1 "Cop 1.1 句柄未保存：entry11：sleep_timer = lv_timer_create(...)；进入前先删旧 timer"。
  - commit `bb1819b` 信息 "save the lv_timer handle so back/destroy can cancel the pending sleep"。
  - 但这是 Sleep 屏，scr_mgr 一次只允许一个屏——**不可能"进入" Sleep 屏两次**（除非用户 Back 后再 Sleep）。
- **影响**：
  - 极端情况（任务在屏 destroy 后未结束、用户重新进 Sleep）：旧 timer 引用如果仍指向已销毁的屏上下文，`lv_timer_del()` 会访问野指针。
  - `entry11` 删除旧 timer 的逻辑优先级应当高于"创建新"——但代码层面仍是先 `lv_timer_create` 再赋值，还是先 `lv_timer_del(sleep_timer)` 再 `lv_timer_create`？需要确认。
- **最小修复**：
  1. 在 `entry11` 顶部加 `if (sleep_timer) lv_timer_del(sleep_timer); sleep_timer = NULL;`，明确顺序。
  2. 在 commit message 加 "no nested Sleep screen possible, but defensive cleanup guarantees no double-free"。

### 1.8 async IPC contract 文档化路径合规，但尚未覆盖"无任务路径"

- **严重性**：Low
- **位置**：`docs/async_ipc_contract.md`（新建于 `ceade9c`）
- **证据**：
  - 申请书 §2.8 "10 条硬性规则 + 4 任务表格化"，新文件确实存在。
  - 但是 Sleep 屏（`bcca4d2` / `bb1819b`）**不创建任务**——已被多方记录。
  - 文档是否仅约束"有任务的屏"未明。
- **影响**：未来维护者看到此文档会假设"所有屏都遵守"——Sleep 屏与默认实现差异未明示。
- **最小修复**：`docs/async_ipc_contract.md` 顶部加一段 "本章约束**异步 HTTP 请求屏**；Sleep 屏 / Keys 屏 / GPS 屏等纯本地屏不适用。"

### 1.9 NVS 原子写测试覆盖：未说明 NVS 写失败模拟场景

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp::openai_save_config`（暂存-校验-换入三步，`fb2ac62`）
- **证据**：
  - 申请书 §2.4 "暂存-校验-换入三步（`*.tmp` 键写读回校验 → 覆盖正式键 → 失败回滚旧值），保存失败不再产生混合配置"。
  - 但未提供：
    - 单元测试或集成测试：模拟 `Preferences::putString` 失败（如 NVS 满）
    - 失败时回滚路径的具体日志
    - 用户可见的错误提示文案
- **影响**：
  - 与 WiFi Test 类似，"失败回滚"语义产品端体验依赖于 UI 层的 msgbox 文案——但申请书未提。
- **最小修复**：
  1. 在 `ui_ai_cfg.cpp` Save 失败时显示 "Save failed: <reason>"，明确失败原因（NVS 满 / 写超时 / 校验失败）。
  2. 在 `ceade9c` commit 后续补充 `tests/test_nvs_atomic_save.md`（伪测试），描述 mock 失败注入的预期 UI 反馈。

### 1.10 验证清单 §4 14 项全部 ⏸，无人实测

- **严重性**：High（流程问题，已第 3 次出现）
- **位置**：申请书 §4
- **证据**：
  - 14 项真机验证全部 ⏸ "等待用户实测"。
  - 上两轮评审（`23942f6..9b104d1` / `01f8eac..8b96656`）已两次标记同一问题。
- **影响**：
  - 申请合并到 master 后才会触发用户实测——若发现严重 bug，需要回滚 commit 与历史 commit。
  - 本批 `eecebda` 是 **AI Text 大改**，相当于重新设计——未实测就合并风险极高。
- **最小修复**：
  1. **强力建议**：申请人合并前至少完成 §4 中编号 5/6/7（Sleep）三项验证（不可逆操作），以及编号 11/12（AI Chat）的开发机模拟（用 PC 串口模拟）。
  2. 申请人将实测结果回填到 §4："开发机模拟" 与 "用户真机实测" 两列分别填充。
  3. CI 增加 `pio run -e pda2` 之后用 `unity` / `doctest` 等串口单测，至少覆盖 `openai_save_config` 三步原子写。

### 1.11 `ceade9c` 删除了前两轮的独立评审申请文档，与"申请合并吸收"规则冲突

- **严重性**：Low
- **位置**：`docs/reviews/wifi-config-keyboard-review-request-eecebda.md` 与 `docs/reviews/wifi-config-keyboard-review-request-bb1819b..ceade9c.md` （按 §17 L17 与合并说明）
- **证据**：
  - 申请书 §合并说明 "原文件随本申请删除，git 历史保留（不覆盖）" —— 但实际删除应发生在 git 层（git rm），而 `ceade9c` 是 docs-only commit。
  - 我已确认 `ls docs/reviews/` 当前只看到 `wifi-config-keyboard-review-request-bb1819b..ceade9c.md`，未看到 `wifi-config-keyboard-review-request-eecebda.md` —— 说明这两个原始申请确实被合并删除。
- **影响**：
  - 与评审工作流约定"历史文档保留不覆盖"不完全一致；本批以"git 历史保留"作妥协，但审查各轮独立 evidence 时无法回到原始申请书。
  - 未来合并申请可能形成"前 N 轮结果证据消失"的趋势。
- **最小修复**：
  1. 在 `ceade9c` commit 信息中明示 "原 round 19 / round 20 申请书已被并入；如需找原始请求，可参考 git log 删除前的 tree" —— 提供具体 commit hash。
  2. 在 README / `docs/reviews/README.md`（建议新建）写一段 "申请合并流程"：何时可合并、合并后如何追溯。

---

## 2. 通过项

### Part 1（`eecebda` AI Text 聊天界面）

- **气泡布局正确**：AI 左对齐 + 用户右对齐，符合主流 chat UX；`lv_obj_align_to` 实现自然。
- **自动滚到底**：`lv_obj_scroll_to_y(LV_COORD_MAX)` 保证用户看到最新消息。
- **代次校验**：`s_chat_page_gen` 在 entry/destroy 自增，防止离页后到达的回复覆盖 UI。
- **草稿保留策略**：成功才清空输入框；失败（网络 / 鉴权 / 超时 / 任务错误）保留可重试。
- **滚动静音**：`lv_obj_scroll_by(120, ±)` 而非逐字符滚，节省 LVGL 重绘。
- **气泡宽度 178px**（含 border）+ label 170px（`LV_LABEL_LONG_WRAP`）—— EPD 240px 屏宽适配。

### Part 2（第 17/18 轮整改 8 commit）

- **`bb1819b` Sleep 屏**：1s tick 倒计时 `Sleep in: 2` → `1` → 深睡；`repeat_count(4)` 兜底；callback NULL 守卫；`ext1(1UL << BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ANY_LOW)` 与注释一致；`wake = setup() 冷启动`，**结构性免疫** 修饰键跨深睡残留。
- **`6da7fe2` WiFi scan 释放 target**：abort 时记录 target count + event callback 中 `cnt > target` 自行清 pending，永久 "Scan busy" wedge bug 修复。
- **`6da7fe2` Busy 代次化**：`busy_gen` 携带，代次不匹配不释放 busy —— 杜绝 Cop 1.4 stale busy release。
- **`6da7fe2` 队列 NULL 检查**：两处 `xQueueCreate` 返回值检查；失败即提示且不置 busy / 不启动任务 —— 杜绝 boot 早期崩溃。
- **`84090c1` CA bundle Python 重写**：`chr(10)` byte-exact newline + `openssl x509` 逐张解析；实测 5 根全 PASS。
- **`fb2ac62` openai 原子写**：暂存-校验-换入，失败回滚；`examer → examiner` 拼写修正；显式 `timeout_ms` 参数（Test 10s / Chat 30s）。
- **`3bc255f` AI Config**：Test 改用最小 chat-completion，msgbox 显示回复前 70 字；状态行持续解释 Save 为何被挡。
- **`3bc255f` Test 队列创建检查**：与 `6da7fe2` 同样的 NULL 检查范式。
- **`3bc255f` Task snapshot**：Test 任务持有草稿自有副本，请求中编辑不污染 —— 解决 Cop 1.6 共享缓冲问题。
- **`d21bd18` AI Chat Task snapshot**：`chat_send_req_t` 自有快照，全局 `chat_prompt_buf` 删除；busy 代次化。
- **`d21bd18` UTF-8 截断**：截断前回退到码点边界，追加 `(truncated)`。
- **`e210b46` http_utils UA 策略**：注释明示 `http_get_ua` 专用、不全局 UA。
- **`ceade9c` 文档**：`docs/async_ipc_contract.md` 4 任务表格化 + 10 条硬性规则；归档前两批评审结果。

---

## 3. 继承风险

- **Critical**：API Key 入库（未在本批处理，按用户决策延后）。
- **主 1.6 `AI_SYSTEM_PROMPT` 仍硬编码**：`fb2ac62` 仅修正拼写 + 注释，未实现 NVS 化；本评审要求在下次 AI Config 重构中处理。
- **主 1.11 / bcca4d2 主 1.5 真机回归**：再次推迟到下次评审，建议把 Sleep / AI 两屏验收作为下批申请的"准入证"。

---

## 4. 审批意见

- [ ] A. 全量接受
- [x] **C. 部分接受**（按下列条件）：

  - ✅ 接受 `eecebda` Part 1 AI Text 聊天界面，本评审要求 **下批必须添加 NVS / SPIFFS 持久化**（§1.2）
  - ✅ 接受 `bb1819b` Sleep 屏（含本次的倒计时 + NULL 守卫 + ext1 注释），但需合并前完成 §1.10 真机回归 5/6/7 三项（不可逆操作）
  - ✅ 接受 `6da7fe2` WiFi scan + busy 代次 + 队列检查（结构免疫 + 防御 + NULL 检查三件套）
  - ✅ 接受 `84090c1` CA bundle Python 重写（解决 shell 转义陷阱）
  - ✅ 接受 `fb2ac62` openai 原子写 + 拼写修正
  - 🟡 接受 `3bc255f` AI Config Test 改 chat-completion，但要求**合并前**在 msgbox 显示计费提示（§1.3）
  - ✅ 接受 `d21bd18` AI Chat 任务快照 + UTF-8 截断 + busy 代次
  - ✅ 接受 `e210b46` http_utils UA 注释
  - ✅ 接受 `ceade9c` 文档（但要求加 `docs/async_ipc_contract.md` 顶部 "Sleep 屏等纯本地屏不适用" 标注，§1.8）
  - ⏸ `2875efb` docs-only 申请合并 commit —— 可保留，下次评审前若仍延后 Key 项，将作为追溯证据

  - ❌ Critical Key 项必须下次评审前见到 `openai_api.h:36` 的真实 Key 字符串被删除或占位符替代
  - ⏸ 真机回归 §4 14 项最迟在下一批申请提交前完成；优先级 Sleep(5/6/7) > AI Config(8/9/10) > AI Chat(11/12)

- [ ] B. 退回修订（不推荐，仅在真机回归发现严重 bug 时启用）

---

## 5. 关联阅读

- [Sleep 屏评审](wifi-config-keyboard-review-result-bcca4d2.md) + [第 8-16 轮整改评审](wifi-config-keyboard-review-result-01f8eac..8b96656.md) —— 本评审的继承前提。
- [`allinone-design-ai-ux-flow-review-result.md`](allinone-design-ai-ux-flow-review-result.md) §1.7 模型历史 / §1.1 当前生效可见 —— 建议在 chat 历史持久化时一并落实。
- 后续申请模板：`docs/reviews/wifi-config-keyboard-review-request-eecebda..ceade9c.md` 已建立"合并申请"格式，建议在 `README` 或工作流手册中固化。

---

**评审人**：Claude（allinone-design / pda2 评审视角），已交叉核对申请书 + 关键 commit 的实际 diff（`eecebda` / `bb1819b` / `6da7fe2` / `3bc255f` / `d21bd18` / `84090c1` 的 stats 与代码片段）。
