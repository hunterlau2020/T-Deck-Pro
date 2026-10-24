# 评审结果：mainline + review 双应用设备端设计稿 v3 delta 复审（qwen）

- **评审日期**：2026-10-24
- **申请文件**：[`mainline-design-review-request-24741f2.md`](mainline-design-review-request-24741f2.md)
- **评审提交**：`24741f2`（评审对象 = `docs/mainline-design.md` v3 的 v2→v3 delta）
- **评审基线**：`git rev-parse HEAD` = `068733f`；`git diff 24741f2 HEAD` = 仅本申请文件
  （程序性，不评）——归属完毕，无意外落入。
- **评审范围**（§2.5.3 delta 口径，申请头部已划定）：P1-1 所涉 §3/§4/§5 字段矩阵与题型
  映射 + qwen P2/P3 同批项（§2/§3.3/§4.1/§4.4/§7/§8/§10 改动段）；未改段（§1/§6/§9）
  做回归扫视（扩面条件①）——**未发现回归**，v2 已闭合声明均保持。
- **前置**：本人 v2 轮结论 C（`mainline-design-review-result-5b32605-qwen.md`，P1-1 +
  P2-1/P2-2/P2-3 + P3-1/P3-2/P3-3）；五方 2-3 分歧按 C 方 required action 修正。
- **评审结论**：**L1 DOC-ALIGNED 达标 → A（设计稿语义，§2.5.2）**。
  delta 内无新协议/架构 P0/P1；两条 Nit 归口既有实现合同行，不绑门禁、不倒逼 v4。
  本轮属多方轮（上轮有 P1）——本 A 为单方输入，属主汇总裁决。

---

## 一、delta 闭合逐项核验（v2 发现 → v3 落点 → 独立验证）

后端参照系：`E:/works/english_learning_system` 工作树**干净**（`git status` 仅 untracked
`.qwen/`/`.zcode/`，HEAD = `c143d68`），全部 file:line 于 2026-10-24 在当前树上复核。

| v2 发现 | v3 落点 | 独立验证（本轮重核） | 状态 |
|---|---|---|---|
| **P1-1** 平面白名单自相矛盾 | §3.2 条件化显示矩阵（五题型行，允许/丢弃集逐型） | 矩阵逐行对 builder 载荷核：**recognition**（`studyplan.py:1513-1540`，渲染 word/phonetic/options/pos、丢弃 correct_option/meaning_zh/meaning_en/tts_text）✓；**recall**（`:1438-1455`，渲染 meaning_zh/phonetic/pos/meaning_en，recall 恒掩码 `:1443-1446`）✓；**cloze**（`:1683-1695`，渲染 sentence_en/sentence_zh/meaning_zh、丢弃 word/cloze_word/cloze_forms/tts_text）✓；**output**（`:1888-1897/:1914-1920`，渲染集含 scene_words/scene_patterns/min_scene_words/min_chars，无词项答案）✓。v2 轮"认读无题干/拼写无线索"矛盾消除：`word` 按题型条件进出 | **闭合** |
| **判型双键**（codex/claude 新 P1） | §3.1 + §10 固定判型顺序（先 `segment=="output"`+`task`，再 `task_type`） | output prompt 确无 `task_type` 键（`:1888/:1914` 仅 segment/task/…）；服务端自身缺省 `.get("task_type","recall")`（`mastery.py:380`）——设备若模仿该缺省会把输出题误判为拼写，§10 固定顺序恰好规避。✓ | **闭合** |
| **P2-1** cloze 未枚举→系统性压分 | §2/§3.2/§4.4 完形 = textarea 一等题型 | 装配频率复核：热身 `idx%2==1` 尝试 cloze、学习段后半优先 cloze（`:1806-1812/:1855-1868`），"25-50%"引用与源码一致；作答交互与拼写同构（textarea），组件复用成立。✓ | **闭合** |
| **P2-2** finish 409 空表未定义 | §3.3 finish 行分治：空表→显示 message + 跳回 `segment="output"` 项 | `sessions.py:157-159`（OutputRequirementMissing → `missing_item_ids:[]`）✓；判据文本可见性由 §3.2 output 渲染集（min_scene_words/min_chars/scene_words）保障。边缘见 Nit-1 | **闭合**（Nit-1 归 IC-1） |
| **P2-3** submit/finish 缺 409 plan_superseded 列 | §3.3 两行补 409 → 会话作废 + 回 HOME | `sessions.py:105-113`（submit）/`:139-146`（finish）✓；§7 回归 4 扩为"开课/提交/结课三处"。✓ | **闭合** |
| **P3-1** recommend=null 第四态 | §3.3 + §4.1 HOME 四态 "create a plan on web" | `studyplan.py:1394-1396`（plan None → recommend None）✓ | **闭合** |
| **P3-2** passed/quality nullable | §3.3 submit 行标注 + combined/score_write 兜底 | `schemas/learning.py:41-58`（输出项 None，注释在案）✓ | **闭合** |
| **P3-3** 载荷超枚举 | §3.2 枚举外一律丢弃（点名 word_id/start 顶层/review-start 附加字段） | `plan.py:341-351/:419-424/:490-497` ✓；无 prompt 外答案泄漏面结论维持 | **闭合** |
| scene_words 半答案疑虑 | §3.2 判定 = 合法脚手架入渲染集 | 完课判据要求引用且必须可见：`supply.py:262-276` `output_fallback_ok`（归一化含 ≥1 场景词形 + ≥15 字符）+ `mastery.py:927-931` 409 文案。判定维持 ✓ | **闭合** |

