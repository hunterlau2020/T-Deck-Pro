# 学习主线设备端设计（mainline，v1 设计稿）

> 状态：**v1 设计稿，待 L1 设计评审（G-开工门槛）**。申请人 ZCode（GLM
> 代理），2026-09-28。评审依据 `docs/review_guide.md` v1.3 设计稿口径
> （§0-§4、§2.5 收口、§6、§9【D】、§10）；结论 = L1 是否达标 + A/B/C。
> 服务端权威文档：`E:/works/english_learning_system/docs/FSD_LM_DESIGN.md`
> （v4.2 冻结）；端点契约以 `backend/scripts/remote_api_demo.py` 为准。

## 1. 背景与目标

后端"学习主线"（目标 × 场景 × 计划 × 课）自 v1.20.0 起**对 API-Key
learn 通道全面开放**主线课开课/作答/结课（后端 Changelog"二百七十"）。
本设计把这条链路落到 T-Deck Pro：新应用屏 **mainline（学习主线）**，
设备作为**学习终端**消费主线——今日推荐、开课、逐题作答、结课得分。

**设备定位边界（目标）**：设备是学习执行端，不是计划管理端。计划生成/
切换目标/每日配额（`/plan/regenerate|switch|preview|confirm`、
`/onboarding/goal|daily-quota`）为网页 Bearer-only——设备只读消费并在
状态行引导"manage on web"。定级测（`/onboarding/level-test[/skip]`）
learn 可调，留待后续轮（本版不做）。

## 2. 范围

**v1（本设计稿，目标版本 1.30，单评审轮）**：完整作答闭环——
今日推荐页（GET /plan/next + 进度）→ 开课 → 逐题作答（认读=选项、
拼写=键盘输入、输出=文本造句）→ 结课得分页。口语题（speak/跟读类）
按 `skipped=true` 提交（0 分入分母、重开错题周期），提示行注明
"voice: ASR in round 2"。

**Round 2（另行申请）**：MiniMax ASR 口语题（录音→转写→speak_text，
注意 `ota_busy()` 互斥）；`/plan/history` 历史页；纯复习课
`/reviews/lesson/start`（v1 仅消费 /plan/next 的 recommend=review 指引，
提示"review drill: open on web or wait"？——**否**：review 端点
learn 可调且不占配额，v1 直接支持，见 §4.5）。

## 3. 服务端契约（设备消费面，learn 通道）

鉴权：`X-API-Key`（learn scope）+ `X-Gate-Pin`（既有 pp_request 自动
携带）。全部阻塞调用，仅 worker 内使用。

| 端点 | 用途 | 设备消费字段 |
|---|---|---|
| `GET /plan/next` | HOME 页 | `recommend(lesson\|review\|done)`、`due_minutes`、`lesson{id, lesson_index, lesson_kind, title, scenario_code, est_minutes, status}` |
| `GET /plan` | HOME 进度行 | `plan{goal_exam, total_lessons, completed_lessons, cursor_lesson, course_completed, consolidating, due_total}`；`plan:null` = 未建计划（引导网页） |
| `POST /lessons/{id}/start` | 开课 | `session_id, mode, items[]{id, target_layer, prompt{task_type, segment, options[], scene_words}, scaffold_level, submitted}`；409 `Lesson already completed`；409 `plan_superseded` |
| `POST /session-items/{id}/submit` | 逐题 | body `{user_input?, skipped?}`；响应 `{score_write, combined, passed, quality, is_retry, in_errorbook, mastery_layer}` |
| `POST /sessions/{id}/finish` | 结课 | `{score, xp_awarded, correct_items, total_items, skipped_items}`；409 `missing_item_ids[]`（跳回首个未交项）；输出强制律 409 |
| `POST /reviews/lesson/start` | 纯复习课 | `{session_id, mode, items[], due_total}`（不占课序/不耗配额） |

**HW-MAINLINE-1（安全不变量，本设计的承重线）**：`prompt` 内的
`word`、`correct_option` 是服务端参考答案——**penpal_api 解析层直接
丢弃，永不进入设备侧结构体**；判分完全信任 submit 响应。渲染白名单 =
`task_type / segment / options[] / scene_words / scaffold_level`。
未知 `task_type` → 渲染降级（segment 文本 + skipped 提交路径），不猜。

**实现合同（L2，绑实现 G-合入，不在本稿展开）**：JSON 字段容错矩阵、
`PP_ML_ITEMS_MAX`（目标 20，超限开课即拒）、user_input ≤1000 字截断、
Idempotency-Key 使用（start 自然幂等，不强制携带）。

## 4. 设备侧架构

### 4.1 屏幕（照 ui_newdict/ui_leveltest 范本）

三页 `LV_OBJ_FLAG_HIDDEN` 切换（单刷、禁滚动）：

- **HOME**：今日推荐卡（recommend 三态：lesson 课卡=kind/index/title/
  分钟；review=due 指引；done=通关/巩固态）+ 计划进度行（goal、x/y、
  due_total）+ 行池 ≤7 行（后续轮放课表）。Enter = 开课/开复习。
