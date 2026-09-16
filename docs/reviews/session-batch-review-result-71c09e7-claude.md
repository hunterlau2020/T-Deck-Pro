# 评审结果：会话批次 `71c09e7`（claude）

- **评审日期**：2026-09-16
- **申请文件**：[session-batch-review-request-71c09e7.md](session-batch-review-request-71c09e7.md)
- **评审提交**：`71c09e7`（父 `6eb8245`；27 文件 +4149/−1580）
- **工作树 HEAD**：`42ace3c`（`git diff 71c09e7 HEAD` 只涉及 `docs/`/`scripts/` 诊断工具与账本条目，
  未改 `examples/pda2` 实现文件 — 本轮 findings 对当前 HEAD 仍成立）
- **评审依据**：`docs/review_guide.md` v1.3，**代码**口径（§2.3：A/B/C，A 不得挂 P0/P1）。
- **前置声明**：本文件写作前已读过 [`…-71c09e7-grok.md`](session-batch-review-result-71c09e7-grok.md)。
  按 §0 原则 3"独立复现优先"——下列每条 P1/P2 我都重新去读了 `file:line` 原文并自行推演，
  不是转述 Grok 的结论；核对后**结论与 Grok 一致（C，1×P1+6×P2）**，但证据链与部分严重度
  论证是我独立得出的，其中 P2-2 我补了一处 Grok 未写的机制细节（见下）。
- **评审结论**：**C 部分接受**。**1×P1**（`verifyRollbackLater()` 缺失，层 2 回滚在当前
  Arduino 启动路径上不存在）须当批修复。其余 6×P2 应修，登记 `issue_list.md` 绑后续
  G-合入，不阻断本批"保留"的部分。未达 L2（本环境无 `pio`，BUILD 未独立复跑）。
- **G-合入**：否（P1 未闭合）。**G-真机 / G-发布**：否（申请 §8 已自陈回滚矩阵/覆盖层吸收/
  断网重试/低电阀均未真机测）。

---

## 0. 区间文件归属（§0 原则 5 / §7.2①）

`git diff --name-only 6eb8245..71c09e7` 共 27 文件，逐一归属（未发现遗漏文件）：

| 文件 | 组 | 处置 |
|---|---|---|
| `ca_bundle_full.h`、`scripts/gen_ca_bundle.py`、`scripts/extra_roots/*.pem`、`scripts/verify_chains_vs_bundle.py` | 1 CA | 受评（脚本/根证书不豁免，§0 原则 5） |
| `ui_voice_ai.cpp` | 2 语音上下文 + 7 TTS | 受评 |
| `factory.ino`、`peri_keypad.cpp`、`ui_deckpro.cpp`、`ui_deckpro.h` | 3 休眠 + 6 菜单 + 8 OTA UI | 受评 |
| `ui_penpal.cpp`、`ui_penpal.h`、`ui_penpal_write.cpp`、`penpal_api.{h,cpp}` | 4 PenPal + 5 Whoami API 复用 | 受评 |
| `ui_whoami.cpp`（新） | 5 Whoami | 受评 |
| `ui_ai_cfg.cpp` | 7 开关样式 | 受评 |
| `ota_update.{h,cpp}`（新）、`ota_trust_anchor.h`（新）、`peri_gps.cpp`、`scripts/ota_gen_key.py`、`scripts/ota_sign.py`、`env.cfg.example` | 8 OTA | 受评 |
| `.gitignore`、`scripts/capture_boot.py`、`scripts/extract_missing_roots.py` | 附属 | 受评（私钥 gitignore 规则 / 辅助脚本，不是实现代码不豁免） |

**流程note（不占缺陷）**：申请明确用户要求单 commit 归档 8 组不相关变更，按 §0 原则 5 逐组
给 findings，不因"未按模块拆 commit"退回申请。

---

## 复核方法

Grok 的报告已经覆盖了全部 8 组，我没有重新逐组走查（那会是纯重复劳动），而是把精力放在
**独立验证 Grok 报告里最重的两条**（P1 verifyRollbackLater、P2-2 inflight 时序）——这两条
分别是"层 2 回滚是否存在"和"我自己在 v6 设计评审里判过的一个遗留问题在实现里是否复现、
以什么机制复现"，都值得亲自读代码而不是转述。其余 5 条 P2 我逐条读了引用的 `file:line`
原文确认证据成立，未发现 Grok 的事实性错误。

---

## Findings

### P1：`verifyRollbackLater()` 未覆盖 — 层 2 回滚在当前启动路径上不存在

