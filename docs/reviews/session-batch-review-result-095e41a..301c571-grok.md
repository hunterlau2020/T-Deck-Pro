# 评审结果：修复轮核销 + v1.0 批次 `095e41a..301c571`（Grok）

- **评审日期**：2026-09-17
- **评审人**：Grok（Cursor）
- **申请文件**：[session-batch-review-request-095e41a..301c571.md](session-batch-review-request-095e41a..301c571.md)
- **评审对象**：`095e41a` `22e3b87` `110802a` `301c571`（父 `2c86eca`；11 文件 +415/−30；首末含两端）
- **工作树 HEAD**：`4a3e0a0`（`git diff 301c571 HEAD` 仅申请/台账文档，**本批代码 findings 仍存活**）
- **评审依据**：[`docs/review_guide.md`](../review_guide.md) **v1.3**。本对象是**代码**（修复轮 + 新 NVS 写路径），不是设计稿——A/B/C 用 §2.3 **代码**口径：A 不得挂 P0/P1；P2 应修进 `issue_list.md` 台账。§0 原则 8：已标「已修」的条目要**构造反例击穿修复**，不是重报原缺陷。
- **对照**：上轮 `71c09e7` Grok C（[`session-batch-review-result-71c09e7-grok.md`](session-batch-review-result-71c09e7-grok.md)；1×P1 + 6×P2）；OTA v6 实现合同 IC-1…IC-6。
- **评审结论**：**A 全量接受**。无 P0、无未闭合 P1。上轮 **P1 与 P2-1/2/3/4 修复成立**（SIM 击不穿）。**P2-5 核销声明被证伪**（仍应修，不挡 A）；**P2-6** 作者已自列遗留；v1.0 缓存引入 **1×新 P2**（在途 PROFILE 写回 NVS）。未独立 `pio run` → L2 BUILD 轴 `UNKNOWN`，不挡代码 A。
- **G-合入**：是（无 P0/P1）。**不得**把申请 §1.1 P2-5 行写成已闭合。**G-真机 / G-发布**：否（PENDING_VERIFY 回滚矩阵仍 ⏸；P2-5 下载中 Sleep 压栈未测）。

---

## 0. 区间文件归属（§0 原则 5 / §7.2①）

`git rev-list --count 2c86eca..301c571` = **4**；`git diff --name-only` = **11**，与申请归属表一致。