## 二、自评三项核验（申请 §4）

1. **矩阵是否滞后于服务端工作树** → **VERIFIED，未滞后**：后端仓干净（见上），
   本轮对 `_word_prompt:1438` / `_build_recognition_prompt:1513` / `_build_cloze_prompt:1644`
   / output 两类 `:1888/:1914` 全部重 grep 定位 + 字段集重核，与 §3.2 矩阵一致。
   漂移防线（§8：实采校准 + 枚举外丢弃 + 未知降级）结构正确——矩阵滞后时失败模式是
   "多丢弃/降级 skipped"，不是"泄漏答案"，防线方向安全。
2. **cloze 词形（三单等）无本地提示是否影响完课率** → **不影响判分，VERIFIED**：
   服务端判分对目标词原型 + `cloze_forms` 全部形态取最优（`mastery.py:420-433`，
   `score_spelling(form, input)` 逐形态比对）——设备送裸文本即可，词形容差全在服务端。
   本地提示属体验增强，留 Round 2 无碍 v1。
3. **skipped 提交输出项是否豁免输出强制律** → **不豁免，VERIFIED（代码级答案，§8 该项可
   销记）**：finish 判"每项**最新**提交"经 `output_fallback_ok(sub.user_input or "", …)`
   （`mastery.py:915-927`）；skipped 提交 `user_input=None` → `""` → `output_fallback_ok`
   首行 `if not text: return False`（`supply.py:266-267`）→ 仍 409。设备侧推论：输出题
   **绝不可入自动 skipped 路径**——v3 §4.4 已正确将输出列为一等 textarea 题型、auto-skip
   仅限 speak/未知，设计与服务端语义自洽。死锁排查：409 空表→跳回输出项循环**有出口**
   （用户可退 HOME；未结会话服务端保留，重开课走自然幂等返回同一会话 + `submitted`
   位图可续答，`plan.py:405-424` + `_items_payload` submitted 字段）。

## 三、新发现（Nit ≤3，§7.3；不绑门禁，归口既有 IC 行）

### Nit-1：409 空表跳回的前提"存在 output 项"有一个服务端异常面未兜底