- **位置**：全仓 `examples/pda2` 无 `verifyRollbackLater`（`rg -n verifyRollbackLater
  examples/pda2` → 0 hits，本轮独立重跑确认）。`factory.ino:662-886` 新增的整套
  boot-WDT 自证窗口（`esp_task_wdt_init(60, true)` → `esp_task_wdt_add(NULL)` →
  `boot_wdt_feed()` 散布五处喂狗点 → `loop()` 里 `esp_ota_mark_app_valid_cancel_rollback()`）
  实现得很完整，但这整套机制建立在一个前提上：镜像在 `setup()` 期间必须仍处于
  `ESP_OTA_IMG_PENDING_VERIFY` 状态，直到 loop() 里的自证点才翻转。
- **证据**：这个前提在本仓库**历史上已被三方独立读过 arduino-esp32 框架源码逐行核实**（不是
  本轮新论断，是把已确立的框架事实核对到本次实现是否覆盖）——`docs/reviews/
  ota-update-design-review-result-45d11cf-qwen.md` §一① 记录了具体行号：
  `cores/esp32/esp32-hal-misc.c:207 bool verifyRollbackLater() __attribute__((weak));
  :208 {return false;} :222 initArduino() :225 if(!verifyRollbackLater()){ :228
  esp_ota_get_state_partition(...) :229 if(ota_state==ESP_OTA_IMG_PENDING_VERIFY)
  :231 esp_ota_mark_app_valid_cancel_rollback();`。即：Arduino 的 `initArduino()`
  在 `setup()` **之前**运行，只要不覆盖这个默认返回 `false` 的 weak 函数，就会**立即**
  把任何处于 pending 状态的镜像标记为 valid——这发生在 `factory.ino:673` 打开 WDT 窗口
  **之前**（`initArduino()` 是框架 `main.cpp` 里 `setup()` 调用前的固定步骤）。设计稿
  `docs/ota-update-design.md` §9 组 4 明确把 `verifyRollbackLater` 列为与 WDT 窗口同批
  必须落地的项（`git log -p` 可见 v3/v4/v5/v6 历次修订稿 §5.1"机制事实"段都重申
  "OTA 回滚在下一次 boot 才判定；`initArduino()` 自动自证须以 `extern "C" bool
  verifyRollbackLater() { return true; }` 覆盖"）——本批 `71c09e7` 只落地了 WDT 窗口
  本身，漏掉了这一行前置覆盖。
- **本仓库内的具体触发路径（比抽象反例更实）**：`factory.ino:761-766`——
  ```c
  if(isT_Deck_Pro_v1_1) {
      if (!drv.begin()) {
          Serial.println("Could not find DRV2605");
          while (1) delay(10);
      }
      ...
  }
  ```
  这段 `while(1)` 死循环位于 WDT 窗口打开（`:673`）**之后**、自证点（`:879`，在 `loop()`
  里）**之前**。这正是这套 WDT 机制设计出来要兜底的那类"setup() 内挂死"场景（IC-5，
  Grok 表已列为"未过，可达性 3"——DRV2605 探测失败仅 V1.1 机型可达）。但由于
  `verifyRollbackLater()` 缺失，`initArduino()` 早在 `drv.begin()` 执行之前就已经把
  pending 镜像标记为 valid；WDT 60s 后触发的复位只是把设备重启回**同一个已经"validated"**
  的镜像——不会回滚到 app 的另一槽。也就是说，即使 WDT 窗口/喂狗点全部正确，只要
  `verifyRollbackLater` 缺失，这套机制的可观测行为退化成"挂死→复位→挂死→复位"的
  无限循环（brick，需 USB 分块烧录恢复），而不是设计承诺的"挂死→复位→自动回滚到
  上一个好版本"。
- **影响**：契约破坏（设计 §5.1/§9 组 4 承重前提缺失）。`A/2/有声 → P1`（"有声"指
  WDT 复位本身有串口日志；但复位后设备仍在循环挂死，用户看到的是"设备一直重启"而非
  明确的"回滚失败"提示，实际后果接近不可恢复）。按 §4.2.1"确定性自相矛盾…无论载体
  有声静默都是 P0/P1"——这不是"未来实现者可能写错"的假想级联，是本批**已经**落地的
  WDT 机制在当前代码状态下就推不出设计承诺的行为，回升条件明确：这一行不补，任何
  未来 OTA 镜像只要在 `setup()` 里挂死（不限于 `drv.begin()`，任何外设初始化卡死都算）
  都会踩中。
- **两轴**：`VERIFIED`（CODE：`rg` 0 hits + `factory.ino:761-766` 挂死点确认存在于
  WDT 窗口内；框架行为引用既往三方读源码的 `VERIFIED` 结论，本轮环境无 `~/.platformio`
  缓存无法重新读 `esp32-hal-misc.c` 原文，框架事实本身标 `PARTIALLY_VERIFIED`——沿用
  历史 `VERIFIED` 记录，未独立重跑）/ `VIOLATES`（`docs/ota-update-design.md` §5.1/§9
  组 4；`CLAUDE.md`"变砖不可远程恢复"权重条款，§0 原则 9）。
- **最小修复**：`factory.ino`（或独立 `.cpp`）内加
  ```c
  extern "C" bool verifyRollbackLater() { return true; }
  ```
  必须在 `#include <esp_task_wdt.h>`/`esp_ota_ops.h` 一带、任何 `setup()` 之外的全局
  作用域声明（弱符号覆盖需要链接期可见，不能放在函数体内部）。修复后必须真机验证一次
  §8 回滚矩阵行 5（自毁测试固件：新固件未自证 → bootloader 自动回滚）——申请已自陈
  这项未测，**当批修复后仍需这项真机回归才能算闭合，不能只凭编译通过结案**。

