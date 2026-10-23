# 设计评审结果：学习主线（mainline + review 双应用）v2（Grok）

- **评审日期**：2026-10-23（同日补：对照本机 `~/english_learning_system/` 服务端源码，不改结论档）
- **评审人**：Grok（Cursor）
- **申请文件**：[mainline-design-review-request-5b32605.md](mainline-design-review-request-5b32605.md)
- **设计稿**：[../mainline-design.md](../mainline-design.md)（v2）
- **评审提交锚**：`5b32605`（`git log -1 -- docs/mainline-design.md`）
- **工作树 HEAD**：`185aa10`（申请改锚；`git diff 5b32605 HEAD -- docs/mainline-design.md` 空）
- **服务端树**：`/home/debian/english_learning_system`（申请写的 `E:/works/english_learning_system` 即此副本）。核了 `docs/FSD_LM_DESIGN.md` v4.2、`docs/ENGINEERING_CONTRACTS.md` §1.3/§4、`backend/app/api/v1/endpoints/{plan,sessions,onboarding}.py`、`backend/app/services/{studyplan,mastery,srs,supply}.py`、`backend/app/schemas/learning.py`、`frontend/app.js` 作答渲染、`backend/tests/test_studyplan.py` 用例 8。
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**（§0–§4、**§2.5 收口强制**、§6、§9【D】、§10）
- **评审范围**：**全篇**，非 delta。v1（`4b16209`）从未出过评审结论；v2 是本主题第一轮实际评审。§2.5.3「默认只核 diff」不触发。v1→v2 diff 只用来找**拆分时删掉的承重句**（见 IC-8）。
- **扩面**：无协议/架构 P0/P1。服务端复核**加硬了实现合同**（题型/409 形态/课项上限），不开设计 v3。
- **对照**：同轮工作树有未入库的 Claude 结果
  `mainline-design-review-result-5b32605-claude.md`（L1+A）。本文件独立走查后对照，不采信其结论作证据。
- **评审结论**：**L1 DOC-ALIGNED + A**（服务端复核后**维持**）。无未闭合的协议/架构 P0/P1。**G-开工通过**（1.30 `ui_mainline`）。残留项进下方实现合同表，门禁 = 实现 commit 的 L2 / G-合入，**不开设计 v3**。

---

## 0. 服务端复核补记（相对初稿：结论不变，合同加硬）

初稿把 FSD/409/课项分布标 `CLAIM_ONLY`。对照本机服务端后，下列从猜测变成 **CODE/SPEC `VERIFIED`**。无一需要改承重规范，故不升 C、不开 v3。

