# 设计评审结果：OTA 固件远程升级 v6（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)（文内标明 v6 修订稿）
- **评审提交锚**：`97a4f53`（`git log -1 -- docs/ota-update-design.md`）
- **工作树 HEAD**：`8467051`（指南 v1.3 已落地；`git diff 97a4f53 HEAD -- docs/ota-update-design.md` 空）
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**（§2.5 收口强制）
- **复审范围（§2.5.3 delta）**：`git diff e0ec3bb..97a4f53` 规范性段落 + 作者 §0 六条变化点。未改章节当作已闭合，不重开架构。
- **扩面**：无（未发现回归改坏已闭合声明；未发现新段与未改段矛盾；未在未改段拿出独立协议/架构 P0/P1）
- **对照**：v5 Grok C
  [ota-update-design-review-result-e0ec3bb-grok.md](ota-update-design-review-result-e0ec3bb-grok.md)
- **评审结论**：**L1 DOC-ALIGNED + A**。无未闭合的协议/架构 P0/P1。**G-开工通过**。残留项进下方实现合同表，门禁 = 实现 commit 的 L2 / G-合入，**不开 v7**。
- **算法裁定（沿用）**：ECDSA P-256。
- **决策项（自证语义）**：**接受 v6 修正**。自证 = 首个 FULL flush **软件序列完成**；屏坏固件会自证、不回滚（层 3 USB）。撤销对本决策的 v5 接受词（彼时依据的是已被代码证伪的「屏坏回滚」）。

---

## 1. 为何本轮给 A（相对 v5 的 C）

v5 本侧拒绝 L1 的唯一挡开工项是：跨页存活 + 深度 1 队列未写离屏排空点，按字面 `xQueueSend(portMAX_DELAY)` 可把 `s_ota_inflight` 永久占住。v6 §3.3 已写：

- `factory.ino` `loop()` **无条件** `ota_result_poll()`（先排空、后屏活跃）；
- 结果发送改 `xQueueOverwrite`（发送路径永不阻塞）；
- inflight 与队列解耦，递减在 worker 唯一出口。

按 v1.3 §2.5.1，其余未钉死的指针剥离 / overwrite 旧槽 delete / inflight `++` 时序，属于「与 PenPal 同构的类型/调用约定」，**不挡 L1、禁止因此开新设计版**。v5 用代码口径「A 不得挂必修项」给 C，本轮按 §2.5.2 / §9 条 24 改判。

---

## 2. v5 Findings 闭合核对（独立回正文，不采信 §12）

| v5 条目 | v6 正文 | 状态 |
|---|---|---|
| Codex P1 队列值传递所有权不成立 | §3.1 队列只运指针；禁含 `std::string` 结构按值入队；PenPal `pp_result_t` 同款 | **闭合**（布局细节 → IC-2/IC-3） |
| Codex P2 create 失败 / 满队列 / 离屏 | §3.3 失败不递增；overwrite 不阻塞；每拍 poll | **闭合方向**（`++` 时序 → IC-1） |
| Codex P3 add 失败仍置守卫 | §5.2 `if (e == ESP_OK) s_boot_wdt_subscribed = true` | **闭合** |
| Grok P2 离屏排空点 | §3.3 无条件 poll + drain-then-active | **闭合** |
| Grok P3-1 字节串编码 | §2.1 十进制无前导零 / 小写 hex / UTF-8 + §8.5 互测 | **闭合** |
| Grok P3-2 SD 虚标「已封顶」；`drv.begin`「已拆」 | 喂狗列表补 SD；§5.1 删掉「已拆」假现状 | **闭合该虚标**（拆除 `while(1)` 仍是组 4，IC-5） |
| Grok P3-3 §8.3 vs 吞键 | §4 全屏吸收 + §8.3 重写 | **闭合** |
| Qwen P2 屏坏回滚假声明 | §5.2/§11 + 自评③ 改为软件序列；屏坏不回滚 | **闭合**（CODE 复验成立） |
| Qwen P3-1 EPD 库内等待 | §5.3 T 纳入 `_busy_timeout` | **闭合** |
| Qwen Nit inflight 全退出路径 | §3.3 唯一出口 + 10min deadline | **闭合** |

签名六字段不含 `sig`、TWDT `add(NULL)`、取号一次、`last_seq` 仅 `end(true)` 后写——未改段，维持已闭合。

---

## 3. Findings

无协议/架构 P0/P1。无挡 L1 项。

（实现合同见 §4，不定级为挡开工缺陷。）

---

## 4. 实现合同跟踪表（§2.5.2 强制；绑 G-合入）

