# 评审结果：PenPal 点击死机根治——LVGL 池扩容 48K→64K（qwen）

- **评审日期**：2026-09-13
- **申请文件**：[penpal-pool-freeze-review-request-721e04a.md](penpal-pool-freeze-review-request-721e04a.md)
- **评审提交**：`721e04a`（`lvgl: enlarge LV_MEM pool 48K -> 64K - PenPal click-freeze root cause`）
- **评审范围**：`config/lv_conf.h`（`LV_MEM_SIZE` 48K→64K + 根因注释）、`examples/pda2/ui_penpal.cpp`（`pp_dbg_pool()` 水位观测点）。按申请说明纳入定位证据链（`1269bf7`/`f8d73f6` bisect）与 issue_list §15 核对。
- **评审依据**：`docs/review_guide.md`（本项目评审指南 v1.0）
- **评审结论**：**A 全量接受**（L3 SCENARIO-VERIFIED）。修复正确、根因证据链完整、真机已确认；附 2 Nit + 1 决策项（均不阻断）。

---

## 声明矩阵核验（review_guide §4.4）

申请每条强声明独立核验，全部 `VERIFIED`：

| # | 申请声明 | 核验方式 | 状态 |
|---|---|---|---|
| ① | 改动 +23/−1，2 文件 | `git show --stat 721e04a`：lv_conf.h +8/−1、ui_penpal.cpp +16 | ✅ VERIFIED |
| ② | `LV_MEM_SIZE` `(48U*1024U)`→`(64U*1024U)` | `git show 721e04a -- config/lv_conf.h` diff 逐字命中 + 根因注释 | ✅ VERIFIED |
| ③ | 池水位 create 2172B → cached 540B → wbshow 524B | commit message `[PD]` 遥测三行与申请 §2.3 **逐字一致** | ✅ VERIFIED (PROBE) |
| ④ | app RAM 50.1%→55.1%，余量 ~17K | commit message "same checkpoints now read ~17K free; app RAM 50.1% -> 55.1%" | ✅ VERIFIED (BUILD) |
| ⑤ | panic 回溯 LoadProhibited 0x22 via `pp_waitbox_show→lv_btn_create→lv_obj_mark_layout_as_dirty` | commit message 完整一致（ELF 哈希校验后 addr2line 解码） | ✅ VERIFIED (HW) |
| ⑥ | 真机 mail list + Sync + thread open + topic pick 全稳定 | commit message "Device-verified … (user-confirmed 2026-08-29)" | ✅ VERIFIED (HW，用户确认) |
| ⑦ | `pp_dbg_pool()` 观测点进主线（create/cached/wbshow 三处） | `git show 721e04a -- ui_penpal.cpp`：新增 16 行 `pp_dbg_pool()` + `[PD]` 串口行 | ✅ VERIFIED (CODE) |

**两轴标注（§4.3）**：轴一 = `VERIFIED`（CODE+BUILD+PROBE+HW 四证齐备）；轴二 = `CONFORMS`（本改动为编译期配置 + 串口观测点，不触及异步任务面，async_ipc_contract 不适用）。

---

## Findings（无阻断项）

### Nit-1：`pp_dbg_pool()` 代码注释的生命周期与申请意图矛盾

- **位置**：`examples/pda2/ui_penpal.cpp` `pp_dbg_pool()` 上方注释（`721e04a` 新增）。
- **证据**：注释写 *"keep until the enlargement (LV_MEM_SIZE 64K) is device-verified, then trim to a boot-time line or drop"*；而申请 §3 明确要把 `pp_dbg_pool()` **永久**留作观测点（"防再次逼近临界而无感"），且 §2/commit 已记 device-verified（2026-08-29）。注释自述的"验证后即裁剪/删除"生命周期与"永久保留"意图直接冲突。
- **影响**：后续读者无法判断该观测点是临时诊断还是常设；可能误删承重的水位预警。坐标 `E/3/有声 → Nit`。
- **两轴**：`VERIFIED`（CODE）/ `CONFORMS`。
- **最小修复**：二选一改齐——把注释改为"常设水位观测点，widget tree 增长时据此预警"，或按注释原意裁剪为 boot-time 单行。建议前者（保留观测价值）。

