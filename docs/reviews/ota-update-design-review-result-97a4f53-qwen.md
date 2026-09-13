# 评审结果：OTA 固件远程升级设计稿 v6（qwen）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md) **v6 修订稿**，锚 = **`97a4f53`**（"result channel, lifecycle contract, semantics fix"；git log 最新一条）。
- **评审类型**：设计稿对齐评审（review_guide **v1.3** §1.1 / §2.1 **L1 DOC-ALIGNED** track；**收口轮**——作者声明架构不变、只闭合上一轮 P1/P2/P3）
- **评审依据**：`docs/review_guide.md` **v1.3**（§2.5 设计稿收口、§0 原则 11 收口优先于完备、§0 原则 4b 增量审查锁定、§0.2 阻断力绑证据严重级、§1.1.1 L1 非目标）；上位准则 `CLAUDE.md`、`docs/async_ipc_contract.md`、`docs/issue_list.md`
- **评审范围（按 §0 原则 4b delta 锁定）**：v5→v6 diff（§0 六变化点）+ 作者变化点 + v5 轮 qwen 缺陷闭合。**未改章节（§2.2/2.3 信任根·配置源、§7 发布·基线、§5.4 三层模型骨架）当已闭合，不重开**——本轮无"修复引入回归"或"新旧段矛盾"，无扩面理由。
- **前轮关系**：v1=`…-36e88df-qwen.md`、v3=`…-45d11cf-qwen.md`、v4=`…-3eb6f7e-qwen.md`、v5=`…-e0ec3bb-qwen.md`，各轮独立锚不覆盖（§7.2）。
- **评审结论**：**L1 DOC-ALIGNED 达标 + A-（带跟踪项接受）**。**无协议/架构 P0/P1**；v5 轮 qwen 的 P2-1/P2-2/P3-1/Nit 全部正确闭合；v6 delta 经独立核验全部落在仓库既有可工作先例上。**按 §0 原则 11 + §2.5.2，无协议/架构 P0/P1 时必须给 L1+A/A-，不得因实现合同缺口给 C 倒逼 v7**。残留 4 项实现合同（P2@L2）+ 1 Nit 转入《实现期待办》绑实现 commit 的 G-合入。**建议过 G-开工、属主冻结设计，下一评审对象 = 实现 commit**（§2.5.4）。

---

## 一、v6 delta 核验（§0 六变化点；DOC/SPEC/CODE 证据）