---

### P2-2：`s_ota_inflight` 递增时机与 PenPal 先例相反，且用**非原子赋值**实现——是永久卡死不是瞬时越界

- **位置**：`ota_update.cpp:536-553`（`ota_check_async`）、`:555-581`
  （`ota_start_async`）、`:262-268`（`ota_task_exit`）。
- **证据**：
  ```c
  static volatile int s_ota_inflight = 0;      /* :35 — 普通 volatile int，非 atomic 类型 */
  ...
  static void ota_task_exit(void)              /* :262 */
  {
      s_ota_hw_lock = false;
      s_ota_inflight = 0;                      /* :265 — 绝对赋值，不是 __atomic_sub_fetch */
      WiFi.setSleep(true);
      vTaskDelete(NULL);
  }
  ...
  bool ota_check_async(uint32_t gen)            /* :536 */
  {
      if (s_ota_inflight > 0) { ...; return false; }
      ...
      if (xTaskCreate(ota_check_task, ...) != pdPASS) return false;
      s_ota_inflight = 1;                       /* :551 — create 成功后才赋值 */
      return true;
  }
  ```
  对照 `ui_penpal.cpp:529-538`（PenPal 先例）：`__atomic_add_fetch(&s_pp_inflight, 1,
  __ATOMIC_RELAXED)` 在 `xTaskCreate` **之前**执行，失败才 `__atomic_sub_fetch` 回滚，
  注释明确写"worker 可能在 xTaskCreate 返回给这边之前就已经在另一个核上跑完"。
  v6 设计稿 §3.3/§13 反复声称"复用 PenPal 同款先例"（我在 v6 设计评审 `…-97a4f53-
  claude.md` 里已指出这处引用与设计文字本身顺序相反），本批实现把顺序上的偏差原样
  落进了代码，**而且比设计文字描述的更糟**：PenPal 用的是原子加减（可交换），我在
  v6 设计评审里正是依据"两次原子操作最终都会执行、可交换"论证过"不会永久卡死，只是
  瞬时窗口可能被突破"——**这个论证对当前实现不成立**：这里 `s_ota_inflight` 全程都是
  普通整数的**绝对赋值**（`= 0` / `= 1`），不是原子加减，赋值顺序直接决定最终值，
  不可交换。
- **反例（比"瞬时越界"更严重的可复现路径）**：`ota_check_async` 内 `xTaskCreate`
  返回后，调用方唯一要做的下一件事就是执行 `:551` 这一条 store 指令；而被创建的
  `ota_check_task`（另一核上可能立即被调度）如果走最快的早退路径（如 `OTA_URL` 未
  在 `/env.cfg` 配置——设备首次刷入本批固件、用户还没来得及写 OTA_URL 就是这个状态）
  会在几条语句内就跑到 `ota_send_result` + `ota_task_exit()`，即 `:265` 把
  `s_ota_inflight` 写回 `0`。如果这次写发生在调用方执行 `:551` 之前，调用方的
  `s_ota_inflight = 1` 会**在 worker 已经退出之后**把值改回 1——此后再没有任何代码路径
  会把它改回 0（worker 已经 `vTaskDelete` 了），`s_ota_inflight` **永久**停在 1。
  之后每次按 Check/Update 都会看到 UI 提示"previous OTA op closing"/"start failed
  (busy)"（`ui_deckpro.cpp:1172`/`:1187`，本轮独立核对确认这两处文案确实存在、
  用户可见），OTA 功能对该设备永久不可用，直到手动重启。这不是需要压测才暴露的边缘
  情况，是"OTA_URL 未配置"这种**最常见的初始状态**下就可能触发的路径。
