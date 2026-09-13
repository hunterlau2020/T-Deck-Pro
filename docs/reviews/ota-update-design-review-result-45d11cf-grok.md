# 设计评审结果：OTA 固件远程升级 v3（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)（文内标明 v3 修订稿）
- **评审提交锚**：申请书指定 `45d11cf`；工作树 HEAD = `a74f316`（仅多 15 行评审须知
  抬头，不改技术正文）。本结果按申请书文件名锚写入，不覆盖 v2 结果。
- **对照**：
  - v2 Grok A（6 P2）：[ota-update-design-review-result-v2-grok.md](ota-update-design-review-result-v2-grok.md)
  - v2 Codex C（3 P1）：[ota-update-design-review-result-be6a52c-codex.md](ota-update-design-review-result-be6a52c-codex.md)
  - v2 Claude C（2 P1）：[ota-update-design-v2-review-result-claude.md](ota-update-design-v2-review-result-claude.md)
- **评审结论**：**C 部分接受**。v2 轮 5 条 P1 的**机制方向**全部落地（硬件锁、
  公钥入仓、低电互斥、发布/USB 正文、WDT 提供复位）。**不能给 L1 DOC-ALIGNED**：
  新写的自证窗口把 TWDT 说成「8s、setup 首行订阅」，与 Arduino-ESP32 2.0.14
  全局 TWDT=5s 以及现有 `setup()` 内已有的多秒阻塞**数字对不上**——按字面实现会
  把健康新固件误回滚。P0/P1 不可风险接受（`review_guide.md` §4.2）。
- **算法裁定（沿用 v2）**：ECDSA P-256，不捆绑 Ed25519。

---

## v2 P1 / P2 闭合核对

| 来源 | v3 处置 | 状态 |
|---|---|---|
| Codex P1-A 页面 gen 不能互斥写 flash | §3.3 `s_ota_hw_lock` CAS + 取消令牌三检查点 + `Update.abort()` 不切槽 | **闭合** |
| Codex P1-B 无复位则不回滚 | §5 订阅 loopTask TWDT；§8.1 拆忙等/让出型/自证前后 | **方向对，参数错 → 本轮 P1-1** |
| Codex P1-C 公钥进 gitignore | §2.2 `ota_trust_anchor.h` tracked；私钥才 gitignore | **闭合**（编码格式仍歧义，P2-2） |
| Claude P1-A 低电关机未互斥 | §4 `low_voltage_timer_cb` 见锁跳过 `ui_shutdown_on()`；Sleep 禁入；`WiFi.setSleep(false)`；§8.5 用例 | **闭合** |
| Claude P1-B 发布/USB 正文丢失 | §7.1/§7.2 + §5 层 1/2/3 精简表 | **闭合**（USB 命令仍非可照抄，P3） |
| Grok P2-1 `OTA_URL` | §2.3 `env_get`，val≤160；example 注释 12/159 | **闭合** |
| Grok P2-2 公钥/P1363 | tracked 头 + 禁 `read_signature` + §8.3 DER 拒绝 | **部分**：P1363 钉死；点格式未钉死 |
| Grok P2-3 10min deadline | §3.2 + §6 | **闭合** |
| Grok P2-4 写槽期关机/键/sleep | §4 收口 | **闭合** |
| Grok P2-5 回滚用例物理矛盾 | §8.1 a–d 重写 | **用例对**；依赖 P1-1 的 WDT 真能撑过健康 `setup()` |
| Grok P2-6 实现合同 | §10 + §9 拆提交 + 枚举追加 + abort | **闭合**（进度队列深度 1 未收，P2-4） |
| Codex P2 notes/seq/基线 | §2.1 + §7.3 | **闭合**（`last_seq` 写入时机错，P2-1） |
| Claude P2 明文 scheme | §3.1 仿 `pp_request` | **闭合** |
| Qwen P3 版本一致性 / x-MD5 | 发布脚本校验；不采纳 x-MD5 | **维持** |

