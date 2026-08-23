# 第 8-16 轮双评审整改评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-01f8eac..8b96656.md](wifi-config-keyboard-review-request-01f8eac..8b96656.md)
- **评审依据**：[主评审结果 23942f6..9b104d1](wifi-config-keyboard-review-result-23942f6..9b104d1.md)（10 Findings）+ Copilot 复审 10 Findings
- **关联 commit**：`01f8eac` `cb8201f` `57356fa` `8b96656`
- **评审结论**：**部分接受**（Critical 项延后、其余接受但仍需真机回归）

---

## 0. 关键判定

申请人**明确申请延后**修订 Critical 项（主 1.1 / Cop 1.1：真实 API Key 入库），处理路径为"OpenRouter 后台 revoke + 后续 NVS-only + 缺 Key 提示"——本评审承认这一用户决策的合法性，但 **Critical 项状态在代码层不关闭**，下一次评审时仍作为不通过项出现，直到 `AI_KEY_DEFAULT` 真实 Key 字符串从 git HEAD 移除。即便后续单独申请删除 `AI_KEY_DEFAULT`，旧 Key 字符串仍留在 git 历史内，建议同时附 `git filter-repo` 计划 / 仓库 README 通告。

---

## 1. Findings

### 1.1 真实 API Key 仍硬编码入库，用户主动申请延后

- **严重性**：**Critical**（遗留项，本批未处理）
- **位置**：`examples/pda2/openai_api.h:30` —— `#define AI_KEY_DEFAULT "REDACTED-OPENROUTER-KEY"`
- **触发场景**：任何 clone / fork / 镜像均可读取，与申请人申请时间无关。
- **证据**：
  - 已用 `git show 57356fa:examples/pda2/openai_api.h` 复核，关键字符串未被本批 4 个 commit 改动。
  - `openai_api.cpp:18` 仍使用 `p.getString("key", AI_KEY_DEFAULT)` 作为 NVS 缺省值——意味着即使本机已轮换 Key，出厂或 NVS 清空后的设备仍会回退到该 Key。
  - 申请书 §1 明确写 "用户将去 OpenRouter 后台 revoke；代码暂不删除 `AI_KEY_DEFAULT`（用户'暂时不修复'）" —— 申请人已记录用户决策。
- **影响**：
  - Key 撤销只能阻止**新滥用**，无法清除 git 历史的暴露面。
  - 后续任何 PR / 派生仓库 / CI cache 都会包含该 Key 字符串。
  - 形成永久的反模式：未来贡献者可能复制 "在头文件放默认 Key" 的做法。
- **最小修复路径**（不强制本轮，但下次必查）：
  1. 删除 `AI_KEY_DEFAULT` 真实 Key 字符串，改为空 `""` 或注释占位。
  2. NVS 缺 Key 时由 `openai_load_config()` 返回 false / 上层提示"未配置 Key"。
  3. `git filter-repo` 移除历史中的敏感字符串（或诚实接受历史已暴露，仅控制未来）。
  4. 添加 `SECURITY.md` 与 CI `gitleaks` hook。

### 1.2 模块拆分：本批 4 commit 按模块切分符合约定

- **严重性**：Pass（整改完成）
- **证据**：`01f8eac`（CA bundle 修复 + 脚本）/ `cb8201f`（扫描竞态 + WiFi 页异步 IPC）/ `57356fa`（AI Config Test/Save/timeout + AI Config 队列 IPC）/ `8b96656`（AI Chat 草稿 / UTF-8 / 队列 IPC）—— 任一 commit 可独立回退。
- **改进**：相比上轮 `048ea73` / `d8f0ab7` / `23942f6` 的三模块混一，本批拆分清晰，可独立 cherry-pick / revert。
- **建议**：`cb8201f` 同时改了"扫描代次 + WiFi 页队列 IPC + 页面代次" 三件事，虽然属同一屏（WiFi status），但仍偏大；如果后续还要迭代 WiFi 屏，建议进一步把"页面代次基础设施"与"WiFi 屏具体使用"拆分。

