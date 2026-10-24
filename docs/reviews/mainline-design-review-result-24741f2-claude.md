# 评审结果（复审）：学习主线 mainline + review 双应用设备端设计 v3 —— P1-1 修正 delta 复审（Claude）

- **评审对象**：`24741f2`（设计 v3）+ `068733f`（本申请）；复审范围 = 申请自限的
  P1-1 所涉 §3/§4/§5 字段矩阵与题型映射 delta + qwen P2/P3 同批项（§2.5.3③，本文件
  另按同条款独立扩面两处未改段，见下）。
- **独立性声明**：评审时未读同轮其他模型（codex/qwen/deepseek/grok）针对本 delta 的
  结果文件（若已存在）；仅读了 `5b32605` 轮（v2）已入库的四份结果 + qwen 的 v2 结果，
  作为"前置分歧"背景（申请 §0 本身引用了它们）。
- **证据口径**：`docs/mainline-design.md` v2→v3 全量 diff（`git diff 5b32605 24741f2`）
  + `~/english_learning_system` 后端源码逐行核对（`backend/app/services/studyplan.py`、
  `backend/app/services/mastery.py`、`backend/app/schemas/learning.py`、
  `backend/app/api/v1/endpoints/sessions.py`）。未起后端进程、未跑
  `remote_api_demo.py`、未编译/真机——与前轮口径一致，设计稿评审本不需要这些。

## 0. 结论先行

**L1 达标。A（接受，据此开工 `ui_mainline` 1.30）**，附实现合同跟踪表（§2）与两条
残余 P2（§1.2/1.3，不挡 L1，绑实现合入）。

v3 的 §3.1（判型双键）+ §3.2（条件化显示矩阵）**同时且正确地**关闭了 v2 轮两条
独立的 P1：

1. 我方（claude，v2 轮改判）与 codex 的 P1："输出题判型键不是 `task_type`"——v3
   §3.1 明文"输出题判型键 = `segment=="output"` + 独立字段 `task`……不得把 `task`
   当未知 `task_type` 误入降级"，并在 §10 固定判型顺序（先查 `segment+task`，
   再查 `task_type`）。**这正是 deepseek 在同轮分歧登记里主张的"最小修复"**
   （把未写的判型键写下来，而非假想级联）——双方分歧因此被设计正文本身消解，
   不需要属主裁决两边谁对，因为 v3 已经同时满足两边的"最小修复"要求。
2. qwen 的 P1-1："平面白名单与题型自相矛盾（`word` 对认读是题干、对拼写是答案）"
   ——v3 §3.2 改为逐题型条件矩阵。

## 1. Findings（已验证修复 + 残余）

### 1.1 已验证修复（非发现，记录验证过程，§0 原则 8）

逐题型核对矩阵是否与后端真实 payload 一致（而不是只核对设计正文内部是否自洽）：

| 题型 | 后端实际 payload（file:line） | v3 矩阵声明 | 核对结果 |
|---|---|---|---|
| `recognition` | `studyplan.py:1528` `_build_recognition_prompt`：`{task_type, word, phonetic, pos, meaning_zh, meaning_en, correct_option, options, tts_text, segment}` | 渲染=word/phonetic/options/pos；丢弃=correct_option/meaning_zh/meaning_en/tts_text | ✅ 字段集完全对应，无遗漏无多余 |
| `recall`（拼写） | `studyplan.py:1438` `_word_prompt`：`is_recall` 分支把 `meaning_en` 服务端掩码为 `____`（:1443），payload `{task_type, word, phonetic, pos, meaning_zh, meaning_en(已掩码), tts_text, segment}` | 渲染=meaning_zh/phonetic/pos/meaning_en(已掩码)；丢弃=word/tts_text | ✅ 一致；`meaning_en` 渲染安全因服务端已替换答案词，矩阵未重复声明掩码前提但结论正确 |
| `cloze` | `studyplan.py:1816-1826`：`{task_type:"cloze", word, phonetic, pos, meaning_zh, meaning_en, sentence_en, sentence_zh, cloze_word, cloze_forms, tts_text, segment}` | 渲染=sentence_en/sentence_zh/meaning_zh；丢弃=word/cloze_word/cloze_forms/tts_text | ✅ 一致（`meaning_en`/`pos`/`phonetic` 未出现在任一列——矩阵隐式丢弃，枚举外丢弃规则兜底，不构成泄漏） |
| `segment=="output"` | `studyplan.py:2018-2029`（`topic_writing`）与 `:2043-2053`（`sentence_making`）：`{segment, task, topic_id?, topic_title?, scene_words[], scene_patterns[], min_scene_words, min_chars}` | 渲染=topic_title/scene_words/scene_patterns/min_scene_words/min_chars/task/segment；丢弃=（无词项答案） | ✅ 一致；`topic_id` 未入矩阵任一列→枚举外丢弃兜底收走，无害（纯内部引用 id，非答案） |