四方已闭合、本轮不再开放：专用 TLS、ECDSA P-256、自管流式、`verifyRollbackLater`、
`drv.begin()` 拆雷、手动触发、契约行草案。

---

## Findings

### P1-1：自证窗口「setup 首行 + 8s TWDT」会被现有 `setup()` 误杀健康固件

- **位置**：设计稿 §5.2、§0.2、§8.1；对照
  `examples/pda2/factory.ino:621-799`、`:501-540`（`A7682E_init`）、
  `examples/pda2/peri_gps.cpp:32-50` + `:320-382`（`GPS_Recovery`/`getAck`）、
  `:146-189`（`ink_screen_init` 全刷）、`:764`（`disp_full_refr`）。
- **证据（CODE + SPEC + 框架 sdkconfig）**：
  1. Arduino-ESP32 2.0.14 **esp32s3** `sdkconfig`：
     `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`、`CONFIG_ESP_TASK_WDT_PANIC=y`、
     idle CPU0 已订阅。TWDT 是**全局一个超时**：`esp_task_wdt_add(loopTask)`
     **不会**把超时改成 8s；要 8s 必须 `esp_task_wdt_init(8, true)`，同时
     把 idle 的超时也拉到 8s。设计只写「订阅 8s」，没写 re-init / 自证后
     恢复 5s。
  2. `loopTask` 默认**未**订阅（`main.cpp` 里 `loopTaskWDTEnabled = false`；
     `enableLoopWDT()` 不会在 `setup()` 前自动调用）。`delay()` 只喂 idle，
     **不**喂 loopTask——这点设计说对了。
  3. `enableLoopWDT()` / `esp_task_wdt_reset()` 的自动喂只发生在 `setup()`
     **返回之后**的 `for(;;) loop()`。窗口若从 setup 首行算起，整段
     `setup()` 必须**手工**喂狗，否则到期即 panic 复位 → pending 新镜像被
     回滚。
  4. 现有 `setup()` 里**单函数**就已经接近或超过 5s/8s，且函数内部没有
     `esp_task_wdt_reset()`：
     - `A7682E_init`：`testAT(1000)` 最多 5 次 + 失败路径 `delay(1000)` ≈
       **6.1s**。机 #2 音频版无调制解调器时**每次开机走满失败路径**。
     - `gps_init`：`setupGPS()` 已注释，但 `GPS_Recovery()` 最多两次；每次
       4×`getAck`（800ms 忙等）≈ **3.2s / 6.4s**。
     - `ink_screen_init` + `disp_full_refr`：各一次 EPD 全刷 **1–2s**。
  5. 设计缓解语是「几个关键初始化步骤间显式喂狗」。在 peri_init **之间**
     喂狗填不满函数**内部**的 6s `delay`/`testAT`。8s − 6.1s = 1.9s
     余量，容不下同函数前后的 GPIO 脉冲，更容不下「忘记 re-init、实际 5s」。
- **反例序列（机 #2，字面实现）**：OTA `Update.end(true)` → 重启进新槽
  `PENDING_VERIFY` → setup 首行 `esp_task_wdt_add`（超时仍 5s）→
  `A7682E_init` 走满 5×1s → TWDT panic → 下次 boot 回滚到旧槽。串口有
  WDT backtrace，用户看到「升级成功后又回到旧版本」，同一 `seq` 固件无法
  稳定住。机 #1 若 AT 一次性成功则可能侥幸，双机门禁仍失败。
- **影响**：后果 **B**（层 2 回滚契约被健康启动误触发；新镜像「不能完成
  首次启动」）；可达性按目标机取 **1**（机 #2 每次 OTA 首启即中；不是
  「仅音频功能」的档 3 边角）。有声（WDT panic）。坐标 **B/1/有声 → P1**。
  回升条件：无（两台量产形态都会跑 `A7682E_init`）。
