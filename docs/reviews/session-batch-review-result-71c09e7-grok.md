# 评审结果：会话批次 `71c09e7`（Grok）

- **评审日期**：2026-09-16
- **评审人**：Grok（Cursor）
- **申请文件**：[session-batch-review-request-71c09e7.md](session-batch-review-request-71c09e7.md)
- **评审对象**：`71c09e7`（父 `6eb8245`；27 文件 +4149/−1580）
- **工作树 HEAD**：`42ace3c`（`git diff 71c09e7 HEAD` 仅文档/诊断脚本，**本批代码 findings 仍存活**）
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**。本对象是**代码**（含新屏/新异步任务），不是设计稿——A/B/C 用 §2.3 **代码**口径；§2.5 收口规则不把 P2 升成新设计版，但 **A 不得挂 P0/P1**。
- **对照**：OTA 设计 v6 L1+A（[`ota-update-design-review-result-97a4f53-grok.md`](ota-update-design-review-result-97a4f53-grok.md)）实现合同 IC-1…IC-6，本批评 G-合入。
- **评审结论**：**C 部分接受**。无 P0。**1×P1**（缺 `verifyRollbackLater` → 层 2 回滚空转）须当批修；其余 P2 进 `issue_list.md` 绑后续 G-合入。未达 L2（无独立 `pio run`；P1 未闭合）。
- **G-合入**：否（P1 未修）。**G-真机 / G-发布**：否（申请已自陈 OTA §8 回滚矩阵未测）。

---

## 0. 区间文件归属（§0 原则 5 / §7.2①）

`git diff --name-only 6eb8245..71c09e7` 共 27 文件，全部受评：

| 文件 | 组 | 处置 |
|---|---|---|
| `examples/pda2/ca_bundle_full.h`、`scripts/gen_ca_bundle.py`、`scripts/extra_roots/*.pem`、`scripts/verify_chains_vs_bundle.py` | 1 CA | 受评 |
| `examples/pda2/ui_voice_ai.cpp` | 2 语音上下文 + 7 TTS | 受评 |
| `examples/pda2/factory.ino`、`peri_keypad.cpp`、`ui_deckpro.cpp` | 3 休眠 + 6 菜单 + 8 OTA UI | 受评 |
| `ui_penpal.cpp`、`ui_penpal.h`、`ui_penpal_write.cpp`、`penpal_api.*` | 4 PenPal + 5 Whoami API | 受评 |
| `ui_whoami.cpp` | 5 Whoami | 受评 |
| `ui_ai_cfg.cpp` | 7 开关样式 | 受评 |
| `ota_update.*`、`ota_trust_anchor.h`、`peri_gps.cpp`、`scripts/ota_*.py`、`env.cfg.example` | 8 OTA | 受评 |
| `.gitignore`、`scripts/capture_boot.py`、`scripts/extract_missing_roots.py` | 附属 | 受评（密钥 gitignore / 辅助脚本） |

HEAD 之后落入、**不评**：`CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` §18–21 / 本申请文件 / `scripts/{check_spiffs_dump,parse_ota_image,read_serial}.py`（本批之后的文档账本）。

**流程（不占缺陷）**：G-提交要求按模块拆 commit；申请写明用户要求单 commit。按组给 findings，不因拆分失败退回。申请缺 §7.1 验证状态表 / 回滚 / 审批栏——本轮仍审代码，不退回申请。

---

## Findings

### P1：未覆盖 `verifyRollbackLater()`，层 2 回滚在 Arduino 启动路径上不存在

- **组**：8 OTA
- **位置**：全仓 `examples/pda2` 无 `verifyRollbackLater`；设计稿组 4 要求写在 `factory.ino`；对照 `docs/ota-update-design.md` §9.5 与 v1/v3 已核框架 `esp32-hal-misc.c`（`verifyRollbackLater` weak 默认 `false`，`initArduino()` 在 `setup()` **之前**对 `ESP_OTA_IMG_PENDING_VERIFY` 调 `esp_ota_mark_app_valid_cancel_rollback()`）。
- **证据 / 反例**：
  1. `rg verifyRollbackLater examples/pda2` → 0 hits。
  2. 现有自证点在 `factory.ino:877-885`（loop 里 `mark_valid` + 关 WDT）。对 **USB 烧录**的已验证槽，该调用常返回 `ESP_OK`——申请引用的串口 `[OTA] self-attest: mark valid ok` **不能**证明 pending 镜像会等到 loop。
  3. 反例：OTA 写入一张在 `setup()` 里挂死的镜像 → `initArduino` 已自证 → TWDT 60s 复位后 bootloader **不回滚** → 坏镜像重启循环。层 2 文案（设计 §5.4）按现状推不出。
