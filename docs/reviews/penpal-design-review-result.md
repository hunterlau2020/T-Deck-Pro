# 笔友（PenPal）App 设计文档评审结果

- **评审日期**：2026-08-21
- **设计稿**：[penpal-design.md](../penpal-design.md)
- **评审范围**：v1 草案全文（§1–§9，含变更历史）
- **评审结论**：**C 部分接受**（设计整体通过；§3.2 HOME 串行两段、§4.4 条件按钮可见性、§6 资源脚本路径三项需在实现阶段落地前补登到申请书）

---

## 1. Findings

### 1.1 异步 IPC 契约 8 条规则对照完全吻合

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:103-128`（§3.2）
- **验证**：
  - 对照 `docs/async_ipc_contract.md` 11 条硬性规则：队列深度 4 + busy 仅 UI 读写
    + busy 必须携带代次（`s_pp_busy_gen`）+ 结果首字段 `gen` + 代次不匹配一律丢弃 +
    每任务独占请求快照（`new` → 任务内 `delete`）+ 栈 1024×8 + 优先级 1 +
    `xQueueCreate` 失败不置 busy 不启动任务 + Close 取消 = gen++。
  - §3.2 末段自我纠正："HOME 刷新改为串行两段"——这是对契约 §2.7"同一时刻
    最多 1 个在飞结果"的显式回应（初稿想并发 PALS+MAILBOX 后改回串行）。
  - 任务类型枚举 `PALS / MAILBOX / THREAD / SEND / TOPICS / FIX / POLISH / TIPS`
    与契约 §1 表格的四类（wifi_test / time_sync / ai_test / chat_send）命名风格一致。
- **结论**：契约遵循度高，waitbox + Close 取消语义与 chat 屏同款，风险可控。

### 1.2 明文 HTTP 分派隔离 `http_utils.cpp` 的边界控制得当

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:130-145`（§3.3）
- **验证**：
  - `penpal_api.cpp` 自带 URL 前缀分派：`http://` → `WiFiClient`，`https://` → `WiFiClientSecure`。
  - 复用 `http_require_wifi()` 与 `http_get_tls_mode()`——WiFi 状态检查与 TLS 策略
    不重复造轮子，仅新增"明文通道"分支。
  - 不改 `http_utils.cpp`——避免波及已评审的 AI / Weather / WiFi 路径；属于合理的
    scope discipline（隔离未知格式的测试服务器）。
- **结论**：架构边界清晰；明文通道仅用于本地测试服务器（生产部署需走 https，§R8 已识别）。

### 1.3 R3 单槽 NVS 偏差：理由成立，匹配现有 weather / wifi 模式

- **严重性**：✅ 通过（含 §3.4 偏差说明）
- **位置**：`docs/penpal-design.md:147-162`（§3.4）+ §8 R3
- **验证**：
  - SECURITY.md §"Secrets architecture"明确：`ai` 双槽、`weather` 双槽；
    `wifi` / `holidays` 单槽。penpal 用 `penpal` 单槽与 `wifi` 同级别。
  - 偏差理由："base/key 无组合一致性要求 + 写失败重存即可"——经核对：
    - base URL 更新 + key 未更新：旧的 base + 新的 key（仍有效）或 新的 base + 旧的 key
      （仍有效）——不存在"半新半旧更糟"的状态。
    - 对比 AI Config 双槽：custom model 必须整组原子替换（base + key + model 三件套
      任一不一致即视为未保存），半成品状态对用户而言比 fallback 更危险。
  - `env_secrets.cpp` 接口 `env_get(key, out, outlen)` 不限 key 名；PENPAL_BASE /
    PENPAL_KEY 接入无需改 env_secrets.cpp。
- **结论**：偏差决策合理，建议在 commit message 中保留显式说明以便后续审计。

