# 评审结果：OTA 固件远程升级设计稿 v3（qwen）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md) **v3 修订稿**，锚 = **`45d11cf`**（"all five v2-round P1s and nine P2s folded in"）。工作副本另含 `a74f316`（仅追加 §头部"评审要求"指令块，不改设计实质，故沿用申请指定的 `45d11cf` 锚）。
- **评审类型**：设计稿对齐评审（review_guide §1.1 / §2.1 **L1 DOC-ALIGNED** track）
- **评审依据**：`docs/review_guide.md` v1.2；上位准则 `CLAUDE.md`、`docs/async_ipc_contract.md`、`docs/issue_list.md`；申请头部"评审要求"四条
- **评审范围**：v3 全文（§0 变化点 / §1 现状盘点 / §2 清单协议 / §3 模块 / §4 UI+全局互斥 / §5 回滚自证 / §6 契约登记 / §7 发布+基线 / §8 验证计划 / §12 v2→v3 处置对照）；并按 MACAO 模式 B（✅≠证据）逐条回正文核实 §12 处置是否真落地。
- **评审结论**：**C 部分接受**（暂未达 L1 DOC-ALIGNED）。v3 对 v2 五 P1 + 九 P2 的处置**绝大多数已真正落进正文**（非仅列表声明），事实根基经独立核验全部属实；但 v3 **新引入的 WDT 自证机制与既有 `setup()` 阻塞profile 冲突，构成 1×P1**（在机 #2 上确定性使好固件回滚、OTA 永不成功），另有 1×P2（seq 提交时机）+ 2×P3 + 2 Nit。**P1/P2 须在开工前修订设计**；P1 不可风险接受（review_guide §4.2）。

---

## 一、§1"已核实事实" + v3 新增代码引用逐项核验（review_guide §4.4 矩阵）

参照系（§3.5）= pda2 实际使用的框架预编译产物 + repo 工作副本源码：

