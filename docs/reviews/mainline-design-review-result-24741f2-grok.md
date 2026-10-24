# 评审结果：学习主线设计 v3 delta（Grok）

- **评审日期**：2026-10-24
- **评审人**：Grok（Cursor）
- **申请文件**：[mainline-design-review-request-24741f2.md](mainline-design-review-request-24741f2.md)
- **设计稿**：[../mainline-design.md](../mainline-design.md)（v3）
- **评审提交锚**：`24741f2`（`docs: mainline design v3 - P1-1 fix ...`）
- **工作树 HEAD**：`068733f`（仅追加本申请；`git diff 24741f2 HEAD -- docs/mainline-design.md` 空）
- **服务端树**：`/home/debian/english_learning_system` HEAD `571d29a`（`feature/ux-phase2`）。申请写的 `studyplan.py:1439-1540/1683-1695/1888-1897` 以 **HEAD** 为准。工作树对 `studyplan.py` 等有未提交改动（cloze 选句加多候选），**prompt JSON 键集未变**。
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**（§0–§4、**§2.5 收口强制**、§6、§9【D】、§10）+ 申请声明的 **§2.5.3 delta 复审**
- **复审范围**：`git diff 5b32605 24741f2 -- docs/mainline-design.md` + 申请 §1 修正对照表。`24741f2` 本身只改这一份设计稿。区间内其它 commit（`185aa10` 申请改锚、四方 v2 结果、`633da32` qwen 结果）**不评**。
- **扩面**：无。未改章节不重开。start 行 403/409 笔误位于 **本 delta 改写的 §3.3**，在范围内，不构成扩面。
- **独立性**：开工时 `ls docs/reviews/*24741f2*` 仅申请文件；完稿前同基线出现 `mainline-design-review-result-24741f2-codex.md`——**未打开、不采信**。不打开 `*-review-result-5b32605-{claude,codex,qwen,deepseek}.md` 作为本轮证据。v2 本侧结果 `...-5b32605-grok.md` 只用于合同编号对照与格式，闭合核调用服务端源码 + v3 正文。
- **评审结论**：**L1 DOC-ALIGNED + A**。无未闭合的协议/架构 P0/P1。**G-开工通过**（1.30 `ui_mainline`）。残留项进下方实现合同表，门禁 = 实现 commit 的 L2 / G-合入，**不开 v4**。

---

## 1. 为何本轮给 A（相对 v2 的 P1-1）

v2 平面白名单（丢弃 `word`/`correct_option`、渲染 `task_type/segment/options/scene_words`）与生产装配不可同时满足：认读题干就是 `word`，输出项没有 `task_type`。无论上一轮把它标成协议 P1 还是实现合同，**最小修复都是改正文矩阵**。v3 已经把修复写进承重规范：

- §3.1 判型双键：词项 `task_type` ∈ {recognition, recall, cloze}；输出 `segment=="output"` + `task`；
- §3.2 逐题型允许/丢弃集，UI 与解析器共用一张表；
- §10 判型顺序固定（先 output 再 task_type）。

按 §2.5.1，正文不再「按规范编码必错」。其余缺口（start 的 403 笔误、`PP_ML_ITEMS_MAX=20`、skipped 不豁免强制律）都能在不改架构的前提下按既有模块同构落地，**不挡 L1、禁止因此开 v4**。

---

## 2. 申请对照表闭合（独立回服务端，不采信申请「✅」）

服务端定位默认 `~/english_learning_system` HEAD `571d29a`。行号工作树已漂，下表给 HEAD。

