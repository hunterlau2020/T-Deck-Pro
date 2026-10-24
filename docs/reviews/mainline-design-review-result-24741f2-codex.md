# 评审结果：mainline 设计 v3 P1-1 修正 delta（Codex）

- **评审日期**：2026-10-24
- **申请文件**：[`mainline-design-review-request-24741f2.md`](mainline-design-review-request-24741f2.md)
- **评审提交**：`24741f2c7761f0131969c051f8031ac01bef1505`
  （父版：`5b32605`）
- **评审范围**：按 `docs/review_guide.md` v1.3 §2.5.3，仅核
  v2→v3 delta：§3.1/§3.2 的判型与字段矩阵、§3.3/§4.1/§4.4 的同批
  P2/P3 修正，以及与服务端实际载荷的契约对照；未重开未改架构。
- **评审快照**：T-Deck 远端只读快照
  `origin/HD-V2-250915@24741f2`；服务端只读工作树
  `/home/debian/english_learning_system@571d29a`。后者存在未提交改动；
  已检查其 `studyplan.py` 改动，仅涉及 cloze 例句/词性选择，未改变本
  报告引用的题型键、字段名或端点判分分支。
- **评审结论**：**L1 DOC-ALIGNED 达标；A，允许 G-开工。**

## Findings

本轮没有未闭合的协议/架构 P0/P1。v3 的最小改动已使原 P1-1 的合法服务端
载荷具有无泄漏、可显示的设备侧表示；因此依 `review_guide.md` §2.5.2，
不得因余下实现合同给 C。

### P2-1：Review 页的 `due_total` 预读来源须在实现期钉死

- **位置**：`docs/mainline-design.md:80,84,97`（`24741f2`）
- **证据（SIM）**：设计要求进入 REVIEW 页先显示 `due_total`、再由 Enter
  开会话；但端点表中 `POST /reviews/lesson/start` 才返回 `due_total`。
  服务端 `backend/app/api/v1/endpoints/plan.py:467-496` 在该 POST 内先创建
  `LearningSession`，不能把它作为无副作用的预读。相反，`GET /plan` 的
  `due_total` 已列于设计 `docs/mainline-design.md:80`，可作进入页的读取源。
- **影响**：若实现为“为显示数量而 POST start”，仅进入页面就创建会话；若
  不预读则不能满足“先显示 due_total”的正文。该行为存在直接、同构的实现
  路径，但不妨碍采用 GET `/plan` 的正确实现，属实现合同而非 L1 阻断。
  后果 B / 可达性 1 / 有声 → **P2**。
- **两轴**：`SIM`；`DOC-PARTIALLY-ALIGNED`（正确来源可由现有端点唯一选出，
  但尚未在架构段明确绑定）。
- **最小修复 / L2 证据**：实现申请须把 REVIEW 进页请求明确为 `GET /plan`
  并读取 `plan.due_total`；真机验证“进入 Review 不创建会话，Enter 后恰创建
  一次”。

### P2-2：Review start 的丢响应恢复策略须在实现期验证

- **位置**：`docs/mainline-design.md:84,97,102-104,157-158`
  （`24741f2`）
- **证据（SIM）**：`POST /reviews/lesson/start` 无 `Idempotency-Key` 参数；
  服务端 `backend/app/api/v1/endpoints/plan.py:467-496` 每次调用都会进入
  `studyplan.review_lesson_session()`。后者在
  `backend/app/services/studyplan.py:2063-2115` 新建会话，且可能在毕业词抽查
  时更新 `graduated_at`。操作序列为：Enter 发 POST → 服务端成功提交 → Wi-Fi
  在响应返回前断开 → 用户重试 Enter；第二次 POST 可创建第二个会话。
- **影响**：会话恢复体验和毕业词抽查频率可能受影响，但正文未承诺 exactly-once
  或离线续课；实现可选择不自动重试、超时后明确提示重新开始，或协调服务端补
  幂等/续会话能力。不是“按正文任何实现均必错”，属实现合同。
  后果 B / 可达性 2 / 有声 → **P2**。
- **两轴**：`SIM`；`DOC-ALIGNED`（设计没有错误地声称该端点自然幂等）。
- **最小修复 / L2 证据**：实现申请声明超时与重复 Enter 的策略；在网络断开
  响应场景真机或探针验证不会静默无限重试，并记录用户可见恢复路径。

## 已通过项