| # | v3 声明 | 核验 | 状态 |
|---|---|---|---|
| ① | **Arduino 自动自证**：`initArduino()`（`esp32-hal-misc.c:207-235`）在 `setup()` 前对 `PENDING_VERIFY` 自动 `mark_app_valid`，除非覆盖 weak `verifyRollbackLater()` | 读框架 `cores/esp32/esp32-hal-misc.c`：`:207 bool verifyRollbackLater() __attribute__((weak));` `:208 {return false;}` `:222 initArduino()` `:225 if(!verifyRollbackLater()){` `:228 esp_ota_get_state_partition(...)` `:229 if(ota_state==ESP_OTA_IMG_PENDING_VERIFY)` `:231 esp_ota_mark_app_valid_cancel_rollback();` | ✅ VERIFIED（**行号精确**；这是整个回滚设计的地基，v3 §5.1"缺它一切形同虚设"判断正确） |
| ② | ESP-IDF OTA 未确认镜像**下一次 boot** 才 `PENDING_VERIFY→ABORTED` | 与 ① 同源机制（bootloader 在 boot 时判 pending 态）；设计引官方文档 | ◐ PARTIALLY_VERIFIED（机制自洽，未真机复现 pending→aborted——§8.1 已列入真机用例，L1 track 可接受） |
| ③ | `Update.abort()/begin(size)/end()/write()` 可用（自管流式，不用 HTTPUpdate） | 框架 `libraries/Update/src/Update.h`：`:81 void abort();` `:49 bool begin(size_t...)` `:55 size_t write(uint8_t*,size_t)` `:76 bool end(bool)` | ✅ VERIFIED |
| ④ | ECDSA P-256/SHA-256；用 `mbedtls_ecdsa_verify`，**禁** `mbedtls_ecdsa_read_signature`（吃 ASN.1 DER）；mbedtls 2.28 无原生 Ed25519 | `ecdsa.h:315 int mbedtls_ecdsa_verify(...)`、`:507 mbedtls_ecdsa_read_signature(...)`（确为 DER 路径）；`mbedtls/version.h` = **2.28.4** | ✅ VERIFIED（见 Nit-1：Ed25519 平台其实有，但在 libsodium 非 mbedtls） |
| ⑤ | IEEE P1363 大端 r‖s 64B base64；设备端 `mbedtls_mpi` 拆 r/s | mbedtls 2.28 提供 `mbedtls_ecdsa_verify(grp,hash,hash_len,Q,r,s)`，需 MPI 形式的 r/s——与"拆 r‖s 调 verify、禁 read_signature"一致 | ✅ VERIFIED（SPEC 自洽） |
| ⑥ | 电量外部供电判定 `ui_battery_27220_get_input()`（gauge 无效时不用 SOC=0 误拦） | `ui_deckpro_port.cpp:535 bool ui_battery_27220_get_input(void){return ui_battery_is_external_power_present();}`、`.h:138` 声明 | ✅ VERIFIED |
| ⑦ | 全局低电巡检 `low_voltage_timer_cb` / `ui_shutdown_on()`（§4.1 互斥对象） | `ui_deckpro.cpp:136 static void low_voltage_timer_cb(lv_timer_t*)`、`:35 low_voltage_timer`；`ui_deckpro_port.cpp:659 ui_shutdown_on()`、`.h:167` | ✅ VERIFIED（cb 与关机入口均存在，可加 `s_ota_hw_lock` 互斥） |
| ⑧ | 明文/https scheme 分支照 `penpal_api.cpp:pp_request()` 的 `is_https` 模式（:130-143） | `penpal_api.cpp:130 const bool is_https=(url.rfind("https://",0)==0);` `:131` 非 http(s) 拒绝 `:135 is_https && tls_mode!=INSECURE` `:142 WiFiClient plain; :143 WiFiClientSecure secure;` | ✅ VERIFIED（**行号精确**；模式可直接复用） |
| ⑨ | HTTPS 用 `setCACertBundle(CA_BUNDLE_MOZILLA)`，不经 `http_apply_tls()`、不继承 `tls_insecure` | `http_utils.cpp:42 client.setCACertBundle(CA_BUNDLE_MOZILLA);`、`:23 #include "ca_bundle_full.h"`；`http_apply_tls` 读全局 `s_tls_mode`（:25/:40 `setInsecure()`）——v3 绕开它即切断 v1 轮 P1 的继承链 | ✅ VERIFIED（**v1 轮 qwen P1 已闭合**：OTA 不再继承 AI trust 开关） |
| ⑩ | `OTA_URL` 走 `/env.cfg`（`env_get`，val ≤160 够用）；现行 12 条/159 字符 | `env_secrets.cpp:21 #define ENV_MAX_ENTRIES 12`、`:25 char val[160]`（注释"sk-api ~125 chars"）、`:51 s_env_cnt<ENV_MAX_ENTRIES` | ✅ VERIFIED（**数字精确**；删过时"8 条/95 字符"注释的处置正确） |
| ⑪ | `SCREEN2_2_ID` **追加枚举末尾**（不插 SCREEN2_1 与 SCREEN3 之间，避免重编号） | `ui_deckpro.h`：`SCREEN2_1_ID`(:54) 与 `SCREEN3_ID`(:55) 相邻；枚举尾为 `SCREEN_PENPAL_ID`(:80)——追加于尾不动既有值 | ✅ VERIFIED（处置正确；见 Nit-2 语义位置） |
| ⑫ | `pp_dbg_pool` 池水位观测点（721e04a 教训） | `ui_penpal.cpp` `pp_dbg_pool()`（721e04a 引入，已确认存在） | ✅ VERIFIED |

**小结**：v3 §1 + 正文 12 处代码/事实引用，11 条 `VERIFIED`、1 条 `PARTIALLY_VERIFIED`（②真机态，L1 可接受）。**事实功课扎实，参照系引对**（框架预编译产物 vs repo 内同名干扰文件）。

---

## 二、§12 v2→v3 处置是否真落地（MACAO 模式 B：✅≠证据）