### Nit-2 / follow-up：LV_MEM 池是全局资源，峰值 = 所有屏的最大值

- **位置**：`config/lv_conf.h` `LV_MEM_SIZE`（全局）；观测点仅在 `ui_penpal.cpp`。
- **证据**：64K 是按 **PenPal** 峰值（wbshow 时 used ≈ 47.5K，free 524B）定的。但 LVGL 内存池是**进程级单池**，运行期峰值取决于当前最重的屏，而非 PenPal 独有。AI Chat（长回复 + 多轮上下文气泡）、词典、天气等屏的对象树同样从这 64K 分配。
- **影响**：PenPal 不再是唯一逼近临界的屏；下一个"压垮的稻草"可能出现在另一重屏，而该屏没有 `pp_dbg_pool()` 观测点 → 无感逼近。坐标 `D/2/静默 → 观察`（非本批引入，是池扩容后新暴露的观测盲区）。
- **两轴**：`PARTIALLY_VERIFIED`（纸面推演成立，未对其余重屏实测水位）/ `CONFORMS`。
- **最小修复**：对最重的 2-3 个屏（AI Chat 满上下文、词典长词条、天气三页）做一次 `pp_dbg_pool()` 式水位审计；或把水位观测点提取为通用 helper，在重屏 create 时各打一行。登记 `issue_list.md` 作 follow-up，不阻断 `721e04a`。

### 决策项（答申请 §4.4）：诊断批弃置——确认主线无 off-UI-thread SPIFFS/LVGL 写

- **位置**：申请 §4.4 提及弃置的诊断批含"worker 写 SPIFFS 移到 UI 线程"改动。
- **证据**：该诊断改动的存在暗示曾怀疑主线有 worker 线程直接写 SPIFFS（违反 async_ipc_contract §2.1 所有权：结果 `new`→队列→UI 消费后 `delete`/持久化，任务线程不应碰 UI/FS 持久化）。诊断批"整体弃置未合入" → 主线未变。
- **影响**：若主线确有 worker 线程 SPIFFS 写，则是一条独立于池耗尽的潜在竞态/契约违约（B 类）；若主线本就干净，弃置完全正确。坐标待定（取决于核验结果）。
- **两轴**：`UNKNOWN`（未在本轮核验主线 SPIFFS 写所在线程）/ 待定。
- **最小修复**：核验主线 `/chat.log`/PenPal 缓存的 SPIFFS 写是否在 UI 线程（grep `SPIFFS`/`File` 写点 + 看其调用栈是否经队列回 UI）。干净 → 弃置认可、关闭本决策项；不干净 → 单独开条目走正式评审。**非 `721e04a` 阻断项**。

---

## 已通过项

- **根因定位方法可信**：三段设备 bisect（`1269bf7` 稳定 → `f8d73f6` 一点即崩 → 空 label 探针排除渲染逻辑 → `pp_dbg_pool()` 水位实锤 524B）是教科书式收窄，把"渲染逻辑 bug"与"池耗尽"正确分离；诊断构建崩溃点漂移（`lv_mem_buf_get` 挂死 / layout-dirty panic）统一归因池耗尽，未误判为多个独立 bug。
- **修复对症且最小**：扩容 + 根因注释 + 水位观测点，未夹带无关重构；注释把"48K 为何不够、64K 余量、如何监控再生"写清，符合 review_guide §5.3 的池纪律。
- **build 坑联动正确**：commit message 与申请 §4.3 都点明 `config/lv_conf.h` 经 `-include` 无依赖跟踪、改后必须 `pio -t clean`（build 文档坑 7），并以 size 报告 RAM 涨幅（50.1%→55.1%，+16384B = 池增量）作为"真生效"验收——正是 review_guide §3.3 要求的 BUILD 参照系核对。
- **真机回归覆盖申请点名路径**：mail list / Sync / thread open / topic pick 四条（用户确认），对应 issue_list §15 的崩溃触发面。

