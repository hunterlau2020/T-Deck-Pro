# 学习主线设备端设计（mainline + review 双应用，v2 设计稿）

> 状态：**v2 设计稿，待 L1 设计评审（G-开工门槛）**。申请人 ZCode（GLM
> 代理），2026-09-28。评审依据 `docs/review_guide.md` v1.3 设计稿口径
> （§0-§4、§2.5 收口、§6、§9【D】、§10）；结论 = L1 是否达标 + A/B/C。
> **v1 → v2 变更（2026-09-28，用户指令）**：纯复习课从 mainline 应用
> 拆出为**独立 Review 应用**——单 app 不过载；作答交互组件两 app 共享。
> 服务端权威文档：`E:/works/english_learning_system/docs/FSD_LM_DESIGN.md`
> （v4.2 冻结）；端点契约以 `backend/scripts/remote_api_demo.py` 为准。

## 1. 背景与目标

后端"学习主线"（目标 × 场景 × 计划 × 课）自 v1.20.0 起**对 API-Key
learn 通道全面开放**主线课开课/作答/结课（后端 Changelog"二百七十"）。
本设计把这条链路落到 T-Deck Pro，拆为**两个单一职责应用**：

| 应用 | 职责 | 服务端面 | 目标版本 |
|---|---|---|---|
| **mainline（学习主线）** | 今日主线课：推荐 → 开课 → 作答 → 结课 | `/plan` `/plan/next` `/lessons/{id}/start` `/session-items/{id}/submit` `/sessions/{id}/finish` | **1.30**（本稿主体） |
| **review（复习课）** | 到期复习集训：开复习会话 → 作答 → 结课 | `/reviews/lesson/start` + 同款作答/结课端点 | **1.31**（第二轮，复用 mainline 的会话组件） |

**设备定位边界（目标）**：设备是学习执行端，不是计划管理端。计划生成/
切换目标/每日配额（`/plan/regenerate|switch|preview|confirm`、
`/onboarding/goal|daily-quota`）为网页 Bearer-only——设备只读消费并在
状态行引导"manage on web"。定级测（`/onboarding/level-test[/skip]`）
learn 可调，留待后续轮（本版不做）。

## 2. 范围

**mainline v1（目标 1.30，单评审轮）**：主线课闭环——今日推荐页
（`GET /plan/next` + `/plan` 进度）→ 开课 → 逐题作答（认读=选项、
拼写=键盘输入、输出=文本造句）→ 结课得分页。`recommend=review` 时
HOME 显示复习指引（"N due — see Review app"），**不开复习会话**。

**review v1（目标 1.31，紧随的第二轮）**：进入显示 `due_total` +
Enter 开复习会话（`POST /reviews/lesson/start`，不占课序/不耗配额）→
逐题作答（与 mainline 共享同一作答组件）→ 结课。毕业词抽查 ≤1 题混入
热身最前由服务端组装，设备无感。

**口语题（两 app 同规）**：按 `skipped=true` 提交（0 分入分母、重开
错题周期），提示行注明 "voice: ASR in round 2"；Round 2 接 MiniMax
ASR（录音→转写→speak_text，注意 `ota_busy()` 互斥）。

**界面英文**（零字库风险）；学习内容的中文走 Font_Hanzi_16（词库即
字库提取源，验收含 CJK 出字抽查）。

## 3. 服务端契约（设备消费面，learn 通道）

鉴权：`X-API-Key`（learn scope）+ `X-Gate-Pin`（既有 pp_request 自动
携带）。全部阻塞调用，仅 worker 内使用。