| # | v6 变化点 | 核验 | 状态 |
|---|---|---|---|
| ① | **结果通道改指针传递**（Codex P1）：队列只运 `ota_result_t*`，worker `new`→`xQueueOverwrite`→UI `delete` 恰一次；禁含 `std::string` 结构按值入队 | `xQueueOverwrite` 存在于框架 `freertos/queue.h`；**先例核实**：PenPal `pp_result_t` 同款指针通道（`ui_penpal.cpp:1386 while(s_pp_q && xQueueReceive(s_pp_q,&res,0)==pdTRUE)` drain、`:565 res->gen!=s_pp_gen||!s_pp_active` stale-drop）；与 `async_ipc_contract §2.1`（worker new→队列→UI delete）一致 | ✅ VERIFIED（v5"按值入队含 string 结构=悬空"的反例成立，v6 指针化修复正确） |
| ② | **离屏排空点**：`factory.ino` loop() 无条件 `ota_result_poll()`，先排空后屏活跃判断（PenPal drain-then-active 同款） | `factory.ino:827-828 penpal_keyboard_poll();`（每拍无条件调，与 ai_chat/ai_cfg/shutdown poll 并列）；`ui_penpal.cpp:1379 penpal_keyboard_poll()` → `:1383` 注释"drain results FIRST (chat pattern)" → `:1386` 先 `xQueueReceive` 排空 | ✅ VERIFIED（先例真实存在且行为如述；OTA 同构合理） |
| ③ | **单飞/锁生命周期合同**：`s_ota_inflight` 在 `xTaskCreate` 成功后递增、失败即删快照不递增；递减在 worker 唯一出口（四路汇合）与 `s_ota_hw_lock` 同点；`xQueueOverwrite` 永不阻塞 | 先例：PenPal `s_pp_inflight`（`ui_penpal.cpp:61 volatile int`、`:531 __atomic_add_fetch`（创建前）、`:534 __atomic_sub_fetch`（失败回滚）、`:455 __atomic_sub_fetch`（出口减））——v6 的"创建失败回滚 + 唯一出口递减"正是该先例纪律 | ✅ VERIFIED（**v4 轮 qwen P2-1 + v5 轮 qwen Nit 闭合并强化**） |
| ④ | **TWDT add 失败分支**（Codex P3）：守卫 `s_boot_wdt_subscribed` 仅在 `add` 返回 `ESP_OK` 后置位；失败保持 false，喂狗点全跳过 | `esp_task_wdt.h:123` reset 在未订阅时返回 `ESP_ERR_NOT_FOUND`——v6"仅 ESP_OK 置守卫"消除"对未订阅任务 reset"错误路径，与 v5 轮已确证的 add(NULL)/re-init 语义一致 | ✅ VERIFIED（SPEC + 承 v4/v5 框架头核验） |
| ⑤ | **自证语义声明修正**（Qwen P2）：撤销 v5 自评③/§11"屏坏固件回滚"假声明，改"软件刷新序列完成、不证明像素上屏；屏坏会自证不回滚（可接受，层 3 兜底）" | 代码证据复核：`factory.ino:239-241 if(full){disp_flush_done_seq=disp_flush_req_seq;}`（nextPage 循环退出后**无条件**更新）+ `GxEPD2_EPD.cpp:149-151 if(micros()-start>_busy_timeout){…"Busy Timeout!"}`（**有界**超时后继续）→ 屏坏经库超时 → done_seq 照常更新 → 自证触发、不回滚 | ✅ VERIFIED（**v5 轮 qwen P2-1 闭合**；v6 §5.2/§11 新声明与代码现实一致，假声明已撤） |
| ⑥ | **编码钉死 + EPD 库内等待/SD 段喂狗 + §8.3 重写**（Grok P3-1/P3-2/P3-3、Qwen P3-1） | §2.1 规范化字节串：十进制无前导零/64 位小写 hex/UTF-8 + 示例串 + §8.6 两端重建逐字节互测向量；§5.3 EPD 段=库内 `_waitWhileBusy` 上限 `_busy_timeout`（不可插喂狗，T 下界来源）；`sd_care_init` 补喂狗点——核实 `factory.ino:748 peri_init_st[E_PERI_SD]=sd_care_init();` **inline 于 setup()**（确为阻塞段，v5 漏列、v6 补对）；§4 覆盖层改全屏吸收容器（消解 waitbox 220×130 外触摸打到底层控件的矛盾） | ✅ VERIFIED（**v5 轮 qwen P3-1 闭合**；sd_care_init inline 属实） |

**小结**：v6 §0 六变化点，**全部 VERIFIED**，且①②③⑥均落在仓库内**已工作**的先例（PenPal 指针通道 / drain-then-active / `s_pp_inflight` / inline 外设 init）上——非新造机制，实现风险低。

## 二、v5 轮 qwen 缺陷闭合核对（§0 原则 8：验证修复，构造反例击穿）

| v5 qwen 发现 | v6 处置 | 反例击穿尝试 | 判定 |
|---|---|---|---|
| **P2-1** 自评③/§11"屏坏回滚"假声明 | §5.2 语义声明块 + §11 修正条 + §0.5 | 反例"屏坏是否仍回滚"：`factory.ino:240` 无条件更新 + `GxEPD2` 有界超时 → 屏坏**不回滚**，与 v6 新声明一致；旧假声明已撤 | ✅ 闭合（修复正确，反例不再成立） |
| **P2-2** 验签快照所有权 / TOCTOU | §3.1 指针通道 + §3.2 单一堆对象 worker→UI→Update 移交、无共享缓冲 | 反例"陈旧 Check worker 污染已确认快照"：inflight cap 1（②③）+ 单一堆对象所有权链 → 结构上无第二 worker、无共享可写缓冲 | ✅ 闭合（TOCTOU 消除） |
| **P3-1** EPD 库内 `_waitWhileBusy` 不可喂狗、§5.3"秒级"低估 | §5.3 把 `_busy_timeout` 明列为**最长不可喂狗间隔**、纳入 T 下界、实现期实测；§5.2 喂狗点注明"EPD nextPage 循环每轮 nextPage() **之前**"（库内不可插） | 反例"T 是否仍可能 < EPD 库内等待"：T=max(间隔,含 `_busy_timeout`)×2、下限 30s → 公式保证 T>该间隔（值待实现期实测，方法成立） | ✅ 闭合（方法正确，值属 L2 校准） |
| **Nit** inflight 须全退出路径递减 | §3.3 唯一出口（成功/失败/取消/超时四路汇合）+ 10min deadline 保证终退出 | 反例"worker 挂死永不递减→OTA 永久锁死"：10min 绝对 deadline（任务本地）强制退出释锁 | ✅ 闭合 |

