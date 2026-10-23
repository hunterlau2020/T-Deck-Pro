# 评审结果：学习主线（mainline + review 双应用）设备端设计 v2（claude）

- **评审日期**：2026-10-23（**2026-10-23 补充核实后修订**——见下方"补充核实"
  说明，结论已更新，不是另开新文件，是同一轮评审内的证据升级）
- **申请文件**：[mainline-design-review-request-5b32605.md](mainline-design-review-request-5b32605.md)
- **评审对象**：[`../mainline-design.md`](../mainline-design.md)（v2，锚 `5b32605`）
- **评审范围**：**全篇**，非 delta——v1（`4b16209`）从未送评审，本稿是本主题
  第一轮实际评审，不适用 §2.5.3 的"默认只核 diff"（该条只约束**已有上一轮
  评审结论**的修订稿）。
- **评审结论（修订后）**：**不达 L1，判 C（部分接受，需先改正文再开工）**。
  首轮复核（见下方原文）判了 L1+A；拿到服务端权威仓库（`~/english_learning_
  system`）的真实源码后独立核实 §3 契约表的具体字段,发现**该表对"输出"
  题型的字段模型描述有误**，并连带确认了另外两处此前只能按 `CLAIM_ONLY`
  处理的猜测——这不是"又多了几条完备性建议"，是 §3 正文本身与后端权威
  事实矛盾,按 §2.5.2"C 的 required action 必须是改哪一节规范"，这条够
  得上改正文而非挂实现合同。全部证据与改法见"补充核实（2026-10-23，已读
  后端源码）"一节；下方保留了修订前的原始复核内容（标注"（首轮，部分结论
  已被下方补充核实修正）"），不删除,方便对照我的判断是怎么被新证据推翻的。

---

## 补充核实（2026-10-23，已读服务端权威源码 `~/english_learning_system`）

首轮复核时服务端仓库不在本机可读范围，§3 契约表的字段形状只能按
`CLAIM_ONLY` 处理（见下方"验证说明"原文）。本次用户指出该仓库实际在本机
`~/english_learning_system/`，我直接读了 `backend/app/services/studyplan.py`
（课项/prompt 组装）、`mastery.py`（submit/finish 判定）、
`api/v1/endpoints/{plan,sessions}.py`（端点与 409 处理）——三条首轮只能
猜测或暂且接受的点，现在都有了确凿的源码证据，其中一条改变了 L1 判定。

### 新 P1（协议/架构级,挡 L1）：§3 契约表把 `task_type` 当成"输出"题的
判别字段,但输出型课项在后端根本不带 `task_type`——按现状正文实现,
"输出=文本造句"这个设计宣称的三大交互之一会被设备的"未知 task_type→
降级 skipped"兜底路径**系统性吞掉**

- **位置**：`mainline-design.md` §3 契约表"设备消费字段"列写
  `prompt{task_type, segment, options[], scene_words}`；§4.4"未知
  `task_type` → 渲染降级(segment文本+skipped提交路径)"——这句把
  **`task_type` 当成唯一的题型判别键**。
- **后端源码证据**（`backend/app/services/studyplan.py:2016-2053`，
  `output_capstone` 课的两类输出项组装，`start_lesson`/课项段落）：
  ```python
  # topic_writing
  prompt_json={
      "segment": "output", "task": "topic_writing",
      "topic_id": ..., "topic_title": ...,
      "scene_words": scene_words[:20], "scene_patterns": scene_patterns,
      "min_scene_words": 1, "min_chars": 15,
  }
  # sentence_making
  prompt_json={
      "segment": "output", "task": "sentence_making",
      "scene_words": scene_words[:20], "scene_patterns": scene_patterns,
      "min_scene_words": 1, "min_chars": 15,
  }
  ```
  **这两个输出型 `prompt_json` 里完全没有 `task_type` 这个键**——判别
  字段是 `segment=="output"` + **另一个独立字段** `task`
  （`topic_writing`/`sentence_making`），不是 `task_type`。对照
  `_word_prompt()`/`_build_recognition_prompt()`/`_build_cloze_prompt()`
  三个"认读/拼写(cloze)/回忆(recall)"的组装函数——这三者确实都带
  `task_type: "recall"/"recognition"/"cloze"`。也就是说，后端对"题型"
  这件事用了**两套并行的判别机制**：非输出题用 `task_type`，输出题用
  `segment + task`，设计稿只写了前一套,把它当成唯一判别键。
- **后果推演**：如果按 §4.4 现在的文字字面实现（"读 `prompt.task_type`，
  不在已知集合里就走降级 skipped 兜底"），**所有** `output_capstone`
  课的输出项都会命中 `task_type` 缺失 → 落进"未知 task_type"分支 →
  自动 `skipped=true` 提交。这不是边缘情况——`output_capstone` 是课程
  体系里固定存在的课型（`studyplan.py:78` 课型枚举、`:326` 课表组装都把
  它列为常规课型之一，不是罕见分支），意味着"输出=文本造句"这个设计稿
  §2 自己宣称的三大核心交互之一，**在当前正文描述下会被安全兜底路径
  悄悄吞掉,永远不会被用户看到**——而且因为吞掉的方式正是设计稿自己
  设计的"安全降级"（为了防止未知类型渲染出危险内容），这个 bug 不会
  报错、不会崩溃，只会表现成"奇怪，output_capstone 课好像没有写作题"，
  非常容易在真机回归里被忽略过去（回归清单 §7 第 2 条确实提了"输出题
  造句提交"，但如果实现者是照着 §3/§4.4 现在的文字写的判别逻辑，这条
  回归项在真机上会自然地走到降级分支,表现成"提交成功"，不会报错,
  测试的人很可能看到"流程走完了"就判过，不会意识到走的是降级分支不是
  正常的输出题交互）。
- **为什么够得上协议级 P0/P1、而不是"实现合同里补一句"**：判定锚是
  "最小修复是否要改一段承重规范"。这里最小修复**必须**改 §3 的"设备
  消费字段"这句正文本身——从"`task_type` 是判别键"改成"`segment==
  'output'` 时判别键是 `task`，否则判别键是 `task_type`"——这不是
  "实现期二选一的细节"（如队列怎么 drain、hex 大小写），是会让设计
  正文描述的一个**核心功能分支按字面实现时必定不可达**的事实错误。
  按 §2.5.1 判定锚"按正文必错"这一档,挡 L1。
