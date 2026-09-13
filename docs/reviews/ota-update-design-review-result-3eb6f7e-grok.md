# 设计评审结果：OTA 固件远程升级 v4（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)（文内标明 v4 修订稿）
- **评审提交锚**：`3eb6f7e`（`git log -1 -- docs/ota-update-design.md`）
- **对照**：v3 Grok C
  [ota-update-design-review-result-45d11cf-grok.md](ota-update-design-review-result-45d11cf-grok.md)
  （另：同锚 Codex 已有结果，本文件独立走查，不覆盖、不合并）
- **评审结论**：**C 部分接受，不给 L1 DOC-ALIGNED**。v3 挡开工的 P1-1
  （8s TWDT vs `setup()` 阻塞）以及 seq 时机、公钥格式、Check worker、
  进度死锁均已在正文闭合。v4 **新引入**一条协议级 P1：签名对象写成
  「七字段按序拼接 / 取 JSON 各字段值」，把 `sig` 自己编进待签输入，
  正常清单不可构造、Check 必拒。P0/P1 不可风险接受。
- **算法裁定（沿用）**：ECDSA P-256，不捆绑 Ed25519。

---

## v3 Findings 闭合核对

| v3 条目 | v4 处置 | 状态 |
|---|---|---|
| Grok P1-1 8s TWDT 误杀健康固件 | §5.2 废除 8s；先校准 T=实测×2 下限 30s；`esp_task_wdt_init(T,true)`；四处函数**内部**喂狗；自证后恢复 5s；§8.2 健康固件正向 | **闭合**（残余：句柄/喂狗生命周期见本轮 P2-1） |
| Grok P2-1 `last_seq` 下载前写 | §2.1 仅 `Update.end(true)` 后写；§8.5 同 seq 重试 | **闭合** |
| Grok P2-2 65B「压缩点」 | §2.2 钉死 65B 未压缩 SEC1；§8.4 压缩/64B 拒绝 | **闭合** |
| Grok P2-3 Check 在 UI 线程 | §3 `ota_check_async` / UI 零 HTTPS | **闭合** |
| Grok P2-4 进度深度 1 + `portMAX_DELAY` | 原子百分比 + 队列只运结果；写循环禁 `xQueueSend` | **闭合** |
| Grok P3-1 明文只查 `OTA_URL` | §2.1 清单 `url` 双查 | **闭合** |
| Grok P3-2 notes 换行 | §2.1 禁 `\n`/`\r` | **闭合** |
| Grok P3-3 USB 命令不可照抄 | §7.2 `erase_region 0xE000 0x2000` + 先读 + ota_0 默认语义 | **闭合** |
| Grok P3-4 互斥只用 lock | §4 `lock \|\| busy` | **闭合** |
| Grok P3-5 承重数字不在 v3 | §7.3 基线表 | **闭合** |
| Codex v3 P1 首帧 ≠ `lv_async_call` | §5.2 改 EPD 完成序列号 | **方向对**（比较式写法见本轮 P2-2） |
| Qwen v3 P3 低电互斥无上界 | §4 10min 安全阀 | **闭合** |

---

## Findings

### P1：签名对象把 `sig` 编进待签字节，协议自指、正常清单不可验

- **位置**：§2.1 L96–100。示例 JSON 七项为
  `version, url, size, sha256, seq, notes, sig`。正文连续写：
  「签名对象 = **七字段**按序拼接」「构建方式：取 **JSON 各字段值**
  直接拼接，每字段后一个 `\n`」「`sig` = ECDSA …」。§8.4 仍写
  「篡改七字段任一」。
- **证据（SPEC，可直接推出）**：
  1. 待签输入若含最终 `sig`，发布端必须在签名前就知道签名结果 →
     自指，无解。
  2. 若对空/`""` 占位 `sig` 签名，设备读到的是填好的 base64，拼接
     结果与发布端不同 → **所有合法清单验签失败**。
  3. 这是相对 v3 的**回退**：v3 把规范化串显式列为
     `version \n url \n size \n sha256 \n seq \n notes \n`（六项），
     「七字段全必填」只约束 JSON 有 `sig` 这一列。v4 删掉六项清单，
     把「七」接到「签名对象」上。
