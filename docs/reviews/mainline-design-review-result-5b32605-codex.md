# 评审结果：学习主线（mainline + review 双应用）设备端设计 v2（Codex）

- **评审日期**：2026-10-23
- **申请文件**：[mainline-design-review-request-5b32605.md](mainline-design-review-request-5b32605.md)
- **评审对象**：[`../mainline-design.md`](../mainline-design.md) v2，设计锚
  `5b32605`
- **评审范围**：本主题 v1（`4b16209`）未曾获评，本轮按首轮核对全文 §1–§10，
  并复核 v1→v2 的 `4b16209..5b32605` delta。
- **评审依据**：`docs/review_guide.md` v1.3（设计稿口径）。
- **结论**：**L1 未达标，C（须修正文后再过 G-开工）**。存在一项协议/架构
  **P1**：设计定义的答案隔离白名单与当前服务端实际题型载荷矛盾；照正文编码时，
  正常主线题会无题干或被 `skipped`，主线闭环不可用。

> 本结果替代本评审者稍早在未能读取服务端源码时给出的“L1+A”暂定判断；后端路径
> `/home/debian/english_learning_system` 由属主补充后，以下 DOC/CODE/SIM 证据使该
> 暂定判断失效。后端工作树有与本评审无关的未提交改动；本轮严格只读，不写入该仓库。

---

## 区间文件归属

`git diff --check 4b16209 5b32605` 通过。区间共 3 个文件：

| 文件 | 归属 / 处置 |
|---|---|
| `docs/mainline-design.md` | **受评**：v2 双应用设计正文，P1 位于其服务端契约、答案隔离和作答交互段。 |
| `TODO.md` | **受评**：只登记设计评审待办；其中仍写 v1/旧申请名，是随后 `185aa10` 已修正的时点文档，不改变 v2 的协议结论。 |
| `docs/reviews/mainline-design-review-request-4b16209.md` | **受评**：v1 申请归档；现申请已由 `185aa10` 改名/改锚为 `5b32605`，不构成 v2 实现面。 |

当前 T-Deck-Pro HEAD 为 `185aa103376702190fb54c74ffe8443a88f557b2`；设计正文相对
`5b32605` 未再改动。后置同步进本仓的 `scripts/remote_api_demo.py` 与本机
`english_learning_system` 源码仅作服务端契约佐证，**不**归入本设计提交。

## Findings

### P1-1：HW-MAINLINE-1 将所有 `prompt.word` 视为不可进入设备的参考答案，但服务端把它作为认读题题干；合法 cloze / output 的判型和题干字段也不在允许集

- **设计位置**：`docs/mainline-design.md:61-65` 声明解析层丢弃 `word` /
  `correct_option`，渲染白名单只含 `task_type`、`segment`、`options`、
  `scene_words`、`scaffold_level`；`:103-110` 又仅定义认读、拼写、输出三类。
- **服务端证据**：
  - [`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:1527)
    的 `recognition` 载荷以 `word` 为题干、`options` 为可选答案，
    `correct_option` 才是不得显示的判分真值。
  - [`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:1815)
    会产生合法 `task_type="cloze"`，题干/提示在 `sentence_en`、`sentence_zh`、
    `meaning_zh`，而正确填词另在 `word`、`cloze_word`、`cloze_forms`。
  - [`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:1935)
    说明主线热身会生成 cloze，`:1962-1997` 说明非 review-drill 主线后半段也优先
    生成 cloze；不是罕见的未知扩展题型。
  - [`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:2020)
    的输出题以 `segment="output"` 加 `task="topic_writing|sentence_making"` 判型，
    并带 `scene_patterns`、`min_scene_words`、`min_chars`；它没有设计稿所假定的
    `task_type`。