### 1.4 `pen_pal_id=null` 行 + 只读模式显式覆盖（实测驱动）

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:204`（§4.4）+ §2.1 ⑨
- **验证**：
  - §2.1 ⑨ mailbox 实测 Sophie 行的 `pen_pal_id=null`（笔友关系已删但线程残留）。
  - §4.4 显式响应：`pen_pal_id <= 0` → 隐藏 Reply + 信头下提示 `pal removed - read only`。
  - HOME §4.1 配套："`pen_pal_id=null` 的行照常显示（THREAD 内只读）"。
- **结论**：服务端实测已识别该异常路径并贯穿到 UI 三处（HOME / THREAD / Reply 按钮），
  处理完备。

### 1.5 THREAD 触摸滚动 + 单信正文截断复用 chat 既有机制

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:259-264`（§4.4）+ §4.4 "± 键滚动"
- **验证**：
  - §4.4 显式声明"触摸滚动 redraw-on-release + +/- 键滚动（chat 同款）"——引用
    commit 1f46630 既有机制。
  - 单信正文 >4KB 截断加 `(truncated)`（UTF-8 边界，chat 同款）—— §4.4 末段。
  - 与 chat 屏实现路径一致，无新增技术风险。
- **结论**：复用正确，触发机制与边界条件均与现有约定对齐。

### 1.6 数据模型预算 <32KB 峰值与 PSRAM 现实吻合

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:285-298`（§5）
- **验证**：
  - 静态数组上限：pal ≤5、topic ≤16、thread_row ≤24、letter 单信 4KB / 线程 16KB。
  - `std::string` 正文（chat 同款堆分配），峰值 <32KB。
  - 当前 pda2 RAM 47.5%（申请人 1.5 报告），留有充足余量。
- **结论**：内存预算合理，无溢出风险。

### 1.7 commit 拆分预案 4 步、依赖顺序正确

- **严重性**：✅ 通过
- **位置**：`docs/penpal-design.md:340-356`（§9）
- **验证**：
  - 顺序：API client → screen UI → menu icon → docs。
  - 依赖链：commit 1 是 commit 2 的前置（无 API 客户端无法跑通 UI 网络路径），
    commit 2 是 commit 3 的前置（无屏则 menu 项不可达），commit 4 收尾。
  - 与申请人前序 wifi-config-keyboard 评审要求的"5–7 commit 分段"一致。
- **结论**：拆分粒度合适，符合"先难后易 + 先基础后集成"的节奏。

### 1.8 asset / 资源脚本路径需与现有约定对齐

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:317`（§6 文件规划表 + scripts/gen_img_penpal.py）
- **触发场景**：实现 commit 3 时需要 `examples/pda2/src/img_penpal.c` + Python 生成脚本。
- **证据**：
  - 已确认 `examples/pda2/src/` 下有 24 个 `img_*.c`（img_A7682E / img_calculator / ...），
    命名规范 `img_<snake>`，符合设计 §6 路径。
  - `src/assets.h` 用 `LV_IMG_DECLARE(img_xxx)`——加 `LV_IMG_DECLARE(img_penpal)`
    一行即可，模式吻合。
  - **但 `examples/pda2/scripts/` 当前仅有 `ca_bundle_check.py` / `ca_bundle_check.sh`，无任何 `gen_img_*.py`**。
- **影响**：设计 `scripts/gen_img_penpal.py` "脚本可复再生"是新约定；当前 img_*.c
  的生成流程未文档化（可能是手工或外部工具）。
- **最小修复**：
  1. 在 commit 3 中同时提供 `gen_img_penpal.py` 并附 README 说明它与现有 24 个
     img_*.c 的生成关系（即使其他 24 个不是 Python 生成，也需说明本屏选择了 Python）。
  2. 或者改用 `tools/img_convert`（SquareLine Studio / LVGL 官方工具）一次性产出，
     与现有流程一致（需申请人核实当前 img_*.c 的实际生成方式）。

