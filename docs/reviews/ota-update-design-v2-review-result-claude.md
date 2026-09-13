# 设计评审结果：OTA 固件远程升级 v2 修订稿（Claude 复审）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md)（v2，`be6a52c`）
- **v1 四方评审**：Codex C / Grok C / Qwen(opencode) C / Claude A
  （[本目录下对应四份结果文件](.)）
- **本次结论**：**C 部分接受**——v2 对 v1 四方提出的绝大多数 P1/P2 处置
  到位、且可独立核实；但发现 **1 项被 §11 修订表"标记为已关闭、实际未
  关闭"的安全隐患**（Grok P1-3 的一半）与 **1 项 v1→v2 重写引入的新
  结构性缺陷**（§6 悬空引用 + 发布/USB 回退流程正文丢失），两项都应在
  开工前修订。本次复审比我上一轮 v1 评审（A）更严格——上一轮遗漏的
  正是这次发现的第一项。

## 复核方法

对 v2 §11 修订表列出的每一条"处置"，逐条回到 v2 正文核实是否真的落地，
并对照 Codex/Grok/Qwen 三份原始评审的"最小修复"原文逐句比对（而不是只
读 v2 自己的转述），同时对 v2 新增的具体断言（`verifyRollbackLater`、
`drv.begin()`、`CA_BUNDLE_MOZILLA`、`pp_request` scheme 分支等）在仓库
现有源码中重新验证。

## Findings

### P1（本轮新发现）：低电自动关机 / Sleep 自动休眠未与 OTA 写 flash 互斥——Grok P1-3 的"禁止 Sleep/低电关机"半条被静默丢弃

- **位置**：v2 §3.2、§6（均未提及）；实际代码
  `examples/pda2/ui_deckpro.cpp:136` `low_voltage_timer_cb()` +
  `ui_deckpro_port.cpp:659` `ui_shutdown_on() -> PPM.shutdown()`。
- **v2 §11 修订表第 4 行称**："§3.2 弃用 HTTPUpdate，自管流式 + SHA-256 +
  size 校验 | Grok P1-2/P1-3"——即声称 Grok P1-3 已关闭。但复读 Grok
  P1-3 原文，其"最小修复"是**两部分**："分钟级超时（或按块续传）+
  `WiFi.setSleep(false)`；**OTA 期间禁止 Sleep / 低电关机**"。v2 §3.2
  只字未提超时策略之外的第二部分。
- **证据（本轮独立核实，非转述）**：
  1. `ui_deckpro.cpp:4793`（`ui_deckpro_entry()`，全程序只调用一次）：
     `low_voltage_timer = lv_timer_create(low_voltage_timer_cb,
     LOW_VOLTAGE_POLL_MS, NULL);`——**从不 pause/del**，与
     `touch_chk_timer`/`taskbar_update_timer`（同函数内创建后立刻
     `lv_timer_pause`，按屏幕 entry/exit 恢复/暂停）形成对比：低电
     定时器是**贯穿设备整个生命周期、独立于当前显示哪个屏幕**的
     全局巡检。
  2. `low_voltage_timer_cb()`（:136-168）：外部电源不在场时，电压/
     gauge 判定"低"就锁存倒计时，倒计时到 0 且尚未发起过关机，直接
     `ui_shutdown_on()`——**全程不检查是否有 OTA 任务在跑**。
  3. `ui_shutdown_on()`（`ui_deckpro_port.cpp:659-663`）直接
     `PPM.shutdown()`，没有任何"写 flash 中"的互斥判断。
  4. Sleep 屏的 `esp_deep_sleep_start()`（`ui_deckpro.cpp:4454`）复核后
     确认是**屏幕级**定时器（`sleep_timer` 只在 Sleep 屏 entry 时
     创建、exit 时 `lv_timer_del`），不会在用户停留在 OTA 屏时触发——
     这一条 Grok 原文提及但风险不成立，本轮予以澄清排除；**低电关机
     这条是全局的，风险成立**。
