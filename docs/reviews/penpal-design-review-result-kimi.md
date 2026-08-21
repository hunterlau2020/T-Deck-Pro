# 笔友（PenPal）App 设计文档评审结果（kimi，第二轮独立复核）

- **评审日期**：2026-08-21
- **设计稿**：[penpal-design.md](../penpal-design.md)（v1 草案）
- **评审范围**：v1 全文（§1–§9），并对照前次评审
  [penpal-design-review-result.md](penpal-design-review-result.md) 的结论逐项复核
- **评审方式**：设计稿全部外部引用逐条对照仓库现状核实（异步契约、http_utils、
  ui_deckpro 菜单代码、ui_ai_chat 模式、env_secrets、SECURITY.md、
  remote_api_demo.py、lv_conf.h），非纯静态阅读
- **评审结论**：**C 部分接受**——架构与交互设计整体可实施，但有 **2 处会导致实际
  缺陷的正确性错误**（§6 菜单 page_num 公式、§2 LLM 超时）和 **1 处契约违背**
  （§3.2 缺 `s_pp_busy_gen`），须在 commit 1 落地前修订设计稿。

---

## 1. Findings

### 1.1 §6 `page_num` 改为向上取整与门控语义矛盾——off-by-one，且现有代码已潜伏

- **严重性**：High
- **位置**：`docs/penpal-design.md:349`（§6 集成点 ②）
- **触发场景**：菜单第 19 项落地后，在第 3 页继续向左滑。
- **证据**：
  - 设计稿：`page_num = MENU_BTN_NUM/9` 改为 `(MENU_BTN_NUM+8)/9`，19 项时 = 3。
  - 实际代码：`menu_get_gesture_dir` 的门是 `if(page_curr < page_num) page_curr++`
    （`examples/pda2/ui_deckpro.cpp:287`）——`page_num` 被当作**最大页下标**使用。
    改为 3 后 `page_curr` 可达 3，而第 4 页不存在：无 screen 可切、无页点可染
    （现有分支只处理 `page_curr==0/1`，:303-314）。
  - 该 off-by-one **现存代码已潜伏**：18 项时 `18/9=2`，第 2 页再左滑即把
    `page_curr` 推到 2，命中无分支（界面不更新但内部状态已越界）。
  - 设计稿未提及的两处连带改动：
    - 按钮创建循环硬编码两页二分 `if(i < 9) menu_screen1 else menu_screen2`
      （`ui_deckpro.cpp:453-458`），加第 3 页必须改为 `i/9` 分派；
    - `ui_Panel4` 页点按 child 下标寻址（:306-307），第 3 个页点需新建并定位。
  - 前次评审跟踪项 T6 接受了 `(MENU_BTN_NUM+8)/9` 公式——本评审以门控代码为证，
    该公式不可按原样实施。
- **影响**：菜单手势越界到不存在的页；按现公式实现必现。
- **最小修复**：`page_num = (MENU_BTN_NUM-1)/9`（保持"最大下标"语义），或改为页数
  语义并同步把门改成 `page_curr < page_num - 1`；§6 集成表补登按钮创建循环与
  第 3 页点两处改动；顺手修掉现存的 18 项越界。

### 1.2 §2 LLM 端点 120s 超时与设计稿自身证据（demo 180s）矛盾

- **严重性**：High
- **位置**：`docs/penpal-design.md:25`（§2 超时策略）
- **触发场景**：correction/polish/tips 在服务端耗时 120–180s 时（长信 + 慢模型）。
- **证据**：
  - 设计稿原文："demo 脚本 timeout 180s → 客户端这三类用 **120s** 超时"。
  - `scripts/remote_api_demo.py:47` 实测 `httpx.Client(..., timeout=180)`——180s
    正是服务端耗时的上界证据。客户端 120s 会把最慢（往往也是最长的信）的响应
    拦腰截断，推断方向反了。
- **影响**：长信的纠错/润色稳定超时失败，用户感知为功能不可用。
- **最小修复**：客户端 LLM 超时改为 ≥180s（建议 180s + 余量），或补一句论证为何
  服务端实际不会超 120s。waitbox Close 取消（§3.2）已兜底体验，超时宁可偏长。

### 1.3 §3.2 缺 `s_pp_busy_gen`——违背异步契约规则 3，且 waitbox 取消场景正需要它

- **严重性**：High
- **位置**：`docs/penpal-design.md:117-126`（§3.2）
- **触发场景**：waitbox Close 取消（busy=false）后旧任务仍在飞，此时用户发起新
  请求；旧任务结果随后到达。
