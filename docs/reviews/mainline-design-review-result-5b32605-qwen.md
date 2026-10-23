# 评审结果：mainline + review 双应用设备端设计稿 v2（qwen）

- **评审日期**：2026-10-23
- **申请文件**：[`mainline-design-review-request-5b32605.md`](mainline-design-review-request-5b32605.md)
- **评审提交**：`5b32605`（评审对象 = `docs/mainline-design.md` v2 全文；v1 `4b16209` 未送审即拆分，本轮为 v2 首轮）
- **评审基线漂移**（§0 原则 5）：`git rev-parse HEAD` = `185aa10`；`git diff 5b32605 HEAD --stat` =
  TODO.md（时点记录，不评）/ 申请文件改名（程序性，不评）/ `scripts/remote_api_demo.py`
  （demo 脚本入设备仓，非设计对象，转 IC-6 实测工具）——归属完毕，无意外落入。
- **评审范围**：设计稿口径（review_guide v1.3 §0–§4、**§2.5 强制**、§6、§9【D】、§10）；
  首轮 = v2 全文（§2.5.3 首轮不适用 delta 默认）。
- **评审结论**：**L1 未达标（1 项协议/架构 P1）→ C（设计稿语义）**。
  required action = 改 §3 HW-MAINLINE-1（平面白名单 → 按 task_type 条件化显示矩阵）+
  同批补 4 处小改（P2-1/P2-2/P2-3/P3-1）；**架构不倒退，非 B**。

---

## Findings

### P1-1：HW-MAINLINE-1 平面渲染白名单与 §2 承诺的题型不可同时满足——认读题按正文必无题干

- **位置**：`docs/mainline-design.md` §3（HW-MAINLINE-1 段）、§2（认读=选项/拼写=键盘）、§4.4
- **证据**（SPEC 自相矛盾 + 后端仓 CODE 逐行核实）：
  - 设计 §3：`word`/`correct_option` 为参考答案，解析层**一律**丢弃、永不进设备结构体；
    渲染白名单 = `task_type/segment/options[]/scene_words/scaffold_level`（**平面**，不分题型）。
  - 认读 prompt（`E:/works/english_learning_system/backend/app/services/studyplan.py:1516-1540`
    `_build_recognition_prompt`）：载荷 = {task_type, word, phonetic, pos, meaning_zh, meaning_en,
    correct_option, options, tts_text, segment}。**题干就是 `word`**（看英文词、四选一中文释义；
    判分 = options 序号/文本对 correct_option，`mastery.py:402-414`）。白名单排除 `word` →
    认读题屏上只有 4 个中文选项、**没有题干**，无法作答。
  - 拼写（recall）prompt（`studyplan.py:1439-1455` `_word_prompt`）：线索 = `meaning_zh/phonetic/pos`
    （`meaning_en` 服务端已掩码为 "____"，:1443-1446），答案 = `word`。线索字段全部不在白名单 →
    **拼写题屏上无线索可渲染**。
  - 即：`word` 对认读是题干、对拼写是答案；`meaning_zh` 对拼写是线索、对认读是答案等价
    （`correct_option = clean_short_meaning_zh(meaning_zh)`）。**不存在任何平面白名单同时满足
    两者**——"是否存在可行的正确实现满足正文" = 否（§2.5.1 条件式成立，非假想级联）。
- **影响**：设计正文确定性自相矛盾（§4.2.1①：确定性自相矛盾不因 DOC 载体降级）；最小修复必须
  改写承重不变量段落 → §2.5.1 第一行，**挡 L1**。不在负面清单（签名/回滚/密钥/TLS/变砖），
  但该不变量是设计稿自declared 的承重线、且是申请评审焦点 1 的正面命中。
