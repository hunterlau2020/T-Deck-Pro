# 学习主线设备端设计（mainline + review 双应用，v3.2 设计稿）

> 状态：**v3.2 设计稿；v3 已过 L1 复审（四方全 A，G-开工通过，不开
> v4——grok）；v3.1 吸收复审 IC 修正（正文 5 处）；**v3.2 新增 §3.4
> 答案封存/揭示通道**（真机验收驱动的 HW-MAINLINE-1 语义细化，随
> v1.30 代码评审一并核）**。申请人 ZCode（GLM 代理），2026-10-24。
> 评审依据 `docs/review_guide.md` v1.3 设计稿口径。
> **v1→v2**：复习课拆独立 Review 应用（用户指令）。
> **v2→v3**：按五方评审（claude/codex/qwen C + deepseek/grok A，2-3 裁决）
> 修正 P1-1（白名单→条件化显示矩阵 + 判型双键）并同批闭合 qwen
> P2-1/P2-2/P2-3/P3-1/P3-2/P3-3——各发现的服务端源码证据
> （`studyplan.py`/`sessions.py`/`schemas/*` file:line）见各结果文件。
> 服务端权威：`FSD_LM_DESIGN.md` v4.2；端点契约
> `backend/scripts/remote_api_demo.py`。

## 1. 背景与目标

（承 v2，无变更）后端"学习主线"（目标 × 场景 × 计划 × 课）自 v1.20.0
起对 API-Key learn 通道全面开放主线课开课/作答/结课。设备侧拆两个
单一职责应用：

| 应用 | 职责 | 服务端面 | 目标版本 |
|---|---|---|---|
| **mainline（学习主线）** | 今日主线课：推荐 → 开课 → 作答 → 结课 | `/plan` `/plan/next` `/lessons/{id}/start` `/session-items/{id}/submit` `/sessions/{id}/finish` | **1.30** |
| **review（复习课）** | 到期复习集训：开复习会话 → 作答 → 结课 | `/reviews/lesson/start` + 同款作答/结课端点 | **1.31** |

设备定位边界（目标）：学习执行端，非计划管理端（写面网页 Bearer-only，
设备只读引导）；定级测留后续轮。

## 2. 范围

**mainline v1（1.30）**：今日推荐页（`GET /plan/next` 四态 + `/plan`
进度）→ 开课 → 逐题作答（**认读=选项、拼写=键盘、完形=键盘、输出=
文本造句**）→ 结课得分页。`recommend=review` 显示指引行，不开复习会话。

**review v1（1.31）**：进入显示 `due_total` + Enter 开会话 → 同款作答
→ 结课。毕业词抽查热身由服务端组装，设备无感。

**口语题（两 app 同规）**：`skipped=true` 提交 + 提示行 "voice: ASR in
round 2"；Round 2 接 MiniMax ASR。

**界面英文**；内容中文走 Font_Hanzi_16（验收含 CJK 抽查）。

## 3. 服务端契约与字段矩阵（v3 核心修订）

鉴权：`X-API-Key`（learn）+ `X-Gate-Pin`（pp_request 自动携带）。

### 3.1 判型双键（qwen P1-1 / codex / claude 新 P1）

- 词项题判型键 = **`task_type`**（`recognition` / `recall` / `cloze`）；
- **输出题判型键 = `segment=="output"` + 独立字段 `task`**
  （`topic_writing` / `sentence_making`）——**不走 `task_type`**，
  不得把 `task` 当未知 `task_type` 误入降级；
- **主线装配现网无 speak 项**（grok 复审核实）——speak 行仅作矩阵
  预备；**真未知类型 → 停在题卡**（显示 segment 文本 + "unsupported
  type"），**禁止自动 skipped**（IC-1）。

### 3.2 HW-MAINLINE-1（条件化显示矩阵，替代 v2 平面白名单）

纪律不变：**非下表"渲染"集一律解析层丢弃、永不进设备结构体；判分
完全信任 submit 响应**。字段角色按题型互换（`word` 对认读是题干、对
拼写是答案），故允许集/禁止集**逐题型**定义：

| 题型（判型键） | 渲染（进结构体） | 丢弃（答案/答案等价） |
|---|---|---|
| `task_type=recognition` | `word`, `phonetic`, `options[]`, `pos`(可选) | `correct_option`, `meaning_zh`, `meaning_en`, `tts_text` |
| `task_type=recall`（拼写） | `meaning_zh`, `phonetic`, `pos`, `meaning_en`(服务端已掩码为 `____`) | `word`, `tts_text` |
| `task_type=cloze`（完形，高频：new_study 课常态 25-50% 项） | `sentence_en`(含空), `sentence_zh`, `meaning_zh` | `word`, `cloze_word`, `cloze_forms`, `tts_text` |
| `segment=output`（`task` 缺省按 `sentence_making`，不得因缺 `task` 掉进未知） | `topic_title`, `scene_words[]`, `scene_patterns[]`, `min_scene_words`, `min_chars`, `task`(判型), `segment` | —（无词项答案；`scene_words` 是**合法脚手架**非半答案——完课判据要求引用且必须可见，服务端 `studyplan.py:1888-1897`） |
| speak（矩阵预备，现网无） | `segment` 文本 | 其余全部（skipped 路径） |
| 真未知类型 | `segment` 文本 | 其余全部；**停在题卡禁止自动 skipped**（IC-1） |