| 申请条目 | v3 正文 | 服务端事实 | 状态 |
|---|---|---|---|
| **P1-1** 白名单 vs 题型载荷 | §3.2 五题型矩阵 | `_build_recognition_prompt`（`:1527-1537`）含 `word/options/correct_option`；`_word_prompt`（`:1438-1456`）recall 含 `word`+掩码 `meaning_en`；`_build_cloze_prompt` 含 `sentence_en/cloze_word/cloze_forms`；输出无 `task_type` | **闭合** |
| **判型双键** | §3.1 + §10 先 `segment==output` | `studyplan.py:1888-1915` 输出项只有 `segment`+`task`；`mastery.py:380` 对无 `task_type` 才默认 `"recall"`——那是**服务端计分**回退，设备不得照抄为 skip | **闭合** |
| **P2-1** cloze 一等题型 | §2/§3.2/§4.4 textarea 与拼写同构 | 新学后半段优先 cloze、热身奇数项试 cloze（`:1937-1997`）；失败回退 recall，不是 skip | **闭合** |
| **P2-2** 空表 409 | §3.3 空表→message + 跳回 output，不取表首 | `OutputRequirementMissing` → `{message, missing_item_ids:[]}`（`sessions.py:155-159`）；仅 `lesson_kind=="output_capstone"`（`mastery.py:927-960`） | **方向闭合**（判别序 → IC-2） |
| **P2-3** submit/finish `plan_superseded` | §3.3 两行 409 → 作废 + HOME | `sessions.py:105-113,137-144`；测试 `test_studyplan.py:1049-1060` | **submit/finish 闭合**；§3.3 **start 行仍写 409 `plan_superseded`**，与 `plan.py:370-375` 的 **403** 不符 → IC-2，不挡 L1 |
| **P3-1** `recommend=null` | §4.1 HOME 四态 | `next_action` 无计划返回 `recommend: None`（`:1395-1396`）；`PlanNextOut.recommend: str \| None`（`schemas/study.py:129-130`）。多出来的 `"next"` 键被响应模型丢掉，线上是 `{recommend:null, lesson:null, ...}` | **闭合** |
| **P3-2** `passed/quality` nullable | §3.3 submit 行 + combined 兜底 | 输出项 `word is None` 早退，无这两键（`mastery.py:483-484`）；schema 默认 `None`（`learning.py:53-54`）；skipped 同样无 | **闭合** |
| **P3-3** 枚举外丢弃 | §3.2 末条 | `LessonStartOut` 顶层 `lesson_id/index/kind/title`（`study.py:82-89`）；复习 `graduated_sampled_word_ids/included_count`（`:92-100`）。丢弃正确，无 prompt 外答案泄漏 | **闭合** |
| scene_words 脚手架 | §3.2 输出行「合法脚手架」 | HEAD `:1871`「判据要求必须可见」+ `:1888-1915` 下发 `scene_words[:20]`。申请/正文写的 `:1888-1897` 在 HEAD 是 topic_writing JSON 块（评注在 1871）；工作树漂到 `:2004` | **闭合**（行号 Nit） |

申请自评三条（§3）：

1. **矩阵 vs 未提交后端**——HEAD 键集与矩阵一致。工作树只改 cloze **选哪句**，不改键。L2 钉后端 SHA（IC-8）。
2. **cloze_forms 本地提示**——服务端 `mastery.py:419-431` 已接受全部 `cloze_forms`。设备只送裸文本不降低可判分空间；本地词形提示是教学法可选项，不是协议。
3. **输出项 skipped 是否豁免强制律**——**不豁免**。skipped 会建 Submission（故不进 `missing_item_ids`），但 `output_fallback_ok("", scene_words)` 为假（`supply.py:262-267`），finish 仍空表 409。反例：输出项 skipped → finish → 再跳回输出项，直到真实作答。→ IC-2。

---

## 3. Findings

无协议/架构 P0/P1。无挡 L1 项。

（实现合同见 §4，不定级为挡开工缺陷。）

---