- **反例**：按 §2.1 字面实现 `ota_sign.py` 与设备验签 → 第一次 Check
  即拒绝；OTA 功能面为零。无需攻击者、无需弱网。
- **影响**：后果 **B**（签名协议不可满足 / 完整性状态机违约）；可达性
  **1**（正常 Check）。有声（验签失败提示）。坐标 **B/1/有声 → P1**。
- **两轴**：`VERIFIED`（DOC）/ `VIOLATES`（签名/验签合同）。
- **最小修复**（一小段，不必重开长篇 v5 架构）：
  1. 签名对象**严格六字段**、**不含 `sig`**：
     `version \n url \n size(十进制无前导零) \n sha256(64 位小写 hex) \n seq(十进制) \n notes \n`。
  2. JSON 仍七键全必填；`sig` 是对上述字节的分离签名。
  3. 发布脚本与设备用同一拼接函数；§8 加一组固定测试向量（含
     notes 空串、url 长短）。
  4. 「篡改七字段」测试计划可保留（改 `sig` 也应拒），但规范正文
     不得再说「签名对象 = 七字段」。

### P2-1：`esp_task_wdt_add(loopTask)` 不是可编译的任务句柄；喂狗点未与订阅期对齐

- **位置**：§5.2「`esp_task_wdt_add(loopTask)`」「具名喂狗点…
  EPD `nextPage`/BUSY 等待循环每轮」「自证后 `esp_task_wdt_delete(loopTask)`」。
- **证据（CODE / SPEC）**：
  1. Arduino-ESP32 2.0.14 `loopTask` 是 `void loopTask(void *)` 入口
     函数；句柄是 `loopTaskHandle`。`esp_task_wdt_add` 要 `TaskHandle_t`。
     按字面写 `add(loopTask)` **不能当合同照抄**（类型都不对）。
     `setup()` 里正确写法是 `esp_task_wdt_add(NULL)`（当前任务即
     loopTask）或 `add(loopTaskHandle)`。
  2. 具名喂狗插在 **所有** EPD `nextPage` 循环。自证后已
     `delete` + 恢复 5s；之后每次正常全刷仍 `esp_task_wdt_reset()`。
     设计要求「检查全部返回值」→ 将稳定得到 `ESP_ERR_NOT_FOUND`
     （未订阅任务 reset）。与「返回值检查」合同冲突。
- **影响**：后果 **B**（WDT 合同不能稳定验证 / 照抄不能编）；可达性
  **2**（须按该 API 字面落地）。有声（编译失败或串口 error）。坐标
  **B/2/有声 → P2**。
- **两轴**：`VERIFIED`（Arduino 2.0.14 任务模型 + IDF TWDT API）/
  `NO_CONTRACT`（订阅期与 reset 期未写成同一谓词）。
- **最小修复**：合同写成：
  - 窗口开：`esp_task_wdt_init(T, true)` + `esp_task_wdt_add(NULL)`，
    检查返回值；失败则记日志（此窗口不能提供层 2，属实现期阻断）。
  - 喂狗：仅当 `s_ota_boot_wdt`（或等价）为真；自证后清标志，EPD
    路径不再 reset。
  - 窗口关：`esp_task_wdt_delete(NULL)` + `esp_task_wdt_init(5, true)`。
  - 禁止在设计正文把函数名 `loopTask` 当作句柄。

### P2-2：§5.2 把有副作用的 `ui_disp_full_refr_seq()` 写进比较式

- **位置**：§0.2（取号 → 再核对，正确）；§5.2
  「`ui_disp_flush_done_seq() >= ui_disp_full_refr_seq()`」。
- **证据（CODE）**：`disp_full_refr_seq()`（`factory.ino:864-867`）
  **每次调用** `disp_flush_req_seq++` 并返回新号。现有正确用法是
  WiFi 扫描覆盖层（`ui_deckpro.cpp:2808` 取一次，`:2830` 用保存值
  比较 `ui_disp_flush_done_seq()`）。若按 §5.2 表达式在 loop 里每拍
  求值：右侧每拍 bump 一号，done 永远追 `req-1`，**永远达不到自证**
  → T 秒 WDT → 健康固件回滚（与 v3 P1-1 同一结局，路径换成公式）。
  `ui_disp_full_refr_seq` / `ui_disp_flush_done_seq` 符号与行号
  （`ui_deckpro_port.h:42-43`，`factory.ino:241/:864`）声明属实。