- **两轴**：VERIFIED（SPEC + 后端仓 CODE file:line）｜VIOLATES（设计自相矛盾：§2/§4.4 vs §3）
- **最小修复**（改 §3，波及 §2/§4.4 各一句；下表可作 v3 底稿）：HW-MAINLINE-1 改为
  **按 task_type 条件化显示矩阵**——"非渲染集一律解析层丢弃、判分全信 submit 响应"的纪律不变，
  仅把平面集合改为条件集合：

  | task_type | 渲染（进结构体） | 丢弃（答案/答案等价） |
  |---|---|---|
  | recognition | word, phonetic, options[] | correct_option, meaning_zh, meaning_en, tts_text |
  | recall（拼写） | meaning_zh, phonetic, pos, meaning_en(服务端已掩码) | word, tts_text |
  | cloze | sentence_en(含空), sentence_zh, meaning_zh | word, cloze_word, cloze_forms, tts_text |
  | output（topic/sentence_making） | topic_title, scene_words[], scene_patterns[], min_scene_words, min_chars | —（无 word 答案） |
  | speak / 未知 | segment 文本 | 其余全部（skipped 提交路径，设计已有） |

  `scaffold_level` 现网载荷恒为 null（组装处全部 `scaffold_level=None`），可留矩阵备位。
  **焦点 1 后半答复**：`scene_words` 判定 = 合法脚手架、非半答案——服务端完课判据强制引用
  ≥1 场景词且判据必须可见（`studyplan.py:1888-1897`，一百八十三 claude A3 注释在案）。
  **属主决策注**：认读题以文字 `word` 作题干是网页端"音义认读"（TTS 播音）的可视等价；
  若要保持"音义"语义可在 Round 2 走既有 TTS 路径播 `tts_text`（播放不需上屏，不违反矩阵）。

### P2-1：cloze 是高频真实题型，未入 §2 枚举——"未知类型→skipped"兜底将系统性压分

- **位置**：`docs/mainline-design.md` §2（认读/拼写/输出三类 + speak）、§8 首条
- **证据**：`studyplan.py:1730+` `start_lesson_session`——热身段非 review_drill 课
  `idx % 2 == 1` 一律尝试 cloze（:1806-1812）；学习段后半（`i >= half`）优先 cloze
  （:1855-1868）；cloze prompt 结构 :1683-1695（含 `sentence_en` 挖空句）。即常态
  new_study 课约 **25%–50% 课项 task_type="cloze"**。设计的"未知 task_type → skipped
  （0 分入分母）"由此不是罕见兜底而是高频路径 → 课分系统性偏低、SRS 收到假阴性。
- **影响**：D/1/有声（RESULT 分数可见）→ P2。不挡 L1（降级路径安全、课能结），但必须与
  P1-1 矩阵修订**同批**闭合（矩阵加 cloze 行即覆盖；交互 = textarea，与拼写组件同构复用）。
- **两轴**：VERIFIED（后端 CODE）｜VIOLATES（§2 枚举 vs 服务端现实）
- **最小修复**：§2 枚举补 cloze（作答 = textarea 输入词形，服务端按 `cloze_forms` 判）；
  或属主显式裁决 v1 接受 cloze 计 0 并登记决策项（§10 语义）。建议前者——增量仅一行渲染。
- **回升条件**：服务端改装配策略使 cloze 降为零星时，可降观察项。

### P2-2：输出强制律 409 携带 `missing_item_ids: []`（空表）——§4.4"自动跳回首个未交项"对空表未定义

- **位置**：`docs/mainline-design.md` §4.4、§3 finish 行
- **证据**：`backend/app/api/v1/endpoints/sessions.py:157-159` `OutputRequirementMissing` →
  409 `detail={"message": str(exc), "missing_item_ids": []}`。设计只写"finish 409
  missing_item_ids → 自动跳回首个未交项"；空表取首项 = 未定义（越界或无处可跳 → 用户在
  RESULT 无提示可懂地打转）。
