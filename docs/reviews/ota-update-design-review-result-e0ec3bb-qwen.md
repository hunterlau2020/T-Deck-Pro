# 评审结果：OTA 固件远程升级设计稿 v5（qwen）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md) **v5 修订稿**，锚 = **`e0ec3bb`**（"sig self-reference fixed plus four v4-round P2s"；git log 最新一条，符合申请头部"锚=本稿 commit SHA"）。
- **评审类型**：设计稿对齐评审（review_guide §1.1 / §2.1 **L1 DOC-ALIGNED** track）
- **评审依据**：`docs/review_guide.md` v1.2；上位准则 `CLAUDE.md`、`docs/async_ipc_contract.md`、`docs/issue_list.md`；申请头部"评审要求"
- **前轮关系**：本文件是 **v5 轮**（锚 `e0ec3bb`）的 qwen 评审；v1=`…-36e88df-qwen.md`、v3=`…-45d11cf-qwen.md`、v4=`…-3eb6f7e-qwen.md`，各轮独立锚、绝不覆盖（§7.2）。v5 已吸收 v4 轮 qwen 的 **P2-1/P2-2/P3-1/P3-2/Nit 全部 5 条**（§0 行 4/5/6、§12 行 4/5/6/7/3）。
- **评审结论**：**C 部分接受**（很接近 L1，**无 P1**）。v4 轮 qwen 的 5 条全部正确闭合、签名自指 P1 已修、TWDT 合同 API 经独立核验成立；但 v5 **自评③ + §11 把"屏坏固件回滚"列为已裁决决策，与 EPD flush 代码现实矛盾**（自证门控只证明软件刷新跑完、不证明像素上屏）——该确定性声明为假，**阻断 L1**，须在批准前改正。余 1×P3 + 1 Nit。**另：诚实登记本人 v4 轮盲点**（漏掉签名自指 P1，见第六节）。

---

## 一、v5 修复项核验（review_guide §4.4 矩阵 + §0.3 独立复现）

