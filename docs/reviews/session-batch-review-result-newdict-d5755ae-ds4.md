# 评审结果：WordBank "new_dict" 词库 APP（v1.19，单提交 d5755ae，ds4）

- **评审日期**：2026-09-18
- **申请文件**：`docs/reviews/session-batch-review-request-newdict-d5755ae.md`
- **评审依据**：`docs/review_guide.md` v1.3（代码口径 §2.3 A/B/C）
- **评审提交**：`d5755ae`（v1.19，10 文件 / 835 insertions）。关联文档提交 `355e3dc`
  （`docs/async_ipc_contract.md` 的 WordBank/LevelTest 行）按"不评代码面"处理，
  但其**断言与代码逐条对齐核过**（见 Nit-1）。
- **评审基线**：`git rev-parse HEAD` = `bb28ce2`；`d5755ae` 之后**无代码 commit**（仅
  `355e3dc`/`5d1dbfb`/`bb28ce2` 三个 docs），提交面无漂移。**注意**：评审时工作树**非干净**——
  `examples/pda2/ui_newdict.cpp` 等 5 个文件有未提交的 v1.20 在改内容（NewDict 搜索优先 +
  Home/List 标签页 + exam books）。本结果**一律以 `git show d5755ae:<path>` 为准**，
  不采信工作树；v1.20 请另开申请。
- **结论**：**C 部分接受**——异步契约、解析层缓冲、菜单/轮询接线均成立；详情页有 1 处
  CJK 字体漏切（N1，P2/应修）与 2 处键缓冲/重入边界（N2、N3，P3/应修）。

## Findings

### P2（应修｜绑 G-合入）：DETAIL 页的 examples / tail 两个标签漏了 CJK 字体切换——中文例释与词块全空白

- **位置**：`examples/pda2/ui_newdict.cpp:348`（`lv_label_set_text(s_d_ex_lab, ex)`）、
  `:359`（`lv_label_set_text(s_d_tail_lab, tail)`）；
  对照组（**已正确**使用 `nd_set_text`）：`:332`（head）、`:341`（senses）；
  文本来源：`examples/pda2/penpal_api.cpp:764-767`（examples 拼成 `"%s / %s"` = en / zh）、
  `:780-783`（chunks 拼成 `"%s (%s)"` = text (zh)）、`:788-802`（assoc）
- **证据**：
  1. 解析层**必然**注入中文：本地服务实测 `GET /words/281/detail` 返回
     `examples[0] = {"en":"She suffered appalling injuries.","zh":"她伤势非常严重"}`，
     `chunks` 为 `{"text":..., "zh":...}` 数组 ⇒ 拼装后的行内含 CJK。
  2. 字体能力：`Font_Mono_Bold_15` 无 CJK 字形——这正是 v1.16 真机报告的症状
     （"词汇题选项中文渲染为空白"，见 `docs/issue_list.md` §23/§24 与 v1.16 申请描述）。
  3. 三个细节页标签里**两个切了两个没切**，而文件头 `:25-27` 与提交说明明确声明
     "Chinese (meaning_zh, example zh, chunk zh) switches the label to Font_Hanzi_16
     per content" ⇒ 实现与自述不符（漏切，非设计）。
- **影响**：详情页是 APP 的主产出；每个带双语例句/词块的词条都会丢失中文半句
  （用户看到英文后的 ` / ` 空白）。`C/1/有声 → P2`（症状可见，非静默；可达性 1 = 正常打开详情）。
- **两轴**：`VERIFIED`（CODE 事实 + 服务端实测 JSON 形状；渲染后果按 v1.16 既有真机结论外推，
  本机无 EPD）｜`VIOLATES`（`CLAUDE.md` 工作笔记"`LV_COLOR_DEPTH=1` + 字体无 CJK 必空白"的
  既有约定 / v1.16 修复先例）
- **最小修复**：`:348`/`:359` 改 `nd_set_text(lab, ...)`（两行）。
  **须补回归（HW）**：打开任一双语例句词条，中英两段都必须出字。
  **提醒**：v1.20 在改文件里这两处**仍是** `lv_label_set_text`（工作树 `:503`/`:514`），
  合入 v1.20 时一并修。

