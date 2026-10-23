# 评审结果：学习主线（mainline + review 双应用）设备端设计 v2（deepseek）

> **修订说明**：初稿（同日、同锚 `5b32605`、未归档）基于 T-Deck 仓库内证据，
> 后端字段大量标 `UNKNOWN`；后端权威仓库本机可达后完成**全量实核**并原位修订：
> 若干 UNKNOWN → `VERIFIED`，P2-1/P2-2/P2-3 重写，结论维持 L1+A。实核口径：
> `~/english_learning_system`（本机）——`backend` HEAD `64b34df`（v1.22.3）+
> 工作树未提交改动（`schemas/learning.py` 追加 3 个可选字段、`studyplan.py`
> 句法/输出重构等；引用行号以工作树为准，涉契约面的关键字段在 HEAD 同存）；
> `docs/FSD_LM_DESIGN.md`（v4.2）、`docs/ENGINEERING_CONTRACTS.md` §1.1/§1.3 已核。
> **同轮分歧（2-2）**：Codex 与 Claude（修订后，读后端）对同一字段角色/判型
> 问题判 P1/C（不达 L1）；Grok 与本文件判 P2/A（事实四方一致、定级不同）。
> 已按 §0 冲突四步登记于文末"多方分歧"节，属主裁决项——本文件不以票数
> 放行 G-开工。

- **评审日期**：2026-10-23
- **评审方**：DeepSeek V4.1 Flash（OpenCode CLI 运行；按 §7.2 用模型名）
- **申请文件**：[mainline-design-review-request-5b32605.md](mainline-design-review-request-5b32605.md)
- **评审对象**：[`../mainline-design.md`](../mainline-design.md)（v2，锚 `5b32605`）
- **评审范围**：**全篇**（§1–§10）+ v1→v2 delta（`4b16209..5b32605`，单文件
  +83/−61）——v1 从未送审，本稿是该主题第一轮实际评审，不适用 §2.5.3。
- **基线核对**：T-Deck 侧 HEAD `185aa10`；`git diff 5b32605 HEAD --stat` 仅
  TODO/申请改锚/`scripts/remote_api_demo.py` 同步，**设计正文零改动** → 锚有效。
- **评审依据**：`docs/review_guide.md` v1.3 设计稿口径（§0–§4、§2.5 收口、
  §6、§9【D】、§10）。
- **评审结论**：**L1 DOC-ALIGNED 达标 + A 接受（G-开工）**。对照后端权威后
  **维持无协议/架构 P0/P1**——三处结论性修订（对照实码）：① HW-MAINLINE-1
  字段分类按题型勘误（`word` 在 recognition 是题面而非答案；真实答案字段
  另含 `cloze_word`/`cloze_forms`）；② start 的 `409 plan_superseded` 实为
  **403**；③ `PP_ML_ITEMS_MAX=20` 按服务端常量**常态可超**。三处均为实现
  合同级（§2.5.1 负清单外、正文可满足），**不倒逼设计 v3**；合同表见文末。
  **注意**：同轮 Codex 与 Claude（修订后）以同组事实判 P1/C——分歧属定级
  适用（见文末"多方分歧"节），G-开工是否即时放行待属主按 §2.5.5 裁决。

---

## Findings

### P2-1：HW-MAINLINE-1 须按题型重定"字段角色表"——`word` 的类别声明错误；黑名单漏 `cloze_word`/`cloze_forms`；白名单缺全部题面字段

- **位置**：设计 §3 HW-MAINLINE-1（L61-65："`prompt` 内的 `word`、
  `correct_option` 是服务端参考答案"；白名单 =
  `task_type/segment/options/scene_words/scaffold_level`）与 §4.4（L103-105）。