## 4. 实现合同跟踪表（§2.5.2 强制；绑 G-合入）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| **IC-1** 矩阵落地 | §3.2 表变成解析器与 UI **同一张 C 常量表**。判型在裁剪之前：① `segment=="output"` 即输出题（`task` 缺省当 `sentence_making`，不要因为缺 `task` 掉进未知）；② 再看 `task_type` ∈ {recognition,recall,cloze}；③ 其余 **停在题卡**，禁止自动 skipped（主线装配没有 speak 项）。裁剪后结构体只留该行「渲染」列；**item 信封** `id/target_layer/submitted` 不裁。设备侧保存派生 `kind` 枚举——表里没把 `task_type` 列入渲染列，丢掉原字段后仍要能选渲染器。禁止 `Serial.printf` 开课 JSON 原文。 | 1.30 实现申请 | CODE：结构体无 `correct_option`/`cloze_word`/`cloze_forms`；认读标签能显示 lemma；挖空标签是 `____` 句；output 无 `task_type` 也能进 textarea | 待 |
| **IC-2** 4xx 分发（含自评③） | **按服务端，不按 §3.3 start 行那句「409 plan_superseded」**：`POST /lessons/{id}/start` 计划不匹配 / 无计划 → **403** `"Lesson does not belong to current active plan"`（`plan.py:370-375`）；已完成 → 409 `"Lesson already completed"`（字符串，无 `reason` 键）。**submit/finish**：`reason==plan_superseded` **优先于** `missing_item_ids`（plan_superseded 体 **没有** 该键，缺键不得当 `[]`）。非空表 → 跳首个未交项。空表 → **仅** output_capstone 强制律，跳回**最后一个未达标** `segment==output` 项，显示 `message`。**输出项禁止 skipped 提交**（skipped 仍 409 空表，见自评③）。其它 4xx/5xx → 提示 + Back。 | 1.30 | CODE：四条路径分发；SIM：网页切计划后 start=403 / submit=409；输出 skipped 后 finish 仍 409 且不回 HOME | 待 |
| **IC-3** HOME 门控 | Enter **仅** `recommend=="lesson"` 且 `lesson.id` 有效。`plan:null` **或** `recommend==null` → 「create a plan on web」，不画 done。`recommend==review` 指引行的 N 用 `GET /plan` 的 `due_total`（个数），不用 `due_minutes`。`course_completed ∧ due_total>0` = consolidating，主推复习，不是通关无事。 | 1.30 | CODE：Enter 门控；四态文案；巩固期 | 待 |
| **IC-4** Review 进入 | `due_total` 在 `GET /plan` 的 plan 对象上（`studyplan.py:712,732`）。`entry()` 先 GET；`due_total==0` **禁止** `POST /reviews/lesson/start`（该 POST **每次新建**会话，空到期也会 commit）。1.31 申请核。 | 1.31 | CODE：due=0 零 POST；超时不连打 start | 待 |
| **IC-5** 双 app 并存 | 服务端允许主线未结业会话与复习会话并存（锁只按 `(user, lesson)`）。独立 `ml_*`/`rv_*` worker + gen/busy/队列。跨 app 4xx 走 IC-2。 | 1.30/1.31 | CODE：两队列两 gen；互不释放对方 busy | 待 |
| **IC-6** `PP_ML_ITEMS_MAX` 不得为 20 | §10 仍写 20，**公式已证伪**。热身 `min(cap−新学, cap/2)` ÷ 0.5 min：非成人最多 ~25 热身 + 配额 3–8；成人 cap=45 → 最多 ~45 热身 + 配额。硬拒方向对（禁止静默截断），常数必须抬（建议 ≥64）或热身分页。1.31 纯复习 `get_due_reviews(limit=None)` **无帽**，不能沿用主线上限整课拒绝。 | 1.30 / 1.31 | 上限 ≥ 公式上界或有分页；size 报告 RAM；超限文案 | 待 |
| **IC-7** worker / 栈 / 菜单 | worker 抄 **LevelTest**：队列永驻、`xQueueSend` 2s 有界、禁止 Whoami 第三变体（忙等不自释放）。结果堆拷贝，UI `delete`。`items[N]` 随 IC-6 抬升后更肥——**禁止** UI 线程 `= T()` / `memset`。`SCREEN_MAINLINE_ID` 追加枚举**末尾**（现尾 `SCREEN2_2_ID`）。契约 §1 补 `ml`/`rv` 两行与实现同批。布局恢复内容栈 ≤ 容器高 −10px（v2 删成遥测一行）。主线 start **无 key 也自然幂等**，重开从首个 `submitted==false` 继续；已交项再 Enter = `is_retry`（不二次推进 SRS）。 | 1.30 | CODE：对照 LT 四件套；无肥赋值；枚举尾；契约表两行 | 待 |
| **IC-8** 后端钉 SHA | 实现对照 `english_learning_system` **`571d29a`**（或开工时再钉一颗）。工作树未提交不改键集，但 cloze 选句仍在动——矩阵漂移防线 = 枚举外丢弃 + 未知停题卡。`target_word` **不是** API 字段。 | 1.30 | 实现申请写明对照 SHA；实采 JSON 键 ⊆ 矩阵 | 待 |

Nit（不占缺陷、不挡、≤3）：① §3.2 引用 `studyplan.py:1888-1897` 在 HEAD 是 topic_writing JSON，可见性评注在 `:1871`；工作树已漂到 `:2004`——钉 IC-8 即可。② §3.3 start 行把 403 写成 409 `plan_superseded`（L2 按 IC-2，不改正文、不开 v4）。③ 申请 §4 把设计 C 写成「列残余，不倒逼 v4」——指南 §2.5.2 的 C 只用于未闭合协议 P0/P1；本轮 A，无 v4。

疑问清单（不占缺陷）：认读 v1 以文字 `word` 代替网页 TTS「音义」（§8 已登记 Round 2，属主决策）。完形是否在题干旁加「用句中词形」一行提示——教学法，非协议。

---

## 5. 已通过项（delta 独立复核）

- **P1-1 反例击不穿**：按 §3.2，认读显示 `word`+`options`、藏 `correct_option`/`meaning_zh`；拼写/挖空藏 `word`/`cloze_word`/`cloze_forms`；挖空句经 `build_cloze_sentence` 全形态替换为 `____`（`text_support.py:691-728`）。v2「丢 word → 认读无题干」不再可构造。
- **输出项不再被未知 skip 吃掉**：判型先 `segment==output`。v2「无 task_type → skipped → output_capstone 永 409」不再可构造。
- **cloze 有作答面**：与拼写同构 textarea，不是 skipped 兜底。
- **空表 409 有定义**：与 `LessonIncomplete` 非空表分治；强制律只在 capstone。
- **submit/finish superseded → HOME**：与 `PlanSuperseded` 守卫一致（复习 `lesson_ref is None` 不守卫）。
- **HOME 四态**：`recommend` 四值与 `PlanNextOut` 一致；无计划不是 done。
- **HW-MAINLINE-1 方向**：丢弃集按题型定义，评审专攻「当前题型丢弃集上屏/入结构体/入日志」。`scene_words` 是完课判据可见脚手架，不是半答案。
- **未改架构仍闭合**：双 app 单职责、答题留在 SESSION、learn 通道 `X-API-Key`+`X-Gate-Pin`、不跳 `ui_leveltest`、不落本地会话。压缩未把这些改坏。