- **影响**：B/2/有声（有 409 message）→ P2（状态机骨架已在，缺一条空表守卫，不改承重规范）。
- **两轴**：VERIFIED｜NO_CONTRACT（设计未写该分支）
- **最小修复**（= IC-1）：§4.4 补一句——`missing_item_ids` 为空 → 显示 message（输出须引用
  ≥1 场景词）+ 跳回 items 中 `segment="output"` 项重做，不取表首。

### P2-3：§3 契约表仅 start 标注 409 plan_superseded；服务端 submit/finish 同样会抛

- **位置**：`docs/mainline-design.md` §3 表 submit/finish 行
- **证据**：`sessions.py:105-113`（submit → `record_submission` 抛 `PlanSuperseded` → 409
  `reason=plan_superseded`）、`sessions.py:139-146`（finish 同款）。§4.4 的"409
  plan_superseded（仅 mainline）→ 提示 + 回 HOME"写在作答段、可解读为覆盖 submit，但 §3 表
  submit 行无 409 列——表格与现实不符，实现者易漏 mid-session 切计划路径（评审焦点 2 正面）。
- **影响**：E→B/2/有声，取 P2（表格遗漏即实现遗漏的高危文档缺陷）。
- **两轴**：VERIFIED｜VIOLATES（§3 表 vs 后端现实）
- **最小修复**（= IC-2）：§3 表 submit/finish 行补"409 plan_superseded → 会话作废 + 回 HOME"。
- **焦点 2 SIM 结论**：双 409 组合下线性 UI **无卡死洞**——跳回缺项后"已提交题再按 Enter =
  重提交（is_retry）"提供出路；服务端 `missing_item_ids` 为权威，与设备 submitted 位图漂移
  （submit 响应丢失但服务端已记录）自洽：missing 表不含该项 → 不跳回 → finish 可通过。

### P3-1：`/plan/next` 第四态 `recommend=null`（未建计划）未入 §4.1 HOME 三态枚举

- **证据**：`studyplan.py:1394-1396` plan 为 None → `recommend: None`；`schemas/study.py:129-135`
  `PlanNextOut.recommend: str | None`。设计 §4.1 HOME 只枚举 lesson/review/done；§3 只在 /plan 行
  写了 `plan:null` 引导网页。新用户首次进 mainline 时推荐卡渲染未定义。
- **最小修复**（= IC-3）：HOME 枚举补第四态 "no plan → create a plan on web" 指引行（与 §3
  plan:null 引导同构，一行文案）。

### P3-2：`SubmitResult.passed/quality` 可空（输出项为 None）——§4.4 状态行反馈需空值容忍

- **证据**：`schemas/learning.py:41-58`——`passed: bool|None`、`quality: int|None`，注释明确
  "输出项（造句/话题，word_id=None）不驱动调度，字段为 None"。设计 §3 submit 行未标 nullable。
- **最小修复**（= IC-4）：§3 表标 nullable；状态行空值时以 `combined`/`score_write` 兜底渲染。

### P3-3：start 实际载荷大于 §3 枚举——`items[].word_id`、LessonStartOut 顶层字段、ReviewLessonStartOut 的 `graduated_sampled_word_ids/included_count`

- **证据**：`plan.py:341-351` `_items_payload`（items 含 `word_id` + `submitted`）、:419-424
  LessonStartOut（顶层 lesson_id/lesson_index/lesson_kind/title）、:490-497 ReviewLessonStartOut
  （含 included_count/due_total/graduated_sampled_word_ids）。设计 items[] 枚举缺 `word_id`；
  review start 枚举缺 included_count/graduated_sampled_word_ids（"设备无感"可接受，但容忍矩阵
  须写明丢弃）。
- **最小修复**（= IC-5）：L2 JSON 容错矩阵写明"枚举外字段一律解析层丢弃"。已核实
  **无 prompt 外答案泄漏面**：`SessionItemOut`/`_items_payload` 不含 `target_word`
  （demo step22 的 `it.get("target_word")` 是防御性写法，实际恒 None）。

---