- `scaffold_level` 现网恒 null（组装处全部 `None`），矩阵留备位。
- **枚举外字段一律解析层丢弃**（含 `word_id`、start 顶层
  `lesson_id/lesson_index/lesson_kind/title`、review start 的
  `graduated_sampled_word_ids/included_count`——P3-3，已核实无 prompt
  外答案泄漏面）。
- UI 与解析器**共用同一张表**（单一事实源，实现期以常量表落地）。

### 3.4 答案封存/揭示通道（v3.2；HW-MAINLINE-1 语义细化）

真机验收反馈："提交后的结果弹窗应显示正确答案"。原矩阵"解析层丢弃"
的本意是**作答前答案不可达 UI**；提交成功后答案已无揭示风险（服务端
判分已完成，网页端同样于提交后展示正确答案）。v3.2 细化：

- 解析时答案字段（recognition 的 `correct_option`、recall 的 `word`、
  cloze 的 `cloze_word`；output 无）**封存（seal）**进 penpal_api 内部
  vault（静态数组，UI 不可见、不可枚举，随会话重建而重置）；
- 唯一释放路径 `penpal_ml_reveal_answer(item_id)`——调用时机约定为
  **该 item 的 submit 已成功之后**（UI 侧纪律，评审 L2 核对点）；
- 判分仍完全信任 submit 响应；答案揭示仅用于结果反馈显示；
- 不变式：作答前（submit 前）任何 UI/日志路径拿不到答案——原矩阵
  "丢弃"列对**显示**语义不变，封存是对"显示后"的补充。

### 3.3 端点表（v3 修订版）

| 端点 | 用途 | 归属 | 设备消费字段与错误路径 |
|---|---|---|---|
| `GET /plan/next` | HOME 推荐 | mainline | `recommend(lesson\|review\|done\|**null**)`、`due_minutes`、`lesson{...}`；**null=未建计划（P3-1 第四态）** |
| `GET /plan` | HOME 进度行 | mainline | `plan{goal_exam, total_lessons, completed_lessons, cursor_lesson, course_completed, consolidating, due_total}`；`plan:null` 引导网页 |
| `POST /lessons/{id}/start` | 开主线课 | mainline | `session_id, mode, items[]{id, target_layer, prompt, submitted}`（prompt 按 §3.2 矩阵裁剪）；**计划不匹配/无计划 → 403**（`plan.py:370-375`，~~409 plan_superseded~~ v3 笔误，IC-2 修正）；409 already-completed（字符串无 reason 键） |
| `POST /session-items/{id}/submit` | 逐题 | 共享 | body `{user_input?, skipped?}`；响应 `{score_write, combined, passed(**nullable**), quality(**nullable**), is_retry, in_errorbook, mastery_layer}`（P3-2：输出项 passed/quality=null，状态行以 combined/score_write 兜底）；**输出项禁止 skipped 提交**（skipped 仍会空表 409 且重开循环，claude/grok 复审核实——服务端 `output_fallback_ok("")` 恒假）；**409 `plan_superseded` → 会话作废 + 回 HOME（P2-3）** |
| `POST /sessions/{id}/finish` | 结课 | 共享 | `{score, xp_awarded, correct_items, total_items, skipped_items}`；409 判定**优先级：`reason==plan_superseded` 优先于 `missing_item_ids`**（superseded 体无该键，**缺键不得当空表**，IC-2）→ 回 HOME；409 非空 `missing_item_ids[]` → 跳首个未交项；409 空表 = 输出强制律 → 显示 message + 跳回**最后一个未达标** `segment=="output"` 项（items 中无 output 项 → 仅显示 message 停 RESULT，qwen Nit-1 兜底） |
| `POST /reviews/lesson/start` | 开复习会话 | review | `{session_id, mode, items[], due_total}`（`graduated_sampled_word_ids/included_count` 丢弃） |

## 4. 设备侧架构

### 4.1 mainline（1.30）

三页 HIDDEN 切换：**HOME**（推荐卡**四态**：lesson 课卡 / review 指引
"N due — see Review app" / done 通关巩固 / **no plan → "create a plan
on web" 指引行**（P3-1）+ 进度行）。**SESSION**（题卡，§4.4）。**RESULT**
（得分/XP/对错统计；Enter 回 HOME 重拉）。

### 4.2 review（1.31）

两页：**REVIEW**（进入仅 `GET /plan` 取 `due_total`——**不创建会话**；
Enter 才 POST start；due=0 显示 "no review due"；丢响应/重复 Enter
有限恢复 = IC-ML-02）→ 复用 SESSION/RESULT（mode=review 无主线进度
语义）。