- **影响**：`C/2/有声 → P2`（安全控制失效类"busy 永久卡死"，可重启恢复，故不升 A/B；
  可达性档 2——需要具体的跨核调度时序，不是每次必现，但触发条件是"未配置场景/连接
  失败早退"这种常见输入，不是刻意反常操作，比 Grok 报告里"无 WiFi"这个例子更容易
  在真实使用中撞上）。
- **两轴**：`VERIFIED`（CODE：`s_ota_inflight` 声明为 `volatile int` 非原子类型，
  两处赋值均为普通 store，逐行核对无误）/ `VIOLATES`（`async_ipc_contract.md` 未
  单列"单飞守卫递增时机"条款，但 PenPal 已确立的同类先例是本项目唯一的正确答案来源，
  按 §2.5.1"与既有模块同构"判据，v6 设计稿也已承诺遵循）。
- **最小修复**：与 PenPal `ui_penpal.cpp:529-538` 逐行同构——`xTaskCreate` **调用前**
  置 `s_ota_inflight = 1`；创建失败立即回滚为 `0`。因为当前实现从未用过原子操作
  （`volatile int` 直接赋值），顺带评估是否也要像 PenPal 一样换成 `__atomic_*`——
  单飞 cap=1 场景下若仍只有 UI 线程单入口调用 `ota_check_async`/`ota_start_async`，
  非原子赋值本身不构成新增风险（不存在两个写者），**但 `ota_task_exit` 侧的写和
  UI 侧的写始终是跨核并发**，建议至少这两处改用 `__atomic_store_n`，避免依赖
  "读写单字节在这颗芯片上凑巧原子"这种没有语言层保证的假设。

---

### P2-1：语音多轮上下文把 `(role, content)` 错填成 `(user_text, assistant_text)`，第二轮起必错

- **位置**：`ui_voice_ai.cpp:315-358`（`vai_ctx_add_pair` 存 `pair(user, assistant)`；
  `vai_ctx_build` 里 `:354-355` 取值）→ `openai_api.h:14`（`ai_message_t::role`
  的文档注释：`/* "user" or "assistant" */`）。
- **证据**：本轮独立核对了契约源头——`openai_api.h:14` 明确 `role` 字段只应是字面量
  `"user"`/`"assistant"` 两个值之一。`ui_voice_ai.cpp:354-355`：
  ```c
  msgs[n].role = vai_ctx[i].first.c_str();     /* .first 是用户原话文本，不是 "user" */
  msgs[n].content = vai_ctx[i].second.c_str(); /* .second 是助手回复文本 */
  ```
  而 `vai_ctx` 的元素类型是 `pair<string,string>`，`vai_ctx_add_pair(user, assistant)`
  在 `:329` 处 `vai_ctx.push_back(make_pair(u, a))`——`.first`/`.second` 从头到尾就是
  "用户说的原话"和"助手回复的原话"，从未被赋成过字面量 `"user"`/`"assistant"`。第二轮
  起，`openai_chat_multi` 收到的 history 数组每一条的 `role` 字段都是用户上一句话的
  原文，不是角色标识。
- **反例**：用户说"我叫李四"（确认成功入史）→ 再说"我叫什么"→ 本轮 `vai_ctx_build`
  把历史那一条拼成 `{"role":"我叫李四","content":"<助手上一轮回复>"}` 送进 API——
  多数 OpenAI 兼容后端会拒绝非法 `role` 枚举值（4xx）或把它当未知角色处理，"连续
  语音对话有记忆"这个用户需求从当前代码推不出来能正常工作。
- **影响**：`D/1/有声 → P2`（每次 follow-up 都会踩到，非偶发；算法/边界正确性类，
  API 通常会返回错误而非静默错误——若某后端宽松到接受任意 `role` 字符串并静默处理，
  实际后果会退化成"胡答"，仍不升级，因为触发即错的本质没变）。
- **两轴**：`VERIFIED`（CODE，源头契约 + 赋值语句均逐行核对）/ `VIOLATES`
  （`async_ipc_contract.md` §5.1"多轮上下文轮次配对"条款的精神——角色标签必须是
  `user`/`assistant` 字面量；本组的设计说明"整轮 (user, assistant) 对入史"这个
  描述本身没错，错在把这对**当字段值**而不是当**载荷**填进了 API 协议要求的
  `role`/`content` 两个槽位）。
- **最小修复**：`vai_ctx` 改存四元组或拆成两条 `ai_message_t` 意图对象
  （`{"user", u}` + `{"assistant", a}`），`vai_ctx_build` 按**消息条数**（而非
  "轮"）裁剪预算，`vai_ctx.size()>2` 的"一次 erase 两个 turn"逻辑一并对齐成
  "按整轮（两条消息）裁剪"。修复后最小验证：两轮对话时打印实际发出的
  `messages` JSON（或串口 `send: N context msgs`，参照 AI Text 已有日志格式），
  确认 `role` 恒为 `user`/`assistant` 字面量。