| # | v5 声明 | 核验 | 状态 |
|---|---|---|---|
| ① | **签名自指 P1 修复**：签名对象=显式**六字段**（version/url/size/sha256/seq/notes），`sig` 排除在外（§2.1） | SPEC：六字段字节串 `version\n url\n size\n sha256\n seq\n notes\n` → `sig=Sign(该串)`，设备端同序重建六字段验签——**无自指、可构造**。§8.6 加"占位 sig + 六字段正确签名 → 通过"用例钉死可执行性 | ✅ VERIFIED（修复正确；v3 自洽写法找回 + 保留 v4 两条澄清） |
| ② | **TWDT 句柄用 `add(NULL)`/`delete(NULL)`**（NULL=当前任务，禁写函数名 `loopTask`）（§5.2） | 框架 `esp_task_wdt.h:73-74` *"Input NULL to subscribe the current running task"*、`:117-118` delete(NULL)=unsubscribe current；`main.cpp:40 void loopTask(void*)` 是入口**函数**、句柄是 `loopTaskHandle`——v5"禁写函数名、用 NULL"**API 正确** | ✅ VERIFIED |
| ③ | **re-init 改超时**（`init(T,true)` 开窗 / `init(5,true)` 恢复，无需 deinit）（§5.2） | `esp_task_wdt.h:28-30` *"If the TWDT is already initialized… will update the timeout period and panic configurations"*（v4 轮已确证，v5 承用） | ✅ VERIFIED（自评②的 API 不确定性 DOC 级闭合；仍建议 §8.1a 真机复核 panic 复位） |
| ④ | **喂狗守卫 `s_boot_wdt_subscribed`**：自证后 EPD 刷新不再 reset（避免 `ESP_ERR_NOT_FOUND`）（§5.2） | `esp_task_wdt.h:123` reset 在未订阅时返回 `ESP_ERR_NOT_FOUND`；自证点 `delete(NULL)` 后 loopTask 不再订阅 → 守卫标志关闭后续 reset，逻辑自洽 | ✅ VERIFIED（SPEC） |
| ⑤ | **单飞守卫 `s_ota_inflight`**（cap 1，覆盖 Check+Update，跨 gen/页面，不依赖 busy）（§3.1） | 照 PenPal `s_pp_inflight`（`acc3893`，issue_list §10，已真机验证）；正确指出 v4 缺口=busy 被 contract §2.8 gen-reset | ✅ VERIFIED（**v4 轮 qwen P2-1 闭合**） |
| ⑥ | **快照所有权链**：Check worker 经结果（`ota_check_result_t{gen,ok,manifest,err}`）交回 UI → CONFIRM_WAIT 期 UI 持有 → 确认时 launch-time copy 进 Update 任务自有副本；无共享静态（§3.2） | 符合 `async_ipc_contract §2.5`（任务持自有副本、不共享可变缓冲）；"确认的包=刷入的包"四点同一快照，TOCTOU 关闭 | ✅ VERIFIED（**v4 轮 qwen P2-2 闭合**） |
| ⑦ | **`A7682E_init` 失败路径 ≈8.4s**（修正 v3/v4 的 6.1s）（§1） | `factory.ino:517-535`：`retry++ > 5` 后置自增跑 **7 轮** `testAT(1000)`=7000ms + break 分支 1100ms + 上电 70ms + 循环后 200ms ≈ 8370ms | ✅ VERIFIED（**v4 轮 qwen P3-1 闭合**，数字逐行复核一致） |
| ⑧ | **校准度量=最长喂狗间隔**（T 约束间隔非总时长），`T=max(实测最长间隔)×2` 下限 30s（§5.3） | 符合 TWDT 语义（每任务 reset 间隔，非累计运行时长） | ✅ VERIFIED（**v4 轮 qwen P3-2 闭合**；但见 P3-1：EPD 段间隔被低估） |
| ⑨ | **取号副作用隔离**：`ui_disp_full_refr_seq()` 只调一次存变量，轮询只读 done（§0.3/§5.2） | `factory.ino:863 disp_full_refr_seq(){disp_full_refr(); return disp_flush_req_seq;}`（调用即 `req_seq++` + 触发整刷）；既有惯用法 `ui_deckpro.cpp:2808→2830`、`:4518→4494` 均"取号一次→轮询 done" | ✅ VERIFIED（**v4 轮 qwen Nit 闭合**） |

**小结**：v5 §0 六项变化点 + §12 八条处置，**九处可核验项全部 VERIFIED**；v4 轮 qwen 5 条逐条正确闭合，签名自指 P1（Codex/Grok/Claude 命中）修复正确，TWDT 可实施合同的 API 假设（add(NULL)/re-init/reset 守卫）经框架头文件独立确证。

---

## Findings

### P2-1：自评③ + §11"屏坏固件回滚 / 自证=用户可见可用"是**已裁决决策**，但与 EPD flush 代码现实矛盾——自证门控只证明软件刷新跑完，**不证明像素上屏**

- **位置**：设计自评③（"若新固件显示硬件失效（首帧永远不完成），WDT 会把'屏坏但系统健康'的固件也回滚……请评审确认接受"）+ §11（"自证依赖 EPD 首帧（屏坏固件回滚是有意决策——自证语义 = 用户可见可用）"）；冲突代码 `examples/pda2/factory.ino:228-241`（`flush_epd_bitmap`）+ `lib/GxEPD2/src/GxEPD2_EPD.cpp:137-151`（`_waitWhileBusy`）。
- **证据（代码现实，逐行核）**：
  1. `factory.ino:229-235`：`display->firstPage(); do{…drawInvertedBitmap…}while(display->nextPage());` 随后 `:239-241 if(full){ disp_flush_done_seq = disp_flush_req_seq; /* THIS frame reached the panel */ }`——**`done_seq` 在 nextPage 循环退出后无条件更新，不校验面板是否真的渲染**（注释"reached the panel"是假设、非检查）。
  2. `GxEPD2_EPD.cpp:137 _waitWhileBusy(...)`：`:145/:148 if(digitalRead(_busy)!=_busy_level) break;` `:149 if(micros()-start > _busy_timeout)` `:151 Serial.println("Busy Timeout!")`——**busy 等待有界**（`_busy_timeout` 构造参数），超时即打印"Busy Timeout!"并**继续/返回**，不无限挂死。
  3. 推论：**屏坏（BUSY 卡死）→ `_waitWhileBusy` 超时返回 → `nextPage()` 返回 → `:235` 循环退出 → `:240` done_seq 更新 → 自证门控 `done_seq >= full_refr_seq()` 达标 → `esp_ota_mark_app_valid_cancel_rollback()` 触发 → 固件被标记有效、_不回滚_**。