### 1.9 §3.2 HOME 串行两段 Sync 缺乏状态行 UX 说明

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:121-128`（§3.2 末段 + §4.1 状态行）
- **触发场景**：用户在 HOME 点 Sync → PALS 完成后 MAILBOX 才发起，期间状态行
  如何呈现两段进度。
- **证据**：
  - §3.2 末段："HOME 刷新改为串行两段：先 PALS，结果回来后再发 MAILBOX，状态行
    提示两步进度"——意图清晰但未细化文案。
  - §4.1 顶栏 + 状态行布局：`status line（同步状态 / 错误）`，未明确两段文本格式。
- **影响**：用户可能看到 `Sync: pals...`，等待 1-2 秒后无新提示（MAILBOX 阶段是否
  在跑不清楚），再次点击 Sync 触发重复请求（§3.2 已 busy 拒绝，但 UI 反馈滞后）。
- **最小修复**：
  1. 状态行分两段文本：`PALS OK, syncing mailbox…` → `Mailbox OK`（或失败码）。
  2. 或者将 MAILBOX 阶段直接并入 PALS 任务——服务端一次返回 pal + 列表（需后端配合，
     当前 §2 端点表无此组合，**本评审建议先按串行实现**）。

### 1.10 §4.4 Fix/Polish 按钮可见性规则未明确

- **严重性**：Medium
- **位置**：`docs/penpal-design.md:270-273`（§4.4 底部条件按钮）
- **触发场景**：在第 2 / 3 / ... 页翻看"对方来信"时，Fix/Polish 按钮是否可见/可点。
- **证据**：
  - §4.4 描述："底部条件按钮 [Fix] [Polish] （我的信才有） / [Reply] （第 1 页才有）"。
  - "我的信"判定明确为 `sender_user_id != null`，但"才有"二字的 UI 行为是
    hide / disable / 灰显未说明。
  - 对比 chat 屏 msgbox 既有约定倾向于"按钮一直显示但 disable"。
- **影响**：实现时三种选择（hide / disable / 灰显）行为差异显著；用户预期"对方来信
  不能 Fix"是否需要提示（避免以为按钮坏了）。
- **最小修复**：
  1. 显式声明：非自己信 → 按钮 `lv_obj_set_state(..., LV_STATE_DISABLED)` + tooltip
     "Only your letters can be corrected"（或同等文案）。
  2. 或者 hide（节省底部空间，但破坏按钮位置稳定性）。

### 1.11 §4.2 写信 body ≥50 字节门槛与 chat 屏 200-char 限不一致

- **严重性**：Low
- **位置**：`docs/penpal-design.md:215-218`（§4.2 发送门槛）
- **触发场景**：用户从 chat 屏切到 COMPOSE 写信时，对"最低字数"预期不一致。
- **证据**：
  - §4.2："发送门槛：title 非空、body ≥50 字节（英语信件按字符计；不足时状态行
    `Need 50+ chars (now N)`）"。
  - §4.2 末段："发送只走 Send 按钮（chat 屏既定语义）"。
  - chat 屏最低门槛待补查（评估已知 ≥200 字符，但本评审未独立核实）。
- **影响**：跨屏阈值不一致可能让用户困惑；50 字节仅约 10 词英语，对 KET 写信练习
  是否足够需 UX 决策。
- **最小修复**：
  1. 在 §4.2 加一行说明 "50 字节 = KET 写作最短篇幅 ~10 词，对应试场景" 或明确
     "≥200 字符以对齐 chat 屏"。
  2. 状态行 `Need 50+ chars (now N)` 中 N 的单位（字节/字符）需明确，否则
     "字符数"与"字节数"对用户而言都是"字"。

### 1.12 §4.1 last_at 截取 `MM-DD HH:MM` 隐含 ISO 格式依赖

- **严重性**：Low
- **位置**：`docs/penpal-design.md:189`（§4.1 + 状态列说明）
- **触发场景**：服务端 `created_at` 字段格式若变更（如改为 epoch 或 RFC3339 + tz），
  客户端 substring 截取可能产生乱码或越界。
- **证据**：
  - §4.1："`last_at` 显示 `MM-DD HH:MM`（ISO 串截取，不转时区——服务端时间即本地时间）"。
  - §R1 "后端字段后续变化（非合同化 API）" 已识别该风险。
- **影响**：低概率但破坏 UI 体验（last_at 显示乱码导致线程列表不可读）。
- **最小修复**：
  1. §R1 处置已写"解析全部防御式（字段缺失→默认值+串口日志）"，但 `last_at` 长度
     校验未明示——建议补：`strlen(last_at) >= 16` 才截取，否则显示原串。
  2. 长期：建议与后端约定 `last_at` 固定 ISO 8601 无时区格式，并写入
     `docs/api_contract.md`（如不存在则新建）。

### 1.13 §4.5 FB 页标题 Correction / Polish 切换机制缺失

- **严重性**：Low
- **位置**：`docs/penpal-design.md:281`（§4.5 顶栏）
- **触发场景**：用户在 THREAD 点 Fix → FB 页打开后，标题写死 "Correction / Polish"
  中的哪一个？任务结果如何区分显示 layout？
- **证据**：
  - §4.5 顶栏 `[Back] Correction / Polish   (degraded?)`——意图动态切换。
  - 但 FB 页只有一个滚动容器，correction 与 polish 的 JSON 结构差异显著（correction
    是 corrections 数组 vs polish 是 improved_email + improvements + topic_coverage）。
- **影响**：实现者需自行决定 FB 页是双模板切换还是共用滚动容器 + 不同渲染分支。
- **最小修复**：§4.5 加一行："任务类型（FIX/POLISH）由 `s_pp_q` 结果 `kind` 字段区分，
  FB 页 `entry()` 据此选择 layout（correction 列表 vs polish 三段：原文 / improvements /
  coverage）。"

### 1.14 §3.4 一次性配置引导：config_keys.h 在 gitignore 内对新人 onboarding 不友好

- **严重性**：Low
- **位置**：`docs/penpal-design.md:147-162`（§3.4）+ README 缺位
- **触发场景**：首次 clone 仓库的开发者运行 `platformio run` 时，penpal 功能编译失败
  （缺 `PENPAL_BASE_DEFAULT_DEV` / `PENPAL_KEY_DEFAULT_DEV`）。
- **证据**：
  - §3.4 第 3 层：`config_keys.h`（本地）—— gitignored（参考 SECURITY.md）。
  - 现有 `config_keys.h` 模板内 `OWM_API_KEY` / `AI_KEY_DEFAULT_DEV` 已有示例，
    复制时不会被空定义打断编译。
- **影响**：新开发者编译时 penpal 模块报未定义错误（虽然 gitignored 的目的是防止
  密钥入库，但缺 default 值会让初次编译失败）。
- **最小修复**：
  1. `config_keys.h` 模板内 `PENPAL_BASE_DEFAULT_DEV` / `PENPAL_KEY_DEFAULT_DEV`
     留空定义 + 注释指引（与现有 `GEMINI_API_KEY` 注释形式一致）：
     `// #define PENPAL_BASE_DEFAULT_DEV "http://192.168.x.x:8000"`
  2. README 或 penpal-design.md §3.4 补一句"首次构建请复制 config_keys.h.example，
     按 §3.4 注释填入 PENPAL_*_DEV"。

