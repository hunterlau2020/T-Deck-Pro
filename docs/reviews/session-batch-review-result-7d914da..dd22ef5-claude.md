# 评审结果：NewDict v1.20 重做 + ds4 三段评审处置轮（7d914da..dd22ef5，claude）

- **评审日期**：2026-09-19
- **申请文件**：[session-batch-review-request-7d914da..dd22ef5.md](session-batch-review-request-7d914da..dd22ef5.md)
- **评审提交**：`7d914da`（v1.20 NewDict 重做）、`dd22ef5`（v1.21 ds4 三段处置轮），
  区间首不含（`7d914da..dd22ef5`）
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）+ §2.5 修复轮 delta 复审。
- **评审范围**：§2 核销表按 §0 原则 8 逐条重读代码构造反例；申请自评 6 处逐一尝试击穿
  （对称三格③）；v1.20 的其余新代码（考试卡列表、行池共享、菜单挪位）按常规走查。
  本轮我把大部分精力放在**申请自己点名的"最没把握"第 2 条**——这条我确实构造出了
  一个 ds4 结果里已经预判、但 `dd22ef5` 并未完全堵上的残余分支。
- **评审结论**：**C 部分接受**。L1/N1/F1′/Nit-1/Nit-2 五项核销**全部独立验证成立**；
  申请自评 3/4/5/6 四条均未能击穿；但**自评第 2 条（N2 的失败分支）本轮构造出了
  真实反例**——`nd_consume()` 里 DETAIL **失败**时走 `nd_render_tabs()`，未经过
  `nd_render_detail()`，`s_enter_pending` 未被清位，与 ds4 结果原文预判的场景完全
  相符。参照上一轮（`bdb75bf..7bab081`）ds4 自己的先例——"哪怕只剩一处新发现/残留
  的 P3，也不给全量 A"——本轮同样不给 A。

---

## 复核方法

这批的结构和上一轮（`bdb75bf..7bab081`）的 ds4 处置轮几乎同构：核销表 + 自评"最
没把握"清单，明确邀请评审员来击穿。我没有重新过一遍三份前置 ds4 结果的全部内容
（那些已经是独立、扎实的复核，见 `session-batch-review-result-leveltest-family-
ds4.md`、`-newdict-d5755ae-ds4.md`、`-bdb75bf..7bab081-ds4.md`），而是：①读了这
三份文件里 L1/N1/N2/N3/F1′/Nit 的原始反例序列和最小修复建议；②逐条对照 `dd22ef5`
的 diff，看修复是否真的落在原反例路径上；③申请自评 6 条，每条都在代码里手工走
一遍调用序列，尝试构造新反例。

---

## §2 核销表复核

### L1（P2，leveltest）—— 成立，构造申请自评①的交错窗口未能击穿

`s_enter_pending` 的置位条件从"非 QUIZ 页"收紧为"仅 HOME 页"（`ui_leveltest.cpp:
574`），并在 `lt_render_question()`（`:402`）首行加了 `s_enter_pending = false;`。
我重新走了一遍 ds4 原始的 7 步反例：起步连按两次 Enter，第二次按键发生在
"HISTORY 已返回、页面仍是 HOME、NEXT 请求已在飞"这个窗口——此时置位条件
`s_lt_page == LT_PAGE_HOME` 仍然成立，所以标志**还是会被设置**（这点申请自评①
说得对："v1.18 的原始场景…仍覆盖"）。关键在于消费顺序：`leveltest_keyboard_poll()`
每 tick **先**调用 `lt_consume()`，NEXT 结果到达时 `lt_consume()` 内部调用
`lt_render_question()`（清位）——**清位发生在同一 tick 内、早于**该 tick 后面才
执行的"标志是否幸存到 RESULT"检查点（`:553`该检查点本身条件未变，仍是
`s_lt_page != LT_PAGE_QUIZ`，但由于清位已经先手，检查点看到的值已经是 false）。
我没能构造出"清位发生得比检查点晚"的时序（两者在同一次函数调用栈内顺序执行，
不存在真正的并发交错）——**这条反例打不穿**。

### N1（P2，newdict）—— 成立