`scene_words` 合法性（焦点 1 答复）单独核验：`mastery.py:926-957`
`finish_session`→`lesson_kind=="output_capstone"` 分支——`output_fallback_ok(user_input,
scene_words)` 是完课判据本身（引用 ≥1 场景词、规范化、≥15 字符），**判据字段必须
对用户可见才能完成任务**，故 `scene_words` 是合法脚手架而非半答案的结论成立。
**但 v3 延用的行号引用 `studyplan.py:1888-1897` 是错的**（该区间是开课并发锁定读
的复查逻辑，与 scene_words 判据无关；真实位置在 `mastery.py` 的
`finish_session`，约 926-957 行）——此错误行号系从 qwen 结果文件原样抄入 v3 正文，
未经复核。**Nit，不挡 L1**，但设计稿正文引用的是错的 file:line，建议开工前一并
订正，否则实现者按此行号去对后端会找错代码。

finish 空表 409（P2-2，qwen）路由到"`segment=="output"` 项"是否对多输出项场景
未定义：核对 `studyplan.py:296-325`（`close_segment`）——每个 `output_capstone`
课时恰好生成 **一个** output ref（`writing_topic` 或 `output_fallback` 二选一，
`refs` 列表长度恒为 1），故会话内 `segment=="output"` 的 `SessionItem` 恒为单项。
`mastery.py:928-933` 的 `output_items = [...]` 列表推导式是防御性写法但现网恒长度
1。**"跳回该 output 项"无歧义，不是未定义行为**，qwen/v3 的处置正确。

开发者自评 ③（skipped 提交输出项是否绕过输出强制律）：核对
`mastery.py:343-377`（`record_submission` 的 `skipped` 分支）——skipped 提交写入
`user_input=None` 的 Submission；`finish_session` 的输出判据取**该项最新提交**
(`mastery.py:936-952`) 调 `output_fallback_ok(sub.user_input or "", scene_words)`，
空字符串必不含场景词 → 判据恒不满足 → `OutputRequirementMissing` 仍会在 finish 时
409。**结论：skip 不能绕过输出强制律**，且 v3 §4.4 的作答枚举本身就没有给输出题
分配 skip 入口（输出题只有 textarea 一条路径，skip 仅对 speak/未知生效）——
该自评担忧在设计层面是**不可达**（device 侧从不对 output 项发 `skipped=true`），
可以从 §8 风险表移除而不是继续挂着（现状是已经移除，核对后确认移除正确）。

### 1.2 残余 P2-A（carry-forward，未被本轮"修正对照表"提及，不挡 L1）

