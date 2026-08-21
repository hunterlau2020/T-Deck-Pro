# 笔友（PenPal）App 设计文档

> 状态：**v3.1 修订稿，待复审**（2026-08-22）。v1/v2/v3 经四轮评审
> （`penpal-design-review-result.md`、`penpal-design-review-result-kimi.md`、
> Codex/Kimi v2 复审各一份、Codex v3 复审 `…-8109c9e.md`）；本版关闭 Codex v3
> 复审两项边界（SEND 在飞编辑锁 / null 笔友残留行查询定义，见变更历史）。
> 参考实现接口：`scripts/remote_api_demo.py`；API schema 于 2026-08-21/22 对本地测试服务器
> `http://127.0.0.1:8000` 实测确认（curl GET 探测 + demo 全流程，见 §2）。

## 1. 背景与目标

- **场景**：英语书信写作练习（KET/IELTS 方向）。设备端作为远程笔友服务的客户端：
  收发信件、按主题聚合阅读往来、对自己的信请求 **纠错（correction）** 与
  **润色（polish）**、回信前请求 **沟通建议（tips）**。
- **调试闭环**：NPC 笔友（Sophie / Mei）收到信后 ~2 分钟自动回信，无需真人在线，
  单机即可走通"写信→等回信→再回信"全流程。
- **测试环境**：
  - 服务器：本地 `http://127.0.0.1:8000`（后端另行部署，不在本仓库）；
  - 账号（`X-API-Key` 即账号身份）：`89rg35eua2` = hunter，`3s60yrgdua` = terry；
  - **设备不能用 127.0.0.1**——Cfg 页需填运行后端的 PC 的局域网地址
    （如 `http://192.168.x.x:8000`；后端需监听 0.0.0.0，Windows 防火墙放行 8000 入站）。
- **菜单入口**（用户已拍板）：第 1/2 页不动，**第 3 页只放 PenPal**（9/9/1）。

## 2. 服务端 API 契约（实测）

认证：所有请求带 `X-API-Key: <key>` 头。基础路径 `<base>/api/v1`。
LLM 类端点（correction/polish/tips）服务端调用大模型，**耗时可达分钟级**
（demo 脚本实测 `timeout=180`，即服务端耗时的上界证据，`remote_api_demo.py:47`）
→ 客户端这三类用 **180s** 超时（与服务端上界对齐：waitbox Close 取消已兜底体验，
超时宁可偏长——若按 120s 设限，被截断的恰是最慢、往往也最长的信，kimi §1.2），其余 CRUD 用 **20s**。

| # | 端点 | 方法 | 用途 |
|---|---|---|---|
| ① | `/pen-pals` | GET | 笔友列表（首页 icon 行） |
| ② | `/topics/suggestions` | GET | 推荐写作主题（按年龄段题库） |
| ③ | `/emails` | POST | 写信（body 带 `topic_id` 可选；**可带 `Idempotency-Key` 头**，§2.2） |
| ④ | `/emails?thread_root_id[&pen_pal_id]` | GET | 读线程（**按首信锚点精确取**，v3 起；`subject` 参数为兼容通道——跨线程合并同主题返回，仅旧客户端用；**`pen_pal_id` 可选**——R9 上线 2026-08-22：缺省时凭 `thread_root_id` 按 key 用户授权读取残留线程，响应 `pen_pal_id` 为 `null`；两参皆缺 400、`=0` 仍 400） |
| ⑤ | `/emails/{id}/correction` | POST | 纠错（仅自己的信） |
| ⑥ | `/emails/{id}/polish` | POST | 润色（仅自己的信） |
| ⑦ | `/emails`（body 带 `thread_root_id` 锚点） | POST | 回信（subject 仍带 `Re: ` 前缀——锚点缺失时的服务端兜底，v3 起双保险） |
| ⑧ | `/emails/{id}/tips` | POST | 回信建议（基于最近一封收到的信） |
| ⑨ | `/emails/mailbox` | GET | 信箱线程概览（首页列表，行含 `thread_root_id`） |
| ⑩ | `/users/me/profile` | GET | 本人资料（name/age_band/level/city/interests，demo `:74-81` 步骤⓪ 有演示，已上线）。**v1 暂不接入**：PROFILE 页按 §4.6 由 ①+⑨ 合成的是**笔友**资料，此端点是**本人**资料；留作后续廉价增量（kimi §1.8/T1） |

### 2.1 实测响应 schema（2026-08-21）

**① pen-pals**（hunter 账号实测）：
```json
[{"id":6,"owner_user_id":1,"partner_user_id":4,"is_npc":false,"npc_id":null,
  "status":"active","name":"terry"},
 {"id":14,"owner_user_id":1,"partner_user_id":null,"is_npc":true,"npc_id":3,
  "status":"active","name":"Mei"}]
```
- **无 `GET /pen-pals/{id}` 详情端点**（实测 404）→ PROFILE 页由 ①+⑨ 合成（§4.6）。

**② topics**：
```json
[{"id":7,"title":"Learning English: my story","exam_tag":"IELTS",
  "background":"Your pen friend is also learning English. ...",
  "guiding_questions":["Why are you learning English?","..."]}]
```

**⑨ mailbox**（按 `last_at` 降序，服务端已排好）：
```json
[{"thread_root_id":57,"pen_pal_id":14,"subject":"Learning English: my story",
  "counterpart":"Mei","counterpart_kind":"npc","count":2,"unread":1,
  "state":"pending","last_sender":"Mei","last_at":"2026-08-20T15:10:01",
  "snippet":"Dear Hunter,..."}]
```
- **`thread_root_id` = 线程锚点**（首信 id，v3 起取线程/回信都用它，demo `:126`
  `root_id = email["thread_root_id"]`）；**同主题可以有多条线程**（demo `:224-238`
  步骤⑨：同 subject 再写 = 新线程，各自成行）→ 按行 `subject` 显示、按
  `thread_root_id` 寻址；