---

### P2-3：`xQueueOverwrite` 覆盖丢失路径没有先 `Receive` 旧指针，存在真实泄漏（非"延迟不丢"）

- **位置**：`ota_update.cpp:270-274`（`ota_send_result`）、`:583-591`
  （`ota_result_poll`）。
- **证据**：
  ```c
  static void ota_send_result(ota_result_t *r) {
      if (s_ota_q) xQueueOverwrite(s_ota_q, &r);   /* :272 */
      else delete r;
  }
  ...
  void ota_result_poll(void) {                      /* :583 */
      if (!s_ota_q) return;
      ota_result_t *r = NULL;
      if (xQueueReceive(s_ota_q, &r, 0) == pdTRUE && r) {
          if (s_consumer) s_consumer(r);
          else delete r;
      }
  }
  ```
  `xQueueOverwrite`（深度 1 队列）在槽位已有元素时直接覆盖，**不触发任何回调**、
  旧元素的内容对调用方而言凭空消失。申请 §9②自评"若 loop 被长阻塞（EPD busy
  10s × 连续多帧）结果消费延迟但不丢（worker 已退，只延迟 delete）"——这个说法在
  "两次 overwrite 之间 poll 从未跑过一次"的具体场景下不成立：第一次 `overwrite`
  写入指针 A（此时队列槽从空变为有值，`xQueueReceive` 本该在下次 poll 时取到 A 并
  `delete`）；如果在下次 `ota_result_poll()` 真正执行之前，`s_ota_inflight` 已经
  归零（`ota_task_exit` 在 `ota_send_result` **之后**才调用 `vTaskDelete`，见
  `:262-268`——释放 inflight 与释放队列槽是两个独立时间点）且用户/自动逻辑发起了
  第二次 Check/Update，第二个 worker 完成时会对同一个深度 1 队列再次
  `xQueueOverwrite`——此时槽内还没被 poll 取走的指针 A 被指针 B **直接覆盖**，
  `delete r`（A）永远不会发生：不是延迟，是真实丢失一次 `ota_result_t` 分配
  （小对象，几十到上百字节量级，非致命但确实是泄漏）。
- **影响**：`B/2/静默 → P2`（无错误信号——两次覆盖都会正常返回，用户观察不到异常；
  与 P2-2 若叠加发生会放大：P2-2 卡死后用户反复重试，若每次重试都先经历一次快速
  失败+overwrite，会重复丢失多个结果结构）。
- **两轴**：`VERIFIED`（CODE：`xQueueOverwrite`/`xQueueReceive` 用法逐行核对）/
  `VIOLATES`（设计 §3.1 自己写明"覆盖会丢一个未消费的结果结构"是已知残余风险，
  但申请 §9②把这个已知风险重新描述成了"不丢"，与设计文本本身矛盾——按 §9 自审
  【文档对齐】模式 B，"声明"与"证据"没对上）。
- **最小修复**：`ota_send_result` 在 `xQueueOverwrite` 前先 `xQueueReceive(s_ota_q,
  &old, 0)`，若成功取到非空 `old` 则 `delete old` 再覆盖写入新指针。

---

### P2-4：OTA 下载走 `http_apply_tls()`，继承 AI 功能的全局 "Trust self-signed" 开关

- **位置**：`ota_update.cpp:314-315`（manifest 请求）、`:422-423`（固件下载）；
  `http_utils.cpp:25-44`（`s_tls_mode` 是模块级全局变量，`HTTP_TLS_INSECURE` 时
  `setInsecure()`，否则 `setCACertBundle(CA_BUNDLE_MOZILLA)`）。
- **证据**：`s_tls_mode` 由 `http_set_tls_mode()` 写入，是**进程级单例状态**——AI Cfg
  屏的 "Trust self-signed" 开关与 OTA 客户端共用同一个变量。`docs/ota-update-design.md`
  v6 §3.4 要求 OTA 用**专用** `setCACertBundle`、不继承 Trust 开关；`ota_update.cpp`
  实际调的是共享的 `http_apply_tls()`，未做区分。这个耦合本身是本仓库既有的、已登记
  的已知问题（`CLAUDE.md`/`docs/issue_list.md` §7.4："TLS Trust toggle 影响所有
  HTTPS 消费者，不只 AI，UI 文案须说明"，closed 于 `a58a73c`——但那次修复只是补了
  文案，没有把 OTA 从共享状态里摘出来）。
