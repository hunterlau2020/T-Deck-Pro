# 评审结果：LevelTest 功能族（v1.13 建成 + v1.16 字库 + v1.17 阶梯重写 + v1.18 吞键修复，ds4）

- **评审日期**：2026-09-18
- **申请文件**：`docs/reviews/session-batch-review-request-leveltest-family.md`
- **评审依据**：`docs/review_guide.md` v1.3（代码口径 §2.3 A/B/C）
- **评审提交**（4 个，**非连续**——中间夹的 `0bc0fcc`/`ae39763`/`7bab081` 属修复轮 delta 复审，
  结论见 `session-batch-review-result-bdb75bf..7bab081-ds4.md`）：

  | commit | 内容 | 评审面 |
  |---|---|---|
  | `bdb75bf` | v1.13：LevelTest 初版（整卷）+ 主菜单三屏重排（21 入口，`menu_btn` 加 `page` 字段） | 代码（9 文件） |
  | `e411e73` | v1.16：Font_Hanzi_16（SimSun 16px bpp4，4057 字形）+ 按内容切字体 | 代码（6 文件，含 71037 行生成物） |
  | `72b4cce` | v1.17：单题自适应阶梯重写 + 终止事件提交 | 代码（6 文件） |
  | `bfc27d2` | v1.18：在飞窗口的 Enter 缓冲 | 代码（4 文件） |

- **评审基线**：`git rev-parse HEAD` = `bb28ce2`（越过后仅 docs commit，`examples/pda2/ui_leveltest.cpp`
  与本族 4 个 SHA 逐字节一致：`git diff bfc27d2 HEAD -- examples/pda2/ui_leveltest.cpp` 为空）。
- **结论**：**C 部分接受**——v1.17 的核心（阶梯转录 / 终止提交）经我独立**穷举式**复核成立，
  v1.16 字库覆盖独立复核成立；但 v1.18 新引入的 Enter 缓冲有 1 处状态机缺口（L1，P2/应修）。

## Findings

### P2（应修｜绑 G-合入）：`s_enter_pending` 只"发"不"销"——缓冲的 Enter 会活到测试结束，在 RESULT 渲染的**同一 tick** 被补射，结果页被当场覆盖并自动重开一场

- **位置**：`examples/pda2/ui_leveltest.cpp:549-555`（消费点）、`:566-569`（置位点）、
  `:398-416`（`lt_render_question()` → `lt_page_show(LT_PAGE_QUIZ)`）、`:418-437`/`:491`
  （`lt_render_result()` → `lt_page_show(LT_PAGE_RESULT)`）、`:121-129`（`lt_session_reset()`，清除点之一）
- **反例序列**（正常操作，无需误操作；EPD 慢刷新使每一步都在用户预期内）：

  1. 进场：`entry_lt()`（`:584-592`）启动 HISTORY 请求，页面 HOME，状态行 "Enter: start the test"；
  2. 用户按 Enter 一次（这正是 v1.18 的**目标场景**：请求在飞，键被缓冲）→ `:566-569` 置位；
  3. HISTORY 返回，`lt_consume()`（`:493-499`）渲染 HOME，**同一 tick** 的消费点（`:549`）成立
     → `lt_session_reset()` 清位 + 启动首个 NEXT（`lt_status("Fetching first question...")`）；
  4. **页面仍是 HOME、屏上没有任何题目**（EPD 未刷新），用户再按一次 Enter → 再次置位；
  5. 首题返回：`lt_consume()` → `lt_render_question()`（`:459`）→ `lt_page_show(LT_PAGE_QUIZ)`（`:415`）；
     紧接着**同一 tick** 的消费点判据 `s_lt_page != LT_PAGE_QUIZ`（`:549`）为**假** ⇒ 既不消费也不清除；
  6. 整场答题期间该标志恒真（`:555` 之后 return，`:572` 的分支只对 Enter 且无在飞时生效；
     `lt_session_reset()` 不再被调用）；
  7. 终止 → 提交返回 → `lt_render_result()`（`:491`）→ `lt_page_show(LT_PAGE_RESULT)`（`:436`）；
     **同一 tick** 的消费点判据此时成立（页面 RESULT、`s_lt_task` 已空）⇒
     `lt_session_reset(); lt_status("Fetching first question..."); lt_start(LT_REQ_NEXT);`。