- `state` 实测取值：`pending`（等回信）/ `replied`（已回）/ `sent`（已发出）；
- `counterpart` = 对端显示名（首页行的"发送者名称"用它 + `last_sender`）；
- **`pen_pal_id` 可为 `null`**（笔友关系已删但线程残留，实测 Sophie 行
  `thread_root_id=1`）。**v3.1 时实测**（2026-08-22，hunter 账号，GET-only）：
  `?thread_root_id=1` 缺 `pen_pal_id` → 422、`?pen_pal_id=0&…` → 400、带 pal
  锚点查询 200 → null 行当时无读取通道，HOME 过滤 null 行（§4.1）+ §8 R9
  服务端需求。**R9 上线并复测**（2026-08-22，GET-only）：`?thread_root_id=50`
  单参 → **200**，响应 `pen_pal_id=null`、emails `[50,56]`（hunter/Mei）；
  两参皆缺 → 400。→ HOME 恢复"显示 + 只读"（§4.1/§4.4，随实现 commit 2 落地）。

**④ thread**（`?pen_pal_id&thread_root_id` 精确锚点查询）：
```json
{"pen_pal_id":14,"emails":[
  {"id":50,"sender_user_id":1,"receiver_user_id":null,"npc_id":3,
   "subject":"Learning English: my story","content":"Dear friend, ...",
   "parent_email_id":null,"is_npc_reply":false,"sender_name":"hunter",
   "created_at":"2026-08-20T14:11:11","read_at":null,"topic_id":7,
   "thread_root_id":57}, ...]}
```
- `sender_user_id != null` → 我写的信（纠错/润色按钮的判定依据）；
- **v2 曾按 mailbox 行 `subject` 查线程**（服务端 `Re: ` 聚合）——该通道现为
  **兼容通道，会跨线程合并同主题信**（demo `:21-22`）：同主题多线程（⑨ 场景）
  下取到混合结果。v3 改按 `thread_root_id` 精确取，`Re: ` 归并交给服务端
  锚点逻辑，客户端不做 subject 双查询/前缀归并；
- emails 数组**升序**（旧→新）→ UI 侧反转后 index 0 = 最新。

**③ send 请求/响应**：`{"pen_pal_id":N,"subject":"...","topic_id":N|null,
"content":"...","thread_root_id":N?}`（回信带锚点，demo `:185-193`）→
`{"email":{"id":N,"topic_id":N,"thread_root_id":N,...},"reply_pending":bool}`
（新线程首信的 `thread_root_id` = 自身 id，demo `:126`）。

**⑤ correction**：`{"degraded":false,"corrections":[{"type":"...","from":"...",
"to":"...","explanation":"..."}]}`

**⑥ polish**：`{"degraded":false,"improved_email":"...",
"improvements":["..."],"topic_coverage":[{"question":"...","status":"..."}]}`

**⑧ tips**：`{"degraded":false,"tips":["..."]}`

- `degraded=true` 表示服务端 LLM 降级，FB 页/ msgbox 标注 `(degraded)`。

### 2.2 发信幂等键（服务端 2026-08-22 已实现，Codex v2 P1 的关闭依据）

创建型 POST（发信 ③/⑦）可带 `Idempotency-Key: <32 位 hex>` 请求头
（demo `:24-30` 演示、`:53-56` 生成惯例、`:121-143` 重放实证）：

- **首次**：`201` 新建；**同 key 重发**（上次结果未确认：关等待框/超时/断连）：
  `200` + 响应头 `Idempotent-Replayed: true`，正文返回**同一封信**（id 不变，
  线程不多出第二封，demo `:139-142` 有断言）；
- 服务端按 **(用户, key)** 判重，key 只在**投递语义**上绑定一次发送；
  **收到确认成功后 key 作废；草稿内容变化必须换新 key**（重放返回的是服务端
  已存正文，与新草稿无关，demo `:29`）；
- 不带该头 = 与旧版完全一致的行为。

客户端职责（§3.2/§4.2 落实）：投递前生成并保存 key；结果未确认期间的重试
**复用同一把**；确认成功后丢弃。penpal_api 自有请求函数需透传响应头，把
`Idempotent-Replayed` 识别为成功（SEND 结果带 `replayed` 标志，状态行区分
`sent ok` / `sent ok (replayed)`）。

## 3. 总体架构

### 3.1 单屏多页（不用 scr_mgr push/pop）

一个 `SCREEN_PENPAL_ID`，内部 7 个页面用 `LV_OBJ_FLAG_HIDDEN` 切换
（weather 三页模式的扩展，符合 `examples/pda2/README.md:78` 的
"Use pagination, not scrolling"）：

| 页 | 内容 | 进入 |
|---|---|---|
| HOME | 笔友 icon 行 + 线程列表 + 翻页 | 菜单第 3 页点 PenPal |
| COMPOSE | 写信（new / reply 两模式） | HOME 点笔友 icon / THREAD 点 Reply |
| TOPICS | 推荐主题列表 | COMPOSE 点 Pick |
| THREAD | 同主题往来信（1 封/页） | HOME 点线程行 |
| FB | 纠错/润色结果 | THREAD 点 Fix/Polish |
| PROFILE | 笔友资料 | COMPOSE 点 View |
| CFG | 服务器地址 + API key | HOME 点 Cfg |

理由：**COMPOSE→TOPICS→返回 的草稿必须原样保留**（textarea 不能被销毁重建）；
单 keyboard poll、单代次、无跨屏生命周期传递。

### 3.2 异步模型（完全遵守 `docs/async_ipc_contract.md`）

- 整个 app 共用：1 个结果队列 `s_pp_q`（深度 4）+ 1 个 `s_pp_busy`（仅 UI 线程读写）
  + **busy 所属代次 `s_pp_busy_gen`**（契约规则 3）+ 1 个页面代次 `s_pp_gen`
  （entry/destroy/取消时 +1）。**busy 释放规则：结果仅在 `res->gen ==
  s_pp_busy_gen` 时才允许清 `s_pp_busy`**——迟到结果不得解锁新请求
  （先例 `s_wifi_test_busy_gen`，`ui_deckpro.cpp:1479`，kimi §1.3）。
- 每次请求 `xTaskCreate`（栈 1024×8、优先级 1，契约 §2.6）：任务持有**自有快照**
  （base/key/参数全部拷贝，`new` → 任务内 `delete`），JSON 解析也在任务线程完成，
  结果结构体 `new` 后 `xQueueSend` 移交 UI，UI 消费后 `delete`。