| 文件 | 归属 | 处置 |
|---|---|---|
| `examples/pda2/factory.ino` | 评 | P1 override + splash 版本 |
| `examples/pda2/ota_update.cpp` | 评 | P2-2/3/4 |
| `examples/pda2/ui_voice_ai.cpp` | 评 | P2-1 |
| `examples/pda2/ui_deckpro_port.cpp` | 评 | `fw_version_string` |
| `examples/pda2/ui_whoami.cpp` | 评 | NVS 缓存 + FW label |
| `examples/pda2/fw_version.h` | 评 | 新文件 |
| `scripts/flash_verified.py` | 不评（构建脚本档 §1.1） | BUILD-VERIFIED 声明未独立复跑 → `UNKNOWN` |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` / `docs/ota-update-design.md` | 不评（纯文档） | 台账/补注 |

**区间外邻居（强制核，因申请用它们做核销证据）**：`examples/pda2/ui_deckpro.cpp` `idle_sleep_timer_cb`（P2-5）；`docs/async_ipc_contract.md`（P2-6）；`examples/pda2/ota_update.h`（明文宏 / `OTA_APP_VERSION`）。

HEAD 之后落入、**不评**：本申请文件本身（`4a3e0a0`）。

**流程（不占缺陷）**：四 commit 未按「一模块一提交」拆（修复 / 刷机脚本 / 文档 / v1.0 混在同一申请）。申请已自校 4/11，按组给 findings，不因拆分退回。

---

## 1. 上轮核销（击穿实验，不是复读申请表）

| 上轮 | 申请处置 | 本轮裁定 | 证据 |
|---|---|---|---|
| **P1** `verifyRollbackLater` 缺失 | ✅ 已修 | **闭合** | `factory.ino:660` `extern "C" bool verifyRollbackLater() { return true; }`。全仓仅此一处。initArduino 弱符号覆盖成立（沿用上轮对 `esp32-hal-misc.c` 的框架事实）。**PENDING_VERIFY 真机回滚仍 ⏸**——闭合的是「启动路径上函数不存在」，不是 §8 矩阵。 |
| **P2-1** 语音 role=用户原文 | ✅ 已修 | **闭合** | `ui_voice_ai.cpp:360-368` 每轮写 `"user"` / `"assistant"` 两条；`n+2>max_msgs` 守卫。原反例（第二轮 JSON role=整句用户话）击不穿。HW 多轮 ⏸。 |
| **P2-2** inflight 创建后置位 | ✅ 已修 | **闭合** | `ota_check_async` `:565-570`、`ota_start_async` `:595-601`：create **前** `s_ota_inflight=1`，失败归零。快失败 worker 在另一核先跑完 `ota_task_exit` 再把计数打回 0 之后，UI 不会再写 1。未改 `__atomic_*`（IC-1 原文）；本板对齐 32-bit store 对 0/1 足够，**不重开**。 |
| **P2-3** Overwrite 丢旧指针 | ✅ 已修 | **闭合** | `ota_send_result` `:277-284`：Peek+Receive 后 `delete old` 再 Overwrite。原反例（第二 worker Overwrite → 第一块永远无 delete）击不穿。Peek 与 UI `ota_result_poll` 并发时 Receive 失败则不 delete（所有权已到 consumer）——无双重释放。Peek 可省略，见 Nit。 |
| **P2-4** `http_apply_tls` 继承 Trust | ✅ 已修 | **闭合**（安全目标） | 两处 `setCACertBundle(CA_BUNDLE_MOZILLA)`，`http_apply_tls` 仅出现在注释。Trust ON 不再 `setInsecure()`。残留：NTP 仍按 `http_get_tls_mode()!=INSECURE` 才 `http_ensure_time`（`:312-317`）——Trust ON + 未对时会 **CA 校验失败**（失败闭合，非 MITM）。见 Nit。明文双宏见 P3。 |
| **P2-5** idle 5min 仍 push Sleep | 核实既有实现已防 | **未闭合 / 核销 CONTRADICTED** | 见下方 **P2-5**。`sleep_do_enter` 拒深睡 ≠ `idle_sleep_timer_cb` 拒 push。 |
| **P2-6** 契约未登记 | ❌ 未闭合 | **未闭合**（作者诚实） | `grep ota\\|whoami docs/async_ipc_contract.md` = 0。§4.3 `NO_CONTRACT` 仍在。 |

---

## Findings

### P2-5（上轮未闭合）：`idle_sleep_timer_cb` 仍不看 `ota_busy()`——申请「已防 push Sleep」被代码证伪

- **位置**：`ui_deckpro.cpp:4977-4992`（本批**未改**；申请 §1.1 引「既有实现」作核销证据）。对照 `sleep_do_enter` `:4816-4822`（`ota_busy()` 才 return）。
- **证据 / 反例**（与上轮同一条，用来击穿「已核实」句，不是新架构）：
  1. idle 回调路径：活动超时 → 只拦 `audio.isRunning()` → `scr_mgr_push(SCREEN11_ID)`。函数体内 **零次** `ota_busy()`。
  2. 拒睡点只在 `sleep_do_enter`（Sleep 倒计时结束、真深睡前）。
  3. 反例：OTA 下载覆盖层「DO NOT POWER OFF」→ 无按键/触摸打点 → 5 分钟 push Sleep → 倒计时结束因 busy 返回 → 设备不深睡，但 Sleep 屏压进栈；idle 窗口重置后再过 5 分钟可再 push。下载继续，OTA 覆盖层/Check 按钮被盖住。
- **申请错在哪**：把「不会 `esp_deep_sleep_start`」写成「拒绝 push Sleep」。`095e41a` 提交说明同样把二者等同。层 2 电源轨安全仍在；**UI 打断**从未修。
- **影响**：`C/2/有声 → P2`（须 OTA 在飞 + 5 分钟无操作）。不升 P1：写 flash 的电源轨未被拉断。回升：G-发布前仍未改且现场长下载。
- **两轴**：`VERIFIED`（CODE）/ `VIOLATES`（上轮最小修复未落地；申请 §1.1 P2-5 行 **CONTRADICTED**）。
- **最小修复**：`idle_sleep_timer_cb` 在 `ota_busy()`（或已在 SCREEN11）时直接 return，可重计时，**勿 push**。SIM：busy=1 时走完回调无 `scr_mgr_push`。HW：下载满 5 分钟覆盖层仍在。

### P2-6（上轮未闭合）：OTA / Whoami 仍不在 `async_ipc_contract.md`

- **位置**：契约表仍只有 WiFi Test / Time Sync / AI Test / AI Chat。
- **影响**：`B/2 → P2`（§4.3 `NO_CONTRACT`：新型任务须同轮补契约或显式例外）。作者 §3.1 已自列下批——**接受为遗留**，不挡 A。
- **最小修复**：表加 SCREEN2_2 / Whoami 行：指针队列、drain 点（`ota_result_poll` / `whoami_keyboard_poll`）、inflight、Whoami 队列永删、OTA overwrite 前 delete、Whoami 结果须带 gen（见下条）。

### P2-7（本批新，v1.0 缓存）：Cfg 保存清缓存不闭合——在途 PROFILE 会把旧用户写回 NVS

- **位置**：`ui_whoami.cpp:494-502`（`wa_cfg_save_cb` 清 NVS + RAM）× `:366-370`（`wa_consume` 成功即 `wa_cache_save`）× `:136`/` :877`（`s_wa_gen` 自增于 entry，consume **从不读**，destroy 里 `(void)s_wa_gen`）。
- **证据 / 反例**：
  1. `wa_msg_t`（`:126`）无 `gen` 字段。上轮 Whoami 已记「consume 不看 gen」为 Nit——当时只污染 RAM，重启会再拉。
  2. 本批 `wa_cache_save` 把同一条成功路径落到 `"whoami"`/`me_prof`。迟到结果 **跨重启存活**。
  3. 反例：Me 页 Refresh（worker 持 key A 快照）→ Cfg 改 key 为 B 并 Save → `wa_cache_clear` + `s_profile_valid=false` + 占位渲染 → **不** `s_wa_gen++`、**不**取消 `s_wa_task` → A 的结果到达 → `wa_cache_save` 把用户 A 写回 NVS，RAM 再标 valid。配置命名空间已是 B。重启：`wa_entry` 缓存命中，状态行 `cached`，**不联网**，Me 页仍是 A。
  4. 同族：Save 只在 `server_ok && prov_ok` 清缓存（`:494`）；仅 server 成功、provider 失败时 key 已换、缓存仍在（`:503-504`）。
- **影响**：`D/2/静默 → P2`（须「在途 fetch + 随后 Save」；表面 cached 成功）。不升 P1：非变砖/非 key 泄漏，Refresh 可纠正。回升 P1：若产品把 Me 页身份当承重（错用户档案当「我」）。
- **两轴**：`VERIFIED`（CODE/SIM）/ `VIOLATES`（契约 §2.2/§2.9 迟到结果须 gen 丢弃；申请自评「Cfg 保存清缓存覆盖换 key」**对该反例不成立**）。
- **最小修复**：① 结果带 `gen`，consume 不匹配则 `delete`、**禁止** `wa_cache_save`；② Save（key/base 变更）时 `s_wa_gen++` 并视在途为 stale；③ server 已保存即清缓存，不要等 provider 双成功。SIM：上述键序后 NVS `me_prof` 为空或为 B。HW：§2 真机清单加第⑦项。

---

## 实现合同（v6 IC → 本批）

| ID | 目标 | 本批 | G-合入 |
|---|---|---|---|
| IC-1 inflight 时序 | create 前 ++ | **过**（P2-2 闭合；非 `__atomic` 但不重开） | 过 |
| IC-2 overwrite 旧指针 | overwrite 前 delete | **过**（P2-3 闭合） | 过 |
| IC-3 确认→Update | 无 UAF | **过**（上轮已过，本批未改路径） | 过 |
| IC-4 poll 接线 | factory loop 无条件 drain | **过**（`factory.ino:901-902`） | 过 |
| IC-5 `drv.begin` while(1) | 失败继续 | **未过**：`factory.ino:774-776` 仍死循环。本批碰了 `factory.ino` 但未触该路径。可达性 3 | 待（P3 台账，不挡本批 A） |
| IC-6 喂狗 / T | 校准 | 未新证据；T=60s / 本板 10s busy 仍上轮结论 | 待（绑 G-真机） |

P1 override 是层 2 的**必要前提**，已落地；窗口是否真回滚仍待 PENDING_VERIFY 矩阵（申请已诚实）。

---

## 已通过项（独立走查）

- **P1 接线**：override 与 loop 自证（`:888-896` `mark_valid` + 关 WDT）并存；注释写明去掉任一侧 = 必回滚或永不回滚。`095e41a` 说明里的「删掉重复 `ota_self_validate_poll`」**本 diff 未见**（该符号全仓 0 hits）——无回归，只是提交说明多写。
- **P2-1**：`vai_ctx_add_pair` 仍存 pair；build 侧 2× 展开。调用方 `vai_ctx_build(ctx, VAI_CTX_MAX_MSGS)`（16）+ `n+2` 守卫 → 最多 8 轮进 API，不会写穿数组。
- **P2-2/3/4**：见核销表。`ota_url_scheme_ok` 默认拒 `http://`。
- **版本串**：`fw_version_string` 解析 `__DATE__`（空格填充日 `%d` 可吃）；`v[28]` 对 `v1.0 build yyyy-mm-dd` 足够。三处显示：splash / SF Version / Whoami Cfg。静态缓冲、仅 UI 线程。
- **缓存 happy path**：`pp_profile_t` 为 POD `char[]`（`penpal_api.h:199-205`，228B），`putBytes` 不是把 `std::string` 指针落盘。`getBytesLength != sizeof` → 未命中。`s_cache_read` 每 boot 一次；命中不联网。worker 不碰 NVS（作者自评成立）。`wa_me_render` NULL 守卫仍在。
- **脚本/文档**：`flash_verified.py` / issue_list §22 / 串行单机刷写——按申请不评代码行为；§22 与「变砖=刷写不完整」叙事自洽，本环境未复跑脚本。