### 1.3 异步任务范式：gen + 队列 + 所有权转移已落地，但仍有边界未明

- **严重性**：High
- **位置**：`cb8201f`（WiFi status 队列 IPC）、`57356fa`（AI Config 队列 IPC）、`8b96656`（AI Chat 队列 IPC）
- **证据**：
  - 申请书 §2 给出统一范式图，并约定：
    - 工作任务 `new Result → xQueueSend → UI 线程 delete`（所有权转移）
    - 队列深度 4
    - 页面 / 请求代次校验
    - `busy` 仅 UI 线程读写
  - `cb8201f` 提交信息确认 "heap-allocated structs (ownership transfers to the UI thread) and carry the page generation: no volatile-guarded std::string access, stale results from a previous page visit are dropped" —— 与申请书承诺一致。
- **未明确**：
  - **队列深度 4 与高并发**：如果用户在 WiFi Test 进行中反复按重试（页面停留 > 4 任务时长），第 5 次请求会 `xQueueSend(portMAX_DELAY)` 阻塞工作线程栈；任务阻塞期间无法退出页面（因为 gen 失效后结果被丢弃但任务未自我删除）。
  - **退出页面时任务栈复用**：申请书未说明 "任务未完成时 destroy 是否能让任务自然终止"。如果 std::atomic<bool> cancelled 在任务入口时检查一次后即退出，可能漏掉 destroy 之后再发起的请求。
  - **`busy` 仅 UI 线程读写** vs **任务回调期间 mutex**：LVGL timer / 主 loop 期间访问 busy 没问题，但若在 ISR 或 keypad 回调中也读 busy，需要 atomic 保护。
  - **msgbox 倒计时与任务生命周期**：`AI Config` 中 "HTTP timeout = 10s = msgbox 倒计时" 与 "Close = Cancel（请求代次++，迟到结果丢弃）" 是不同语义，前者是真的 10s 超时，后者是 UI 主动丢弃——但 Close 与 timeout 竞态谁先生效？需要明确 Close 后 task 是否被强制 cancel。
- **影响**：
  - 边缘场景下任务资源泄漏（栈 / 队列项）。
  - Close 与 timeout 竞态产生不期望的"看似成功"提示。
- **最小修复**：
  1. 在 `cb8201f` / `57356fa` / `8b96656` 三个 commit 的代码注释或 `docs/async_ipc_contract.md`（建议新建）明确：
     - 队列深度上限 + 阻塞策略（若请求 > 4 排队时是否拒绝新请求）
     - destroy 与 Close 后的 task cleanup 路径（是否需要 `xTaskAbortDelay`-类机制或简单 `s_cancelled` 标志）
     - 任务栈 / 优先级 / 看门狗设置（三处必须一致）
  2. 验证表新增三项：
     - 在 WiFi Test 进行中连按 5 次重试 → 不崩溃、不栈溢出
     - msgbox Close + 10s timeout 同时触发 → 仅一种生效
     - 离页后任务仍在飞 → 完成后 ui 不被改动