| 初稿判断 | 服务端事实 | 合同变化 |
|---|---|---|
| `target_word` 是开课 JSON 的答案字段 | `SessionItemOut` 无此键（`schemas/learning.py:16-21`）；demo `:889` 的 `it.get("target_word")` 是死回退。真答案字段是 `prompt.word` / `correct_option` / **`cloze_word`+`cloze_forms`** | IC-1 纠正 |
| 丢弃 `word` 即可防泄题 | 网页认读**必须显示** `prompt.word`（`frontend/app.js:5603`）。拼写/挖空才藏 `word`。一刀切丢弃会让认读没有题干 | IC-1 按题型分流 |
| 渲染白名单够用 | 生产题型是 `recognition` / `cloze` / `recall`；输出项**没有 `task_type`**，靠 `segment=output`+`task`。白名单缺 `meaning_zh`/`sentence_en`/`scene_patterns`，挖空/拼写/输出无法出题 | IC-1、IC-6 |
| `plan_superseded` 可能在 start | **start 是 403** `"Lesson does not belong to current active plan"`（`plan.py:370-375`）。**submit/finish 才是 409** `reason=plan_superseded`（`sessions.py:105-113,137-144`；`test_studyplan.py:1049-1060`） | IC-2 纠正挂点 |
| 输出强制律 409 也能 `missing_item_ids` 跳回 | `detail = {message, missing_item_ids: []}`（`sessions.py:155-159`；契约 §1.3）。空数组跳回必失败；网页按 `segment==output` 末项恢复 | IC-2 |
| `recommend` 三态 | 无计划：`{"recommend": null, "lesson": null}`（`studyplan.next_action:1395-1396`）。有计划才是 `lesson\|review\|done` | IC-3 |
| `recommend=review ∧ due=0` | `backlog_trigger` = `idle≥3 ∧ due_minutes>2×cap`（`srs.py:377-379`）。`recommend=review` 时到期负荷必 >0，该边缘**实际不可达** | 自评③降级 |
| `PP_ML_ITEMS_MAX=20` 待抽样 | 热身预算 `min(cap−新学, cap/2)`，词 0.5 min（`srs.py:346-349`，`supply.py:32-36`）。非成人 cap=25 → 热身最多 **25 词** + 配额 3–8；成人 cap=45 → 热身最多 **45 词**。有到期堆积的主线课**常态 >20**。纯复习 `get_due_reviews(limit=None)` 无帽 | IC-7 从「抽样」改为「20 已证伪，禁止带着合入」 |
| start 是否自然幂等 | 主线 **无 key 也自然幂等**（同课未结业会话原样返回，`plan.py:398-421`）。纯复习 **每次 POST 新建会话、不支持 Idempotency-Key**（契约 §4 行「纯复习课开课写面」） | IC-5、IC-9 |
| 服务端是否单会话 | `(user, lesson)` 互斥只覆盖主线开课。主线会话 + 复习会话可并存。§4.3「单会话兜底」作废，§8 那句是对的 | IC-5 |

---

## 1. 为何给 A（五个邀请攻击 + 自评三条）

判定锚 = §2.5.1「最小修复」。五个焦点都能指出一条与既有模块同构的补句/校准，没有任何一条必须改掉承重规范（签名/回滚/密钥/TLS/变砖）才能编码。服务端复核没有推翻这一点。

| 焦点 | 能否构造协议级反例 | 去向 |
|---|---|---|
| ① HW-MAINLINE-1 答案上屏 | 否。存在正确实现：按题型分流（认读显示 `word`、藏 `correct_option`；拼写/挖空藏 `word`/`cloze_word`）。`scene_words` 是输出脚手架，服务端刻意下发（`studyplan.py:2003-2005`）。 | IC-1 |
| ② 409 `plan_superseded` × `missing_item_ids` | 否。线性机不会死锁。挂点已钉：submit/finish 409；start 403；输出强制律空 `missing_item_ids`。 | IC-2 |
| ③ 不做定级测是否设备死路 | 否。设备已有 Level 应用；计划写面网页 Bearer-only。无计划时 `recommend=null`，HOME 必须走「manage on web」，不能画 done。 | IC-3 |
| ④ 双 app 状态串扰 | 否。独立 worker 同构。review 进入须 `GET /plan` 读 `due_total`；两 app 会话服务端允许并存。 | IC-4、IC-5 |
| ⑤ 时钟未同步 vs「每日」 | 否。设备不本地判日；`idle_days`/`due_at` 都在服务端。TLS 时间走既有 `http_ensure_time`。 | 通过 |

自评：① 未知 `task_type` **不能**默认 skip——输出项没有 `task_type`，默认 skip 会让 `output_capstone` 永远 409（IC-6）；② `PP_ML_ITEMS_MAX=20` **已被装配公式证伪**（IC-7）；③ `recommend=review ∧ due=0` 按 `backlog_trigger` 实际不可达。

---

## 2. Findings

无协议/架构 P0/P1。无挡 L1 项。

（实现合同见 §3，不定级为挡开工缺陷。按 §2.5.2，设计稿 A **必须**附合同表，不得因合同缺口改判 C。）

---