逐条回 v3 正文核实（不只读 §12 表）：①§3.3 全局锁+取消令牌三检查点—**正文有**；②§5.2 task WDT 8s—**正文有**（但见 P1-1）；③§2.2 公钥入 tracked `ota_trust_anchor.h`—**正文有**；④§4.1 低电互斥+§4.2 Sleep 禁入+setSleep+吞键—**正文有**；⑤§7.1/7.2/5.4 发布步骤+USB 完整命令+三层表—**正文有**；⑥§2.1 notes/seq 入签名+单调—**正文有**（但见 P2-1）；⑦§7.3 tracked 基线—**正文有**；⑧§2.3 OTA_URL env—**正文有**；⑨§2.1 P1363 钉死+§8.3 DER 拒绝用例—**正文有**；⑩§3.2 超时模型+§6 契约例外—**正文有**；⑪§3.1 明文 plain client—**正文有**；⑫§10 实现合同清单—**正文有**；⑬Qwen P3（版本一致性入脚本 §7.1；x-MD5 不采纳因 sha256 已入签名）—**正文有，处置合理**。

**结论：13 条处置全部真正落进正文，无"列表声明 ✅ 但正文未改"的虚处置。** 这是 v3 的突出优点。

---

## Findings

### P1-1：8s task-WDT 自证窗口 < 既有 `setup()` 内 `A7682E_init()` 最坏阻塞（~8.4s）→ 机 #2 上好固件每次 boot 被 WDT 复位并回滚，OTA 永不能成功

- **位置**：设计 §5.2（"自证前把 loopTask 订阅 `esp_task_wdt`（8s）……在几个关键初始化步骤间显式喂狗"）；冲突代码 `examples/pda2/factory.ino:751`（`setup()` **inline** 调 `A7682E_init()`）+ `:501-540`（`A7682E_init` 体内 `:518 retry_cnt=5` / `:520 while(!modem.testAT(1000))` / `:524 delay(100)` / `:526 delay(1000)`）。
- **证据（最坏阻塞实测推算，modem 缺席/无响应板）**：
  - `while(!modem.testAT(1000))` 每次 `testAT` 无响应即满超时 **1000ms**；`retry++ > retry_cnt(5)` 为后置自增比较，需到第 **7** 次迭代才 `6>5` 成立 break → **7×1000ms = 7000ms**；
  - break 分支 `delay(100)+delay(1000) = 1100ms`；循环前 power-on `delay(10+50+10)=70ms`；循环后 `delay(200)`；
  - **合计 ≈ 8370ms ≈ 8.4s > 8s WDT 超时**。该循环内**无任何 `esp_task_wdt_reset()`**（`delay()` 不喂 task WDT，§5.2 自己也这么说）。
  - 触发面：`setup():751` **无条件** inline 调 `A7682E_init()`；**机 #2 = V1.1 音频选配版、无 A7682E**（`issue_list §16`："A7682E AT 无响应"）→ 每次 boot 必走满 ~8.4s。机 #1 有模块、`testAT` 快速成功则不受影响（但模块无响应/无天线时同样触发）。
- **影响**：v3 §5.2 把 loopTask 订阅 8s task-WDT、窗口覆盖"setup() 起点→自证点（首帧渲染后，§5.3）"。机 #2 上 `A7682E_init()` 在 setup() 内阻塞 8.4s 且无喂狗 → **WDT 在第 8s 复位**，永远到不了 §5.3 的自证点 → 下次 boot `PENDING_VERIFY→ABORTED` → **回滚刚装的好固件**。后果：**OTA 在机 #2（及任何 4G 无响应板）上确定性失败**，且表现为"每次升级都回滚"。坐标：状态机违约（layer-2 对健康固件误触发）+ 整变体功能不可用，**B/1/有声 → P1**（边界近 A：新固件"不能完成启动"，但安全回落旧固件、不变砖）。**P1 不可风险接受**（§4.2）。
- **两轴**：`VERIFIED`（CODE：`factory.ino:751/501-540` 阻塞时长可逐行推算；§5.2 WDT 窗口定义）/ `VIOLATES`（设计 §5.2 与既有 `setup()` 阻塞 profile 冲突；违背 §8.2 自己立的"自证不绑网络/外设、避免好固件被回滚"原则）。
- **最小修复**（设计须改，任选/组合，并补 §8 负向用例）：
  1. **收窄 WDT 订阅窗口**：在**会阻塞的外设 init（尤其 `A7682E_init()`）之后**再订阅 task-WDT，把窗口对准真正易死的 main-loop 入口段；或
  2. **在长阻塞 init 内喂狗**：`A7682E_init()` 的 retry 循环内加 `esp_task_wdt_reset()`（及任何 >数秒的 init 步骤）；或
  3. **抬高 WDT 超时**到 > 最坏 init（如 15s）——但削弱挂死检测灵敏度，不单独推荐；或
  4. **把 `A7682E_init()` 的 AT 握手移出 loopTask**（已有 `a7682_task`，:537，但握手在 inline 段）。
  - 推荐 **1+2**。**必补 §8 负向用例**：「机 #2（无 4G）刷入正常新固件 → boot 不得被 WDT 误复位/回滚」——现 §8.1 只测"挂死会回滚"，**缺"慢但正常的 boot 不回滚"**这一对偶用例（正是本 P1 的盲区）。