- **影响**：后果 **B**（自证谓词不可照抄）；可达性 **2**（须照抄
  §5.2 表达式；若实现者先读 §0.2 / WiFi 先例则可避开）。有声
  （WDT 回滚）。坐标 **B/2/有声 → P2**。回升 P1：实现按比较式每拍
  调用 `ui_disp_full_refr_seq()`。
- **两轴**：`VERIFIED`（CODE + SPEC 内部 §0 vs §5 不一致）/
  `CONFORMS`（意图与 WiFi 门控一致，写法未钉死）。
- **最小修复**：§5.2 改成与 WiFi 覆盖层同构的两行合同：

  ```
  boot_seq = ui_disp_full_refr_seq();   /* 只调一次，建议替换 setup() 末尾的 disp_full_refr() */
  /* loop 里：if (ui_disp_flush_done_seq() >= boot_seq) → mark_valid + 关 WDT */
  ```

  禁止在谓词右侧调用 `ui_disp_full_refr_seq()`。§8.2 记：done 追上
  **同一次**取号，而不是后续又请求的全刷。

### P3（Nit / 实现期，不单独挡 L1；P1 闭合后一并写进合同）

1. **§8.3 硬件锁并发用例与 §4 吞键矛盾**：下载覆盖层「吞全部按键」，
   用户无法 Back 离屏再进。该用例应按探针调用第二次 `ota_start_async`，
   或改在「结果已出、reboot 确认前」测「禁止再开写」。
2. **§2.3 声称 `env.cfg.example` 已删「Max 8 / 95 chars」**：工作树
   `examples/pda2/env.cfg.example:10` **仍在**。设计稿用「已删」违反
   ✅≠证据；改为「commit 组 1 删除」或同批改文件。
3. **校准样本 vs 慢路径**：§5.2 具名喂狗未列 `sd_care_init()`（
   `factory.ino:461` `SD.begin`）。申请头部已把慢 SD / GPS 冷启动列为
   最没把握。T=30s 对已喂狗的 1s 级间隙足够；**未喂狗且 `SD.begin`
   挂死/极慢**仍会误回滚。实现合同把 SD（及任何 `delay`/`begin`>1s
   的 peri）列入喂狗或移出窗口；`ota-baseline.md` 用矩阵（两机 ×
   有/无卡 × 4G 成败）而不是单次打点。坐标 **B/3 → P3**，慢卡成为
   常态则回升 P2。
4. **`last_seq` 在旧固件 `end(true)` 时提交**：健康固件若被误回滚，
   seq 已烧掉，须 seq+1 才能重试。v4 把它写成「回滚后拒重装同一坏包」
   的期望；对**误回滚的好包**过严。更稳是新固件 `mark_valid` 后再写
   （Qwen v3 的 pending_seq 跨重启）。属决策项，不升 P1。
5. **§5.2 残留「预期 8s→T 秒」**：8s 已废除，删掉以免实现者再抄 8。

---

## 已通过项

- 自证窗口：先校准、re-init、函数内部喂狗、下限 30s、自证后恢复 5s；
  健康固件正向用例。相对 v3 P1-1 **机制闭合**。
- `CONFIG_ESP_TASK_WDT_PANIC=y` / `TIMEOUT_S=5` 写入基线；复位表述为
  「预期、§8.1a 待验证」。
- EPD 完成序列号作为门控**意图**正确（API 存在；须按 P2-2 取号一次）。
- `last_seq` 仅成功 `end` 后写；同 seq 失败可重试。
- 公钥 65B 未压缩唯一格式。
- Check/Update 共用 worker；UI 零网络。
- 进度原子轮询；写循环无队列发送；10min 任务本地 deadline。
- 低电 `lock || busy` + 10min 安全阀。
- 明文 URL 双查；notes 禁换行；USB `erase_region` 可照抄；ota_0 语义
  写明。
