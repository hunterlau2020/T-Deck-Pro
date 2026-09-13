# 评审结果：OTA 固件远程升级设计稿 v4（qwen）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md) **v4 修订稿**，锚 = **`3eb6f7e`**（"the WDT self-validation window redesigned per four reviews"；git log 最新一条，符合申请头部"锚=本稿 commit SHA"）。
- **评审类型**：设计稿对齐评审（review_guide §1.1 / §2.1 **L1 DOC-ALIGNED** track）
- **评审依据**：`docs/review_guide.md` v1.2；上位准则 `CLAUDE.md`、`docs/async_ipc_contract.md`、`docs/issue_list.md`；申请头部"评审要求"
- **评审范围**：v4 全文（§0 变化点 / §1 新增已核实事实 / §2 清单协议 / §3 全异步模块 / §4 UI+互斥 / §5 回滚自证 / §6 契约 / §7 发布+基线 / §8 验证计划 / §12 v3→v4 对照）；逐条回正文核实 §12 处置是否真落地（MACAO 模式 B：✅≠证据）。
- **前轮关系**：本文件是 **v4 轮**（锚 `3eb6f7e`）的 qwen 评审；v1 轮见 `…-36e88df-qwen.md`、v3 轮见 `…-45d11cf-qwen.md`，各轮独立锚、绝不覆盖（§7.2）。v4 已显式吸收 v3 轮 qwen 的 P1-1/P2-1/P3-1/P3-2/Nit-1/Nit-2（§0、§12 行 1/4/7/8/13/14）。
- **评审结论**：**C 部分接受**（接近 L1 DOC-ALIGNED，但两处异步生命周期规格缺口须先补进设计）。**无 P1**——v3 轮 qwen P1-1（WDT 窗口 vs `A7682E_init` 阻塞）已被 v4 §5.2 重设计**正面关闭**，且其核心 API 假设经独立核验成立。余 **2×P2**（§3 全异步重写的单飞守卫 + 验签快照所有权）+ **2×P3**（§1 一处"已核实"数值低估、§5.2 校准度量mis-target）+ 1 Nit。P2 触及 `async_ipc_contract` 承重不变量，须在开工前补进设计。

---

## 一、v4 §1"新增已核实事实" + 新代码引用逐项核验（review_guide §4.4 矩阵）