### P2-1：`seq` 在"开始下载前"写入 NVS → 下载失败后无法重试同版本（防回放状态机过激）

- **位置**：设计 §2.1（"验签通过后若 `seq <= last_seq` → 拒绝"＋"每次**实际开始下载前**把 `seq` 写入 NVS"）。
- **证据/反例**：清单 `seq=42`、`last_seq=41`。Check 通过（42>41）→ **下载前** `last_seq:=42`。下载因断网/45s 空闲超时失败（§3.2）→ 用户重试**同一** `seq=42` 清单 → `42<=42` → **被当回放/降级拒绝**。即一次失败下载把该版本永久锁死，直到发布 `seq=43`。这与 §8.4"下载中断网→abort+报错"的可重试预期矛盾（能 abort 却不能 retry）。
- **影响**：防回放状态机的持久态与"成功安装的最高 seq"语义不符（应只在**成功后**提交）。后果 B（状态机/正确性）；可达性 2（弱网下失败后重试，常见）；有声（误报"replay/downgrade"）。坐标 **B/2/有声 → P2**。
- **两轴**：`VERIFIED`（SPEC：§2.1 两条规则联立即可推出反例）/ `VIOLATES`（§2.1 自身的"防回放"意图被"下载前提交"破坏）。
- **最小修复**：`last_seq` 只在**安装成功后**提交。建议：下载前写 `pending_seq`（非 `last_seq`）；**新固件自证后**（§5.3）把 `last_seq:=pending_seq`（`pending_seq` 经 NVS 跨 reboot 传递）；失败/回滚则 `pending_seq` 永不提交 → 同版本可重试、旧版本仍被拒。设计须把"seq 提交时机"明确到"自证后"，而非"下载前"。

### P3-1：低电关机互斥无安全超时 → 自证后挂死且持锁时，临界电量下关机被无限抑制（深放电）

- **位置**：设计 §4.1（`low_voltage_timer_cb` 在 `s_ota_hw_lock` 置位时跳过 `ui_shutdown_on()`，"关机决策延后到任务结束后下一轮巡检"）。
- **证据/反例**：OTA 运行发生在用户进 Settings 时，此时固件**早已自证**（§5.3 首帧后）→ §5.3 自证后已**退出 WDT 订阅**→ OTA 写槽期间若挂死（layer-3，无 WDT）且 `s_ota_hw_lock` 仍置位 → 低电巡检永久跳过 `ui_shutdown_on()` → 临界电量下不关机 → 锂电池深放电/不洁掉电。§3.2 的 45s 空闲 + 10min 绝对 deadline 在**任务能跑到检查点**时会 abort 释锁，故常态可恢复；本项是"挂死绕过 deadline"的深角。
- **影响**：后果 C（硬件应力/深放电，可人工复位恢复——该挂死本就属 layer-3）；可达性 3（自证后挂死 + 持锁 + 临界电量，corner of corner）。坐标 **C/3 → P3/观察**。
- **两轴**：`PARTIALLY_VERIFIED`（纸面推演；未真机造挂死+低电）/ `CONFORMS`（设计已声明 layer-3 兜底，本项是纵深防御补强）。
- **最小修复**：给互斥加安全超时——`s_ota_hw_lock` 持续 > (10min deadline + 余量) 时，低电巡检恢复 `ui_shutdown_on()`（推定任务已挂）。一行时间戳比较，防无限抑制。

### P3-2：深度 1 队列同时承载"每 10% 进度"+"最终结果" → worker 背压耦合下载吞吐到 EPD 刷新