| 合同 ID | 正确性目标 | 绑定实现 commit/申请 | L2 验证证据 | G-合入状态 |
|---|---|---|---|---|
| **IC-1** inflight 时序 | 与 PenPal 同构：`xTaskCreate` **前** `__atomic_add_fetch`，失败立即 `--` 并 `delete` 快照。禁止「create 成功返回后再 ++」——双核上 worker 可能在 `xTaskCreate` 返回前跑完唯一出口并把计数减到 0，随后 UI 再 ++ 会把 inflight 永久卡在 1。证据：`ui_penpal.cpp:529-537` 注释即此竞态。v6 §3.3 字面是「成功后递增」，L2 按 PenPal 写，不按该句照抄。 | （实现申请锚） | CODE：create 前 ++ / 失败 --；快失败 worker（无 WiFi）后 inflight==0 | 待 |
| **IC-2** overwrite 旧指针 | `xQueueOverwrite` **丢弃槽内旧字节、不会回调**。被覆盖的指针 **poll 看不见**。覆盖丢失路径的 `delete` 必须发生在 overwrite 调用点：先 `xQueueReceive(..., 0)` 取出旧指针再 overwrite（或等价 peek+delete）。§3.1「被覆盖的旧指针由每 tick 排空兜底」这句话实现期作废。 | （实现申请锚） | CODE：overwrite 前收旧指针；SIM：结果未 poll 时第二 worker 快失败，无泄漏 | 待 |
| **IC-3** 确认→Update 指针 | `ota_check_result_t*` 与 `ota_start_async(..., ota_manifest_t*)` 不是同一类型。「全程同一堆对象、无复制」的正确写法：manifest 单独堆分配，确认时从 result **剥离** 后再 `delete` 外壳；或 `start_async` 接收整个 result 指针。禁止把内嵌成员指针交给 Update 后立刻删外壳（UAF）。 | （实现申请锚） | CODE：剥离/整对象移交二选一钉死；确认屏 sha256 == 写入槽 sha256 | 待 |
| **IC-4** 排空接线 | `factory.ino` `loop()` 在 `penpal_keyboard_poll()` 旁 **无条件** 调 `ota_result_poll()`；函数内先 drain 再 `if (!s_ota_active)`。v6 已写进 §3.3/§10，L2 核是否真接到 loop。 | （实现申请锚） | CODE：`factory.ino` 有调用；destroy 路径仍 drain | 待 |
| **IC-5** `drv.begin` `while(1)` | 组 4：失败改记日志继续（§8.1e）。现状 `factory.ino:708-710` 仍是死循环，v6 已不再写成「已拆」。 | 组 4 | CODE：无 `while(1) delay`；失败路径可走到自证 | 待 |
| **IC-6** 喂狗审计 | 实现期按 §10：setup/loop 任何 >T/4 阻塞插喂狗。清单外仍须看 `SPIFFS.begin(true)` format。GxEPD2 本板 `_busy_timeout = 10s`（`GxEPD2_310_GDEQ031T10.cpp:17` 传 `10000000` µs）；`T ≥ 30s` 对该默认值余量成立，仍以校准实测为准。 | 组 4 / 校准 | 两机打点 ≥2×；format/慢 SD 间隔 < T/2 | 待 |

Nit（不占缺陷、不挡、建议 ≤3）：① §5.3 T 公式多一个反引号；② §1 把 `ui_penpal.cpp:1330` 写成 drain 点，现网 drain 在 `:1379-1393`（`:1330` 是 cfg 键处理）；③ 自评「最有把握③」仍写 launch-time **copy**，与 §3.2「无复制」不一致，以 §3.2 为准。

疑问清单（不占缺陷）：`review_guide.md` §5.7 仍写「`setup()` 末尾自证」；本稿是 loop 首帧软件序列。属指南滞后，不挡 OTA 开工。

---

## 5. 已通过项（delta 独立复核）

- **指针通道方向正确**：FreeRTOS 队列字节复制，禁止含 `std::string` 按值入队。与契约 §2.1 / PenPal `new`→队列运指针→UI `delete` 同构。Codex v5 P1 的反例按正文不再可构造。
- **离屏不再堵发送**：overwrite + 无条件 poll。v5 Grok P2 反例（Check 离屏且不重进 → `xQueueSend` 永久阻塞）击不穿。
- **签名编码**：六字段 + 无前导零 + 小写 hex + UTF-8；示例字节串与 JSON 七键、`sig` 排除一致。v4 自指击不穿。
- **自证语义与代码一致**：`factory.ino:229-241` `done_seq` 在 `nextPage` 循环后无条件更新；`GxEPD2_EPD.cpp:137-151` busy 等待有界超时后继续。屏坏 → 会自证。v6 声明与 CODE 同向。
- **TWDT add 失败**：守卫仅 `ESP_OK` 置位，与失败分支一致。
- **覆盖层吸收**：§8.3 改为「下载中按键/框外触摸无副作用」，与 §4 全屏容器不再矛盾。
- **T 与本板默认 busy 超时**：构造参数 10s，下限 30s 盖得住；校准仍要实测（IC-6）。

---

## 6. 验证说明

- 锚 `97a4f53`。区间文件：`docs/ota-update-design.md`（受评）、`CHANGELOG.md`（v6 摘要与 §0/§13 一致，受评）。HEAD 之后的指南/v5 qwen 结果文件不在本区间。
- CODE：`factory.ino:229-241/:708-710/:828-829`；`ui_penpal.cpp:529-537`（create 前 ++）、`:1379-1393`（drain-then-active）；`GxEPD2_EPD.cpp:137-151`；`GxEPD2_310_GDEQ031T10.cpp:17`。
- 无 `pio run`，无真机。BUILD/HW = `UNKNOWN`。设计稿阶段按 §1.1 不虚构 CODE/HW 门禁。

