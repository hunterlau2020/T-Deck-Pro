# 设计评审结果：OTA 固件远程升级 v5（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)（文内标明 v5 修订稿）
- **评审提交锚**：`e0ec3bb`（`git rev-parse HEAD`；`git log -1 -- docs/ota-update-design.md`）
- **对照**：v4 Grok C
  [ota-update-design-review-result-3eb6f7e-grok.md](ota-update-design-review-result-3eb6f7e-grok.md)
- **评审结论**：**C 部分接受；不给 L1 DOC-ALIGNED**。v4 四方共同 P1（签名自指）
  与 4 条 P2（TWDT 句柄、取号副作用、单飞守卫、快照所有权）已在正文闭合，
  伪代码可照抄。本轮新 P2：跨页存活 + 深度 1 结果队列 **未指定离屏排空点**，
  按字面实现 Check 离屏后 worker 会堵在 `xQueueSend`，`s_ota_inflight` 永不归零。
  无 P0/P1。补一段 factory 循环排空合同即可开工，不必重开架构。
- **算法裁定（沿用）**：ECDSA P-256。
- **决策项（自评③）**：**接受**。自证语义 = 用户可见可用；屏坏固件被 T 秒
  WDT 回滚是层 2 的正确结果，不是缺陷。

---

## v4 Findings 闭合核对

| v4 条目 | v5 处置 | 状态 |
|---|---|---|
| 四方 P1 签名对象含 `sig` | §2.1 显式六字段、`sig` 排除；§8.6 占位 sig 正向用例 | **闭合**（编码细节见本轮 P3-1） |
| Codex/Grok P2 TWDT `add(loopTask)` + 自证后仍 reset | §5.2 `add(NULL)`/`delete(NULL)`；`s_boot_wdt_subscribed` 守卫 | **闭合** |
| Grok P2-2 `ui_disp_full_refr_seq()` 写入比较式 | §5.2 取号一次存变量，轮询只读 done | **闭合** |
| Qwen P2-1 单飞只靠 busy | §3.1 `s_ota_inflight` cap 1 覆盖 Check+Update；§8.4 | **闭合**（离屏归零见本轮 P2） |
| Qwen P2-2 CONFIRM_WAIT 快照无主 | §3.2 结果携带 manifest → UI 持有 → launch-time copy | **闭合** |
| Qwen P3-1 A7682E 6.1s | §1 改为 ≈8.4s（7 轮 testAT，独立复算成立） | **闭合** |
| Codex P3 / Qwen P3-2 T 度量 | §5.3 改最长喂狗间隔×2、下限 30s | **闭合方向**（慢 SD 声称过满，见 P3-2） |
| Grok P3-2 `env.cfg.example`「已删」 | v5 不再声称已删；commit 组 1 改模板 | **闭合该虚标**（文件仍旧，属实现待办） |
| Grok P3-5 「预期 8s」残留 | 正文无 8s 超时合同 | **闭合** |
| Grok P3-1 §8.3 vs 吞键 | 未改 | 本轮 P3-3 |
| Grok P3-3 `sd_care_init` 无喂狗 | v5 写成「慢 SD 已封顶」但列表仍四处 | 本轮 P3-2 |
| Grok P3-4 `last_seq` vs 误回滚好包 | 仍 end 后写；属决策，不升 | 维持 |

---

## Findings

### P2：跨页存活的 worker 结果通道没有离屏排空点 → inflight 可永久占用

- **位置**：§3.1 / §6（`s_ota_inflight` cap 1；队列深度 1 只运结果；
  **任务可跨页面存活**）；§8.4（Check 在飞 → **离屏** → 重进 → 立即 Check
  被拒）。对照契约 `docs/async_ipc_contract.md` §2.7（`xQueueSend(...,
  portMAX_DELAY)`）与反例库「屏已被覆盖 → 队列须在主循环排空」。
- **证据（SPEC + CODE 先例）**：
  1. v5 把「跨页存活」写成契约例外，但消费者只写 Settings(SCREEN2_2)。
     未要求 `factory.ino` 的 `loop()` **无条件**排空 OTA 结果队列。
  2. 仓库既有跨页任务（PenPal）的排空点是
     `penpal_keyboard_poll()`：`factory.ino:828-829` 每拍调用，
     `ui_penpal.cpp:1330` **先 drain 再** `if (!s_pp_active) return`。
     没有这条，`xQueueSend(..., portMAX_DELAY)` 在深度 1 队列上会一直堵。
  3. PenPal 在 send **返回之后**才 `__atomic_sub_fetch(&s_pp_inflight)`
     （`:450`）。send 阻塞则 inflight 不减。