| 端点 | 用途 | 归属 | 设备消费字段 |
|---|---|---|---|
| `GET /plan/next` | HOME 推荐 | mainline | `recommend(lesson\|review\|done)`、`due_minutes`、`lesson{id, lesson_index, lesson_kind, title, scenario_code, est_minutes, status}` |
| `GET /plan` | HOME 进度行 | mainline | `plan{goal_exam, total_lessons, completed_lessons, cursor_lesson, course_completed, consolidating, due_total}`；`plan:null` = 未建计划（引导网页） |
| `POST /lessons/{id}/start` | 开主线课 | mainline | `session_id, mode, items[]{id, target_layer, prompt{task_type, segment, options[], scene_words}, scaffold_level, submitted}`；409 `Lesson already completed`；409 `plan_superseded` |
| `POST /session-items/{id}/submit` | 逐题（两 app 共用） | 共享 | body `{user_input?, skipped?}`；响应 `{score_write, combined, passed, quality, is_retry, in_errorbook, mastery_layer}` |
| `POST /sessions/{id}/finish` | 结课（两 app 共用） | 共享 | `{score, xp_awarded, correct_items, total_items, skipped_items}`；409 `missing_item_ids[]`（跳回首个未交项）；输出强制律 409 |
| `POST /reviews/lesson/start` | 开复习会话 | **review** | `{session_id, mode, items[], due_total}`（不占课序/不耗配额） |

**HW-MAINLINE-1（安全不变量，两 app 共同的承重线）**：`prompt` 内的
`word`、`correct_option` 是服务端参考答案——**penpal_api 解析层直接
丢弃，永不进入设备侧结构体**；判分完全信任 submit 响应。渲染白名单 =
`task_type / segment / options[] / scene_words / scaffold_level`。
未知 `task_type` → 渲染降级（segment 文本 + skipped 提交路径），不猜。

**实现合同（L2，绑实现 G-合入，不在本稿展开）**：JSON 字段容错矩阵、
`PP_ML_ITEMS_MAX`（目标 20，超限开课即拒）、user_input ≤1000 字截断、
Idempotency-Key 使用（start 自然幂等，不强制携带）。

## 4. 设备侧架构

### 4.1 mainline 应用（1.30）

三页 `LV_OBJ_FLAG_HIDDEN` 切换（单刷、禁滚动）：

- **HOME**：今日推荐卡（`recommend=lesson` 课卡 = kind/index/title/
  分钟；`=review` 复习指引行 "N due — see Review app"；`=done` 通关/
  巩固态）+ 计划进度行（goal、x/y、due_total）。Enter = 开课。
- **SESSION**：当前题卡（作答交互见 4.3，与 review 共享组件）。
- **RESULT**：得分 / XP / 对错统计（finish 响应）；Enter 回 HOME
  （重拉 /plan/next）。

`recommend=review` 的指引行走状态行或专用行，不做跨 app 跳转逻辑
（用户从菜单进 Review——**避免 app 间跳转**，单职责）。

### 4.2 review 应用（1.31）

两页：**REVIEW**（进入即显示 `due_total` + Enter 开会话）→ 复用
SESSION/RESULT（`mode=review` 时 RESULT 不做主线课进度语义）。题源 =
毕业词抽查 ≤1 题热身 + 到期复习题（服务端组装，设备无感）。

### 4.3 异步（wa_* 单飞四件套，契约 §1 补两行）

两 app 各自独立 worker（`ml_task_func` / `rv_task_func`）+ 独立
gen/busy/队列——**不共享在飞状态**，天然互斥（同一账号服务端单会话
语义兜底）。请求快照按值拷贝；结果首字段 gen；队列 4×ptr 一次建；
`xQueueSend 2s` 有界自释放；栈 1024×8、prio 1。会话状态（session_id、
items、游标、submitted 位图）为 UI 线程静态所有权，worker 不读。

### 4.4 作答交互（共享组件，两 app 同款）

- 认读题 = 选项行 1-4（键盘数字/触摸行，Enter 确认，1-based 提交）；
- 拼写/输出题 = 单行 textarea（键盘输入，Enter 提交，Backspace 删字）；
- speak 类 = 自动 skipped 提交 + 提示行；
- 在飞 Enter 缓冲（v1.18），其余键 busy 反馈（§33 A-4）；已提交题
  再按 Enter = 重提交（服务端 `is_retry` 建模）；
- 状态行 `item i/n` + 提交反馈（passed/quality）；finish 409
  `missing_item_ids` → 自动跳回首个未交项；409 `plan_superseded`
  （仅 mainline）→ 提示 + 回 HOME。