- **影响**：自评③/§11 的确定性声明"屏坏固件回滚 / 自证语义=用户可见可用"为**假**——自证门控实测的是"LVGL→EPD 软件刷新序列跑完"，**不是"像素上屏/用户可见"**。这是被列入 §11"已裁决事项（不再开放）"且**显式请评审确认接受**的决策，其机制前提与代码不符。后果 **B/D**（已裁决契约的语义声明与实现不符——若照批，§11 将固化一条假不变量，误导实现/测试：可能有人据此加多余的面板健康门控，或写一个永远不发生的"屏坏回滚"§8 用例）；可达性 2（声明恒在，影响在"有人据假前提行动"时显现）；静默（假声明躺在已裁决节）。坐标 **B/2/静默 → P2**。**注**：实际运行行为（屏坏→自证→不回滚）本身**是可接受的**（回滚也修不好坏屏，层 3 USB 兜底）——缺陷在**文档/决策声明为假**，非运行安全洞。
- **两轴**：`VERIFIED`（CODE：`factory.ino:240` 无条件更新 + `GxEPD2_EPD.cpp:149-151` 有界超时，二者联立即证伪自评③）/ `VIOLATES`（§11 已裁决声明与 `factory.ino` EPD flush 现实不符；review_guide §9【D】"确定性用语须标注目标 vs 已验证事实"）。
- **最小修复**（二选一，须在批准 L1 前定）：
  - **(推荐) 改正语义声明**：把自评③/§11 改为——"自证门控 = **首个 FULL flush 的软件序列完成**（`disp_flush_done_seq` 达标），它捕获 setup/loop 首帧前的挂死与 LVGL 崩溃，但**不证明像素上屏**；屏坏（GxEPD2 busy 超时）时 done_seq 仍更新 → 固件**会自证、不回滚**，这是可接受的（回滚修不好坏屏，层 3 USB 兜底）。"**不要**把"屏坏回滚"列为已裁决事实。
  - (若真要屏坏门控) 自证门控不能只看 `done_seq`——须加真实面板健康检查（如读 BUSY 状态/ID），代价高、收益低，不推荐。
- **评审动作**：**拒绝按现状确认自评③**（申请显式请评审"确认接受"，但其机制前提经核验为假）。

### P3-1：§5.3"EPD nextPage 每轮喂狗、间隔秒级"低估——单次 `nextPage()` 内部 `_waitWhileBusy` 可达 `_busy_timeout`（库内不可喂狗），是 EPD 段最长喂狗间隔

- **位置**：设计 §5.2 具名喂狗点"EPD nextPage/BUSY 等待循环每轮" + §5.3"间隔都在秒级（testAT ~1s/轮、getAck ~800ms/轮）"；代码 `factory.ino:229-235`（喂狗点在 `:229` do-while **循环层**，即相邻 `nextPage()` 之间）+ `GxEPD2_EPD.cpp:137-151`（`_waitWhileBusy` 在 `nextPage()` **内部**）。
- **证据**：§5.2 的 EPD 喂狗点只能插在 `factory.ino:229` 的 `do{…}while(nextPage())` 循环体（每轮之间），**无法插进 GxEPD2 库内部的 `_waitWhileBusy`**。一次 `nextPage()` 调用内部 `_waitWhileBusy` 在慢/坏面板上可阻塞至 `_busy_timeout`（GxEPD2 全刷默认量级 ~10s）——这段**库内阻塞无喂狗机会**。故 EPD 段的"最长喂狗间隔"≈ 一次 `nextPage()` 的 busy 等待（可达 ~`_busy_timeout`），**不是 §5.3 说的"秒级/每轮"**。
- **影响**：后果 **D**（校准度量对 EPD 段估计偏小）；所幸 `T` 下限 **30s** 通常 > GxEPD2 `_busy_timeout`，故不致误触发——但 §5.3"间隔都在秒级"的表述会让实现者误以为 T 可收得很紧。坐标 **D/2 → P3**。
- **两轴**：`VERIFIED`（CODE：喂狗点在库外循环层 + `_waitWhileBusy` 有界但可达 `_busy_timeout`）/ `CONFORMS`（30s 下限兜底，机制不破）。
- **最小修复**：§5.3 注明"EPD 段最长喂狗间隔 = 单次 `nextPage()` 内部 `_waitWhileBusy`，上界 = GxEPD2 `_busy_timeout`（库内不可喂狗）；**T 下限 30s 必须 > 实测 `_busy_timeout`**"；校准打点须含"一次 full `nextPage()` 在慢/坏面板下的最长耗时"，不能只测健康快路径。