- **两轴**：`VERIFIED`（CODE：`factory.ino`/`peri_gps.cpp` 行号；框架
  sdkconfig 自 GitHub `arduino-esp32/2.0.14/tools/sdk/esp32s3/sdkconfig`
  拉取，本机无 pio 缓存故标框架事实为独立复跑、非本仓 blob）/
  `VIOLATES`（设计 §5.2 的 8s 数字 vs 现有 setup 阻塞；且会让 Codex P1-B
  的「复位保证」打在错误对象上）。
- **最小修复**（写入 §5/§8，不必另开长篇 v4，但**改完再编码**）：
  1. 明确：窗口内 `esp_task_wdt_init(T, true)` + `esp_task_wdt_add(loopTask)`；
     自证后 `esp_task_wdt_delete(loopTask)` 并 `esp_task_wdt_init(5, true)`
     恢复框架默认（idle 继续被看）。
  2. `T` 不得拍脑袋 8s。先在**当前**固件对两机打点：`setup()` 入口、
     每个 `peri_init_*`、`lvgl_init`/`ui_deckpro_entry`/`disp_full_refr`、
     自证点的 `millis()`。取 max(单段) × 2 或整段 setup+首帧 的上限（建议
     起步 **30s**），再在函数**内部**喂狗：`A7682E_init` 的 `testAT` 循环、
     `GPS_Recovery`/`getAck`、EPD `nextPage`/BUSY 等待、`pcm5102a_init`
     写 WAV。
  3. §8 增加正向用例：**健康**固件 OTA 后两机都必须自证成功、不得回滚；
     记录 reset 原因（应无 TWDT）与 time-to-valid。
  4. 8s 只适合「已测单段 <3s」之后再收紧，不能当设计初值。

### P2-1：`last_seq` 在下载前写入，失败后同一版本无法重试

- **位置**：§2.1「每次实际开始下载前把 `seq` 写入 NVS」；验签后
  `seq <= last_seq` → 拒绝。
- **证据（SPEC/SIM）**：单调计数的目的是防**已成功安装**的清单回放，不是
  惩罚传输失败。反例：清单 `seq=42`，`last_seq=41` → 用户 Update → NVS
  写成 42 → 45s 空闲超时 / 10min deadline / `Update.abort()` → Check 再
  拉同一清单 → `42 <= 42` 拒绝。发布端必须再签 `seq=43` 的**同一** bin
  才能救。叠加 P1-1（健康固件被 WDT 回滚时 last_seq 已前进）会变成
  「回滚到旧版 + 不能再装刚才那包」。
- **影响**：后果 **B**（seq 状态机把「看见过」当成「已安装」）；可达性
  **2**（下载失败/超时是 2.1MB 路径的常态）。有声（拒绝提示）。坐标
  **B/2/有声 → P2**。与 P1-1 同时发生时回升为实质挡升级，须同批改。
- **两轴**：`VERIFIED`（DOC）/ `NO_CONTRACT`（NVS 语义未入契约表，属
  设计自洽问题）。
- **最小修复**：`last_seq` **只在 `Update.end(true)` 成功之后**写。Check
  验签阶段只比较、不写。失败/取消/abort 保持原 `last_seq`。§8.2/§8.3
  加：故意断网失败后同一 seq 必须仍能重试；成功安装后回放必须拒绝。
  回滚发生时 last_seq 已前进 → 不能重装**同一坏包**，这是期望；重装需
  `OTA_ALLOW_DOWNGRADE` 或 seq+1。

### P2-2：信任根点格式自相矛盾（「65B 压缩点」）

- **位置**：§2.2「公钥（raw 65B 压缩点或 64B Q_x||Q_y）」。
- **证据**：SEC1：压缩点 = `0x02/0x03` + 32B = **33B**；未压缩 =
  `0x04` + 32B + 32B = **65B**；无前缀仿射 = 64B。`mbedtls_ecp_point_read_binary`
  认 33 或 65，不认「65B 压缩」。两种都写进头文件 = 发布脚本与固件各
  实现一套就会**全部验签失败**（或偶然一种能过、换机构建不过）。