- 结果结构体：首字段 `gen`，**第二字段 `type`**（枚举
  `PP_RES_PALS / MAILBOX / THREAD / SEND / TOPICS / FIX / POLISH / TIPS`）——
  单队列复用 8 类请求，UI 消费端按 `type` 分派处理路径与 `delete`，缺它则 8 类
  结果的释放/分发只能实现期临场发明（kimi §1.4）。
- 页面代次不匹配（迟到结果、离屏结果、取消后结果）一律丢弃，且**不得**触碰
  `s_pp_busy`；busy 释放只认 `s_pp_busy_gen`。
- UI busy 期间拒绝新请求；队列创建失败 → 提示、不置 busy、不启动任务。
- waitbox（`ui_ai_chat.cpp` 同款秒级倒计时，EPD 友好）：所有网络请求弹出，
  **加 Close 按钮**——v3 起按请求类型分两种语义（Codex v2 P1 修复）：
  - **读/算型**（PALS/MAILBOX/THREAD/TOPICS 与 FIX/POLISH/TIPS）：Close =
    **取消**（`s_pp_gen++`、`busy=false`、任务不杀、迟到结果按代次丢弃）——
    correction/polish 可能耗时 180s，不能锁死输入。重试无副作用（FIX/POLISH/
    TIPS 是派生结果，服务端不建信）；
  - **创建型**（SEND）：Close = **不再等待，后台继续**——仅隐藏 waitbox，
    `s_pp_gen` **不**递增、busy 保持（契约 §2.7 单在飞不破；SEND 超时 20s，
    busy 最多再占 20s）。结果照常按代次消费：状态行 `sent ok (replayed)?`，
    busy 释放。v2 的"Close=取消"会把已发出的 POST 变成结果未确认，用户重按
    Send 即双发（Codex P1 原案）；幂等键（§2.2）+ 后台继续双保险后，重按
    Send 也只重放同一封。
  - **SEND 在飞期间 COMPOSE 编辑锁**（Codex v3 复审 P1 修复）：Send 按下即把
    Title/Body textarea 与 Pick/Tips 置 `LV_STATE_DISABLED`，Close（后台继续）
    **不解锁**，状态行 `sending in background...`——否则用户 Close 后编辑的
    新草稿会在旧请求成功时被"清空 COMPOSE"语义误清。结果消费时仍**比对
    payload 快照**才清空（锁下必相等，双保险）；不等等（未来旁路编辑）则
    保留草稿、状态行单独提示 `previous send ok`。失败/超时 → 不清空、解锁、
    报错。屏销毁随屏重置（重建后 payload 不可能等于快照，key 自然失效）。
  - **这是全仓首例 waitbox 可取消/可收起交互**（现有 chat waitbox 打开时吞噬
    全部输入），commit 4 评审申请书将显式标注（kimi §1.9/T2）。
- **幂等键生命周期（客户端，RAM）**：Send 按下 → 校验通过 → `esp_fill_random`
  16 字节生成 32 hex key，与 **payload 快照**（pal_id/subject/topic_id/
  content/thread_root_id）一起存入屏级 `s_pp_idem`（~1.2KB RAM）→ 发起 POST。
  结果确认成功 → 清 `s_pp_idem`、清空 COMPOSE（v2 语义不变）；未确认（超时/
  断连/离屏丢结果）→ **保留**，下次按 Send 时比对：payload 与快照一致 →
  复用同一把 key（重放安全）；任何编辑（含 TOPICS 重选）→ 换新 key。**不落
  NVS**：断电即丢草稿（§5 薄客户端取舍），重写的内容本就是新投递，新 key
  语义正确；与 COMPOSE 草稿不落盘（R7）一致。
- **偏差说明（单队列）**：契约先例为每任务一队列（`s_wifi_test_q`/`s_chat_q`…，
  契约表格 `async_ipc_contract.md:13-18`）；penpal 8 类请求共用 `s_pp_q`——同屏同类生命周期、结果以
  `type` 字段分派，属登记过的合理偏差（kimi §1.4）。
- HOME 刷新为**串行两段**：先 PALS，结果回来后再发 MAILBOX，以满足契约 §2.7
  "UI busy 期间拒绝新请求"保证的同一时刻最多 1 个在飞结果；状态行两段文案：
  PALS 成功后 `PALS OK, syncing mailbox…`，MAILBOX 成功后 `Mailbox OK`
  （哪步失败就止于哪步的报错）（kimi v2 §3.1）。**串行两段期间 busy 保持到
  MAILBOX 结果消费完毕才释放**——第二段在 PALS 结果消费路径内直接链式发起、
  busy 不清；若中间清 busy，用户此刻插入第三请求会挤占 busy，MAILBOX 链发起
  被拒、HOME 停在"有笔友无列表"状态（kimi v2 §3.4）。

### 3.3 网络层：明文 HTTP 支持（不改 http_utils）

现有 `http_utils.cpp` 只走 `WiFiClientSecure`（HTTPS）。笔友测试服务器是明文
`http://`，且将来部署形态未知 → `penpal_api.cpp` 自带请求函数：

```
URL 前缀 http://  → WiFiClient（明文，无 NTP/TLS 依赖）
URL 前缀 https:// → WiFiClientSecure（按 http_get_tls_mode() 现行策略）
其余              → 报错 "URL must start with http:// or https://"
```

不改已评审的 http_utils（避免波及 AI/Weather/WiFi 路径）；`http_require_wifi()`
与 TLS 模式仍复用。

### 3.4 配置链（沿用 SECURITY.md 模式）

```
NVS 命名空间 "penpal"（键 base / key）
  → SPIFFS /env.cfg（PENPAL_BASE=... / PENPAL_KEY=...，env_secrets 解析）
  → gitignored config_keys.h（PENPAL_BASE_DEFAULT_DEV / PENPAL_KEY_DEFAULT_DEV，
    本地开发填 PC 局域网 IP + hunter 测试 key）
  → 空默认（Cfg 页引导填写）
```

