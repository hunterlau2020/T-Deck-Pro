# 第 8-16 轮合并申请评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-23942f6..9b104d1.md](wifi-config-keyboard-review-request-23942f6..9b104d1.md)
- **关联 commit**：`23942f6` `d8f0ab7` `8001ff1` `64ebcb7` `048ea73` `9551bd7` `6be70eb` `e5b109d` `d4ccf28` `9b104d1`
- **评审结论**：**退回修订**
- **本次评审侧重**：申请书本身（结构 / 模块粒度 / 验证充分性 / 风险披露） + 涉及代码已确认的关键问题；具体实现的代码细节不在本文范围内，留待按修订后的逐 commit 重新申请。

---

## 1. Findings

### 1.1 真实 API Key 硬编码并入库，违反基本安全规范

- **严重性**：**Critical**
- **位置**：`9b104d1` → `examples/pda2/openai_api.h` 新增 `#define AI_KEY_DEFAULT "REDACTED-OPENROUTER-KEY"`
- **触发场景**：任意 clone / pull / fork 仓库者、CI runner、依赖该仓库的二次开发均可读取该 Key。
- **证据**：
  - `openai_api.cpp` 的 `openai_load_config()` 在 NVS 无值时回退到 `AI_KEY_DEFAULT`，相当于"出厂内置 Key"。
  - 申请书 30 行明确写 ⚠️ "已入仓库需保密/轮换"——申请人已意识到风险但仍提交。
  - 即使用户立即去 OpenRouter 撤销该 Key、生成新 Key，**新 Key 仍然要再被提交进仓库一次**才会沿用同样的"出厂默认"模式——形成"泄露 → 轮换 → 再次泄露"的死循环。
  - 没有任何 NVS 迁移 / 首次启动检测 / Key 缺失提示的兜底——只靠"出货前手动烧 NVS"或"用户主动改 NVS"才能绕开。
- **影响**：
  - 该 Key 一旦被 GitHub 抓取或被恶意 PR 复制，整张 OpenRouter 额度被滥用；用户账户的钱/配额将被消耗到 0。
  - 任何后续 commit 中的"默认 Key"都会作为 git 永久历史存在，无法彻底清除（filter-branch / BFG 也只能从最新 HEAD 移除）。
  - 与之共存的 `AI_SYSTEM_PROMPT`（"You are a KET English examer"）也写死在头文件，调整体验需改源码重烧。
- **最小修复**：
  1. **删除 `AI_KEY_DEFAULT` 宏**，删除 `examples/pda2/openai_api.h` 中的真实 Key 字符串。
  2. NVS 缺 Key 时改为 `openai_load_config()` 返回 `false` / 由调用方上抛"未配置 Key"提示（与 `http_require_wifi` 现有契约一致），让 AI 屏显示"Set AI Key"并跳到 AI Cfg 屏。
  3. 同一思路适用于 `AI_SYSTEM_PROMPT`：改为 NVS 可覆盖，默认值可保留，但**默认值不得是用户私有内容**——可改为 `""`（让 OpenRouter 走默认 system 或厂商默认）或写在 `README.md` 中的"可选用例"列表。
  4. **轮换已泄露 Key**：OpenRouter 后台立即 revoke；同步检查账户用量 / 计费。
  5. 仓库根目录增加 `SECURITY.md`："禁止提交真实 Key / Token / 证书私钥；CI 加 `gitleaks` / `trufflehog` 预检 hook"。

### 1.2 多个 commit 混入无关模块，违反"代码按模块拆 commit"约定