### Nit：`s_ota_inflight` 递减须覆盖**全部**退出路径，否则 worker 异常挂死会永久锁死 OTA

- **位置**：设计 §3.1（"`s_ota_inflight`……`xTaskCreate` 前增、任务退出减"）。
- **证据**：§3.1 只说"任务退出减"，未明示**每条**退出路径（成功 end / 失败 abort / 取消 abort / 超时）都减。若任一路径漏减（或 worker 卡在不可达 deadline 的阻塞里永不退出），`inflight` 永为 1 → 后续 Check/Update 全被拒（"previous OTA op closing"）→ OTA 永久锁死至重启。v4 §3.3 对 `s_ota_hw_lock` 已有"三条路径同一出口"纪律，`inflight` 应同款。
- **影响**：E（实现纪律/健壮性）；坐标 **E/2 → Nit**。
- **最小修复**：§3.1/§10 合同补一句"`s_ota_inflight` 递减在任务**唯一出口**（成功/失败/取消/超时四路径汇合后），与 `s_ota_hw_lock` 释放同点；10min 绝对 deadline 保证任务终将退出释锁"。

---

## 二、§12 v4→v5 处置是否真落地（MACAO 模式 B：✅≠证据）

逐条回正文：①§2.1 六字段 + §8.6 占位 sig 用例—**正文有**；②§5.2 TWDT 合同（add(NULL)/守卫/失败处理）—**正文有**（API 已核 ②③④）；③§5.2 取号一次—**正文有**；④§3.1 `s_ota_inflight` + §8.4 用例—**正文有**；⑤§3.2 所有权链 + §8.5 用例—**正文有**；⑥§1 8.4s—**正文有**；⑦§5.3 最长喂狗间隔度量—**正文有**（但 EPD 段低估，P3-1）；⑧自评③显式提请裁定—**正文有**（但前提为假，P2-1）。**8/8 真落地，无虚标**；唯 ⑦⑧ 落地内容本身有 P3-1/P2-1 需修正。

---

## 三、已通过项

- **签名自指 P1 正确修复**：六字段显式枚举、`sig` 排除、§8.6"占位 sig→通过"用例钉死可构造性——干净利落。
- **TWDT 可实施合同 API 全部成立**：`add(NULL)`/`delete(NULL)`=当前任务（头文件 :73/:117 确证）、`init` re-call 改超时（:28-30）、reset 守卫避 `ESP_ERR_NOT_FOUND`（:123）；"禁写函数名 `loopTask`、用 NULL"纠正了 v4 的潜在笔误。
- **v4 轮 qwen 5 条全闭合**：P2-1（`s_ota_inflight` 单飞，照 PenPal 先例）、P2-2（快照所有权链，TOCTOU 关闭）、P3-1（8.4s）、P3-2（喂狗间隔度量）、Nit（取号一次）——逐条 VERIFIED。
- **喂狗完备性审计入合同**（§5.3/§10"新增 setup 长阻塞 >T/4 必须同步插喂狗点"）：正面回答自评①，把"漏标喂狗点"从隐性风险转为实现期 checklist 项 + §8.1b 回滚矩阵兜底。
- **§8 验证计划补齐 v4 缺口**：§8.4 Check 并发（P2-1 用例）、§8.5 快照一致性 sha256 比对（P2-2 用例）、§8.6 占位 sig（P1 可执行性）——发现与用例一一对应。