### P3（应修｜绑 G-真机）：缓冲 Enter 没有页面绑定——从 DETAIL 退回 LIST 时被"补射"，把用户弹回刚离开的详情

- **位置**：`ui_newdict.cpp:443-449`（消费点，仅 `s_nd_page == ND_PAGE_LIST`）、
  `:483-486`（置位点，仅 LIST 页生效）、`:406` 与 `:457`（DETAIL → `nd_render_list()`）
- **反例序列**：
  1. LIST 页按 Enter 打开焦点行 ⇒ `nd_open_detail()`（`:418-425`）发起 DETAIL 请求，
     **页面仍是 LIST**（`:394-400` 才切页）；
  2. 请求在飞期间再按一次 Enter ⇒ `:483-486` 置 `s_enter_pending = true`（"不吞键"的既定行为）；
  3. 详情返回 ⇒ `nd_render_detail()`（`:410`）切到 DETAIL；消费点判据 `s_nd_page == ND_PAGE_LIST`
     为假 ⇒ 既不消费也不清除；
  4. 用户按 Back/Enter 回列表 ⇒ `nd_render_list()`（`:457`/`:477`）切回 LIST ⇒
     **下一 tick** 消费点成立 ⇒ `nd_open_detail(s_focus)` 又打开同一个详情（自限一次）。
- **影响**：退一次退不掉（需再退一次），无数据后果、无卡死。多前提（需"在飞时按 Enter" +
  "那次请求恰是 DETAIL"）⇒ 可达性 3；`C/3/有声 → P3`。
- **两轴**：`PARTIALLY_VERIFIED`（CODE + 时序推导）｜`VIOLATES`（v1.18 引入的"键缓冲"语义
  未绑定页面，跨页存活——与 LevelTest 同源缺陷，见 `session-batch-review-result-leveltest-family-ds4.md` L1）
- **最小修复**：同 L1——置位时记下当时页面（或把置位限定在"能就地消费的页面"），
  并在 `nd_render_detail()` 里 `s_enter_pending = false;` 兜底。
  **须补回归**：SIM/HW——打开详情 → 在飞时按 Enter → 返回列表，应停在列表。

### P3（应修｜绑 G-真机）：entry 时上一场请求仍在下飞 ⇒ 列表永远不自动拉取（申请自评第 6 条，确认成立）

- **位置**：`ui_newdict.cpp:527-538`（`entry_nd`）、`:530`（gen++）、`:532`（`if (s_list.count == 0 && !s_nd_task)`）
- **反例序列**：首次进屏（`s_list.count == 0`）→ `nd_fetch_list(0)` 发起请求 → 立刻 Back
  （`exit_nd`，gen++）→ 立刻再进（`entry_nd`，gen++；此时 `s_nd_task` 仍非空）⇒
  走 `else` 分支只 `nd_render_list()`；随后旧请求返回，因 gen 不符被 `nd_consume()` 丢弃（`:380`）
  ⇒ **无任何自动重拉**，屏上停在 `Words 0 / 0`，须用户按键才出数据。
- **影响**：极端时序下进入一个"空白词库"（状态行也无提示，`s_status` 仍是上次的值）。
  可达性 2（需"进屏后立刻退出再立刻进入"且首次请求未回）；`C/2/有声 → P2`？——
  本条的可见症状是"空列表 + 需按键"，**无数据后果、一键即恢复**，且申请已自评在案，
  按 §0 原则 7 归类为**待补边界**而非新缺陷 ⇒ 下调为 **P3**（坐标记 `C/2/有声 → P2 → 按"一键可恢复"降为 P3`，
  留痕供复核）。
- **两轴**：`PARTIALLY_VERIFIED`｜`NO_CONTRACT`（属契约 §2.8 页面生命周期的边缘情形，建议在
  契约表 WordBank 行补一句"entry 时若有在飞请求：不重拉，依赖用户按键"或按下面修掉）
- **最小修复**：`entry_nd` 的 `else` 分支里补 `if (s_nd_task) s_enter_pending = true;`
  ——任务转空后由现有消费路径（`:445`：`s_query_dirty || s_list.count == 0` ⇒
  `nd_fetch_list(0)`）自动补拉。

## Nit（≤3）