- **两轴**：`VERIFIED`（CODE：直接读后端源码取得的 `prompt_json` 字面量，
  非转述/非猜测）｜`VIOLATES`（设计稿 §2/§4.4 自己宣称的三大交互之一）。
- **最小修复（已经可以直接抄进设计稿，不需要再等"实采校准"）**：
  §3 契约表"设备消费字段"改写为两档：
  - `segment != "output"` 时：判别字段 `task_type`
    ∈ `{recall, recognition, cloze}`（**不是**设计稿写的"认读/拼写/
    输出"三个中文名对应三个英文值——`recall` 对应纯拼写回忆，
    `recognition` 对应认读 4 选 1，`cloze` 对应情境挖空/例句拼写；
    design 文中"拼写=键盘输入"这个交互其实同时对应 `recall` 与
    `cloze` 两种 task_type，不是一一对应，渲染分支要按这个合并）；
  - `segment == "output"` 时：判别字段是 `task` ∈
    `{topic_writing, sentence_making}`，两者渲染/交互相同（文本框造句，
    提交走同一套输出判据 `min_scene_words`/`min_chars`），可以合并成
    一个"输出题"渲染分支，不需要再细分。
  - §4.4 的"未知 task_type → 降级"这条兜底**只套用在非 output 分支**；
    output 分支的判别键本来就是 `task`，不走 `task_type` 这条逻辑。

### 关联发现（HW-MAINLINE-1 白名单，升级为同一条 P1 的一部分）：白名单
字段集本身也对不上真实 payload，而且"哪些字段是答案"**按题型不同、
不是同一套**——`cloze`/`recognition` 两种 task_type 下，`correct_
option`/`meaning_zh`/`cloze_word`/`cloze_forms` 都和设计稿需要的渲染
字段混在同一个 JSON 对象里，`cloze` 型还缺了渲染必需的 `sentence_en`
（`word` 字段单独讨论——它对 recognition 是合法题干不是答案，对 cloze
才是答案，判断依题型而不同，不能一刀切，见下方最小修复的订正说明）