- **可复现路径**：OTA 精确 check 电量 `<30%` 拒绝（§4），但 30% 以上
  开始下载后，2.1MB 流式下载 + 边写边算 SHA-256 在弱 WiFi 下可能持续
  数十秒到数分钟；若这段时间内电池继续放电触及
  `low_voltage_should_latch()` 的电压/gauge 阈值（阈值与 30% 电量阈值
  不是同一个量，没有互斥关系保证不重叠），`LOW_VOLTAGE_SHUTDOWN_DELAY_
  MS` 倒计时一到就会在 `Update.write()` 中途执行 `PPM.shutdown()`——
  这是"应用自己触发的断电"，比外部意外断电更容易触发（不需要真的
  拔电池，只需要电量在升级过程中继续走低），后果与 §5 层 1 描述的
  "坏包/中断"不同——层 1 假设的是"otadata 不翻转、重启回旧版"，但如果
  `Update.write()` 正在向 app1 分区写入时被硬关机，被打断的是**这次
  写入本身**而不是"重启决策"，取决于具体打断时机，理论上仍能靠"otadata
  确实没翻转"兜底恢复到旧版（这点和外部断电一致），但目前设计完全没有
  分析或声明这一风险点，也没有给出"OTA 进行中屏蔽低电自动关机"的选项——
  这正是 Grok P1-3 原文特别单独列出"低电关机"（而不只是外部断电）的
  原因：**它是应用自身的既有逻辑，比外部断电更容易在测试中被忽略**。
- **最小修复**：`ota_run()` 开始下载前设一个全局标志（如
  `s_ota_writing`），`low_voltage_timer_cb()` 在该标志为真时跳过
  `ui_shutdown_on()` 调用（只保留倒计时 UI 提示，不真正关机；或改为
  下载完成/失败后立即重新评估）；同时按 Grok 建议加
  `WiFi.setSleep(false)`。这条互斥应该写进 §3.2 或 §6，并在 §8 验证
  计划里加一条"OTA 下载中触发低电锁存 → 确认不会被自动关机打断"。

### P1（本轮新发现）：v1→v2 重写丢失"发布流程"正文，§5 的 §6 引用悬空

- **位置**：v2 全文；对照 v1（`36e88df`）§6"发布流程"。
- **证据**：v1 §6 原文包含两块**operationally 必需**的内容：
  1. 发布步骤（`pio run -e pda2` → 上传 firmware.bin → 更新清单
     version/url/notes → 设备 Settings 操作序列）；
  2. USB 强制回滚的**具体命令**："清 otadata（`write_flash 0xE000` 8KB
     `0xFF`）即回 app0"。

  v2 把章节顺序改成了 0/1/2/3/4/5(回滚与自证)/6(**契约登记**)/7(分区表
  受控化)/8(验证计划)/9(工作量)/10/11——**发布流程整节在 v2 里找不到**，
  既没有以原样保留，也没有在 §11 修订表里被列为"移除/延后"。更严重的是
  v2 §5 第 4 点原文："层 2 文案修正…'强制回 app0'的唯一手段是 **USB 清
  otadata（§6）**"——但 v2 的 §6 现在是"契约登记"（gen/busy/队列语义），
  完全不包含 otadata 清除步骤。**这是一个悬空的章节引用，指向的内容
  已经不存在**。
  - 同理，v1 §5 的"失败/回滚三层模型"表格（层 1/2/3 各自的场景与结果，
    含"层 3：全挂 → USB 分块烧录 + backups/ 两机备份整片恢复"这一行）
    在 v2 里也没有整体保留——v2 §5 只逐点"打补丁"式修订层 2 的文案和
    机制，通篇没有再完整列出层 1/3。单独阅读 v2（不对照 v1 git 历史）
    的读者，看不到"层 3 具体怎么全量恢复"和"发布一次新固件的完整操作
    序列"这两块此前已经写清楚、且后续实现/运维会直接照做的内容。