- **说明为何不升 P0/P1**：清单仍要求有效的 ECDSA 签名才允许写入 flash（`ota_verify_
  signature`，`:103-141`），所以即使某用户为了接一个自签名的内部 AI 测试服务器打开了
  Trust 开关、间接让 OTA 的 TLS 证书校验也失效，攻击者也无法在没有私钥的情况下伪造出
  能通过验签的固件；实际可被利用的面收窄成"中间人能读到明文固件内容 + 能做下载期
  DoS/回复损坏数据触发下载失败"，固件本身不是机密，且 §10 已有 `Content-Length`/
  SHA-256 双重校验挡住损坏数据被写入。
- **影响**：`C/2/静默 → P2`（需要用户主动打开 Trust 开关这个前提，多前提折算取
  可达性档 2；无错误信号，UI 不提示"OTA 也在用不安全 TLS"）。回升条件：若签名验证
  被绕过，或未来产品要求明文固件也保密。
- **两轴**：`VERIFIED`（CODE）/ `VIOLATES`（v6 设计稿 §3.4"专用 CA、不继承 Trust"的
  正文承诺）。
- **最小修复**：OTA 客户端直接调 `WiFiClientSecure::setCACertBundle(CA_BUNDLE_MOZILLA)`
  自建连接，不经过共享的 `http_apply_tls()`/`s_tls_mode`；即使用户为 AI 功能打开了
  Trust，OTA 仍强制证书校验。

---

### P2-5：5 分钟自动休眠定时器不检查 `ota_busy()`，下载覆盖层期间仍会 push Sleep 屏

- **位置**：`ui_deckpro.cpp:4977-4993`（`idle_sleep_timer_cb`，本轮独立读取原文，
  仅检查 `audio.isRunning()`，未见任何 `ota_busy()` 引用）；对照 `:4815-4822`
  （`sleep_do_enter` 内确有 `if (ota_busy()) { ...; return; }` 门控，本轮独立核对
  确认该函数层面的实际深睡入口是安全的）。
- **反例**：OTA 下载覆盖层显示"DO NOT POWER OFF"期间用户不会有任何按键/触摸
  （覆盖层无按钮，设计本意如此），5 分钟计时器到期后 `idle_sleep_timer_cb` 仍会
  无条件 `scr_mgr_push(SCREEN11_ID, false)`，把 Sleep 倒计时屏压到 OTA 覆盖层之上。
  倒计时结束后 `sleep_do_enter()` 因 `ota_busy()` 为真而拒绝真正进入深睡（不掉电，
  这点是安全的），但 Sleep 屏已经压栈，UI 观感是"下载中途弹出了一个别的屏"，且
  `idle_sleep_timer_cb` 每个 5 分钟窗口都会重复 push（`:4990` 每次都重新打时间戳，
  不是一次性）。
- **影响**：`C/2/有声 → P2`（UI 卡死/错乱类，不影响下载本身完整性，不升 A/B——
  `esp_deep_sleep_start` 从未被调用，flash 写入不受影响；可达性档 2：需要下载耗时
  超过 5 分钟这个特定时序，正常 WiFi 下载多数场景下达不到，慢速网络或大镜像会撞上）。
- **两轴**：`VERIFIED`（CODE）/ `VIOLATES`（无直接对应的契约条款，按 §4.3 判为
  `UNDECIDED` 更准确地说是设计遗漏——设计文档描述的互斥点只覆盖了"Sleep 入口 busy
  拒绝"，没考虑"idle 定时器本身要不要在 OTA 忙时提前 return"这一层）。
- **最小修复**：`idle_sleep_timer_cb` 顶部加 `extern bool ota_busy(void); if
  (ota_busy()) { s_last_activity_ms = millis(); return; }`（重新打时间戳而非放弃
  计时，行为与现有 `audio.isRunning()` 分支一致）。

---

### P2-6：OTA / Whoami 两个新异步任务未登记进 `async_ipc_contract.md`

- **位置**：`docs/async_ipc_contract.md`（本轮 `rg -n "OTA|Whoami|ota_update|whoami"`
  独立重跑，0 命中）；契约表当前只有 WiFi Test / Time Sync / AI Test / AI Chat 几行。
- **影响**：`B/2 → P2`（§4.3 `NO_CONTRACT`：新型异步任务须同轮补契约或显式登记例外，
  本批两个都没有）。这不是说 OTA/Whoami 的实现本身违约（本轮独立核对了两者的队列
  创建/drain/gen 使用，OTA 见上文 P2-2/P2-3 之外的部分、Whoami 见"已通过项"，两者
  基本遵循 PenPal 模式），是文档口径缺失——下一个改这两个模块的人无法从契约表一眼
  确认"这里该怎么处理迟到结果/取消/离屏"，只能去读实现反推。
