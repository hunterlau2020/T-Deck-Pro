# 笔友（PenPal）App 设计文档

> 状态：**v1 草案，待设计评审**（2026-08-21）。评审通过后按 §9 commit 拆分预案实施。
> 参考实现接口：`scripts/remote_api_demo.py`；API schema 于 2026-08-21 对本地测试服务器
> `http://127.0.0.1:8000` 实测确认（curl GET 探测，见 §2）。

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
（demo 脚本 timeout 180s）→ 客户端这三类用 **120s** 超时，其余 CRUD 用 **20s**。

| # | 端点 | 方法 | 用途 |
|---|---|---|---|
| ① | `/pen-pals` | GET | 笔友列表（首页 icon 行） |
| ② | `/topics/suggestions` | GET | 推荐写作主题（按年龄段题库） |
| ③ | `/emails` | POST | 写信（body 带 `topic_id` 可选） |
| ④ | `/emails?pen_pal_id&subject` | GET | 读同主题往来信（线程） |
| ⑤ | `/emails/{id}/correction` | POST | 纠错（仅自己的信） |
| ⑥ | `/emails/{id}/polish` | POST | 润色（仅自己的信） |
| ⑦ | `/emails`（subject 带 `Re: ` 前缀） | POST | 回信 |
| ⑧ | `/emails/{id}/tips` | POST | 回信建议（基于最近一封收到的信） |
| ⑨ | `/emails/mailbox` | GET | 信箱线程概览（首页列表） |

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
[{"pen_pal_id":14,"subject":"Learning English: my story","counterpart":"Mei",
  "counterpart_kind":"npc","count":2,"unread":1,"state":"pending",
  "last_sender":"Mei","last_at":"2026-08-20T15:10:01","snippet":"Dear Hunter,..."}]
```
- `state` 实测取值：`pending`（等回信）/ `replied`（已回）/ `sent`（已发出）；
- `counterpart` = 对端显示名（首页行的"发送者名称"用它 + `last_sender`）；
- **`pen_pal_id` 可为 `null`**（笔友关系已删但线程残留，实测 Sophie 行）→
  线程打开时只读、隐藏回信按钮（§4.4）。

**④ thread**：
```json
{"pen_pal_id":14,"emails":[
  {"id":50,"sender_user_id":1,"receiver_user_id":null,"npc_id":3,
   "subject":"Learning English: my story","content":"Dear friend, ...",
   "parent_email_id":null,"is_npc_reply":false,"sender_name":"hunter",
   "created_at":"2026-08-20T14:11:11","read_at":null,"topic_id":7}, ...]}
```
- `sender_user_id != null` → 我写的信（纠错/润色按钮的判定依据）；
- **`Re: ` 前缀线程服务端聚合**（实测：以 `"Learning English: my story"` 查询，
  返回了 subject 为 `"Re: Learning English: my story"` 的信）→ 客户端不需要
  双查询/前缀归并，直接按 mailbox 行的 `subject` 取线程；
- emails 数组**升序**（旧→新）→ UI 侧反转后 index 0 = 最新。

**③ send 请求/响应**：`{"pen_pal_id":N,"subject":"...","topic_id":N|null,"content":"..."}`
→ `{"email":{"id":N,"topic_id":N,...},"reply_pending":bool}`。

**⑤ correction**：`{"degraded":false,"corrections":[{"type":"...","from":"...",
"to":"...","explanation":"..."}]}`

**⑥ polish**：`{"degraded":false,"improved_email":"...",
"improvements":["..."],"topic_coverage":[{"question":"...","status":"..."}]}`

**⑧ tips**：`{"degraded":false,"tips":["..."]}`

- `degraded=true` 表示服务端 LLM 降级，FB 页/ msgbox 标注 `(degraded)`。

## 3. 总体架构

### 3.1 单屏多页（不用 scr_mgr push/pop）

一个 `SCREEN_PENPAL_ID`，内部 7 个页面用 `LV_OBJ_FLAG_HIDDEN` 切换
（weather 三页模式的扩展，符合 README "Use pagination, not scrolling"）：

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
  + 1 个页面代次 `s_pp_gen`（entry/destroy/取消时 +1）。
- 每次请求 `xTaskCreate`（栈 1024×8、优先级 1，契约 §2.6）：任务持有**自有快照**
  （base/key/参数全部拷贝，`new` → 任务内 `delete`），JSON 解析也在任务线程完成，
  结果结构体 `new` 后 `xQueueSend` 移交 UI，UI 消费后 `delete`。
- 结果首字段 `gen`；代次不匹配一律丢弃（迟到结果、离屏结果、取消后结果）。
- UI busy 期间拒绝新请求；队列创建失败 → 提示、不置 busy、不启动任务。
- waitbox（chat 同款秒级倒计时，EPD 友好）：所有网络请求弹出；
  **加 Close 按钮 = 取消**（gen++、busy=false、迟到结果丢弃）——correction/polish
  可能耗时 120s，不能锁死输入。
- 请求类型：`PALS / MAILBOX / THREAD / SEND / TOPICS / FIX / POLISH / TIPS`。
  HOME 进入时合并触发 PALS+MAILBOX 两个任务（busy 检查放宽为"同类请求 busy"
  才允许并发两条？**不**——契约 §2.7 以"UI busy 期间拒绝新请求"保证同一时刻
  最多 1 个在飞结果。HOME 刷新改为**串行两段**：先 PALS，结果回来后再发 MAILBOX，
  状态行提示两步进度。）

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
  → SPIFFS /env.cfg（PENPAL_BASE=... / PENPAL_KEY=...，env_secrets 解析，8 槽够用）
  → gitignored config_keys.h（PENPAL_BASE_DEFAULT_DEV / PENPAL_KEY_DEFAULT_DEV，
    本地开发填 PC 局域网 IP + hunter 测试 key）
  → 空默认（Cfg 页引导填写）
```