- **反例（§8.4 自己的操作序列）**：Check 在飞 → Back 离屏（destroy：
  busy=false / gen++，worker 仍在）→ **不重进** Settings。worker 结束
  `xQueueSend` 无人收 → 永久阻塞 → inflight 保持 1 → 此后任何 Check/Update
  均报 "previous OTA op closing"，直到复位。§8.4 若「立即重进」可能碰巧
  在页面 timer 里排空，把死锁藏起来；**不重进**才是跨页存活真正要覆盖的
  分支。
- **影响**：后果 **C**（OTA 入口永久拒、可复位恢复；hw_lock 未取、不双写
  flash）；可达性 **2**（Check 在飞离屏，设计自己要求可离屏）；有声。
  坐标 **C/2/有声 → P2**。回升 P1：实现用 `portMAX_DELAY` 且只在 SCREEN2_2
  存活期 drain。
- **两轴**：`VERIFIED`（DOC §6 例外 + PenPal 排空点 CODE）/
  `VIOLATES`（`async_ipc_contract.md` §2.7 隐含「必有接收方」；§6 跨页例外
  未配接收方）。
- **最小修复**（一段合同，写入 §6 + §10）：
  1. `ota_poll()` 挂进 `factory.ino` `loop()`（与 `penpal_keyboard_poll`
     同构）：**无论 SCREEN2_2 是否在栈上都 drain**。
  2. stale gen：丢结果、释放堆、不改 UI；inflight 仍由 worker 在 send
     返回后减（`xTaskCreate` 失败必须回滚计数，同 `ui_penpal.cpp:527-528`）。
  3. 禁止只在屏幕 `entry`/`timer` 里收结果。写循环继续禁 `xQueueSend`。

### P3-1：签名字节串未钉死编码，发布端与设备可能拼出不同输入

- **位置**：§2.1「`size`（十进制）、`sha256`、`seq`（十进制）…各值后接
  `\n`」。v4 本轮最小修复曾要求「无前导零 / 64 位小写 hex / UTF-8」；
  v5 恢复了六字段枚举，**未带回编码钉**。
- **证据**：`size: 2100240` 若一边 `str(int)`、一边 JSON 序列化浮点或
  带前导零；`sha256` 一边 `hexdigest()` 小写、一边固件 `printf %02X`。
  签名对象按「JSON 值」拼接，两边各合法实现即可验签失败。§8.6 占位 sig
  用例钉的是自指，不钉大小写。
- **影响**：后果 **D**；可达性 **2**（实现分歧）。**D/2 → P3**。回升 P2：
  发布脚本与设备用了不同 hex 大小写且无互测向量。
- **两轴**：`PARTIALLY_VERIFIED`（六字段排除 `sig` 已自洽）/ `NO_CONTRACT`
  （字节级编码）。
- **最小修复**：§2.1 加一句：UTF-8；整数十进制、无符号、无前导零、无
  科学计数；`sha256` 64 位 `[0-9a-f]`；空 `notes` 仍输出最后一行 `\n`。
  `ota_sign.py` 与设备共用同一拼接说明；§8.6 加一条大小写 hex 拒绝。

### P3-2：§5 把尚未落地的改动写成「已核实 / 已封顶」

- **位置**：§5.1「`drv.begin()` 的 `while(1)` **已拆**」；§5.3「慢路径
  （GPS 冷启动、**慢 SD**）落入喂狗点之间的间隔被逐段封顶」。
- **证据（CODE，✅≠证据）**：
  1. `factory.ino:708-710` 仍是 `if (!drv.begin()) { … while (1) delay(10); }`。
     §8.1e「失败 → 记日志继续」是**计划**，不是现状。pending 窗口内
     `delay` 不喂已订阅的 loopTask，T 秒 WDT 仍会回滚——层 2 碰巧能兜住，
     但实现者会以为组 4 不用改这里，§8.1e 真机必失败。
  2. §5.2 具名喂狗仍只列 A7682E / GPS `getAck` / EPD `nextPage` / PCM 写
     WAV。`sd_care_init()`（`:461` `SD.begin`，`:748` 在 setup 内）与
     `SPIFFS.begin(true)`（`:731`，损坏时 format）**不在列表**。GPS 冷启动
     确被 getAck 每轮封顶；**慢 SD 的「已封顶」被源码证伪**。