- **严重性**：High
- **位置**：`d8f0ab7`、`048ea73`、`23942f6`
- **触发场景**：后续发现某一模块需回退 / cherry-pick / 单独回归时，因 commit 混合而被迫保留无关改动。
- **证据**：
  - `d8f0ab7` "add GTS Root R4, NTP sync after connect, CST-8 timezone" —— 同一 commit 改了 3 个不相关模块：CA bundle（证书）、WiFi 连接后 NTP 同步（运行时）、`configTzTime` 时区（编译期常量）。任意一项回退都会影响其他两项。
  - `048ea73` "SCAN_DONE event sync, async WiFi Test, Time Sync button" —— 同一 commit 改了 3 个不相关模块：扫描中止竞态（`WiFi.onEvent`）、WiFi Test 异步化（FreeRTOS task + LVGL timer）、WiFi 屏列表项（UI）。其中任一模块出问题都不便隔离。
  - `23942f6` "fix WiFi Test TLS failure - complete CA bundle + NTP time check" —— CA bundle 扩展 + `time(nullptr)` 校验 + NTP 重试混在一个 commit。
  - 相比之下 `64ebcb7`（硬件 FIFO 排空）、`8001ff1`（UA 头）、`9551bd7`（状态栏时间）、`6be70eb` / `e5b109d` / `d4ccf28`（AI 配置三连改）均符合"单模块单 commit"约定。
  - 评审工作流约定明确写明：**"代码按模块拆 commit"**（用户 memory）。
- **影响**：
  - `048ea73` 把第 4 轮 1.2（High，扫描中止竞态）的修复与"新功能：WiFi Test 异步化 + Time Sync 按钮"合并——若第 4 轮 1.2 修复不彻底，无法单独追加修复而不带其他改动。
  - `d8f0ab7` 把时区从 PST8PDT 改 CST-8 与 CA bundle 升级混在一起——若只想回退时区，会同时回退关键 CA 升级。
  - 申请书提供的"全量回滚"命令一次性撤 10 个 commit 实际等同于回退整个评审周期；**无法选择性回退**。
- **最小修复**：
  1. 拆 commit `d8f0ab7` 为：
     - `d8f0ab7a` "ca_bundle: add GTS Root R4"
     - `d8f0ab7b` "wifi: NTP sync after successful connect"
     - `d8f0ab7c` "factory: switch configTzTime from PST8PDT to CST-8"
  2. 拆 commit `048ea73` 为：
     - `048ea73a` "wifi: defer scanDelete() until SCAN_DONE event"
     - `048ea73b` "wifi_status: async WiFi Test via FreeRTOS task + LVGL timer"
     - `048ea73c` "wifi_status: add Time Sync list item"
  3. 拆 commit `23942f6` 为：
     - `23942f6a` "ca_bundle: add ISRG Root YR + DigiCert G2"
     - `23942f6b` "http_utils: fail fast when system time has not been synced"
  4. 拆 commit `9b104d1`（按 §1.1 修复后）至少为：
     - `9b104d1a` "ui_ai_chat: multi-line input box + Send/Clear buttons"
     - `9b104d1b` "ui_ai_chat: async send via FreeRTOS task"
     - `9b104d1c` "openai_api: match OpenRouter curl reference request body"
     - `9b104d1d` "openai_api: surface missing NVS Key instead of using built-in default"

### 1.3 验证状态"⏸ 待用户真机复测"项过多，关键路径未跑通

- **严重性**：High
- **位置**：申请书 §4 验证状态表
- **触发场景**：评审通过后合并，但 AI 配置屏与 AI 对话屏的主流程未在真机上验证。
- **证据**：
  - 验证表 4 项中 3 项标 ⏸：
    - "AI 配置 Test msgbox / 默认 model、key"——尚未在硬件上确认 msgbox 行为、Test 按钮路径、默认 Key / Model 生效路径。
    - "AI 对话发送（新请求体 + 异步）"——`9b104d1` 的 `cJSON` 拼接 + FreeRTOS 任务 + 异步 UI 刷新三段链路未端到端验证。
    - "第 4-7 轮修复项回归"——这是已经在前 7 轮评审中反复出现的回归测试项；本轮再次挂"待用户真机复测"。
  - 申请书 §1 写"申请人：Claude（pda2 现场调试，配合用户实测按键）"——申请人已与硬件交互，但验证表仍写"待用户真机复测"，说明申请人未实际跑完。
- **影响**：
  - AI 屏是本次 8-16 轮的核心新功能（占 10 个 commit 中的 4 个：6be70eb / e5b109d / d4ccf28 / 9b104d1），未经验证就合入会复现 WiFi 配置屏早期多轮返工的模式。
  - 第 4-7 轮修复项未回归意味着新加的 `64ebcb7` 硬件 FIFO 排空是否真正生效、扫描中止竞态是否真的解决、`keypad_clear_chars()` 与 `keypad.flush()` 的双清顺序是否正确，都还没有证据。