- **影响**：结果页在"起步多按一次 Enter"这一**自然序列**下不可读——RESULT 的渲染紧接同一次 poll
  内被 `Fetching first question...` + 新题覆盖（EPD 全刷 1-2 s，用户看不到结果），
  整场 2~16 题作废且**无任何提示**。`C/2/有声 → P2`（症状可见：刚答完又出现 Q1）。
- **两轴**：`PARTIALLY_VERIFIED`（CODE + 时序推导；真机复现未做，无设备）｜
  `VIOLATES`（`async_ipc_contract.md` §2.8 页面生命周期/键缓冲语义无对应条款，但违反
  §5.3 页面状态机纪律：同一标志跨页面存活且无清除点）
- **最小修复**：把置位条件从"非 QUIZ"收紧为"**HOME 页**"（`:567`），并在 `lt_render_question()`
  里 `s_enter_pending = false;` 兜底（进入 QUIZ 即作废缓冲）。
  **须补回归**：SIM —— 队列/时序推演或探针；HW —— 起步连按两次 Enter，走完一场后
  **结果页必须停留**，串口在 `[LT] terminated:` 之后不应立刻出现新的 `[LT] a1 ...`。

## Nit（≤3）

- **Nit-1（契约措辞与实现不一致，同一批引入）**：`docs/async_ipc_contract.md` §2 规则 7 的
  已批准变体写"用 `pdMS_TO_TICKS(2000)` 有界发送、**超时自 `delete` 结果**"，
  但实现只做有界发送、**不检查返回值也不释放**（`ui_leveltest.cpp:223-227`；
  `ui_newdict.cpp:173-177` 同款，源头是 `ui_whoami.cpp:292-296`）。
  按"单飞 + 深度 4 + UI 不积压"的论证队列实际不可能满 ⇒ **后果不可达**，故不记为缺陷；
  但契约文本描述了一个不存在的分支（该行是 `355e3dc` 本批登记的）。修法二选一：
  补 `if (xQueueSend(...) != pdTRUE) delete m;`，或把契约措辞改成"有界发送，队列满时丢弃
  （当前单飞下不可达，未实现自释放）"。
- **Nit-2（题干截断与 exclude 的潜在错配）**：`pp_lt_question_t.stem[128]` 是显示副本，
  `lt_exclude_add(s_cur.stem)`（`:520`）用的是**截断后**的串；若服务端题干 >127 B，
  排除项与真实题干不同名 ⇒ 服务端不排除 ⇒ 同卷重复题。当前语法题库题干远短于此（观察项），
  不需要现在改，但值得在 `penpal_api.h` 的 `stem[128]` 注释补一句前提。

## 已核实成立（含申请点名的最没把握点逐条答复）

1. **`lt_stair_step` 对 `grade_staircase` 的转录：成立，我做了两层独立复核**
   - 逐行对齐参考实现（`backend/app/services/leveling.py:363-418` 与
     `scripts/remote_api_demo.py` 步骤 ⑯ 的 `while len(lt_answers) < 16` 循环）：
     连对 2 升级 / 连错 2 降级 / `downs>=2` 定格 / 地板不增 `downs` / 封顶检查在转移检查**之后**
     （第 16 答同时登顶时报登顶，与 Python 的 `for...else` 语义一致）/ `s_ans_n` 先记后步 ⇒
     提交的前缀恰为消费前缀。**逐条相符**。
   - **我自己的对拍（未用作者的脚本）**：把设备侧状态机按 `ui_leveltest.cpp:132-155` + `:505-533`
     重新转写为 Python，直接喂**服务端口径**的 `grade_staircase`：
     · 20000 组随机序列 → 终止判定/消费数 **0 偏差**，答案数分布 2..16，四条终止路径全覆盖
       （B2+ top 538 / Pre-A1 floor 12406 / 16-answer cap 2842 / 2nd demotion 4214）；
     · **枚举式**穷举：长度 2..16 的全部序列中，设备**可能提交的**前缀共 **117586 条** →
       `terminated == True` 且 `consumed == len(前缀)`，**0 偏差**。
       ⇒ "提交条件 = 终止事件、提交恰为消费前缀"这一 v1.17 的核心声明**独立成立**
       （这正是 v1.16 强提 400 的反面）。