- 设备端通过 **CFG 页**修改（两个 textarea + Save + 状态行）。
- **容量说明（kimi §1.5/T4）**：`env_secrets` 8 槽上限（`ENV_MAX_ENTRIES`，
  `env_secrets.cpp:21`；第 9 条起**静默丢弃**，`:48`）+ 值上限 95 字符
  （`val[96]`）。现有有意义键最多 7 个（`OPENROUTER_KEY`/`OWM_KEY`/
  `WEATHER_COORDS` + 4 个 AI provider key），加 `PENPAL_BASE`/`PENPAL_KEY`
  理论上限 9 > 8——provider 键与 penpal 键同配时 env.cfg 末条静默失效；NVS 优先
  （Cfg 页 Save 即写 NVS，此后不依赖 env.cfg）故实际影响有限。Base URL textarea
  限长 **≤95**（§4.7），与 env.cfg 值上限对齐（96 字符的 base 无法经 env.cfg
  完整往返）。
- **单槽 NVS**：penpal 配置用单槽写 + 错误提示，**不做** AI Config 的双槽原子
  保存——测试服务器地址/非敏感 key，写失败重存即可，无"半新半旧配置可见"风险
  （base/key 无组合一致性要求）。单槽早有先例：weather key/coords
  （`ui_weather.cpp:294-295`）、per-provider AI key（`ui_ai_cfg.cpp:316-321`）
  均单槽 `putString`，SECURITY.md 也仅 `ai` 标注双槽——故此为**遵循既有先例**，
  非待评审偏差（kimi §1.7 勘误了前次评审的"weather 双槽"误读）。

## 4. 页面交互设计

通用：`lv_font_montserrat_14`、黑白配色、顶栏 y=0..32（Back 按钮 + 标题 + 页内
功能按钮，尺寸沿用 `ui_ai_chat.cpp` 顶栏按钮的 54×30）。**键盘驱动导航 + 触摸可用**（现有各屏同级别：
按钮可触摸点击，键盘负责文本/翻页/返回）。

### 4.1 HOME（首页）

```
┌──────────────────────────────────────┐
│ [Back] PenPal            [Cfg] [Sync]│  顶栏
│ ┌─────┐ ┌─────┐ ┌─────┐              │  笔友 icon 行（≤3 个，66×52）
│ │ S   │ │ M   │ │ t   │  S/Mei/terry │  首字母方块 + 名字；点→COMPOSE(new)
│ └─────┘ └─────┘ └─────┘              │
│ status line（同步状态 / 错误）        │
│ ┌──────────────────────────────────┐ │
│ │ Mei          08-20 15:10  [1new] │ │  行1：last_sender（回落 counterpart）+ last_at + unread
│ │ Learning English: my story  pend  │ │  行2：subject + state
│ ├──────────────────────────────────┤ │  5 行 × 34px；点行→THREAD
│ │ terry        08-20 14:33         │ │
│ │ usage test   sent                │ │
│ │ ...                              │ │
│ └──────────────────────────────────┘ │
│ [◀ Prev]      page 1/2      [Next ▶] │  底部翻页（超 1 屏才显示）
└──────────────────────────────────────┘
```

- 数据：mailbox 上限 **24 行**，每页 5 行；行 1 名称显示 `last_sender`
  （最后一封的发件人，= 我时显示自己名字），空/缺失回落 `counterpart`
  （§5 结构体两字段都有）；`last_at` 显示 `MM-DD HH:MM`
  （ISO 串截取，不转时区——服务端时间即本地时间）；
- 状态列：`unread>0` 显示 `[Nnew]`；`state` 缩写 `pend/repl/sent`；
- `pen_pal_id=null` 的行**显示**（笔友已删线程残留；R9 上线后凭
  `thread_root_id` 单参只读打开，2026-08-22 复测 §2.1）——行内名称用
  `counterpart`，进 THREAD 只读形态（§4.4）；
- 键盘：`+/-`（Sym/Alt 层）= 翻页，`\n` = Sync，`\b` = 返回菜单；
- Sync = 串行 PALS→MAILBOX 刷新（NPC 回信后手动拉取）。
- **base/key 未配置**（解析链全空）时：HOME 正常进入，icon 行/列表区显示空、
  状态行 `configure server in Cfg` 引导；Sync 同文案直接报错，不发网络请求
  （kimi §1.11.4）。

### 4.2 COMPOSE（写信，new / reply 两模式）

```
┌──────────────────────────────────────┐
│ [Back] Write / Reply                 │
│ To: Mei                     [View]→PROFILE │
│ Topic: Learning English...   [Pick]→TOPICS │  (new 模式；topic 可不选)
│ Title: [____________________]        │  单行 textarea（≤56 字节，UTF-8 边界）
│ ┌──────────────────────────────┐ ┌──┐│
│ │ Body (multi-line)            │ │Sen││  多行 textarea（≤1000 字符）
│ │                              │ │d  ││
│ └──────────────────────────────┘ └──┘│
│ 123 chars        [Tips](reply 才有)  │  字数标签
│ status line（校验/发送错误）         │
└──────────────────────────────────────┘
```

- **new 模式**：收件人 = 点 icon 的笔友；topic 可选；title 手填或由 TOPICS 带入。
- **reply 模式**：记录所在线程的 `thread_root_id`（发送 body 带锚点，§2 ⑦），
  subject 预填 `Re: <线程 subject>`（锚点缺失时的服务端兜底，双保险），
  无 Topic 行，多 **Tips** 按钮（§4.2.1）。
- **Title 限长 56 字节（按字节，非字符）**（Codex v2 P2 修复）：v2 的
  "≤60 字符"按 ASCII 假设，60 个非 ASCII 字符 UTF-8 可达 240 字节，超出
  §5 显示缓冲 `subject[64]`；改按**字节**在输入侧截断（UTF-8 边界回退，
  `ui_ai_chat.cpp` 同款），状态行字数标签随 Title 焦点显示 `NN/56 bytes`。
  56 的取值使 `Re: `（4 字节）+ 56 = 60 ≤ 64——回信标题必落在显示缓冲内。
  发送正文用 **canonical `std::string`** 组 JSON，不经定长缓冲拷贝。