- **影响**：后果 **D**（算法/编码边界）；可达性 **1**（第一次 Check
  真清单即触发，若编码选错）。有声。坐标 **D/1/有声 → P2**。
- **两轴**：`VERIFIED`（SPEC 与 SEC1/mbedtls 常识）/ `NO_CONTRACT`。
- **最小修复**：钉死一种，建议 **未压缩 65B：`0x04 || Qx || Qy`**，头文件
  十六进制数组 + 注释禁止压缩。发布脚本与设备同一 `mbedtls_ecp_point_read_binary`。
  `key_id` 可保留作诊断，不必进签名。§8.3 加「压缩点 / 64B 无前缀 → 拒绝」
  （若钉死 65B）。

### P2-3：Check/precheck 未规定 worker；UI 线程 HTTPS 会卡住 EPD/键

- **位置**：§3 API `ota_fetch_manifest` / `ota_precheck` 为同步 `bool`；
  §3.4 任务形态只写 `ota_run`。
- **证据**：现有 HTTPS 消费者（WiFi Test / AI Test / PenPal / Chat）都
  是 worker + 队列。Check 含 TLS + 清单下载 + 验签，秒级阻塞。若在
  LVGL 事件里直接调，`lv_timer_handler`/`keypad_loop` 停转，与「主循环
  持续泵」只覆盖 Update 阶段矛盾。
- **影响**：后果 **C**（UI 冻，可重启）；可达性 **1**（按 API 字面把
  Check 放进按钮回调即中）。有声。坐标 **C/1/有声 → P2**。
- **两轴**：`VERIFIED`（SPEC 缺任务）/ `UNDECIDED`（实现可自行包一层
  worker；设计未禁止，也未要求）。
- **最小修复**：Check 与 Update 共用同一 worker 状态机（或两个任务仍
  共用 `s_ota_hw_lock` 之外的 busy）。`ota_fetch_manifest` 只作任务内
  函数。确认框之前的电量/`OTA_URL` 检查可在 UI 线程做（无网络）。

### P2-4：进度用深度 1 队列 + `portMAX_DELAY`，写 flash 时可能与 EPD 互锁

- **位置**：§3.4「进度每 10% 经队列刷 UI」；§6 例外声明深度 1。契约
  规则 7 默认 `xQueueSend(..., portMAX_DELAY)`。
- **证据（SIM）**：OTA 任务 core 0 上 `Update.write` 会占 flash cache；
  UI 刷 EPD 也走 SPI/flash。深度 1 时若发送阻塞等 UI 消费，UI 又卡在
  被 flash 写入拖住的 `disp_full_refr`/`flush`，下载任务停在
  `xQueueSend`，进度与写入互相等。PenPal/Chat 用深度 4 +「busy 期间
  只有一个在飞结果」，OTA 进度是**高频**事件，模型不同。
- **影响**：后果 **C**（卡在 do not power off，直到 10min 绝对超时才
  abort——若超时检查也在同一被卡住的任务里，则连超时都不跑，升为冻
  结需人工复位）。可达性 **2**（EPD 刷新与 10% 进度撞车）。半静默。
  坐标 **C/2/有声 → P2**（超时文案会出现则有声；死锁则近静默，按有声表
  不升级以免双计）。
- **两轴**：`PARTIALLY_VERIFIED`（SIM，无真机）/ `VIOLATES`（契约规则 7
  的「队列不会积压」前提被深度 1 + 多进度事件打破，§6 例外未改发送策略）。
- **最小修复**：进度用 `volatile`/`atomic` 百分比，UI 定时器轮询；队列
  **只承运结果** `ota_result_t`。禁止在 `Update.write` 循环里
  `xQueueSend(..., portMAX_DELAY)`。绝对 deadline 用 `millis()` 在写入
  循环本地检查，不依赖 UI。

### P3（Nit，不挡开工，编码时收）

1. **明文策略只写了 `OTA_URL`**（§2.3），未写清单内已签名的 `url` 也必须
   在 `OTA_ALLOW_PLAINTEXT=0` 时拒绝 `http://`。发布脚失手签明文 bin URL
   时 HTTPS 清单挡不住固件 MITM。同开关两处都判。
