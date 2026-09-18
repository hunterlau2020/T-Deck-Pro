# 评审结果：NewDict v1.20 重做 + ds4 三段处置轮（GPT）

- **评审日期**：2026-09-18
- **申请文件**：[session-batch-review-request-7d914da..dd22ef5.md](session-batch-review-request-7d914da..dd22ef5.md)
- **评审依据**：`docs/review_guide.md` v1.3（代码口径 §2.3 A/B/C；G-评审 §2.2、§7.1）
- **评审基线**：`HEAD` = `c5734c4c8b33314b488ce7a45e667f490181b9b0`。该 HEAD 已越过
  被评代码的末端 `dd22ef5`，新增内容仅为申请文件命名/引用修订，未改变本轮受评代码。
- **评审提交与实际连续范围**：申请意图为端点 `7d914da`（v1.20）和 `dd22ef5`
  （v1.21）；按指南“首末含两端”复核的连续范围为 `7d914da^..dd22ef5`，共 **3** 个
  提交：

  | commit | 结论 |
  |---|---|
  | `7d914da` | issue：引入 NewDict Home/List 在飞切换的结果归属缺口（P2-1） |
  | `e794875` | docs-only：三份 ds4 结果归档；不改变设备运行行为 |
  | `dd22ef5` | clean（对 L1/N1/F1′/N2 的本轮修复成立）；未覆盖 P2-1 |

- **评审结论**：**C 部分接受**。保留 1 项 P2（绑 G-合入）；此外申请文件未满足 G-评审
  的范围/归属表要求，须最小修订后重新提交，不要求重做本轮代码。

## 区间文件归属

| 文件 | 归属 | 处理 |
|---|---|---|
| `CHANGELOG.md` | 文档 | 核对 v1.20/v1.21 记录与本轮代码相符 |
| `TODO.md` | 文档 | 核对真机验收仍标待执行；不将作者声明当作通过证据 |
| `docs/issue_list.md` | 文档/台账 | L1/N1/F1′/N2 已有承接；本结果的 P2-1 须补入库 |
| `docs/reviews/session-batch-review-result-bdb75bf..7bab081-ds4.md` | 文档归档 | 范围内的既有评审结果，无运行时行为 |
| `docs/reviews/session-batch-review-result-leveltest-family-ds4.md` | 文档归档 | 同上 |
| `docs/reviews/session-batch-review-result-newdict-d5755ae-ds4.md` | 文档归档 | 同上；本轮核销 N1/N2 时作为前置证据 |
| `examples/pda2/fw_version.h` | 受评（低风险） | v1.20→v1.21 版本说明与代码相符 |
| `examples/pda2/penpal_api.cpp` | 受评 | exam 参数编码与 `/words/exams` 解析；未发现本轮独立阻断项 |
| `examples/pda2/penpal_api.h` | 受评 | 新 API/缓冲声明与调用方相符 |
| `examples/pda2/ui_deckpro.cpp` | 受评 | 菜单入口与 F1′ 的 stop 修复已核 |
| `examples/pda2/ui_leveltest.cpp` | 受评 | L1 的 HOME-only 置位及进入 QUIZ 清位已闭合 |
| `examples/pda2/ui_newdict.cpp` | 受评（重点） | P2-1 位于此；N1/N2 修复已闭合 |
| `examples/pda2/ui_newdict.h` | 受评（低风险） | 设备侧英文检索能力注记属实 |
| `examples/pda2/ui_whoami.cpp` | 受评（低风险） | 有界 `xQueueSend` 失败自释放已补齐 |

## Findings

### P2-1（应修｜绑 G-合入）：在飞请求的结果按“当前 Tab”归属，切 Tab 后 List 会卡在空考试页