**Review 页进入（Enter 前）如何取得 `due_total`，未声明读来源/时机**——这是 codex
在 v2 轮已给出的 P2-1（见 `mainline-design-review-result-5b32605-codex.md` 对应节），
v3 的 §4.2 仍写"REVIEW（进入即显示 `due_total` + Enter 开会话；due=0 显示 'no
review due'）"，与 v2 原文逐字相同，未补上调用哪个端点、什么时机拉取。由于
mainline/review 是独立 app、独立 worker、不共享在飞状态（§4.3），review app
不能假设复用 mainline HOME 页已经拉过的 `/plan` 结果——必须自己发起一次读（大概率
是 `GET /plan` 取 `due_total`，但正文未写）。**判定**：P2，实现合同缺口（"进入页
发哪个请求"答案已经在既有端点表里，不是协议空白），不挡 L1；但本轮"修正对照表"
只收了 qwen 的批次，codex 的这条 P2 被静默漏了，既没纳入也没在正文/申请里说明
为何不纳入——建议补进下方 IC 跟踪表，否则容易在编码时被遗忘。

### 1.3 残余 P2-B（carry-forward，§4.3 未改段的过时断言，§2.5.3③独立扩面）

§4.3 仍写"两 app 各自独立 worker……**不共享在飞状态，天然互斥（同一账号服务端单
会话语义兜底）**"——该句 v2→v3 逐字未改。但 codex 在 v2 轮的 P2-2、以及本机
`~/.claude/projects/.../memory/feedback_mainline_impl_contracts.md` IC-1 都已核实：
`studyplan.review_lesson_session`（`:2063` 起）**每次调用无条件 `db.add(sess)`
新建会话**，不像 `start_lesson_session` 有"查找既有未结束会话 → 复用"的自然幂等
分支。即："同一账号服务端单会话语义兜底"这句话对 **review** app 不成立——如果
设备侧 Enter 后网络丢响应、用户退出 REVIEW 页（entry/destroy 使 busy 复位，§4.3
规则 8）再重新进入按 Enter，会在服务端产生第二个未结束的 review 会话（不是协议
级崩溃，但与正文自我声明的"天然互斥"矛盾，且会消耗一次毕业词抽查混入机会——
`included_count` 语义因此可能跟设备侧预期脱节）。

**判定**：这条在 v2 轮已经被 codex 点出、且本机项目记忆已经记成 IC-1——是已知的
实现合同缺口，不是本轮新发现，**不升级为 P1**（§9 自审 26：NO_CONTRACT/同构缺口
不得被误升；这里"服务端不提供幂等"是既有事实，最小修复 = 客户端自己做防重入
或接受"review 允许多会话、靠 due_total 下降去重"，都是实现选择，不触及签名/
回滚/密钥/TLS/变砖负面清单）。**但 v3 既然已经在改 §4.3 周边措辞做批次收口，却
把这句明知有问题的断言原样留下，建议借这轮一并订正**（例如改成"mainline start
自然幂等；review start 每次新建会话，无服务端幂等兜底，重入防护由设备侧 busy+gen
承担"），否则这句话会继续误导下一个读设计稿决定"要不要加客户端防重入"的实现者。

## 2. 实现合同跟踪表（§2.5.2，绑实现 commit 的 L2/G-合入）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| IC-11 | §3.2 矩阵落地为 UI/解析器共用常量表；判型顺序先查 `segment+task` 再查 `task_type` | `ui_mainline` 1.30 申请 | 对照矩阵逐型单测/串口抽样 | 待 |
| IC-12 | REVIEW 页进入时机发 `GET /plan` 取 `due_total`（P2-A，§1.2） | 同上 | 串口日志见该请求在 REVIEW `entry()` 发出 | 待 |
| IC-13 | review start 无服务端幂等；设备侧需 busy+gen 防重入兜底，且 §4.3 措辞订正为不再声称"天然互斥"（P2-B，§1.3） | 同上 | 代码审查 + 真机"进→退→进→Enter"手动复现一次 | 待 |
| IC-14 | 设计稿 scene_words 判据行号引用订正为 `mastery.py` 实际行（§1.1 Nit） | 文档 patch（可随实现 commit 带一行） | diff 核对 | 待 |

## 3. 验证说明