- **最小修复**：契约表加两行：OTA（指针队列深度 1 + `xQueueOverwrite`、
  `ota_result_poll` 每 tick 无条件排空、`s_ota_inflight` cap 1 语义）、Whoami
  （队列一次创建永不删、`whoami_keyboard_poll` 先 drain 后判 `kbd_active`）。

---

## 已通过项（本轮独立复核，非转述申请或 Grok 报告）

- **组 5 Whoami Z 序**：本轮独立读取 `ui_whoami.cpp:673-747`（`wa_create`），确认
  两个 240×320 tab 页容器确实在返回按钮/tab 按钮**之前**创建（源码里的注释也如实
  解释了 LVGL 同级兄弟"后建居上"的规则），修复方向正确。
- **组 5 Whoami 队列生命周期**：`whoami_keyboard_poll()`（`:618-622`）开头无条件
  `wa_consume()` 排空，与 `penpal_keyboard_poll` 同构；本轮未见队列被 `destroy()`
  删除的路径（与"队列一次创建、永不删"的申请描述一致）。
- **组 7 TTS/Trust 开关样式**：`ui_voice_ai.cpp:698`、`ui_ai_cfg.cpp:637` 均确认
  选择器写的是 `LV_PART_INDICATOR | LV_STATE_CHECKED`，不是默认态选择器——本轮独立
  `rg` 核对，两处都对。
- **组 8 OTA 互斥/精读**：`sleep_do_enter()` 对 `ota_busy()` 的门控（`:4819-4822`）
  独立核对属实（即 P2-5 的问题止于"UI 压栈"，不延伸到"真的掉电中断写flash"）；
  UI 侧对 `ota_check_async`/`ota_start_async` 返回 `false` 均有可见文案兜底
  （`ui_deckpro.cpp:1172`/`:1187`），不存在"调用失败但界面无反应"的静默失败。
- **组 8 签名/编码链路**：`ota_verify_signature`（`ota_update.cpp:103-141`）用
  `mbedtls_mpi_read_binary` 读 r/s（P1363 定长切片，不是 `mbedtls_ecdsa_read_signature`
  这个此前已知的 ASN.1 陷阱）；`ota_signed_bytes`（`:81-98`）六字段顺序与
  `ota_parse_manifest`（`:146-239`）字段名逐一核对一致，十进制无前导零/小写 hex 的
  编码钉死在解析阶段就做了拒绝校验（`:180-193`、`:201-208`），不是"信了就存"。

---

## 验证说明

- 本环境无 `pio`、无设备：BUILD/HW 证据一律 `UNKNOWN`；本文件所有结论止于 CODE/SIM/
  DOC 级别的静态复核。
- 独立读取（非转述）的仓库源码位置：`ota_update.cpp`（全文 618 行通读一遍）、
  `factory.ino:1-60`/`650-900`（setup 头部 + WDT 窗口 + loop）、
  `ui_voice_ai.cpp:290-370`/`684-700`、`ui_deckpro.cpp:1155-1199`/`4815-4994`、
  `ui_whoami.cpp:600-750`、`ui_ai_cfg.cpp:620-640`、`http_utils.cpp:1-55`/`280-299`、
  `openai_api.h:14`、`openai_api.cpp:780-812`、`ui_penpal.cpp:495-538`（P2-2 对照
  先例）、`docs/async_ipc_contract.md`（`rg` 确认 0 命中）。
- `verifyRollbackLater` 的框架侧事实（`esp32-hal-misc.c` 具体行号）沿用本仓库既有的
  三方独立核实记录（`…-45d11cf-qwen.md`/`-grok.md`），本轮环境无框架缓存未能重新
  读一遍原文，如实标 `PARTIALLY_VERIFIED`（框架事实）+ `VERIFIED`（本仓库侧
  `verifyRollbackLater` 缺失 + 具体挂死触发点）。

---

## 对称三格（§7.2）

1. **全区间**：27 文件均归属（见 §0），未只看申请靶向清单；额外主动去看的：
   `async_ipc_contract.md`（P2-6，申请未提及）、`http_utils.cpp` 的 `s_tls_mode`
   实际存储位置（P2-4 的根因，申请只说"继承 Trust"没深挖是不是全局单例）、
   `openai_api.h` 的 `role` 字段契约注释（P2-1 判定的直接依据）。
