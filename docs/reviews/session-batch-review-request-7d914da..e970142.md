# 评审申请：NewDict v1.20 重做 + ds4 三段评审处置轮（7d914da..e970142）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-09-18
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）；含修复轮 delta 复审（§2.5）
- **关联 commit**（2 个，区间 `7d914da..e970142`，**首不含**）：
  - `7d914da` — **v1.20**：NewDict 搜索优先 + Home/List 双 Tab + 考试选卡
    （用户三条指令：入口挪第一屏；首页搜索框；List 列考试名 → 考试词书。
    新 API：`GET /words/exams` + `GET /words?exam=`，范例步骤 ⑫b）
  - `e970142` — **v1.21**：ds4 三段评审处置（L1/N1 两 P2 + F1′/N2 + 3×Nit）
- **背景**：三份前置申请已出结果（均 C 部分接受，ds4）——本申请按其
  "入库动作"指示另开：①v1.20 在 newdict 结果里被明确要求"另开申请"
  （评审时工作树的 v1.20 内容未被采信，其中 `:503/:514` 预判的 N1 同款
  漏切已在本区间 `e970142` 修复）；②`e970142` 是三份结果的处置轮。
  **本申请同时覆盖 v1.20 功能面与 v1.21 修复核销。**
- **硬件**：机 `28:37:2f:91:2c:20`（COM5）**待刷 v1.21**（v1.20 刷写时
  设备深睡安全中止，零写入；唤醒后补刷）。

## 头部自校

- `git rev-list --count 7d914da..e970142` = **2**（与上方列表一致）
- `git diff --name-only 7d914da e970142`：v1.20 = 7 文件 494+/175-；
  v1.21 = 10 文件 109+/15-
- `git diff --check 7d914da e970142`：通过

### 区间文件归属表

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/ui_newdict.cpp` / `.h` | **评（重点）** | v1.20 全部交互重做（Tab/搜索框/考试卡/词书/共享行池）+ N1/N2 修复 |
| `examples/pda2/penpal_api.h` / `.cpp` | **评（重点）** | `penpal_wb_exams()` 新增 + `penpal_wb_list()` 加 `exam` 参数；`stem[128]` 前提注记 |
| `examples/pda2/ui_deckpro.cpp` | **评** | v1.20 菜单 NewDict 挪第一屏；v1.21 F1′ 快径补 stop |
| `examples/pda2/ui_leveltest.cpp` | **评** | L1：Enter 缓冲收紧 HOME + `lt_render_question()` 清位 |
| `examples/pda2/ui_whoami.cpp` | 评（低风险） | Nit-1：`xQueueSend != pdTRUE` 自释放（两行） |
| `examples/pda2/fw_version.h` | 评（低风险） | v1.20/v1.21 版本链 |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` | 不评（纯文档） | v1.20/v1.21 台账 + §31 处置表 |

## §1 v1.20 变更明细（NewDict 重做）

- **页面结构**：共享行池（8 行）+ 两顶层页面（TABS/DETAIL）+ 两 Tab
  （Home/List，触摸切换，激活态浅灰底）。
- **Home tab**：单行搜索框（`lv_textarea`，**EPD 纪律 v1.12**：CURSOR part
  `bg_opa=TRANSP` + `anim_time=0`——无闪烁无插入符，输入即回显）；
  Enter = 查询脏或无结果 → 搜索，否则开焦点行；+/- 焦点/翻页。
- **List tab**：`GET /words/exams` → 15 张考试卡（"IELTS  5700" 降序，
  本地翻页 `s_exam_top`）→ 开卡 = `GET /words?exam=` 词书（服务端 skip
  翻页）；退格逐级返回（词书→考试卡→退出）；任意字母输入自动跳回 Home。
- **penpal_api**：`pp_wb_exam_t{exam[20], count}` + `PP_WB_EXAM_MAX 16`；
  exam 与 q 同走 `s_urlenc`（中文考试名"中考"→ percent-encoded）。

## §2 v1.21 修复核销（对照三份结果的"保留"项）

| 发现 | 最小修复（ds4 给出） | 本区间实现 |
|---|---|---|
| L1 (P2) | 置位收紧 HOME + `lt_render_question()` 清位 | 按此实现（两处，含注释） |
| N1 (P2) | `:348/:359` 改 `nd_set_text` | v1.20 同位置改（ds4 预判 `:503/:514`） |
| F1′ (P3) | 快径补 `esp_wifi_scan_stop()` + 注释收窄 | 按此实现 |
| N2 (P3) | 置位绑定页面 + `nd_render_detail()` 清位兜底 | 清位加在 `nd_render_detail` + `nd_tab_set` |
| N3 (P3) | entry 在飞时 `s_enter_pending = true` 补拉 | **不适用**：v1.20 起entry 不再自动拉取（结构性消除） |
| Nit-1 | 二选一（补实现/改措辞） | 补实现：三处 `!= pdTRUE` 自释放 |
| Nit-2 ×2 | 头注释 | ui_newdict.h 能力注记 / penpal_api.h 前提注记 |

## 自评最没把握的点（请重点击穿）

1. **L1 修复的完备性**：置位 HOME-only 后，v1.18 的原始场景（首次进入
   history 在飞时按 Enter，页面 HOME）仍覆盖 ✓；但"HOME 页 Enter 缓冲 →
   首题渲染（清位）"与"HISTORY 返回（HOME 渲染）→ 同 tick 消费点发箭"
   的先后是否还有交错窗口？
2. **N2 的清位位置**：`nd_render_detail` + `nd_tab_set` 之外，行池触摸
   （`nd_row_cb` → `nd_open_detail`）发起 DETAIL 期间再按 Enter 的置位
   （此时页面仍 TABS、tab 任意）——返回 DETAIL 渲染时被清 ✓；但若该次
   DETAIL **失败**（`nd_render_tabs()` 回列表），缓冲仍在——会在下一 tick
   补射一次"开焦点行"。可接受（等价用户重按一次）还是该连失败分支一起清？
3. **考试卡本地翻页算术**：`s_exam_focus` 越过 `s_exam_top + 8` 时
   `s_exam_top = s_exam_focus - 8 + 1`；反向 `< s_exam_top` 时贴齐——
   15 卡两页（8+7），边界推演请复核。
4. **Tab 激活态**：写申请时自查发现 v1.20 初版用 `LV_OPA_20` 灰底——按
   issue_list §23 阈值（灰≥128=白）混出 ~204 **不可见**；本提交已改
   黑底白字反色（二值安全）。请复核现实现 + 真机确认激活 Tab 肉眼可辨。
5. **exam 参数与 q 叠加**：设备从不同时传（Home 传 q、List 传 exam），
   但 `penpal_wb_list` 允许同时传——服务端语义为 AND 过滤，无冲突。
6. **v1.20 菜单第 1 屏现为 7 入口**（6/7/8 → 7/7/8），布局坐标复用空位
   (23,189)，无溢出。

## 验证边界（诚实声明）

- 编译：v1.20、v1.21 各自 `pio run -e pda2` SUCCESS。
- 刷写：**v1.20/v1.21 均未上机**（设备深睡，COM5 断，待唤醒补刷）。
- API 冒烟（本地开发服务器，已重启加载"九十九"代码）：`/words/exams`
  15 卡降序（IELTS 5700 → 中考 1903）、`?exam=中考` → 1903 词、
  items 带 exam_tags 回显——全 200。
- L1/N1/N2/F1′ 的修复效果：静态实现 + 逻辑推演（SIM 级），真机回归
  待刷机后按 §31 清单执行——**全部 CLAIM_ONLY**。