---

## 四、验证说明（评审方环境）

- 开发机（Windows，repo 工作副本 + `~/.platformio` 框架缓存）独立核验：读框架 `esp_task_wdt.h`（②③④：add/delete NULL 语义、re-init 更新超时、reset 返回码）；`rg`+读 `factory.ino:209-242`（EPD flush 无条件更新 done_seq）、`GxEPD2_EPD.cpp:137-151`（`_waitWhileBusy` 有界超时）、`factory.ino:517-535`（A7682E 8.4s 逐行复核）、`factory.ino:863-873`+`ui_deckpro.cpp:2808/2830/4518/4494`（EPD seq 惯用法）、`peri_gps.cpp`（GPS_Recovery/getAck 存在）；SPEC 核 ①⑤⑥ 签名/单飞/所有权链。
- **未独立复跑** `pio run`：设计稿无实现代码；§8 真机项无设备，按 §3.4 标 `UNKNOWN`（设计稿阶段合理，§8 计划完整）。
- 未对工作区作任何代码修改。

## 五、对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：是。v5 是设计稿（无代码 diff）；评审方未止于 §12 处置表，把 §0 六变化点 + §1 修正 + §5.2 TWDT 合同全部拉到框架头文件/repo 源码/库源码核验，并**主动追到设计未质疑的自评③机制前提**——`factory.ino:240` × `GxEPD2_EPD.cpp:149-151` 联立证伪"屏坏回滚"（P2-1），非 §12 处置项、非任何前轮发现。
2. **快照是否独立复跑**：部分。框架头/库源码/repo `file:line` 均独立读取（已记命中行）；`pio` 构建与 §8 真机用例无环境，标 UNKNOWN。
3. **申请"最没把握"处是否构造反例**：是，逐条——
   - 自评①（喂狗点覆盖完备性）：构造反例"某长阻塞段漏喂狗点"→ §5.3/§10 已加"新增 >T/4 阻塞必须插喂狗"合同 + §8.1b 兜底，反例被设计覆盖（成立）；但发现 EPD 段间隔被低估（P3-1，库内不可喂狗）。
   - 自评②（re-init idle 订阅处理）：构造反例"re-init 影响 idle0 订阅/超时"→ 头文件 :28-30 证 re-init 只更新 timeout/panic、add(NULL)/delete(NULL) 只动 loopTask、idle0 全程订阅不受影响——反例不成立（v5 假设正确）。
   - 自评③（屏坏→WDT 回滚）：构造反例"屏坏但 nextPage 经 busy 超时返回"→ `:240` 无条件更新 done_seq → 自证触发 → **不回滚**，**击穿**自评③（P2-1）。

## 六、评审方盲点登记（review_guide §9：连续漏审须显式登记）

- **本人 v4 轮（`…-3eb6f7e-qwen.md`）漏掉签名自指 P1**：v4 §2.1"七字段按序拼接"把 `sig` 计入签名对象（`sig=Sign(…,sig)` 自指、不可构造），Codex/Grok/Claude 三方 v4 轮均命中，**qwen 未命中**。复盘：v4 报告 §9【D】"字段/键名一致性"一行（v4 结果文件 line 117）只核了**七字段命名一致**，未核**签名对象的字段集合语义**（sig 是否应被排除）——即"字段名对不对"查了，"哪些字段该进签名"没查。
- **激活的 checklist 项**：§9【D】"字段/键名一致性"须扩展为"**字段集合 + 语义**一致性"——凡签名/哈希/MAC 类规范化字节串，必查"被签集合是否含签名自身/是否漏字段/顺序是否两端一致"，而不只查命名。本轮（v5）已据此核 ① 六字段集合正确、sig 排除、两端同序——闭环。
- **更正记录**：v5 §12 行 1 把该 P1 归为"Codex/Grok/Claude/Qwen 四方共同"——**就 qwen 而言不准确**（qwen v4 轮未独立命中，系 v5 头转述）。特此登记，避免"四方一致"掩盖实际漏审。