**v5 轮 qwen 4 条全部正确闭合，反例均不再成立。**

## 三、§2.5 三分法分类（本轮所有残留项 → 无一挡 L1）

按 v1.3 §2.5.1 对 v6 残留逐项分类（判定锚=**最小修复**）：

| 残留项 | 类别 | 挡 L1？ | 开 v7？ |
|---|---|---|---|
| manifest 在结果结构内的精确堆布局（独立堆对象 vs 结构成员）、exactly-once delete 全路径 | **实现合同**（§1.1.1：内存所有权/传参形式=L2 决策；最小修复=照 PenPal `pp_result_t` 同构钉死，非改承重规范） | 否（P2 应修@L2） | **否** |
| `xQueueOverwrite` 覆盖未消费旧指针的"泄漏窗口一个结果结构" | **实现合同**（inflight cap 1 + 每拍排空已使其结构上近不可达；L2 加断言/防御性 delete 即可） | 否（P2 应修@L2） | **否** |
| T 的具体数值（`_busy_timeout`/SD/A7682E/GPS 实测） | **实现合同/校准**（§5.3 已给方法 T=max×2 下限 30s；值=实现期打点，§8.1/8.2 验） | 否（P2 应修@L2） | **否** |
| 喂狗点完备性（除已列 5 处外，setup/loop 是否还有 >T/4 阻塞段，如 I2C 扫描 `factory.ino:672-690`、bq25896/bq27220/LoRa init） | **实现合同**（§5.3/§10 已立"新增长阻塞段必须同步插喂狗"合同；L2 审计） | 否（P2 应修@L2） | **否** |
| §1 引 `ui_penpal.cpp:1330` 而 drain-then-active 实在 `:1379-1386` | **完备性/文风**（行号轻微漂移，机制描述正确） | 否（Nit） | **否** |

**无一项属"协议/架构不可满足或按正文必错"**——签名可构造（六字段+编码钉死+互测向量）、回滚窗口不会系统性误杀（T=max×2 含 `_busy_timeout`、re-init API 已确证）、TLS/密钥边界不自相矛盾（专用 bundle 不继承 insecure）、结果通道/所有权/单飞均落在已工作先例上。故按 §2.5.1 **无 P0/P1 挡 L1**。

> **§1.1.1 适用**：上述"manifest 堆布局 / 是否 trivially copyable / delete 路径"正是 §1.1.1 明列的 **L1 非审查目标**（"伪代码非生产代码，严禁 ABI/指针/析构所有权源码级静态分析；内存所有权与传参形式属 L2"）。**v1.2 下我可能把 IC-1 当 P2 给 C（如本人 v5 轮所为）；v1.3 下它是实现合同，绑 L2，不挡开工**——这正是本轮应收敛之处。

## 四、实现合同清单（A- 跟踪项，绑实现 commit 的 G-合入；非 L1 阻断）

> 转入《实现期待办》/设计稿 §10，L2 代码评审核（对照 `async_ipc_contract.md` + PenPal 先例）：

- **IC-1（所有权链 exactly-once）**：实现须保证 manifest/结果堆对象**单一所有权**沿 worker(Check)→UI(CONFIRM_WAIT 持有)→Update 任务移交，**无按值复制**；delete 恰一次覆盖**全部路径**（成功消费 / stale-gen 丢弃 / 离页 destroy / cancel / 覆盖丢失 / `xTaskCreate` 失败）。L2 对照契约 §2.1 + PenPal `pp_result_t` new/delete 纪律核。
- **IC-2（overwrite 泄漏窗口）**：L2 加断言/防御——inflight cap 1 已结构上排除"两个未消费结果并存"；若仍要兜底，`xQueueOverwrite` 前先 `xQueuePeek`+delete 旧指针，或注释说明泄漏上界=1 个结果结构且每拍排空使其不可达。
- **IC-3（T 校准）**：实现期打点实测 `_busy_timeout`（GxEPD2 构造参数）+ SD 段 + A7682E（≈8.4s 已静态核）+ GPS 各段最长**喂狗间隔**，`T=max×2` 下限 30s，两机各测入 §7.3 基线；§8.1a/2 真机核 T ≥ 2× 最长间隔且健康固件不误回滚。
- **IC-4（喂狗完备性审计）**：L2 审计 setup()/loop() 全部阻塞段，凡 >T/4 且库内不可喂狗者（如 GxEPD2 `_waitWhileBusy`）须确保 T 覆盖；可插喂狗者（I2C 扫描、bq25896/bq27220/LoRa init、SD）按 §10 合同插 `esp_task_wdt_reset()`（受 `s_boot_wdt_subscribed` 守卫）。
- **Nit-1**：§1 行号 `ui_penpal.cpp:1330` 更正为 `:1379-1386`（`penpal_keyboard_poll` drain-then-active 实际位置）。