| # | v4 声明 | 核验 | 状态 |
|---|---|---|---|
| ① | **TWDT 框架默认**：`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`、`PANIC=y`、idle CPU0 已订阅、**loopTask 默认未订阅**（`main.cpp loopTaskWDTEnabled=false`）、`delay()` 不喂 loopTask | 框架 `tools/sdk/esp32s3/sdkconfig:1179 PANIC=y`、`:1180 TIMEOUT_S=5`、`:1181 CHECK_IDLE_TASK_CPU0=y`；`cores/esp32/main.cpp:34 bool loopTaskWDTEnabled;`、`:69 loopTaskWDTEnabled=false;`、`:47 if(loopTaskWDTEnabled){…reset}` | ✅ VERIFIED（**逐项精确**） |
| ② | **`esp_task_wdt_init(T,true)` re-init 可改超时**（§5.2 核心机制；自评②"最没把握、无真机复跑"） | 框架 `esp_system/include/esp_task_wdt.h:28-30`：*"If the TWDT is already initialized when this function is called, this function **will update the TWDT's timeout period and panic configurations** instead."* | ✅ VERIFIED（**DOC 级确证**：IDF 4.4 的 init 再调用即更新 timeout/panic，无需 deinit；v4 自评② 的 API 不确定性在文档层**已闭合**——仍建议 §8.1a 真机复核 panic 实际复位） |
| ③ | `esp_task_wdt_add/delete/reset` 可用；deinit 在有订阅者时报错 | `esp_task_wdt.h:59 deinit`（`:56 ESP_ERR_INVALID_STATE: tasks still subscribed`）、`:83 add`、`:105 reset`、`:125 delete` | ✅ VERIFIED（v4 用 add(loopTask)/reset/delete + init 改超时，**不走 deinit**，规避了"idle0 仍订阅则 deinit 失败"陷阱——选型正确） |
| ④ | **EPD 首帧完成序列号** `ui_disp_full_refr_seq()`/`ui_disp_flush_done_seq()` 存在（`ui_deckpro_port.h:42-43`；底层 `factory.ino:864` 实现、`:241` 写入） | `ui_deckpro_port.h:42/:43` 声明；`ui_deckpro_port.cpp:42/:47` **定义**；底层 `factory.ino:863 disp_full_refr_seq(){disp_full_refr(); return disp_flush_req_seq;}`、`:870 disp_flush_seq_done(){return disp_flush_done_seq;}`、`:241 disp_flush_done_seq=disp_flush_req_seq /* THIS frame reached the panel */`、`:856 disp_flush_req_seq++` | ✅ VERIFIED（行号精确；机制成立） |
| ⑤ | （隐含）首帧门控用"取号→轮询 done≥target"——是否为既有惯用法 | `ui_deckpro.cpp:2808 wifi_scan_ovl_flush_seq=ui_disp_full_refr_seq();` → `:2830 ui_disp_flush_done_seq()>=wifi_scan_ovl_flush_seq`（WiFi 扫描覆盖层）；`:4518 sleep_wait_seq=ui_disp_full_refr_seq();` → `:4494 ui_disp_flush_done_seq()>=sleep_wait_seq`（Sleep 倒计时） | ✅ VERIFIED（**v4 §5.2 自证门控复用了 repo 内两处已验证的同款惯用法**——取号一次、存目标、轮询 done；非新造机制，降低实现风险） |
| ⑥ | `setup()` 阻塞 profile：`A7682E_init` 失败路径 **≈6.1s**（机 #2 每次开机必走满） | `factory.ino:751 peri_init_st[E_PERI_A7682E]=A7682E_init();`（**inline 于 setup()**）；`:517-531` retry 循环 | ⚠️ **数值低估**——实测推算 **≈8.4s**（见 P3-1）；函数存在性与 inline 调用属实 |
| ⑦ | `GPS_Recovery ≈3.2–6.4s`、`getAck` 每轮喂狗点 | `peri_gps.cpp:11 static bool GPS_Recovery();`、`:385 GPS_Recovery(){…}` 内 `:392/:396/:400/:407 getAck(...)`、`:320 static int getAck(...)`；`gps_init()`(:32) `:40/:43 result=GPS_Recovery()`；`factory.ino:749 gps_init()` inline 于 setup() | ✅ VERIFIED（函数存在、在 setup() 阻塞路径、getAck 多轮——具名喂狗点定位准确） |
| ⑧ | `pcm5102a_init` 写 WAV（setup 阻塞步） | `factory.ino:542 bool pcm5102a_init(void)`、`:559 SPIFFS.begin(false)&&!SPIFFS.exists("/pcmtone.wav")`、`:562 SPIFFS.open("/pcmtone.wav",FILE_WRITE)`、`:581 "[PCM] test tone written"` | ✅ VERIFIED |
| ⑨ | mbedtls 2.28.4 无 EdDSA；平台经 libsodium 有 Ed25519（采 mbedtls ECDSA 避免引入 libsodium） | `mbedtls/version.h` = 2.28.4；`libsodium/.../sodium/crypto_sign_ed25519.h` 等存在 | ✅ VERIFIED（**v3 轮 qwen Nit-1 措辞已被 v4 §11 精确采纳**） |

**小结**：v4 §1 九处新增引用，**8 条 VERIFIED（多处行号精确）**、1 条数值低估（⑥，见 P3-1，非_load-bearing）。**v4 自评②（最没把握的 WDT re-init 语义）经框架头文件独立确证为 API 正确**——这是本轮最重要的正向核验：v3 轮 qwen P1-1 的修复机制（re-init 改超时 + 具名喂狗 + 30s 下限）在 API 层成立。

---

## 二、§12 v3→v4 处置是否真落地（逐条回正文）