- **位置**：设计 §3.4（"结果 `ota_result_t` 走队列（深度 1）→ UI delete；进度每 10% 经队列刷 UI"）+ §6（"队列深度 1"列为契约例外）。
- **证据**：进度（高频，~10 条）与结果（1 条）若共用同一深度-1 队列、`xQueueSend(portMAX_DELAY)`，则 worker 在 UI 未排空前阻塞；EPD 局刷 ~0.3s/次 → 下载循环每 10% 被 EPD 拖 ~0.3s（全程 ~3s）。功能不丢（背压、非溢出），但把下载吞吐与 EPD 刷新强耦合；且与 `async_ipc_contract §2.7`（深度恒 4、≤1 在飞）的常态不同——v3 已正确登记为例外，但**进度+结果复用一个深度-1 队列**的语义未写明（是否同队列？进度被结果覆盖时如何？）。
- **影响**：后果 E/D（清晰度 + 轻微吞吐耦合）；可达性 2。坐标 **E/2 → P3/观察**。
- **两轴**：`VERIFIED`（DOC：§3.4 未区分进度/结果通道）/ `CONFORMS`（§6 已登记深度例外）。
- **最小修复**：§3.4 写明进度与结果是否同队列；若同队列，注明背压为预期（下载随 EPD 节奏）且 UI 必排空故 `portMAX_DELAY` 安全；或进度走独立轻量通道（单个 `atomic int percent`，UI 定时器轮询），结果独占深度-1 队列——更解耦。

### Nit-1：§2.1/§11"mbedtls 2.28 无原生 Ed25519"措辞不精确（平台经 libsodium 有 Ed25519）

- **位置**：设计 §2.1（"不捆绑 Ed25519——mbedtls 2.28 无原生支持"）、§11。
- **证据**：mbedtls 确为 **2.28.4**（`version.h`），2.28 无 EdDSA——**对 mbedtls 而言正确**。但框架 include 树含 `libsodium/.../sodium/crypto_sign_ed25519.h`、`ed25519_ref10.h` 等——**平台经 libsodium 提供 Ed25519**。故"无 Ed25519 支持"应精确为"**mbedtls 2.28 无 EdDSA；用 mbedtls ECDSA 可避免引入 libsodium 签名依赖**"。
- **影响**：E（文档准确性）。**算法决策本身不受影响**——ECDSA P-256 已 §11 裁决（Grok A + Claude），且复用已链接的 mbedtls、不增依赖，是合理选择。仅修正 stated rationale。坐标 **E/3 → Nit**。
- **最小修复**：把 §2.1/§11 的理由改为"mbedtls 2.28 无 EdDSA，选用 mbedtls ECDSA P-256 以复用 TLS 已有密码栈、不引入 libsodium"。

### Nit-2：`SCREEN2_2_ID` 追加于枚举末尾（`SCREEN_PENPAL_ID` 之后）语义位置怪 + §5.4/§4.1 等"子节号"实为编号列表项

- **位置**：设计 §4（枚举追加）、§12/§5（引用"§5.2/§5.3/§5.4""§4.1/§4.2"）。
- **证据**：(a) 功能上追加于尾正确（不重编号既有 id），但 `SCREEN2_2_ID`（Settings 第二页子屏）排在 `SCREEN_PENPAL_ID` 之后，枚举顺序与菜单层级不再对应——可读性 Nit（若有代码按枚举序假设层级则需留意，未见）。(b) §5 是编号列表 1-4、§4 是 bullet 子项 1-3，但正文/§12 以"§5.2/§5.4/§4.1"引用，暗示存在 `###` 子节实则没有——软引用，可读但易 mis-anchor。
- **影响**：E（可读性/文档一致性）。坐标 **E/3 → Nit**。
- **最小修复**：(a) 枚举尾加注释"// SCREEN2_2 appended at tail to avoid renumbering (Grok P2-6)"；(b) 把 §5/§4 的编号项升为 `### 5.1/5.2…` 真子节，或引用处改"§5 第 2 项"。

---

## 三、已通过项（v3 相对 v1/v2 的实质进步）