- **SIM（可复现）**：
  1. 服务端返回一题 `task_type=recognition, word="apple", options=[...]`；
  2. 实现严格执行设计的“解析层直接丢弃 word”后，UI 只剩四个释义选项，没有
     “apple”这一题干；
  3. 对正常 `task_type=cloze`，设计的未知类型规则会渲染 `segment` 后发
     `skipped=true`；对 output，因判型键为 `task` 而非 `task_type`，同样误入降级；
  4. 主线课的题目因而无法按其教学语义作答，或被系统性跳过。
- **定级**：后果 B（设备与服务端题型契约破坏，主线核心闭环失效）× 可达性 1
  （正常开课即可遇到）= **P1**。最小修复必须重写承重的字段/题型规范，不能仅在
  实现时“猜着兼容”；故按指南 §2.5.1 阻断 L1。
- **Required action（改设计正文）**：将 HW-MAINLINE-1 改成**按题型的最小
  允许集 + 显式禁止集**，并让 UI 与解析器共用该表：

  | 题型 | 可进入/可显示的题干 | 必须解析即丢弃（不可日志/不可上屏） |
  |---|---|---|
  | `recognition` | `word`、`phonetic`、`options[]`、可选 `pos` | `correct_option`、`tts_text` |
  | `recall` | `meaning_zh`、可选 `pos/phonetic` | `word`、`tts_text` |
  | `cloze` | `sentence_en`、可选 `sentence_zh/meaning_zh/pos` | `word`、`cloze_word`、`cloze_forms`、`tts_text` |
  | `segment=output` + `task` | `topic_title`、`scene_words`、`scene_patterns`、`min_scene_words`、`min_chars` | 任何词项参考答案字段；不得把 `task` 当未知 `task_type` |

  `scene_words` 在输出题是服务端明示的写作判据，而非唯一参考答案：后端用它判断
  “至少引用一个场景词、长度至少 15”（[`mastery.py`](/home/debian/english_learning_system/backend/app/services/mastery.py:890)）。可显示，但应以该语义写入设计。

### P2-1：Review 进入页在尚未 Enter 时没有声明 `due_total` 的读来源

- **位置**：`docs/mainline-design.md:55` 仅把 `GET /plan` 的 `due_total` 标为
  mainline 进度字段；`:89-91` 却要求 Review 一进入即显示它、Enter 后才允许
  `POST /reviews/lesson/start`。