---

## Nit / 疑问（不占缺陷编号，≤3 + 疑问）

1. **双明文宏**：`ota_update.h:38` / `ota_url_scheme_ok` 用 `OTA_ALLOW_PLAIN_HTTP`；`ota_update.cpp:11-12,330,439` 用 `OTA_ALLOW_PLAINTEXT`（默认 0）。只开其中一个，实验室 `http://` 仍失败。生产双关，不升 P2。`TODO.md` 回滚矩阵写要编 `OTA_ALLOW_PLAIN_HTTP`——按现状还不够。
2. **双版本号**：OTA 屏 `Current:` 走 `OTA_APP_VERSION` `"v2.6-260915"`（`ota_update.h:33`）；splash/Whoami/系统信息走 `v1.0 build …`。301c571 引入第二套未收口第一套。
3. **`ota_update.h:8-11` 仍写 inflight「create 成功后再递增」**——与 095e41a 代码相反，注释滞后。

疑问：`http_ensure_time` 仍绕过 INSECURE（`:312-317`）。P2-4 改 CA 后这条变成「Trust ON 时 OTA 可能因 1970 对时失败」。失败闭合，不升缺陷。建议 https 路径**总是**对时。

`wa_cache_load` 后未把各 `char[]` 末字节置 0；仅当 NVS 整段无 NUL 才读穿。观察。