### 1.4 Save 门槛：代码已实现 `s_ai_test_passed` 语义，但需要 UX 层"显示/重置"原则

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_cfg.cpp:61`、`:182`、`:330`、`:381`、`:390`、`:498`
- **证据**：
  - 代码确认实现：
    - `static bool s_ai_test_passed = false;`（行 61，需通过 Save 才允许）
    - `if (!s_ai_test_passed)` 阻挡 Save（行 182）
    - 编辑字段时 `s_ai_test_passed = false`（行 381、390、498）—— "edited: Test is stale"
- **未明确**：
  - **UX 层反馈**：用户编辑后是否看到"Save disabled — Run Test first"的提示？申请书未给 AI Cfg 屏 wireframe 上 Save 按钮的禁用状态示意。
  - **多字段编辑累计**：如果用户先 Test → 改 Base → 改回原 Base，Test 是否仍然有效？代码显示"改任意字段立即失效"，对单字段错误测试友好但对"想尝试两种 Base 对比"的用户不友好。
  - **错误后的 Test**：Test 失败后 `s_ai_test_passed` 显然是 false（初始值未变），但**用户编辑失败字段前**是否能 retry 当前未变更的字段？需要明确。
- **影响**：用户看不到"Save 不可点"原因，反复尝试 Save + 看错误，体验差。
- **最小修复**：
  1. Save 按钮 disabled 时改文案为 "Run Test first" 或 "Test stale — re-run"（让用户知道原因）。
  2. 编辑任意字段后，在 Save 按钮旁显示 `! edited` 标记，提示 Test 已失效。
  3. AI Cfg wireframe 在 §3 增补"Save/Test 按钮的 3 状态：Test 通过 / Test 未通过 / 编辑后失活"。

### 1.5 AI Test 用草稿端点：对自定义端点解析失败无 fallback

- **严重性**：Medium
- **位置**：`57356fa` —— "Test 端点从草稿 Base 推导（`chat/completions → models?limit=2`），用草稿 Key 请求"
- **证据**：
  - 推理规则假设"Base 末尾是 `/chat/completions`"。
  - 设计文档 §4 L167 端点预填 `https://openrouter.ai/api/v1/chat/completions`；常用自定义端点（OpenAI 直连、Cloudflare AI Gateway、自建 reverse proxy）的实际 Base URL 可能是：
    - `https://api.openai.com/v1/chat/completions` → 替换末尾 `/chat/completions` → `https://api.openai.com/v1/models?limit=2` ✅
    - `https://my-proxy.com/openai/v1/chat/completions` → 替换末尾 → `https://my-proxy.com/openai/v1/models?limit=2` ✅
    - `https://my-proxy.com/openai/chat/completions`（无 /v1/） → 替换末尾 → `https://my-proxy.com/openai/models?limit=2` —— 可能仍可工作，但路径变浅。
    - `https://my-proxy.com/?path=chat/completions`（path in query）→ 替换无效 → Test 请求路径错乱。
- **影响**：自定义端点解析失败时 Test 失败但原因不明确，用户不知是 URL 错还是 URL 解析策略不适用。
- **最小修复**：
  1. 在 `ui_ai_cfg.cpp::derive_models_url()` 中显示如果未匹配 `/chat/completions$` 时显式 fallback："Try directly: `<base 的 v1 部分>/models?limit=2`"，并允许用户手动改 Base 后再试。
  2. 错误提示明示 "Test endpoint cannot be derived from Base; check Base ends with /chat/completions or test manually"，而不是简单 Test 失败。
  3. 在 `issue_list.md` 跟踪此非主流端点形态。

### 1.6 `AI_SYSTEM_PROMPT` 仍硬编码，无法配置且与"系统提示"决策矛盾

- **严重性**：Low
- **位置**：`examples/pda2/openai_api.h:29` —— `#define AI_SYSTEM_PROMPT "You are a KET English examer..."`
- **证据**：
  - 与 `AI_KEY_DEFAULT` 类似，是 `#define` 嵌死。
  - 上轮评审 1.1 仅聚焦 Key，未涉及 system prompt；本轮也没有处理。
  - `KET English examer` 拼写错误（应为 `examiner`）——申请人 commit 注释里也保留了这个错词。
- **影响**：
  - 用户无法切换"通用助手" / "雅思口语教练" 等 persona。
  - 拼写错误在 OpenRouter 系统提示中可能被部分模型视作噪声。
- **最小修复**：
  1. 至少修正 `examer` → `examiner`。
  2. 把 `AI_SYSTEM_PROMPT` 也移到 NVS `ai.system` 字段，并在 AI Config 屏增加对应编辑（与 base/model/key 同列）。
  3. 若决定不放开配置，则在头注释明示 "v1 固定 system prompt" 与后续 v2 计划。