- **最小修复**：
  1. 申请人提交前应完成真机回归并把验证结果回填到 §4 表格，至少包括：
     - AI Cfg 单 Test → msgbox 倒计时 → 成功/失败反馈 + 默认 model / key 生效
     - AI Chat 输入 → Send → 异步显示 `Thinking...` → 收到响应 → 多行显示
     - 触发 4 个已知回归场景（连接期间按键清、首尾场退出 AI 屏、双 Shift 层切换、Sym 锁跨页保留）
  2. 验证记录用 `git notes` 附加到对应 commit，或单独 `docs/qa/2026-08-16-round-8-16-qa.md` 留档。

### 1.4 异步任务（WiFi Test / AI Send）的生命周期与并发安全未在申请书中描述

- **严重性**：High
- **位置**：`048ea73`（WiFi Test 异步任务）、`9b104d1`（AI Send 异步任务）
- **触发场景**：用户在页面等待响应时 Backspace 退出、再次进入页面、再次发起请求；或 WiFi 重连、AP 切换过程中任务仍在运行。
- **证据**：
  - 申请书对两条异步路径都只用一句话描述："FreeRTOS task + LVGL timer" / "FreeRTOS task + 发送中吞键"。没有描述：
    - 任务退出时如何通知 LVGL 端停止轮询
    - 任务在页面销毁后是否仍能写 UI（越界访问风险）
    - 多任务并发（WiFi Test 中途发起 AI Send）如何仲裁
    - 任务栈 / 优先级 / watchdog 设置
  - 第 7 轮评审 1.3 解决的是"15s UI freeze"，但任务 IPC 与 UI 线程的同步原语（队列 / 标志 / 互斥量）未明确。
  - `48ea73` 修改 `http_require_wifi` 契约 6 行变更只显示行为变更，没显示同步原语。
- **影响**：
  - 异步任务在 UI 线程外修改 `String` 或 LVGL 对象 → 崩溃 / 内存破坏。
  - 多次快速进入 / 退出 WiFi 屏 → 多个孤立任务同时跑，后续 UI timer 多次触发回调，越界写。
  - 没有泛化抽象（`async_http_task_t` 或类似）→ 后续 GPS 屏 / 词典屏接入异步时需要重新发明。
- **最小修复**：
  1. 申请书补充"异步任务生命周期"段落，至少明确：
     - 任务创建 / 退出时机（页面 `create` / `destroy`）
     - 回调投递机制（`xQueueSend` 到 UI 线程 / `lv_async_call`）
     - 任务栈大小（建议 4KB+）、优先级（建议与 LVGL 线程同优先级或低 1 档）
     - 看门狗 / 超时（即便 UI 不再监听，超时必须自销毁）
  2. 提取公共 `async_http_task_t` 抽象（参考 LVGL `lv_async_call` 模式），避免 WiFi Test / AI Send 各自实现。
  3. 验证表新增一行："异步任务越界退出场景"——模拟 page destroy → 任务写 UI 是否崩溃。

### 1.5 CA bundle 一次性补全没有维护机制，对长期使用是新负债

- **严重性**：Medium
- **位置**：`23942f6`、`d8f0ab7`、`9551bd7`、`048ea73`
- **触发场景**：项目运行 1-2 年后，因 Chrome Root Program / Mozilla CA Store 弃用某些根证书，HTTPS 同样失效。
- **证据**：
  - 申请书 §3 决策 1："根证书一次性补全（4+1 根），覆盖 ifconfig.me / openrouter.ai / Cloudflare 等主流链；**任何 CA 新签发的站自动验证，无需按站下载**"。
  - 实际仍是 5 根硬编码（`ISRG X1` / `ISRG YR` / `DigiCert G2` / `GlobalSign R3` / `GTS R4`），没有运行时 OTA 更新机制。
  - 评审流程此前要求"`ca_bundle_check.sh` 实施时新建"（见 `allinone-design.md` §2.3 L74）——目前 8-16 轮均未提交该脚本。
  - 没有文档说明每个根的 `notBefore` / `notAfter` 与对应证书的等价关系。
- **影响**：
  - 1-2 年后若中间证书轮换、设备访问新签发证书的网站 → 失败，回退到"未知 CA"的诊断路径用户无法自助。
  - 该设备是墨水屏手持设备，不易 OTA；维护窗口稀少。