2. **exclude 封顶算术：成立，读法也对**。`LT_EXCL_MAX 1990`，`lt_exclude_add()`（`:157-168`）
   以 `s_excl_len + need <= 1990` 准入（`need` 含 NUL 与 `|||`），实际最大 1989 字节 + NUL。
   服务端 `Query(max_length=2000)`（`api/v1/endpoints/users.py:143-145`）校验的是**解码后**的值
   （FastAPI 在参数解析后校验），故 URL 编码膨胀不计入；编码后长度只影响请求行，实测路径
   `/users/me/level-test/questions/next?...` 无长度限制。**单场内 16 答 × ~100 B + 分隔符 ≈ 1.6 KB
   < 1990**，封顶实际不会触发（与"repeats are legal"的注释一致）。另核实 `B2+` 编码为 `B2%2B`
   与服务端 `pattern=^(Pre-A1|A1|A2|B1|B2\+)$` 相符。
3. **中途拉题失败不提交：偏离合理，"永远无法完成"的担心在本题库下不成立**。404 的触发条件是
   服务端 `next_level_test_question()` 两个 maker 都返回 None（`leveling.py:226-290`），
   其中语法侧只有"该级题池在 exclude 后为空"（`bank[0]` 前先按 exclude 过滤）；
   而**每级题池 = 20（Pre-A1/A1/A2）~32（B2+）条，单场答案上限 16** ⇒ 一个级别最多排除 16 条
   < 20 ⇒ `bank` 不可能为空 ⇒ **本库下 404 不可达**。词汇侧（`_vocab_one` 需 ≥4 词）返回
   None 只是断掉一条 maker，语法侧仍在。
   仅当**某级题池被裁到 ≤16 条**时该路径才可达（部署前提，属用户侧），此时"重试一次后放弃且不提交"
   是**正确选择**：服务端要求 `terminated` 才收（未终止前缀必 400），盲答续推等于伪造自报。
   建议不改行为；若要更友好，可在状态行补一句"（本级别题库不可用，请稍后重试）"。
4. **答案 level 上报口径：`LT_ORDER[s_st_idx]` 与 `s_cur.level` 恒等，无分叉窗口**。
   `rq->level` 在 `lt_start()` 时快照（`:265`），`s_st_idx` 只在 `lt_answer()` 里经
   `lt_stair_step()` 变动（`:521`），而 `s_cur` 只被**代次匹配**的结果覆盖（`:448`、`:457`）⇒
   从"发出请求"到"记录答案"之间 `s_st_idx` 不可能变。且服务端 `grade_staircase` 明确
   "answer 的 level 仅作展示"（`leveling.py:369`），两口径不构成契约风险。
5. **字库覆盖：独立复核成立（且比申请更严——我按**当前**库核）**。从
   `Font_Hanzi_16.c` 解析出 **4057** 个 `U+` 字形（与申请一致），并对**当前** `dev.db`
   （**23016** 词，申请生成时是 21311 词）的 `meaning_zh` 全串并集做差：仅缺 **4** 个码位——
   `U+0009`/`U+000A`（制表/换行，不可渲染）、`U+02D0`（IPA ː）、`U+E7B3`（PUA）——
   **后两者正是申请声明"有意不含"**。ASCII 0x20–0x7E 全含，语法题库源码字符集零缺。
   ⇒ "题库后续增长超集"的风险在当前库上尚未发生（生成物已多覆盖 ~1705 词的新增字形）。