## 焦点问题逐条结论（对应申请 §3）

1. **HW-MAINLINE-1 完备性** → **P1 成立**（P1-1）。不变量方向正确且必要——demo step22 证实
   `correct_option`/`word` 确在下发载荷中（`remote_api_demo.py:892-899` 直接以其自动作答）；
   但平面白名单与 §2 题型承诺自相矛盾。`scene_words` = 合法脚手架非半答案（判据必须可见）。
   附加发现：认读题的 `meaning_zh` 是答案等价，矩阵须按题型条件丢弃。
2. **409 组合** → 线性推进 UI 无卡死/重复提交洞（SIM 见 P2-3）；两个文档级洞：空表跳回
   未定义（P2-2）、submit/finish 的 plan_superseded 缺表行（P2-3）。
3. **范围边界** → 无死路：未建计划 → 网页引导机制已在（§3 plan:null），补 P3-1 第四态即闭合；
   定级测不做 = §1"定位边界"显式记录的决策（已知简化语义，非缺陷），设备上呈现为 HOME
   第四态指引而非静默失败。
4. **双应用拆分** → 架构健康：独立 worker/队列/gen/busy 与 `async_ipc_contract.md` 规则 7
   wa 已批准变体（2s 有界 + 自释放，2026-09-18 登记）逐项一致，栈 1024×8/prio1 合规则 6；
   共享组件形态留 L2 = 实现选型，按 §2.5.1 NO_CONTRACT 不升级。串扰风险收 IC-7（组件状态
   per-app 实例化或 entry 重置；§7 回归已列"先后进入互不串状态"）。跨 app 并发：后端核实
   `(user,lesson)` 互斥仅覆盖主线开课（`plan.py:388` `lesson_start_guard`），review 与 mainline
   会话并存无显式服务端约束——设计 §8 已登记实现前 demo 实测，维持 ⏸（IC-7）。
5. **"每日"语义** → **PASS**：`idle_days/due_minutes/backlog_trigger/suggested_date` 全在
   服务端算（`studyplan.py:1392-1411`），设备零本地判日，时钟未同步无残留坑。

## 开发者自评三项的反例核验（对应申请 §4）

1. **task_type 兜底是否足够** → **不足**，反例已构造：cloze 是高频真实类型而非"未知类型"
   （P2-1）；认读渲染矛盾（P1-1）。skipped 安全降级机制本身保留正确，但枚举须先对齐服务端现实。
2. **PP_ML_ITEMS_MAX=20** → 静态不可判：项数 = warmup（budget ≤ cap/2 分钟，随 age_band）+
   快照词数（随计划配额）+ output refs（`studyplan.py:1795-1800`）。⏸ 转 IC-6 实测：测试账号跑
   `remote_api_demo --channel learn` step22 记录 `len(items)`，或直查后端库各 lesson_kind 项数
   分布；常态 >20 → 提高 MAX（items 为定长 POD 数组，RAM 增幅可控）或分页。设计 fail-safe
   （显式拒绝开课，防 finish 永远 409）方向正确。
3. **recommend=review 且 due=0** → SIM：**可达**——HOME 读 `due_minutes` 与 Review 应用进入
   重查 `due_total` 是两次采样，间隔内到期被消费/时间推移即归零；设计 §8 处置（"no review
   due"）正确且必要。PASS。

## 已通过项

- **端点/字段对齐**（均对后端仓 file:line 直接核读，DOC VERIFIED）：`recommend` 枚举
  lesson|review|done（`studyplan.py:1405-1409`）；`lesson{}` 七字段逐一吻合（:1417-1425）；
  /plan 的 `consolidating/due_total`（:712-732）；submit 响应 ⊆ `SubmitResult`
  （`learning.py:41-58`）；finish 响应 = `FinishResponse`（:62-74）；reviews/lesson/start 带
  `due_total`（`plan.py:490-497`）；"409 Lesson already completed"（`plan.py:380` +
  `LessonStartBlocked`→409 :455-462）；自然幂等 = 未结业会话原样返回 + `Idempotent-Replayed`
  头（`plan.py:405-424`）；输出强制律 409 可证伪（`sessions.py:127-135` docstring）。