2. **独立复跑**：无 `pio`/无设备，均为 SIM 级纸面推演，逐条见上文"反例"段。P2-2
   补充了 Grok 报告没写的机制细节（`s_ota_inflight` 是非原子 `volatile int` 绝对
   赋值，不可交换，因此是永久卡死而非瞬时窗口）——这处修正了我自己在 v6 设计评审
   （`…-97a4f53-claude.md`）里"原子加减可交换、不会永久卡死"的论证，那个论证是对
   PenPal 先例和设计文字成立的，对本批把顺序**和**原子性都改掉的实现不成立，如实
   记录这次自我修正。
3. **申请"最没把握"处反例**：申请 §9①（WDT T=60s 未校准、V1.0 慢路径余量）——
   本轮未独立构造新反例，沿用 Grok 已指出的"本板 `_busy_timeout` 实际是 10s 不是
   申请写的 30s"这一处不一致，未重复验证；②（overwrite 覆盖丢失）——本轮独立
   反例见 P2-3，比申请自评的"不丢，只延迟"更进一步给出了"两次 overwrite 之间
   poll 一次都没跑"的具体丢失场景；③（Whoami/PenPal NVS 共享键并发）——本轮未
   深入，认可申请与 Grok 报告"两屏不同时活跃"的判断，不单列反例。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 1 非 UI 线程未见调 LVGL（OTA/Whoami 均经队列 + `*_keyboard_poll`/`*_result_poll`）。
- [x] 2 busy 释放：OTA 用 `s_ota_inflight`/`s_ota_hw_lock` 替代 gen 匹配的 busy（单飞
      cap 1，无多代次结果需要甄别）；stale-drop 路径见 P2-2/P2-3（此二项即"释放时机
      有问题"的具体实例）。
- [x] 3 worker `new` 的结果 UI 侧 `delete` 一次——正常路径成立；P2-3 是覆盖丢失路径下
      的例外，已单列。
- [x] 4 队列先建后置 busy：`ota_check_async`/`ota_start_async` 均先 `xQueueCreate`
      检返回值，失败才 `return false`（不置 inflight）——成立；create 失败路径本身
      正确，问题在**成功**路径的赋值顺序（P2-2）。
- [x] 5 任务持请求快照：`ota_update_task` 的 `snap_t` 深拷贝 `m = *snap` 后
      `delete snap`（launch 时移交）——独立核对成立，无跨核共享可变缓冲。
- [x] 6 迟到结果丢弃路径：OTA 单飞 cap 1、无页面 gen 概念，"迟到"退化成"覆盖丢失"
      （P2-3），机制不同但本质是同一类问题未被处理。
- [x] 7 未见 `pp_*_t`/`ota_*_t`/`wa_*_t` 结构被 `memset`。
- [x] 8 未见 UI 线程 `= T()` 肥聚合新增。
- [x] 15 每条 P1/P2 均给了 `file:line` + 反例（见上）。
- [x] 16 申请真机串口证据未附原文，自证一次的声明标 `CLAIM_ONLY`；BUILD 因无 pio
      标 `UNKNOWN`。
- [x] 18 `ota_check_async`/`ota_start_async`/`whoami_*` 均在 `ui_deckpro.cpp`/
      `ui_whoami.cpp` 找到真实调用方，非"实现但未接线"。
- [x] 19 §6 反例库"OTA 下载中途/自毁固件/低电量"三类均已推演（P1 + 已通过项）；
      "离页后旧结果到达"类因 OTA 无页面概念，退化核对见 P2-3。
- [x] 20 未与"申请要求单 commit"这一票数对抗按模块拆分的流程要求；P1 判定完全按
      证据（§0 原则 2）。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒批次）
- [x] **C. 部分接受**

**当批必修（P1）**：补 `extern "C" bool verifyRollbackLater() { return true; }`；
修复后须真机跑一次自毁固件回滚场景（§8 表行 5）才能宣称层 2 可用，不能只凭编译
通过结案。

**应修（P2，登记 `issue_list.md` 绑后续 G-合入，不要求本批立刻堵门）**：P2-1 语音
上下文 role/content 错位；P2-2 `s_ota_inflight` 时序 + 改非原子赋值为与 PenPal
同构（或至少改 `__atomic_store_n`）；P2-3 `xQueueOverwrite` 前先 drain 旧指针；
P2-4 OTA 专用 CA、不继承 Trust 开关；P2-5 idle 定时器在 `ota_busy()` 时不 push
Sleep；P2-6 契约表补 OTA/Whoami 两行。

**保留**：组 1 CA 根修复方向、组 4/5/6/7 已独立核对的修复、OTA 签名/流式下载/
poll 接线/精读部分。

**回退项**：无（不要求整批 revert；P1 是加一行弱符号覆盖 + 真机回归，侵入面小，
不需要推倒重来）。