### 4.5 数据结构（草案，L2 细化）

`pp_ml_next_t / pp_ml_plan_t / pp_ml_session_t{items[PP_ML_ITEMS_MAX]}
/ pp_ml_submit_t / pp_ml_finish_t / pp_ml_review_t`——纯值成员，字符串
定长截断（`s_copy_disp` UTF-8 边界 + "..."）。

## 5. 安全

- 认证沿用既有链：`X-API-Key`（learn）+ `X-Gate-Pin`（NVS→env.cfg→
  默认），无新增秘密。
- HW-MAINLINE-1 不可协商：评审请专攻"是否存在任何路径把参考答案带上
  屏"。
- 会话/进度**不落本地持久化**（v1）——断电丢会话，重新开课走服务端
  自然幂等；本地缓存（PenPal cache v3 模式）留 Round 2。

## 6. 字库与文案

界面英文（零字库风险）。学习内容的中文（释义/场景词）走
Font_Hanzi_16——词库即字库 3955 字提取源，验收含 CJK 出字抽查；若
服务端新增词越出全集 → 重跑字体管线（TODO 既有预案）。

## 7. 回归清单（真机，G-真机）

**mainline（1.30）**：
1. HOME 三态渲染（lesson/review 指引/done）+ 进度行数字与网页一致；
2. 开课 → 认读题 1-4 作答 → 提交反馈 → 推进；拼写题键盘输入含中文
   释义出字；输出题造句提交；speak 题 skipped 提示；
3. 全项提交后 finish → RESULT 分数/XP 与网页一致；缺项 409 → 跳回；
4. 网页切计划后设备开课/提交 → 409 `plan_superseded` → 回 HOME；
5. 在飞 Enter 缓冲、busy 反馈、退出屏后在飞结果丢弃（gen）；
6. 布局遥测：三页无裁切（首上机数行数）。

**review（1.31）**：due_total 显示、开会话、作答组件同款回归、
finish 统计；与 mainline 先后进入互不串状态（独立 worker 验证）。

## 8. 风险与开放问题

- prompt 显示契约未逐 task_type 冻结 → 以 `remote_api_demo.py
  --channel learn` 实采校准 + 渲染白名单兜底；
- 课项超 `PP_ML_ITEMS_MAX` → 开课显式拒绝（防 finish 永远 409）；
- `recommend=review` 但 due=0 → Review 应用内显示 "no review due"；
- 双 app 同账号并发开会话（mainline 课 + review 会话）服务端语义 →
  实现前用 `remote_api_demo` 验证（预期：单飞由设备侧保证，服务端
  对不同 session 并发无约束）；
- 时钟未同步对"每日"语义的影响：服务端权威（suggested_date/到期都在
  服务端算），设备不本地判日。

## 9. 版本与入库

**1.30 = mainline**（本稿主体，先做）；**1.31 = review**（复用会话
组件，紧随第二轮）。`CHANGELOG` 按日条目；`TODO` 登记 Round 2（ASR/
历史页/本地缓存/定级测）；`async_ipc_contract.md` §1 补 ml/rv 两行。
评审通过（L1 + G-开工）后编码，代码轮另发评审申请（§7.1 全段）。

## 10. 实现合同（L2，绑实现 G-合入）

- `PP_ML_ITEMS_MAX = 20`；超限 → "lesson too large" 拒绝；
- `user_input` 上限 1000 字（服务端约束），设备 textarea 按显示宽度
  再截；选项 ≤4 行渲染（超出拒绝开课——当前契约仅认读 4 选项）；
- worker 栈 1024×8；结果 msg 含 session/items 时为堆拷贝，消费后
  `delete`（规则 1/7）；
- 作答/SESSION/RESULT 组件在 mainline 轮实现为**可复用形态**
  （`ui_mainline.cpp` 内聚 + 对 review 暴露最小接口，或独立
  `ui_ml_session.cpp`——实现期定，评审不阻塞）；
- 首上机布局遥测三页各一行（`[ML] layout: ...`）。