- **后端实码证据（逐题型）**：
  - **recognition（认读）**：题面 = `word` + `phonetic` + `pos` + `options`；
    答案 = `correct_option`。Web 端渲染 `prompt.word` 为题干大字
    （`frontend/app.js:5603`，区块 5589-5618）；服务端 prompt 含
    `word/tts_text`（`studyplan.py:1527-1538`）。→ **`word` 是刺激源，不是答案**。
  - **recall（拼写回忆）**：答案 = `word`/`tts_text`；题面 = `meaning_zh`
    （+`meaning_en` 遮蔽后，`app.js:5687-5693`）。
  - **cloze（例句挖空）**：答案 = `word`/`tts_text`/**`cloze_word`/`cloze_forms`**
    （prompt 出处 `studyplan.py:1815-1828`；判分用 `cloze_word`+变形容差
    `mastery.py:419-432`——`cloze_word` 就是挖空处词形）；题面 =
    `sentence_en`(已挖空)/`sentence_zh`/`meaning_zh`/`pos`/`phonetic`。
  - **output（输出课）**：**无 `task_type` 键**——prompt =
    `{segment:"output", task:"topic_writing"|"sentence_making", topic_title,
    scene_words[≤20], scene_patterns, min_scene_words:1, min_chars:15}`
    （`studyplan.py:2017-2028`/`2043-2054`）；Web 端 dispatch 用
    `prompt.task_type || (target_layer==='recall'?'recall':'output')` 兜底
    （`app.js:5587`）。
- **反例（照正文实现的两种走向都不对）**：
  1. 照"白名单只留 5 类字段"实现：除 `options` 外**无题面可渲染**——recall/
     cloze 的 `meaning_zh`、cloze 的 `sentence_en/zh`、输出课的
     `topic_title/scene_patterns/min_chars` 全是白名单外字段；recognition
     只剩选项没有题干。
  2. 照"`word` 全局丢弃"字面实现：recognition 题面丢失（孩子看不到要认的
     词）；而按 §3"未知 `task_type` → 渲染降级 + skipped 提交路径"实现，
     **输出项因无 `task_type` 被判"未知" → 自动 skipped → output_capstone
     课永远撞输出强制律 409**，与 `ENGINEERING_CONTRACTS.md` §1.3 明令的
     "不得让孩子停留在 finish→409 循环"直接冲突。
  3. 对照：设计 §4.4 又列"输出题 = textarea"（已知题型）——§3 与 §4.4
     对同一输入给出不同推断，正文内部不一致。
- **撤回前稿判断**：`target_word` 不在当前 `_items_payload`
  （`plan.py:340-350` 仅产 `id/word_id/target_layer/prompt/scaffold_level/
  submitted`）；后端 demo `scripts/remote_api_demo.py:779` 的
  `it.get("target_word")` 是防御性回退，非现行字段——以本条实列字段为准。
- **影响**：教学完整性/状态机 B/2/静默 → **P2**（不在 §2.5.1 负清单：签名/
  回滚/密钥/TLS/变砖；最小修复 = 与既有模块同构的字段映射表；按题型表实现
  的正确实现存在 → 不升 P1）。
- **最小修复**：L2 冻结**逐题型「字段角色表」**（display / answer / drop 三列，
  由实采 JSON + 上列出处生成）：display ⊆ {word(recognition)、phonetic、pos、
  meaning_zh、meaning_en、sentence_en、sentence_zh、options、topic_title、
  scene_words、scene_patterns、min_chars、min_scene_words}；answer = {word/
  tts_text（recall/cloze）、correct_option（recognition）、cloze_word/
  cloze_forms（cloze）}；输出项 dispatch 以 `segment/task` 入表；黑名单随表
  生成。正文 HW-MAINLINE-1 的"word=参考答案"一句按题型**勘误**（随实现申请
  头部报送，不重开设计版）。

### P2-2：会话/错误面逐条钉死（对照 `ENGINEERING_CONTRACTS.md` §1.3 与实码）

- **位置**：设计 §3 表（L56-59）、§4.2（L89）、§4.3（L96-97）、§4.4（L107-110）、
  §8（L152-155）。
- **逐条实核 + 反例**：
  - **(a) start 错误面勘误**：非当前计划课 → **403** `"Lesson does not belong
    to current active plan"`（`plan.py:371-375`）；已完课 → 409
    `"Lesson already completed"`（`:376-377/395-396`）；并发重叠 → 锁内复用
    既有会话（`:381-421`）。**设计 §3 把 `409 plan_superseded` 挂在 start 行
    是事实错位**——该 409 只出现在 submit/finish（`sessions.py:105-113/137-145`）。
    设备必须同时处理 403（提示 + 回 HOME/刷新计划）与 409。
  - **(b) 断点续做初游标**：start 自然幂等 ✓ 实码确认——未结业会话原样返回
    （`plan.py:361-362` 注释 + `:398-421`），重放 items 带 `submitted` 位
    （`plan.py:340-350`，由真实 Submissions 计算）。设计未写初游标规则 →
    应为"首个 `submitted=false`"；全 true 则直接 finish。
  - **(c) finish 409 载荷形状**：合取缺项 `detail={message, missing_item_ids:[…]}`
    （`sessions.py:146-154`）；输出强制律 `detail={message, missing_item_ids:[]}`
    （`:155-160`；判据"归一化后引用 ≥1 场景词形且 ≥15 字符"，`mastery.py:936-957`、
    契约 §1.3）。设备解析须读 **`detail.*` 包裹**（设计未写错误体形状）；
    `missing_item_ids` 与本地无交集 → 禁止自动循环，提示 + 回 HOME。
  - **(d) 输出强制律必须实现契约 §1.3 的"客户端 409 恢复条款"**：去改写 →
    回退到**最后一个输出型课项**（`prompt.segment === 'output'` 从服务端下发
    items 反查，兜底末项）；恢复目标**不得依赖会话内累计状态**；**重进课同样
    适用**（否则孩子停在 finish→409 循环——四家评审 fix4 同指）。判据必须可见：
    题面展示 `scene_words` + 要求（≥1 场景词、≥15 字符）——服务端注释明说
    "判据要求必须可见"（`studyplan.py:2003-2005`），web 渲染见 `app.js:5778-5793`。
    设计的作答组件没有这一显示面。
  - **(e) 输出项提交反馈**：输出项（`word_id=NULL`）提交响应**无** `passed/
    quality`（`mastery.py:483-484` 提前 return；`schemas/learning.py:51-55` 描述
    "输出项……字段为 None"）→ 反馈行不能显示对错/quality，只能"已提交/待结课
    判定"；设计 §4.4 "提交反馈（passed/quality）"须分题型。
  - **(f) 重交安全 ✓ VERIFIED**：`is_retry` 只记订正重交证据，不重复 SRS/
    掌握推进/错题本（`mastery.py:449-462` 检测、`:505-519` 提前返回）——设计
    "再按 Enter = 重提交（服务端 `is_retry` 建模，安全）"成立。**附注**：
    `ENGINEERING_CONTRACTS.md` §1.1 将 submit/finish 的显式幂等键标为
    `[target]`（未落地，"硬件依赖这些端点前须落地并回归"）——重交靠 `is_retry`
    语义兜底（响应丢失后重交无双推进）；设计应在 §3 L2 注记该依赖与证据。
  - **(g) finish 重放形状**：已结业会话再 finish → 200
    `{session_id, finished_at, score, xp_awarded:0, streak_days}`，**无
    `correct_items/total_items/skipped_items`**（`mastery.py:671-680`）→
    finish 重试路径必须容忍缺键，且**不得把 `xp_awarded=0`（重放）当首次
    结果展示**。
  - **(h) review start 无自然幂等**：`review_lesson_session` 每次新建会话、
    无既有会话复用（`studyplan.py:2063-2110`）——重进/双击产生孤儿会话、
    同词重复复习；且**空到期也会建会话并 commit**（`plan.py:477-478`）→
    `due_total==0` 时禁止 POST start（显示 no-review-due）。设备须防双击并
    定义"RESULT→REVIEW 再进"语义。入口 `due_total` 取数确认：`GET /plan`
    携带（`studyplan.py:731`）或 start 响应（`plan.py:477-496`）。附注：
    mainline HOME 的 `recommend=review ∧ due=0` 边缘**实际不可达**
    （`backlog_trigger` 要求 `due_minutes > 2×cap`，`srs.py:370-373`）——
    守卫仍需要，但只落在 Review 应用入口。
  - **(i) 并发（§8 开放问题实核）**：未见任何跨会话/跨类型单会话约束
    （mainline 开课守卫仅进程内 `(user, lesson)`；review start 无守卫；
    API-key 通道对两类会话均放行 `sessions.py:27-42`）→ 服务端**不约束**
    两 app 并发（`PARTIALLY_VERIFIED`：静态未见约束，建议实采双开确认）。
    但 §4.3"不共享在飞状态，**天然互斥**"的表述错误（设备侧确无互斥），
    与 §8 自相矛盾——措辞须修正。
  - **(j) `recommend` 可为 `null`**：无计划时 `/plan/next` 返回
    `recommend=null`/`lesson=null`（`studyplan.py:1395-1396`；
    `PlanNextOut.recommend: str|None`，`schemas/study.py:129-134`）→ HOME
    需增 `recommend=null` 分支（或 `plan:null` 优先）。与同日 Claude P2#2
    同结论，现为实证。
  - **(k) 口语题 v1 无对象**：主线/复习会话只生成 recognition/cloze/recall/
    output（`studyplan.py:1928-2056`/`2063-2110`）→ "口语题一律 `skipped`"
    在 v1 无实际命中项（Round 2 ASR 另议；登记即可，非缺陷）。
- **影响**：状态机/契约破坏 B/2/静默 → **P2**（与同日 Claude P2#1 同族，
  本条为超集：含错误体形状、恢复条款、重放形状、断点续做、并发实证）。
- **最小修复**：上列 (a)–(j) 于实现申请前**一次性实采钉死**（错误面矩阵 +
  409 恢复路径 + 重放形状 + 断点续做），绑实现 G-合入；配 SIM 推演表 +
  真机回归扩项（§7 项 3/4/5 扩容）。

### P2-3：`PP_ML_ITEMS_MAX=20` 按服务端常量**常态可超**；选项>4 校验为死代码

- **位置**：设计 §10（L168-170）、§8（L151）；申请自评②（request L63-65）。
- **后端实码证据（量化）**：课项 = 热身 + 学习词 + ≤1 输出
  （`studyplan.py:1928-2056`）。热身预算 `warmup_budget = max(0,
  min(cap−new_est, cap/2))`（`srs.py:346-349`），词 ≈ 0.5 分钟
  （`srs.py:36-42`）→ 少儿三档 `cap=25`（`supply.py:33-36`）时**热身可达
  25 项**；配额默认 3/5/6/8（`supply.py:28-30`）。例：15 到期词 + 5 学习词
  = **21 > 20**（常态小积压即触发）；上界 ≈ 25+6+1 = **32**（初中档）。
- **反例**：孩子隔两天再学（到期 15+）→ 该课被设备"开课即拒" → 正是 SRS
  最需要复习的场景全部不可用。
- **影响**：可用性/边界 E/2/有声 → **P2**（显式拒绝、无完整性风险；改上限
  或分页 = 与既有模块同构的常量/流程，属实现合同）。
- **最小修复**：编码前用 `--channel learn` 抽样若干真实课实测定档（建议
  上限 ≥32，配 `pio run` size 报告核 RAM）；或定义分页/截断策略。；
  **选项>4 拒绝开课为死代码**：服务端 recognition 恰 4 选项、否则回退
  recall（`studyplan.py:1523-1525`）→ 降级为防御性校验，不入验收。
- 与 Claude 同轮 P3 同题：本轮升级为"按常量常态超限"的量化结论，两轮合并。

### P3：文档对齐小项（申请/设计日期 vs 实际提交日；TODO.md 版本锚残留）

- **证据**：(a) 申请与设计均写 **2026-09-28**（request L3、design L4），实际
  锚 commit `4b16209`/`5b32605` 与 HEAD 为 **2026-10-23**（`git show`），
  相差 25 天；(b) `TODO.md:43-44` 同行为"设计稿 v1（`4b16209`）……
  （`…-5b32605.md`）"——v1 锚与外链 v2 文件混用。
- **影响**：文档一致性 E/1/静默 → P3（防台账时间线/版本漂移）。
- **最小修复**：日期对齐实际提交日或注明"起草日"；TODO 行改锚 `5b32605`。

---

## 评审焦点与自评逐条闭合（后端实核后更新）

| 项 | 判定 | 依据 |
|---|---|---|
| 焦点① HW-MAINLINE-1 完备性 | **策略成立，字段角色表须按题型重定** | "解析层丢弃"仍正确；但 `word` 在 recognition 是题面（`app.js:5603`），答案字段另含 `cloze_word/cloze_forms`（`studyplan.py:1815-1828`）；白名单缺全部题面字段 → P2-1 |
| 焦点② 409 组合卡死/重复提交洞 | **构造出反例（实证）** | start=403 勘误；输出项 `task_type` 缺失 × §3"未知→skipped" = 输出课死循环（P2-1.2）；finish 两类 409 载荷 + 恢复条款（P2-2 c/d）；重交安全已证（P2-2 f） |
| 焦点③ 范围边界（定级测不做） | **不构成新死路** | goal/quota 本就 Bearer-only（`onboarding.py` 头注、FSD:238）；设备已有 LevelTest 覆盖 staircase；HOME `plan:null` 引导行足够（优先级规则并入 P2-2 j） |
| 焦点④ 双 app 拆分边界 | **成立，一处措辞勘误** | 无共享可写状态；并发实核 = 服务端不约束（P2-2 i）；共享组件形态属 L2 选型（NO_CONTRACT 不升 P1） |
| 焦点⑤ "每日"语义/时钟 | **成立** | 设备不本地判日、v1 无本地持久化；`suggested_date`/due 均服务端算；NTP 仅 https+非 insecure 要求（`penpal_api.cpp:186-191`） |
| 自评① 显示契约未逐型冻结 | **兜底不足，须以"字段角色表"驱动** | 未知 task_type 降级本身对未来的未知类型可用；但对**现存** output（无 task_type 键）与三题型题面字段，白名单式兜底不可用（P2-1） |
| 自评② `PP_ML_ITEMS_MAX` | **认同需实测；已量化常态超限** | P2-3（热身 ≤cap/2÷0.5min 项） |
| 自评③ `recommend=review` 且 due=0 | **mainline 侧不可达；Review 入口仍需守卫** | `backlog_trigger` = `idle≥3 ∧ due_minutes>2×cap`（`srs.py:370-373`）→ `recommend=review` 时 due 必 >0；Review 应用入口 due=0 显示 no-review-due 且禁止 POST start（P2-2 h）+ `recommend=null` 分支（P2-2 j） |

---

## 已通过项（独立复核；对照后端权威）

- **六端点消费字段全部实证存在**（前稿 UNKNOWN 转 `VERIFIED`）：
  `/plan/next` 的 `recommend(lesson|review|done)`/`due_minutes`/`lesson{id,
  lesson_index, lesson_kind, title, scenario_code, est_minutes, status}`
  （`studyplan.py:1405-1426`）；`/plan` 的 `goal_exam/total_lessons/
  completed_lessons/cursor_lesson/course_completed/consolidating/due_total`
  与 `plan:null`（`:699-745`）；start 的 `session_id/mode/items[]{id,
  word_id,target_layer,prompt,scaffold_level,submitted}`（`plan.py:340-350`）；
  submit 的 `{score_write,combined,skipped,is_retry,quality,passed,
  mastery_layer,in_errorbook}`（`schemas/learning.py:41-59`）；finish 的
  `{score,xp_awarded,correct_items,total_items,skipped_items}`（`:62-79`，
  可选键）；review start 的 `{session_id,mode,items,due_total}`（`plan.py:
  477-496`）。设计字段无一处虚构。
- **start 自然幂等 + 并发守卫** ✓（`plan.py:361-365/398-421`）；**跳过语义**
  （SRS 零触碰 + 错题本重开）✓（`mastery.py:343-377`）；**输出判据**（≥1
  场景词 + ≥15 字符，与完成强制律同源）✓（`mastery.py:381-398`）。
- **重交安全** ✓（is_retry 无重复推进，`mastery.py:505-519`）——设计原句成立。
- **认证/传输层复用** ✓（`penpal_api.cpp:175/221-222`；API-key 通道放行两类
  会话 `sessions.py:27-42`）。
- **异步模型** 与 `async_ipc_contract.md` §1 同构（一次建队列、快照按值、
  gen 首字段、2s 有界自释放属已批准变体、栈 1024×8）；busy 变体正文未钉死
  （L2 选型，须与 §1 补行同批登记，IC-8）。
- **范围边界** ✓（FSD:238：写面 Bearer-only 清单；level-test learned 可调）；
  "每日"语义 ✓；回归清单覆盖 409/gen/busy/布局 ✓。

---

## 与同轮其他结果的关系（Claude / Codex / Grok）

### Claude（`…-5b32605-claude.md`；文件已修订：首轮 A → 读后端后改判 C）

- **修订后结论**：**不达 L1，判 C**（"需先改正文再开工"）。修订发生在我方
  首读之后、本批入库之前；入库文件为修订版（637 行，含"补充核实"节）。
  其新 P1 与 Codex P1-1 同族（输出题判别键缺失 + 字段角色/白名单不符），
  论点为"§4.4 按字面实现必把输出课吞成 skipped"（详见下节分歧对勘）。
- **其首轮 P2 的实核答案**（并入我方 P2-2）：submit/finish **确实**会
  `409 plan_superseded`（`sessions.py:105-113/137-145`）；start 则是 **403**
  （`:375`）非 409；无计划时 `recommend=null`（`studyplan.py:1396`）；并发
  会话服务端**不约束**。
- **其首轮"构造不出任何让 `word/correct_option` 进入设备内存的路径"**：
  范围有误（recognition 题面就是 `word`，`app.js:5603`）——其修订版已自行
  修正到"按题型分流"（与本方 P2-1 同构）。

### Codex（`…-5b32605-codex.md`，已访问后端）与 Grok（`…-5b32605-grok.md`，已访问后端）

- **事实层三方完全一致**（互相独立印证）：三题型字段角色、输出项无
  `task_type`、cloze 为一等题型、`word` 的题干/答案双角色、`correct_option`/
  `cloze_word`/`cloze_forms` 为判分真值、start=403 勘误、review start 无幂等、
  20 上限被装配公式证伪——与本文件 P2-1/P2-2/P2-3 同构。
- **Grok 与本文结论一致**（L1+A，合同表），并有两处本文件采纳的补强：
  `recommend=review ∧ due=0` 实际不可达（`backlog_trigger`，`srs.py:370-373`；
  已并入自评③/P2-2 h）；v2 删除 v1 的§33 布局预算句（"内容栈 ≤ 容器高 −10px"）
  → 已并入 IC-10。Grok 另登记 Nit（`pp_url` 已含 `/api/v1` 前缀勿再叠、
  菜单枚举追加末尾）与本文件 P3 同族，属主合并即可。
- **Codex 与本文为定级分歧**（事实相同）：Codex 判 **P1/C**（"按正文闭环
  失效"，B×1）；Claude 修订后同判 **C**；本文与 Grok 判 **P2/A**（存在按
  题型分流的正确实现）。**2-2**——分歧详析与属主裁决路径见下节，
  **不以票数放行**。

---

## 多方分歧登记与属主裁决（Codex P1-1 + Claude 新 P1 vs 本文件/Grok 的 A；**2-2**）

- **分歧性质**：不是事实分歧，是 §2.5.1 判定锚的适用分歧。四方字段事实一致
  （`word` 的题干/答案双角色、`cloze_word`/`cloze_forms` 为答案、输出无
  `task_type`）；分歧在"最小修复是否必须改掉承重规范"。
- **C 方（Codex + Claude，修订后）的核心主张**：§3/§4.4 把 `task_type` 当
  题型判别键，而输出项无此键——按字面实现，输出项落"未知 → skipped"→
  `output_capstone` 课永远撞输出强制律 409（`missing_item_ids=[]`，无跳回
  目标）→ "设计宣称的三大交互之一必被吞掉" = B/1 → P1/C；最小修复 = 改
  §3/§4.4 正文（判别键两档：`segment=="output"` 用 `task`，否则用
  `task_type`）。Claude 给出可照抄的改法，称"不需要再等实采校准"。
- **本文件维持 P2 的复算（四步）**：
  1. **该失败情景的根因是"判别键未写"，不是"写错"**：§3 表列字段、§4.4 列
     输出=textarea 交互，但**从未写**输出项的判别键；`segment` 又本就在设计
     自己的白名单里（§3 L64）。§2.5.1 明文："**不得把 `NO_CONTRACT`（没写
     布局）直接升 P1。未写 = 实现选型，L2 二选一钉死。只有'正文已经唯一可
     推断的写法不可满足'才是设计 P1**"——本情景属前者。
  2. **"按正文必错"的唯一性不成立**：§4.4 已把"输出题"列为一等交互，
     "未知 task_type → skipped"（§3 L65）与之矛盾 → 存在两个读法；且 §8
     自述"显示契约未逐型冻结 → 实采校准"，正文自带校准豁免。
  3. **存在可行的正确实现**（条件式条款核验点）：按题型分流（认读显示
     `word`、拼写/挖空藏 `word`/`cloze_word`/`cloze_forms`、输出以
     `segment` 判别）满足 HW-MAINLINE-1 的**安全目的**（判分真值不上屏）。
     至于"`word` 一刀切"的**写错**部分：照字面丢弃后认读仍有可提交的
     选项交互、recall/cloze 仍可提交，协议面无"全失败"（损题面 =
     E/完备性档，P2 合同），安全声明仍被满足。
  4. **负清单未触及**（签名/回滚/密钥/TLS/变砖）→ 不强制升协议类。
- **C 方定级的成立条件**：若把"字面 = 唯一读法"（输出必然进 skipped 分支、
     且视其为协议不可满足）视为前提，则 B/1 → P1。该读法字形可辩护，但其
     唯一性被 §4.4/§8 与 §2.5.1 的 `NO_CONTRACT` 条款反驳——本文件不改判，
     仅登记其阻断主张。
- **属主裁决（§2.5.5 明许：证据只支持实现合同时可按 §0 原则 2 采纳 A）**：
  - **选项①（本文件推荐）**：A + IC-1/IC-2 **编码前冻结**——字段角色表 +
    输出 dispatch 键 + 错误面矩阵在实现申请头部冻结（即 C 方 required
    action 的同一产物），设计正文以**勘误**随申请报送，不重开 v3、不空转
    一轮复审。
  - **选项②（从严）**：采纳 C——以"设计正文勘误（§3 判别键两档 + §4.4
    兜底边界 + 字段角色表）+ delta 复审"作为 G-开工前置（Claude 的改法本身
    即"可直接抄进设计稿"的节级勘误，非全文重开）。
  - 两选项**工程产物相同**（同一张按题型字段表 + 输出 dispatch 键 + 409
    恢复条款 + 20 上限重定），差异只在门禁表述与是否再走一轮正文复审。
- **§0 原则 2 提示**：C 方（2 份）的 P1 主张在属主裁决前**具有阻断效力**——
  本文件同样不以"另两方给 A"（2-2 平票）为由建议放行；请属主就本焦点做一次
  显式裁决后再开 G-开工。修订版 Claude 文件与 Codex 文件即 C 方完整论证。

---

## 验证说明

- **环境**：后端权威仓库**本机可达**（`~/english_learning_system`，HEAD
  `64b34df` v1.22.3 + 工作树未提交改动，已声明）；T-Deck 侧无 pio、无设备
  （设计评审，L1 最低证据要求 = DOC/SPEC `VERIFIED`，无 BUILD/HW 缺口）。
- **本轮实核清单**（全部只读）：`backend/app/api/v1/endpoints/plan.py`
  （start/review-start/payload）、`sessions.py`（submit/finish/409/通道闸）、
  `backend/app/services/studyplan.py`（plan_summary/next_action/四段课组装/
  review 组装/prompt 构造）、`mastery.py`（提交/重交/合取/输出判据/完成重放）、
  `srs.py`、`supply.py`、`schemas/learning.py`、`schemas/study.py`、
  `frontend/app.js`（题型渲染 ground truth）、`docs/ENGINEERING_CONTRACTS.md`
  §1.1/§1.3、`docs/FSD_LM_DESIGN.md`、`backend/scripts/remote_api_demo.py`；
  T-Deck 侧复核 `docs/mainline-design.md` v1→v2 diff、`remote_api_demo.py`
  副本、`async_ipc_contract.md`、`issue_list.md` §30/§33、`TODO.md`。
- **声明矩阵**：①BUILD/②HW/⑤PROBE —— `NOT_APPLICABLE`（设计稿）；后端字段/
  行为 —— 本轮 `VERIFIED`（上列 file:line）；"两 app 并发无约束" ——
  `PARTIALLY_VERIFIED`（静态未见，建议实采双开）；标准 `[target]` 幂等键
  （submit/finish）—— 引用契约原文，标注目标态。
- **工作树声明**：后端工作树含未提交改动；引用行号以工作树为准；涉契约面
  关键字段在 HEAD 同存（`git show HEAD` 抽查 cloze/recognition prompt 键
  一致）。§3.5 参照系要求满足（T-Deck 侧锚未移动；后端以现状工作树为口径）。

---

## 对称三格（§7.2）

1. **全区间**：本稿首轮送审（v1 未审）→ 按全篇（§1–§10）+ v1→v2 delta 评审；
   额外自查申请未点名面向：字段角色表、输出项 dispatch、finish 重放形状、
   断点续做、`recommend=null`、并发约束实核、20 上限量化。基线漂移已核。
2. **独立复跑**：无 pio/无设备——如实标注；后端与前端证据均为本人只读重读
   （file:line 如上），与 Claude 同轮文件相互独立（该文件为未跟踪本地状态，
   未据其结论改写我方证据判定）。
3. **自评三条 + 焦点五条反例**：自评①——构造（现存 output 项无 `task_type`
   → §3 规则吞为 skipped → 409 循环；三题型题面字段不在白名单）；自评②——
   构造并量化（15 到期 + 5 学习 = 21 > 20）；自评③——构造（`recommend=null`
   实证；review `items=[]` 守卫）。焦点五条逐条见上；②构造出 (a)–(k)。

---

## §9 自审清单【D】

- [x] **21 字段/键名一致性**：设计 §3 表与后端字段逐一对上（实证）；**唯一
      不符 = start 行的 `409 plan_superseded`（实为 403）**；`word` 的答案/
      题面类别按题型勘误；均已入 P2-1/P2-2，未打钩。
- [x] **22 代码块可执行性**：设计无 JSON/伪代码块；`--channel learn` 与
      Web/后端 file:line 引用可照抄核验。
- [x] **23 ✅/⏸ 对应证据**：设计稿未冒充已验证；日期与提交日不符（P3-a）；
      字段级声明本轮已全部落实证据或转 IC。
- [x] **24 无协议/架构 P0/P1 → L1+A + 合同表** ✓（未因完备性给 C）。
- [x] **25 delta 适用性**：首轮 → 全篇（对称三格①）。
- [x] **26 最小修复未触负清单**；`NO_CONTRACT`（共享组件形态、busy 变体、
      字段容错矩阵）未误升 P1。
- [x] **27 未做假想级联升格**：`cloze_word`/`task_type` 缺失路径等均以实码
      + 现存字段定性 P2 合同，非"未来实现者可能写错"。

---

## 实现合同跟踪表（§2.5.2，绑 ui_mainline 1.30 / ui_review 1.31 实现申请）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| IC-1 | **字段角色表**（逐题型 display/answer/drop；输出项 `segment/task` dispatch；黑名单随表生成；HW-MAINLINE-1 勘误随申请） | 1.30 实现申请（**编码前冻结**） | 实采 JSON 键集表 + `grep` 答案字段 0 命中设备结构体/日志/渲染 | 待 |
| IC-2 | **错误面矩阵**：start 403/404/409；submit/finish `detail.*` 两类 409；`missing_item_ids` 无交集兜底；通用 4xx/5xx 兜底不猜 reason | 1.30/1.31 | 实采错误语义记录 + SIM 推演表 + 真机回归扩项 | 待 |
| IC-3 | **断点续做 + finish 重放形状**：初游标 = 首个 `submitted=false`；finish 重试容忍重放缺键、不展示 `xp_awarded=0` | 1.30 | 实采三组（重放/重交/结课后重 finish）+ 回归 | 待 |
| IC-4 | **输出强制律 UI**：判据可见（场景词/≥15 字符）+ 去改写回退最后 output 项 + 重进课适用 | 1.30 | 实采 409 载荷 + 真机回归（重进课恢复） | 待 |
| IC-5 | **review 面**：入口 `due_total` 取数（/plan 或 start）+ 防双击/无幂等说明 + RESULT→REVIEW 刷新 | 1.31 | 实采 + 设计 §3 表更新 | 待 |
| IC-6 | **并发双开实采** + §4.3"互斥"措辞勘误 | 1.30/1.31 | 实采两并发会话（mainline+review）记录 | 待 |
| IC-7 | **`PP_ML_ITEMS_MAX` 实测定档**（建议 ≥32 + RAM size 报告）或分页策略；选项>4 降级为防御注释 | 1.30 | 抽样分布表 + `pio run` size 报告 | 待 |
| IC-8 | `async_ipc_contract.md` §1 补 ml/rv 两行（busy 变体与规则 7 变体钉死）——与代码同批入库 | 1.30 | 契约 diff + 代码对照 | 待 |
| IC-9 | 文档对齐：申请日期 / TODO.md 版本锚（P3）；`pp_url` 前缀勿叠（Grok Nit 同族） | 本轮或 1.30 | diff 核对 | 待 |
| IC-10 | v1 §4.1 布局预算句删除 → L2 恢复（内容栈 ≤ 容器高 −10px + 遥测）；items 增肥后禁 UI 线程 `= T()`/`memset`（§33 学费） | 1.30 | CODE：无肥赋值；`[ML] layout:` 遥测 | 待 |

> 入库轮义务（§7.2）：本表随 1.30 实现申请头部引用（IC-1/IC-2 为**编码前
> 冻结项**），或转 `issue_list.md` 编号承接；实现轮 G-合入逐条核销。

---

## 审批意见

- [x] **L1 DOC-ALIGNED 达标**（G-开工门槛）
- [x] **A. 接受，据此开工**（P2/P3 全进实现合同表，绑实现 G-合入；IC-1/IC-2
      先冻结再写解析层与状态机，不倒逼设计 v3）
- [ ] B. 退回重构　[ ] C. 部分接受

> **G-开工放行条件（本方意见）**：本结论与同轮 Codex、Claude（修订后）的
> P1/C 存在定级分歧（事实一致，2-2，见"多方分歧登记"节）。按 §0 原则 2，
> 请属主先对该焦点做一次**显式裁决**（§2.5.5 明许采纳 A，或从严按 C 方走
> 正文勘误 + delta 复审）；无论哪条路径，IC-1/IC-2 的产物（按题型字段表 +
> 输出 dispatch 键 + 错误面矩阵）都是**编码前冻结项**。裁决落定前，本方
> 不建议以票数放行。