2. **规范化字节串**「每字段一行」未禁止 `notes` 含 `\n`/`\r`。应用 JSON
   字段拼接而非按行 split，并拒绝 notes 内换行。
3. **§7.2 USB 命令** `<8KB 0xFF>` 不能照抄；应写
   `esptool.py erase_region 0xE000 0x2000`。擦空 otadata 后 bootloader
   默认 **ota_0**，不是「另一张已验证槽」。两次 OTA 后 ota_0 可能已是
   较新镜像——须先 `read_flash` 再决定擦还是重写哪一槽（正文已提到读
   otadata，命令本身仍含糊）。
4. **§4「OTA busy 任一阶段」用 `s_ota_hw_lock` 挡关机**，但锁在
   `Update.begin()` 前才取，Check/确认框阶段不持锁。Check 被低电关机可
   接受；把互斥条件写成 `s_ota_hw_lock || s_ota_busy` 以免以后有人把
   下载 GET 放在取锁前。
5. **§1 承重数字**（槽 0x640000、`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`、
   otadata @0xE000）写「同 v2、不再复述」。USB 回退已有地址；仍建议 §7.3
   基线文件列一行表，避免只存在于 `be6a52c` 正文。

---

## 已通过项

- 全局硬件锁与 UI gen 解耦；取消令牌三检查点；离页不启动第二写槽任务。
- 公钥 tracked / 私钥 gitignore；换根仅 USB。
- 低电定时器与 `PPM.shutdown()` 互斥；Sleep 入口拒绝；`WiFi.setSleep(false)`
  成对恢复；无效 SOC 改看外部供电。
- 流式下载、Content-Length、槽容量、失败 `abort()`、清单快照。
- 读空闲 45s + 绝对 10min；明文宏走 `WiFiClient`。
- P1363 r||s + 禁止 `mbedtls_ecdsa_read_signature`。
- `SCREEN2_2_ID` 追加在枚举末尾（当前末项为 `SCREEN_PENPAL_ID`，
  `ui_deckpro.h:80`）；池水位观测点。
- 发布脚本读 `utilities.h` 版本；`notes`/`seq` 入签名。
- 提交拆成 4 组；回滚真测先于首次真实 OTA。
- `env_get` 12 条 / val[160] 与 §2.3 一致（`env_secrets.cpp:21,25`）；
  现 example 仍写「Max 8 / 95 chars」（`:10`），v3 已要求改掉。

---

## 验证说明

- 读 v3 全文（HEAD `a74f316` = `45d11cf` + 评审须知抬头）及三份 v2 结果。
- CODE：`factory.ino` setup/`A7682E_init`/`drv.begin`、`peri_gps.cpp`
  `gps_init`/`GPS_Recovery`、`ui_deckpro.cpp` `low_voltage_timer_cb:136-174`
  与 `ui_deckpro_entry` 创建低电定时器、`env_secrets.cpp`、`ui_deckpro.h`
  屏枚举、`async_ipc_contract.md` 规则 6/7/10、`http_utils.cpp` TLS。
- 框架：GitHub `espressif/arduino-esp32@2.0.14`
  `tools/sdk/esp32s3/sdkconfig`（TWDT 5s / rollback=y）+
  `cores/esp32/esp32-hal-misc.c`（`verifyRollbackLater` 默认 false、
  `enableLoopWDT`/`initArduino` 自证）。本环境无 `~/.platformio`、无真机、
  未 `pio run`。
- 设计稿评审：DOC/SPEC/CODE/SIM；HW 全 `NOT_APPLICABLE`。

---

## 对称三格

1. **全区间**：技术正文 = `45d11cf`；`45d11cf..a74f316` 只改申请书抬头。
   对照文件不限于设计稿：`factory.ino`、`peri_gps.cpp`、`env_secrets.cpp`、
   `ui_deckpro.{h,cpp}`、契约、v2 三份结果。无实现代码落入本锚。