## 3. 实现合同跟踪表（§2.5.2 强制；绑 G-合入）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| **IC-1** 按题型的题面/答案分流 | **不要**一刀切丢弃 `word`。对照网页 `app.js:5587-5704` + 开课装配 `studyplan.py:1438-2056`：① **recognition**：显示 `word`+`phonetic`+`options`，藏 `correct_option`；② **cloze**：显示挖空后的 `sentence_en`/`sentence_zh`+`meaning_zh`，藏 `word`/`cloze_word`/`cloze_forms`；③ **recall**：显示 `meaning_zh`（热身/复习藏音标到提示），藏 `word`；④ **output**（无 `task_type`）：显示 `scene_words`/`scene_patterns`/`topic_title`/`min_chars`。解析白名单 = 上述题面字段 + `id`/`submitted`/`scaffold_level`/`segment`/`task`/`task_type`/`target_layer`/`pos`。禁止 `Serial.printf` 开课 JSON 原文。`target_word` **不是** API 字段，勿按 demo 死回退实现。 | （实现申请锚） | CODE：结构体无 `correct_option`/`cloze_word`/`cloze_forms`；认读标签能显示 lemma；挖空标签是 `____` 句不是原词 | 待 |
| **IC-2** 409/403 分发 | **start**：非当前计划 → **403**（不是 `plan_superseded`）；已完成 → 409 `"Lesson already completed"`。**submit/finish**：计划已 `superseded` → 409 `{reason: plan_superseded}`（仅主线，`lesson_ref` 空的复习课不守卫）。finish 合取缺项 → 409 `{missing_item_ids:[...]}` 跳回首个。finish **输出强制律**（仅 `output_capstone`）→ 409 `{missing_item_ids:[]}`：跳回**最后一个 `segment==output` 项**（契约 §1.3），禁止按空数组下标。任意其它 4xx/5xx → 提示 + Back。 | （实现申请锚） | CODE：三条路径分发与上表一致；SIM：网页切计划后提交、输出课不合格 finish | 待 |
| **IC-3** HOME 空计划与 Enter 门控 | 无计划：`GET /plan` → `{plan:null}`；`GET /plan/next` → `{recommend:null, lesson:null}`。`plan:null` **或** `recommend==null` 无条件引导网页，不画 done。Enter **仅** `recommend=="lesson"` 且 `lesson.id` 有效时开课。`course_completed ∧ due_total>0` 是 `consolidating`（巩固期，主推复习）。 | （实现申请锚） | CODE：Enter 门控；空计划文案；巩固期不显示「通关无事」 | 待 |
| **IC-4** Review 进入先 GET | `due_total` 在 `GET /plan` 的 plan 对象上（`studyplan.py:712,732`）。review `entry()` 调该 GET；`due_total==0` **禁止** `POST /reviews/lesson/start`——该 POST **总会新建会话**（`plan.py:477-478`），空到期也会 commit。`included_count` 可在开会话后显示。 | 1.31 实现申请 | CODE：review 进入有 GET；due=0 零 POST start | 待 |
| **IC-5** 并发钉死 §8 | 服务端**允许**主线未结业会话与复习会话并存（开课锁只按 `(user, lesson)`）。§4.3「单会话语义兜底」作废。跨 app 的 403/409 走 IC-2。纯复习 start **无**自然幂等、**不**接受 Idempotency-Key——超时后禁止盲目重 POST start（会堆会话）。 | （实现申请锚） | CODE：两队列两 gen；复习 start 失败可退出且不连打 | 待 |
| **IC-6** 题型分流，禁止未知默认 skip | 抄网页：`task_type \|\| (target_layer==recall ? recall : output)`。`segment==output` 即使没有 `task_type` 也走 textarea，**永不自动 skip**（否则 output_capstone 被输出强制律永远拦住，且 `missing_item_ids` 为空无法跳回）。`cloze` 是主线后半段的一等题型，不是「未知」。主线装配没有 speak 类项（`start_lesson_session` 只处理 `writing_topic`/`output_fallback`）；v1 不必为不存在的 speak 项自动 skip。skip 失败不得推进游标、不得自动 finish。 | （实现申请锚） | SIM：output 无 task_type 能提交句子；cloze 能填空；未知类型停在题卡 | 待 |
| **IC-7** `PP_ML_ITEMS_MAX` 不得为 20 | 热身公式已证 20 盖不住有到期堆积的主线课（非成人最多约 25 热身 + 配额；成人约 45 热身 + 配额）。1.30 须抬上限（建议 ≥64）或热身分页；硬拒方向仍对（禁止静默截断）。1.31 纯复习无帽，必须分页/分课，不能沿用主线上限硬拒整课。 | （实现申请锚） | 上限常数 ≥ 公式上界或有分页；超限文案；review 分页 | 待 |
| **IC-8** 布局 −10px 与肥结构 | v1 有「内容栈 ≤ 容器高 −10px + 状态行先渲染后消息」（§33 学费）；v2 删成只剩遥测一行。L2 恢复该预算。`items[N]` 在 N 按 IC-7 抬升后更肥——**禁止** UI 线程 `= T()` / `memset`。CJK 标签走 `lt_has_cjk` 切 `Font_Hanzi_16`。 | （实现申请锚） | CODE：无肥赋值；create 打 `[ML] layout:` 且栈高低于容器 −10 | 待 |
| **IC-9** worker 变体 + 主线自然幂等 | worker 抄 **LevelTest/WordBank**：队列永驻、2s 有界发送、禁止混用 Whoami 第三变体。排空二选一：(a) 屏内 poll + `entry()` 先 drain；(b) `factory.ino` loop 无条件 poll。契约 §1 补 `ml`/`rv` 两行与实现同批。主线 start **无 key 即可**复用未结业会话（`plan.py:398-421`）——重开必须从 `submitted==false` 的首项继续；已提交项再 Enter 是 `is_retry`（`mastery.py:508-519`，不二次推进 SRS），不是双计。 | （实现申请锚） | CODE：对照 LT 四件套；契约表有两行；重开会话从首个未交项起 | 待 |
| **IC-10** 菜单接线 | `SCREEN_MAINLINE_ID` / `SCREEN_REVIEW_ID` **追加枚举末尾**（现尾 `SCREEN2_2_ID`，`ui_deckpro.h:87`），不插中间。菜单页公式保持最大下标语义（issue_list §8）。1.30 先上 mainline 一钮即可。 | （实现申请锚） | CODE：枚举尾；`scr_mgr_register`；`menu_page_apply` 页数不被第 23 钮打穿 | 待 |