- **VERIFIED（SPEC + CODE，file:line）**：§1.1 全部四行题型对照表、scene_words
  判据、空表路由单项性、skip 不绕过输出强制律——均为后端源码直接读取，非推断。
- **CLAIM_ONLY（未起服务）**：`remote_api_demo.py` 实采抽样未做；真实网络时序下
  "退出再进入"竞态（§1.3）未在真机或集成环境复现，是静态代码路径推导，不是
  运行观测——结果文件标注为**推导证据**，不升级为"已复现"。
- **未执行**：PlatformIO 编译、真机、后端进程启动——与历次设计稿评审口径一致。

## 4. 对称三格（§7.2）

1. **范围**：核了申请自限的 §3/§4/§5 delta 全文 + qwen 批次全部 6 条逐条字段级
   核对；额外按 §2.5.3③独立扩面核了 §4.3（未改段），因为我方持有独立证据
   （review 非幂等）落在这一段。
2. **独立复核**：`git diff 5b32605 24741f2 -- docs/mainline-design.md` 全量读取
   （未只读申请列出的修正对照表摘要）；`studyplan.py`/`mastery.py`/`schemas/
   learning.py`/`sessions.py` 对应函数逐行读取，非 grep 摘要臆测。
3. **申请焦点**：矩阵字段级核对 = 直接回应申请焦点 1（HW-MAINLINE-1 完备性）；
   空表 409 单项性 + skip 不绕过 = 回应焦点 2（409 组合是否卡死，答案：不卡死，
   且 skip 路径不是旁路）。

## 5. §9 自审清单（适用 D/ALL）

- [x] 21 字段声明位置（设计矩阵）vs 实际读取位置（后端 builder 函数）逐型核对一致。
- [x] 22 本稿无需手动解析的示意代码块；矩阵以 Markdown 表给出，已核对可执行性
     （落地为常量表的方式在 §10 已写明）。
- [x] 23 未把计划/申请里的 ✅ 当证据——修正对照表每行"✅"本身我都回去核对了一遍
     后端事实,不是直接采信申请的自我报告。
- [x] 24 本轮核实后无新增协议/架构 P0/P1 → 给 L1+A，P2 全部进 IC 表，未因"还能
     更完整"降级。
- [x] 25 复审默认只核 diff 声明的改动面；§4.3 扩面有独立证据支撑（§1.3），非
     随意重开未改段。
- [x] 26 review 非幂等（P2-B）是既有 NO_CONTRACT 缺口，判定未升级为 P1——理由
     见 §1.3（最小修复不触负面清单,是实现选择）。
- [x] 27 矩阵/判型顺序是设计正文的 Markdown 表与一句判型顺序声明,不是对未来
     C++ 实现做 ABI 级假想推演。

## 6. 审批意见

- [x] **L1 DOC-ALIGNED 达标**
- [x] **A. 接受，据此开工（`ui_mainline` 1.30 编码）**
- [ ] B. 退回重构
- [ ] C. 部分接受

本轮 v2→v3 的两条独立 P1（我方/codex 的判型双键缺失 + qwen 的平面白名单自相
矛盾）均已被 §3.1/§3.2 的修订正确、且与后端源码逐字段核对一致地关闭。残余为
两条 P2（§1.2/1.3，均系 v2 轮已被 codex 指出但本轮"修正对照表"未纳入的遗留项，
非新发现）与一条 Nit（引用行号），全部收入上方 IC 表，绑 `ui_mainline` 1.30
实现 commit 的 G-合入，不倒逼 v4。与 v2 轮 deepseek 登记的分歧（NO_CONTRACT
是否应升级为 P1）因 v3 已把判型键明文写入正文而消解，不再需要属主就那条分歧
本身裁决；但 §1.2/1.3 两条 P2 建议属主过目一次，确认同意以 IC 形式带入实现轮
而非现在补版（与我方判断一致：均非负面清单触发项）。

---

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BBTX8xR86SK6gFxrCJ7Tji