- **最小修复**：
  1. 提交 `examples/pda2/scripts/ca_bundle_check.sh`（评估设计文档 §2.3 L74 已要求），CI 中每次构建前自动跑。
  2. CA bundle 头部维护一个 `static const char *const ca_bundle_meta[]` 注释表：每个根的 `subject`、`notBefore`、`notAfter`、对应的 `openssl s_client` 验证日期。
  3. 申请书 / README 中新增"bundle 失效处理流程"：症状 → 用户可选恢复步骤（远程刷 bundle / 启用 Insecure TLS）。
  4. mbedtls `MBEDTLS_X509_CRT_VERIFY_CB` 钩子记录失败原因到串口（便于现场诊断），不暴露给终端用户。

### 1.6 状态栏时间默认刷新周期依赖键事件 / 10s 电量刷新，长时间不按键会过时

- **严重性**：Medium
- **位置**：`9551bd7` "show real local time in the menu status bar"
- **触发场景**：用户长时间停留在菜单页（不按键也不动），状态栏时间显示 1 分钟前的时刻。
- **证据**：
  - 申请书正文："状态栏时间从硬编码 '10:19' 改为实时本地时间（`--:--` 至 NTP 同步）；后并入电量 10s 刷新周期"。
  - 设计文档 §9.3 风险 3 详细讨论 EPD 全刷昂贵（1-2s），由此推断状态栏刻意走 10s 部分刷新。
  - 申请书未说明：菜单页是否注册了独立的 1s / 60s 定时器更新时间？或完全依赖电量周期 10s？
  - 用户实际"状态栏时间写死"反馈可能也是看到了 10s 滞后而误判为"写死"。
- **影响**：
  - 10s 刷新对于查看"当前小时"足够，但分钟级显示会让用户多次怀疑设备是否卡住。
  - 与 §9.3 三种 EPD 刷新策略（全刷 / 局刷 / 部分刷）的关系未明——状态栏时间刷新是否计入"局刷 vs 局部局刷" 的全刷计数器未定义。
- **最小修复**：
  1. 菜单页注册 1 分钟级 LVGL timer（不刷 EPD 仅在内存中更新 label），仅在分钟实际变化时触发局刷。
  2. 申请书中明确写"状态栏时间刷新策略：1 分钟级内存更新 + 10s 局刷 / 实际分钟变化时触发局刷"，与 §9.3 方案对齐。
  3. 验证表新增一项："菜单页停 5 分钟不动，状态栏时间刷新正确"。

### 1.7 NVS schema 多次变更无迁移步骤，旧设备升级可能读旧值后行为异常

- **严重性**：Medium
- **位置**：`6be70eb`（AI Cfg 重构）、`9b104d1`（AI_KEY_DEFAULT 引入）、`048ea73`（WiFi connect 后自动 NTP）
- **触发场景**：用户从第 7 轮前的固件升级到第 16 轮固件，NVS 已有旧 `ai` / `wifi` 命名空间的值。
- **证据**：
  - `6be70eb` 重构 AI Cfg 为多行 + 多字段，但 NVS 键名（`base` / `model` / `key`）保持不变——`openai_api.cpp::openai_load_config()` 仍按 `getString("base", ...)` 读取，理论上兼容。
  - `9b104d1` 引入 `AI_KEY_DEFAULT`——对于没有存过 Key 的旧设备，**首次启动会立即用真实 Key**，覆盖之前的"未配置"行为（之前默认 key 是空字符串）。
  - `048ea73` "连接成功后自动 NTP 同步"——对于升级前已在用、但 NTP 一直没同步的旧设备，行为从"用户手动按 Time Sync"变成"连接后自动同步"，可能与其他 WiFi 业务冲突。
  - 申请书未提"NVS 迁移步骤"或"首次启动检测"。
- **影响**：
  - 旧设备升级后可能立即出现"`AI_KEY_DEFAULT` 生效 → 真实请求被发出 → 用户以为是测试机被滥用"。
  - 时区相关 NVS（如果有）跨大版本变化时丢配置。