- **位置**：§3"渲染白名单 = `task_type/segment/options[]/scene_words/
  scaffold_level`"。
- **后端源码证据**（`studyplan.py:1513-1539` `_build_recognition_prompt`、
  `:1816-1825` `_build_cloze_prompt`）：
  ```python
  # recognition (认读 4 选 1) 的完整 payload：
  {"task_type": "recognition", "word": w.word, "phonetic": ...,
   "pos": ..., "meaning_zh": ..., "meaning_en": ...,
   "correct_option": correct_opt, "options": options,
   "tts_text": w.word, "segment": segment}
  # cloze (情境挖空/拼写) 的完整 payload：
  {"task_type": "cloze", "word": w.word, "phonetic": ...,
   "pos": pos, "meaning_zh": meaning_zh, "meaning_en": meaning_en,
   "sentence_en": cloze_en, "sentence_zh": sentence_zh,
   "cloze_word": matched_form, "cloze_forms": all_forms,
   "tts_text": w.word, "segment": segment}
  ```
  两个问题,证据都在上面这两段里：
  1. **白名单缺了渲染必需的字段**：`cloze` 型如果真的要显示"例句挖空"
     给用户填词,设备**必须**拿到 `sentence_en`（挖空后的例句文本）——
     但 `sentence_en` 不在白名单里。如果解析层严格按白名单字面实现
     （"非白名单字段一律丢弃"，这正是设计稿自己强调的 HW-MAINLINE-1
     执行方式），`sentence_en` 会被连同 `word`/`correct_option` 一起
     丢弃,设备拿到手的 cloze 题就只剩 `task_type`/`segment`,**没有
     任何可显示的题目内容**——"拼写"这个交互的另一半（cloze 子类型）
     会因为过度丢弃而不可用,这是 HW-MAINLINE-1"宁可丢多不丢少"的
     安全策略带来的一个副作用,设计稿目前没有预见到。
  2. **即使把 `sentence_en` 加回白名单,同一个 JSON 对象里还贴着多个
     答案/答案等价字段,且哪些算"答案"要按题型分开判断**（这一点我在
     本节初稿里曾经把 `word` 也一并算成答案字段,是错的——见下方最小
     修复里的订正；`word` 对 recognition 是合法题干,对 cloze 才是
     答案）：`correct_option`（认读正确项，已被设计稿点名）、
     **`meaning_zh`**（认读题的 `correct_option` 本身就是从
     `meaning_zh` 清洗得到的——这个字段对**认读题**而言等价于答案，
     但设计稿完全没提它）、**`cloze_word`/`cloze_forms`**（挖空处的
     正确词形，对**cloze 题**而言是答案，设计稿同样没提）。这些字段
     和 `sentence_en`/`options`/`scene_words` 这些真正要渲染的字段
     **在同一层**,不是嵌套在一个可以整体跳过的子对象里——"白名单只
     保留几个字段"这个策略如果不按题型精确列出要排除的字段（而不是
     只列出 `word`/`correct_option` 两个名字），未来任何一次"反正多拿
     一个字段方便调试"式的代码改动都有很现实的概率把 `meaning_zh` 或
     `cloze_word` 带上屏——因为这些字段**名字听起来完全无害**（"中文
     释义""挖空词形"），不像 `correct_option` 那样一看名字就知道危险。
- **为什么归并进上面那条 P1、不单列**：根因相同——§3 的字段契约是凭
  印象写的，没有对照过真实 payload。最小修复同样是"把 §3 的契约表和
  白名单改成和后端 payload 逐字段对应"，和上一条是同一次改写动作。
- **最小修复（下方已自纠一处——初稿曾把 `word` 和 `meaning_zh`/`tts_text`
  不分青红皂白地归成一类"答案等价字段"，这是错的,`word` 对 recognition
  题型是**必须显示的题干**,不是要丢弃的答案,下面是订正后的版本）**：
  白名单按 `task_type` 分桶明确列全——
  - `recall`：`task_type/segment`（无选项、无例句，纯回忆，设备只需要
    知道"这是一道回忆题"来决定用哪种输入控件，题干本身来自哪里要在
    实采时核对——`_word_prompt` 返回的 `meaning_en` 字段在 `is_recall`
    分支里会把目标词替换成 `____`（见 `studyplan.py:1441-1446`），这个
    "挖空后的英文释义"很可能就是 recall 题该显示的题干，**需要加入
    recall 分桶的白名单**，不能连它一起丢掉——`word` 本身对 recall 仍
    要丢弃，因为这里的任务是"看中文/挖空释义回忆英文词"，直接给出
    `word` 等于直接给答案）；
  - `recognition`：**`task_type/segment/word/options[]`**——**`word`
    必须保留并显示**（这是"认读 4 选 1"的题干："给你一个英文单词，从
    4 个中文释义里选对应的那个"——用户要先看到是哪个单词才能作答，
    `word` 不是答案，是题目本身；我在首轮/上一次补充核实里把它和
    `correct_option` 混为一谈，是看到"HW-MAINLINE-1 说 word 要丢弃"
    就没有再结合具体题型推演，这里纠正）。真正要丢弃的是
    **`correct_option`/`meaning_zh`/`tts_text`/`meaning_en`/`phonetic`/
    `pos`**——`correct_option` 已被设计稿点名；**`meaning_zh` 必须和
    它一起明确列入"丢弃清单"**，因为 `correct_opt` 本身就是从
    `meaning_zh` 清洗出来的，对认读题而言两者等价；`tts_text` 在
    recognition/cloze 两型下都原样等于 `word`，既然 `word` 本身已经
    合法可见，`tts_text` 丢不丢对这两型都不构成新的泄露面，但因为
    "名字听起来和配音/朗读有关"，容易被未来某次"加个朗读功能"的改动
    误用成"读出答案的发音"（如果未来有 TTS 朗读功能,应该读的是
    **已经在 `options[]` 里显示的那几个候选项**，不是凭空多读一次
    `word`——这条留给 Round 2 ASR/TTS 工作时对照）；
  - `cloze`：`task_type/segment/sentence_en`（**补**——挖空后的例句，
    渲染必需，首轮遗漏）；`sentence_zh/cloze_word/cloze_forms/word/
    meaning_zh/meaning_en/tts_text/phonetic/pos` 全部丢弃
    （`cloze_word` 就是挖空处的正确词形，必须和 `correct_option`
    同等级对待；这里 `word` 确实要丢，因为 cloze 的题干是
    `sentence_en` 这个挖空句子，不需要也不应该额外看到原词）；
  - 输出型：`task/segment/scene_words/scene_patterns/min_scene_words/
    min_chars/topic_title`（这组没有答案字段，`task` 是新增判别键，
    `scene_words`/`scene_patterns` 本来就是设计稿已列的白名单项，
    不冲突）。
  **按题型分桶列全、而不是一句"白名单之外都丢"**，是因为同一个字段
  名在不同桶里角色不同（`word` 在 recognition 是题干、在 cloze 是
  答案）——不分桶的单一白名单描述不出这种差异，容易被下一个不知道
  这段推演的人看岔。

### 原 Finding P2-①（409 路径）现已验证为真实存在，证据从"推演"升级为
"确认"——`plan_superseded` 在 submit/finish 都会触发，且 finish 的 409
有三种不同形状需要精确区分

- **位置**：首轮报告里的同名 Finding（下方保留），现追加后端源码确证。
- **后端源码证据**（`backend/app/api/v1/endpoints/sessions.py:103-160`）：
  - `/session-items/{id}/submit`：捕获 `mastery.PlanSuperseded` →
    `409 {"message": "Plan switched; lesson session voided",
    "reason": "plan_superseded"}`——**这个 409 §3 契约表对 submit
    一行完全没有列**（submit 那一行目前只写了成功响应体，连"有 409"
    这件事都没提）。
  - `/sessions/{id}/finish`：三种互斥的 409，**形状互不相同**：
    1. `PlanSuperseded` → `{"message": ..., "reason": "plan_superseded"}`
       （**没有** `missing_item_ids` 键）；
    2. `LessonIncomplete` → `{"message": ..., "missing_item_ids":
       [非空列表]}`（设计稿唯一提到的那种）；
    3. `OutputRequirementMissing`（设计稿称"输出强制律409"）→
       `{"message": ..., "missing_item_ids": []}`（**键存在但是空
       数组**——如果设备侧的判别逻辑是"`missing_item_ids` 非 null 就
       跳回首个未交项"，这种情况下会错误地尝试跳到一个不存在的"首个
       未交项"（根本没有未交项，是输出判据没满足），需要专门用
       "`missing_item_ids` 存在且非空"与"存在但为空"两种状态区分
       "真缺项"和"输出强制律未达标"，这个区分点设计稿完全没写）。
- **结论**：这条从首轮的 P2（基于"后端 README 大概率有这个机制,建议
  校验"的推演）升级为**已核实的事实**，但严重度判定维持 P2——根因
  和上面那条 P1 不同：这条的最小修复是"把 §3 表格补全 + 加一条
  disambiguation 规则"，不涉及"设计宣称的某个交互模式根本走不到"这种
  更严重的后果，三种 409 分支无论怎么处理错了，最坏后果是"提示文案
  不准确/误跳转"，不是"某个功能完全消失"。**但既然已经从推演变成了
  确认的事实，必须在 §3 正文写清楚上面三种形状的区分方法，不能停留在
  '建议用 remote_api_demo 校验'这句话**，因为我已经把校验结果给出来了。

### 订正（并入上条 P2，不单列）：§3 契约表把 `plan_superseded` 挂在
`/lessons/{id}/start` 这件事本身**是错的**——`start` 遇到计划不匹配时
返回的是 **403**，和 `plan_superseded` 完全是两条不同的错误

- **位置**：`backend/app/api/v1/endpoints/plan.py:369-376`
  （`start_lesson`）。
- **证据**：
  ```python
  plan = studyplan.active_plan(db, current_user.id)
  if plan is None or lesson_row.plan_id != plan.id:
      ...
      raise HTTPException(status_code=403,
                           detail="Lesson does not belong to current active plan")
  if lesson_row.status == "completed":
      raise HTTPException(status_code=409, detail="Lesson already completed")
  ```
  `start_lesson` 里只有两处会抛错：无计划/计划不匹配→**403**（和
  `plan_superseded` 的字符串、状态码都不一样）；课已完成→409 "Lesson
  already completed"（设计稿这条写对了）。`plan_superseded` 这个
  409 reason **只在 `mastery.py` 的 `PlanSuperseded` 异常里出现**，
  而那个异常只在 `record_submission`/`finish_session` 里被捕获（即
  submit/finish），`start_lesson` 完全不涉及这个异常类。
- **影响**：设计稿 §3 表格"`/lessons/{id}/start`...409 `plan_superseded`"
  这一句本身是无依据的——不是"也可能发生在这里"，是"根本不会发生在
  这里"；而 `start` 真正会遇到的 403（"课不属于当前计划"）设计稿完全
  没提,§4.4 的错误处理描述里也没有任何分支对应 403。一个全新用户/
  计划被切换后的用户点开一门旧计划下的课,会直接撞上一个设计稿里不
  存在的状态码,依赖通用兜底处理（见上方"新 P1"finding 里已经建议补
  的"未预期4xx/5xx 不崩溃"）。
- **最小修复**：§3 表格 `/lessons/{id}/start` 一行的 409 列只保留
  "Lesson already completed"，新增一行"403 Lesson does not belong to
  current active plan（计划已切换/课不在当前计划里，提示+回HOME，
  与 submit/finish 的 `plan_superseded` 走同一套提示文案和处理路径——
  对用户来说这是同一件事"真正活跃的计划变了"，只是服务端在不同端点用
  了不同的状态码/reason 表达，设备侧可以统一处理成同一个 UI 结果）"。

### 新发现（P2）：`review` app 的开会话端点**不是自然幂等**，和
`mainline` 的开课端点行为不对称——设计稿把两者当同一种"single-flight
由设备侧保证"处理，没写这条差异

- **位置**：`backend/app/api/v1/endpoints/plan.py:467-493`
  （`start_review_lesson` → `studyplan.review_lesson_session`）对照
  `:353-410`（`start_lesson`，mainline 开课）。
- **证据**：`start_lesson` 在建新会话前先查"该课是否已有未结业会话"，
  命中就原样返回已有会话（`existing is not None` 分支，
  `response.headers["Idempotent-Replayed"] = "true"`）——这是设计稿
  §8 自己提到的"mainline 课/review 会话并发开会话...预期单飞由设备侧
  保证"里隐含假设的"重复调同一个 start 是安全的"这件事的真实依据。但
  `studyplan.review_lesson_session()`（`:2077-2115`）**没有任何这类
  查重逻辑**——`sess = LearningSession(user_id=user.id,
  mode=SessionMode.REVIEW); db.add(sess)` 每次调用都无条件新建一个
  会话,服务端这一侧完全不做"复习开会话"的幂等。
- **影响**：如果设备侧对两个 app 的"开会话"调用套用同一套重试/超时
  策略（比如"HTTP 超时就重试一次,反正服务端会原样返回同一个会话"——
  这正是 mainline 侧安全、review 侧不安全的那条假设），review app 在
  网络抖动场景下的一次超时重试会在服务端**创建两个复习会话**（不是
  同一个会话的两次返回），设备侧如果只记录"最后一次 start 响应的
  session_id"，前一个孤立会话里分配出去的到期复习词条（`rl.due_at`
  等调度状态是否因为被装进了一个"从未真正完成"的 session 而产生副作用,
  要看 `finish_session`/错题本/SRS 调度对"未结业 session"的处理是否
  有兜底——这部分本轮未继续往下追,但"两个 app 的 start 幂等语义不同"
  这件事本身已经是确凿的、设计稿没写的契约差异,不管后续影响大小都
  应该写进正文）。
- **影响判定**：`C/3/静默 → P2`（需要"网络超时+设备发起重试"这个特定
  时序才触发,且触发后不报错,后果大小依赖 SRS 调度对孤立会话的兜底
  强度,本轮未继续深挖该兜底是否存在——留给 §10 实现合同校验）。
- **最小修复**：§4.3/§8 补一句"mainline 的开课请求可以安全重试（服务端
  自然幂等,原样返回已有会话）；**review 的开会话请求不能重试**（服务端
  每次调用都新建会话，设备侧对 review 的 `start` 调用要么不设超时重试、
  要么重试前先靠一次`GET /plan`确认有没有已经成功的迹象，不能假设
  "超时了再调一次"是安全的）"。

### 原 Finding P2-②（新用户/无计划渲染优先级）现已验证为真实存在，且
比首轮判断的更具体——`recommend` 不是三值枚举，无计划时是 `null`

- **位置**：首轮同名 Finding（下方保留），现追加确证。
- **后端源码证据**（`backend/app/services/studyplan.py:1392-1397`，
  `next_action()`）：
  ```python
  def next_action(db: Session, user: User) -> dict:
      plan = current_plan(db, user.id)
      if plan is None:
          return {"next": None, "recommend": None}
      ...
  ```
  `/plan/next` 在用户从未建过计划时返回 `{"next": null, "recommend":
  null, ...}`——`recommend` 是**第四个值 `null`**，不是设计稿 §3 契约表
  枚举的 `lesson|review|done` 三者之一。
- **后果比首轮推演的更具体**：如果设备侧解析代码对 `recommend` 字段做
  字符串比较（`strcmp(recommend, "lesson")` 这类），而 JSON 解析库
  （如 cJSON）对 `null` 值调用 `cJSON_GetStringValue` 返回的是 `NULL`
  指针——若渲染分支没有先判空就直接 `strcmp(NULL, "lesson")`，这是
  **真实的 NULL 指针解引用,会崩溃**,不只是"渲染不对"。而且这个场景
  的触发条件是"brand-new 设备用户、从未在网页上建过计划"——对应
  申请自评③点名的"新用户是否死路"，现在有了明确答案：**不处理的话,
  不是"死路"（产品体验问题），是"设备上崩溃"（技术故障）**，比我首轮
  的判断更严重。
- **结论**：维持 P2（最小修复仍是"补一条处理 `recommend==null`/字段
  缺失时的分支,渲染引导网页文案,不往下解析 `lesson` 子对象"，不改
  架构），但**优先级应提升到和上面的 P1 一起在编码前处理**，因为这是
  每个新用户第一次开机就会撞到的路径，不是边缘场景。

---

## 复核方法（首轮，2026-09-28；部分结论已被上方补充核实修正）

本稿邀请评审攻击的五个焦点 + 开发者自评三条，我没有简单逐条"回答成立/
不成立"，而是尝试对每一条**构造具体的输入/状态序列**去验证设计正文能否
唯一推出预期行为——这是 §2.5.1 判定锚"最小修复"的操作方法：如果某个
担忧的最小修复必须改掉一段承重规范（签名/回滚/密钥/TLS/变砖类），才算
协议级；如果最小修复是"补一句明确的优先级/兜底说明"，且有既有模块同构
的答案可抄，就是实现合同，不挡 L1。

服务端权威文档（`E:/works/english_learning_system`）不在本仓库内，本轮
无法独立读取后端源码核实字段形状——这类声明按 §3.2 统一标 `CLAIM_ONLY`，
不因为"设计稿写得详细"就升级为 `VERIFIED`。本仓库内可核实的部分（既有
`pp_request`/`X-Gate-Pin` 传输层、`async_ipc_contract.md` 现有登记格式、
既有屏幕的"池/栈/代次"纪律先例）本轮做了交叉核对。

---

## Findings（首轮判定"均为实现合同/完备性,不挡L1"——此判定已被上方
"补充核实"部分修正：第一条升级为 P1 协议级,见上；下方两条的严重度
判定本身不变,但证据已从推演升级为确认,详见上方对应小节）

### P2：`plan_superseded` 的 409 契约只写在"开课"端点，未说明已开课后（submit/finish）是否也会命中同一 409，线性推进 UI 缺这条分支的显式处理

- **位置**：`mainline-design.md` §3 契约表（`POST /lessons/{id}/start` 行
  列了 `409 plan_superseded`；`POST /session-items/{id}/submit` 与
  `POST /sessions/{id}/finish` 两行**没有**列这个 409）；§4.4 只写
  "409 plan_superseded（仅 mainline）→ 提示 + 回 HOME"，没说这条分支挂在
  哪个调用点上。
- **证据/反例推演**：设计把"网页切计划"的互斥点钉在**开课那一刻**（契约
  表明确把这个 409 挂在 `start`）。但一堂课的生命周期是"开课→逐题提交
  （可能持续数分钟）→结课"，网页侵入计划的时间点不受设备控制——完全可能
  在**已经开课成功之后**才在网页上切计划。如果后端对这种情况的真实行为
  是"旧 session 的 submit/finish 继续放行到底"（session 一旦发出即不受
  后续计划变更影响），现状设计是对的，不需要改；如果后端真实行为是
  "submit/finish 也会对一个已经 superseded 的计划下的 session 返回某个
  40x"（无论是不是同一个 `plan_superseded` 字符串），现状设计没有为这条
  分支准备任何处理——取决于解析层对"未预期的 409/错误 reason"的默认
  行为是"安全地显示错误+退出"还是"崩溃/卡在原地等一个不会来的响应"，
  这两者设计稿都没写。
- **为什么不挡 L1**：这不是"正文已经唯一可推断出不可满足"的情形（§2.5.1
  负面清单只覆盖签名/回滚/密钥/TLS/变砖，本条不在其中）；也不是"必须改
  承重规范"——它是一处需要**先用申请自己在 §8 已经提议的同一个工具**
  （`remote_api_demo.py --channel learn`）把后端真实行为问清楚，再决定
  "这个分支要不要补"的事实问题,不是设计缺陷。
- **最小修复**：在 §3 契约表里补一行事实核验结果（submit/finish 在
  session 已开、计划后来被切的情况下，真实返回什么）；无论结果是哪种，
  §4.4 补一句通用兜底——"任何非预期的 4xx/5xx（不止 `missing_item_ids`/
  `plan_superseded` 两种已知 reason）→ 提示错误 + 允许 Back 退出，不假设
  特定 reason 字符串、不崩溃"，与本项目既有屏幕（PenPal/OTA）"未知错误
  一律走通用错误提示通道而不是专属解析"的既定纪律同构。绑 L2，不开新
  设计版。

### P2：新用户/未建计划场景的 HOME 渲染优先级未写明——`/plan` 的 `plan:null` 与 `/plan/next` 的 `recommend` 字段谁说了算

- **位置**：§3 契约表 `/plan` 行写"`plan:null` = 未建计划（引导网页）"；
  `/plan/next` 行只枚举了 `recommend(lesson|review|done)` 三个值；§4.1
  HOME 页描述"推荐卡 + 进度行"由两个端点的响应共同渲染，但没写两者冲突
  时（如一个新用户还没建计划，但 `/plan/next` 仍然返回了某个
  `recommend` 值）该以哪个为准。
- **反例推演**（对应申请自评焦点③"定级测不做是否导致新用户死路"）：一个
  刚拿到设备、从未在网页上走完 onboarding 的用户直接进 mainline app——
  `/plan` 大概率返回 `plan:null`；但 `/plan/next` 这个端点如果本身设计
  为"即使没有计划也要返回点什么"（比如降级返回 `recommend:"done"` 或者
  某个未枚举的第四值），设备按"三态枚举 + 进度行"的既定渲染逻辑，可能
  渲染出一个看起来"课程已完成/无事可做"的 HOME 页，而不是"请先去网页
  建计划"的引导——对新用户造成误导性的"死路"（不是技术死锁,是产品体验
  死路：新用户看不出该做什么)。
- **为什么不挡 L1**：这同样是"事实未核实"而非"协议不可满足"——取决于
  `/plan/next` 在无计划场景下的真实返回值,这个事实本可以也应该用
  §8 已经提出的同一套校准动作查清楚。即使查出来是"未枚举的第四值"，
  修复也只是"HOME 渲染加一条优先级规则"，不触及签名/回滚/密钥/TLS/
  变砖负面清单。
- **最小修复**：HOME 渲染加一条显式优先级——"`plan:null` 时无条件显示
  '在网页上设置你的学习计划'引导文案，不管 `/plan/next` 返回什么"；
  校准时把 `/plan/next` 在 `plan:null` 场景下的真实返回值记入 §3 契约表
  （目前只写了 `plan` 这一个端点会出现 `null`，没写 `/plan/next` 这个
  姊妹端点在同一场景下的行为，是一处"字段声明位置 vs 实际读取位置"的
  §9 模式 A 风险——两处各写各的,容易漏审)。

### P3（证据从"未统计,留待校准"升级为"按真实配额算术，大概率确实偏低"）：
课项数超 `PP_ML_ITEMS_MAX=20` 的拒绝阈值与后端的时长预算算术不匹配

- **位置**：§10"`PP_ML_ITEMS_MAX=20`；超限→拒绝开课"；申请 §4 自评②
  "真实主线课的项数分布我未统计，若常态超限则 v1 不可用"。
- **后端源码证据**（`backend/app/services/supply.py:33-36`
  `CAP_MINUTES`、`backend/app/services/srs.py:36-40`
  `ESTIMATED_MINUTES`、`studyplan.py:1927-1931` 热身段组装）：
  - 单课时长总帽：成人 45 分钟，其余年龄段 25 分钟（`CAP_MINUTES`）；
  - 单词类课项估时 0.5 分钟/项（`ESTIMATED_MINUTES["word"]`）；
  - 热身段预算 = `cap/2`（`studyplan.py` 热身组装注释"budget ≤
    cap/2"），即成人最多 22.5 分钟热身，按 0.5 分钟/词换算**最多 45
    条热身项**；主体课项段（新学/复习巩固）还要再占用剩余时长预算，
    理论上限同样是几十条的量级。仅热身段一项,在到期复习词条充足
    （真实用户长期未学习后回归的常见场景）时就能轻松摸到 20 以上。
  - 这不是"猜的阈值可能偏低"，是**按后端自己的时长预算公式推出偏低
    的结论**——45 分钟成人课在"到期复习词条充足"这个完全正常（不是
    异常/边缘）的场景下,热身段自己就可能装不下 20 这个上限,还没算
    上正课与输出段。
- **复核意见**：这条本身的设计（硬性上限 + 显式拒绝，而不是静默截断
  导致 finish 永远 409——设计 §8 自己点出了"防 finish 永远 409"这个
  好理由）是正确的工程选择；问题是这个阈值的具体数值,现在有了算术
  上的依据可以说"大概率偏低",不再是"没数据,不知道"。严重度维持 P3
  （最小修复仍是"调整一个常量数值"，不触及架构），但**不建议继续
  按"L2 实现期校准"延后**——按上面的算术，首批真机测试几乎必然会撞上
  这个阈值（尤其是任何一个有长期未登录、到期复习词条堆积的测试账号），
  建议直接在设计稿里把这个数字改大（比如 40-50，覆盖成人单日时长帽的
  理论上限）或改成按年龄段分级,而不是等实现阶段再发现。
- **最小修复**：§10 把 `PP_ML_ITEMS_MAX` 从 20 改为能覆盖
  `CAP_MINUTES[ADULT]=45` 分钟预算的量级（按 0.5 分钟/项估算，建议
  ≥60 留安全边际，输出型课项 5 分钟/项另算，不会让总数超过热身+正课
  的量级太多）；真实分布仍建议编码前用 `remote_api_demo.py --channel
  learn` 抽样核对一次，但这次不是"从零猜测"，是"验证改大后的数字够不
  够",性质不同。

---

## 已通过项（独立复核，非转述申请；首轮，第一条已被补充核实部分修正——
见条目内的订正说明,不是整条推翻，是"策略"和"具体字段清单"要分开看）

- **HW-MAINLINE-1 的"解析层丢弃"策略（作为策略本身）架构健壮,优于
  "渲染层过滤"——但首轮把"丢弃哪些字段"想当然地等同于设计稿原文列的
  `word`/`correct_option` 两个，这部分被上方"补充核实"订正（`word`
  对 recognition 题型是合法题干，不能丢）**：设计把答案等价字段挡在
  **解析阶段**而不是等渲染时再过滤，这个**架构选择**本身依然成立且
  更健壮——即使未来某次代码修改在渲染层引入新分支（比如给某个
  `task_type` 加调试输出），只要解析层已经把真正的答案字段（按题型
  区分后是 `correct_option`/`meaning_zh`/`cloze_word`/`cloze_forms`，
  不是笼统的"`word`/`correct_option`"两个）挡在外面，这些字段就不存在
  于设备内存里，没有"可以不小心带上屏"的数据源。申请把这条列为"评审
  焦点1"邀请攻击，我尝试从"未知 task_type 兜底路径""日志/调试输出
  路径"两个方向找是否存在遗漏的渲染分支——**策略层面**确实找不到反例
  （这条判定维持成立）；但"字段清单列对了没有"是另一个独立问题，答案
  是"没列对"（`word` 列错了方向，`meaning_zh`/`tts_text`/`cloze_word`/
  `cloze_forms`/`sentence_en` 完全没列）——这部分已经从"已通过"移到
  上方"新 P1"及其关联发现里,不再留在这里当通过项。
- **双 app 独立 worker/gen/busy/队列的拆分边界**：mainline 与 review
  各自持有完全独立的异步状态（不共享在飞请求/结果/代次），这是本项目
  已经在 WiFi/AI/PenPal/Whoami/OTA/LevelTest/WordBank 七组异步任务上
  反复验证过的既定模式（`async_ipc_contract.md` §1 表逐行都是"各管各的"，
  从未出现过两屏共享一个队列导致串扰的先例）——本轮没能构造出"mainline
  在飞时触发 review 状态变化"或反过来的路径，因为两者在架构上根本没有
  共享的可写状态。"作答组件共享导致重复实现漂移"这个风险本身被设计稿
  正确地标记为 L2 关注点（§10"实现期定，评审不阻塞"），不需要在设计阶段
  解决。
- **"每日"语义的时钟风险已被结构性消除**：设计明确"会话/进度不落本地
  持久化（v1）"——这直接切断了本项目过去在 PenPal 缓存上真实吃过的那类
  "设备时钟未同步导致 TTL/新鲜度判断失真"的坑（`penpal_api.cpp` 的
  `stale_clock` 机制就是为了弥补这个坑而存在的，详见本仓库 PenPal 缓存
  v2/v3 的既有实现）——本设计因为v1根本不做本地缓存,connect不到那套
  "clock 未同步时如何判断缓存新鲜度"的问题,是比"事后修一个 stale_clock
  标志"更干净的解法（直接不引入需要本地判新鲜度的状态）。
- **传输层复用既有基础设施，非凌空发明**：本仓库内独立核对 `pp_request()`
  （`examples/pda2/penpal_api.cpp:175`）与 `X-Gate-Pin` 头的既有处理
  （`:222`）确认设计里"既有 pp_request 自动携带"这句话属实，不是设计稿
  单方面声称——六端点封装可以直接搭在这套已验证过的 HTTP 传输层上，不
  需要重新设计认证/错误处理的地基。
- **`async_ipc_contract.md` 现有表格格式与设计提议的"补 ml/rv 两行"**
  能直接对应——本仓库核对了表里 Whoami/OTA/PenPal/LevelTest/WordBank
  五种风格各异的队列/busy/代次组合，设计稿选择的"任务句柄即 busy、单飞"
  风格（§4.3）与 Whoami/LevelTest/WordBank 三者同款，是既有先例内的
  选择，不是新发明的第四种模式。

---

## 验证说明（首轮部分，见下方"验证说明补记"覆盖其第一条）

- ~~服务端权威文档/API 不在本仓库，本轮**未能独立核实**任何后端字段形状、
  409 reason 枚举完整性、`/plan/next` 在 `plan:null` 场景下的真实返回值~~
  ——**此条已过期**：用户告知该仓库实际在本机 `~/english_learning_system/`
  后,上方"补充核实"一节已直接读取 `backend/app/services/studyplan.py`/
  `mastery.py`、`backend/app/api/v1/endpoints/{plan,sessions}.py` 的
  源码,三条此前的 `CLAIM_ONLY` 推演已升级为 `VERIFIED`（CODE，逐行引用
  了文件路径与具体代码块，非转述）。
- 本仓库内独立核对：`examples/pda2/penpal_api.cpp`（`pp_request`/
  `X-Gate-Pin`）、`docs/async_ipc_contract.md`（现有七行登记格式）、
  本项目既往关于 PenPal 缓存 `stale_clock` 机制的既定实现（用于判断本
  设计"不做本地缓存"是否真的绕开了那类坑，而不是重新制造一个）。
- 无设备、无 pio：本稿是纯设计评审，§3.3 L1 的证据最低要求本就只是
  DOC/SPEC VERIFIED，不需要 BUILD/HW，此条不构成缺口；本轮补充核实把
  证据从 DOC/SPEC 提到了更高的 CODE 级别，不是"超额要求"，是正好把
  首轮唯一的证据缺口补上了。

### 验证说明补记（2026-10-23）

- 读取范围：`~/english_learning_system/backend/app/services/studyplan.py`
  （`next_action`、`_word_prompt`、`_build_recognition_prompt`、
  `_build_cloze_prompt`、`start_lesson` 课项段落、`review_lesson_session`）、
  `mastery.py`（`record_submission` 的 `_guard_active_plan` 守卫路径）、
  `api/v1/endpoints/sessions.py`（`submit`/`finish` 的全部 409 分支）、
  `api/v1/endpoints/plan.py`（`/plan`、`/plan/next` 路由）、`backend/
  README.md` 的"二百五十四"条变更记录（`D4` 完课纵深防御，交叉印证
  `plan_superseded` 机制确实存在且有配套测试）。
- 未读：`reviews.py`（`/reviews/lesson/start`，review app 对应端点——
  review 侧的课项组装经 `review_lesson_session` 核对过，只用
  `_word_prompt`（task_type 恒为 `recall`），不触及本轮发现的
  `task_type`/`segment+task` 双判别键问题，这条可以确认 review app
  不受影响；但端点层 `reviews.py` 本身的 409/鉴权细节本轮未读，不在
  本轮三条发现的范围内，如需也可以补）、`seed_data`/真实课数据（第 5
  项"ITEMS_MAX=20 校准"仍未验证，维持 P3，不是本轮新增的范围）。

---

## 对称三格（§7.2）

1. **全区间**：本稿是该主题第一轮评审（v1 从未送审），按全篇而非 delta
   评审——逐章（§1-§10）都读过，不只看申请"评审焦点"五条点名的范围；
   额外主动检查了 §3 契约表里**没有被点名**的"`/plan` 与 `/plan/next`
   的字段交叉一致性"（即 P2 的第二条发现），这条不在申请的五个焦点里。
2. **独立复跑**：无后端源码可读，无设备、无 pio——本轮证据止于
   DOC/SPEC 级的交叉引用一致性检查 + 本仓库内既有基础设施/先例的核对，
   如实标注，未假装验证了本仓库之外的事实。
3. **申请"最没把握"3 条 + 评审焦点5条**：逐一见上文——焦点①（HW-
   MAINLINE-1）判定**成立且给出了比申请描述更强的架构理由**；焦点②
   （409 组合）与自评③（新用户死路，映射到焦点③）**各自构造出了具体
   反例**，列为 P2（见 Findings）；焦点④（双app拆分）、焦点⑤（时钟
   语义）均未能构造出反例，判定成立；自评①（task_type 兜底）判定
   成立并给出补强理由；自评②（ITEMS_MAX 校准）认可为 L2 任务，不需要
   现在验证。

---

## §9 自审清单【D】/【ALL】（首轮打钩，第 24 条结论已被补充核实修正——见紧随的订正行）

- [x] 21 字段/键名一致性：`/plan` 的 `plan:null` 与 `/plan/next` 的
      `recommend` 枚举在**同一场景**下的交叉一致性未冻结——这正是本轮
      发现的 P2 第二条,不是通过项，如实记录在 Findings 而非这里打钩。
- [x] 22 代码块可执行：本稿没有可解析的 JSON/伪代码示例块需要核对
      "示意 vs 可执行"（§3 是表格描述，不是贴代码），本条 `NOT_APPLICABLE`。
- [x] 23 ✅/⏸ 对应证据：本稿没有出现 ✅/⏸ 符号误用（设计稿阶段，验证
      状态用的是"实采校准""纸面推演"这类明确措辞，未冒充已验证）。
- [ ] **24（订正）无协议/架构 P0/P1 时是否给了 L1+A：首轮打勾判"是"，
      **补充核实后推翻**——`task_type`/`segment+task` 双判别键的契约
      错误经后端源码证实，按"按正文必错"够得上协议级 P0/P1，本条本轮
      判**不通过**，结论改判 C。
- [x] 25 复审是否默认 delta：不适用——本稿是首轮评审（v1 未送审），按
      §2.5.3 的适用条件（"已有上一轮评审结论的修订稿"）本条不触发，
      本轮按全篇评审，已在对称三格①说明。
- [ ] 26（订正）`NO_CONTRACT`/既有模块同构缺口是否被误升为 P1：首轮判
      "否"（三条发现都归 P2/P3）；补充核实后**其中一条确实需要升为 P1**——
      不是"误升"，是首轮证据不足导致**误降**，订正方向相反，一并记录在
      这里以免下次只看打钩表漏看訂正说明。

---

## 审批意见（已按"补充核实"修订，以本节为最终结论）

- [ ] L1+A 开工接受　[ ] B 退回重构　[x] **C 部分接受——先改 §3 正文
      再开工，不需要推倒重构**

**C 的 required action（改哪一节规范，不是"把实现细节写完整"）**：
1. **§3 契约表"设备消费字段"与 §4.4"未知 task_type 兜底"两处正文必改**：
   按上方"补充核实"给出的分桶（`segment!="output"` 走 `task_type`
   ∈ `{recall,recognition,cloze}`；`segment=="output"` 走 `task`
   ∈ `{topic_writing,sentence_making}`）重写判别逻辑，否则"输出=文本
   造句"这个核心交互按现在的文字实现必定不可达。这条是本轮改判 C 的
   唯一理由，**不是"建议"，是开工前必须先落在设计正文里的一句话量级
   的修正**——证据和改法我已经在上方写全，不需要再开一轮"实采校准"。
2. **白名单按 task_type 分桶列全丢弃/保留清单，且按题型区分 `word`
   字段的角色**（`word` 对 recognition 是合法题干必须保留，对 cloze
   才是答案必须丢弃——不是统一丢弃；真正要和 `correct_option` 并列
   明确列为"丢弃"的是 `meaning_zh`（认读题答案等价）与
   `cloze_word`/`cloze_forms`（挖空题答案），这两组"名字听起来无害"
   的字段不能只靠"白名单之外都丢"隐含覆盖）；`cloze` 型的白名单补
   `sentence_en`（渲染必需，首轮遗漏，目前会被过度丢弃）。
3. §3 契约表：① 把 `plan_superseded` 从 `/lessons/{id}/start` 一行
   移除，改成该行真实会报的 **403**（"课不属于当前活跃计划"）；
   ② 补全 submit 的 `409 plan_superseded`、finish 的三种互斥 409
   形状（`reason` 字段存在 vs `missing_item_ids` 非空 vs
   `missing_item_ids` 空数组三者如何区分）；③ §4.4 补一条通用"未预期
   4xx/5xx（含 403）不崩溃、允许退出"的兜底，start 的 403 与
   submit/finish 的 `plan_superseded` 建议统一处理成同一个 UI 结果
   （"计划已变更，返回首页"）。
4. HOME 渲染补"`recommend` 可能是 `null`（含 `plan` 为 null 的全新
   用户）——设备侧解析先判空再比较字符串，不走 `strcmp(NULL, ...)`
   这类会崩溃的路径，null 时显示'去网页设置学习计划'引导文案"。
5. §4.3/§8 补一句"mainline 开课请求自然幂等可安全重试；**review 开
   会话请求服务端不做幂等,不能假设重试安全**"（新发现，P2，不阻工但
   建议随第 1/2 项一起写进这轮正文修订，省得再开一轮）。
6. `PP_ML_ITEMS_MAX` 建议从 20 直接改大（按成人 45 分钟预算×0.5分钟/
   项的算术，建议 ≥60，不再是"留给实现期校准"——算术已经给出，详见
   上方 Findings；维持 P3，不是本轮改判 C 的理由，但建议和第 1-3 项
   一起改掉，不要分两轮）。

以上第 1、2 两项是本轮改判 C 的依据,必须先改进设计正文（§3/§4.4）、
经一次**delta 复审**（只核这两节改动，不必重走全篇，按 §2.5.3）确认后
才能转 L1+A 开工；第 3-6 项维持 P2/P3，可以作为同一次正文修订的附带
内容一起改掉,也可以留到 L2 实现合同清单,不单独构成挡开工的理由。

**与同主题其它评审方结论的分歧登记（§0 原则 2"证据>票数"，不比票数比
证据）**：`docs/reviews/mainline-design-review-result-5b32605-grok.md`
已落 **L1+A**（经 `CLAUDE.md` working note 交叉确认，该结果同样独立
核实了"output 题型无 `task_type`"这一事实，与本结果的新 P1 指向同一处
证据——即两份结果对"问题是否存在"这件事本身并无分歧，分歧在于**这条
证据该归 P1（挡 L1,本结果）还是实现合同（不挡 L1,grok 结果）**。本结果
的判断依据是 §2.5.1 的判定锚"最小修复是否要改一段承重规范"——这里
最小修复确实要改 §3 正文"设备消费字段"这一句（从单判别键改成双判别键），
不是"实现期二选一的细节"；且后果是"宣称的核心交互在当前文字下必定不
可达"，不是"有一个可行实现、只是没写清楚"。审批人/下一位评审方如果
认为这条应该维持 grok 结果的"实现合同"定性，请在新一轮结果里针对**这
一条分歧本身**给出反例，而不是简单比较两份结果谁判得宽——本文件和
grok 结果都不覆盖、各自成立，按惯例**都保留，不互相覆盖**。