1. **原 P1-1 的字段角色冲突已闭合。**
   `docs/mainline-design.md:62-66` 将显示集按题型拆分。服务端
   `studyplan.py:1527-1538` 的 recognition 载荷中，`word` 是题干、
   `correct_option` 是答案；v3 恰保留前者、丢弃后者。故不再出现 v2 的
   “全局丢弃 `word` 导致题干消失”。
2. **cloze 是一等题型且无答案泄漏。** 服务端
   `studyplan.py:1815-1828` 发出 `sentence_en/sentence_zh/meaning_zh` 及
   `word/cloze_word/cloze_forms`；v3 `:64,110-111` 显示前组、丢弃后组并使用
   textarea。因此合法的高频 cloze 不会再走未知→skipped，也不会显示参考词形。
3. **输出题的双键与实际载荷一致。** 服务端
   `studyplan.py:2020-2028,2046-2055` 用 `segment:"output"` 加 `task`
   （无 `task_type`）。v3 `:48-51,65` 要求先按该双键判型，故输出题不再被
   当作未知 `task_type` 降级。`scene_words` 是服务端完课判据要求可见的脚手架
   （`studyplan.py:2003-2005`），不构成参考答案泄漏。
4. **认读 1-based 提交与判分一致。** v3 `:108` 的数字选择和服务端
   `mastery.py:399-416` 的 1-based index 分支一致。
5. **同批错误路径修订与实际端点一致。** v3 `:82-83,115-116` 已覆盖
   submit/finish 的 `plan_superseded`；服务端分别在
   `sessions.py:105-113,137-145` 返回该 409。输出强制律返回空
   `missing_item_ids`（`sessions.py:155-160`），v3 `:83,112-113` 已规定
   提示并跳回 output 项，而非取空表首项。

## 实现合同跟踪表（L2 / G-合入）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| IC-ML-01 | Review 进页仅 GET `/plan` 读取 due_total；Enter 才 POST start | mainline/review 实现申请（待） | 静态调用链 + 真机进入页不创建会话 | 待 |
| IC-ML-02 | review-start 丢响应/重复 Enter 有明确、有限的恢复行为 | mainline/review 实现申请（待） | 探针或真机断网响应场景 | 待 |
| IC-ML-03 | §3.2 矩阵以 UI/解析器共用常量表实现；判型先 output 双键再 task_type | mainline 实现申请（待） | parser 单元/静态对照 + 真机四类题 | 待 |

## 验证说明

- 执行 `git diff --check 5b32605 24741f2`：无输出，文档 diff 无空白错误。
- 以 `git diff 5b32605 24741f2 -- docs/mainline-design.md` 复核 delta，且
  检查该提交仅变更该设计稿。
- 无 T-Deck 真机，未烧录；本结论为文档/服务端静态契约证据（SIM），不宣称
  L2/L3 或 HW 已通过。

## 对称三格

1. **全区间 diff**：已核 v2 `5b32605` → v3 `24741f2` 的完整
   `docs/mainline-design.md` diff；依 §2.5.3 未重开未变章节。
2. **快照独立复跑**：已只读检查远端 T-Deck 快照与服务端的题型 builder、
   submit/finish、start 路由；无 PlatformIO/设备复现，HW = UNKNOWN。
3. **申请“最没把握”反例**：已构造字段矩阵漂移、cloze 词形、输出空表 409
   三类路径。矩阵与现有 builder 键集一致；词形由服务端
   `mastery.py:419-436` 判分；输出空表由 v3 跳回 output 规则承接。毕业词
   抽查的“输出项 skipped 是否豁免”仍应由实现期 demo 覆盖，未升格为设计 P1。

## 评审自检（指南 §9 适用项）

- [x] 15. 每个 P2 有 `file:line` 与反例操作序列。
- [x] 16–17. 未把作者/服务端声明当作真机证据；静态证据和 HW UNKNOWN 分开标注。
- [x] 20. 以服务端实现复核既有评审分歧，按证据而非票数裁决。
- [x] 21–23. 核对端点字段、JSON 键与错误体；未将计划写成已验真机。
- [x] 24–27. 无协议/架构 P0/P1，给 L1+A；P2 进入上表，不以实现完备性阻断
  v3。

## 审批意见

- [x] **L1 DOC-ALIGNED 达标（G-开工）**
- [x] **A. 接受，据此开工**
- [ ] B. 退回重构
- [ ] C. 部分接受