### 1.15 §7 真机回归 COM5 硬编码

- **严重性**：Low
- **位置**：`docs/penpal-design.md:319-326`（§7 烧录）
- **触发场景**：用户在不同 USB 端口 / 不同 OS 上烧录时 COM5 不存在。
- **证据**：
  - §7 步骤 3："烧录 COM5（先停串口监控——既定流程）"。
- **影响**：与既有平台无关，申请人按本地约定即可——评审只需提醒"非 COM5 环境需改
  platformio.ini upload_port"。
- **最小修复**：§7 加一句"非 COM5 环境请改 `platformio.ini` 的 `upload_port` 后
  执行 `pio run -e pda2 --target upload`"。

---

## 2. 已通过项汇总

- §2 API 实测 schema（pen-pals / topics / mailbox / thread / correction / polish / tips）
- §3.1 单屏 7 页 HIDDEN 切换 + 单 keyboard poll + 单代次
- §3.2 异步 IPC 8 条规则完全吻合（含 HOME 串行两段自我纠正）
- §3.3 `penpal_api.cpp` 自带 URL 前缀分派，`http_utils.cpp` 不改
- §3.4 R3 单槽 NVS 偏差 + SECURITY.md 配置链 4 层（NVS / env.cfg / config_keys.h / 默认）
- §4.1 HOME：icon 行 + 线程列表 + 翻页 + 状态列 + 串行 Sync
- §4.2 COMPOSE：new / reply 双模式 + topic 可选 + title/body 双 textarea
- §4.3 TOPICS：suggestion msgbox + Use 带回 title + Cancel 不破坏
- §4.4 THREAD：3 按钮导航 + 触摸滚动（chat 同款）+ deleted-pal 只读
- §4.6 PROFILE：①+⑨ 合成（无 profile 端点的妥协方案）
- §4.7 CFG：base URL + API key + 状态行
- §5 数据模型预算 <32KB
- §6 文件规划 6 个新文件 + 6 处现有文件改动
- §8 R1/R3/R4/R7 风险已识别并有处置