- **v1 轮 qwen P1（OTA 继承全局 TLS Trust 开关 × 无签名 = MITM 刷固件）已彻底闭合**：§3.1 OTA 用独立 `WiFiClientSecure` + `setCACertBundle(CA_BUNDLE_MOZILLA)`、**不经 `http_apply_tls()`、不继承 `tls_insecure`**；且 §2 引入 **ECDSA P-256 签名清单（含 version/url/size/sha256/seq/notes 六字段规范化字节串）+ P1363 编码钉死 + §8.3 DER/篡改/错公钥/缺字段/回放拒绝用例**——传输与签名双层，比 v1 的"仅 HTTPS + 无签名"强一个数量级。✅
- **v1 轮 qwen P2（OTA 任务须遵守 async_ipc_contract）已落地**：§6 显式登记契约行（消费者/任务/结果队列/busy+gen/取消=令牌+gen+1/超时=45s 空闲+10min 绝对 deadline），并把"栈 16KB、队列深度 1、任务跨页面存活"作为**显式例外声明**——符合 review_guide §4.3 `NO_CONTRACT→登记例外` 的正确做法（未重蹈 weather fetch 未登记覆辙）。✅
- **v1 轮 qwen P2（自证点太早、覆盖不到 UI 回归）已采纳**：§5.3 自证点从"setup() 末尾"移到**首帧 UI 渲染后**（`lv_async_call`/首个 `lv_timer_handler`），并把"自证后挂死"明确登记为 layer-3 已知边界。方向正确（P1-1 是其 WDT 实现的新副作用，非否定该改进）。✅
- **v1 轮 qwen P3（清单 version 与 bin 烘焙版本同步）已落地**：§7.1 发布脚本"校验清单 version == bin 内烘焙版本（结构性防错位）"。x-MD5 不采纳（sha256 已入签名）处置合理。✅
- **回滚地基核验扎实**：`verifyRollbackLater()` weak 覆盖（§5.1）+ "不重启不回滚→须 WDT 提供复位保证"（§1/§5）的机制认知正确（①核验）；三层模型（§5.4）+ USB 回退完整命令（§7.2，含"OTA 后运行槽可能是 app1、先读 otadata 再擦"修正）+ tracked 基线 `docs/ota-baseline.md`（§7.3，回应"备份不能是唯一兼容性依据"）。✅
- **全局互斥面广**：低电关机互斥（§4.1，正确指出 30% 前置检查挡不住下载途中放电）+ Sleep 禁入 + `WiFi.setSleep(false)` + waitbox 吞键 + 结束一律恢复——把"写 flash 期不可被断电/休眠/误触打断"覆盖完整。✅
- **§12 处置无虚标**：13 条逐条回正文核实全部真落地（见第二节）。✅

---

## 四、验证说明（评审方环境）

- 开发机（Windows，repo 工作副本 + `~/.platformio` 框架缓存）独立核验：读框架 `esp32-hal-misc.c`（①自动自证 + verifyRollbackLater weak，行号逐一对上）、`Update.h`（③abort/begin/end/write）、`ecdsa.h`+`version.h`（④mbedtls 2.28.4 + ecdsa_verify/read_signature）、`libsodium` 头（Nit-1 Ed25519 归属）；`rg` 核 repo 源码 ⑥⑦⑧⑨⑩⑪⑫ 全部命中（多处行号精确）；读 `factory.ino:501-540/621-751` 推算 `A7682E_init()` 最坏阻塞 ~8.4s（P1-1）。
- **未独立复跑** `pio run`：本对象是设计稿（无实现代码可编译）；固件体积等沿用 v2 已核值。
- **无设备**：②的 `PENDING_VERIFY→ABORTED` 真机态、§8 全部真机用例均无设备复现，按 §3.4 标 `UNKNOWN`/`PARTIALLY_VERIFIED`（设计稿阶段本就不要求 HW 证据，§8 验证计划已完整列入，符合 L1 track）。
- 未对工作区作任何代码修改（设计稿评审）。