- **learn 通道开放声明属实**：`FSD_LM_DESIGN.md:238`（(二百七十) 开放、(二百七十三) 并发互斥）
  + 各端点 `get_current_user_learn` + `_guard_api_key_channel` 只放行主线课/纯复习会话
  （`sessions.py:28-43`）——与设计 §1/§3 一致。
- **既有设施复用声明属实**：`pp_request` 存在（`penpal_api.cpp:175`）、`X-Gate-Pin` 自动携带
  （`penpal_api.cpp:222`）；无新增秘密，合 CLAUDE.md 第 2 条秘密链。
- **异步架构与契约一致**：单飞四件套/队列 4×ptr 一次建/2s 有界自释放/栈 1024×8 prio1/
  快照按值拷贝/结果首字段 gen/会话状态 UI 静态所有权——逐项对 `async_ipc_contract.md` §2
  规则 2/5/6/7 已批准 wa 变体核对通过；承诺 §1 补 ml/rv 两行（IC-8）避免 NO_CONTRACT，正确。
- **UI/EPD 纪律**：三页 HIDDEN 切换（无滚动无动画）合 CLAUDE.md EPD 工作注；界面英文 +
  Font_Hanzi_16（词库=字库提取源 + CJK 出字抽查）与 NewDict 先例同构；无本地持久化为显式
  v1 决策（§5），断电重开课走服务端自然幂等——幂等语义已核实真实存在。
- **回归清单质量**（§7）：覆盖 409 双路径、在飞 Enter 缓冲、gen 丢弃、双 app 串扰、CJK 出字、
  布局遥测——对 §6 反例库【异步/生命周期】各行有对应项。

## 验证说明（评审方环境）

- 无真机、无联调服务器/key：未复跑 pio 构建、未实跑 remote_api_demo（如实标 UNKNOWN，
  非证伪）→ demo 实测转 IC-6。设计稿口径无需 CODE/HW 证据（§1.1），结论全部基于
  DOC/SPEC + 后端仓（`E:/works/english_learning_system`）源码直接核读，file:line 可复核。
- prompt 载荷字段集按组装函数逐型核读（`_word_prompt` / `_build_recognition_prompt` /
  `_build_cloze_prompt` / output 两类）——P1-1 矩阵即由此产出，非推测。

## 对称三格（§7.2 强制）

- **① 全区间**：v2 首轮 = 全文评审（§1–§10 全读，非仅申请靶向清单）；基线漂移已记头部并完成
  三文件归属（TODO.md/申请改名/demo 脚本副本，均不评+理由）。
- **② 独立复跑**：无 pio/设备，构建与 demo 无法复现（标 UNKNOWN）；契约结论以后端源码
  file:line 为准逐条复核，可照抄复查。
- **③ 最没把握反例**：三项逐一构造/推演（见"自评三项反例核验"），其中 #1 命中 P1-1/P2-1，
  #2 转 ⏸ 实测项，#3 PASS。

## 审批意见

- [ ] A. 接受，据此开工
- [ ] B. 退回重构
- [x] **C. 部分接受（设计稿语义，§2.5.2）**——L1 未达标，唯一阻断项 P1-1。
  **Required action（改哪一节规范）**：
  1. **§3 HW-MAINLINE-1**：平面白名单 → 按 task_type 条件化显示矩阵（P1-1 表为底稿）；
  2. **§2**：题型枚举补 cloze（或属主显式裁决接受 skipped 计 0，登记决策项）；
  3. **§3 表**：submit/finish 行补 409 plan_superseded；
  4. **§4.4**：finish 409 空 `missing_item_ids` 守卫一句；
  5. **§4.1**：HOME 补 recommend=null 第四态。
  以上为 v3 全部改动面；其余章节（架构/异步/安全/字库/回归）本轮已闭合，v3 复审按
  §2.5.3 delta 口径只核上述段落。