- **SESSION**：当前题卡。认读题 = 选项行 1-4（键盘数字/触摸行，Enter
  确认，1-based 提交）；拼写/输出题 = 单行 textarea（键盘输入，Enter
  提交，Backspace 删字）；speak 类 = 自动 skipped 提交。状态行
  `item i/n` + 提交后反馈行（passed/quality）。进度 `>` 游标，无回退
  （线性推进；409 缺项时自动跳回首个未交项）。
- **RESULT**：得分 / XP / 对错统计（finish 响应）；Enter 回 HOME
  （重拉 /plan/next）。

布局预算：沿用 §33 教训——内容栈 ≤ 容器高 −10px，create 时一次性
布局遥测串口打印；状态行黑字、先渲染后消息。

### 4.2 异步（wa_* 单飞四件套，契约 §1 补行）

`ml_task_func / ml_start / ml_consume`：请求快照 `ml_req_t`（gen/kind/
base/key/lesson_id/item_id/payload 字符串按值拷贝）；结果 `ml_msg_t`
**首字段 gen**；`s_ml_task` volatile busy、`s_ml_gen` entry/exit +1；
队列 4×ptr 一次建；`xQueueSend 2s` 有界自释放；栈 1024×8、prio 1。
会话状态（`session_id`、items 数组、当前游标、submitted 位图）为 UI
线程静态所有权，worker 不读。

### 4.3 交互与吞键

在飞 Enter 缓冲（v1.18），其余键 busy 反馈（§33 A-4）；SESSION 页
作答已提交后同题再按 Enter = 重提交（服务端 `is_retry` 建模，安全）；
409 `plan_superseded` → 提示行 + 回 HOME。

### 4.4 数据结构（草案，L2 细化）

`pp_ml_next_t / pp_ml_plan_t / pp_ml_session_t{items[PP_ML_ITEMS_MAX]}
/ pp_ml_submit_t / pp_ml_finish_t`——纯值成员，字符串定长截断
（`s_copy_disp` UTF-8 边界 + "..."）。

### 4.5 纯复习课

`recommend=review` 时 Enter 走 `POST /reviews/lesson/start`（learn 可
调、不占配额）；作答/结课复用 SESSION/RESULT 页（`mode=review` 时
RESULT 不显示 XP 进度语义差异——服务端响应同构）。

## 5. 安全

- 认证沿用既有链：`X-API-Key`（learn）+ `X-Gate-Pin`（NVS→env.cfg→
  默认），无新增秘密。
- HW-MAINLINE-1 是本设计的不可协商项：评审请专攻"是否存在任何路径把
  参考答案带上屏"。
- 会话/进度**不落本地持久化**（v1）——断电丢会话，重新开课走服务端
  自然幂等；本地缓存（PenPal cache v3 模式）留 Round 2。

## 6. 字库与文案

界面英文（零字库风险）。学习内容的中文（释义/场景词）走
Font_Hanzi_16——词库即字库 3955 字提取源，验收含 CJK 出字抽查；若
服务端新增词越出全集 → 重跑字体管线（TODO 既有预案）。

## 7. 回归清单（真机，G-真机）

1. HOME 三态渲染（lesson/review/done）+ 进度行数字与网页一致；
2. 开课 → 认读题 1-4 作答 → 提交反馈 → 推进；拼写题键盘输入含中文
   释义出字；输出题造句提交；speak 题 skipped 提示；
3. 全项提交后 finish → RESULT 分数/XP 与网页一致；缺项 409 → 跳回；
4. 网页切计划后设备开课/提交 → 409 `plan_superseded` → 回 HOME；
5. 在飞 Enter 缓冲、busy 反馈、退出屏后在飞结果丢弃（gen）；
6. 布局遥测：三页无裁切（首上机数行数）。

## 8. 风险与开放问题

- prompt 显示契约未逐 task_type 冻结 → 以 `remote_api_demo.py
  --channel learn` 实采校准 + 渲染白名单兜底；
- 课项超 `PP_ML_ITEMS_MAX` → 开课显式拒绝（防 finish 永远 409）；
- `recommend=review` 但 due=0 的边缘 → HOME 显示 done 语义，不发起；
- 时钟未同步对"每日"语义的影响：服务端权威（suggested_date/到期都在
  服务端算），设备不本地判日。

## 9. 版本与入库

目标版本 **1.30**；`CHANGELOG` 按日条目；`TODO` 登记 Round 2（ASR/
历史页/本地缓存/定级测）；`async_ipc_contract.md` §1 补 ml 行；评审
通过（L1 + G-开工）后编码，代码轮另发评审申请（§7.1 全段）。

## 10. 实现合同（L2，绑实现 G-合入）

- `PP_ML_ITEMS_MAX = 20`；超限 → "lesson too large" 拒绝；
- `user_input` 上限 1000 字（服务端约束），设备 textarea 按显示宽度
  再截；选项 ≤4 行渲染（超出拒绝开课——当前契约仅认读 4 选项）；
- ml worker 栈 1024×8；结果 msg 含 session/items 时为堆拷贝，消费后
  `delete`（规则 1/7）；
- 首上机布局遥测三页各一行（`[ML] layout: ...`）。