- 发送门槛：title 非空、**body ≥50 字符**（英语信件按**字符**计数，与状态行
  `Need 50+ chars (now N)` 的 N 单位一致；kimi §1.11.3）；不足时不弹网络请求；
- 键盘：打字进当前焦点框；`\t`（Alt+Enter）切换 Title/Body 焦点；Body 内 `\n` =
  换行；**发送只走 Send 按钮**（`ui_ai_chat.cpp` 既定语义）；`\b` 在空框时返回 HOME；
- 发送成功：清空 COMPOSE、回 HOME、自动触发 Sync（查看 NPC 回信需再等 ~2 分钟，
  用户手动 Sync）；成功状态行区分 `sent ok` / `sent ok (replayed)`（§2.2 幂等
  重放识别），失败/未确认则 COMPOSE 保留原文，重按 Send 走 §3.2 幂等键
  复用/换新逻辑。

#### 4.2.1 Tips（reply 模式）

- 取当前线程**最近一封非我信**（`sender_user_id == null`）的 email id，POST ⑧；
- 结果 msgbox：逐条列出 tips（滚动 label）；无收到的信时 msgbox 提示
  "No incoming letter yet"。

### 4.3 TOPICS（推荐主题）

```
┌──────────────────────────────────────┐
│ [Back] Topics                        │
│ Learning English: my story     IELTS │  行：title + exam_tag
│ Technology in daily life       IELTS │  ~6 行/页，超过则底部翻页
│ A trip I dream of              IELTS │
│ ...                                  │
│ [◀ Prev]                [Next ▶]     │
└──────────────────────────────────────┘
```

- 数据：topics 上限 **16 条**；
- 点行 → **suggestion msgbox**：`title` + `background` + `guiding_questions` 逐条 +
  `[Use] [Cancel]`；
  - Use → 返回 COMPOSE，`topic_id` 记录、Title 自动填入 topic title（可再编辑）；
  - Cancel / `\b` → 返回 COMPOSE，保持原状（**不选主题也允许发送**）；
- 键盘：`+/-` 翻页，`\b` 返回；msgbox 打开时 `\n`=Use、其他键=Cancel（`ui_ai_chat.cpp` 确认框
  同款键盘语义）。

### 4.4 THREAD（对话界面，Gmail 式主题聚合）

```
┌──────────────────────────────────────┐
│ [|◀ Start] [< Prev] [Next ▶]    2/5  │  顶栏 3 按钮 + 计数
│ From: Mei        08-20 15:10         │  信头（我的信显示 To:）
│ ┌──────────────────────────────────┐ │
│ │ Dear Hunter,                     │ │  信正文（滚动容器）
│ │ Thank you for telling me about   │ │  触摸滚动 redraw-on-release +
│ │ Momo—what a wonderful name...    │ │  +/- 键滚动（`ui_ai_chat.cpp` 同款）
│ │                                  │ │
│ └──────────────────────────────────┘ │
│ [Fix] [Polish]     （非我信 DISABLED）│  底部条件按钮
│ [Reply]               （第 1 页才有）│
└──────────────────────────────────────┘
```

- 非我信（NPC 来信）→ Fix/Polish 置 `LV_STATE_DISABLED`（灰显、**位置稳定**
  不 hide——避免按钮增删引起布局跳动），不弹提示；Reply 可见性不受影响，
  维持"第 1 页才有"（kimi v2 §3.1）。

- 进入即按 HOME 行的 `thread_root_id` 精确取线程（§2 ④，v3 起——同主题多线程
  不再被 subject 兼容通道合并）；
- 数据：线程信件**时间逆序**分页，**每页 1 封**，index 0 = 最新（首页）；
- `|◀ Start` = 回到 index 0；`< Prev` = 更旧一封；`Next ▶` = 更新一封；
  到边界时按钮置灰；
- `pal_id == 0`（null→0 哨兵，§5；笔友已删线程残留行，R9 上线后 HOME
  显示、此处只读打开）：查询走 `thread_root_id` 单参残留通道（§2 ④），
  信头提示 `pal removed - read only`、隐藏 Reply；Fix/Polish 对我的信
  维持可用（按 email id，服务端仍可纠）；
- **我的信**（`sender_user_id != null`）→ `Fix` / `Polish` → FB 页（异步 180s）；
- **第 1 页**（index 0）→ `Reply` → COMPOSE(reply)；
- 键盘：`+/-` 滚动正文，`\b` 返回 HOME；
- 单信正文 >4KB 截断加 `(truncated)`（UTF-8 边界，`ui_ai_chat.cpp` 同款）。

### 4.5 FB（纠错/润色结果）

FB 页 `entry()` 按 §3.2 结果 `type`（FIX / POLISH）选择渲染布局——correction
列表 vs polish 三段，两布局切换时全量重建（kimi v2 §3.2）。

```
┌──────────────────────────────────────┐
│ [Back] Correction / Polish   (degraded?)│
│ ┌──────────────────────────────────┐ │
│ │ [grammar] "I like cat" →          │ │  纠错：type + from→to + 说明
│ │   "I like cats"                   │ │
│ │   explanation...                  │ │
│ │ ...                               │ │  润色：improved_email 全文 +
│ │                                   │ │  improvements 逐条 + coverage
│ └──────────────────────────────────┘ │  滚动容器（同 THREAD 正文）
└──────────────────────────────────────┘
```

- correction/polish 的 JSON 在任务线程格式化为展示文本；
- `degraded=true` 标题后缀 `(degraded)`；
- 键盘：`+/-` 滚动，`\b` 返回 THREAD。

### 4.6 PROFILE（笔友资料，合成）

- 服务端无**笔友**详情端点（⑩ 是**本人**资料端点，已上线但对象不同，§2）→
  由 ①+⑨ 合成：`name`、`NPC pen pal` / `pen pal`（`is_npc`）、`status`、
  线程数（⑨ 中该 pal 的行数）、未读合计；
- 键盘：`\b` 返回 COMPOSE。

### 4.7 CFG（配置）