Nit（不占缺陷、不挡、建议 ≤3）：① 申请 §5 把设计稿 C 写成「P2 列清单」——指南 §2.5.2 的 C 只用于未闭合协议 P0/P1；本合同表属于 A。② `TODO.md:43-46` 仍写设计稿 v1/`4b16209`，申请文件已是 `5b32605`。③ 路径在设备侧由 `pp_url` 加 `/api/v1`（`penpal_api.cpp:139-146`），设计稿写 `/plan/next` 与 demo 一致，实现勿再叠一层前缀。

疑问清单（不占缺陷）：`GET /onboarding/state` 可做更细空态（step 0 未定级 / 1 已定级 / 2 未选目标 / 3 无计划），非必须。网页预学闪卡（契约 §1.5）本版不做，与「定级测不做」同类。

---

## 4. 已通过项（独立复核）

- **设备 = 执行端、计划写面网页**：`POST /onboarding/goal|daily-quota` 与 `/plan/regenerate|switch|preview|confirm` 为 Bearer-only；learn 只读 `GET /plan*` + 开课/作答/结课（契约 §4 + FSD §1.2）。
- **认读 1-based 提交**：服务端认读同时接受选项原文和 1-based 数字（`mastery.py:404-415`）。
- **`skipped: true`**：计 0 分入分母、重开错题、不动 SRS（契约 §1.3）。主线 v1 没有独立 speak 题型，这条主要给显式跳过入口，不是给「口语题自动 skip」。
- **独立 worker 拆分**：不共享在飞状态。作答组件复用留给 §10。
- **传输层可复用**：`pp_request` 已带 `X-API-Key` + `X-Gate-Pin` + HTTPS `http_ensure_time`。
- **截断先例**：`s_copy_disp` UTF-8 边界 + `"..."`。
- **超上限硬拒**优于静默截断——阈值本身按 IC-7 改，策略保留。
- **不落本地会话**：断电走服务端自然幂等重开（主线）。
- **`is_retry` 不双推进 SRS**：`mastery.py:508-519` 与设计「已提交再 Enter = 重提交」同向。