- **影响**：后果 **E**（基线事实错误）兼 **B/3**（未喂狗的 `SD.begin`
  挂死/极慢 → 健康镜像误回滚）。坐标 **E/1 + B/3 → P3**。回升 P2：校准
  样本只有快卡，慢卡/format 成常态。
- **两轴**：`CONTRADICTED`（已拆 / 慢 SD 封顶）/ `NO_CONTRACT`（这两步
  是计划还是现状未分开）。
- **最小修复**：§5.1 改为「组 4 拆除 `drv.begin` 的 `while(1)`，改记日志
  继续」；§5.2 喂狗列表加上 `SD.begin` 循环（若库无可插点则列为**未喂狗
  单步**，校准必须测）和 `SPIFFS.begin(true)` format；删掉「慢 SD 已封顶」
  的全称句。

### P3-3：§8.3 硬件锁并发用例与下载期吞键仍矛盾

- **位置**：§8.3「下载中 Back 离屏 → 重进 → 立即 Update」；§4 下载覆盖层
  「吞全部按键」。v4 Grok P3-1，v5 §12 未列。
- **影响**：用例按键路径不可达。**E/2 → P3**。
- **最小修复**：改为探针第二次 `ota_start_async`（不依赖 Back），或改测
  「结果已出、reboot 确认前禁止再开写」。离屏排空用本轮 P2 的 §8.4 覆盖。

---

## 已通过项（本轮独立复核，非转述）

- **签名对象六字段、不含 `sig`**：JSON 七键；正文连续写「六个进字节串 /
  `sig` 是结果本身」；§8.6 占位 sig 正向。v4 P1 击不穿。
- **TWDT 合同**：`esp_task_wdt_add(NULL)` / `delete(NULL)`；喂狗受
  `s_boot_wdt_subscribed` 守卫；关窗 `init(5, true)`。v4 P2-1 击不穿。
  `add` 失败后伪代码仍置 `subscribed=true` 与散文「降级」略不一致 → 实现
  时写成 `subscribed = (e == ESP_OK)`，不单列。
- **取号一次**：与 `ui_deckpro.cpp:2808→2830`、`:4518→4494` 同构。
  `disp_full_refr_seq()`（`factory.ino:864-867`）有副作用属实。建议组 4
  **替换** `:764` 的 `disp_full_refr()`，不要额外再调一次（当前
  `done_seq = 完成时刻的 req_seq`，多调一次仍可能达标，但会双整刷）。
- **`s_ota_inflight` + 快照所有权链**：UI 单线程下 confirm 的 copy-then-launch
  关闭 TOCTOU；无静态共享 `s_ota_manifest`。Qwen v4 P2-1/P2-2 的原反例
  不再成立；残余是本轮 P2 的「离屏无人收」。
- **A7682E 失败路径 ≈8.4s**：`factory.ino:518-535` 后置自增，`testAT(1000)`
  七次 + break 1100ms + 上电 70ms + 后 200ms ≈ 8370ms。VERIFIED。
- **EPD 门控 API**：`ui_deckpro_port.h:42-43` 包装 `factory.ino:864/:872`；
  `:241` 仅 FULL 路径写 done。
- **明文双查、notes 禁换行、65B 未压缩 SEC1、`last_seq` 仅 `end(true)` 后写、
  进度原子、低电 `lock\|\|busy` + 10min 阀、USB `erase_region`、SCREEN2_2
  追加枚举末尾**（当前末项 `SCREEN_PENPAL_ID`，`ui_deckpro.h:80`）——承前闭合。
- **`ENV_MAX_ENTRIES=12` / `val[160]`**（`env_secrets.cpp:21,25`）加
  `OTA_URL` 容量够；example `:10` 仍写 Max 8/95，组 1 改，不升缺陷。

---

## 验证说明

- 锚 `e0ec3bb`。`git diff 3eb6f7e..e0ec3bb --stat`：设计稿 + CHANGELOG +
  四份 v4 结果（归档，不评）。