---

## 答申请 §4 请评审重点

1. **池尺寸 64K 是否合理 / 有无更优做法**：合理。`LV_MEM_SIZE` 是内部 SRAM 的静态 BSS 数组，64K 占 320KB SRAM 的 20%，落在 55.1% RAM 预算内，对观测峰值（~47.5K used）留 ~16K 余量。**不建议**改 `LV_MEM_CUSTOM=1` 接 PSRAM/`heap_caps`：ESP32-S3 PSRAM 经 QSPI/OPI 与 flash 共享总线，alloc/free 慢约 2-3x，且 LVGL 频繁小块分配会放大该延迟——可能恶化你们刚打完的"EPD 刷新阻塞主循环→音频泵断粮"时序仗（CHANGELOG 2026-09-11 ⑤）。墨水屏设备（慢刷、非动画）用内部 SRAM 静态池是正确取舍。`pp_dbg_pool()` 水位预警是防 tree 再生的对的手段（见 Nit-2 扩展建议）。
2. **观测点噪声是否可接受**：可接受（每次进 PenPal ~2 行 + 每次网络动作 1 行，串口低频）。但须先解决 Nit-1 的注释生命周期矛盾。
3. **lv_conf.h `-include` 坑表述/规则是否足够**：准确且足够。"必须 `-t clean` + 看 size RAM 涨幅核对"已写进 build 文档坑 7 与 commit message，规则闭环。
4. **诊断批整体弃置是否认可**：对本 commit 范围认可（`721e04a` 应聚焦池扩容）。但见上方决策项——须确认主线无 off-UI-thread SPIFFS 写；若有，那条改动应单独走正式评审而非随诊断批沉默丢弃。

---

## 验证说明（评审方环境）

- 评审方在开发机（Windows，repo 工作副本）独立核验：`git cat-file -t` 确认 `721e04a`/`f8d73f6`/`1269bf7` 均存在；`git show --stat`/`git show 721e04a` 读取真实 diff 与 commit message，逐字比对申请声明（矩阵 ①-⑦）。
- **未独立复跑** `pio run -e pda2`（本轮未触发构建）：RAM 50.1%→55.1% 与 ~17K 余量引自 commit message（作者 BUILD 证据），评审方标 `VERIFIED`（来源可信、与 +16384B 池增量自洽），但非本轮独立重跑——如需 G-发布 级证据，建议补一次 `pio -t clean` + `pio run -e pda2` 的 size 报告。
- **无设备**：真机稳定声明（矩阵 ⑥）引自 commit "user-confirmed 2026-08-29"，评审方无 COM5/COM6 设备复现，按 §3.4 尊重作者+用户 HW 证据，不降级为 CLAIM_ONLY（已有明确用户确认 + 操作序列）。
- 未对工作区作任何代码修改。

## 对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：是。本批仅 1 commit，`git show 721e04a` 全量读取（2 文件 +23/−1 全部过目），无未审文件。
2. **快照是否独立复跑**：部分。git 对象/diff/commit message 独立读取并逐字比对（已记命令）；`pio` size 报告未独立重跑（见验证说明，标来源 VERIFIED 而非本轮复现）。
3. **申请"最没把握"各处是否构造反例**：申请 §4 自陈的 4 个疑点（池尺寸/噪声/坑表述/弃置）逐条回应；并对"池扩容是否真解决而非掩盖"构造反例追问——确认水位 524B→~17K 是数量级改善（非边缘），且根因（pool 耗尽）由空 label 探针排除渲染逻辑后独立坐实，反例不成立。

## 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

> 附带（不阻断合入）：Nit-1 注释生命周期改齐；Nit-2 全局池水位审计登记 issue_list follow-up；决策项——确认主线无 off-UI-thread SPIFFS 写后关闭。