- **位置**：v3 §3.3 finish 行 / §4.4
- **证据**：`mastery.py:905-910`——output_capstone 课若**组装不出任何 output 项**
  （数据异常面），同样抛 `OutputRequirementMissing`（空表 409），但此时设备按 §3.3
  "跳回 `segment="output"` 项"将找不到目标项。
- **影响**：可达性 3（服务端组装 bug 面才可达），E 类 → Nit。
- **最小修复**：IC-1 实现时加一条兜底分支——items 中无 output 项 → 显示 message +
  回 HOME（登记串口日志），不无限跳。

### Nit-2：§3.2 cloze 行未显式列 `phonetic/pos/meaning_en`（掩码）——默认丢弃规则已覆盖，但常量表宜全键穷举

- **证据**：cloze 载荷实有 12 键（`studyplan.py:1683-1695`），矩阵 cloze 行两列合计
  列出 8 键；`phonetic/pos/meaning_en`（已掩码）落在"渲染集外一律丢弃"默认规则，语义
  无歧义、不泄漏。
- **最小修复**：§10"矩阵落地为常量表"时按 builder 载荷**全键穷举**每型三态
  （渲染/丢弃/默认丢弃），新键出现即触发表更新——归 IC-5，无需改设计正文。

## 四、已通过项（delta 外的本轮确认）

- §4.5 结构体"prompt 按矩阵裁剪后仅存渲染集字段"——不变量从渲染层前移到解析层，
  较 v2 更严，方向正确；§5 评审攻击面同步改写为"丢弃集字段带上屏/日志/结构体"。
- §7 回归清单与 v3 修订一一对应（四态/完形渲染/空表 409/三处 superseded/遥测）。
- §8 已闭合项划掉留痕（合规修订卫生）；TTS Round 2 注记（tts_text 播放不上屏，
  不违反矩阵）登记为属主决策注，处理正确。
- §10 `user_input ≤1000` 与服务端约束吻合（`learning.py:30` `max_length=1000`）。
- 未改段回归扫视：§1/§6/§9 与 v2 一致，无措辞漂移破坏已闭合声明（扩面条件①不触发）。

## 五、验证说明（评审方环境）

- 无真机、无联调服务器/key：未实跑 `remote_api_demo`（IC-6 维持 ⏸）；设计稿口径无需
  CODE/HW 证据（§1.1）。结论全部基于 DOC/SPEC + 后端仓源码直接核读（工作树干净，
  file:line 于 2026-10-24 复核，可照抄复查）。
- v2 轮已核且本轮未触及的声明（端点/字段对齐、learn 通道、pp_request/X-Gate-Pin、
  异步契约一致性）沿用 v2 结果文件证据，未重复展开——沿用来源：
  `mainline-design-review-result-5b32605-qwen.md`"已通过项"。

## 六、对称三格（§7.2 强制）

- **① 全区间/delta**：按申请划定范围核 v2→v3 全部改动段（§2/§3/§4.1/§4.4/§5/§7/§8/§10
  + 头部），并对未改段做回归扫视；基线漂移已记头部（24741f2..HEAD 仅申请文件）。
- **② 独立复跑**：无 pio/设备，构建与 demo 无法复现（如实标 UNKNOWN，非证伪）；
  矩阵/双键/409/nullable 各声明以后端源码 file:line 逐条独立复核，未采信申请书对照表
  的 ✅ 自标（§9-23：逐条重验后方可记闭合）。
- **③ 最没把握反例**：三项逐一构造——#1 重 grep 全部 builder 定位（闭合）；#2 构造
  三单词形输入反例，服务端 cloze_forms 容错命中（`mastery.py:424-431`，闭合）；
  #3 构造 skipped 输出提交序列，`output_fallback_ok("")=False` → 409 不豁免（闭合，
  §8 该验证项可销记为代码级已证）。

## 七、审批意见

- [x] **L1 DOC-ALIGNED 达标（G-开工）**
- [x] **A. 接受，据此开工（v1.30 mainline 编码）**——设计稿语义（§2.5.2）：无协议/
  架构 P0/P1；Nit-1/Nit-2 归口 IC-1/IC-5，实现合同绑 L2/G-合入，**不倒逼 v4**。