- CODE：`factory.ino` setup/`drv.begin`/`A7682E_init`/`sd_care_init`/
  `disp_full_refr*`/`ink_screen_init`；`ui_penpal.cpp` inflight+全局 drain；
  `ui_deckpro.cpp` WiFi/Sleep 取号；`env_secrets.cpp`；`ui_deckpro.h` 枚举。
- 无 `pio run`，无真机。TWDT re-init 沿用 IDF 4.4 头文件（前轮 Qwen
  VERIFIED）。BUILD/HW = `UNKNOWN`。

---

## 对称三格

1. **全区间**：`3eb6f7e..e0ec3bb`。受评 = `docs/ota-update-design.md` +
   `CHANGELOG.md`（v5 摘要与正文一致）。四份 `…-3eb6f7e-*.md` = 前轮归档，
   不评。无 OTA 实现代码藏在区间里。
2. **独立复跑**：未采信 §0/§12 处置表。逐字核 §2.1 签名段、§5.2 伪代码、
   §3.1/§6 跨页例外；A7682E 8.4s 与 `drv.begin`/`SD.begin` 回源码。
3. **最没把握处反例**：
   - ① 喂狗完备性：慢 SD / SPIFFS format 不在四处列表（P3-2）；未来新 peri
     靠 §10 合同，接受为残余风险。
   - ② `esp_task_wdt_init` re-init：不升缺陷（前轮头文件已证）。
   - ③ 屏坏首帧永不完成：T 秒回滚 = 期望层 2，**接受决策**。
   - 另对 §6「跨页存活」构造「Check 离屏且不重进」→ 击穿 inflight 归零（P2）。

---

## 声明矩阵

| # | 声明 | 状态 |
|---|---|---|
| 签名对象 = 六字段，不含 `sig` | `VERIFIED`（SPEC） |
| 字节级编码唯一 | `NO_CONTRACT`（P3-1） |
| `add(NULL)` + 守卫喂狗可照抄 | `VERIFIED`（SPEC） |
| `ui_disp_full_refr_seq` 只取一次 | `VERIFIED`（SPEC；与 CODE 惯用法一致） |
| `s_ota_inflight` 跨 gen 单飞 | `PARTIALLY_VERIFIED`（在页内闭合；离屏见 P2） |
| 确认的包 = 刷入的包 | `VERIFIED`（SPEC 所有权链） |
| `drv.begin` while(1) 已拆 | **CONTRADICTED**（P3-2） |
| 慢 SD 喂狗间隔已封顶 | **CONTRADICTED**（P3-2） |
| A7682E 失败路径 ≈8.4s | `VERIFIED` |
| 屏坏 → 回滚 | `UNDECIDED`→本轮**接受**（决策） |

---

## §9 自审清单【D】/【ALL】

- [x] 15 P2 有位置 + 反例（§8.4 不重进）
- [x] 16 校准/回滚真机项 `CLAIM_ONLY`；无 pio → BUILD `UNKNOWN`
- [x] 17 参照系 = `e0ec3bb` 工作树（干净）
- [x] 21 键名：六字段 + `OTA_URL` + `last_seq` 一致；编码未钉（P3-1）
- [x] 22 JSON 可解析；§5.2 TWDT 伪代码可照抄；§7.2 USB 可照抄；
      跨页结果发送**不可**从正文推出必归零（P2）
- [x] 23 「已拆 / 慢 SD 封顶」无对应 diff（P3-2）

【C】1–9、12–14、18：无实现代码，`NOT_APPLICABLE`。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒 v5 架构）
- [x] C. **部分接受**——方向与 v4 P1/P2 修复成立，**开工前置**只剩 P2：
      §6/§10 写明 `ota_poll()` 进 `loop()` 无条件 drain（PenPal 同构）+
      create 失败回滚 inflight。P3 同批各补一两句，不必再开 v6 架构轮。

**不给 L1**：跨页存活例外下，离屏后 inflight 是否归零不能从对照表唯一推出
（§9 条 22）。

**给 C 而非 A**：A 不得挂必修项；P2 触及契约 §2.7 / 反例库覆盖屏。

**给 C 而非 B**：签名、WDT、取号、单飞、快照链均已可实施；本轮是漏写排空点，
不是方向错误。
