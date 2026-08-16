# 第 21 轮评审合并申请评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..538e6d0.md](wifi-config-keyboard-review-request-844a907..538e6d0.md)
- **关联 commit**：`844a907` `9c075c5` `7fec0e5` `e31cd06` `9b376da` `e60b2e8` `8461f65` `a6388b0` `e1b2d0f` `06f9235` `23063b0` `5dd8e32` `9a89cdd` `ba31181` `35e9eae` `74c24ff` `538e6d0`
- **评审依据**：
  - [主评审 eecebda..ceade9c](wifi-config-keyboard-review-result-eecebda..ceade9c.md)（部分接受，11 Findings）
  - [Copilot 复审 eecebda..ceade9c](wifi-config-keyboard-review-result-eecebda..ceade9c-copilot.md)（退回修订，10 Findings）
  - [主评审 844a907..e1b2d0f](wifi-config-keyboard-review-result-844a907..e1b2d0f.md)（部分接受）
  - [Copilot 复审 844a907..23063b0](wifi-config-keyboard-review-result-844a907..23063b0-copilot.md)（退回修订，10 Findings）
  - [本评审与同日早先 AI 交互 UX 评审/产品体验评审独立](allinone-design-ai-ux-flow-review-result.md)
- **评审结论**：**部分接受**（P0 Sleep 必须追加 commit / 等真机实测后补登）

---

## 1. Findings

### 1.1 P0 Sleep 三项不可逆操作的真机实测仍 ⏸，且 frame-wait 实现尝试了两次

- **严重性**：**High**（阻塞级别）
- **位置**：申请书 §4 "P0 Sleep（5/6/7，不可逆操作）"
- **当前状态**：
  - `7fec0e5` 引入 `disp_full_refr_wait()` + 5s 等待 + `lv_task_handler` 泵。
  - `9a89cdd` 反其道：删除 `disp_full_refr_wait()`，改用"帧序号 + 50ms watcher timer"，注释明示原方案"重入 LVGL、不再等待旧屏帧"。
  - P0 三项实测均标 ⏸。
- **触发场景**：用户在 Sleep 屏点 Sleep → 提示画面 3s → `esp_deep_sleep_start()` → 全局休眠；任何实现错误都会导致：
  - 提示画面根本不出现（倒退到评审 round 17 `bcca4d2` 修过的原 bug）
  - EPD 全刷 1-2s 被吃进倒计时，导致实际可用窗口 < 3s，用户未看清就深睡
  - 倒计时仍在倒但设备已黑屏，用户以为死了
- **证据**：
  - 两次 attempt 都在本批 commit list，`7fec0e5` 后 `9a89cdd` 反向修正——表示前次有未识别的问题或评审 sync 时改了策略。
  - §4 P0 测试 5/6/7 全部标 ⏸ 待用户配合。
  - 申请书 §5 遗留项明确"已连续多轮 ⏸，P0 Sleep 三项必须在合并前完成"。
- **影响**：
  - **不可逆操作的 ⏸ 实测是绝对阻塞项**——frame-wait 逻辑涉及硬件时序 + LVGL tick 上下文 + EPD 波形，没有代码路径能在主机上完全仿真，必须真机 240×320 EPD 实际烧录后观察。
  - 即便本批通过评审合并到 master，未实测的 P0 在用户首次点 Sleep 时可能再吃进深睡。
- **最小修复**：
  1. 申请人必须真机实测 §4 P0 三项并把结果回填到本申请 / `docs/qa/`：
     - 实测视频或 3 张图（"进入提示画面""倒计时中""深睡"）
     - 串口日志 `[EPD] refresh strategy` 与本批选用的 frame-sequence 方案的对应
     - 实测 pause 倒计时 2→1 是否落在提示画面完全显示之后
  2. 不能用"按 BOOT 唤醒"当作测试完成——BOOT 唤醒是 `ext0` 部分唤醒，与 Sleep 提示画面的时序独立验证。

### 1.2 双槽原子保存 + 测试规格文档化，但缺少自动化测试触发

- **严重性**：Medium
- **位置**：`844a907` 双槽保存 + `538e6d0` 测试规格对齐 + `a6388b0` 新建 `tests/test_nvs_atomic_save.md`
- **当前实现**：
  - NVS 用 `active` + `slot N`（N∈{0,1}）双槽 + 单次 `putUChar("active")` 翻转。
  - `tests/test_nvs_atomic_save.md` 列 10 条失败注入用例。