## 五、v1.3 收口判定（§2.5.2 / §2.5.4 / §0 原则 11 / §0.2）

- **为何 A- 而非 C**：§2.5.2"无协议/架构 P0/P1 时设计稿必须给 L1+A/A-；C 仅当有未闭合协议/架构 P0/P1 或承重方向错，且 C 的 required action 必须是'改哪一节规范'非'把实现细节写完整'"。本轮残留全是实现合同（§三），**给 C 倒逼 v7 = 违反 §0 原则 11**。
- **§0.2 阻断力绑证据严重级**：本人**无**带反例的协议/架构 P0/P1 → **无权阻断**；A- 是证据支持的唯一合规结论。（对照 v4 轮：彼时签名自指 P1 有据 → 必须挡；本轮无据 → 不得挡。阻断力随证据，不随轮次惯性。）
- **§2.5.4 收口轮 + 设计冻结建议**：v6 是收口轮（架构不变、只闭合 P1/P2/P3），delta 已核闭合。**建议属主：本轮后冻结 OTA 设计，下一评审对象 = 实现 commit 组（§9 的 0–5）**；同主题已满 6 版（v1→v6），远超 Max-3-Rounds 熔断线，残留分歧（若有）由属主裁决转《实现期待办》，**不再开 v7 × 多方**。
- **G-开工 判据满足**：L1 达标 + 无未闭合协议/架构 P0/P1 + 实现合同已列清单绑 G-合入。

---

## 六、验证说明（评审方环境）

- 开发机独立核验（delta 范围，§0 原则 4b）：`factory.ino:827-828`（每拍 poll）、`ui_penpal.cpp:61/455/504/531/534/565/1379-1386`（drain-then-active + `s_pp_inflight` 原子纪律）、`factory.ino:748`（sd_care_init inline setup）、`factory.ino:239-241` + `GxEPD2_EPD.cpp:149-151`（done_seq 无条件更新 + 有界超时，复核 v5 已证）、框架 `freertos/queue.h`（`xQueueOverwrite`）、`esp_task_wdt.h`（承 v4/v5 已确证 add(NULL)/re-init/reset 返回码）。
- **未独立复跑** `pio run`：设计稿无实现代码；§8 真机项无设备，按 §3.4 标 `UNKNOWN`（设计稿阶段合理，§8 计划完整、§9 校准前置）。
- 未改 `review_guide.md`、未改设计稿、未对工作区作任何修改。

## 七、对称三格（review_guide §7.2 强制；设计稿复审①按 §2.5.3 delta）

1. **是否核了 diff（设计稿复审=本版 diff + 变化点，非整份重开）**：是。按 §0 原则 4b 锁定 v5→v6 delta（§0 六变化点 + §12/§13 闭合表），逐项核验；未改章节（§2.2/2.3/§5.4/§7）当已闭合不重开，本轮无扩面理由（无回归、无新旧矛盾）。
2. **快照是否独立复跑**：部分。delta 涉及的 repo `file:line` 与框架头/库源码均独立读取核验（已记命中行）；`pio` 构建与 §8 真机用例无环境，标 UNKNOWN。
3. **申请"最没把握"处是否构造反例**：是，逐条——
   - 自评①（喂狗完备性）：反例"某未列阻塞段漏喂狗"→ v6 已把 `_waitWhileBusy`（库内不可喂）明列为 T 下界、补 SD 段、§10 立"新增长阻塞必插喂狗"合同；剩余"未来新增段"风险转 IC-4 实现期审计——反例被设计+合同覆盖（成立）。
   - 自评②（re-init idle 跨段行为）：承 v4/v5 框架头 DOC 确证（re-init 更新 timeout/panic、add(NULL) 只动当前任务、idle0 全程订阅不受影响）；§8.1a 真机兜底——反例不成立（API 语义已确证）。
   - 自评③（自证语义，本轮按 qwen P2 修正）：反例"屏坏是否回滚"→ 代码证实**不回滚**，与 v6 新声明一致——假声明已撤，反例闭合。