```
┌──────────────────────────────────────┐
│ [Back] PenPal Cfg                    │
│ Base URL: [http://192.168.x.x:8000 ] │  textarea（≤95 字符，§3.4 对齐 env.cfg 值上限）
│ API key:  [89rg35eua2            ]   │  textarea（≤16 字符）
│ [Save]     status line（保存结果/    │
│            当前解析链来源）           │
└──────────────────────────────────────┘
```

- Save = NVS `penpal` 写 + 状态行报错（单槽，§3.4）；
- 打开时 textarea 预填当前生效值；键盘与 COMPOSE 一致（`\t` 切换焦点）。

## 5. 数据模型与内存预算

```cpp
typedef struct { int id; bool is_npc; char name[24]; char status[12]; } pp_pal_t;        // ≤3+2
typedef struct { int id; char title[64]; char tag[12];
                 char background[96]; char guiding[192]; } pp_topic_t;                   // ≤16
typedef struct { int root_id; int pal_id; char subject[64]; char from[24];
                 char last_sender[24]; char state[12]; int unread; int count;
                 char last_at[20]; } pp_thread_row_t;  // ≤24；root_id = thread_root_id 锚点（§2 ⑨）
typedef struct { int id; bool mine; char sender[24];
                 char time[20]; string content; } pp_letter_t;  // 线程总预算 16KB、单信 4KB
```

- **null 哨兵**：mailbox 的 `pen_pal_id == null` 解析为 `pal_id = 0`（服务端 id
  从 1 起，0 不做合法 id 使用；kimi §1.6）。**null 行的处置（Codex v3 复审
  P2 → R9 上线收尾，2026-08-22）**：R9 上线前 HOME 过滤 `pal_id == 0` 的行；
  上线后（§2.1 复测 200）HOME **显示**、THREAD 走 `thread_root_id` 单参
  残留通道只读打开（`penpal_get_thread` 的 `pen_pal_id<=0` 即省略该参数）。
- **subject 的两副面孔（Codex v2 P2）**：定长 `subject[64]` 只是**显示拷贝**
  （超长按 UTF-8 边界截断加 `...`）；**发送侧 canonical 副本走
  `std::string`**（§4.2 输入限 56 字节，`Re: `+56=60 恒在显示缓冲内，
  实际不会触截断——缓冲上限是防御，不是常态路径）。
- **线程预算与逐出**：总预算 16KB、单信正文 4KB 截断加 `(truncated)`（UTF-8
  边界）；累计超 16KB 时**丢弃最旧信**（emails 升序数组前部），THREAD 顶栏计数
  相应减少，首次逐出状态行提示 `oldest dropped (size limit)`（kimi §1.6/T3）。
- 静态数组 + `std::string` 正文（`ui_ai_chat.cpp` 同款堆分配）；估算峰值 <32KB，PSRAM/堆充足；
- 无本地持久化：mailbox/线程状态以服务端为准（薄客户端），断电无损失语义；
  COMPOSE 草稿 v1 **不落盘**（RAM 内跨页保留；退出屏即丢——评审如要求再按
  chat.draft 模式补 `/penpal.draft`）。

## 6. 文件规划与集成点

| 文件 | 内容 | 预估行数 |
|---|---|---|
| `examples/pda2/penpal_api.h/.cpp` | 配置链 + http/https 分派请求 + 8 端点封装（cJSON 解析为上述结构体） | ~420 |
| `examples/pda2/ui_penpal.h` | 页面枚举、共享状态容器、`penpal_keyboard_poll()` 原型 | ~60 |
| `examples/pda2/ui_penpal.cpp` | 屏生命周期 + HOME + CFG + 异步框架（队列/busy/代次/waitbox） | ~620 |
| `examples/pda2/ui_penpal_write.cpp` | COMPOSE + TOPICS | ~450 |
| `examples/pda2/ui_penpal_read.cpp` | THREAD + FB + PROFILE | ~450 |
| `examples/pda2/src/img_penpal.c` + `scripts/gen_img_penpal.py` | 信封图标 50×50（`LV_IMG_CF_TRUE_COLOR_ALPHA`；2 字节/像素**仅因本构建 `LV_COLOR_DEPTH=1`**，depth 16 时为 3——kimi §2 核实，勿照抄到彩色构建）。`gen_img_penpal.py` 为**新增**脚本：`scripts/` 与 `examples/pda2/scripts/` 均无 `gen_img_*.py` 先例（kimi §1.11.2），非复用既有约定 | ~60 |

集成点（改动现有文件）：

| 文件 | 改动 |
|---|---|
| `ui_deckpro.h` | enum 追加 `SCREEN_PENPAL_ID` |
| `ui_deckpro.cpp` | ① menu_btn_list 追加第 19 项 `{SCREEN_PENPAL_ID, &img_penpal, "PenPal", 23, 13}`；② **菜单第 3 页**（4 处配套，缺一不可，kimi §1.1）：新增 `menu_screen3`（复制 screen2 样式、默认 HIDDEN）；页数公式**保持最大下标语义** `page_num = (MENU_BTN_NUM-1)/9`（18 项存量的 off-by-one 已由 `de78338` 修复；**不可**用 `(MENU_BTN_NUM+8)/9`——门控 `page_curr < page_num` 会放行到不存在的第 4 页）；**按钮创建循环** `if(i < 9) screen1 else screen2`（:453-458）改为按 `i/9` 三路分派到 screen1/2/3；`menu_get_gesture_dir` 加 `page_curr==2` 分支 + `ui_Panel4` 页点 2→3（现有页点按 child 下标寻址 :306-307，第 3 点需新建定位）；③ `scr_mgr_register(SCREEN_PENPAL_ID, &screen_penpal)` |
| `factory.ino` | `loop()` 追加 `penpal_keyboard_poll()` |
| `env.cfg.example` | 追加 `#PENPAL_BASE=` / `#PENPAL_KEY=` 注释行 |
| `config_keys.h.example` | 同步追加两行注释模板 `// #define PENPAL_BASE_DEFAULT_DEV ...` / `// #define PENPAL_KEY_DEFAULT_DEV ...`——首次 clone 不改文件也能直接编过（kimi v2 §3.3） |
| `src/assets.h` | `LV_IMG_DECLARE(img_penpal)` |
| `config_keys.h`（本地） | 追加两个 `*_DEV` 定义（gitignored，不入库） |