①§5.2 自证窗口重设计（废 8s→校准 T、re-init、四处具名喂狗、自证后恢复 5s）—**正文有**；②§5.2 自证点改 EPD 首帧序列号—**正文有**（且复用既有惯用法，⑤）；③§5.2/§7.3 PANIC=y 入基线 + "复位"改"预期(§8.1a 待验证)"—**正文有**；④§2.1 `last_seq` 仅 `Update.end(true)` 后写 + §8.5 同 seq 重试用例—**正文有**（**v3 轮 qwen P2-1 闭合**）；⑤§2.2 公钥钉死 65B 未压缩 SEC1 + §8.4 拒绝用例—**正文有**；⑥§3 Check/Update 共用 worker、UI 零网络—**正文有**（但见 P2-1/P2-2）；⑦§3.4/§6 进度原子轮询、队列只运结果、写循环禁 xQueueSend—**正文有**（**v3 轮 qwen P3-2 闭合**）；⑧§4.1 互斥 `lock||busy` + 10min 安全阀—**正文有**（**v3 轮 qwen P3-1 闭合**）；⑨§2.1 签名 url 双查 + notes 禁换行—**正文有**；⑩§7.2 USB 命令可照抄 + 先读 otadata—**正文有**；⑪§7.3 基线承载表—**正文有**；⑫§8.3 硬件锁并发用例—**正文有**；⑬§11 Ed25519 措辞—**正文有**；⑭§4 枚举注释 + 子节引用式—**正文有**。

**结论：14 条处置全部真正落进正文，无虚标。** v3 轮 qwen 的 6 条（P1-1/P2-1/P3-1/P3-2/Nit-1/Nit-2）逐条闭合。

---

## Findings

### P2-1：§3 全异步重写——OTA worker 的"单飞"仅靠 `s_ota_busy`，而 busy 被页面 gen-reset；Check 阶段（无硬件锁）可被并发重入

- **位置**：设计 §3（"Check 与 Update 共用同一 worker 任务与 `s_ota_busy`（同一时刻只有一个 OTA 网络操作）"；"`s_ota_hw_lock`（CAS）"在 `Update.begin()` **之前**才取）；交互对象 `docs/async_ipc_contract.md` §2.8（destroy：busy=false；entry：gen++）。
- **证据/反例**：`s_ota_hw_lock` 只覆盖**写槽阶段**（§4 低电互斥用 `lock || busy` 正说明 Check 阶段无锁、仅 busy 兜底）。序列：用户在 Settings 触发 **Check**（worker 跑网络验签，busy=true）→ 按 Back 离屏（contract §2.8 destroy：**busy=false**、gen++；worker 持快照仍在飞）→ 重进 Settings（entry：gen++，busy=false）→ 再触发 Check → **创建第二个 OTA worker**。此时两个 OTA worker 并发跑网络（均处于 Check 阶段、未取 hw_lock）→ 违反 §3 自称的"同一时刻只有一个 OTA 网络操作"，且二者都改共享状态机（IDLE→CHECKING→…）→ 状态错乱/busy 卡死/相位误转。`async_ipc_contract §2.7` 的"≤1 在飞"前提（"busy 期间拒绝新请求"）在 busy 被 destroy 复位后不成立。
- **影响**：后果 **C**（OTA UI 状态机错乱 / busy 卡死，可重启恢复；若两者都进到 Update，hw_lock CAS 让一个失败、不致双写 flash，故不至 A/B）；可达性 **2**（Check 在飞时离屏再重进——弱网下 Check 耗时长，窗口不小）；有声（UI 状态错乱可见）。坐标 **C/2/有声 → P2**。
- **两轴**：`VERIFIED`（SIM：按 contract §2.8 + §3 文本推演即可复现并发；并有 repo 内同族前例）/ `VIOLATES`（`async_ipc_contract.md` §2.7"≤1 在飞"前提；§3 自称的单飞不变量）。
- **precedent**：这正是 PenPal "READ-Close 僵尸 worker 堆积"同族问题——`acc3893` 的修法是 **`s_pp_inflight` 原子计数（`xTaskCreate` 之前递增、入队后递减）+ 超 cap 拒绝**（issue_list §10）。OTA 设计已声明"任务可存活跨页面"为契约例外，却只给写槽阶段配了 hw_lock，**未给 worker 本身配跨 gen 的在飞守卫**。
- **最小修复**：新增 `s_ota_inflight`（atomic，cap 1，`xTaskCreate` 前递增、任务退出递减），**覆盖 Check 与 Update 两阶段**；inflight 未归零时拒绝新 Check/Update（提示"previous OTA op closing"），不依赖会被 gen-reset 的 `s_ota_busy`。补 §8 用例：Check 在飞 → 离屏 → 重进 → 立即 Check → 第二请求被 inflight 拒（与 §8.3 的 hw_lock 并发用例并列，但针对 Check 阶段）。