## 八、§9 自审清单（设计稿档：【D】+【ALL】；v1.3 新增 24-27）

```text
【D 文档对齐】
□ 21 字段/键名一致：清单六字段(version/url/size/sha256/seq/notes)+sig 在 §2.1 JSON/签名字节串/§7.1 脚本/§8.5 用例命名一致；编码钉死(十进制无前导零/小写hex/UTF-8) — 核过 ✅
□ 22 代码块可执行：§2.1 JSON 合法；§5.2 TWDT 调用序列与框架 API 签名一致(add/delete NULL、init 再调用、仅 ESP_OK 置守卫)；§7.2 命令可照抄 — 核过 ✅
□ 23 ✅≠证据：§12/§13 闭合表逐条回正文 + 拉代码核验(第一/二节)，未轻信"已闭合" — ✅
□ 24 无协议/架构 P0/P1 时是否给 L1+A/A-（P2 进实现合同）？— 是，给 A-，4 项实现合同转 §四，未因完备性给 C ✅（本条正是 v1.3 新增、本轮关键）
□ 25 复审是否默认只核 diff/变化点？扩面是否声明？— 是，§0 原则 4b delta 锁定，无扩面 ✅
□ 26 NO_CONTRACT/同构缺口是否被误升为挡 L1 的 P1？— 否，manifest 堆布局/overwrite 泄漏窗口均归实现合同(§三)，未升 P1 ✅
□ 27 是否把伪代码当生产 C++ 做 ABI/指针静态分析、或假想级联升格？— 否，§1.1.1 适用，所有权/传参形式明确归 L2 ✅
【ALL 通用】
□ 15 每条结论给证据：delta 六变化点逐条 file:line + 先例(第一节)；本轮无 P0/P1，无需反例阻断 ✅
□ 16 作者声明独立核验或如实标注：①-⑥ 独立核验；自评①②③构造反例(第七节)；真机项标 UNKNOWN ✅
□ 18 已设计≠与既有代码相容：v6 delta 对照 PenPal 指针通道/drain/inflight 先例 + factory.ino EPD/SD 路径，确认相容(第一节) ✅
□ 20 结论相反时核对方事实(非比票数)：v5 轮 Codex/Grok 给 C 的项(结果通道/排空)v6 已实修，本人独立核其闭合(第二节)，不因"上轮多方 C"惯性续 C ✅
```

## 审批意见

- [ ] A. 全量接受
- [x] **A-（带跟踪项接受）** — L1 DOC-ALIGNED 达标；无协议/架构 P0/P1；4 项实现合同（§四 IC-1..4）+ Nit-1 转《实现期待办》绑实现 commit 的 G-合入，**免再发设计评审**
- [ ] B. 退回重构
- [ ] C. 部分接受

> **G-开工 建议**：满足（L1 + 无未闭合协议/架构 P0/P1 + 实现合同已列清单）。**建议属主裁定 OTA 设计 A- 开工、冻结设计**（§2.5.4/§2.5.5），下一评审对象 = 实现 commit 组（§9 步骤 0–5，校准打点前置）。
>
> **净评**：v6 把 v5 轮四方（含 qwen）的 1 P1 + 2 P2 + 4 P3 + 1 Nit **全部正确闭合**，且修复全部落在仓库内已工作的先例上（PenPal 指针通道 / drain-then-active / `s_pp_inflight` / inline 外设 init）——架构稳定、协议可构造、回滚窗口不误杀、所有权链无 TOCTOU、自证语义已诚实。**这是 OTA 设计第一个真正达 L1 的版本**。按 v1.3 收口纪律，本轮应 A- 开工、停止 vN×多方空转；残留全是 L2 实现合同，由实现 commit 评审核（IC-1..4 已列）。本人 v5 轮曾对同类实现细节给 C（§四自评），v1.3 §1.1.1/§2.5 正是为纠此偏——本轮据新指引收敛。