## 3. 跟踪项表（已识别但不阻塞本批实施）

| # | 项 | 来源 | 跟踪至 |
|---|---|---|---|
| T1 | `scripts/gen_img_penpal.py` 引入新约定，与现有 img_*.c 生成流程未对齐 | §1.8 | commit 3 落地前 |
| T2 | `last_at` ISO 格式依赖（服务端未合同化） | §1.12 / §R1 | 与后端约定写入 api_contract |
| T3 | FB 页 Correction / Polish 布局切换机制 | §1.13 | commit 2 实现时 |
| T4 | config_keys.h 新人 onboarding 引导 | §1.14 | README 补登 |
| T5 | COM5 硬编码与跨平台 | §1.15 | §7 文档补登 |
| T6 | menu_get_gesture_dir 加 `page_curr==2` 分支的精确写法（基于现有 `page_curr < page_num` 门逻辑，page_num 由 (MENU_BTN_NUM+8)/9 重算为 3） | §6 集成点 ② | commit 3 实现时 |

## 4. 验证说明

- 本评审基于静态阅读 + 仓库现状核查：
  - `docs/async_ipc_contract.md`（11 条硬性规则 + 适用范围）
  - `docs/SECURITY.md` + `examples/pda2/env.cfg.example` + `examples/pda2/config_keys.h`（配置链 4 层）
  - `examples/pda2/env_secrets.cpp`（任意 key 名 + ENV_MAX_ENTRIES=8）
  - `examples/pda2/ui_deckpro.cpp:249-281`（menu_btn_list 18 项 9/9 布局）
  - `examples/pda2/ui_deckpro.cpp` scr_mgr_register 既有 31 项 + `src/assets.h` 19 项 LV_IMG_DECLARE
  - `examples/pda2/src/` 24 个 img_*.c 文件命名规范
  - factory.ino `XXX_keyboard_poll()` 模式（9 个既有 poll）
- 当前环境无 `platformio` / 真实硬件，未独立复现固件编译与真机回归。
- 申请人 `scripts/remote_api_demo.py` 端点实测结果（§2 schema）未独立复跑 curl，
  但实测数据格式与 §3 解析路径自洽。

## 5. 审批意见

- [ ] A. 全量接受
- [ ] B. 仅保留设计稿
- [x] C. **部分接受**
- [ ] D. 拆分提交

**接受范围**：§3 架构 / §4 页面交互 / §5 数据模型 / §6 文件规划整体可接受；§9 commit
拆分预案的依赖顺序（API → UI → menu → docs）可作为实现阶段基线。

**前置条件（commit 1 落地前必须满足）**：

- §1.9 HOME 串行两段 Sync 状态行文案明确（commit 1 / 2 之间）
- §1.10 Fix/Polish 按钮 disable / hide 行为确定（commit 2 落地前）
- §1.8 `scripts/gen_img_penpal.py` 路径与现有 img_*.c 生成方式对齐（commit 3 落地前）

**遗留项**：

- §1.11 / §1.12 / §1.13 / §1.14 / §1.15 已登记 §3 跟踪表，实现阶段补登即可。
- §8 R8 明文 HTTP 上生产前必须切 https（客户端已支持，复用 `http_get_tls_mode()`）。

---

**评审人**：Codex（第三方静态复核视角，已对照 `docs/async_ipc_contract.md` 11 条规则、
`docs/SECURITY.md` 配置链、`examples/pda2/` 现有屏实现模式（chat / ai_cfg / weather /
wifi）逐项验证）。