- **影响**：契约破坏（设计承重回滚地基缺失）。`B/2/静默 → P1`（需先完成一次 OTA 写槽才踩到；无错误信号）。回升 P0：G-发布前仍未覆盖且无 USB 兜底演练。
- **两轴**：`VERIFIED`（CODE：函数不存在）/ `VIOLATES`（设计 §5.1/§9 组 4；反例库「新固件已切换但自毁 → pending 未自证 → 回滚」）。
- **最小修复**：`extern "C" bool verifyRollbackLater() { return true; }` 放进 `factory.ino`（或独立 .c）；补 §8.1 自毁固件回滚真机（申请已标未测）。**不修则不得宣称层 2 可用、不得 G-发布。**

### P2-1：语音多轮上下文把「turn 对」当成 `(role, content)` 送进 API——记忆功能按正文必错

- **组**：2
- **位置**：`ui_voice_ai.cpp:315-358`（`vai_ctx_add_pair` / `vai_ctx_build`）→ `openai_api.cpp:641-642`（`role`/`content` 原样进 JSON）。
- **证据**：`vai_ctx` 元素是 `pair(user_text, assistant_text)`（`:329`），build 却做 `msgs[n].role = first`、`msgs[n].content = second`（`:354-355`）。第二轮起 history 形如 `{"role":"<用户原话>","content":"<助手回复>"}`，不是 `user`/`assistant` 轮次对。契约 §5.1 / 反例库「多轮上下文轮次配对」要求 role 为 `user`/`assistant`。
- **反例**：用户说「我叫李四」成功后，再说「我叫什么」→ 发出的 history role 是整句用户话，API 拒或当无效角色；申请「连续语音对话有记忆」从代码推不出。
- **影响**：`D/1/有声 → P2`（每次 follow-up；API 4xx 或胡答）。回升 P1：若产品把语音上下文当承重（与文字 Chat 同级）。
- **两轴**：`VERIFIED`（CODE）/ `VIOLATES`（`async_ipc_contract.md` §2.11 轮次配对精神；本组声称「整轮 (user, assistant) 对」）。
- **最小修复**：每个 turn 压两条 `("user", u)` + `("assistant", a)`；淘汰按**条**或按整轮成对裁；`vai_ctx.size()>2` 的「一次 erase 两个 turn」一并改掉。SIM：两轮对话串口/`messages` JSON。

### P2-2：`s_ota_inflight` 在 `xTaskCreate` **成功返回后**才置 1（OTA v6 IC-1 未落地）

- **组**：8
- **位置**：`ota_update.cpp:547-551`、`:574-579`；对照 PenPal `ui_penpal.cpp:529-537`（create **前** ++，失败 --；注释写明双核 worker 可能在 create 返回前跑完）。
- **反例**：无 WiFi 的 Check worker 在另一核于 `xTaskCreate` 返回前走到 `ota_task_exit`（`:265` `inflight=0`）→ UI 再写 `inflight=1` → 永久「previous OTA op closing」。申请自评最没把握②未覆盖此路径。
- **影响**：`C/2/有声 → P2`。回升：与 P2-3 叠成泄漏 + 卡死。
- **两轴**：`VERIFIED` / `VIOLATES`（契约单飞；v6 实现合同 IC-1）。
- **最小修复**：与 PenPal 同构，create 前原子 ++，失败 -- 并删快照。

### P2-3：`xQueueOverwrite` 不回收旧指针（IC-2 未落地）；申请②的「延迟但不丢」不成立