`:348`/`:359`（ds4 报告标注的旧行号，对应本次改动位置）两处 `lv_label_set_text`
改为 `nd_set_text`，与 head/senses 两处已经使用的字体切换函数一致。独立核对无遗漏
第三处。

### N2（P3，newdict）—— **部分成立，本轮击穿了申请自评②预判的残余分支**

`dd22ef5` 在 `nd_render_detail()`（`:490`）和 `nd_tab_set()`（`:643`）两处加了
`s_enter_pending = false;`，覆盖了 ds4 原始反例（DETAIL **成功**打开后退回 LIST
被补射）。但申请自评②自己提出了一个问题："若该次 DETAIL **失败**（`nd_render_
tabs()` 回列表），缓冲仍在——会在下一 tick 补射一次'开焦点行'。可接受…还是该连
失败分支一起清？"——我去读了 `nd_consume()` 的 DETAIL 分支（`:583-593`）：

```c
} else {                             /* DETAIL */
    if (!m->ok) {
        char buf[128];
        snprintf(buf, sizeof(buf), "detail failed: %s", m->err);
        nd_status(buf);
        nd_render_tabs();            /* <-- 失败路径：不经过 nd_render_detail() */
    } else {
        s_detail = m->detail;
        nd_status("");
        nd_render_detail();          /* <-- 只有成功路径清位 */
    }
}
```

**反例确实成立**：TABS 页打开一行 → `nd_open_detail()` 发起 DETAIL 请求（页面仍
TABS）→ 请求在飞期间再按一次 Enter → `s_enter_pending = true`（`:717`，条件为
`s_nd_page == ND_PAGE_TABS`，此时仍满足）→ 该次 DETAIL **失败**（网络错误/服务端
404 等）→ `nd_consume()` 走 `!m->ok` 分支 → 只调 `nd_render_tabs()`，**不经过**
`nd_render_detail()` 也不经过 `nd_tab_set()`，`s_enter_pending` 未被清除 → 下一
tick，`newdict_keyboard_poll()` 的补射检查（`:664` `if (s_enter_pending &&
!s_nd_task && s_nd_page == ND_PAGE_TABS)`）成立 → 自动重新触发**同一行**的
`nd_open_detail()`，即刚失败的那次请求被无声地重试一次。

**这条与 N2 原反例是同一机制、不同触发分支**：原反例的触发条件是"DETAIL 成功"，
这条是"DETAIL 失败"，`dd22ef5` 只堵了前者。

- **影响判定**：后果止于"用户没主动要求的一次重试，重试内容是用户自己刚才想做的
  同一件事（打开同一行）"——不会打开错误内容、不会导致数据损坏或崩溃、不会像原
  N2 那样把用户"弹回一个已经主动离开的页面"。如果失败是瞬时的（网络抖动），这次
  自动重试甚至可能直接帮用户把请求补成功；如果失败是持续的（如该词条 ID 有效但
  详情接口挂了），用户会看到同一条"detail failed"消息重复一次，仅此而已。
  `C/3/有声 → P3`（多前提：需要"在飞时按 Enter" + "那次请求恰好失败"，比原 N2
  的可达性更窄，与 N2 同一严重度档）。
- **两轴**：`VERIFIED`（CODE + 时序推导，逻辑与 N2 完全同构）｜`VIOLATES`（v1.18
  引入的"键缓冲"语义未绑定失败路径，是 N2 同一条纪律的未覆盖分支）。
- **最小修复**：`nd_consume()` 的 DETAIL 失败分支加一行 `s_enter_pending =
  false;`（或者更彻底：把清位挪到 `nd_consume()` 处理 DETAIL 请求的**入口**，
  成功失败共用，比在两个渲染函数里各写一份更不容易再漏第三处）。**不要求本轮
  阻断**——按申请自己的问法，"可接受"的判断也站得住，但既然发现了就建议顺手
  带上，成本是一行代码，比留着一条"已知但未登记"的残余分支更干净。

### N3（结构性消除，newdict）—— 认可

v1.20 把首页改成"搜索优先"（不再是"entry 自动拉取全库"），`entry_nd` 的原始
自动拉取分支已经不存在——本轮独立确认 `ui_newdict.cpp` 里 entry 相关代码路径
（`nd_tab_set`/`nd_page_show` 一带）没有重新引入"进屏自动发请求"的逻辑，N3 所在的
"entry 时上一场请求仍在下飞"这个场景在新交互模型下确实不再有对应的触发点，接受
"结构性消除"的定性。

### F1′（P3，wifi）—— 成立

`wifi_scan_stop_and_release()` 快径（`ui_deckpro.cpp:3372` 一带）现在无条件先调
`esp_wifi_scan_stop()` 再 `WiFi.scanDelete()`，覆盖了 `scanComplete()` 超时判负
分支（驱动仍在飞但函数已返回非 RUNNING）。注释也按 ds4 的建议收窄了"race-free"的
适用范围（只对"已终结"分支成立，不再笼统声称整个快径都无竞态）。

### Nit-1（xQueueSend 自释放）/ Nit-2（头注释）—— 成立

`ui_leveltest.cpp:224`、`ui_newdict.cpp:174`、`ui_whoami.cpp:293` 三处都加了
`if (xQueueSend(...) != pdTRUE) delete m;`，与 `async_ipc_contract.md` 规则 7
已批准的措辞对齐（选择改代码而不是改契约文本，契约文件本轮无需改动，逻辑自洽）。
`ui_newdict.h` 补了"设备侧只能英文检索"的头注释；`penpal_api.h` 的 `stem[128]`
补了"未来题干超长会与 exclude 错配"的前提注记。均确认落地。

---

## 申请自评 6 处逐一复核

1. **L1 交错窗口**：见上文核销表 L1，未能击穿。
2. **N2 清位位置**：见上文核销表 N2，**击穿**——DETAIL 失败分支确实未清位，是
   一条真实但影响很小的残余反例。
3. **考试卡本地翻页算术**：逐行核对 `nd_open_exam`/`+`/`-` 三处对 `s_exam_focus`/
   `s_exam_top` 的调整（`ui_newdict.cpp:746-751`）——前进 `focus>=top+ROWS →
   top=focus-ROWS+1`，后退 `focus<top → top=focus`，是标准的滑动窗口分页算法，
   对 15 张卡/8 行窗口逐步手算（focus 0→14 与 14→0 两个方向）未发现差一或越界。
   **未能击穿**。
4. **Tab 激活态可辨识**：`nd_tab_style()`（`:401-412`）已从 `LV_OPA_20` 灰底改成
   `LV_OPA_COVER` 黑底 + 反色文字（激活）vs 透明底黑字（未激活），与 issue_list
   §23 的"灰 ≥128 阈值→白"结论一致，是二值安全的对比而非灰度混合。**代码层面
   确认已修复**；真机肉眼可辨这一步仍是 `CLAIM_ONLY`（申请自己也标注了这点，
   本轮无设备，尊重该声明）。
5. **exam 与 q 参数叠加**：设备侧 Home/List 两个 Tab 从不同时发出 `q` 和
   `exam`（`nd_fetch_list`/`nd_open_exam` 两个发起点各自只带一个参数，本轮独立
   核对确认），服务端语义按申请描述是 AND 过滤、无冲突。服务端代码本轮未独立
   复核（不在这批 diff 范围内，超出评审面），接受申请描述的低风险判断。
6. **菜单第 1 屏 7 入口**：`ui_deckpro.cpp` 的 `menu_btn_list` diff（`7d914da`）
   显示 NewDict 从 page-2 (95,189) 挪到 page-1 (23,189)，原 page-1 在该坐标此前
   为空位（未被其它条目占用），page-2 该坐标空出、不产生坐标空洞（page-2 后续
   条目坐标未变）。**未发现溢出或坐标冲突**。

---

## 已通过项（本轮独立核对，非转述申请或 ds4）

- 三份 ds4 前置结果里"已核实成立"的条目（LevelTest 阶梯转录穷举复核、字库覆盖
  差集复核、WordBank 异步契约十一条、栈缓冲差一复核等）本轮未重新验证——那些是
  独立、扎实的复核工作，且本区间（`7d914da..dd22ef5`）未改动对应代码，按 §2.5.3
  delta 复审精神，未改章节视为已闭合，不重新开盘。
- v1.20 的行池共享结构（TABS/DETAIL 两顶层页 + 8 行共享池）与 List Tab 的懒加载
  （首次打开才拉 `/words/exams`，`nd_tab_set:646-649`）读了一遍，逻辑与申请描述
  一致，未发现新的资源泄漏或状态残留。

---

## 验证说明

- 本环境无 `pio`、无设备：BUILD/HW 证据沿用申请方记录（编译 SUCCESS、设备深睡
  安全中止刷写、本地开发服务器 API 冒烟），标 `PARTIALLY_VERIFIED`/`CLAIM_ONLY`
  （申请自己已如实标注"全部 CLAIM_ONLY"，本轮不升级）。
- 独立读取的仓库源码位置：`ui_leveltest.cpp:395-420`/`540-580`（L1）、
  `ui_newdict.cpp:401-412`（Tab 样式）、`:487-535`（N1/N2 渲染函数）、`:535-596`
  （`nd_consume` 全量，N2 残余分支的直接证据来源）、`:639-770`（`nd_tab_set`/
  `newdict_keyboard_poll`/翻页算术）、`ui_deckpro.cpp:3372-3390`（F1′）、
  `:275-292`（菜单坐标）、`ui_whoami.cpp:290-296`（Nit-1）、`penpal_api.h:233-239`
  （Nit-2）。三份 ds4 前置结果全文读过，用于重建 L1/N1/N2/N3/F1′ 的原始反例序列。

---

## 对称三格（§7.2）

1. **全区间**：申请归属表 6 个受评文件 + 3 个不评文档文件全部过了一遍；额外去读
   了三份前置 ds4 结果的完整原文（不止申请转述的核销表摘要），这是本轮发现 N2
   残余分支的直接来源——申请自己的核销表摘要没有把"DETAIL 失败分支"单独列出来，
   是我回头核对 ds4 原始反例后再去代码里追的。
2. **独立复跑**：无 pio/无设备，SIM 级为主。L1 的交错时序、N2 残余分支、考试卡
   翻页算术均按调用顺序手工推演，不是转述申请或 ds4 的结论。
3. **申请自评 6 条**：逐一见上文，**第 2 条被击穿**（N2 残余），其余 5 条未能
   构造出反例——如实记录"打不穿"的多数结果，不为凑数量而夸大。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 2 busy/键缓冲释放：`s_enter_pending`（leveltest）现已在唯一置位路径 + 唯一
      消费路径外新增一个清除点，经复核完整；`s_enter_pending`（newdict）的清除点
      **有一条分支遗漏**（N2 残余，已单列）。
- [x] 3 worker `new` 结果 UI `delete`：`nd_consume()` 全部三个 kind 分支末尾统一
      `delete m`（`:595`），DETAIL 失败分支也不例外，没有遗漏——这条本身没问题，
      遗漏的只是"清位"这个次要状态位，不是内存所有权。
- [x] 15 N2 残余 finding 给了具体 `file:line`（`ui_newdict.cpp:583-588`）+ 完整
      反例序列。
- [x] 16 申请 HW/API 证据如实标注 `CLAIM_ONLY`，本轮未越权升级为 `VERIFIED`。
- [x] 20 与 ds4 前置结论一致处未重复；仅在 ds4 自己预判但未验证的"N2 是否该连
      失败分支一起清"这一具体问题上给出了独立、基于代码的答案（击穿）。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

**应修（P3，不阻断，登记 `issue_list.md` 绑 G-真机，参照上一轮同类处理先例）**：
`nd_consume()` 的 DETAIL 失败分支补 `s_enter_pending = false;`（或把清位挪到
DETAIL 请求处理的公共入口）。

**保留**：L1、N1、F1′、Nit-1、Nit-2、tab 激活态自纠、v1.20 全部新交互（搜索优先
Home、考试卡 List、行池共享、菜单挪位）——均已独立核实成立或未能击穿。

**回退项**：无。
