# 评审结果：NewDict v1.20 重做 + ds4 三段评审处置轮（`7d914da..dd22ef5`，Grok）

- **评审日期**：2026-09-18
- **评审人**：Grok（Cursor）
- **申请文件**：[session-batch-review-request-7d914da..dd22ef5.md](session-batch-review-request-7d914da..dd22ef5.md)
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**，代码口径（§2.3 A/B/C）；修复核销按 §0 原则 8（构造反例击穿，不是复读申请表）
- **评审提交**（作者意图 = v1.20 + v1.21；见下方范围订正）：

  | commit | 内容 | 评审面 |
  |---|---|---|
  | `7d914da` | v1.20：NewDict 搜索优先 + Home/List + 考试选卡 | 代码（7 文件，494+/175-） |
  | `e794875` | ds4 三份分段结果入库 | 纯文档，不评代码面 |
  | `dd22ef5` | v1.21：L1/N1/F1′/N2 + 3×Nit | 代码（10 文件，119+/17-） |

- **工作树 HEAD**：`c5734c4`（`git diff dd22ef5 HEAD` 仅本申请文件两笔 docs commit）。**本批代码 findings 仍存活。**
- **评审结论**：**A 全量接受**。无 P0、无未闭合 P1。ds4 保留项 **L1 / N1 / F1′ / N2 / N3 / Nit-1/2** 的主反例均击不穿。v1.20 新引入 **1×P2**（`ND_REQ_LIST` 结果按**当前 tab** 分流，字母跳 Home / 点 Home 的文档路径会把考试词书写进搜索列表）。未独立 `pio run` → L2 BUILD 轴 `UNKNOWN`，不挡代码 A。
- **G-合入**：是（无 P0/P1）。新 P2 **不得**写成已闭合，须进 `issue_list.md`。**G-真机**：否（申请声明 v1.20/v1.21 均未上机，HW 全 `CLAIM_ONLY`）。

---

## 0. 范围订正（申请自校 vs 指南 §7.1）

申请写「区间 `7d914da..dd22ef5`，**首不含**」且 `git rev-list --count 7d914da..dd22ef5` = 2，但文件名按指南是**首末含两端**，正文又把 `7d914da`（v1.20）列为受评提交。两者矛盾：

- git `7d914da..dd22ef5` 实际是 `e794875` + `dd22ef5`，**不含 v1.20**，且申请引用的 `e970142` 在本仓库**不存在**；
- 若只按「首不含」做 diff，会漏掉 `penpal_api.cpp` 与 NewDict 重做本体。

本结果按**作者意图**评 **含 `7d914da` 的 v1.20 功能面 + `dd22ef5` 修复核销**（`e794875` 归不评）。不因自校笔误退回申请。

`git diff --check 7d914da^..dd22ef5`：通过。

### 区间文件归属