- **Nit-1（契约措辞与实现不一致，本批引入）**：`docs/async_ipc_contract.md` §2 规则 7 的
  已批准变体写"有界发送、**超时自 `delete` 结果**"，实现只做有界发送、不检返回值也不释放
  （`ui_newdict.cpp:173-177`，同款见 `ui_leveltest.cpp:223-227`、源头 `ui_whoami.cpp:292-296`）。
  按"单飞 + 深度 4 + UI 不积压"队列实际不可能满 ⇒ 后果不可达，不记为缺陷；但该行是 `355e3dc`
  本批登记的，文本描述了一个不存在的分支。二选一：补 `if (xQueueSend(...) != pdTRUE) delete m;`，
  或改措辞。
- **Nit-2（能力声明 > 设备能力）**：申请/提交说明强调 `q` "同时模糊匹配 word 与 meaning_zh …
  中文子串命中（q=苹果→apple）"。该能力**API 侧成立**（我实测 `q=%E8%8B%B9%E6%9E%9C` → total=1，
  `apple A1 苹果, 家伙; [医] 苹果`），但**设备侧不可达**：查询输入只放行
  `[a-zA-Z0-9 ']`（`:511-512`），键盘无法输入汉字。建议在 `ui_newdict.h` 头注释里写明
  "设备侧只能按英文前缀/子串检索"，避免后续误判为缺陷。
- **Nit-3（在飞期间的按键策略不一致）**：同一段代码里，在飞时字母/数字被**静默丢弃**
  （`:511` 前置 `!s_nd_task`），而退格**不检查** `s_nd_task`（`:455`，会改 `s_query`
  并重绘列表）。后果：请求返回后 `s_query_dirty` 被置假（`:394`），用户以为自己改过查询，
  下一次 Enter 却走"打开焦点行"（`:487-490`）而不是搜索。建议统一为"在飞时查询编辑整体
  冻结并提示，或与 Enter 一样缓冲"（与 N2 同一处收敛更好）。

## 已核实成立（含申请自评 7 条逐条答复）

1. **异步契约逐条符合**（对照 `docs/async_ipc_contract.md` §2 十一条 + §1 表 WordBank 行）：
   队列一次创建永不删除（`:168`，注释解释了 whoami 那次 `xGenericSend` 断言的教训）✓；
   busy = 任务句柄且任务自清（`:179-180`）✓；结果第一字段 `gen`（`:125`）✓；
   `s_nd_gen` entry/exit 各 +1（`:530`、`:543`）✓；请求快照独占 + 任务自删（`:209-226`、`:178`）✓；
   队列先建后启动（`:203-207`）✓；栈 1024×8 / 优先级 1（`:228`）✓；无 LVGL 跨线程调用 ✓。
   `s_nd_q` 的"永不删除"是对既有事故的**正确**继承（契约 §1 表已登记该变体）。
2. **Enter 语义重载**：LIST 页"查询脏或列表空 → 搜索，否则 → 打开焦点行"（`:483-491`）
   自洽；**用户敲字后直接按 +/- 翻页**的场景我按申请的问法推演过：`nd_fetch_list(skip)`
   带**新** `s_query` + **旧** `skip`（`:498-499`/`:504-505`），服务端返回的是"新查询结果集里
   第 skip 条起的一页"——页头 `Words skip+1-skip+count / total` 自洽，数据**不串**，
   只是"从中间开始看"；随后 `s_query_dirty` 被置假（`:394`）。**结论：不记缺陷**
   （属可用性观感，可接受；若想更顺手，可在 `nd_fetch_list` 里对脏查询强制 `skip=0`）。
3. **`nd_render_detail` 栈缓冲无差一**：逐项复算最坏情况——
   `sen[6*84+1=505]`（每行 ≤ 79 字 + `\n` + NUL = 81，循环准入 `off < 505-84` ⇒ 进入时 ≤420，
   写 81 ⇒ ≤501 < 505）✓；`ex[2*164+1=329]`（每行 ≤ 2+159+1+1=163 ≤ 164）✓；
   `tail[2*84+100=268]`（每行 ≤ 2+79+1+1=83 ≤ 84；尾随 assoc ≤ 96+NUL 用剩余空间，
   `snprintf` 截断安全）✓。**无溢出，无差一。**