6. **v1.13 遗留面：菜单与触摸/键盘并发均成立**。21 入口 = 6/7/8（`page` 显式字段，
   `page_num = max(page)` = 2，点 3 个，手势门 `page_curr < page_num`）；
   `lt_opt_cb`（`:292-296`）与键盘路径都经 `lt_answer()` 的 `s_lt_page != QUIZ || s_lt_task`
   门（`:507`），两者同处 UI 线程（LVGL 事件在 loop 内派发）⇒ 无并发双击重复记答；
   答案数写入有 `s_ans_n < PP_LT_A_MAX` 前置守卫（`:511`），不越界。
7. **退格语义与本地 Dict 一致**：`ui_dictionary.cpp:78-85` 是"输入框空才退出"，
   LevelTest 的 `\b`（`:561-565`）是"直接退出（放弃本次会话，服务端无状态可清）"——
   两者语义不同但都属于各自的既定语义，`lt_session_reset()` 在 entry/重测路径均清位（`:588`、`:573`），完备。

## 验证说明

- **评审方环境**：装有 PlatformIO；**无设备**（COM5 不可达）。
- **独立复跑（BUILD）**：`git worktree add <tmp> bb28ce2`（含本族全部 4 个 SHA 的代码超集）
  + `python -m platformio run -e pda2` → **SUCCESS 44.70 s**，`RAM 57.4% (188140/327680)`、
  `Flash 44.2% (2896249/6553600)`。相对 v1.12 基线（Flash 36.4%）的 +7.8pp ≈ 512 KB，
  与 Font_Hanzi_16 (~488 KB) 相符 ⇒ 字库入 flash 的体量声明成立（app 分区 6.5 MB 余量充足）。
- **独立复跑（SIM，本轮新增证据）**：设备阶梯转写 → 服务端 `grade_staircase` 对拍
  （20000 随机 + 117586 穷举前缀，0 偏差，见上 §1）；服务端契约源码逐条核对
  （`users.py` 的 `next` 端点、`leveling.py` 的 `grade_staircase`/`next_level_test_question`/
  `level_count`）；本地服务实测 `q=苹果 → apple` 等（见 newdict 结果，同源）。
- **CLAIM_ONLY / 未复现（尊重申请声明）**：v1.16 真机中文渲染"用户看到了中文题面"、
  v1.17 真机 3 轮端到端（11/16/12 答）、v1.18 修复后未收到复测反馈——**一律按申请声明记
  CLAIM_ONLY**；作者声称的"20000 序列对拍"我虽独立复现了同类实验，但**不复核其脚本本身**
  （脚本不在仓库内）。
- **未验证**：EPD 像素预算（QUIZ 页 4 行 30 px + 128 B 题干换行后是否溢出可视区）——
  无设备无法测量，**不作为发现**，列真机回归建议项。
- `git diff --check bdb75bf bfc27d2`：我复核通过。

## 对称三格

- **① 是否核了全区间 diff**：是。4 个 SHA 的 `penpal_api.*`（v1.17 契约改动）、`ui_leveltest.*`、
  `ui_deckpro.*`（菜单/注册）、`factory.ino`（poll 挂载）、`fw_version.h`（1.13→1.18 链）
  逐个看过；`docs/issue_list.md` §28-§30 与 `CHANGELOG.md` 按"不评代码面"处理。
  **非连续区间**：夹在中间的 3 个修复轮 commit 已由另一份结果覆盖，本结果不重复计。
  字体生成物按申请声明"评生成管线与覆盖论证，不逐行评点阵"执行（覆盖用差集法独立核）。