- **最小修复**：
  1. `openai_api.cpp::openai_load_config()` 增加"是否首次启动"判断：检测 NVS 是否存在 `ai` namespace 任一键；不存在则不读默认 Key，直接返回空。
  2. 申请书 §3 决策 6 补充："NVS 迁移：namespace `ai` 字段名 / 含义不变；新增 `cfg_version` 用于将来升级路径"。
  3. 验证表新增："旧 NVS 升级路径：先有旧值 → 升级后行为符合预期"。

### 1.8 `http_get_ua()` 只在 ifconfig.me 路径使用，UA 头未泛化

- **严重性**：Low
- **位置**：`8001ff1` "send curl User-Agent to ifconfig.me"
- **触发场景**：未来接入其他"返回 HTML / 拒绝非浏览器 UA"的端点（Cloudflare 防护的某些 API、自建反代）。
- **证据**：
  - `8001ff1` 新增 `http_get_ua()`，但只是 `http_get()` 的 UA 变体，没有参数化。
  - 申请书 §3 决策 1 在 CA bundle 段一笔带过，未提 UA 策略。
  - 类似于 WiFi Test 异步任务，这是"单点需求"的实现，缺乏抽象。
- **影响**：
  - 后续每个需要 UA 的端点都需写一份 `http_get_ua_for_xxx()`。
  - 全局 http 客户端行为不一致（有的带 UA 有的不带），调试时易混淆。
- **最小修复**：
  1. `http_get()` / `http_post()` 接受可选 `User-Agent` 形参（默认 `lvgl-iot/1.0` 之类的中性标识）。
  2. ifconfig.me 调用点改用带 UA 版本。
  3. 申请书中说明"UA 策略"决策，至少在 `http_utils.h` 注释。

### 1.9 布局宽度描述"200 字" / "150+ 字符"未在 240px 实际屏宽验证

- **严重性**：Low
- **位置**：`9b104d1` 描述 "input box is multi-line (64px, 200 chars max) with room for 150+ characters"
- **触发场景**：实际渲染时 240px 宽 + 14pt 字体一行只能容纳约 20-25 个 ASCII 字符，200 字实际是 8-10 行；与按钮行 FLOATING 钉底之间的视觉关系未明。
- **证据**：
  - 设计文档 §9.3 L273 明确 EPD 240px ÷ 14pt 字体 ≈ 30 列（ASCII）/ 15 列（CJK）。
  - `9b104d1` 描述 200 字未折算成实际行数。
  - 申请书未提供 AI Chat 屏的截图或 ASCII wireframe。
- **影响**：
  - 多行输入框可能与 Send / Clear 按钮行重叠，需要实测确认。
  - 用户在 200 字 vs 150 字 vs 100 字的不同约束下输入体验差异无客观说明。
- **最小修复**：
  1. 申请书补充 1-2 张 ASCII wireframe 或屏截图（EPD 模拟器 / PC 模拟）。
  2. 重新计算多行输入框在 240px EPD 上的实际容纳能力，文档化为"约 8-10 行 ASCII / 4-5 行 CJK"。
  3. 验证表新增："AI Chat 多行输入 + Send/Clear 渲染布局无相互遮挡"。

### 1.10 申请书未列回归风险：本批 10 commit 对第 4-7 轮修复的端到端验证

- **严重性**：Medium
- **位置**：申请书 §4 验证表 "第 4-7 轮修复项回归 ⏸"
- **触发场景**：本批新增 `64ebcb7`（硬件 FIFO 排空）、`d8f0ab7`（时区）、`9b104d1`（多行 + 异步）等会触发多种状态机切换，与前 7 轮修复的"页面边界双清""扫描期间按键取出"等高度耦合。
- **证据**：
  - 申请书 §4 验证表只列 1 项"第 4-7 轮修复项回归"且标 ⏸，未列具体回归点。
  - 第 4 轮 1.1（扫描覆盖层期间输入丢）、第 4 轮 1.2（SCAN_DONE 竞态）、第 5 轮 1.1（页面切时按键丢失）、第 6/7 轮 1.1/1.2/1.3（WiFi Test 异步化等）均已在第 8 轮 1.1 finding 中要求"未回归"。
  - 本批 `64ebcb7` 的"硬件 FIFO 排空"是修复第 4 轮 1.1 的新举措，但第 4 轮 1.1 旧版本又依赖`keypad_clear_chars()` 的软队列清——双清顺序未明确文档。