## 五、对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：是。v3 是设计稿（无代码 diff），评审方未止于 §12 处置表，而是把 ①-⑫ 全部代码/事实引用拉到框架与 repo 实物逐条核验，并**主动追到设计未提及的交互面**——`setup()` 既有阻塞 profile（`A7682E_init()` inline @ factory.ino:751）× v3 新增 8s WDT 窗口，P1-1 即由此挖出（非申请靶向清单内、非任何前轮发现）。
2. **快照是否独立复跑**：部分。框架头文件、repo `file:line`、`A7682E_init` 阻塞时长推算均独立读取/计算（已记命令与命中行）；`pio` 构建与真机 OTA/回滚路径无环境，标 UNKNOWN（设计稿阶段合理）。
3. **申请"最没把握"处是否构造反例**：是。对 v3 最承重的两条新机制各构造反例——① **WDT 自证窗口**：反例 = 机 #2 无 4G → `A7682E_init` 阻塞 8.4s > 8s → 好固件回滚（**击穿**，P1-1）；② **seq 防回放**：反例 = 下载前提交 seq + 一次失败下载 → 同版本永久拒重试（**击穿**，P2-1）。对"签名清单足够安全"构造反例（篡改/DER/回放）→ §8.3 用例已覆盖，反例不成立（设计稳健）。

## 六、§9 自审清单（设计稿档：填【D】文档对齐 + 【ALL】通用条目）

```text
【D 文档对齐】
□ 字段/键名一致性：清单七字段(version/url/size/sha256/seq/notes/sig) 在 §2.1 JSON、签名字节串(§2.1 六字段规范化)、§7.1 脚本产出、§8.3 用例中命名一致 — 核过，一致 ✅
□ 代码块可执行：§2.1 JSON 合法可解析(无内嵌注释)；§3 函数签名/§3.3 原子声明为 C 伪码、语义自洽；§7.1/§7.2 命令为流程示意(标注了 esptool/write_flash 实参) — 核过 ✅
□ ✅≠证据：§12 每条"处置"未当作已落地，逐条回 §0-§10 正文核实(第二节) — 13/13 真落地 ✅；§1"已核实事实"未轻信，11/12 拉到实物核验 ✅
【ALL 通用】
□ 每条 P0/P1 给了 file:line + 反例：P1-1(factory.ino:751/501-540 + 8.4s 推算)、P2-1(§2.1 两规则联立反例) ✅
□ 作者"已核实/真机"声明独立核验或如实标 UNKNOWN：①③④⑥-⑫ 独立 VERIFIED；②标 PARTIALLY；真机态标 UNKNOWN ✅
□ 参照系与结论一致：核验对象 = pda2 实际用的框架预编译产物(非 repo 内同名干扰 sdkconfig/csv)；P1-1 参照系 = 现网 factory.ino 阻塞 profile ✅
□ "已实现≠已接线"/"已设计≠与既有代码相容"：P1-1 正是此类——v3 新 WDT 机制与既有 inline A7682E_init 不相容，设计未审计 setup() 阻塞 profile ✅(本轮最重要命中)
□ 结论相反时核对方事实(非比票数)：与 v2 轮 grok A/codex C/claude C 的关系——本轮 P1-1 为 v3 新机制引入、前轮无此对象，非分歧；v1 轮 qwen P1/P2/P3 已被 v3 闭合(第三节) ✅
```

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

> **开工前置条件**（review_guide §2.2 G-评审→G-合入）：
> - **P1-1（阻断，不可风险接受）**：解决 8s task-WDT 窗口 vs `A7682E_init()` ~8.4s inline 阻塞——收窄订阅窗口至阻塞 init 之后 / 在长 init 内喂狗 / 抬高超时；**并补 §8 负向用例"机 #2 好固件 boot 不被误回滚"**。
> - **P2-1**：`last_seq` 改为"自证后提交"（下载前只写 `pending_seq`），恢复失败可重试。
> - **P3-1/P3-2**：低电互斥加安全超时；§3.4 写明进度/结果队列关系。
> - **Nit-1/Nit-2**：修正 Ed25519 rationale 措辞；枚举尾注释 + §5/§4 子节号对齐。
> - 上述关闭后，设计稿可达 **L1 DOC-ALIGNED** 并据以开工（实现后另走 L2/L3 代码评审，锚 = 实现 commit）。
>
> **净评**：v3 是一份成熟度很高的设计——三轮迭代、14 条前轮发现真落地、事实引用经得起独立核验、v1 轮 qwen 的 P1/P2/P3 全部闭合。唯一阻断是 v3 **自己新引入的 WDT 自证机制**与既有 `setup()` 阻塞 profile 的冲突（P1-1），且恰好在机 #2 上确定性触发——这正是"设计机制必须对照现网代码现实核验"的典型案例。修掉 P1-1 + P2-1 即可开工。