- **证据**：后端 `GET /plan` 确实给出 `due_total`
  （[`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:698)）；
  Review start 则立即创建会话（[`plan.py`](/home/debian/english_learning_system/backend/app/api/v1/endpoints/plan.py:467)）。
- **最小修复 / L2 合同**：Review entry 明确发只读 `GET /plan`，从
  `plan.due_total` 渲染；`plan:null` 或 `due_total=0` 显示 no-review-due，绝不为取数
  提前 POST start。

### P2-2：Review start 不是自然幂等；网络丢响应会创建不可见的新会话并消耗毕业项抽查机会

- **位置**：设计 `mainline-design.md:95-99,124-125` 将两个应用概括为同一类单飞/
  断电重开语义，但没有区分 Review start。
- **证据与 SIM**：mainline 的 start 的确“同课未结业会话复用”，所以“自然幂等”
  对它成立（[`plan.py`](/home/debian/english_learning_system/backend/app/api/v1/endpoints/plan.py:398)）。
  Review start 却每次新建 `LearningSession`（[`studyplan.py`](/home/debian/english_learning_system/backend/app/services/studyplan.py:2063)），且开课时即重置被抽查毕业词的时间
  （`:2087-2102`）。若 POST 已提交、Wi-Fi 在响应前断开，用户重试会创建第二个
  session，第一条抽查记录已被消耗但用户从未看到该题。
- **最小修复 / L2 合同**：实现前二选一并用服务端测试固定：为 Review start 增加
  用户级未结业会话复用/Idempotency-Key，或 UI 明确不自动重试、失败后由服务端提供
  “继续未结业 Review”读取路径。两 app 的独立 worker 不等于账号级会话互斥，也应在
  该策略下写清。

### P2-3：输出强制律的 finish 409 不是“跳回首个未提交项”场景

- **位置**：设计 `mainline-design.md:108-110` 将 finish 409 统一为
  `missing_item_ids` 后跳首个未交项。
- **证据 / SIM**：服务端在题目已全提交、但 output 未达到“至少一个场景词且至少
  15 字符”时，返回 409 与 `missing_item_ids: []`
  （[`sessions.py`](/home/debian/english_learning_system/backend/app/api/v1/endpoints/sessions.py:155)）。此时没有“首个未提交项”可跳。
- **最小修复 / L2 合同**：识别空 `missing_item_ids` 加输出强制律错误消息时，定位到
  首个 output item，保留 textarea 内容、显示服务端消息并允许重提；未知 4xx/5xx
  则通用报错 + 可 Back 退出。该项不需要新设计版。

## 已通过 / 已修正的初审判断

- `plan_superseded` 在 submit 和 finish 都会返回同一 `reason`，因此设计
  “仅 mainline → 提示并回 HOME”的方向可满足（[`sessions.py`](/home/debian/english_learning_system/backend/app/api/v1/endpoints/sessions.py:105) 与 `:137`）。
- mainline start 的“自然幂等”由后端未结业会话复用保证；先前把它列为 P2 的判断撤回。
- `scene_words` 不是隐蔽的 `correct_option` 等价物；它是输出题的显式作答要求。问题在于
  设计没有把它与 `task`、输出题型的其他安全字段一起定义，而非应当删除它。
- 设备不本地判“每日”且不缓存 session 的边界，未与服务端的计划/到期计算冲突。

## 验证说明

- **VERIFIED（DOC/CODE/SIM）**：设计正文、`4b16209..5b32605` 文件范围、T-Deck-Pro
  的既有 `pp_request` 传输链；以及本机后端的端点、生成器、评分器的行级静态交叉核对。
- **未执行**：没有启动后端、没有以真实 API key 跑 `remote_api_demo.py`，没有 PlatformIO
  编译或真机；因此真实 payload 抽样、网络丢响应与 UI 行为仍分别为
  `CLAIM_ONLY` / `HW UNKNOWN`。P1 的字段矛盾不依赖这些运行条件，已由同一后端源码的
  生成与评分路径直接证明。

## 对称三格

1. **全范围**：首轮核全文和 v1→v2 delta；额外核对服务端计划、会话、题目生成、评分
   四条承重邻接路径，而非只回答申请列出的五个问题。
2. **独立复核**：`git diff --check 4b16209 5b32605` 通过；逐行读取本机后端源码。
   未把未运行的服务端或真机说成通过。
3. **申请焦点**：HW-MAINLINE-1 被 P1 反例击穿；409 的
   `plan_superseded` 路径已证实、输出强制律 409 补为 P2；双 app 的独立 worker
   不提供会话幂等，收敛为 P2-2；“每日”语义无新增反例。

## §9 自审（适用 D / ALL）

- [x] 21 字段声明与实际读取位置已交叉核对；P1-1 即键集/判型不一致的直接证据。
- [x] 22 设计无可执行 JSON 代码块；端点与字段通过后端实现核对。
- [x] 23 未将后端运行或真机状态伪称为已验证。
- [x] 24–27：P1 的最小修复须改当前承重字段协议，故给 C；其余可行实现选择均降为
  P2 合同，不因完备性而扩大阻断。

## 审批意见

- [ ] L1 + A 开工接受
- [ ] B 退回重构
- [x] **C：修正 P1-1 的设计正文后复审（G-开工暂不通过）**

复审只需核 P1-1 所涉 §3/§4/§5 的字段矩阵与题型映射 delta；P2-1～P2-3 绑后续
`ui_mainline` / `ui_review` 实现申请的 L2/G-合入，不应借其反复扩大全稿评审范围。