2. **独立复跑**：未装 pio / 无设备。框架 sdkconfig 与 `esp32-hal-misc.c`
   从 GitHub 2.0.14 tag 拉取。setup 时长为源码静态加总（SIM），未用
   串口 `millis()` 实测，故 P1-1 的「机 #2 必炸」对 **5s 未 re-init** 为
   `VERIFIED`，对「已 re-init 30s + 内喂狗」为 `CLAIM_ONLY`（那是修复后
   的路径）。
3. **最没把握处反例**：v3 无「最没把握」栏。对新增承重件构造了两反例：
   （a）机 #2 健康 OTA 首启走满 `A7682E_init` 失败路径；（b）`seq` 下载前
   落盘后超时。均成立。

---

## 声明矩阵（设计强声明）

| # | 声明 | 状态 |
|---|---|---|
| 8s TWDT 覆盖自证前任何死循环且健康启动不受影响 | **CONTRADICTED**（P1-1） |
| 不重启不回滚；须 WDT 给复位 | `VERIFIED`（IDF 状态机 + 本轮同意） |
| `verifyRollbackLater` 默认 false、`initArduino` 会自动自证 | `VERIFIED`（hal-misc.c 原文） |
| `env_get` 12/159 够 `OTA_URL` | `VERIFIED`（`env_secrets.cpp`） |
| 低电定时器全局、从不 pause | `VERIFIED`（`ui_deckpro.cpp:4793`） |
| 公钥 tracked 才可审计 | `VERIFIED`（同意 Codex P1-C 闭合） |
| 作者「v3 已逐条处置全部 P1」 | **CLAIM_ONLY / 部分 CONTRADICTED**（P1-B 参数未闭合） |

---

## §9 自审清单【D】/【ALL】

- [x] 15 每条 P1/P2 有 file:line + 反例
- [x] 16 作者闭合声明当 CLAIM_ONLY；独立核实源码；无 pio 标 UNKNOWN（BUILD）
- [x] 17 参照系 = HEAD 设计稿 + 2.0.14 GitHub sdkconfig，非本机框架缓存
- [x] 19 推演了失败下载、误回滚、双任务写 flash、低电关机、明文 URL
- [x] 21 键名：`OTA_URL` / NVS `ota` `last_seq` / 清单字段与读取点一致；
      `url`（bin）的 https 策略与 `OTA_URL` 不完全一致（P3）
- [x] 22 JSON 可解析；§7.2 命令**不可**照抄（P3）；§2.2 65B 压缩点不可执行（P2-2）
- [x] 23 无把计划当 ✅；v3 修订表当作者声明复核，P1-B 未过

【C】条目（1–9、12–14、18）本轮无实现代码，标 `NOT_APPLICABLE`。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（重开长篇 v4）
- [x] C. **部分接受**——机制方向可保留；**P1-1 必须改写 §5/§8 后再编码**。
      P2-1/P2-2 建议同一补丁写进设计（各一段话），避免实现期再踩。
      P2-3/P2-4 可作实现合同，不强制再送设计轮。

**不给 L1 的原因**：L1 要求伪代码/超时数字与硬件事实自洽。8s + setup
首行与 `A7682E_init`/`GPS_Recovery`/EPD 全刷冲突，属于文档内数字被源码
证伪，不是「实现时注意一下」。

**给 C 而非 B 的原因**：Codex 三条 P1 的结构解（锁、tracked 公钥、WDT
*作为复位来源*）是对的；低电/发布正文/P1363/10min/OTA_URL 均已在正文。
只需改自证窗口的超时模型与喂狗清单，不必推倒 v3。

**建议补丁范围（设计，约一节）**：§5.2 换成「re-init T（先测后填，默认
≥30s）+ 具名喂狗点 + 自证后恢复 5s」；§2.1 `last_seq` 改 end 后写；
§2.2 钉死 65B 未压缩。然后按 §9 顺序：健康固件 time-to-valid 打点 →
自杀固件回滚矩阵 → 再写生产代码。