## 7. 验证计划

1. **编译**：`python -m platformio run -e pda2`。
2. **服务端预验**（PC 侧，**GET-only，不写测试数据**）：
   `curl -H "X-API-Key: <key>" http://<PC 局域网 IP>:8000/api/v1/pen-pals` 通过；
   **null 行查询已于 2026-08-22 实测两轮**（§2.1：R9 前 422/400 → 服务端
   上线 R9 后单参 `?thread_root_id=50` 复测 200、`pen_pal_id=null`）。后端
   监听 0.0.0.0、Windows 防火墙 8000
   入站放行（**用户侧动作**）。
3. **烧录** COM5（先停串口监控——既定流程）。
4. **真机回归清单**（评审申请 §验证状态）：
   - Cfg 保存 → 状态行确认；重进屏值保留（NVS 生效）
   - HOME 自动/手动 Sync：icon 行 + 线程列表正确显示（对照网页端）
   - 点 Mei icon → COMPOSE：不选 topic 直接写；body <50 字被拒；≥50 发出成功
   - Title 输入中文/CJK 到 56 字节上限截断（UTF-8 边界不切半个字），
     `NN/56 bytes` 标签跟随
   - 选 topic → suggestion msgbox → Use 后 Title 自动带入
   - **幂等重放**：发送后（未确认路径）重按 Send / 断网重连后重发 → 线程只有
     一封信、状态行 `sent ok (replayed)`（对照网页端线程计数）；编辑一字后
     重发 = 新信（新 key）
   - **后台发送编辑锁**（Codex v3 P1）：Send → Close 收起等待框 → Title/Body/
     Pick/Tips 均不可编辑（灰显）、状态行 `sending in background...`；旧结果
     成功后 COMPOSE 清空解锁；失败后草稿保留解锁
   - THREAD：3 个导航按钮、Fix/Polish（我的信）、第 1 页 Reply
   - **同题双线程**（网页端同 subject 再写一封）：HOME 两行各自可开、互不
     混信（thread_root_id 锚点回归）；Reply 落在正确线程
   - 发信后 ~2 分钟 Sync → NPC 回信出现 → 开线程 → Reply + Tips
   - `pen_pal_id=null` 行（网页端删笔友后残留）：HOME 显示、THREAD 只读
     （`pal removed - read only` 信头 + 隐藏 Reply；R9 上线，§4.4）
   - 菜单第 3 页：第 19 项图标显示；左右滑 3 页往返；第 3 页继续左滑**不越界**
     （kimi §1.1 回归——18 项存量幽灵页已由 `de78338` 修复，19 项后同款验证）
   - 断网（关热点）下各操作报错不卡死；waitbox Close 可取消
5. **互通**（可选）：Cfg 改 terry key，重复关键路径。

## 8. 风险与开放问题

| # | 风险/问题 | 处置 |
|---|---|---|
| R1 | 后端字段后续变化（非合同化 API） | 解析全部防御式（字段缺失→默认值+串口日志），显示层降级不崩溃 |
| R2 | 无**笔友** profile 端点（⑩ 本人资料端点已上线，对象不同） | PROFILE 页合成（§4.6），后端补笔友端点后升级 |
| R3 | 配置单槽 NVS | **遵循既有单槽先例**（weather/provider key，kimi §1.7 核实 SECURITY.md 仅 `ai` 标注双槽）——非待评审偏差（§3.4） |
| R4 | montserrat_14 无 CJK 字形 | 信件/界面按英语设计（KET/IELTS 场景），terry 若写中文显示为方块——与其他屏现状一致 |
| R5 | mailbox 服务端无界增长 | 客户端 24 行上限 + 分页，超出提示 `more on web` |
| R6 | LLM 端点 180s 阻塞体验 | waitbox 秒级倒计时 + Close 取消（代次丢弃，`s_pp_busy_gen` 释放规则见 §3.2） |
| R7 | COMPOSE 草稿不落盘 | v1 取舍（§5）；真机试用后决定是否补 `/penpal.draft` |
| R8 | 明文 http 传输 key/信件 | 测试环境接受；生产部署换 https（客户端已支持，TLS 策略复用现有开关） |
| R9 | ~~null 笔友残留线程不可读~~ **已解决**（服务端 2026-08-22 上线 `pen_pal_id` 可选，demo ⑪ + §2.1 GET-only 复测 200/400） | 客户端恢复"显示 + 只读"（§4.1/§4.4/§5，随实现 commit 2）：HOME 显示 null 行、THREAD `thread_root_id` 单参只读打开 |

## 9. commit 拆分预案（实现阶段）

> **落地记录（2026-08-22）**：实际 5 个 commit——R9 服务端中途上线，在 1/2
> 之间插入了客户端恢复 commit。真机回归（§7）与评审申请
> `b48f584..5329383` 待办/在途。

1. `penpal: API client for the pen-pal service` —— penpal_api + 配置链 + env.cfg.example
   （含 §2.2 幂等键生成/复用/作废、thread_root_id 锚点封装、响应头
   `Idempotent-Replayed` 透传）✅ `b48f584`
   - 插入 `16c13e3`：R9 上线后的客户端恢复（§4.1/§4.4/§5 null 行"显示 +
     只读"），设计同步 v3.2
2. `penpal: screen UI - mailbox/compose/thread pages` —— ui_penpal×3 + poll 挂接
   ✅ `b231dd3`
3. `penpal: menu icon + third menu page` —— img_penpal + ui_deckpro 菜单
   ✅ `5329383`
4. `docs: penpal implementation notes + review request` —— README/TODO/CHANGELOG +
   `docs/reviews/wifi-config-keyboard-review-request-<首>..<末>.md`（含 §3.2 单队列
   偏差与 waitbox Close=取消首例说明、§7 回归清单）

## 变更历史