- **证据**：
  - 契约规则 3（`docs/async_ipc_contract.md:25-26`）：busy 必须携带自己的代次
    （`*_busy_gen`），仅代次匹配的结果才允许释放 busy——防止迟到结果释放更新
    请求的 busy。先例：`s_wifi_test_busy_gen`（`ui_deckpro.cpp:1479`，仅在 gen
    匹配时释放 :1514,1547）。
  - §3.2 只声明了 `s_pp_busy` + `s_pp_gen`，无 `s_pp_busy_gen`。
  - §3.2 的 waitbox Close=取消（gen++、busy=false、任务不杀）恰恰是规则 3 针对
    的竞态：取消后 busy 立即释放、旧任务仍在跑，没有 busy_gen 无法区分新旧结果
    的释放权。
- **影响**：取消→重发→旧结果到达的时序下 busy 状态错乱，后续请求被误拒或误放。
- **最小修复**：§3.2 增加 `s_pp_busy_gen`，并写明"结果仅在 `res->gen ==
  s_pp_busy_gen` 时释放 busy"的释放规则。

### 1.4 单队列复用 8 类请求，结果结构需显式类型字段

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:117-127`（§3.2）
- **证据**：
  - 契约表格（`async_ipc_contract.md:13-18`）先例为**每任务一队列**
    （`s_wifi_test_q` / `s_chat_q`…）；§3.2 一个 `s_pp_q` 复用
    `PALS/MAILBOX/THREAD/SEND/TOPICS/FIX/POLISH/TIPS` 8 类，属先例之外的合理
    偏差，但设计稿未声明结果结构体的 `type` 字段，消费端无法分派。
- **影响**：实现者自行发明分派方式，8 类结果结构体的内存释放路径易错。
- **最小修复**：§3.2/§5 写明结果结构体第二字段为 `type`（枚举），UI 消费端按
  type 分派 + delete；并在 §3.4 式的偏差说明里登记"单队列 vs 契约每任务一队列"。

### 1.5 env.cfg 8 槽上限："够用"成立但零余量，溢出静默丢弃；95 字符值上限与 base 96 不符

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:151`（§3.4"8 槽够用"）
- **证据**：
  - `#define ENV_MAX_ENTRIES 8`（`examples/pda2/env_secrets.cpp:21`），解析到 8
    条即停（:48），**第 9 条起静默丢弃，无任何告警**。
  - 现有有意义键已达 7 个：`OPENROUTER_KEY`、`OWM_KEY`、`WEATHER_COORDS` +
    AI provider 表 `DEEPSEEK_KEY/MINIMAX_KEY/QWEN_KEY/TENCENT_KEY`
    （`ui_ai_cfg.cpp:97-108`）。加 `PENPAL_BASE/PENPAL_KEY` 理论上限 9 > 8。
  - 值上限 95 字符（`env_entry_t.val[96]`，:24-26）vs §4.7 base textarea ≤96 字符
    ——96 字符的 base 无法经 env.cfg 完整往返（静默截断）。
- **影响**：同时配齐 AI 多 provider + penpal 的设备，env.cfg 末条静默失效，排查
  困难（当前本机 env.cfg 仅 3 键，不触发）。
- **最小修复**：§3.4 加一句容量说明（"理论 9 键超 8 槽上限，provider 键与 penpal
  键同配时 env.cfg 末条静默丢弃；NVS 优先故实际影响有限"）；base textarea 限长
  改为 ≤95 或注明 env.cfg 路径的截断。