- **组**：8
- **位置**：`ota_update.cpp:270-273`；`ota_result_poll` `:583-590` 只能 `Receive` 槽里**现在**那一个指针。
- **证据**：FreeRTOS `xQueueOverwrite` 丢弃槽内旧 item、无回调。申请 §9②：「loop 被长阻塞只延迟 delete、不丢」——覆盖发生时 poll **看不见**旧指针，是泄漏不是延迟。
- **反例**：Check A 已 `Overwrite`+退出（inflight=0）且本拍 poll 未跑到 → 用户再 Check → worker B 再 `Overwrite` → A 的 `ota_result_t*` 永无 `delete`。
- **影响**：`B/2/静默 → P2`（泄漏一个结果结构；与 P2-2 叠则多次）。
- **两轴**：`VERIFIED` / `VIOLATES`（契约所有权；设计 §3.1「覆盖丢失须 delete」）。
- **最小修复**：overwrite 前 `xQueueReceive(..., 0)` 取出旧指针 `delete`。

### P2-4：OTA HTTPS 走 `http_apply_tls()`，继承 AI「Trust self-signed」

- **组**：8
- **位置**：`ota_update.cpp:315`、`:423`；`http_utils.cpp:37-43`（`HTTP_TLS_INSECURE` → `setInsecure()`）。设计 v6 §3.4「专用 `setCACertBundle`」。
- **说明**：清单 ECDSA 仍挡未签名固件，故不升 P1/P0。Trust 开时镜像明文可被中间人读取，且跳过 NTP（`:301-302`）。
- **影响**：`C/2/静默 → P2`（须打开 Trust）。回升 P1：若验签被绕过或明文宏生产开启。
- **最小修复**：OTA 客户端直接 `setCACertBundle(CA_BUNDLE_MOZILLA)`，忽略 `s_tls_mode`；Trust 开时 OTA 仍校验证书。

### P2-5：5 分钟自动休眠在 OTA 下载期间仍会 `scr_mgr_push(Sleep)`

- **组**：3×8
- **位置**：`ui_deckpro.cpp:4977-4992` 不看 `ota_busy()`；拒绝真睡只在 `sleep_do_enter` `:4819-4822`。
- **反例**：下载覆盖层「DO NOT POWER OFF」→ 无按键/触摸打点 → 5 分钟推 Sleep。倒计时结束 `sleep_do_enter` 因 busy 返回，设备不深睡，但 Sleep 屏压进栈；idle 定时器还会按窗口再 push。下载继续，UI 乱。
- **影响**：`C/2/有声 → P2`。不升 P1：电源轨未被 `esp_deep_sleep_start` 拉断。
- **最小修复**：`idle_sleep_timer_cb` 在 `ota_busy()` 或已在 SCREEN11 时直接 return（可重计时，勿 push）。

### P2-6：OTA / Whoami 新异步任务未写入 `async_ipc_contract.md`

- **组**：5、8
- **位置**：契约表仍只有 WiFi Test / Time Sync / AI Test / AI Chat；设计 §6 要求同批加 OTA 行；Whoami 为新 HTTP worker。
- **影响**：`B/2 → P2`（§4.3 `NO_CONTRACT`：新型异步任务须同轮补契约或显式例外）。
- **最小修复**：契约表加 SCREEN2_2 / Whoami 行：指针队列、drain 点、inflight、Whoami 队列永删例外、OTA overwrite 纪律。

---

## 实现合同（v6 IC → 本批）

| ID | 目标 | 本批 | G-合入 |
|---|---|---|---|
| IC-1 inflight 时序 | create 前 ++ | **未过**（P2-2） | 待 |
| IC-2 overwrite 旧指针 | overwrite 前 delete | **未过**（P2-3） | 待 |
| IC-3 确认→Update | 无 UAF | **过**：`ota_start_async` 深拷进 `snap_t` 后 `delete snap`（`:567-570`） | 过 |
| IC-4 poll 接线 | factory loop 无条件 drain | **过**：`factory.ino:888-891` | 过 |
| IC-5 `drv.begin` while(1) | 失败继续 | **未过**：`factory.ino:763-765` 仍死循环（WDT 窗口内不喂狗 → 60s 复位）。可达性 3 | 待（P3 台账） |
| IC-6 喂狗 / T | SPIFFS format + 校准 | 部分：SD/EPD/GPS/A7682E/PCM 有喂；`SPIFFS.begin(true)` `:786` 无。T=60s 声明「2×30s」与本板 `_busy_timeout=10s`（`GxEPD2_310_GDEQ031T10.cpp:17`）不符；余量偏大，校准仍缺 | 待 |

---

## 已通过项（按组；独立走查，非转述申请）