- **影响**：不是安全漏洞，而是**文档完整性**问题——但这份文档的定位是
  "工作量 §9：预计一次提交"之后要被直接拿来实现和运维的规范，不是纯
  评审沟通稿；发布脚本和"变砖后怎么用 USB 救回来"这两件事恰恰是最需要
  在压力下（真出问题时）能被翻到的内容，不应该只存在于 v1 的 git 历史
  里。
- **最小修复**：把 v1 §6 的发布步骤 + USB 回退命令原样搬回 v2（可以放
  在现有 §7"分区表/bootloader 受控化"之后新增一节，或者并入 §9"工作量
  与顺序"），并把 §5 第 4 点的"（§6）"引用改指到正确章节号；层 1/3 的
  完整三层表建议至少保留一份精简版（哪怕只是一行归纳 + 指回 v1 commit
  的链接），不要求逐字重复但不能留空。

## 已核实到位的 v1→v2 改动（未发现问题）

- **TLS 隔离**（Codex P1 / Grok P1-2 / Qwen P1）：`client.setCACertBundle
  (CA_BUNDLE_MOZILLA)` 与 `CA_BUNDLE_MOZILLA` 均是 `http_utils.cpp`/
  `ca_bundle_full.h` 里已存在的真实符号，直接调用、绕过
  `http_apply_tls()` 的 `s_tls_mode` 分支，确实做到"不可能继承 Trust
  开关"，是这三份评审里最要害的一条 P1 的正确解法。
- **回滚三重修复**（Grok P1-1）：`extern "C" bool verifyRollbackLater()`
  覆盖 + 自证点后移 + 拆 `drv.begin()` 的 `while(1)` 独立复核：
  `factory.ino:708-711` 的 `while (1) delay(10);` 确实存在、确实在
  `setup()`（621-803 行）内、且复核整个 `setup()` 函数只有这一处
  `while(1)`（另一处 `while(1)` 在 `a7682_task` 独立 FreeRTOS 任务里，
  与 `setup()` 挂死无关，v2 正确地没有提它）。`verifyRollbackLater`/
  `initArduino` 自动自证这条机制性描述与 Arduino-ESP32 该核心版本的
  公开已知行为一致（本环境无 `~/.platformio` 缓存，无法逐行核对
  `esp32-hal-misc.c`，但 Grok 已独立核实过行号，与我的一般性认知
  吻合，不构成新的怀疑点）。
- **签名清单**：ECDSA P-256 覆盖 version/url/size/sha256 四字段全部
  必填，公钥走秘密链——设计本身对 P-256 vs Ed25519 的取舍已经如实
  标注"请复审裁定"，**本轮给出裁定：接受 P-256**。理由：mbedtls 2.28
  确认无原生 Ed25519（与 sdkconfig 里 `CONFIG_MBEDTLS_ECDSA_C=y` +
  `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y` 而非 Ed25519 系宏一致），
  为单一签名场景额外捆绑一份第三方 Ed25519 实现（约 30KB flash + 新的
  供应链/维护面）相对于"个人硬件、少量测试机"这个威胁模型收益不成
  比例；P-256 是被广泛验证的成熟方案，只要私钥/规范化字节串处理正确
  （设计里已经写清楚四字段 `\n` 拼接规则），安全性足够，不需要为了
  "更现代"而引入额外依赖。
- **流式下载与签名/哈希校验注入点**（Grok P1-2/P1-3 的超时部分）：
  弃用 `HTTPUpdate`、改 `HTTPClient` 流式 + `Update.write` + 增量
  `mbedtls_sha256` 的顺序（先验证清单签名 → 再下载 → 下载完比对
  size/sha256 → `Update.end(true)`）组合正确，"清单签名信任 sha256
  字段、下载后再验证实际字节确实匹配该 sha256"这个两段式校验逻辑
  自洽，没有绕过点。