---

## 5. 验证说明

- 锚 `5b32605`。区间文件：`docs/mainline-design.md`（受评）。`185aa10` 改申请文件名/锚 + 刷新仓内 `scripts/remote_api_demo.py`。
- 服务端：`/home/debian/english_learning_system`（静态走查，**未**对跑后端、未跑 pytest）。FSD v4.2 / 契约 §1.3 §4 / `plan.py` `sessions.py` `studyplan.py` `mastery.py` `srs.py` `supply.py` / `app.js` 认读·挖空·回忆渲染 / `test_studyplan.py` 用例 8。
- 本仓交叉：`docs/async_ipc_contract.md` §1 表 + 规则 7；`examples/pda2/penpal_api.cpp`；`ui_leveltest.cpp`；`ui_deckpro.{h,cpp}`；`docs/issue_list.md` §8 / §33。
- 无 `pio run`，无真机。BUILD/HW = `UNKNOWN`。设计阶段按 §1.1 / §3.3，L1 只要求 DOC/SPEC。

---

## 6. 对称三格（§7.2；设计稿口径）

1. **范围**：首轮 = 全篇 §1–§10。补核本机服务端后加硬合同，未把服务端新题型（cloze）当成「必须改设备架构」去挡 L1。
2. **独立复跑**：未采信同轮 Claude 结论。设备设计对照服务端装配/409/网页渲染；未跑服务端测试（CODE 行号来自源文件）。
3. **最没把握处反例**：
   - 自评① 未知类型 skip：服务端输出项无 `task_type` → 默认 skip 会让 output_capstone 永远 409 且空 `missing_item_ids`（IC-6）。不升 P1，因为分流键 `segment` 已在白名单上，存在正确实现。
   - 自评② items>20：装配公式证伪 20（IC-7）。抬上限是数字合同，不是新架构。
   - 自评③ due=0：`backlog_trigger` 下 `recommend=review` 时 due 负荷必 >0，边缘不可达。
   - `word` 一刀切丢弃：认读网页显示 lemma（`app.js:5603`），会去掉题干（IC-1）。

---

## 7. 声明矩阵

| 声明 | 状态 |
|---|---|
| learn 通道可 start/submit/finish 主线课 | `VERIFIED`（`plan.py:353-357` + 契约 §4；静态，未打 HTTP） |
| 路径 `/plan/next` 等 = `pp_url` 的 `/api/v1`+path | `VERIFIED`（SPEC+CODE `penpal_api.cpp:139-146`） |
| 鉴权 = 既有 `X-API-Key` + `X-Gate-Pin` | `VERIFIED`（CODE `pp_request`） |
| HW-MAINLINE-1：两字段丢弃 ⇒ 答案不上屏 | `CONTRADICTED` 作为全题型策略（认读必须显示 `word`）→ IC-1 按题型分流；`correct_option`/`cloze_word` 仍须藏 |
| `scene_words` 上屏不算泄答案 | `VERIFIED`（`studyplan.py:2003-2005` 服务端刻意下发判据词） |
| 认读 1-based `user_input` | `VERIFIED`（`mastery.py:412-415`） |
| `plan_superseded` 在 start | `CONTRADICTED`（start=403；submit/finish=409）→ IC-2 |
| `missing_item_ids` 跳回不卡死 | `PARTIALLY_VERIFIED`：缺项数组可跳；输出强制律空数组须按 output 项恢复 → IC-2 |
| review 进入能显示 `due_total` | `VERIFIED` 数据源 = `GET /plan`（`studyplan.py:732`）；设计未接线 → IC-4 |
| 两 app 不共享在飞状态 | `VERIFIED`（设备 SPEC）；服务端单会话 `CONTRADICTED`（§4.3 错、§8 对）→ IC-5 |
| 设备不本地判「每日」 | `VERIFIED`（SPEC；`idle_days` 在服务端 `studyplan.py:680-690`） |
| start 自然幂等、可不带 Idempotency-Key | **主线 `VERIFIED`**（`plan.py:398-421`）；**复习 `CONTRADICTED`**（每次新建）→ IC-5/IC-9 |
| `PP_ML_ITEMS_MAX=20` 盖住真实课 | `CONTRADICTED`（热身公式）→ IC-7 |
| worker = wa_* 四件套 | `PARTIALLY_VERIFIED`：2s 有界 = LT/WB → IC-9 |
| 新用户在设备上非死路 | `VERIFIED`（SPEC+既有 Level 屏+`recommend=null`）→ IC-3 |
| 生产 `task_type` 仅认读/拼写/输出 | `CONTRADICTED`（还有 `cloze`；输出无 `task_type`）→ IC-6 |