- **组 1 CA**：extra_roots 指纹钉在 `gen_ca_bundle.py` `EXTRA_ROOT_SHA256`；DigiCert 2006 PEM 为公开根，非设备私钥。换根 USB 语义与 SECURITY 一致。六端点脚本未在本环境复跑 → BUILD/脚本 `UNKNOWN`，逻辑 `PARTIALLY_VERIFIED`。
- **组 4 PenPal**：失败改 `pp_msgbox_show`（`:700` 一带）；非 ASCII → simsun（`:168-178`）；reply 三路锁 title（`ui_penpal_write.cpp:151-156` / focus 回调 `:528-545` / `ppw_lock_edit` 解锁后重套 `:132-133`）。方向与设备报告同向。HW 仍 `CLAIM_ONLY`。
- **组 5 Whoami**：页先建、顶栏后建（`:675-747`）击穿「按钮被 240×320 盖住」；队列一次创建、`whoami_keyboard_poll` 先 drain（`:618-622`）击穿申请所述 `xQueueGenericSend` 断言；栈 8KB。`s_wa_gen` 在 entry ++ 但 consume **未使用**（destroy `(void)s_wa_gen`）——迟到 PROFILE 仍可写 `s_profile`，Nit，不单列。Cfg 保存调 `pp_notify_cfg_changed`（`:432`）清 PenPal autosync，自评③时序在「两屏不同时活跃」下可接受。
- **组 6 菜单**：Whoami 在第一页 (95,189)；Wifi/Lora 槽位与申请一致。`SCREEN2_2_ID` / `SCREEN_WHOAMI_ID` 追加在枚举末尾（`ui_deckpro.h:81-83`），不插既有 ID 中间。
- **组 7 TTS**：`start_tts` 早退（申请路径）；开关绑 `LV_PART_INDICATOR \| LV_STATE_CHECKED`（`ui_voice_ai.cpp:697-698`，`ui_ai_cfg.cpp` 同款）。NVS `ai`/`tts_enabled`。HW `CLAIM_ONLY`。
- **组 8 OTA（未列缺陷的部分）**：六字段规范化 + 小写 hex + 禁 `read_signature`；`ota_sign.py` 与设备都把 size/seq 当十进制**字符串**（与设计示例 JSON 数字不同，但两端一致，合法清单可验）；`last_seq` 仅 `Update.end(true)` 后写；Content-Length 强制；失败 `abort`；公钥 65B `0x04`；私钥 gitignored；全屏吸收层；低电 `ota_shutdown_blocked` 安全阀；取号虽多一次整刷（见下 Nit）但仍是存变量比 done。

---

## Nit / 疑问（不占缺陷编号，≤3 + 疑问）

1. `factory.ino:819` 已 `disp_full_refr()`，`:825` `disp_full_refr_seq()` 再刷一次——注释「恰好一次」为假；多一次开机整刷，自证仍等第二号。
2. OTA Cancel（`ui_deckpro.cpp:1134-1143`）不调用 `ota2_2_show_idle()`，Check 按钮保持 HIDDEN；键盘 Enter 仍可重试。
3. `env.cfg.example:10` 仍写 Max 8/95，与 `ENV_MAX_ENTRIES=12` / `val[160]` 及新增 `OTA_URL` 不一致（v5 已登记、组 1 未改）。

疑问：本环境无 `~/.platformio`，未复跑 `pio run -e pda2`、未复跑 `verify_chains_vs_bundle.py`。申请 HW 串口无贴出原文 → 自证/CA 真机项 `CLAIM_ONLY`。

---

## 验证说明

- 静态走查 `71c09e7` 与当前工作树（实现文件无后续 diff）。
- 无 pio、无设备。BUILD/HW = `UNKNOWN`。脚本/指纹未独立哈希复算。
- TWDT `add(NULL)` / re-init 语义沿用前轮框架头文件 DOC，本机未再打开 IDF 源码。

---

## 对称三格（§7.2）