- **位置**：[examples/pda2/ui_newdict.cpp:548](../../examples/pda2/ui_newdict.cpp#L548)-[582](../../examples/pda2/ui_newdict.cpp#L582)（`ND_REQ_LIST` 的结果分流）、[639](../../examples/pda2/ui_newdict.cpp#L639)-[652](../../examples/pda2/ui_newdict.cpp#L652)（切换 List 时仅在 `!s_nd_task` 才拉 exams）、[617](../../examples/pda2/ui_newdict.cpp#L617)-[629](../../examples/pda2/ui_newdict.cpp#L629)（空卡列表不能打开）。
- **证据 / SIM**：
  1. 在 Home 输入任意词并按 Enter，`nd_start(ND_REQ_LIST)` 发起 Home 的搜索请求；
  2. HTTP 请求在飞时触摸 List。`nd_tab_set()` 已写入 `s_tab = ND_TAB_LIST`，但因
     `s_nd_task` 非空，不会发起 `ND_REQ_EXAMS`；
  3. 原 Home 搜索返回。`nd_consume()` 不保存请求的发起上下文，只检查**此刻**
     `s_tab == ND_TAB_LIST`，于是把 `m->page` 写入 `s_exam_list`，却没有写入
     `s_exams` / `s_exam_count`；
  4. 当前画面仍是 `ND_LS_EXAMS`，显示 `Exam books (0 words in bank)`；Enter 进入
     `nd_open_exam()` 后因 `idx >= s_exam_count` 直接返回。只有再次触摸同一个 List
     Tab 或离场重进才会补拉考试卡，首次切换本身没有完成其宣称的行为。
- **影响**：正常网络延迟和正常触摸操作即可到达，List 功能在当前访问中不可用但可通过
  重点一次 Tab/重进恢复；`C/2/有声 → P2`。验证状态为 `PARTIALLY_VERIFIED`
  （CODE + 可确定时序 SIM；无设备）；异步请求快照本身存在，但结果没有携带/校验逻辑页
  上下文，故该状态机路径不符合页面结果归属目标。
- **最小修复**：优先让 `nd_req_t`/`nd_msg_t` 保存发起时的 Tab/子页（或单调 request id），
  消费时按该上下文更新对应缓存；当用户已在 List/exams 页时，随后调度 exams 请求而非把
  Home 页面数据误作 exam words。较保守的替代方案是在 `s_nd_task` 非空时拒绝 Tab 切换并
  给出 busy 状态。不得仅靠当前 `s_tab` 判定结果归属。
- **须补回归**：真机在慢网下执行“Home 搜索 → 立即点 List”，List 应自动显示考试卡；反向
  执行“List 词书请求 → 立即回 Home”，Home 亦不得显示考试词书数据。两条都检查重新进入、
  Enter 与 +/- 导航。

## G-评审退回项（申请元数据，不是设备代码缺陷）

- **位置**：[session-batch-review-request-7d914da..dd22ef5.md:10](session-batch-review-request-7d914da..dd22ef5.md#L10)-[37](session-batch-review-request-7d914da..dd22ef5.md#L37)。
- **证据**：`git rev-list --count 7d914da..dd22ef5` 计的是 `e794875` 与 `dd22ef5`，
  不包含 `7d914da`；申请却把结果解释为两项端点提交。按首末含两端应使用
  `git rev-list --count 7d914da^..dd22ef5`，实际为 3。申请中引用的 `e970142` 在当前
  仓库不存在；连续范围实际有上述 14 个文件，但表格省略了 3 个中间结果文档，并将多文件
  合并为一行，均不满足指南 §7.1 的逐文件归属要求。
- **所需动作**：仅修订申请：列出 `7d914da`、`e794875`、`dd22ef5`，用可解析的
  `7d914da^..dd22ef5` 重算自校，逐文件列 14 行，并补齐指南要求的靶向行区间、验证表、
  遗留/回滚/审批与开发者自评段落。此项不要求改动固件代码，但申请修正前不通过 G-评审。

## 已通过项

- **L1（LevelTest 缓冲 Enter）**：`ui_leveltest.cpp` 仅在 HOME 页置位，且
  `lt_render_question()` 进入 QUIZ 时清位。对“history 在飞时 Enter → 起步再按 Enter →
  首题返回 → 结束提交”的原反例推演，缓冲不会再存活到 RESULT；前轮 P2 闭合。
- **N1（NewDict 详情 CJK）**：`nd_render_detail()` 的 examples 与 tail 均改走
  `nd_set_text()`；该函数按内容选择 `Font_Hanzi_16`，双语中文半句不再走仅英文字库。
- **N2（NewDict 缓冲 Enter 跨页）**：详情成功渲染与 Tab 变更均清除
  `s_enter_pending`；前轮定义的跨 DETAIL/Tab 的补射路径已关闭。
- **F1′（扫描 timeout 判负）**：`wifi_scan_stop_and_release()` 的非 RUNNING 快径先调用
  `esp_wifi_scan_stop()` 再 `WiFi.scanDelete()`，覆盖 Arduino timeout 已清 SCANNING
  而驱动仍在飞的分支。真实驱动时序仍须按台账真机验证。
- **Nit-1**：Whoami、LevelTest、NewDict 的 2 秒有界队列发送均检查失败并释放消息，和
  `docs/async_ipc_contract.md` 规则 7 的获准变体一致。
- **考试卡翻页算术**：15 卡、行池 8 行时，焦点从 7 到 8 将 top 推至 1，直至焦点 14 时
  top 为 7；反向焦点越过 top 即贴齐。没有越界或漏卡的静态反例。
- **EPD 激活态**：当前实现为黑底白字与透明黑字的二值对照，不再使用会被二值化为白色的
  `LV_OPA_20` 灰底；肉眼可辨性仍属 HW 验收。

## 验证说明

- `git diff --check 7d914da^ dd22ef5`：通过。
- 评审环境没有 `pio`、`platformio` 或 Python，因此无法独立重跑申请所称的
  `pio run -e pda2`；申请方的 BUILD 声明保留为 `CLAIM_ONLY`。
- 无 COM5/真机，触摸、EPD、Wi-Fi 驱动实际时序及 API 冒烟均未外推为通过；上述硬件项为
  `UNKNOWN` 或 `PARTIALLY_VERIFIED`（已明确其静态前提）。

## 对称三格

1. **全区间 diff**：已按 `7d914da^..dd22ef5` 核对全部 14 个文件，不只采信申请的靶向说明。
2. **独立复跑**：完成 `git diff --check` 与 CODE/SIM 时序复核；构建和真机因环境缺失未独立复跑。
3. **最没把握点反例**：L1、N2、考试卡边界、Tab 可见性、`q`/`exam` 独立参数及菜单布局均已
   走查；其中切换 Tab 与在飞请求的组合构造出 P2-1。

## 审批意见

- [ ] A 全量接受
- [ ] B 退回修订
- [x] C 部分接受

接受本轮 L1/N1/F1′/N2 及 Nit 闭环；P2-1 修复并登记 `docs/issue_list.md`、补足其 HW 回归后，
再过 G-合入。申请元数据按 G-评审退回项最小更正后方可作为正式归档申请。