- [ ] B. 退回重构
- [ ] C. 部分接受

**轮次说明**（§2.5.4）：上轮存在 P1 → 本轮属多方轮，本结果为 qwen 单方 A；属主按
"证据 > 票数"汇总各家结果后签字。按 §2.5.4 收口纪律：本轮无新协议/架构 P0/P1，
属主裁决 A 后设计冻结，下一评审对象 = 实现 commit。

## 八、实现合同跟踪表（§2.5.2，v2 轮 IC-1..IC-8 状态更新，绑实现 L2/G-合入）

| 合同 ID | 正确性目标 | 设计侧状态（v3） | 实现侧验证（L2 证据） | 状态 |
|---|---|---|---|---|
| IC-1 | finish 409 空表 → 判据提示 + 跳回 output 项；**Nit-1 兜底：无 output 项 → 提示 + 回 HOME** | §3.3 已写（含空/非空分治） | 走查 + demo 输出强制律 409 实例 + 兜底分支走查 | 待 |
| IC-2 | submit/finish 409 plan_superseded → 会话作废 + 回 HOME（两 app） | §3.3 已写 | 网页切计划 → 设备 mid-submit 实测（§7 回归 4） | 待 |
| IC-3 | HOME 第四态 recommend=null → 网页建计划指引 | §3.3/§4.1 已写 | demo（未建计划账号）或后端直查 | 待 |
| IC-4 | passed/quality nullable 容忍；状态行 combined/score_write 兜底 | §3.3 已写 | 输出项提交实测 | 待 |
| IC-5 | JSON 容错矩阵：枚举外字段一律丢弃；**Nit-2：常量表按 builder 载荷全键穷举三态** | §3.2/§10 已写 | 走查 + demo step22 原始 JSON 对照 | 待 |
| IC-6 | PP_ML_ITEMS_MAX=20 实证项数分布；常态超限 → 提高 MAX 或分页 | §10 维持 MAX=20 | 分布数据归档本表（demo step22 或 DB 查询） | **⏸** |
| IC-7 | 共享组件状态 per-app 实例化或 entry 重置；跨 app 并发会话语义 demo 实证 | §4.3/§10 维持 | §7"先后进入互不串状态"回归 + 并发实测 | 待（v1.31） |
| IC-8 | async_ipc_contract §1 补 ml/rv 两行；布局遥测三行 | §9/§10 已承诺 | 契约 diff + 串口输出 | 待 |

> 入库轮义务（§7.2）：本表随结果入库即视为移交实现申请；IC-6 的 ⏸ 建议属主同步登记
> `TODO.md` 阻塞项清单（开工前实测，不挡编码启动）。

## 九、§9【D】自审清单勾选

- 21 字段声明 vs 读取位置：v2 轮 3 处不一致全部闭合（本轮逐条重验，见 §一表）；
  无新增不一致。
- 22 代码块/字段名与正文一致：§3.2 矩阵、§3.3 表字段名与后端 schema/builder 逐一对照
  通过；nullable 标注齐全。
- 23 ✅/⏸ 逐条有证据：申请对照表 9 行 ✅ 均未直接采信，逐条独立重验后方记闭合；
  自评 #3 的"未核实"项本轮以代码证据销记。
- 24 无 P0/P1 → L1+A：本轮 delta 无协议/架构 P0/P1，给 A 合规；Nit 归口 IC，未以
  完备性扩大阻断面。
- 25 delta 口径：按申请划定范围复审，未重开全稿；未改段仅回归扫视，扩面条件未触发。
- 26 NO_CONTRACT 未升级：组件形态/常量表落地方式等留 L2 项均按合同处理。
- 27 未做假想级联升格：Nit-1 为服务端异常面兜底（可达性 3，Nit 级），未升 P。