1. **全区间**：27 文件均归属；不只申请靶向。额外看到：语音 role 错位、idle-sleep×OTA、契约未登记、`verifyRollbackLater` 缺失（申请未列为已知缺口）。
2. **独立复跑**：未 `pio run`；未真机。SIM：pending 镜像 × 缺 weak 覆盖；`vai_ctx` 第二轮 JSON；overwrite 丢指针；idle 5min push Sleep。
3. **最没把握处反例**：
   - ① T=60s / V1.0 页 busy：本板默认 10s 不是申请写的 30s；缺校准维持 IC-6。未识别库内等待 >60s → 误回滚，在 **P1 修复前** 实际是「误复位但不回滚」。
   - ② overwrite：申请「不丢」被 P2-3 击穿。
   - ③ Whoami/PenPal NVS：两屏不同时活跃 + `pp_notify_cfg_changed` 可接受；不升缺陷。

---

## 声明矩阵（申请强声明）

| 声明 | 状态 |
|---|---|
| 层 2：自证前挂死 → WDT → 回滚 | **CONTRADICTED**（P1） |
| 真机自证 `mark valid ok` = 回滚可用 | `CLAIM_ONLY` 且与 USB 槽语义混淆 |
| 语音整轮 (user, assistant) 入史 | **CONTRADICTED**（P2-1） |
| overwrite 覆盖丢失由 poll 兜底、不泄漏 | **CONTRADICTED**（P2-3） |
| inflight 唯一出口 + 创建失败不递增 | 失败不递增 `VERIFIED`；成功后置位 **IC-1 未过** |
| 专用 CA、不继承 Trust | **CONTRADICTED**（P2-4） |
| factory loop 无条件 `ota_result_poll` | `VERIFIED` |
| T=2× 构造参数 30s | **CONTRADICTED**（10s）；T=60s 仍 ≥ 下限 |
| CA 六端点全绿 / minimax 真机恢复 | `CLAIM_ONLY` |
| Whoami Z 序 / 队列永不删 | `VERIFIED`（CODE） |

---

## §9 自审清单【C】/【ALL】

- [x] 1 未发现 worker 调 LVGL（OTA/Whoami 经队列/poll）
- [x] 2 OTA 无独立 busy_gen（用 gen + inflight）；Whoami consume 不看 gen（Nit）
- [x] 3 overwrite 路径可漏 delete（P2-3）；consumer 匹配路径 `delete` 一次
- [x] 4 队列先建后任务；失败不启动
- [x] 5 OTA/Whoami 任务持快照拷贝
- [x] 6 OTA stale gen delete；Whoami 离屏靠 NULL 控件
- [x] 7 未见 memset 含 string 的 OTA/Whoami 结果
- [x] 8 未见 UI 线程 `= T()` 肥聚合
- [x] 9 Whoami/OTA 覆盖层 churn 无新池观测点（Nit，不升）
- [x] 10 OTA_URL env 160；未改 key 三缓冲
- [x] 11 私钥 gitignored；tracked 为公钥/公开 CA PEM
- [x] 12 V1.0 申请已声明未测新功能；不得外推双机
- [x] 13 自动休眠×OTA 覆盖层（P2-5）；播放中 idle 重计时 `VERIFIED`
- [x] 14 last_seq 非双槽；掉电窗口可接受
- [x] 15 P1/P2 均有 file:line + 反例
- [x] 16 申请真机串口无原文 → `CLAIM_ONLY`；无 pio → BUILD `UNKNOWN`
- [x] 17 参照系 71c09e7；HEAD 文档漂移已声明
- [x] 18 OTA/Whoami 有生产调用方（Settings / 菜单 / loop poll）
- [x] 19 反例库：pending 回滚、离页结果、Trust、idle
- [x] 20 不与「用户要单 commit」票数对抗拆分；P1 按证据定

【D】21–27：代码评审 `NOT_APPLICABLE`（申请本身缺验证表，已在 §0 流程记下）。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒批次）
- [x] **C. 部分接受**

**当批必修（P1）**：补 `verifyRollbackLater() { return true; }`，否则不得把 WDT 窗口说成层 2 回滚。

**应修（P2，台账 + 建议同后续 commit，勿再塞进设计 v7）**：P2-1 语音 role；P2-2 inflight 时序；P2-3 overwrite delete；P2-4 OTA 专用 CA；P2-5 idle 勿在 `ota_busy` 时 push Sleep；P2-6 契约行。

**保留**：组 1 CA 方向、组 4/5/6/7 已走查的修复、OTA 签名/流式/poll 接线。

**回退项**：无（不要求整批 revert）。