- **契约登记**（Qwen P2）：v2 §6 把 gen/busy/队列深度/取消语义都写清楚
  了，作为设计阶段的"契约草案"是充分的；提醒实现落地时按 review_guide
  的要求把这行**真正写进** `docs/async_ipc_contract.md` §1 表，设计文档
  里的草案不能替代那份契约表本身的登记。
- **发布端版本一致性**（Qwen P3）：把"清单 version 与 bin 内烘焙版本
  一致"从人工检查步骤改为发布脚本自动从 `utilities.h` 读取写入清单，
  比 Qwen 原本建议的"人工核对步骤"更彻底（结构性消除错位可能，而不是
  依赖人不忘记）。
- **分区表/bootloader 受控化**（Codex P1）：两机读回 + SHA-256 存档 +
  框架升级视为兼容性破坏事件的处置方式合理，解决了 Codex 提出的"关键
  前提不可复现"问题（前提是真的执行这一步存档动作，属于实现期任务，
  设计层面已经写清楚要求）。

## 待关闭项小结（开工前）

1. **P1**：OTA 下载/写 flash 期间与低电自动关机（`low_voltage_timer_cb`
   → `ui_shutdown_on` → `PPM.shutdown()`）互斥；补 `WiFi.setSleep(false)`。
2. **P1（文档）**：补回 v1 §6 的发布步骤与 USB 回退命令原文（或等价
   内容），修正 §5 第 4 点悬空的"（§6）"引用；层 1/3 至少保留精简版
   摘要。
3. 已裁定：签名算法用 ECDSA P-256，不需要捆绑 Ed25519（本轮结论，
   非待办）。
4. **P2**：`OTA_ALLOW_PLAINTEXT` 打开后的局域网测试路径，应在 §3.1 明确
   写出"仿照 `penpal_api.cpp:pp_request()` 的 scheme 分支（`is_https`
   判断，:130-143）——`http://` 用纯 `WiFiClient`，`https://` 才用
   `WiFiClientSecure`"，否则宏打开后 manifest/bin 走 `http://` 时用
   `WiFiClientSecure` 对纯 HTTP 服务器根本握手不上，实验室联调步骤
   实际跑不通。

## 验证说明

- 本轮独立核实的源码位置：`ui_deckpro.cpp`（`low_voltage_timer_cb`
  :136、`low_voltage_timer` 创建于 `ui_deckpro_entry` :4793、
  `sleep_timer` 的屏幕级创建/销毁 :4498-4552、`esp_deep_sleep_start`
  :4454）、`ui_deckpro_port.cpp`（`ui_shutdown_on`/`PPM.shutdown()`
  :659-663）、`factory.ino`（`setup()` 边界 :621-803、`drv.begin()`
  `while(1)` :708-711，并确认全 `setup()` 内唯一一处）、
  `http_utils.cpp`（`http_apply_tls`/`CA_BUNDLE_MOZILLA` :25-40）、
  `penpal_api.cpp`（`pp_request` scheme 分支 :112-143）。
- 逐条对照了 v1 原文、v2 正文与 Codex/Grok/Qwen(opencode) 三份原始
  评审结果文件（未只读 v2 §11 的转述）。
- 本环境未安装 `pio`、未缓存 arduino-esp32 框架包，`esp32-hal-misc.c`
  行号级细节、`tools/sdk/esp32s3/sdkconfig` 具体行号仍属"沿用 Grok 独立
  核实结果，本轮未重新验证"，与前几轮的环境限制一致。
- 本文档只评审设计文本，不涉及任何实现代码（v2 状态仍为"待复审，未写
  实现代码"）。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**——上述 2 项 P1（低电关机互斥 + 发布/回退流程正文
  缺失）与 1 项 P2（明文测试路径的 client 分支说明）修订后可进入实现；
  P-256 签名算法选型本轮予以裁定通过，无需再等二期 Ed25519。