- **② 快照是否独立复跑**：部分是——BUILD 独立复跑（干净 worktree @ `bb28ce2`）；
  阶梯逻辑以**自建对拍**独立复现；字库覆盖以 `dev.db` 差集独立复现；
  **真机面全部 UNKNOWN**（无设备），未以编译通过外推任何行为结论。
- **③ 申请"最没把握"7 条是否逐一构造反例**：是，逐条见"已核实成立"1–7 与 Nit-1/2；
  其中第 4 条（`s_enter_pending`）**构造出了反例**（L1），第 3 条（404 部署前提）给了
  可达性的定量边界（题池 >16 即不可达），第 5 条字库用**当前库**（而非申请的快照）复核并加严。

## §9 评审自审清单（承重档）

```text
【异步/契约】1 非 UI 线程调 LVGL？无（任务只碰自持快照与 new 的消息体）✓；
  2 busy 是否只被 gen 匹配结果释放？`s_lt_task` 为任务句柄、任务自清（`:229`），
    gen 不匹配的迟到结果在 `lt_consume()` 丢弃并 delete（`:446-451`）✓；
    **但键缓冲标志 `s_enter_pending` 不在 gen 门控内** ⇒ L1；
  3 worker new 的结果 UI 是否 delete？`lt_consume` 的三条分支都走到 `:500 delete m` ✓；
    快照 `rq` 由任务自删（`:228`）✓；
  4 队列先建后 busy、NULL 不启动？`:253-257` 先建并检返回 ✓；
  5 任务持有 UI buffer 副本？`rq->base/key/level/exclude` 都是 std::string 副本 +
    `ans[]` 值拷贝（`:266-269`）✓；
  6 迟到三路径（离页/取消/超时）：entry/exit 各 ++gen（`:587`/`:597`）✓，结果首字段 gen ✓。
【内存/栈】7 无 memset 非平凡结构（`*out = pp_lt_question_t{}` 值初始化）✓；
  8 无 >400B 聚合赋值（`s_ans` 是 16×~12B）✓；9 无 lv_conf 改动 ✓。
【秘密】10/11 本族不涉 key 缓冲/输入框（复用 penpal 配置链）✓。
【硬件/实时】12 变体无关；13 EPD：每题一次全刷、无光标闪烁（字库切换不改刷新策略）✓；
  14 原子写不涉 ✓。
【方法论】15 每条发现给了 file:line + 反例序列 ✓；16 作者编译/刷写/真机声明按 CLAIM_ONLY
  处理，BUILD 独立重跑 ✓；17 复跑作用域 = 干净 worktree @ bb28ce2，与"本族 4 SHA 超集"
  一致 ✓；18 `leveltest_keyboard_poll` 有生产调用方（`factory.ino:933-934`）✓；
  19 反例库：键缓冲跨页存活、在飞请求、终止边界（第 16 答同时登顶）逐项推演 ✓；
  20 与申请结论相反处（第 4 条）以代码 + 时序证据表述 ✓。
```

## 审批意见：[ ] A 全量接受  [ ] B 退回修订  [x] C 部分接受

- **接受**：v1.13 建成面、v1.16 字库（含覆盖方法论与生成物）、v1.17 阶梯重写（含契约变更与
  "提交=终止事件"的核心声明，经独立穷举复核）、v1.18 的**意图**（不吞键）与 preflight 日志增强。
- **保留**：**L1（P2，应修）**绑 **G-合入**——`s_enter_pending` 需绑定页面/清除点，否则
  结果页在"起步连按两次 Enter"下不可读。
- **入库动作（作者）**：L1 + Nit-1 登记 `docs/issue_list.md`（L1 绑 G-合入；Nit-1 附
  `docs/async_ipc_contract.md` 规则 7 的措辞/实现二选一）；Nit-2 记入该头文件注释即可。
  真机回归建议项一并追加：①起步连按两次 Enter → 结果页必须停留；②QUIZ 页 128 B 题干 +
  4 选项是否溢出可视区。