---

## 验证说明

- 静态走查 `2c86eca..301c571` 六份实现文件 + 核销用的区间外邻居。
- 无 pio、无设备。BUILD/HW = `UNKNOWN`。申请编译 size / COM5 烧录 / 25s 冒烟 → `CLAIM_ONLY`（无串口原文可贴）。
- 未复跑 `flash_verified.py`、未打开 IDF `initArduino` 源码（P1 沿用上轮框架事实 + 本批函数已存在）。

---

## 对称三格（§7.2）

1. **全区间**：11 文件均归属；不只 §1.3 靶向。额外看到：P2-5 核销证伪（`ui_deckpro.cpp` 未改）、Whoami 在途×NVS（申请自评最有把握被击穿）、`OTA_ALLOW_PLAIN_HTTP` vs `OTA_ALLOW_PLAINTEXT`、`OTA_APP_VERSION` 未随 v1.0 收敛。
2. **独立复跑**：未 `pio run`；未真机。SIM：P1 符号存在；P2-1 JSON 两条 role；P2-2 create 前 claim；P2-3 Peek+Receive；P2-4 无 `http_apply_tls` 调用；P2-5 idle 无 `ota_busy`；P2-7 Save 期间迟到 PROFILE 写 NVS。
3. **最没把握处反例**：
   - ① NVS blob 跨版本：`sizeof` 守卫存在；同尺寸改布局才会静默错读——未来固件问题，本批不升。真风险是 **P2-7**（同版本、错用户），不是 sizeof。
   - ② 空命名空间：`nvs.begin(..., true)` 失败 → 自动拉。SIM 成立；干净机 HW `CLAIM_ONLY`。
   - ③ Cfg 状态区 50px：显示裁剪，作者已知。Nit，不升。未覆盖的交点 = Save×在途 fetch（P2-7）。