| 文件 | 归属 | 处置 |
|---|---|---|
| `examples/pda2/ui_newdict.cpp` / `.h` | **评（重点）** | v1.20 交互重做 + N1/N2/Nit |
| `examples/pda2/penpal_api.cpp` / `.h` | **评（重点）** | `penpal_wb_exams` + `exam=`；Nit-2 stem 注记 |
| `examples/pda2/ui_deckpro.cpp` | 评 | 菜单第 1 屏 7 入口；F1′ 快径 `stop()` |
| `examples/pda2/ui_leveltest.cpp` | 评 | L1 置位/清位 + Nit-1 |
| `examples/pda2/ui_whoami.cpp` | 评（低风险） | Nit-1 一行 |
| `examples/pda2/fw_version.h` | 评（低风险） | 1.20 → 1.21 |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` | 不评（纯文档） | 台账 §31 陈述已对照代码 |
| `docs/reviews/session-batch-review-result-*-ds4.md` ×3 | 不评 | `e794875` 结果入库，未覆盖既有结果 |

HEAD 之后落入、**不评**：本申请文件（`618639f` / `c5734c4`）。

---

## 1. ds4 保留项核销（击穿实验）

| 发现 | 申请处置 | 本轮裁定 | 证据 |
|---|---|---|---|
| **L1（P2）** 缓冲 Enter 活到 RESULT 同 tick 补射 | HOME-only 置位 + `lt_render_question()` 清位 | **主反例闭合** | 见 §1.1 |
| **N1（P2）** 详情 examples/tail 漏切 CJK | 改 `nd_set_text` | **闭合** | `ui_newdict.cpp:516` / `:529`；head/senses 仍走 `nd_set_text`（`:500`/`:509`） |
| **F1′（P3）** 快径吞超时判负、跳过 `stop()` | 快径无条件 `esp_wifi_scan_stop()` + 注释收窄 | **主反例闭合** | `ui_deckpro.cpp:3384-3387`：`!= RUNNING` 分支先 `stop()` 再 `scanDelete()`。ds4 原文 impact ②（无代次的 `s_scan_done_cnt`）仍在，不是本条复发 |
| **N2（P3）** 缓冲 Enter 跨 DETAIL 存活 | `nd_render_detail` + `nd_tab_set` 清位 | **主反例闭合**；失败分支残余见自评答复，不升级 | `:490-492`、`:643-645`；消费点仍要求 `s_nd_page == TABS`（`:664`） |
| **N3（P3）** entry 在飞不重拉 | 声称 v1.20 结构性消除 | **闭合（N/A）** | `entry_nd`（`:789-798`）只 `gen++` / 清 pending / 回 Home，**无** `nd_fetch_list`。空列表直到用户搜索，是搜索优先的既定语义 |
| **Nit-1** 契约规则 7 超时自 `delete` | 三处补 `!= pdTRUE` | **闭合** | `ui_whoami.cpp:293-294`、`ui_leveltest.cpp:224-226`、`ui_newdict.cpp:174-176` |
| **Nit-2** 设备侧无中文输入 | `ui_newdict.h` 能力注记 | **闭合** | 头注释 `:10-12`；键盘仍只放行 `[A-Za-z0-9 ']`（`:771-772`） |
| **Nit-2(lt)** `stem[128]` 与 exclude | `penpal_api.h` 前提注记 | **闭合** | `:236-241` |

### 1.1 L1 完备性（申请自评第 1 条，强制击穿）

原反例的承重步骤是：**HISTORY 在飞（HOME）Enter → 缓冲；HISTORY 回、同 tick 发首题；首题在飞时页仍是 HOME，再 Enter 再次置位；首题返回 `lt_render_question` 切 QUIZ，消费点因 `page==QUIZ` 既不消费也不清；标志活过整场，RESULT 同 tick 补射。**

只收紧「置位 = HOME」**不够**：第二次 Enter 发生时页确实还是 HOME。`lt_render_question()` 里的 `s_enter_pending = false`（`:402-404`）才是承重清除点。

poll 顺序闭合交错窗口：

1. `lt_consume()` 先跑（`:545`）。NEXT 成功 → `lt_render_question()` **先清位再** `lt_page_show(QUIZ)`。
2. 然后才判 `s_enter_pending && !s_lt_task && page != QUIZ`（`:554`）。此时 pending 已假、页已是 QUIZ，同 tick 不会发箭。
3. HISTORY 返回只 `lt_render_home()`（`:503`），**不清** pending → 同 tick 消费点发首题。这是 v1.18「不吞键」的目标行为，不是 L1 复发。单飞下 HISTORY 与 NEXT 不能同 tick 到达，不存在「HOME 渲染」与「首题清位」对同一条结果交错。
4. SUBMIT 在飞时页仍是 QUIZ（RESULT 要等 `lt_render_result`），HOME-only 置位进不去；`lt_render_result` 本身不清 pending，但到达 RESULT 时 pending 必已在首题渲染被清。

**HISTORY 返回（HOME 渲染）→ 同 tick 发箭** 与 **首题渲染清位** 没有剩余窗口。主反例击不穿。HW ⑤（起步双 Enter 走完一场，结果页必须停留）仍 ⏸。

### 1.2 N2 失败分支（申请自评第 2 条）

主反例：DETAIL 成功渲染清位 → 退回 TABS 不会补射。闭合。

作者点名的残余：`nd_row_cb` → `nd_open_detail` 期间页仍是 TABS，Enter 置位（`:716-718` 无页面绑定，但 DETAIL 页的键在 `:684-687` 先被吃掉，故置位只发生在 TABS）。DETAIL **失败**走 `nd_render_tabs()`（`:588`）**不清位** → 同一次 poll 的 `:664` 消费点成立 → 再开一次焦点行。

这不是「退一次退不掉」，是失败后自动重试一次，等价用户再按 Enter。**不记缺陷**（§0 原则 7 / 原则 11）。若产品要「失败即作废意图」，在 `:584-588` 加一行 `s_enter_pending = false` 即可，不必再开修复轮。

---

## Findings

### P2-1：`ND_REQ_LIST` 结果按**当前 `s_tab`** 分流，不按请求快照——开考试词书后跳回 Home，词书页会写进搜索列表

- **位置**：`ui_newdict.cpp:216-223`（启动时用 live `s_tab` 填 `q` vs `exam`）× `:548-569`（消费时再读 live `s_tab` 决定写入 `s_list` 还是 `s_exam_list`）× `:771-773`（任意字母 → `nd_tab_set(HOME)`，文档行为）× `:639-652`（点 Home tab 同样切 `s_tab`，在飞请求不取消）
- **证据 / 反例（SIM）**：
  1. List 开 IELTS → `nd_open_exam` 发 `ND_REQ_LIST`（`exam=IELTS`，`q` 空），状态行 "Fetching exam book..."。
  2. 在飞期间按一个字母（申请 §1：**任意字母输入自动跳回 Home**）或点 Home tab → `s_tab = HOME`，`s_nd_gen` 不变，任务继续。
  3. 词书页返回：`m->kind == ND_REQ_LIST` 且 `s_tab == HOME` → `:561-567` 把 **IELTS 8 行**写入 `s_list`，`s_query_dirty = false`。
  4. Home 头显示 `Found 1-8 / 5700`，搜索框里可能只有刚打的那个字母；再 Enter 打开的是考试词，不是该字母的搜索命中。
- **对称路径**（较轻）：Home 搜索在飞时点 List → 结果写入 `s_exam_list`。若当时仍在考试卡视图（`ND_LS_EXAMS`）则暂不显示；若 `list_sub` 仍是 WORDS（开过词书再跳走再回来），考试名头下会出现无关搜索命中。
- **影响**：`D/2/静默 → P2`（错数据集被当成当前视图的查询结果；可达性 2 = 在飞窗口内切 tab / 打字，但窗口是整段 HTTPS，且跳 Home 是文档主路径）。不升 P1：不 busy 死、不写坏 NVS，再搜一次或重开卡可恢复。回升：若把「List 在飞时打字」当日常主路径验收。
- **两轴**：`VERIFIED`（CODE/SIM）｜`VIOLATES`（`async_ipc_contract.md` §2 规则 5「任务持快照」的消费侧对称：结果必须按**发出时的目的地**落地，不能按到达时的 UI 页）
- **最小修复**：`nd_msg_t` 增加目的地（`exam[20]` 或 enum `dest`），worker 从 `rq` 抄过去；`nd_consume` 只按消息字段写入 `s_list` / `s_exam_list`。切 tab 时可选 `s_nd_gen++` 丢弃在飞 LIST（会牺牲「后台填考试卡」）。
  **须补回归（SIM/HW）**：开考试卡 → 在飞时打 `a` 跳 Home → 落地后 Home 不得出现该考试的 5700 词分页；应保持搜索语义或丢弃该结果。

### P3：无例句 / 无词块时 `ex[]`/`tail[]` 未终止就 `nd_set_text`（根因既有 v1.19，本批 N1 触及现场）

- **位置**：`ui_newdict.cpp:511-518`（`ex`）、`:520-529`（`tail`）。对照 senses 在 `off==0` 时有 `"(no senses)"`（`:508`）。
- **证据**：`char ex[...]` 栈上未初始化；`ex_count==0` 时 for 不跑、没有 `ex[0]='\0'`。`nd_set_text` → `lv_label_set_text` → `strlen`。v1.19 同款（ds4 只核了循环跑起来时的差一，没核空数组）。本批 N1 把这两处改成 `nd_set_text`，仍未补终止符。
- **影响**：`D/1/有声 → P2` 按表；因根因既有、ds4 已漏审一轮、且 ESP32 栈上常能撞到 `NUL`（表现为空白而非必崩），**按 §4.2 既有根因走有声表并下调为 P3 应修**。回升：真机无例句词条出现乱码/复位。
- **两轴**：`VERIFIED`（CODE）｜`NO_CONTRACT`
- **最小修复**：`ex[0] = '\0';` / `tail[0] = '\0';`，或与 senses 一样写 `"(none)"`。HW：打开无 examples 的词条，例释区不得乱码。

---

## Nit（≤3）

1. **`ui_newdict.h` 能力注记已补，但首段仍写「menu page two / two internal states (LIST / DETAIL)」**——与 v1.20 第 1 屏 + TABS/DETAIL 不符。改三行即可。
2. **Home 搜索框 placeholder 走 theme 浅灰**（`lv_theme_default.c:558-559` `lv_palette_lighten(GREY,1)`），按 issue_list §23 阈值会画成白。与 Dict / WiFi 输入框同源，状态行 `"type a word, Enter: search"` 兜底。**不记缺陷**；若真机空 Home 完全看不出这是搜索框，再把 placeholder 改 `lv_color_black()`。
3. **申请自校**：`e970142` 不存在；「首不含」与文件名含两端 + 把 `7d914da` 列入受评提交三者不一致。下次申请用 `git rev-list --oneline <first>^..<last>` 贴实际 SHA。

---

## 已通过项（v1.20 功能面 + 自评 3–6）

1. **考试卡本地翻页算术（自评第 3 条）：成立，但是滑动窗不是 8+7 整页。** `PP_WB_ROWS=8`，15 卡下标 0..14。`focus >= top+8` → `top = focus-8+1`；反向 `focus < top` → `top = focus`。推演：focus 0–7 时 top=0；focus=8 → top=1；…；focus=14 → top=7，可见 `[7,14]` 仍是 8 行。末项再 `+`：`focus+1 < 15` 为假，停住。Enter 用 `nd_open_exam(s_exam_focus - s_exam_top)` 还原绝对下标。无越界、无漏卡。作者写的「两页 8+7」是心智模型，实现是一次滚一行。
2. **Tab 激活态（自评第 4 条）：代码二值安全。** `nd_tab_style`（`:407-414`）激活 = `LV_OPA_COVER` 黑底 + 白字，非激活 = 透明底 + 黑字；已不用 `LV_OPA_20`。`lv_obj_get_child(btn, 0)` 即创建时的 label。肉眼可辨 ⏸（EPD 1bit 反色按 §23 应可见）。
3. **exam 与 q 叠加（自评第 5 条）：设备不同时传。** HOME 只填 `rq->q`，LIST 词书只填 `rq->exam`（`:216-223`）；`penpal_wb_list` 两者都走 `s_urlenc`（`:666-673`），中文考试名可编码。AND 过滤是服务端语义，本屏构不出双参数。✓
4. **菜单 7/7/8（自评第 6 条）：无重叠、无溢出。** 第 1 屏 NewDict 落在空位 `(23,189)`，Whoami `(95,189)`，`(167,101)` / `(167,189)` 仍空。坐标轴仍是 `23/95/167 × 13/101/189`。`page` 最大 2，三屏手势门不变。✓
5. **API 层**：`GET /api/v1/words/exams`（`pp_url` 加前缀）；`pp_wb_exam_t{exam[20], count}`；`PP_WB_EXAM_MAX 16`；解析 `out[n] = pp_wb_exam_t{}`。申请的本地 15 卡 / `?exam=中考` 冒烟按 CLAIM_ONLY 不采信、也不证伪。
6. **异步契约（WordBank 行）**：队列一次创建永不删、结果首字段 `gen`、entry/exit 各 `++`、快照 `new nd_req_t`、栈 1024×8 / prio 1、UI 不跨线程调 LVGL、消费全分支 `delete m`。Nit-1 之后规则 7 的「有界发送 + 失败自释放」与三处实现一致。`s_nd_task` 仍由任务自清（已批准 wa 变体）。
7. **搜索框 EPD 纪律**：`LV_PART_CURSOR` `bg_opa=TRANSP` + `anim_time=0`（`:350-352`）。与 v1.12 真门一致。主区 `bg_opa` 那一行 selector 实际是 `CURSOR|MAIN` = 仅 CURSOR（`LV_PART_MAIN==0`），不影响。
8. **N1 字体**：examples/tail 已切 `nd_set_text`；`nd_has_cjk` 仍是高位字节启发式，与 v1.16 同源。真机中文半句 ⏸。
9. **F1′ 注释**已按 ds4 收窄到「done with results **以及** timeout-judged-failed」。快径对已完成扫描的 `stop()` 按框架是 no-op，不破坏 F1 终态早退（避免 `release_pending` 楔死）。

**在飞改查询被 LIST 成功写回清 `s_query_dirty`**（v1.19 Nit-3 变体：v1.20 字母不再在飞丢弃，更容易撞）：搜索在飞时继续打字，返回后 dirty=false，下一次 Enter 开焦点行而非搜索。一键可恢复，**不单列缺陷**；与 P2-1 同源的「消费侧不看请求意图」，修 P2-1 时顺手比较 `s_query` 与快照即可。

---

## 验证说明

- **评审方环境**：无 PlatformIO（`python -m platformio` 不可用）→ BUILD `UNKNOWN`；**无设备**。
- **独立复跑**：CODE + SIM（L1 时序、考试卡窗、LIST 分流、空 `ex[]`）。未跑 `pio run`，不以申请「v1.20/v1.21 各自 SUCCESS」外推。
- **API 冒烟 / 刷写**：申请声明本地 15 卡 / 中考 1903 与「深睡安全中止、零写入」一律 `CLAIM_ONLY`。
- **§31 真机清单 ④⑤⑥**：未测。

---

## 对称三格

- **① 全区间 diff**：是。含 `7d914da` 的 7 文件 + `dd22ef5` 的 10 文件；`e794875` 三份 ds4 结果只核「未覆盖旧结果」。不按申请「首不含」漏评 v1.20。
- **② 快照独立复跑**：否（无 pio / 无设备）。不以编译声明当 VERIFIED。
- **③ 申请「最没把握」6 条**：逐条给了结论。第 1 条 L1 交错窗口 **主反例击不穿**（清位承重 + consume-then-fire）；第 2 条失败分支 **不记缺陷**；第 3–6 条算术/反色/exam/菜单 **成立**。在第 5 条旁边发现 **P2-1**（目的地按 live tab，不是双参数冲突）。

---

## §9 评审自审清单（承重档）

```text
【异步/契约】1 非 UI 线程调 LVGL？无 ✓；2 busy/gen：s_nd_task 任务自清（wa 变体）+ consume gen 丢弃 ✓，
  但 LIST 结果落地不按快照目的地 ⇒ P2-1；3 worker new 结果 UI delete？nd_consume 全分支 :595 ✓，
  发送失败任务自删 ✓；4 队列先建后启动 :205-208 ✓；5 任务持 base/key/q/exam 副本 ✓，消费侧未用 ✓；
  6 迟到三路径 entry/exit ++gen ✓。键缓冲不在 gen 内：L1/N2 主反例已清，N2 失败分支不升级。
【内存/栈】7 无 memset 非平凡结构（page/detail 值初始化）✓；8 无 >400B 栈上 =T()
  （s_list=m->page / s_detail=m->detail 是静态对象赋值）✓；9 无 lv_conf 改动 ✓。
  空 ex[]/tail[] 未终止 ⇒ P3。
【秘密】10/11 本屏不存 key；git grep 真实 key 未复跑，沿用既有 0 hits。
【硬件/实时】12 变体无关；13 EPD：Tab 反色二值安全 / 光标 anim_time=0；placeholder 浅灰观察项；
  14 原子写不涉。
【方法论】15 P2-1/P3 均有 file:line + 操作序列 ✓；16 作者编译/刷写/API 按 CLAIM_ONLY ✓；
  17 无 pio 复跑，标 UNKNOWN ✓；18 newdict_keyboard_poll 有 factory.ino:935-936 调用方 ✓；
  19 反例库：在飞切页、键缓冲跨页、空缓冲、字体漏切逐项推演 ✓；20 与申请「N3 不适用」一致，
  以 entry 无 fetch 为据而非票数。
```

---

## 审批意见：[x] A 全量接受  [ ] B 退回修订  [ ] C 部分接受

- **接受**：v1.20 搜索优先 + 双 Tab + 考试选卡的主路径与 API 接线成立；v1.21 对 ds4 **L1/N1/F1′/N2/N3/Nit-1/2** 的主反例修复成立，可按 v1.21 继续推进。
- **应修（不挡 G-合入）**：**P2-1** 绑 **G-合入**（LIST 结果按请求目的地落地）；**P3** 空 examples/tail 补 `NUL`（可与 P2-1 同批）。登记 `docs/issue_list.md`。
- **真机（G-真机）**：申请 §31 ④ F1′ / ⑤ L1 双 Enter 结果页 / ⑥ 详情中文半句，外加 P2-1 的「开卡后打字跳 Home」。COM5 待刷 v1.21。
- **入库动作（作者）**：P2-1 + P3 进 §31 或新条；申请范围记法下次与指南「含两端」对齐。