- **轮次说明**（§2.5.4）：本轮出现协议/架构 P1 → 属多方轮，建议属主将 v3 delta 另送
  ≥1 家其他 reviewer 复核矩阵段。P2/P3 与 IC 表随实现申请绑 L2/G-合入，**不倒逼扩大改版面**。

## 实现合同跟踪表（§2.5.2，绑实现 commit 的 L2/G-合入）

| 合同 ID | 正确性目标 | 绑定实现 | L2 验证证据 | 状态 |
|---|---|---|---|---|
| IC-1 | finish 409 空 missing_item_ids → 提示判据 + 跳回 output 段项，不取表首 | ui_mainline SESSION 状态机（v1.30） | 走查 + demo 输出强制律 409 实例 | 待 |
| IC-2 | submit/finish 的 409 plan_superseded → 会话作废 + 回 HOME（两 app 各自处理） | 同上 | 网页切计划 → 设备 mid-submit 实测（§7 回归 4 扩展至 submit 路径） | 待 |
| IC-3 | HOME 第四态 recommend=null → 网页建计划指引行 | ui_mainline HOME | demo（未建计划账号）或后端直查 | 待 |
| IC-4 | passed/quality nullable 容忍；状态行空值以 combined/score_write 兜底 | 作答组件 | 输出项提交实测 | 待 |
| IC-5 | JSON 容错矩阵：枚举外字段（word_id/included_count/graduated_sampled_word_ids/idle_days/spread_windows…）一律解析层丢弃 | penpal_api ml/rv 解析 | 走查 + 实采载荷对照（demo step22 原始 JSON） | 待 |
| IC-6 | PP_ML_ITEMS_MAX=20 实证：各 lesson_kind 项数分布；常态超限 → 提高 MAX 或分页 | 开工前 / v1.30 | 分布数据归档本表（demo step22 或 DB 查询） | ⏸ |
| IC-7 | 共享作答组件状态 per-app 实例化或 entry 重置；mainline+review 跨 app 并发会话语义 demo 实证（设计 §8 登记项） | v1.31 review 轮 | §7"先后进入互不串状态"回归 + 并发实测 | 待 |
| IC-8 | async_ipc_contract §1 补 ml/rv 两行；布局遥测三行 `[ML] layout:` | v1.30 实现 commit | 契约 diff + 串口输出 | 待 |

## §9【D】自审清单勾选

- 21 字段声明 vs 读取位置一致：**查出 3 处不一致**（P1-1 白名单 vs prompt 现实、P2-1 题型
  枚举、P2-3 409 表行）+ P3-3 载荷超集——均 file:line 在案。
- 22 代码块可解析/字段与正文一致：设计无 JSON 示例块；§3 表字段名与后端 schema 逐一对照通过
  （含 nullable 差异 → P3-2）。
- 23 ✅/⏸ 逐条有证据：申请无虚标 ✅（设计稿口径，全部 ⏸ 待实现）；自评 #2 的 ⏸ 转 IC-6。
- 24 无 P0/P1 时须给 L1+A：**本轮存在 P1-1，故 C 合规**；P2/P3 全部进实现合同表，未以
  完备性为由扩大阻断面。
- 25 复审 delta 口径：首轮全文，合规；v3 复审范围已在审批意见中限定为 5 处改动面。
- 26 NO_CONTRACT 未升级：共享组件形态/队列 drain 点等留 L2 项均按合同处理，未挡 L1；
  P1-1 最小修复触及的是设计自declared 承重不变量正文，非负面清单误归。
- 27 未把伪代码当生产 C++ 做 ABI 分析；P1-1 为正文确定性自相矛盾（"是否存在可行正确实现"
  = 否），非假想级联。