- **触发场景**：NVS 写过程遭遇断电 / 提交点失败 / 双槽被覆盖。
- **证据**：
  - 申请书 §2.1 列了 spec 的 10 条用例（"含掉电提交点"）——但 `tests/test_nvs_atomic_save.md` 是否被任何脚本运行（CI / pre-build / 单元测试）未提。
  - `8461f65` `ca_bundle_check.sh` 有"启动即查 openssl"——明显可运行的脚本风格；而 `test_nvs_atomic_save.md` 是 markdown 文档而非脚本。
  - 嵌入式 NVS 双槽保存的失败注入只能在 host 仿真或 NVS shim 层 mock 才能自动化；`tests/` 目录仅放了 spec 文档却没有 `test_nvs_atomic_save.cpp` / `pytest` 等真正可执行的入口。
- **影响**：
  - 规格不被执行则 10 条用例只能靠人工逐条模拟，可能漏掉"提交点失败后旧槽数据存在但 inactive"的关键回归。
  - 与 mainline ESP-IDF NVS 库升级路径未明——`Preferences` 升级是否会改变行为？
- **最小修复**：
  1. `tests/` 下加 `test_nvs_atomic_save.py` 或 `test_nvs_atomic_save_host.cpp`（用 ESP-IDF 提供的 `nvs_flash.h` host build），至少把 10 条用例中 3 条关键（掉电 / slot 冲突 / commit failure）做成可自动跑。
  2. `tests/test_nvs_atomic_save.md` 在 spec 文档顶部加 "this spec is enforced by `scripts/test_nvs_atomic_save.py`" 引用。
  3. `ca_bundle_check.sh` 在 `pre-build` 钩子中已挂；`test_nvs_atomic_save.py` 同步挂入。

### 1.3 NVS 写放大：每次响应都 putBytes 整 blob

- **严重性**：Medium
- **位置**：`74c24ff` AI usage stats - mutex-guarded single-blob accounting
- **当前实现**：mutex 串行化累加 8 项指标 → 一次 `putBytes` 整 blob 提交到 NVS `ai_stats`。
- **触发场景**：每次 AI 响应 / 每次 Test ping / 每次重试都触发 NVS 写。
- **证据**：
  - 申请书 §2.10 "每次响应**一次** putBytes blob 提交（magic 校验），持久化失败记日志且 RAM 总数不丢"——这是 NVS 写放大点。
  - ESP32 NVS 单次 commit 100-300ms（取决于键数与数据量），且会触发 wear-leveling——即便 NVS 用 `Preferences` 抽象 + flash 内部 wear-leveling，长期每日数百次响应会缩短 flash 寿命。
  - "Test ping 也计入"——意味着用户在反复试错时会成倍触发 NVS 写。
- **影响**：
  - Flash 寿命：NVS 频繁写 → 闪存 stress；Test ping 频繁触发加剧。
  - 延迟：每次 200-300ms NVS commit 在关键路径上（响应解析后立刻 commit）会拖延 UI 反馈。
- **最小修复**：
  1. 加"节流"：把 ai_stats 持久化改为"每分钟最多 1 次 / 每 N 次响应 1 次 / 用户按 New 时 1 次"——比"每次响应"少几个数量级。
  2. 把 ai_stats commit 移到异步任务（独立低优先级 FreeRTOS task），不阻塞响应解析线程。
  3. 申请书 §5 遗留项已标注"NVS 写放大"持续跟踪——本评审再次确认。

### 1.4 AI Test ping 计入 usage 计数，但用户可能不知道

- **严重性**：Low
- **位置**：`06f9235` `accumulate response usage`、与 `57356fa` 的"Test 用草稿 Key 请求"组合
- **当前实现**：每次响应（含 Test /models ping）都解析 usage 累加到 `ai_stats`。
- **触发场景**：用户在 AI Cfg 改字段 → 反复 Test 验证 → 反复扣费 → totals 累加。
- **证据**：
  - 申请书 §2.10 "Test 的 ping 也计入（真实计费）"——申请人主动标注，但是"Test 计入"是产品决策，需要在 UI 上明示。
  - `e60b2e8` AI Config 文案已经加了 `costs ~1 token (network+auth only)` 标注，但 totals 累加是隐式行为，用户不会主动关联。
- **影响**：
  - 用户看到累积 totals 100K tokens 时会困惑"我没问几次怎么这么多"——是因为反复 Test。
  - 监管 / 计费争议：Test 计费是 OpenRouter 默认行为，但用户很可能把 Test 当 free diagnostic。
- **最小修复**：
  1. ai_stats 拆为两块：`ai_stats.test`（Test 专用累计）与 `ai_stats.chat`（聊天专用累计）；UI 显式分两行展示。
  2. 在 AI Cfg 屏 Test 按钮旁加 "Test counts to usage" 提示。
  3. 提供 "Reset test usage" 按钮（独立于 Reset chat usage）以便用户清 Test 累计。