### 4.3 异步（wa_* 单飞四件套）

两 app 各自 worker（`ml_*`/`rv_*`）+ 独立 gen/busy/队列，不共享在飞
状态。栈 1024×8、prio 1、队列 4×ptr 一次建、2s 有界自释放。会话状态
UI 线程静态所有权。

### 4.4 作答交互（共享组件）

- 认读 = 选项行 1-4（数字/触摸，Enter 确认，1-based）；
- **拼写 = textarea（线索=meaning_zh/phonetic/pos；作答=键盘输入词）**；
- **完形 = textarea（题干=sentence_en 挖空句 + sentence_zh；作答=输入
  词形）**（P2-1：与拼写组件同构复用，非 skipped 兜底）；
- 输出 = textarea（topic_title/scene_words 脚手架可见，造句/话题，完课
  判据须引用 ≥1 场景词——409 空表路径见 §3.3）；
- speak（现网无）= segment 文本 + 自动 skipped；**未知类型 = 停在
  题卡**显示 "unsupported type"，不自动 skipped（IC-1）；**输出项无
  skip 入口**（只 textarea 一条路径，IC-2）；
- 在飞 Enter 缓冲、busy 反馈；已提交题 Enter=重提交（`is_retry`）；
- 409 双路径按 §3.3 表（superseded 回 HOME；missing 空/非空分治）。

### 4.5 数据结构（L2 细化）

`pp_ml_next_t / pp_ml_plan_t / pp_ml_session_t{items[PP_ML_ITEMS_MAX]} /
pp_ml_submit_t / pp_ml_finish_t / pp_ml_review_t`——纯值成员，prompt
**按 §3.2 矩阵裁剪后**仅存渲染集字段。

## 5. 安全

- 认证沿用既有链，无新增秘密。
- **HW-MAINLINE-1 = §3.2 条件化矩阵**（不可协商）：评审专攻"任何
  路径把当前题型的'丢弃集'字段带上屏/日志/结构体"。
- 会话/进度不落本地持久化（v1）；本地缓存留 Round 2。

## 6. 字库与文案

（承 v2，无变更）界面英文；内容中文走 Font_Hanzi_16，验收含 CJK 出字
抽查；服务端新增词越出 3955 字全集 → 重跑字体管线。

## 7. 回归清单（真机，G-真机）

**mainline（1.30）**：1. HOME **四态**渲染 + 进度行与网页一致；2. 开课
→ 认读（1-4）/拼写（中文线索出字）/完形（挖空句渲染）/输出（场景词
脚手架可见）逐题作答 → finish RESULT 与网页一致；3. finish 空表 409 →
显示判据 + 跳回输出项（P2-2 路径）；4. 网页切计划 → 开课/提交/结课三
处 409 superseded → 回 HOME；5. 在飞键规则 + gen 丢弃；6. 布局遥测三页。
**review（1.31）**：due_total/开会话/同款作答/双 app 独立 worker 互不
串扰。

## 8. 风险与开放问题

- ~~平面白名单自相矛盾~~（v3 已改条件矩阵）；
- ~~cloze 未枚举~~（v3 已入 §2/§3.2/§4.4）；
- ~~missing_item_ids 空表未定义~~（v3 §3.3 分治）；
- ~~submit/finish 409 缺列~~（v3 §3.3 已补）；
- prompt 载荷与矩阵的漂移防线：`remote_api_demo --channel learn` 实采
  校准 + 枚举外字段一律丢弃 + 未知题型降级；
- 双 app 同账号并发开会话服务端语义 → 实现前 demo 验证；
- **矩阵 vs 后端漂移**：L2 实现申请钉后端 commit SHA（grok IC-8；
  后端工作树有未提交改动，HEAD 键集与矩阵一致已核）；
- 认读题"音义"语义（网页 TTS 播音）在设备为文字题干等价——Round 2
  可走 TTS 播 `tts_text`（播放不上屏，不违反矩阵；qwen 属主决策注）。

## 9. 版本与入库

1.30 = mainline；1.31 = review。CHANGELOG 按日；契约表 §1 补 ml/rv 行；
L1 复审通过后编码，代码轮另发申请。

## 10. 实现合同（L2，绑实现 G-合入）

- `PP_ML_ITEMS_MAX = 20`；超限拒绝开课；
- `user_input` ≤1000 字（服务端约束）+ 显示宽度截断；
- worker 栈 1024×8；结果堆拷贝，消费后 delete（规则 1/7）；
- **§3.2 矩阵落地为解析器与 UI 共用的常量表**（单一事实源；新题型
  = 改表 + 重验）；判型先查 `segment=="output"`+`task`，再查
  `task_type`（双键顺序固定）；
- **JSON 容错矩阵（L2 交付物）：枚举外字段一律丢弃；`passed/quality`
  nullable；`recommend` nullable；start/review-start 顶层多余字段
  丢弃**（P3 系列收口）；
- 作答/SESSION/RESULT 组件可复用形态（实现期定）；
- 首上机布局遥测（`[ML] layout:`）。