### 1.7 UTF-8 安全断行：算法思路正确，但未在代码注释层明示

- **严重性**：Low
- **位置**：`8b96656` —— "断行点回退越过 continuation byte (0x80-0xBF)，多字节序列不被截断"
- **证据**：
  - 提交信息确认思路。
  - 上轮评审 1.6（Copilot 1.10）涉及 UTF-8 断行问题；本轮修复方向正确。
- **未明示**：
  - **多字节断点的边界条件**：
    - 行末刚好是 CJK 第二字节（`0x80-0xBF`）→ 回退到首字节前断行，是否避免在两 CJK 之间出现孤立的单字节？
    - 行末遇到 4 字节 emoji 第二字节 → 回退到首字节前，emoji 不被截断，但视觉上行尾会留出半个 emoji 位置。
  - **`strlen()` vs `wcwidth()`**：现有评审要求按 `wcwidth()` 判定列数（如 CJK = 2 列），代码实现是否一致？
- **影响**：
  - 大部分情况正常工作，但极端 emoji 与换行交叉处可能有视觉瑕疵。
- **最小修复**：
  1. 在 `ui_ai_chat.cpp::chat_wrap_text`（或对应函数）顶部增加 Doxygen 注释：断行规则、回退逻辑、emoji 列宽策略。
  2. 真机长句（含 emoji）的回答截图作为回归证据（§4 验证清单 ⏸ 第 8 项）。

### 1.8 状态栏刷新策略文档化路径合规，但未提供 implementation 链接

- **严重性**：Low
- **位置**：申请书 §1 行 32 与 `issue_list.md` §5.1
- **证据**：
  - 申请人选择"在电量 10s 周期内检查、仅分钟变化时更新（无额外计时器）"。
  - 文档化路径写 `issue_list §5.1`，但当前 issue_list 不在本评审范围内，无法验证其实际内容是否清晰。
- **最小修复**：在本次合并 commit 加一个变更注释或 `docs/status_bar_refresh.md` 引用 `issue_list §5.1`，便于评审 + 用户追溯。

### 1.9 NVS 迁移文档化路径同上：未来加字段应同步升级 schema

- **严重性**：Low
- **位置**：申请书 §1 行 33
- **证据**：申请人承诺 "无 schema 版本——将来加字段时引入 `cfg_version`"。
- **影响**：现在键名/语义兼容，但下一次 AI Config 屏加字段（如 system prompt 字段、temperature 字段）时，必须同步引入 schema 版本。
- **最小修复**：在 `openai_api.h` 或 `openai_api.cpp` 顶部留 TODO 注释，提醒 "添加字段时引入 cfg_version"。

### 1.10 UA 策略文档化路径：决策合理，但需在 `http_utils.h` 留痕

- **严重性**：Low
- **位置**：申请书 §1 行 34
- **证据**：申请人选择 "UA 策略：`http_get_ua` 仅用于明确按 UA 区分响应的端点；其余端点保持默认 UA，不做全局参数化"。
- **最小修复**：在 `http_utils.h` 函数注释中明示 "UA 通过专用 `http_get_ua()` 注入；默认使用 LibreHttpClient 默认头，不带 curl UA"，便于后续维护者理解。

### 1.11 验证清单仍多项目 ⏸，主功能未真机跑通

- **严重性**：High
- **位置**：申请书 §4
- **证据**：
  - 8 项回归清单均标 ⏸ "待用户配合"。
  - AI Test 草稿端点 ⏸ / Save 门槛 ⏸：两项均为新设计核心逻辑，**未在硬件上确认**。
  - 上轮评审（`23942f6..9b104d1`）已两次提示相同问题，本轮仍未解决。
- **影响**：
  - 本批 4 commit 包含 AI Config 的核心改动（草稿端点 Test、Save 门槛、msgbox 倒计时），但其中三项主路径未验证。
  - 与上轮 `1.3` 同样的"申请前未跑通就 merge"风险。