---

## 7. 对称三格（§7.2；设计稿口径 §2.5.3）

1. **范围**：默认 = `e0ec3bb..97a4f53` diff + 作者 §0 变化点，不是把未改章节当新架构。未扩面。
2. **独立复跑**：未采信 §12 处置表。逐字核 §2.1 编码、§3.1–3.3 指针/poll/overwrite/inflight、§5.2 伪代码与自证声明；自证与排空回源码；PenPal inflight 时序回 `:529`。
3. **最没把握处反例**：
   - ① 喂狗完备性：SD 已进列表；`SPIFFS.begin(true)` 仍靠 §10 审计（IC-6）。未来新 peri 接受为残余风险。
   - ② `esp_task_wdt_init` re-init：不升缺陷（前轮头文件已证）。
   - ③ 自证不证明像素：按 v6 声明接受；用「BUSY 超时仍更新 done_seq」复验，**不再**要求屏坏回滚。
   - 另对「create 后 ++」构造双核快失败 → inflight 卡 1（IC-1，不挡开工）。

---

## 8. 声明矩阵

| 声明 | 状态 |
|---|---|
| 签名对象 = 六字段，不含 `sig` | `VERIFIED`（SPEC） |
| 字节级编码唯一（十进制/小写 hex/UTF-8） | `VERIFIED`（SPEC；L2 互测向量） |
| 结果通道只运指针，禁按值入队 string 结构 | `VERIFIED`（SPEC） |
| 离屏后 inflight 必归零（发送不阻塞 + 无条件 poll） | `VERIFIED`（SPEC；接线 → IC-4） |
| overwrite 旧指针必被 delete | `NO_CONTRACT` 字面「poll 兜底」不成立 → **IC-2** |
| inflight 无双核卡死 | v6 字面「成功后 ++」与 PenPal 先例相反 → **IC-1** |
| 确认的包 = 刷入的包 | `PARTIALLY_VERIFIED`（方向对；类型剥离 → IC-3） |
| `add` 失败不喂狗未订阅任务 | `VERIFIED`（SPEC） |
| `drv.begin` while(1) 已拆 | 不再声称；现状仍在 → IC-5 |
| 自证 = 软件序列；屏坏不回滚 | `VERIFIED`（SPEC+CODE） |
| A7682E 失败路径 ≈8.4s | `VERIFIED`（承前） |

---

## 9. §9 自审清单【D】/【ALL】

【文档对齐】【D】

- [x] 21 键名：六字段 + `OTA_URL` + `last_seq` 与发布步骤一致；编码已钉
- [x] 22 JSON 可解析；§5.2 TWDT 伪代码可照抄（add 失败分支一致）；§7 USB 命令被压缩成散文 → IC 在实现文档恢复，不挡 L1
- [x] 23 无把「已拆 drv」写成现状；真机项全 `CLAIM_ONLY`

【设计稿收口】【D+ALL】

- [x] 24 无协议/架构 P0/P1 → **给了 L1+A**；P2 进实现合同，未因完备性给 C
- [x] 25 复审默认只核 diff/变化点；未扩面
- [x] 26 未把 NO_CONTRACT 升 P1；IC-1/2/3 是与 PenPal 同构的调用约定。签名/回滚/TLS 承重段本轮无新洞
- [x] 27 未把伪代码当 ABI 静态分析去挡开工；双核 inflight 竞态有 PenPal 源码注释作先例，不升协议 P1

【ALL】

- [x] 15 无 P0/P1；IC 均有 `file:line` / 反例
- [x] 16 真机 `CLAIM_ONLY`；无 pio → BUILD `UNKNOWN`
- [x] 17 参照系 = `97a4f53` 设计稿 + 当前工作树源码行号（PenPal drain 行号已漂移，Nit ②）
- [x] 19 反例库 OTA 行：下载中断 / pending 回滚 / 低电 — 承前验证计划，真机仍 ⏸
- [x] 20 与 v5 本侧 C 的差异是指南 v1.3 口径，不是事实被推翻

【C】1–9、12–14、18：无实现代码，`NOT_APPLICABLE`。

---

## 10. 审批意见

- [x] **A. 全量接受（设计稿口径）** — L1 + 无协议/架构 P0/P1；实现合同 IC-1…IC-6 绑实现 G-合入
- [ ] B. 退回修订
- [ ] C. 部分接受

**G-开工：通过。** 下一评审对象是实现 commit（建议按 §9 组 1→4），不要开设计 v7。

**给 A 而非 C**：v1.3 §0 原则 11 / §2.5.2——无协议/架构 P0/P1 必须 L1+A。IC-1 是 §2.5.1 反例里点名的「create 失败回滚计数」同类项。

**给 A 而非 B**：指针通道、排空点、自证语义、编码钉、全屏吸收均已可实施；不是方向错误。