### P2-2：验签后的 manifest 快照在 CONFIRM_WAIT（无任务）阶段的所有权未定义；`ota_result_t{gen,ok,err}` 不携带它 → 与 P2-1 叠加成"确认的包 ≠ 刷入的包"TOCTOU

- **位置**：设计 §3（状态机 `CHECKING → CONFIRM_WAIT（UI 确认，无任务）→ DOWNLOADING`；"结果通道…只运最终结果 `ota_result_t{gen, ok, err}`"；"快照：Check 通过后整个 `ota_manifest_t` 复制进任务，Update 阶段不复拉清单"）。
- **证据/反例**：CONFIRM_WAIT 阶段**无任务**，而结果结构 `{gen,ok,err}` **不含 manifest**——那么 Check 验签通过的 `ota_manifest_t`（version/url/size/sha256/seq/notes/sig）在"Check worker 退出 → UI 展示 → 用户确认 → Update worker 启动"之间**存放在哪、归谁所有**，设计未定义。两种可能实现都有问题：(a) 存进静态 `s_ota_manifest`（worker 写、UI 读、Update worker 再拷）——是 UI/任务共享的可变缓冲，违反 contract §2.5"任务禁读 UI 拥有的可变缓冲 / 任务持自有副本"；(b) 若 Update 阶段"不复拉清单"却无持久快照，则无从取已验签的 url/sha256。**叠加 P2-1**：若一个陈旧 Check worker 在 UI 处于 CONFIRM_WAIT（正展示 manifest A）时把 `s_ota_manifest` 覆盖为 B，用户确认 A、Update 却刷入 B → **TOCTOU（确认的包 ≠ 刷入的包）**，且 B 可能未经历 UI 展示的电量/版本核对。
- **影响**：后果 **B**（完整性/契约破坏——验签与"用户所确认对象"解绑；TOCTOU 绕过 §4 的二次确认语义）；可达性 **2**（需 P2-1 的并发窗口 + CONFIRM_WAIT 期被覆盖）；静默（刷入的与展示的是同一签名格式、无明显报错）。坐标 **B/2/静默 → P2**（静默表上调后仍 P2）。
- **两轴**：`VERIFIED`（DOC：§3 状态机 + 结果结构定义即可推出快照无主）/ `VIOLATES`（`async_ipc_contract.md` §2.5 任务持自有副本、不共享可变缓冲）。
- **最小修复**：明确快照所有权链——**Check worker 把已验签 `ota_manifest_t` 随结果交回 UI**（扩展 `ota_result_t` 携带 manifest，或 Check 专用结果结构），UI 在 CONFIRM_WAIT **拥有**该快照；用户确认 Update 时，**在启动 Update worker 前把 UI 拥有的快照拷进任务自有结构**（contract §2.5 的 launch-time copy），Update 全程只读任务自有副本。如此 CONFIRM_WAIT 期无 worker 写共享态，P2-1 的陈旧 worker 也无法污染"已确认快照"。设计须把这条所有权链写进 §3 + §6 契约行。

### P3-1：§1"A7682E_init 失败路径 ≈6.1s"低估——实际 ≈8.4s（retry 循环后置自增边界多跑 2 轮）