4. **assoc 的 `started` 标志正确**：`:793-802` 首词 `": "`、后续 `", "`；用 `started`
   而非旧版 `off > 2`（`"[type]"` 已使 off=5 恒真）的修法是对的 ✓；
   截断保护 `off >= sizeof-2` 先于写入 ✓。
5. **数据形状 vs 设备缓冲：缓冲安全，但申请的数值陈述已过期**。
   当前 `dev.db`（23016 词）实测：`word` 最长 **20 B**（申请写 16 B；`word[28]` 仍安全）、
   `meaning_zh` 最长 **150 B**（申请写 92 B；`meaning_zh[56]` 靠 `s_copy_disp` 的
   UTF-8 边界截断 + `"..."` 兜底，属显示截断，安全）、`phonetic` ≤ 58 B（`phonetic[24]`，
   同样是显示截断）。**所有设备缓冲都由解析层的 per-field 上限兜底，无溢出路径**；
   仅"实库最长 92B"这一陈述与当前库不符（台账/申请陈述过期，非缺陷）。
   nullable 路径：`:742` 的 `zh` 兜 `en`、`:762` 的 `en` 为空则 `continue` ✓、
   `s_lt_str` 返回 NULL 时 `s_copy*` 走空串 ✓。
6. **entry 自动拉取**：见 N3（确认成立并给出最小修复）。
7. **退格语义**：DETAIL 退格返回列表（`:456-458`）、LIST 查询空时退格退出 APP（`:467-471`），
   与本地 Dict 屏（`ui_dictionary.cpp:78-85`"输入框空才退出"）一致 ✓；`keypad_clear_chars()`
   在退出前调用 ✓。
8. **菜单/接线**：`SCREEN_NEWDICT_ID` 追加在 `SCREEN2_2_ID` **之前**（未破坏 OTA 的
   "追加在末尾"约定，`ui_deckpro.h:81-86`）；第 2 屏 8 个入口仍在 9 格布局内（23/95/167 × 13/101/189）；
   `factory.ino:935-936` 挂 poll ✓；`scr_mgr_register` ✓。
9. **API 契约实测复现**（本地开发服务器，键 `89rg35eua2`）：
   `GET /words?limit=1&skip=0` → `total=6448` ✓（与申请一致）；
   `q=app` → appalling / apparent… ✓；`q=%E8%8B%B9%E6%9E%9C` → `apple A1`（中文子串）✓；
   `GET /words/281/detail` → 字段形状与解析层假设**逐项相符**（`senses` = 组数组，
   组内 `senses[{en,zh}]`；`examples[{en,zh}]`；`chunks[{text,zh}]`；`assoc_groups[{type,words[]}]`）✓；
   **无 key → 401** ✓（生产服务器 401 归因"`/words` 先于 key 通道部署"与本地代码的
   `Depends(get_current_user_learn)` 一致，属环境前提而非代码缺陷）。

## 验证说明

- **评审方环境**：装有 PlatformIO；本地开发服务器（`127.0.0.1:8000`）在跑、可直连；
  **无设备**（COM5 不可达）。
- **独立复跑（BUILD）**：`git worktree add <tmp> bb28ce2`（含 `d5755ae` 的代码超集）
  + `python -m platformio run -e pda2` → **SUCCESS 44.70 s**，
  `RAM 57.4% (188140/327680)`、`Flash 44.2% (2896249/6553600)`。
- **独立复跑（SIM/API）**：见"已核实成立 9"（GREEN 的实测 200/401/分页/中文子串/详情形状）。
  这部分是**开发机可复现**的，已独立重跑；**真机交互（搜索/翻页/详情/触摸）标 UNKNOWN**，
  与申请 §验证边界一致（生产 401 + 待切 base 到本地开发机）。
- **未核**：`ui_newdict.h` 声明与 `ui_deckpro.h` 的枚举值对应关系我核到"注册/菜单/枚举"三处
  一致，未做二进制扫描核对；工作树的 v1.20 内容**不在本轮范围**。

## 对称三格

- **① 是否核了全区间 diff**：是。`d5755ae` 的 10 个文件全部归属：
  申请点名的 8 个（`ui_newdict.*`、`penpal_api.*`、`ui_deckpro.*`、`factory.ino`、`fw_version.h`）
  受评；`CHANGELOG.md`/`TODO.md` 按"不评代码面"处理但陈述已对照核（如 6448/401 归因）；
  区间外的 `docs/async_ipc_contract.md` 属 `355e3dc`，仅核其对本模块的断言（Nit-1）。