### 1.6 数据模型三处与 §2.1 实测 schema 脱节

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:318-325`（§5）
- **证据**：
  1. `pp_thread_row_t` 只有 `from[24]`，但 mailbox JSON 同时给 `counterpart` 与
     `last_sender`（§2.1 ⑨），且 §4.1 行布局两个都用（"发送者名称用它 +
     last_sender"）——结构体缺 `last_sender`。
  2. `pen_pal_id=null` 的解析哨兵未定义：§4.4 判定写 `pal_id <= 0`，但 §5 的
     `int pal_id` 没有声明 null→0/-1 的映射规则。
  3. 线程内存预算 16KB vs 单信 4KB 截断：>4 封长信即超预算，淘汰规则（丢最旧？
     拒收？）未定义。
- **影响**：实现时三处都需临时决策，与"防御式解析"（R1）叠加后行为不可预期。
- **最小修复**：§5 结构体补 `last_sender[24]`；写明 null→`pal_id=0` 哨兵；写明
  线程加载超 16KB 时丢弃最旧信并提示。

### 1.7 前次评审两处引用失实（影响其 §1.1 / §1.3 / T6 结论）

- **严重性**：Medium（对评审记录的可信度）
- **位置**：`docs/reviews/penpal-design-review-result.md:17-20`（§1.1）、`:44-45`（§1.3）、`:259`（T6）
- **证据**：
  1. 前次 §1.1 称设计稿 §3.2 含"`s_pp_busy_gen`"且 8 条规则"完全吻合"——设计稿
     原文（:117-118）只有 `s_pp_busy` + `s_pp_gen`，无 busy_gen（本评审 §1.3）。
  2. 前次 §1.3 称"SECURITY.md 明确：`ai` 双槽、`weather` 双槽"——SECURITY.md:9
     原文为 "namespace `ai` dual-slot, `weather` for OWM key/coords"，**只有 ai
     标注双槽**；代码侧 weather 是普通单 `putString`（`ui_weather.cpp:294-295`），
     per-provider AI key 也是单槽（`ui_ai_cfg.cpp:316-321`）。
  3. 由此，设计稿 R3 的"偏差"定性其实**高估**了——单槽 NVS 早有 weather /
     provider key 先例，SECURITY.md 并未强制双槽；R3 可从"需评审确认的偏差"
     降级为"遵循既有单槽先例"。
  4. 前次 T6 接受的 page_num 公式经本评审 §1.1 证伪。
- **最小修复**：以本评审 §1.1/§1.3/§1.5 为准修订设计稿；前次评审文件按
  docs/reviews 约定不改动，仅在此登记差异。

### 1.8 demo 脚本含 `GET /users/me/profile` 端点，§2 未提及

- **严重性**：Low
- **位置**：`docs/penpal-design.md:27-37`（§2 端点表）
- **证据**：`scripts/remote_api_demo.py:51` 演示 `GET /api/v1/users/me/profile`
  （本人资料：name/age_band/level/city/interests）。不影响"PROFILE 由 ①+⑨
  合成"的决策（该端点是本人而非笔友资料），但可作为 PROFILE 的廉价增量信息。
- **最小修复**：§2 补一行该端点的存在与"暂不接入"的取舍。

### 1.9 waitbox Close=取消是全仓首例交互，需在申请书显式标注

- **严重性**：Low
- **位置**：`docs/penpal-design.md:124-126`（§3.2）
- **证据**：现有 waitbox 无取消按钮——chat 屏 waitbox 打开时**吞噬全部输入**
  （`ui_ai_chat.cpp:802-803`）。设计本身合规（契约规则 9 :41-42 支持 gen++ +
  busy=false、任务不杀），也是 120s+ 请求的正确选择，但它首次把规则 9 落成
  可交互 UI，改变 EPD 屏的等待体验预期。
- **最小修复**：评审申请书中显式标注"waitbox 新增 Close=取消，为首例"；配合
  §1.3 的 busy_gen 一并实现。

### 1.10 §3.2 残留未定稿的自问自答文字

- **严重性**：Low
- **位置**：`docs/penpal-design.md:129-131`
- **证据**："（busy 检查放宽为'同类请求 busy'才允许并发两条？**不**——契约 §2.7…
  ）"是设计 deliberation 的草稿痕迹，结论（串行两段）正确但行文未定稿。
- **最小修复**：改为直陈句："HOME 刷新为串行两段：先 PALS，结果回来后再发
  MAILBOX，以满足契约 §2.7 同一时刻最多 1 个在飞结果。"

### 1.11 归因与措辞五处小修

- **严重性**：Low
- **证据**：
  1. "README 'Use pagination, not scrolling'"出自 `examples/pda2/README.md:78`，
     非根目录 `readme.md`——§3.1 应显式注明。
  2. §6 `scripts/gen_img_penpal.py` 称"脚本可复再生"，但 `scripts/` 与
     `examples/pda2/scripts/` 均无 `gen_img_*.py` 先例（与前次评审 §1.8 结论
     一致）——是新约定，措辞应从"复用"改为"新增"。
  3. §4.2 "body ≥50 字节（英语信件按字符计）"字节/字符混用，状态行
     `Need 50+ chars (now N)` 中 N 的单位需唯一。
  4. §4.1 未定义 base/key 未配置时 HOME 的行为（建议状态行引导 + Sync 报错
     "configure server in Cfg"）。
  5. 文中"chat 屏"指代建议落实为文件名 `ui_ai_chat.cpp` /
     `ai_chat_keyboard_poll()`，避免实现期歧义。
- **最小修复**：逐句修订，无设计变更。

---

## 2. 已核实无误项（抽样列举，均经代码/文件对照）

- 契约 §2.6/§2.7 引用准确：栈 1024×8、优先级 1、gen 首字段、队列深度 4、
  busy 拒绝新请求——`async_ipc_contract.md:24-35`，chat 先例
  `ui_ai_chat.cpp:606-643`。
- `http_utils` 确为 HTTPS-only（`http_utils.cpp:256,294,335,375` 全部
  `WiFiClientSecure`），全 `examples/` 无明文 HTTP 先例——§3.3 自带分派、不改
  http_utils 的决策成立；`http_require_wifi()` / `http_get_tls_mode()` 签名
  与复用计划一致（`http_utils.h:42,51`）。
- §2 的 9 端点与 180s demo 超时逐一对上 `remote_api_demo.py:47-174`。
- chat 既有模式全部属实：54×30 顶栏按钮（`ui_ai_chat.cpp:1027,1035`）、秒级
  倒计时 waitbox（:706-768）、只走 Send 按钮 + 空框 `\b` 回退（:861-870）、
  `+/-` 滚动（:840-842）、触摸滚动 redraw-on-release（:584-600）、4KB UTF-8
  边界截断 `(truncated)`（:64,68,140-151）、`/chat.draft` 草稿（:72,393+）。
- weather 三页 HIDDEN 模式：`ui_weather.cpp:531-557`。
- 菜单结构：18 项 `menu_btn_list`（`ui_deckpro.cpp:252-274`）、`menu_btn` 尾部
  两 int 为**按钮绝对坐标**（`ui_deckpro.h:92-98`，非图标偏移——§6 的
  `{..., 23, 13}` 数值正确）、`menu_screen2` 默认 HIDDEN（:438）、页点仅 2 个
  （:479-494）。
- 图标格式：50×50 `LV_IMG_CF_TRUE_COLOR_ALPHA`、本构建 2 字节/像素成立，但
  **仅因 `LV_COLOR_DEPTH 1`**（`config/lv_conf.h:27`；depth 16 时为 3 字节/像素）
  ——建议 §6 加半句注脚。
- 配置链四层与 `.gitignore` 覆盖 `config_keys.h`、`env.cfg.example` 格式、
  `*_DEFAULT_DEV` 先例（`config_keys.h:34` → `openai_api.cpp:65-67`）、
  NVS 命名空间 15 字符限制内——全部属实。
- 集成点：`SCREEN_*_ID` 枚举可直接追加（`ui_deckpro.h:48-80`）、
  `scr_mgr_register` 流程（`ui_deckpro.cpp:4363-4409`）、`factory.ino:765-784`
  的 `xxx_keyboard_poll()` 挂接模式——均吻合。
- §7 验证计划（GET-only 预验、不写测试数据、断网回归、waitbox 取消项）设计得当。
- §9 commit 拆分顺序（API→UI→menu→docs）依赖正确，与 reviews README 的
  命名/拆分规则兼容。

## 3. 跟踪项表（不阻塞，实现阶段补登）

| # | 项 | 来源 | 跟踪至 |
|---|---|---|---|
| T1 | `users/me/profile` 端点取舍 | §1.8 | commit 1 前定稿 §2 |
| T2 | waitbox Close=取消首例标注 | §1.9 | commit 4 申请书 |
| T3 | 线程 16KB 淘汰规则、null→哨兵、`last_sender` 字段 | §1.6 | commit 2 前定稿 §5 |
| T4 | env.cfg 容量说明 + 95/96 字符对齐 | §1.5 | commit 1 前定稿 §3.4 |
| T5 | §3.2 草稿文字定稿 + 归因/措辞小修 | §1.10/§1.11 | commit 1 前 |
| T6 | 前次评审失实引用登记（busy_gen / weather 双槽 / T6 公式） | §1.7 | 本文件即登记 |

## 4. 验证说明

- 本评审所有"证据"条目均给出 `file:line` 并已在当前工作区实际读取核对，非转述。
- 未执行固件编译与真机回归（设计评审阶段无此要求）；§2 schema 实测数据采信
  设计稿声明，但与 demo 脚本代码路径自洽。
- 前次评审（penpal-design-review-result.md）结论经逐项复核：其 §1.1"8 条规则
  完全吻合"、§1.3"weather 双槽"、T6 公式接受三处不成立（见 §1.7）；其余结论
  （§1.9 状态行文案、§1.10 按钮可见性、§1.8 资源脚本路径等）仍然有效，与本
  评审互补不冲突。

## 5. 审批意见

- [ ] A. 全量接受
- [ ] B. 仅保留设计稿
- [x] C. **部分接受**
- [ ] D. 拆分提交

**接受范围**：§3 架构（单屏多页、异步模型、HTTP 分派、配置链）、§4 页面交互、
§7 验证计划、§9 commit 拆分——整体可作为实现基线。

**前置条件（commit 1 落地前必须完成的设计稿修订）**：

1. §6 page_num 公式修正 + 补登按钮创建循环 / 第 3 页点改动（§1.1）；
2. §2 LLM 超时 ≥180s 或补充论证（§1.2）；
3. §3.2 增加 `s_pp_busy_gen` 及 busy 释放规则（§1.3）。

**建议同批完成**：§1.4（结果 type 字段）、§1.5（env.cfg 容量）、§1.6（数据模型
三处）、§1.10/§1.11（文字定稿）——均为文档级修订，不改架构。

---

**评审人**：Kimi（第二轮独立复核；对设计稿全部外部引用做了代码级对照，
含与前次评审结论的差异登记）。