---

## 8. §9 自审清单【D】/【ALL】

【文档对齐】【D】

- [x] 21 键名：无计划时 `recommend=null`（IC-3）；review `due_total` 在 `GET /plan`（IC-4）；输出项用 `task`/`segment` 不是 `task_type`（IC-6）——模式 A 已收进合同
- [x] 22 无 JSON 示意块。服务端 OpenAPI/Pydantic 与上表一致
- [x] 23 设计稿未把校准标成已做。本补记把原先 CLAIM_ONLY 升到 CODE，未把「未跑 pytest」写成 VERIFIED HTTP

【设计稿收口】【D+ALL】

- [x] 24 服务端复核后仍无协议/架构 P0/P1 → **维持 L1+A**；20 被证伪、cloze 未写进白名单都是实现合同（抬上限 / 抄网页分流），未因「还可以更完整」给 C
- [x] 25 首轮全篇；本补记是同一结果文件的证据升级，不是新设计版
- [x] 26 未把公式证伪的 20 升 P1（最小修复 = 改常数或分页）。未把「认读显示 word」写成否定 HW-MAINLINE-1——不变量仍是「参考答案不上屏」，认读的 lemma 是题干不是 `correct_option`
- [x] 27 未把网页 `app.js` 当设备 ABI；分流表是与既有 UI 同构的显示约定

【ALL】

- [x] 15 无 P0/P1；IC 均有服务端 `file:line`
- [x] 16 真机仍 `CLAIM_ONLY`；服务端静态 CODE，未跑 pytest / 未打 HTTP
- [x] 17 参照系 = `5b32605` 设计稿 + T-Deck HEAD + `/home/debian/english_learning_system` 源码
- [x] 19 反例库：输出强制律空数组、热身 >20、输出无 task_type、start 403 vs submit 409
- [x] 20 与同轮 Claude：仍同为 L1+A。其 P2「submit 是否 409」现为 **VERIFIED 会 409**；其 P2「plan:null vs recommend」现为 **VERIFIED recommend=null**。本侧纠正了 `target_word` 误报，补了 cloze / 空 missing_item_ids / 热身公式

【C】1–9、12–14、18：无设备实现代码，`NOT_APPLICABLE`。

---

## 9. 审批意见

- [x] **A. 接受，据此开工（设计稿口径）** — L1 + 无协议/架构 P0/P1；IC-1…IC-10 绑实现 G-合入（服务端复核后加硬，不倒逼设计 v3）
- [ ] B. 退回重构
- [ ] C. 部分接受

**G-开工：通过。** 下一评审对象是 `ui_mainline` 实现 commit。1.30 编码前先按 IC-7 定 `PP_ML_ITEMS_MAX`（不要写 20），按 IC-1/IC-6 抄网页题型分流。1.31 review 另申请并核 IC-4。不要开设计 v3。

**给 A 而非 C**：v1.3 §0 原则 11 / §2.5.2。服务端把几条合同从 CLAIM_ONLY 变成 CONTRADICTED/VERIFIED，最小修复仍是数字、分流表和 4xx 分发，不是新架构。

**给 A 而非 B**：双应用拆分、learn 消费面、按题型藏答案、三页 HIDDEN、单飞 worker 均可实施。