- **位置**：设计 §1（"现有 setup() 阻塞 profile…`A7682E_init` 失败路径 ≈6.1s"，源自 Grok 静态加总）；代码 `factory.ino:517-535`。
- **证据（逐行推算，modem 无响应板=机 #2）**：`retry_cnt=5; retry=0; while(!modem.testAT(1000)){ if(retry++ > retry_cnt){ delay(100);delay(1000); break; } }`。`retry++ > 5` 为**后置自增比较**：第 1-6 次迭代比较值 0..5 均 `>5` 为假（含第 6 次 `5>5` 假），**第 7 次**比较值 6 `>5` 真才 break → `testAT(1000)` 共调 **7 次 = 7000ms**；break 分支 `delay(100)+delay(1000)=1100ms`；循环前 power-on `delay(10+50+10)=70ms`（:511-516）；循环后 `delay(200)`（:535）。**合计 ≈ 8370ms ≈ 8.4s**，非 6.1s（6.1s = 5×testAT+1100，少算 2 轮 testAT）。
- **影响**：后果 **D/E**（一处"已核实事实"数值低估 ~2s）；**非 load-bearing**——v4 修复用 `T = max(整段实测)×2、下限 30s` + 四处具名喂狗，30s ≫ 8.4s 且喂狗每轮（testAT 间隔 ~1s）即重置，故 8.4 vs 6.1 不影响安全性；校准步（§5.2 打点 millis）会测得真值。但它出现在标注"已核实"的 §1，且**反而更坐实 v3 的 8s 必错**（8.4 > 8）。坐标 **E/3 → P3**。
- **两轴**：`VERIFIED`（CODE：逐行推算）/ `CONFORMS`（不影响设计结论）。
- **最小修复**：§1 把 6.1s 改为 ≈8.4s（或注明"静态加总，以 §5.2 校准打点实测为准"）。顺带：Grok 静态加总把后置自增边界算少 2 轮——校准务必用真机打点，勿用静态估算定 T。

### P3-2：§5.2 校准度量 `T = max(整段 setup+首帧实测)×2` mis-target——有具名喂狗时，约束是"最大不可喂狗单步间隙"而非"整段总时长"

- **位置**：设计 §5.2（"`T = max(整段 setup+首帧实测) × 2`，下限 30s"）。
- **证据/分析**：§5.2 同时在四处**长阻塞函数内部每轮喂狗**（A7682E testAT 每轮、GPS getAck 每轮、EPD nextPage 每轮、PCM 每块）。一旦内部喂狗，TWDT 只关心**相邻两次喂狗之间的最大间隙**，与 setup() 总时长无关——即便总时长 20s，只要每段间隙 <T 就不误触发。故把 T 定成"整段总时长×2"是**过度配置且度量错对象**：(a) 它让 T 远大于真实约束（最大不可喂狗间隙），导致真挂死时要等 T（≥30s）才复位（拖慢 layer-2 回滚）；(b) 真正该校准的是"任何**无法**在内部喂狗的单步阻塞"的最大时长（若四处具名喂狗覆盖全部长阻塞，则最大间隙可能 <5s，框架默认 5s 甚至够用，T 只需略高于最大间隙）。
- **影响**：后果 **D/E**（校准度量与设计意图——既防误触发又尽快捕获真挂死——未对齐；30s 下限使真挂死复位偏慢）；可达性 2。坐标 **D/2 → P3**。
- **两轴**：`VERIFIED`（SIM：TWDT 语义=每任务 reset 间隔，非累计运行时长）/ `CONFORMS`（机制本身可用，度量待精化）。
- **最小修复**：§5.2 把校准目标改为 **`T = max(相邻喂狗点之间的最大不可喂狗间隙) × 2`**（下限可保留 30s 作保守兜底，但说明"有内部喂狗时约束是单步间隙"）；校准打点除记录各段 millis 外，**专门记录每个长阻塞函数内部相邻喂狗的间隔**，取最大者定 T。这样既不误触发（T > 最大间隙）又能尽快捕获真挂死（T 不被总时长虚高）。

### Nit：§5.2 自证门控须"取号一次、轮询比对"，勿在轮询里重复调 `ui_disp_full_refr_seq()`（它有触发整刷的副作用）