- 设备端通过 **CFG 页**修改（两个 textarea + Save + 状态行）。
- **偏差说明**：penpal 配置用单槽 NVS 写 + 错误提示，**不做** AI Config 的双槽原子
  保存——该项是测试服务器地址/非敏感 key，写失败重存即可，无"半新半旧配置可见"
  风险（base/key 无组合一致性要求）。评审申请中注明此偏差及理由。

## 4. 页面交互设计

通用：`lv_font_montserrat_14`、黑白配色、顶栏 y=0..32（Back 按钮 + 标题 + 页内
功能按钮，尺寸沿用 chat 的 54×30）。**键盘驱动导航 + 触摸可用**（现有各屏同级别：
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
│ │ Mei          08-20 15:10  [1new] │ │  行1：counterpart + last_at + unread
│ │ Learning English: my story  pend  │ │  行2：subject + state
│ ├──────────────────────────────────┤ │  5 行 × 34px；点行→THREAD
│ │ terry        08-20 14:33         │ │
│ │ usage test   sent                │ │
│ │ ...                              │ │
│ └──────────────────────────────────┘ │
│ [◀ Prev]      page 1/2      [Next ▶] │  底部翻页（超 1 屏才显示）
└──────────────────────────────────────┘
```

- 数据：mailbox 上限 **24 行**，每页 5 行；`last_at` 显示 `MM-DD HH:MM`
  （ISO 串截取，不转时区——服务端时间即本地时间）；
- 状态列：`unread>0` 显示 `[Nnew]`；`state` 缩写 `pend/repl/sent`；
- `pen_pal_id=null` 的行照常显示（THREAD 内只读）；
- 键盘：`+/-`（Sym/Alt 层）= 翻页，`\n` = Sync，`\b` = 返回菜单；
- Sync = 串行 PALS→MAILBOX 刷新（NPC 回信后手动拉取）。

### 4.2 COMPOSE（写信，new / reply 两模式）

```
┌──────────────────────────────────────┐
│ [Back] Write / Reply                 │
│ To: Mei                     [View]→PROFILE │
│ Topic: Learning English...   [Pick]→TOPICS │  (new 模式；topic 可不选)
│ Title: [____________________]        │  单行 textarea（≤60 字符）
│ ┌──────────────────────────────┐ ┌──┐│
│ │ Body (multi-line)            │ │Sen││  多行 textarea（≤1000 字符）
│ │                              │ │d  ││
│ └──────────────────────────────┘ └──┘│
│ 123 chars        [Tips](reply 才有)  │  字数标签
│ status line（校验/发送错误）         │
└──────────────────────────────────────┘
```

- **new 模式**：收件人 = 点 icon 的笔友；topic 可选；title 手填或由 TOPICS 带入。
- **reply 模式**：subject 预填 `Re: <线程 subject>`（服务端聚合已实测），无 Topic
  行，多 **Tips** 按钮（§4.2.1）。
- 发送门槛：title 非空、**body ≥50 字节**（英语信件按字符计；不足时状态行
  `Need 50+ chars (now N)`，不弹网络请求）；
- 键盘：打字进当前焦点框；`\t`（Alt+Enter）切换 Title/Body 焦点；Body 内 `\n` =
  换行；**发送只走 Send 按钮**（chat 屏既定语义）；`\b` 在空框时返回 HOME；
- 发送成功：清空 COMPOSE、回 HOME、自动触发 Sync（查看 NPC 回信需再等 ~2 分钟，
  用户手动 Sync）。

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
- 键盘：`+/-` 翻页，`\b` 返回；msgbox 打开时 `\n`=Use、其他键=Cancel（chat 确认框
  同款键盘语义）。

### 4.4 THREAD（对话界面，Gmail 式主题聚合）

```
┌──────────────────────────────────────┐
│ [|◀ Start] [< Prev] [Next ▶]    2/5  │  顶栏 3 按钮 + 计数
│ From: Mei        08-20 15:10         │  信头（我的信显示 To:）
│ ┌──────────────────────────────────┐ │
│ │ Dear Hunter,                     │ │  信正文（滚动容器）
│ │ Thank you for telling me about   │ │  触摸滚动 redraw-on-release +
│ │ Momo—what a wonderful name...    │ │  +/- 键滚动（chat 同款）
│ │                                  │ │
│ └──────────────────────────────────┘ │
│ [Fix] [Polish]        （我的信才有） │  底部条件按钮
│ [Reply]               （第 1 页才有）│
└──────────────────────────────────────┘
```

- 数据：线程信件**时间逆序**分页，**每页 1 封**，index 0 = 最新（首页）；
- `|◀ Start` = 回到 index 0；`< Prev` = 更旧一封；`Next ▶` = 更新一封；
  到边界时按钮置灰；
- `pen_pal_id <= 0`（null 行）：信头下提示 `pal removed - read only`，隐藏 Reply；
- **我的信**（`sender_user_id != null`）→ `Fix` / `Polish` → FB 页（异步 120s）；
- **第 1 页**（index 0）→ `Reply` → COMPOSE(reply)；
- 键盘：`+/-` 滚动正文，`\b` 返回 HOME；
- 单信正文 >4KB 截断加 `(truncated)`（UTF-8 边界，chat 同款）。

### 4.5 FB（纠错/润色结果）

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

- 服务端无详情端点（§2.1）→ 由 ①+⑨ 合成：`name`、`NPC pen pal` / `pen pal`
  （`is_npc`）、`status`、线程数（⑨ 中该 pal 的行数）、未读合计；
- 键盘：`\b` 返回 COMPOSE。

### 4.7 CFG（配置）

```
┌──────────────────────────────────────┐
│ [Back] PenPal Cfg                    │
│ Base URL: [http://192.168.x.x:8000 ] │  textarea（≤96 字符）
│ API key:  [89rg35eua2            ]   │  textarea（≤16 字符）
│ [Save]     status line（保存结果/    │
│            当前解析链来源）           │
└──────────────────────────────────────┘
```

- Save = NVS `penpal` 写 + 状态行报错（单槽，§3.4 偏差说明）；
- 打开时 textarea 预填当前生效值；键盘与 COMPOSE 一致（`\t` 切换焦点）。

## 5. 数据模型与内存预算

```cpp
typedef struct { int id; bool is_npc; char name[24]; char status[12]; } pp_pal_t;        // ≤3+2
typedef struct { int id; char title[64]; char tag[12];
                 char background[96]; char guiding[192]; } pp_topic_t;                   // ≤16
typedef struct { int pal_id; char subject[64]; char from[24]; char state[12];
                 int unread; int count; char last_at[20]; } pp_thread_row_t;             // ≤24
typedef struct { int id; bool mine; char sender[24];
                 char time[20]; string content; } pp_letter_t;  // 线程总预算 16KB、单信 4KB
```

- 静态数组 + `std::string` 正文（chat 同款堆分配）；估算峰值 <32KB，PSRAM/堆充足；
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
| `examples/pda2/src/img_penpal.c` + `scripts/gen_img_penpal.py` | 信封图标 50×50（2 字节/像素，`LV_IMG_CF_TRUE_COLOR_ALPHA`），脚本可复再生 | ~60 |

集成点（改动现有文件）：

| 文件 | 改动 |
|---|---|
| `ui_deckpro.h` | enum 追加 `SCREEN_PENPAL_ID` |
| `ui_deckpro.cpp` | ① menu_btn_list 追加第 19 项 `{SCREEN_PENPAL_ID, &img_penpal, "PenPal", 23, 13}`；② **菜单第 3 页**：新增 `menu_screen3`（复制 screen2 样式、默认 HIDDEN）、`page_num = MENU_BTN_NUM/9` 改为向上取整 `(MENU_BTN_NUM+8)/9`、`menu_get_gesture_dir` 加 `page_curr==2` 分支、ui_Panel4 页点 2→3；③ `scr_mgr_register(SCREEN_PENPAL_ID, &screen_penpal)` |
| `factory.ino` | `loop()` 追加 `penpal_keyboard_poll()` |
| `env.cfg.example` | 追加 `#PENPAL_BASE=` / `#PENPAL_KEY=` 注释行 |
| `src/assets.h` | `LV_IMG_DECLARE(img_penpal)` |
| `config_keys.h`（本地） | 追加两个 `*_DEV` 定义（gitignored，不入库） |

## 7. 验证计划

1. **编译**：`python -m platformio run -e pda2`。
2. **服务端预验**（PC 侧，**GET-only，不写测试数据**）：
   `curl -H "X-API-Key: <key>" http://<PC 局域网 IP>:8000/api/v1/pen-pals` 通过；
   后端监听 0.0.0.0、Windows 防火墙 8000 入站放行（**用户侧动作**）。
3. **烧录** COM5（先停串口监控——既定流程）。
4. **真机回归清单**（评审申请 §验证状态）：
   - Cfg 保存 → 状态行确认；重进屏值保留（NVS 生效）
   - HOME 自动/手动 Sync：icon 行 + 线程列表正确显示（对照网页端）
   - 点 Mei icon → COMPOSE：不选 topic 直接写；body <50 字被拒；≥50 发出成功
   - 选 topic → suggestion msgbox → Use 后 Title 自动带入
   - THREAD：3 个导航按钮、Fix/Polish（我的信）、第 1 页 Reply
   - 发信后 ~2 分钟 Sync → NPC 回信出现 → 开线程 → Reply + Tips
   - `pen_pal_id=null` 行（Sophie）只读
   - 断网（关热点）下各操作报错不卡死；waitbox Close 可取消
5. **互通**（可选）：Cfg 改 terry key，重复关键路径。

## 8. 风险与开放问题

| # | 风险/问题 | 处置 |
|---|---|---|
| R1 | 后端字段后续变化（非合同化 API） | 解析全部防御式（字段缺失→默认值+串口日志），显示层降级不崩溃 |
| R2 | 无 profile 端点 | PROFILE 页合成（§4.6），后端补端点后升级 |
| R3 | 配置单槽 NVS（偏离 AI Config 双槽先例） | 设计决策已记录（§3.4），评审重点确认项 |
| R4 | montserrat_14 无 CJK 字形 | 信件/界面按英语设计（KET/IELTS 场景），terry 若写中文显示为方块——与其他屏现状一致 |
| R5 | mailbox 服务端无界增长 | 客户端 24 行上限 + 分页，超出提示 `more on web` |
| R6 | LLM 端点 120s 阻塞体验 | waitbox 秒级倒计时 + Close 取消（代次丢弃） |
| R7 | COMPOSE 草稿不落盘 | v1 取舍（§5）；真机试用后决定是否补 `/penpal.draft` |
| R8 | 明文 http 传输 key/信件 | 测试环境接受；生产部署换 https（客户端已支持，TLS 策略复用现有开关） |

## 9. commit 拆分预案（实现阶段）

1. `penpal: API client for the pen-pal service` —— penpal_api + 配置链 + env.cfg.example
2. `penpal: screen UI - mailbox/compose/thread pages` —— ui_penpal×3 + poll 挂接
3. `penpal: menu icon + third menu page` —— img_penpal + ui_deckpro 菜单
4. `docs: penpal implementation notes + review request` —— README/TODO/CHANGELOG +
   `docs/reviews/wifi-config-keyboard-review-request-<首>..<末>.md`（含 R3 偏差说明
   与 §7 回归清单）

## 变更历史

- 2026-08-21 v1 初稿（待评审）。