### 1.5 Hist → New 改名一致性 — 仍可能存在残留 "Hist" 字样

- **严重性**：Low
- **位置**：`23063b0` 改名 + `35e9eae` 确认弹窗 + `9b376da` 引入 Hist 按钮
- **当前实现**：History 按钮改名 New，确认弹窗 + 键盘路径（Enter=OK、任意其他键=Cancel）。
- **触发场景**：用户读到旧文档 / 旧截图 / 旧注释。
- **证据**：
  - 申请书 §2.11 "UI 文案、代码注释、本申请回归清单全部改用 New"——claim 是已清理全部。
  - 没有文档说明是否清除了：
    - 旧 docs/review 截图中的 "Hist" 字样
    - `TODO.md` / `docs/issue_list.md` / `docs/allinone-design.md` 中提及 Hist 的描述
    - `tests/` 下任何测试 fixture 引用
    - 串口日志 `[AIChat] Hist ...` 类旧符号
- **影响**：
  - 文档/UI/代码存在语义不一致会让未来开发者复制旧版本。
- **最小修复**：
  1. 在 commit 列表中加一条 `grep -rn "Hist" docs/ examples/ tests/ scripts/` 的输出（或在 `a6388b0` 文档 commit 中固化一个 `docs/reviews/symbol-audit.md` 类似清单）。
  2. 若发现残留"Hist"引用，明确登记在本批的清理范围内。

### 1.6 单轮 → 多轮语义转变的衔接措辞

- **严重性**：Low
- **位置**：`e1b2d0f` AI Chat 多轮上下文
- **当前实现**：
  - 把"最近 8KB 历史"整轮次快照进任务；
  - `openai_chat` 保留为单轮包装（Test 用），`openai_chat_multi` 新加多轮；
  - 排除 pending 气泡。
- **触发场景**：用户在文档 / 用户判断"AI 是单轮问答还是多轮对话"上有预期分歧。
- **证据**：
  - 申请书 §2.9 写"此前历史只在本地渲染/持久化，API 请求固定为 `[system, 当前user]`，模型对会话无记忆"——之前是单轮。
  - 同日早先 AI 交互 UX 评审 [`allinone-design-ai-ux-flow-review-result.md`](./allinone-design-ai-ux-flow-review-result.md) §1.7 把"AI 对话实际是无上下文的单轮问答"列为 Medium finding，建议明确选择单轮 vs 多轮——本批 `e1b2d0f` 直接走了"多轮"路线，但 allinone design 文档还没同步更新。
- **影响**：
  - allinone 设计文档 §4 AI 对话屏交互仍说"无上下文"，与现 pda2 多轮实现可能不一致（allinone 是设计文档，pda2 是参考实现，二者最终一致即可但需要同步）。
- **最小修复**：
  1. 在 `docs/allinone-design.md` 加一行更新："AI 多轮上下文已在 pda2 预研实现（commit `e1b2d0f`），allinone 移植时使用 `openai_chat_multi` 而非 `openai_chat`"。
  2. pda2 README 同步：在 §AI Chat 段加"API 请求 = system + 历史 (8KB snapshot) + 当前 user，多轮对话"。

### 1.7 `35e9eae` "孤立 assistant 停止窗口" 边界 UX 说明

- **严重性**：Low
- **位置**：`35e9eae` AI Chat atomic log + turn pairing
- **当前实现**：快照按"整轮（user+assistant 成对）"选择；孤立 assistant 跳过；孤立 confirmed user 尾部允许。
- **触发场景**：用户使用 New 清空历史后立即发新一轮；或网络中断导致 assistant 回复丢失。
- **证据**：
  - 申请书 §2.13 列 "孤立 confirmed user 尾部允许"——意味着孤立的 user 会被选进上下文。
  - 申请书 §4 P1 #15 "轮次配对：多轮对话后串口 `send: N context msgs` 恒为偶数（除尾部未回复的 user）"——已明示偶数规则，但用户看不到这条规则文档。
- **影响**：
  - 用户调试"为什么 AI 引用了不存在的上一句"时会困惑。
- **最小修复**：
  1. 在 `docs/async_ipc_contract.md` AI Chat 段加 "context turn pairing rule" 子节。
  2. 串口日志加 `send: 3 context turns (1 pending user)` 让 pending user 数也明示。

### 1.8 SPIFFS 不自动格式化 — 用户误操作无救