- **位置**：设计 §5.2（"`ui_disp_full_refr_seq()` 取号 → 自证点核对 `ui_disp_flush_done_seq()` 达标"）；代码 `factory.ino:863 disp_full_refr_seq(){ disp_full_refr(); return disp_flush_req_seq; }`（**调用即触发一次整刷 + req_seq++**）。
- **证据**：`ui_disp_full_refr_seq()` 不是纯 getter——它内部 `disp_full_refr()` 会 `disp_flush_req_seq++` 并置 FULL 模式（:855-856）。若在自证轮询里反复调它取号，会**反复触发整刷**（每轮 +1 目标、永远追不上）。repo 既有两处惯用法（`ui_deckpro.cpp:2808→2830`、`:4518→4494`）都是**取号一次存目标、再轮询 done≥目标**——v4 §5.2 文字"取号→核对达标"已隐含此意，但未显式禁止"轮询里重复取号"。
- **影响**：E（实现易错点）；坐标 **E/3 → Nit**。
- **最小修复**：§5.2 补一句"`ui_disp_full_refr_seq()` **仅在进入自证等待时调用一次**取得目标 seq（该调用本身触发首帧整刷），随后每轮 loop 只比对 `ui_disp_flush_done_seq() >= target`，勿重复取号"——与 §10 实现合同清单同列。

---

## 三、已通过项（v4 相对 v3 的实质闭合 + 正向核验）

- **v3 轮 qwen P1-1（8s WDT 窗口 vs `A7682E_init` 阻塞 → 机 #2 好固件每次 boot 被回滚）已正面关闭**：v4 §5.2 废除拍脑袋 8s，改"先校准后定值（T=max×2，下限 30s）+ `esp_task_wdt_init(T,true)` re-init + **四处具名内部喂狗**（A7682E/GPS/EPD/PCM）+ 自证后恢复 5s"。其中 **re-init 改超时的 API 假设经框架头文件独立确证成立**（①②：`esp_task_wdt.h:28-30` 明载 re-init 更新 timeout/panic）；30s 下限 ≫ 实测 ~8.4s；具名喂狗覆盖四处已核实的长阻塞函数（⑥⑦⑧）。机制在 API 与代码现实两层都站得住。✅
- **自证点从"setup() 末尾"→"EPD 首帧完成序列号"**（v3 轮 qwen P2"自证太早覆盖不到 UI 回归"的进一步收紧）：§5.2 用 `ui_disp_flush_done_seq() >= ui_disp_full_refr_seq()` 判首帧真上屏，且**复用 repo 内 WiFi 扫描覆盖层 / Sleep 倒计时两处已验证的同款惯用法**（⑤）——非新造机制，实现风险低；首帧若永不完成（EPD 挂死）则 WDT 兜底回滚，正是期望行为（自评③成立）。✅
- **v3 轮 qwen P2-1（seq 下载前写 → 失败不可重试）已闭合**：§2.1 `last_seq` 仅在 `Update.end(true)` 成功后写、Check 只比较、失败/取消/掉电保持原值；§8.5 加"同 seq 立即重试必须成功"用例。✅
- **v3 轮 qwen P3-1（低电互斥无安全阀）已闭合**：§4 抑制有界（一个 10min 绝对 deadline 窗口），超窗强制放行关机（宁可打断写入——otadata 未翻转可恢复，也不深放电）；§8.6 加用例。互斥条件放宽为 `lock || busy`（Grok P3-4）覆盖 Check 阶段。✅
- **v3 轮 qwen P3-2（深度-1 队列同载进度+结果的背压耦合）已闭合**：§3.4/§6 进度改原子变量轮询、队列只运最终结果、写循环禁 `xQueueSend`/`portMAX_DELAY`、绝对 deadline 任务本地 millis 判定——死锁链拆除。✅
- **v3 轮 qwen Nit-1（Ed25519 措辞）/ Nit-2（枚举位置 + 子节引用）已采纳**：§11 措辞精确化（mbedtls 2.28 无 EdDSA、平台经 libsodium 有、ECDSA 避免引入 libsodium）；§4 枚举追加注释、子节引用改"§5 第 N 项"式。✅
- **信任根/签名/传输层（承 v3 已闭合，v4 进一步钉死）**：公钥 65B 未压缩 SEC1 唯一格式（修 v3"65B 压缩点"自相矛盾）+ §8.4 压缩点/64B 拒绝用例；签名 url 与 OTA_URL 双查 http；规范串=JSON 值拼接（非按行切 JSON 文本）+ notes 禁换行；专用 `setCACertBundle` 不继承 `tls_insecure`。✅
- **§7.2 USB 回退命令完整化**（先读 otadata 确认运行槽、`erase_region 0xE000 0x2000`、ota_0 默认语义、坏槽分块重写）+ §7.3 tracked 基线承载表（分区/bootloader SHA、TWDT 开关证据、校准数据）。✅