- 2026-08-21 v1 初稿（待评审）。
- 2026-08-21 v2 修订（落实 kimi 评审 `penpal-design-review-result-kimi.md` 的
  **C 部分接受**结论）：
  - **前置 1（§1.1）**：§6 菜单页数公式改最大下标语义 `(MENU_BTN_NUM-1)/9`，
    补登按钮创建循环 `i/9` 三路分派与第 3 页点两处连带改动；18 项存量 off-by-one
    同日由 `de78338` 独立修复（评审申请 `de78338`）。
  - **前置 2（§1.2）**：LLM 端点客户端超时 120s → **180s**（§2/§3.2/§4.4/R6）。
  - **前置 3（§1.3）**：§3.2 增 `s_pp_busy_gen` 与 busy 释放规则。
  - 同批：§1.4 结果 `type` 字段 + 单队列偏差登记（§3.2）；§1.5 env.cfg 8 槽容量
    说明 + base 限长 96→95（§3.4/§4.7）；§1.6 数据模型三处（`last_sender[24]`、
    null→0 哨兵、16KB 逐出规则，§5）；§1.8 ⑩ profile 端点登记暂不接入（§2）；
    §1.9 waitbox Close=取消全仓首例标注（§3.2，commit 4 申请书复标）；§1.10
    §3.2 草稿文字定稿为直陈句；§1.11 五处措辞（README 归因 `examples/pda2/
    README.md:78`、图标脚本"新增"而非"复用"、≥50 单位统一为字符、HOME 未配置
    行为、chat 指代落实为 `ui_ai_chat.cpp`）；R3 由"待评审偏差"降级为"遵循
    单槽先例"（§1.7 勘误）。跟踪项 T1-T5 全部落入正文，T6 已由 kimi 文件本身登记。
- 2026-08-22 低危补句（kimi v2 复审 `penpal-design-review-result-kimi-v2.md`
  **A 全量接受**，§3 四项 Low 预铺入文）：§3.2 两段状态行文案 + 串行两段 busy
  保持窗口；§4.4 非我信 Fix/Polish DISABLED；§4.5 FB 页按 `type` 选布局；
  §6 `config_keys.h.example` 模板行。Codex v2 复审
  （`penpal-design-review-result-codex.md`，**C 部分接受**）P1 发信幂等键 /
  P2 canonical subject 待 v3 修订处理——幂等键方案已提交服务端排期。
- 2026-08-22 **v3 修订**（关闭 Codex v2 复审两项 + 跟进服务端演进）：
  - **P1（发信幂等）**：服务端已实现 `Idempotency-Key`（32 hex，重放 200 +
    `Idempotent-Replayed: true`，demo `:24-30/:121-143` 实证）→ 新增 §2.2
    契约小节；§3.2 waitbox Close 按类型拆分——SEND 为"不再等待，后台继续"
    （busy 保持、结果照常消费），读/算型维持取消；幂等键客户端生命周期
    （RAM 快照比对复用、编辑换新、确认作废、不落 NVS）。
  - **P2（canonical subject）**：§4.2 Title 限长"60 字符"→"**56 字节**，
    UTF-8 边界"（`Re: `+56=60 ≤ 64 显示缓冲）；§5 subject[64] 明确为显示
    拷贝、发送侧 canonical `std::string`。
  - **服务端演进跟进（2026-08-21 线程模型）**：线程取数/回信锚点从
    subject 通道改 **`thread_root_id`**（subject = 兼容通道、会跨线程合并——
    同题多线程场景 v2 设计取数错误，属正确性修复）；§2 表/§2.1 schema ⑨④③
    补 `thread_root_id` 字段；⑦ 回信 body 带锚点 + `Re: ` 双保险；
    §5 `pp_thread_row_t` 加 `root_id`。
  - ⑩ `/users/me/profile` 已上线（demo 步骤⓪），仍暂不接入（本人≠笔友，
    §4.6/R2 措辞同步）；§7 回归补幂等重放/同题双线程/Title 字节截断三项。
- 2026-08-22 **v3.1 修订**（Codex v3 复审 `penpal-design-review-result-codex-8109c9e.md`
  **C 部分接受**，两项边界定稿）：
  - **P1（后台 SEND 误清新草稿）**：SEND 在飞期间 COMPOSE 编辑锁（Title/Body/
    Pick/Tips `LV_STATE_DISABLED`，Close 不解锁）；结果消费比对 payload 快照
    才清空（双保险），不等则保留草稿单独提示 `previous send ok`；§7 补
    "Close→编辑被锁→旧结果成功"回归。
  - **P2（null 笔友残留行无已定义查询）**：null 行不带 `pen_pal_id`、仅
    `thread_root_id` 查询（按 key 授权），**未实测**——登记为 §7-2 服务端
    预验前置；拒绝则回落 subject 兼容通道只读展示，两路皆拒登记服务端需求。
    **同日实测定稿**（服务器恢复运行，GET-only）：锚点单查 422（`pen_pal_id`
    "Field required"）、`pen_pal_id=0` 400、subject 单查同样 422、正常锚点
    查询 200 → 主路与回落路均不可用，改为 **HOME 过滤 null 行 + R9 服务端
    需求**（`pen_pal_id` 改可选，§8）；§4.1/§4.4/§5/§7 同步。
- 2026-08-22 **v3.2 修订**（服务端 R9 上线跟进，非评审驱动）：
  - 服务端 `GET /emails` 的 `pen_pal_id` 改**可选**（`scripts/remote_api_demo.py`
    步骤⑪；GET-only 复测：`?thread_root_id=50` 单参 200 + 响应 `pen_pal_id=null`、
    两参皆缺 400）→ §2/§2.1 契约更新；v3.1 的"HOME 过滤 null 行"过渡方案
    退役，恢复"**显示 + 只读**"（§4.1/§4.4/§5/§7 同步、§8 R9 关闭）；随实现
    commit 2 落地（`penpal_get_thread` 在 `pen_pal_id<=0` 时省略该参数）。
- 2026-08-22 **实现落地**（§9 全部完成，代码不再变 Design-follow）：
  `b48f584`（API client）→ `16c13e3`（R9 恢复，即上条）→ `b231dd3`
  （屏幕 UI 三件）→ `5329383`（菜单第 3 页 + 图标）+ docs/评审申请 commit。
  编译/烧录/开机冒烟通过；§7 真机回归清单与评审申请
  `docs/reviews/wifi-config-keyboard-review-request-b48f584..5329383.md`
  待用户实测/评审在途。