- **严重性**：Low
- **位置**：`35e9eae` AI Chat atomic log → `SPIFFS.begin(false)`
- **当前实现**：`SPIFFS.begin(false)` 永不自动格式化，挂载失败时降级 RAM-only；`/chat.log` 用临时文件 + rename 写入；损坏整体丢弃。
- **触发场景**：SPIFFS 因长期写入 / 突然断电产生损坏 → 挂载失败 → 后续 history 全丢 RAM-only → 用户重启 → 历史清空。
- **证据**：
  - 申请书 §2.13 "SPIFFS 永不自格式化" 与 "撕坏的日志整体丢弃" 都在 commit message 中明示。
  - 用户侧没有 "我了解 SPIFFS 损坏可能丢历史" 的提示。
- **影响**：
  - 新策略比自动 format 更安全（自动 format 可能擦掉其他配置），但用户对"日志可能丢"无预期。
- **最小修复**：
  1. AI Chat 屏底部状态行增加 1 行 `Storage: SPIFFS OK / RAM-only (log will not persist)`。
  2. README / docs 标注"chat.log 在 SPIFFS 损坏时会降级 RAM-only，重新烧录或外部格式化可恢复"。

### 1.9 关键路径 doc 已多源记录，README 索引仍是单点

- **严重性**：Low
- **位置**：`a6388b0` 引入 `docs/reviews/README.md`
- **当前实现**：新建 README 描述合并流程。
- **触发场景**：未来开发者 / Claude 找不到评审文件的命名约定 / 申请合并规则。
- **证据**：
  - 申请书 §2.8 "新建 `docs/reviews/README.md`：申请合并流程（何时合并、范围命名、`git show` 追溯方式）"——claim 完成。
  - 没有跨文档链接：CLAUDE.md 中"`docs/reviews/README.md` 才是权威"的指向是否生效需检查。
- **影响**：
  - 文件命名约定（`review-request-<hash>.md` / `review-result-<hash>.md` / `-copilot.md` 后缀）若 README 没有写明，开发者会凭直觉命名。
- **最小修复**：
  1. 验证 `docs/reviews/README.md` 实际内容：是否包含命名约定 / "-copilot.md" 后缀 / 申请与结果应同步提交等条款。
  2. CLAUDE.md "Code review workflow" 段末尾加链接到 `docs/reviews/README.md`。
  3. `git grep -l "wifi-config-keyboard-review-request" examples/` 与 `git grep -l "issue_list" examples/` 做一次冗余检查。

### 1.10 Key 仍在 repo，按用户决策延后（本评审机制限制）

- **严重性**：**项目自有决策**（不阻塞）
- **位置**：`examples/pda2/openai_api.h:30` `AI_KEY_DEFAULT` 仍存在
- **当前状态**：申请人按 `api-key-dev-exception` 决策保留真实 Key，C1/C2 已落地（C1 编译期 `#warning`、C2 `SECURITY.md`）；C3（用户侧 free-tier 轮换）由用户承诺。
- **评审结论**：按项目 CLAUDE.md 与 memory 的 `api-key-dev-exception.md` 文件，本评审**不视为阻塞项**。但本评审仍建议：
  1. 跟踪：OpenRouter 后台是否已 revoke？reviewer 无法验证。
  2. 推公网前必做：删 Key 字符串、移除 `-DAI_KEY_DEFAULT_COMPILED`、filter-repo 清理历史、按 `SECURITY.md` 4 步走——已在 `5dd8e32` 文档化。
  3. 未来轮次中此项不应再作为 High severity 反复出现。

### 1.11 17 个 commit 的合并申请 — 体量大但拆分清晰

- **严重性**：Low（观察性）
- **位置**：申请书 §5 回滚命令 `git revert 538e6d0 .. 844a907`
- **当前状态**：17 个 commit 按模块拆分（双槽保存 / 扫描 / Sleep / WiFi busy / AI Chat / AI Config / CA 脚本 / 文档 / 多轮 / usage / New / Key / frame-wait / 临界区 / atomic log / stats / test spec）。
- **触发场景**：reviewer 阅读负担、回滚选择性、cherry-pick 到 allinone。
- **证据**：
  - 17 个 commit 跨多模块但每个 commit 对应 1 个明确主题——这是好的拆分。
  - 第 21 轮本身就是 20 轮的扩展（申请书 §0 列出 `f1f5965` `331c6f9` `17a8107` 等 doc-only commit 作为范围延伸）——隐含"未来轮 review 申请可能横跨多个评审轮次"的合并模式。
- **影响**：
  - 单次评审需处理 17 个 commit 跨 6+ 模块的问题，单 reviewer 认知负担重。
  - 长 commit 列表的回滚命令需要按正确顺序——申请人已按提交逆序给出，避免回滚冲突。