- **最小修复**：
  1. 在合并前申请人应至少完成以下真机验证并贴证据（串口日志 / 照片）：
     - AI Cfg：编辑 Base 后按 Test → msgbox 倒计时 → 改回的 Base 触发 Test → "Test stale" Save 按钮失活
     - AI Chat：发送失败（关闭 WiFi 中途）→ 草稿仍在输入框
     - 中文回答渲染：含 CJK 与 emoji 的长回答分页不破字
  2. 验证记录写入 `docs/qa/2026-08-16-round-17-qa.md`（建议新建）。
  3. 若无法在合并前完成，则至少在 commit message 中写明 "TODO: 验证" 标签，便于下次评审追踪。

---

## 2. 通过项

- **CA bundle `01f8eac`**：替换损坏的 ISRG Root X1（Mozilla bundle 完整 PEM），新增 `scripts/ca_bundle_check.sh`。`openssl` 逐根解析通过，证书链基础修复完成。
- **扫描代次释放 `cb8201f`**：`SCAN_DONE` 事件从布尔改为计数、超时置 `s_scan_release_pending`、下次扫描"先等事件再 `scanDelete()`"，符合上轮 High finding 1.2 修订要求。
- **异步 IPC 范式统一 `cb8201f` / `57356fa` / `8b96656`**：所有权转移 + gen 校验 + UI 线程独占 busy，三处一致，相较上轮"裸读 std::string"是显著的并发安全升级。
- **AI Config Test 草稿化 `57356fa`**：使用草稿 Base / Key 验证，避免"用 NVS 旧 Key 验证新端点"的失败语义。
- **Save 门槛 `57356fa`**：NVS 写失败返回 bool 不覆盖旧值；Test 未通过拒绝 Save（与"无后续编辑"联动失效）。
- **msgbox 倒计时与 HTTP timeout 合一 `57356fa`**：UI 倒计时不再虚标，10s 真超时。
- **AI Chat 草稿保留 `8b96656`**：发送成功才清草稿；网络 / 认证 / 超时 / 任务错误均保留。
- **UTF-8 断行 `8b96656`**：回退越过 continuation byte，保证多字节序列不被切。
- **wireframe 提供 §3**：AI Text + AI Config 屏布局有 ASCII 图，包含 FLOATING 钉底按钮。

---

## 3. 继承风险（未重复计为新 finding）

- **Cop 1.6**：AI Test 草稿端点解析失败 fallback（已部分列入 §1.5）。
- **Cop 1.8**：msgbox Close 取消语义（已在 §1.3 提到 Close / timeout 竞态）。
- 主 1.6 / 1.7 / 1.8 文档化路径（已部分列入 §1.8 / 1.9 / 1.10），下次评审前应确保 `issue_list.md` 实际内容与申请一致。

---

## 4. 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**：
  - ✅ 接受 `01f8eac` / `cb8201f` / `8b96656` 三 commit
  - ✅ 接受 `57356fa`，但 `AI_KEY_DEFAULT` 真实 Key 仍遗留 —— **本项作为不通过 Critical 单独跟踪**，下次评审前必须见 `openai_api.h` 中真实 Key 字符串被删除或被占位符替代
  - ⏸ 真机回归（§1.11）作为 merge 前 gate，可延后到下一 commit 但不得跨越 2 次以上评审
  - ⏸ §1.4 异步 IPC 边界条件文档化 —— 下一 commit 申请时若复用该范式，必须在新文件头引合同 doc
  - ⏸ §1.5 AI Test 端点解析 fallback 与 §1.6 System Prompt NVS 化 —— 建议在 AI Config 屏下一轮重构中一起处理

---

**评审人**：Claude（allinone-design / pda2 评审视角），已交叉核对申请书 + 4 个 commit 的实际 diff + `examples/pda2/openai_api.h` 当前内容。