---

## 四、验证说明（评审方环境）

- 开发机（Windows，repo 工作副本 + `~/.platformio` 框架缓存）独立核验：读框架 `esp_task_wdt.h`（②③ re-init 语义 + add/delete/reset/deinit 返回码——**这是 v4 自评②"最没把握"项，DOC 级确证为 API 正确**）、`sdkconfig:1179-1182`（① TWDT 默认）、`main.cpp:34/47/69`（① loopTask 默认未订阅）；`rg` 核 repo ④⑤⑥⑦⑧⑨（EPD seq API 定义处 + 两处既有惯用法、GPS_Recovery/getAck 调用链、pcm5102a WAV、A7682E retry 循环逐行推算）；mbedtls/libsodium 版本与头文件归属。
- **修正了评审方自己两处初判**（体现 §0.3"独立复现优先"）：(a) 初疑"re-init 不能改超时"——头文件 :28-30 证伪，v4 正确；(b) 初疑"GPS_Recovery 不存在"——系本人 grep 误用 `-r` 标志，重核确认存在于 peri_gps.cpp 且在 setup() 路径。二者均**不构成对 v4 的发现**，反而确证 v4 引用属实。
- **未独立复跑** `pio run`：本对象是设计稿（无实现代码）；§8 全部真机项无设备复现，按 §3.4 标 `UNKNOWN`（设计稿阶段合理，§8 验证计划已完整）。
- 未对工作区作任何代码修改。

## 五、对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：是。v4 是设计稿（无代码 diff），评审方未止于 §12 处置表，把 §1 九处新增引用 + §5.2 WDT 机制 + §3 全异步重写全部拉到框架头文件/repo 源码核验，并**主动追到设计未充分指定的交互面**——§3 共享 worker 的单飞守卫（P2-1）与 CONFIRM_WAIT 期快照所有权（P2-2），二者均非 §12 处置项、非任何前轮发现，是 v4"全异步"重写**新引入**的规格缺口。
2. **快照是否独立复跑**：部分。框架头/sdkconfig/main.cpp、repo `file:line`、A7682E 阻塞时长逐行推算均独立读取/计算（已记命中行）；`pio` 构建与 §8 真机用例无环境，标 UNKNOWN（设计稿阶段合理）。
3. **申请"最没把握"处是否构造反例**：是，逐条击穿/确证 v4 自评②③——
   - 自评②（WDT re-init 语义）：构造反例"re-init 在已 init 的 TWDT 上无效"→ **被头文件 :28-30 证伪**（re-init 确更新 timeout），v4 假设成立，风险关闭（仍建议 §8.1a 真机复核 panic 复位）。
   - 自评①（校准 T 的两机所有启动场景 ≥2× 边际）：构造反例"某不可喂狗单步 > T/2"→ 发现 §5.2 校准度量 mis-target（P3-2：应校准最大不可喂狗间隙，非整段总时长），反例**部分成立**（度量需修正）。
   - 自评③（首帧刷新失败时 WDT 兜底）：构造反例"EPD 首帧永不上屏"→ done_seq 永不达标 → 自证不触发 → WDT(T) 复位回滚，**正是期望行为**，反例不成立（v4 自评③判断正确）。
   - 另对 §3 全异步重写构造"Check 在飞离屏重进"反例 → **击穿**单飞不变量（P2-1）+ 快照 TOCTOU（P2-2）。

## 六、§9 自审清单（设计稿档：【D】文档对齐 + 【ALL】通用）