## 七、§9 自审清单（设计稿档：【D】文档对齐 + 【ALL】通用）

```text
【D 文档对齐】
□ 字段集合+语义一致性（本轮据第六节扩展）：签名对象=六字段(version/url/size/sha256/seq/notes)、sig 排除、两端同序、notes 禁换行 — 核过 ✅（v4 漏审项本轮闭环）
□ 代码块可执行：§2.1 JSON 合法；§5.2 TWDT 调用序列与框架 API 签名一致(add/delete NULL、init 再调用、reset 守卫)；§7.1/§7.2 命令可照抄 — 核过 ✅
□ ✅≠证据：§12 八条处置逐条回正文(第二节)；§0/§1 九处引用拉实物核验(第一节)；自评③未当既定事实——构造反例证伪(P2-1) ✅
□ 确定性用语标注：自评③/§11"屏坏回滚"是确定性声明且为假 → P2-1 要求改正(不可标注为已验证事实) ✅
【ALL 通用】
□ 每条 P0/P1/P2 给 file:line + 反例：P2-1(factory.ino:240 + GxEPD2_EPD.cpp:149-151 + 死屏反例) ✅（本轮无 P0/P1）
□ 作者"已核实/最没把握"独立核验或如实标注：①-⑨ 独立核验；自评①②③构造反例(第五节) ✅
□ 参照系与结论一致：核验对象 = pda2 实际用的框架头 + 现网 factory.ino/GxEPD2 库源码；P2-1 参照系 = EPD flush 真实代码路径 ✅
□ "已设计≠与既有代码相容"：本轮核心——v5 自评③/§11 的自证语义对照 factory.ino:240 EPD flush 现实，发现不相容(P2-1)；TWDT 合同对照框架 API 相容(✅) ✅
□ 结论相反时核对方事实(非比票数)：v4 轮签名自指 P1 三方命中、qwen 漏——本轮独立核 v5 六字段修复正确(第一节①)，并诚实登记盲点(第六节)，不因"四方一致"掩盖漏审 ✅
```

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

> **达 L1 / 开工前置条件**（review_guide §2.2 G-评审→G-合入；本轮无 P1）：
> - **P2-1（阻断 L1）**：改正自评③ + §11——自证门控实测"首个 FULL flush 软件序列完成"，**非"像素上屏/用户可见"**；屏坏经 GxEPD2 busy 超时仍自证、**不回滚**（此行为可接受，但"屏坏回滚"不得列为已裁决事实）。**拒绝按现状确认自评③。**
> - **P3-1**：§5.3 注明 EPD 段最长喂狗间隔 = 单次 `nextPage()` 内部 `_waitWhileBusy`（上界 GxEPD2 `_busy_timeout`，库内不可喂狗），**T 下限 30s 须 > 实测 `_busy_timeout`**；校准含慢/坏面板 full-nextPage 最长耗时。
> - **Nit**：§3.1/§10 补 `s_ota_inflight` 递减在任务唯一出口（四路径汇合），与 hw_lock 释放同点。
> - 上述关闭后，设计稿可达 **L1 DOC-ALIGNED** 并据以开工（实现后另走 L2/L3 代码评审，锚=实现 commit；§8 真机用例为 G-真机/G-发布 门禁，含 §8.1a WDT re-init panic 复位真机复核）。
>
> **净评**：v5 收敛得**非常干净**——签名自指 P1 修正、TWDT 合同 API 经独立核验全部成立、v4 轮 qwen 5 条逐条正确闭合、发现与 §8 用例一一对应。唯一阻断是 **自评③/§11 把一条与 EPD flush 代码矛盾的"屏坏回滚"语义列为已裁决决策并请评审确认**——经 `factory.ino:240` + `GxEPD2_EPD.cpp:149-151` 核验为假，须改正后方可 L1。改一句话即可，不动机制。另诚实登记：本人 v4 轮漏审签名自指 P1，本轮据 §9 扩展"字段集合+语义"检查项闭环。