- **② 快照是否独立复跑**：是——BUILD 独立复跑（干净 worktree @ `bb28ce2`）+ 本地 API 实测；
  **真机面 UNKNOWN**，不以编译/接口通过外推交互行为。
- **③ 申请"最没把握"7 条是否逐一构造反例**：是。第 1/3/4/5/7 条核到"成立"；
  第 6 条**构造出反例**（N3）；第 2 条（Enter 重载）按申请原问法推演为"可接受、不记缺陷"，
  但**在其旁边发现了另一个真反例**（N2：缓冲跨页存活）。

## §9 评审自审清单（承重档）

```text
【异步/契约】1 非 UI 线程调 LVGL？无 ✓；2 busy 只被 gen 匹配结果释放？任务是句柄自清 +
  gen 门控 ✓，**但键缓冲标志不在 gen 门控内** ⇒ N2；3 worker new 的结果 UI 是否 delete？
  `nd_consume` 全分支到 `:413 delete m` ✓，快照任务自删 ✓；4 队列先建后判空 ✓；
  5 任务持副本（base/key/q/exam/id 全为值/std::string）✓；
  6 迟到三路径：entry/exit 各 ++gen ✓，首字段 gen ✓。
【内存/栈】7 无 memset 非平凡结构（`*out = pp_wb_page_t{}` 值初始化）✓；
  8 无 >400B 聚合赋值：**`s_detail = m->detail`（`:408`）**——pp_wb_detail_t ≈ 28+24+8+64+4+6*80+4+2*160+4+2*80+96
  ≈ 1.1 KB，是"结构体赋值"而非临时聚合，编译为 memcpy 到静态对象，**不产生栈上临时**（对照
  penpal 那条 15 KB 聚合教训：`= T()` 才是栈临时）✓；9 无 lv_conf 改动 ✓。
【秘密】10/11 不涉 key 缓冲/输入框存储；key 由 `penpal_load_config` 供给（本屏不回显）✓。
【硬件/实时】12 变体无关；13 EPD：列表/详情一次全刷，无光标闪烁（本版无 textarea；
  v1.20 引入的 textarea 已按 `anim_time=0`+`bg_opa=TRANSP` 处理——**不在本轮范围**）✓；
  14 原子写不涉 ✓。
【方法论】15 三条发现均给 file:line + 反例序列 ✓；16 作者"编译 SUCCESS/刷写 VERIFIED/API 冒烟"
  按 CLAIM_ONLY 处理，BUILD 与 API 冒烟我独立重跑 ✓；17 复跑作用域 = 干净 worktree @ bb28ce2，
  并显式声明**工作树有未提交 v1.20、不予采信** ✓；18 `newdict_keyboard_poll` 有生产调用方
  （`factory.ino:935-936`）✓；19 反例库：在飞请求 + 离页重入 + 键缓冲跨页 + 渲染字体缺失
  逐项推演 ✓；20 与申请结论相反处（第 6 条）以时序复现表述，不靠票数 ✓。
```

## 审批意见：[ ] A 全量接受  [ ] B 退回修订  [x] C 部分接受

- **接受**：`d5755ae` 的异步契约实现、解析层缓冲与字段映射、菜单/枚举/轮询接线、
  本地 API 契约（分页/中文子串/详情形状/401 语义）全部成立；
  申请人自评的第 1/3/4/5/7 条经复核无问题。
- **保留**：**N1（P2，应修）**绑 **G-合入**（详情页中文不可丢）；
  **N2、N3（P3，应修）**绑 **G-真机**（键缓冲跨页 / entry 在飞不重拉）。
- **入库动作（作者）**：N1–N3 登记 `docs/issue_list.md`（N1 绑 G-合入，N2/N3 绑 G-真机）；
  Nit-1 与 `docs/async_ipc_contract.md` 规则 7 的措辞对齐（与 LevelTest 结果同一条，
  一次改两处）；Nit-2 写进 `ui_newdict.h` 头注释。
  另：**工作树的 v1.20 改动需另开申请**，其中 `:503`/`:514` 仍带 N1 的同款漏切，
  合入时一并修。