```text
【D 文档对齐】
□ 字段/键名一致性：清单七字段在 §2.1 JSON / 签名字节串 / §7.1 脚本 / §8.4 用例命名一致；ota_result_t{gen,ok,err} 与 §3.4/§6 一致 — 核过 ✅（但 §3 快照所有权未定义见 P2-2）
□ 代码块可执行：§2.1 JSON 合法可解析；§3 函数签名/§5.2 WDT 调用为 C 伪码、与框架 API 签名一致（esp_task_wdt_init/add/delete/reset 参数核对）；§7.1/§7.2 命令可照抄 — 核过 ✅
□ ✅≠证据：§12 十四条"处置"逐条回正文核实(第二节) — 14/14 真落地 ✅；§1"已核实事实"未轻信，9 条拉到框架/repo 实物核验(8 VERIFIED + 1 数值低估 P3-1) ✅；v4 自评②未当既定事实，独立查头文件确证 ✅
【ALL 通用】
□ 每条 P0/P1/P2 给 file:line + 反例：P2-1(§3 + contract §2.8 + acc3893 前例 + 离屏重进反例)、P2-2(§3 状态机 + ota_result_t 定义 + TOCTOU 反例) ✅（本轮无 P0/P1）
□ 作者"已核实/最没把握"独立核验或如实标注：①-⑨ 独立核验；自评②确证、自评①③构造反例(第五节) ✅
□ 参照系与结论一致：核验对象 = pda2 实际用的框架预编译头/sdkconfig + 现网 repo 源码；P3-1 参照系 = factory.ino:517-535 实际循环边界 ✅
□ "已设计≠与既有代码/API 相容"：本轮核心动作——v4 新 WDT 机制对照 esp_task_wdt.h(相容✅)、新自证门控对照 repo 既有惯用法(相容✅)、新全异步重写对照 async_ipc_contract(发现 P2-1/P2-2 缺口) ✅
□ 不止 happy path：§3 重写按 §6 反例库"离页后旧结果到达/取消/并发"逐项推演 → 命中 P2-1/P2-2 ✅
□ 结论相反时核对方事实(非比票数)：v3 轮 Grok/Codex/Claude 的 P1（WDT）与 qwen P1-1 收敛同一对象，v4 已合围处置；本轮无与其他评审相反的结论(尚无 v4 轮其他评审) ✅
```

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

> **开工前置条件**（review_guide §2.2 G-评审→G-合入；本轮无 P1，P2 触及承重契约须先补设计）：
> - **P2-1**：§3 增 `s_ota_inflight` 原子在飞守卫（cap 1，覆盖 Check+Update 两阶段，`xTaskCreate` 前递增），不依赖会被 gen-reset 的 `s_ota_busy`；照 PenPal `s_pp_inflight`（acc3893）前例；补 §8 Check 阶段并发用例。
> - **P2-2**：§3+§6 明确验签 manifest 的所有权链——Check worker 经结果把快照交回 UI、UI 在 CONFIRM_WAIT 拥有、Update 启动前 launch-time copy 进任务自有副本（contract §2.5）；杜绝 CONFIRM_WAIT 期共享静态被陈旧 worker 覆盖的 TOCTOU。
> - **P3-1**：§1 把 A7682E_init 6.1s 改 ≈8.4s（或注明以校准实测为准）。
> - **P3-2**：§5.2 校准度量改"最大不可喂狗间隙×2"（非整段总时长×2）；打点专记长阻塞函数内部相邻喂狗间隔。
> - **Nit**：§5.2/§10 注明 `ui_disp_full_refr_seq()` 仅取号一次（有触发整刷副作用），轮询只比对 done≥target。
> - 上述关闭后，设计稿可达 **L1 DOC-ALIGNED** 并据以开工（实现后另走 L2/L3 代码评审，锚=实现 commit；§8 真机用例为 G-真机/G-发布 门禁，含 §8.1a 对 WDT re-init panic 复位的真机复核）。
>
> **净评**：v4 是一份**收敛得很好**的设计——v3 轮四方（含 qwen）的 P1/P2/P3/Nit 逐条真落地，且核心新机制（WDT re-init 改超时、EPD 首帧自证门控）经独立核验**与框架 API 和 repo 既有惯用法相容**（评审方两处初判反被证伪，足见 v4 引用之实）。唯一未闭合面是 §3"全异步"重写**新引入**的两处异步生命周期规格缺口（单飞守卫 P2-1、快照所有权 P2-2）——二者都是本项目 async_ipc_contract + PenPal 前例已趟过的坑，补进设计即可开工。无 P1，方向正确。