- **最小修复**：
  1. 在 `docs/reviews/README.md` 中明确"单次合并申请 commit 数 ≥ 10 时应拆分为多个 doc commit 段（每段 5-7 个），分别走 review 流程"——避免下一轮合并申请更大。
  2. 在评审流程中加入"每 N 个 commit 后强制 doc-only checkpoint commit"，使后续定位 commit 范围更清晰。

---

## 2. 通过项（申请书整改对照）

| 上轮 Finding / 用户需求 | 申请书整改 | 评价 |
|---|---|---|
| **Cop 1.2 High** 双槽原子保存 | ✅ `844a907` + `538e6d0` 规格同步 | 真正落地双槽 + 校验，规格已文档化 |
| **Cop 1.3 High** 扫描临界区 | ✅ `9c075c5` + `ba31181` 读也临界区 | 两轮整改，方向正确（详见 §1.x） |
| **Cop 1.4 Medium** Sleep 等待 | ✅ `7fec0e5` → `9a89cdd` frame-sequence | 改了两版——frame-sequence 更稳（详见 §1.1） |
| **Cop 1.5 Medium** WiFi busy clear on leave | ✅ `e31cd06` | 契约第 8 条已遵守 |
| **主 1.2 High + Cop 1.6/1.7/1.8/1.9 Medium** AI Chat 重做 | ✅ `9b376da` | 动态正文 + SPIFFS + retry reuse 三连改 |
| **Cop 1.10 Medium** CA 脚本 openssl 检查 | ✅ `8461f65` | fail fast + 操作提示 |
| **主 1.8/1.9/1.11** 文档 | ✅ `a6388b0` | reviews/README + test spec + 归档 |
| **Cop 1.4 High** chat.log 原子写 | ✅ `35e9eae` | 临时文件 + 校验和 + rename |
| **Cop 1.5** failed/pending 持久化 | ✅ `35e9eae` | pending 不进日志 / `(failed)` 永不选中 |
| **Cop 1.6** 轮次配对 | ✅ `35e9eae` | 整轮不拆 / 孤立 assistant 跳过 |
| **Cop 1.7** New 确认 | ✅ `35e9eae` | Cancel/OK 弹窗 + 键盘路径完整 |
| **Cop 1.9** 统计并发 | ✅ `74c24ff` | mutex + 单一 RAM 结构 |
| **主 1.1 C1/C2** Key 补偿 | ✅ `5dd8e32` | `#warning` + `SECURITY.md` + 文档化 |
| **用户追加** 多轮上下文 | ✅ `e1b2d0f` | `openai_chat_multi` + 8KB 快照 |
| **用户追加** usage 统计 | ✅ `06f9235` | 容错解析 + 8 项 NVS |
| **用户追加** New 改名 | ✅ `23063b0` | 文案同步 |

---

## 3. 阻塞清单（必须先解决）

| 阻塞项 | 严重性 | 必须满足 |
|---|---|---|
| §1.1 P0 Sleep 5/6/7 真机实测 | High | 实测视频 + 串口日志回填到 §4 |
| §1.2 NVS 双槽测试可执行化 | Medium | `tests/test_nvs_atomic_save.{py,cpp}` 至少 3 条核心用例可运行 |
| §1.3 NVS 写放大节流 | Medium | ai_stats commit 频率方案落地（节流 / 异步） |
| §1.11 17 commit 拆分机制建议 | Low | `docs/reviews/README.md` 加入"≥10 commit 应分段"条款 |

---

## 4. 强烈建议（可纳入下轮评审）

- §1.4 Test ping 与 usage 累计分离
- §1.5 Hist 残留字样审计与清除清单
- §1.6 allinone design §4 与 pda2 多轮实现同步
- §1.7 轮次配对规则文档化
- §1.8 SPIFFS 状态可视

---

## 5. 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**

**接受范围**：本批 17 个 commit 中 16 个 commit 可接受（除 §1.10 Key 项按用户决策外）；具体改动方向合理、模块拆分清晰、与上轮评审 Findings 对应。

**前置条件（合并到 master 前必须满足）**：
- §1.1 P0 Sleep 5/6/7 真机实测完成并补登到申请 §4
- §1.2 / §1.3 / §1.11 在本批合并前给出可执行的提交路径（即使不是本批 commit 实现，也至少在下批 commit 中开始）

**遗留项**：Key 项按 `api-key-dev-exception` 决策延后，跟踪至推公网前。

---

**评审人**：Claude（allinone-design / pda2 评审视角，已交叉核对 `git log 844a907..538e6d0` 的 17 个 commit 实际内容与申请 §2 的对应）。