- **影响**：
  - 不回归意味着本批修复可能重新引入旧 bug。
  - 申请书"修复第 X 轮 1.Y" 描述如果没有可重复的复现脚本 / 测试用例，下一轮评审再次发现同一 bug。
- **最小修复**：
  1. 申请书 §4 验证表展开为清单：
     - 扫描期间按键 → 不应进入下一字段
     - 连按 Backspace 退 WiFi 屏 → 下一屏首帧无残留
     - 双 Shift OR 状态 → 按 1 键字母应该大写
     - Sym 锁跨页 → 进入新页面 Sym 仍锁直到再次按
     - 连接期间按键 → 完成后不进入输入
     - 408 / 401 / 404 → msgbox 错误分类正确
  2. 申请人在真机上拍 1 段视频录制验证首页翻屏，作为评审附件。

---

## 2. 通过项

- **commit id 全列**：10 个 commit 全部按提交顺序列出，符合"评审申请必须带 commit id"约定。
- **commit 描述粒度**：每条 commit 标注"修复第 X 轮 1.Y"或"用户反馈 X"，让评审能快速对照前序评审结果。
- **CA bundle 升级路径清晰**：`23942f6` + `d8f0ab7` 把 1 根扩到 5 根，逐站抓链（`openssl s_client -showcerts`）补充，方法可复现。
- **时区从 PST8PDT 改 CST-8**：解决 16 小时偏移的根因（详见 `issue_list.md` 的"doc-vs-hardware discrepancies"）。
- **Time Sync 按钮**：手动兜底入口，符合"自动 + 手动"双保险的产品原则。
- **异步任务范式统一**（WiFi Test / AI Test / AI Send）：用 FreeRTOS 任务 + UI 轮询，避免 15-30s 主线程冻结。
- **msgbox + 横幅 + 覆盖层 三级弹窗**：分类合理（结果/短暂提示/强制阻塞）。
- **`keypad_clear_chars()` + `keypad.flush()` 双清**：硬件 FIFO + 软件 FIFO 同时清；修饰键独立状态。
- **OpenRouter curl 形状对齐**：`system` + `temperature 0.7` + `reasoning.exclude` + `user.content`；cJSON 正确转义。
- **撤回方案齐全**：10 个 commit 一次性逆序回滚命令可直接复制。

---

## 3. 继承风险（来自前序评审不重复计入）

- 第 4 轮 1.2（SCAN_DONE 竞态，High）——`048ea73` 已尝试修复但仍属于"被合并 commit 的子模块"，拆分后需单独验证。
- 第 6 轮 1.1（WiFi Test 阻塞，High）——`048ea73` 异步化已尝试，但任务生命周期（§1.4）是新风险。
- 第 7 轮 1.3（WiFi Test 反馈，Medium）——`048ea73` 引入弹窗，但弹窗销毁与任务 IPC 路径未明。

---

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. **退回修订**
- [ ] C. 部分接受

**强制修订项**（必须先解决才能进入下一轮评审）：

1. **§1.1 删除 `AI_KEY_DEFAULT` 真实 Key**——安全红线，不可妥协。
2. **§1.2 拆分混合模块 commit**——按本文明细拆 4 个 commit（`d8f0ab7` / `048ea73` / `23942f6` / `9b104d1`）。

**强烈建议修订项**（建议在下一轮评审前完成）：

3. §1.3 真机验证 3 项 ⏸ 任务并补回归记录
4. §1.4 异步任务生命周期文档化
5. §1.7 NVS 首次启动检测 + 迁移路径

**可纳入下一轮评审项**：

6. §1.5 CA bundle 维护机制（与 `ca_bundle_check.sh` 一起提交）
7. §1.6 状态栏时间刷新策略明确
8. §1.8 UA 头泛化
9. §1.9 AI Chat 布局 wireframe
10. §1.10 回归清单展开

> 建议申请人按"先 §1.1 → §1.2 → 重新提交本轮评审" 的顺序处理，避免一次改动与多个 finding 互相干扰。

---

**评审人**：Claude（allinone-design / pda2 评审视角），已交叉核对申请书与 10 个 commit 的实际 diff（`git show 9b104d1` / `d8f0ab7` / `048ea73` / `23942f6`）。