---

## 声明矩阵（申请强声明）

| 声明 | 状态 |
|---|---|
| P1 override 已进固件 | `VERIFIED`（CODE）。PENDING_VERIFY 真回滚 `CLAIM_ONLY` |
| 语音第二轮起 role=user/assistant | `VERIFIED`（CODE）；HW `CLAIM_ONLY` |
| inflight create 前 claim | `VERIFIED` |
| overwrite 前回收旧指针 | `VERIFIED` |
| OTA 不继承 Trust / 专用 CA | `VERIFIED`（安全目标） |
| `ota_busy()` 时自动休眠拒绝 **push Sleep** | **CONTRADICTED**（P2-5） |
| 契约 OTA/Whoami 行本轮未补 | `VERIFIED`（作者自列） |
| Cfg 保存清缓存覆盖「换 key=换用户」 | **CONTRADICTED**（P2-7 在途路径） |
| 缓存命中不联网 | `VERIFIED`（CODE：`wa_entry` 在 `s_profile_valid` 时不 `wa_start`） |
| 编译 SUCCESS + size | `CLAIM_ONLY`（本环境未复跑） |
| COM5 flash_verified 5/5 + 开机 25s 无 rst | `CLAIM_ONLY` |

---

## §9 自审清单【C】/【ALL】

- [x] 1 未发现 worker 调 LVGL（OTA/Whoami 仍经队列/poll）
- [x] 2 Whoami consume 仍不看 gen（P2-7 放大为 NVS）；OTA 用 gen + inflight
- [x] 3 overwrite 路径现 delete；Whoami 成功路径多一次 NVS 写
- [x] 4 队列先建后任务
- [x] 5 OTA/Whoami 任务持快照拷贝（故 Save 改 NVS 挡不住在途 key A）
- [x] 6 OTA stale gen delete；Whoami 离屏靠 NULL 控件，**缓存不靠 gen**
- [x] 7 `pp_profile_t` POD，无 memset string
- [x] 8 未见 UI 线程 `= T()` 肥聚合
- [x] 9 无新池观测点（Nit，不升）
- [x] 10 未改 key 三缓冲
- [x] 11 本批无新秘密；私钥仍 gitignore
- [x] 12 单机烧录 v1.0，不得外推双机；另一台留 `71c09e7`
- [x] 13 P2-5 仍在；播放中 idle 重计时仍成立
- [x] 14 whoami NVS 单槽 `putBytes`，非双槽；掉电窗口可接受
- [x] 15 每条 P2 有 file:line + 反例；无 P0/P1
- [x] 16 申请真机无串口原文 → `CLAIM_ONLY`；无 pio → BUILD `UNKNOWN`
- [x] 17 参照系 `2c86eca..301c571`；HEAD 仅文档已声明
- [x] 18 `verifyRollbackLater` / `fw_version_string` / `wa_cache_*` 均有生产调用方
- [x] 19 反例库：pending 回滚（函数在、矩阵 ⏸）、离页结果（P2-7）、idle×OTA（P2-5）
- [x] 20 核销以代码为准，不采信「既有实现已防」票；无 P1 不给 C（§2.3 / 原则 11 的代码镜像：不把应修 P2 当成退回）

【D】21–27：代码评审大部分 `NOT_APPLICABLE`。申请 ✅/⏸ 与证据：P2-5 的 ✅ **CONTRADICTED**（条 23）。

---

## 审批意见

- [x] **A. 全量接受**（含 §3 遗留按期处置；**不含**把 P2-5 当作已核销）
- [ ] B. 退回修订
- [ ] C. 部分接受

**当批必修**：无（无 P0/P1）。

**应修（台账 `issue_list.md`，绑门禁，下批即可）**：

- P2-5 idle 在 `ota_busy()` 时不 `scr_mgr_push(Sleep)`（绑 G-真机 / 长下载）
- P2-6 契约表补 OTA + Whoami（绑下批文档；Whoami 行须写 gen）
- P2-7 Save/在途 PROFILE：consume 看 gen；Save 递增 gen；server 已写即清缓存（绑 Whoami 缓存真机⑦）

**保留**：P1 override；P2-1/2/3/4 修复；v1.0 版本号与「命中缓存不联网」主路径；IC-1/2/3/4。

**回退项**：无。

**不得宣称**：层 2 PENDING_VERIFY 已真机证明；OTA 下载期间自动休眠 UI 已隔离；Cfg 保存后缓存与当前 key 在并发下一致。