- `esp_task_wdt_init` 在已初始化的 TWDT 上更新 timeout/panic（IDF 4.4
  语义，与 Arduino 2.0.14 一致）——申请「最没把握 ②」按文档可接受，
  真机仍 `CLAIM_ONLY`。

---

## 验证说明

- 读 v4 全文（锚 `3eb6f7e`）及 v3 己方结果；独立核 §2.1 签名段落与
  JSON 示例，不依赖他方转述。
- CODE：`factory.ino` `disp_full_refr_seq`/`disp_flush_done_seq`、
  `A7682E_init`/`sd_care_init`、`ui_deckpro.cpp` WiFi 覆盖层取号模式、
  `env.cfg.example:10`、`ui_deckpro_port.h:42-43`。
- 无 pio、无真机。TWDT re-init 语义沿用 IDF 4.4 文档（`PARTIALLY_VERIFIED`）。
- BUILD/HW = `UNKNOWN`。

---

## 对称三格

1. **全区间**：`45d11cf..3eb6f7e` 技术变更集中在本设计稿（v4 正文）。
   无 OTA 实现代码藏在区间里。顺带核了 `env.cfg.example`（声称已改、
   文件未改）。
2. **独立复跑**：静态 CODE/SPEC/SIM。未 `pio run`。未把 Codex 同锚
   结论当证据；签名自指与 `disp_full_refr_seq` 副作用均从正文 + 源码
   推出。
3. **最没把握处反例**：
   - ① 慢 SD：`sd_care_init` 不在具名喂狗列表（P3-3）。
   - ② `esp_task_wdt_init` re-init：按 IDF 4.4 可更新超时，不升缺陷；
     句柄/`NULL` 才是合同洞（P2-1）。
   - ③ 首帧 BUSY 永等：T 秒 WDT 回滚是期望层 2，不升缺陷；危险的是
     比较式每拍重新取号导致**健康**首帧也永远不达标（P2-2）。

---

## 声明矩阵

| # | 声明 | 状态 |
|---|---|---|
| 签名对象 = 七字段 JSON 值拼接 | **CONTRADICTED**（P1） |
| 先校准 T≥30s + 内喂狗 → 健康 boot 不被 8s 误杀 | `PARTIALLY_VERIFIED`（机制闭合；T 与 SD 路径 CLAIM_ONLY） |
| `ui_disp_*_seq` API 存在 | `VERIFIED` |
| `env.cfg.example` 已删 8/95 注释 | **CONTRADICTED**（P3-2） |
| TWDT PANIC=y 超时即复位 | 框架 sdkconfig `VERIFIED`；真机复位仍 `CLAIM_ONLY`（§8.1a） |

---

## §9 自审清单【D】/【ALL】

- [x] 15 P1/P2 有位置 + 反例
- [x] 16 校准/回滚/re-init 真机项标 CLAIM_ONLY；无 pio
- [x] 17 参照系 = `3eb6f7e` 设计稿 + pda2 源码
- [x] 21 键名：`OTA_URL` / `last_seq` / 清单字段——**签名对象字段集未过**（P1）
- [x] 22 JSON 可解析；签名拼接**不可**按正文执行（P1）；
      `esp_task_wdt_add(loopTask)` **不可**照抄（P2-1）；
      §5.2 比较式 **不可**照抄（P2-2）；§7.2 USB 可照抄
- [x] 23 「已删 example 注释」无对应 diff

【C】1–9、12–14、18：无实现代码，`NOT_APPLICABLE`。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒 v4 架构）
- [x] C. **部分接受**——v3 P1 的窗口重设计可保留；**P1（签名对象不含
      `sig`）必须改正文后再编码**。P2-1/P2-2 各补一段合同即可，建议
      与 P1 同一补丁写进设计，避免实现期再踩。

**不给 L1**：L1 要求伪代码/JSON 规范自洽。签名输入含 `sig` 使验签
关系不可满足（§9 条 21/22 未过）。

**给 C 而非 B**：其余承重件（校准 WDT、EPD 门控意图、seq 时机、
65B 公钥、worker、原子进度、USB、明文双查）已对齐源码与 v3 最小修复。
只需恢复 v3 的六字段规范化串（并钉死十进制/hex 大小写），外加 TWDT
`NULL` 句柄与「取号一次」。