---

## 6. 验证说明

- **环境**：开发机。T-Deck-Pro HEAD `068733f`。无设备、本轮未跑 `pio`（纯文档 delta，HW/BUILD = `NOT_APPLICABLE`）。
- **CODE/SPEC 已核**：上表 file:line。后端 `git status` 脏（`studyplan.py` +241/−54 等），结论绑 HEAD `571d29a`，不以工作树行号为准。
- **CLAIM_ONLY**：申请「已核实无答案泄漏面」——本侧核对 schema 顶层多余字段后同意；真机泄漏面要到实现 L2 用 grep 结构体/日志。
- **未独立复现**：`remote_api_demo --channel learn` 实采、真机四态/409 路径。登记 G-真机（设计 §7）。

---

## 7. 对称三格（§7.2 强制）

① **是否核了全区间 diff（按 §2.5.3 裁剪）**：是。`git diff 5b32605 24741f2 -- docs/mainline-design.md`（§1–§10 几乎整篇重写，核心在 §3/§4/§5/§7/§8/§10）。`24741f2` 文件归属：`docs/mainline-design.md` **受评**。区间内 `docs/reviews/*5b32605*`、`TODO.md`、`scripts/remote_api_demo.py`、申请改锚 **不评**（他轮产物 / 非本设计正文）。未把未改的双 app 拆分架构当新问题重开。

② **快照是否独立复跑**：设计稿无 BUILD。服务端对照 `git -C ~/english_learning_system rev-parse HEAD` = `571d29a`，读 `plan.py`/`sessions.py`/`studyplan.py`/`mastery.py`/`supply.py`/`srs.py`/`schemas/{study,learning}.py`/`text_support.py`。无设备 → HW `NOT_APPLICABLE`，不是证伪。

③ **申请「最没把握」是否构造反例**：是。① HEAD 键集匹配、工作树不改键；② cloze_forms 服务端已收，本地提示非必须；③ skipped 输出项 → finish 仍 `OutputRequirementMissing`（空表 409），已写入 IC-2。

---

## 8. 文件归属（`24741f2`）

| 文件 | 归属 |
|---|---|
| `docs/mainline-design.md` | 受评（v2→v3 全文 delta） |

---

## 9. 评审自审（§9【D】/【ALL】）

```text
【秘密】【C+D】
☑ 10. 无新增 key 路径
☑ 11. 本 commit 无凭据文件

【方法论】【ALL】
☑ 15. 无 P0/P1；IC 均有 file:line
☑ 16. 申请 ✅ 未当 VERIFIED；服务端 CODE 独立核
☑ 17. 参照系 = 设计锚 24741f2 + 后端 HEAD 571d29a（非脏工作树）
☑ 18. 设计稿无生产函数
☑ 19. 反例：认读无题干 / 输出被 skip / 空表当表首 / skipped 豁免强制律 / start 409 当 superseded
☑ 20. 同基线 Codex 结果未打开

【文档对齐】【D】
☑ 21. 矩阵列 vs 装配器键 vs 网页渲染角色一致（认读 word=题干，拼写/挖空 word=答案）
☑ 22. 端点表与 sessions/plan 响应模型可对照；start 409 superseded 一句不可照抄（IC-2）
☑ 23. 申请 ✅ 逐条回证，三条自评都给了反例或放行理由

【设计稿收口】【D+ALL】
☑ 24. 无协议 P0/P1 → L1+A，合同进表，不开 v4
☑ 25. 默认 delta；start 403 笔误在改写段内，未扩面
☑ 26. PP_ML_ITEMS_MAX / worker 布局 / 403 笔误保持实现合同，未升 P1
☑ 27. 无假想级联；skipped 强制律是服务端现码反例，不是「实现者可能写错」
```

---

## 10. 审批意见

- [x] **L1 DOC-ALIGNED 达标**（G-开工）
- [x] **A. 接受，据此开工**（v1.30 mainline 编码）
- [ ] B. 退回重构
- [ ] C. 部分接受

属主签字环节：§10 的 `PP_ML_ITEMS_MAX=20` 与 §3.3 start 行 409 笔误 **不要**再开设计版去改——实现申请按 IC-2/IC-6 写，L2 对照本合同表。
