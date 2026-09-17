# 评审结果：修复轮核销 + v1.0 版本号/缓存批次（095e41a..301c571，claude）

- **评审日期**：2026-09-17
- **申请文件**：[session-batch-review-request-095e41a..301c571.md](session-batch-review-request-095e41a..301c571.md)
- **评审提交**：`095e41a`、`22e3b87`、`110802a`、`301c571`（首末含两端，共 4 commit）
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）。
- **评审范围**：按 §0 原则 8"验证修复而非重复发现"——对 §1.1 核销表的每一行都
  重新去读了引用的 `file:line` 原文，尝试构造反例击穿"已修"声明，而不是转述申请
  的结论；301c571 的 v1.0 新功能按常规全量代码评审（无"已修"声明可核销，是新代码）。
- **评审结论**：**C 部分接受**。P1 与 P2-1/2/2-2/2-3/2-4 的修复**独立核实成立**；
  但 **P2-5 的核销声明与代码不符**——申请称"核实既有实现已防…无代码改动"，本轮
  反例击穿：`idle_sleep_timer_cb`（我在 `71c09e7` 评审里指出的具体函数）未受本轮
  任何一个 commit 触及，OTA 下载期间仍会无条件 push Sleep 屏。P2-6 申请如实自陈
  未闭合，属实。v1.0 新批次（版本号 + Whoami 缓存）代码质量良好，一处 Nit。
- **G-合入**：否（P2-5 核销声明失实，须先改正——见 Required action）。

---

## 复核方法

这轮申请的性质是"核销 + 新功能"混合：前半段（§1.1）是对我自己上一轮
（`71c09e7`）连同 GPT/Grok/Qwen 三方共 1×P1+6×P2 发现的处置报告，后半段（§1.2）
是独立新代码（v1.0 版本号体系 + Whoami 缓存）。核销部分我没有重新审视原始发现是否
成立（那一轮已经四方定论），而是逐条读了申请引用的 `file:line`，尝试用 §0 原则 8
的"突变对照"方式击穿每一条"已修"声明；新代码部分按常规做全量走查（结构体是否
POD、NVS blob 版本兼容、缓存失效路径、UI 布局改动）。

---

## Findings

### P2-5（既有发现，核销声明失实）：`idle_sleep_timer_cb` 未受本轮任何 commit 触及，OTA 下载期间仍会无条件 push Sleep 屏

- **位置**：`ui_deckpro.cpp:4977-4993`（`idle_sleep_timer_cb`）——本轮独立重新读取
  原文，函数体内**没有任何 `ota_busy()` 引用**，与我在 `71c09e7` 评审
  （`…-71c09e7-claude.md` P2-5）里指出的问题完全一样，一字未改。
- **申请的核销声明**（§1.1 表第 6 行）：「核实既有实现已防：`ota_busy()` 时自动
  休眠拒绝 push Sleep；低电另有 10 分钟安全阀（无代码改动）」——本轮 `git diff
  --name-only 2c86eca..301c571` 独立重跑确认 11 个改动文件里**不含
  `ui_deckpro.cpp`**（见 §0 归属表，申请自己也把它标为"不评"之外根本没列——
  `ui_deckpro.cpp` 甚至不在这 11 个文件里），所以"核实"这个动作要么没有真的去读
  `idle_sleep_timer_cb` 这个具体函数，要么把它和另一个函数搞混了。
- **反例（构造，非转述原发现）**：申请引用的"防护"实际指向的是
  `sleep_do_enter()`（`ui_deckpro.cpp:4815-4822`，本轮独立核对确认这处 `if
  (ota_busy()) { ...; return; }` 确实存在且未变）——这个函数只在**倒计时结束、
  真正要进深睡的那一刻**才会被调用，它拒绝的是"真的掉电进入 `esp_deep_sleep_
  start`"，不是我原来那条发现要说的问题。我原来的反例是：OTA 下载覆盖层显示
  "DO NOT POWER OFF"期间用户不会有任何按键/触摸（覆盖层无按钮），5 分钟到期后
  `idle_sleep_timer_cb` 仍会**无条件** `scr_mgr_push(SCREEN11_ID, false)`——这一步
  发生在 `sleep_do_enter()` 之前，与"是否真的进深睡"无关，是"UI 覆盖层被压栈"
  这个独立问题。`sleep_do_enter()` 的防护是真的，但它防的是另一件事；`idle_
  sleep_timer_cb` 这一层完全没有 `ota_busy()` 检查，反例路径与我上一轮写的
  一字不差地仍然成立。
- **影响**：与 `71c09e7` 评审同一条，严重度不变——`C/2/有声 → P2`（不升级：
  `esp_deep_sleep_start` 从未被调用，flash 写入不受影响，只是 UI 观感被打断；
  可达性档 2：需要下载耗时 > 5 分钟这个特定时序）。**新增的问题层面**：这不是
  "还没来得及修"，是核销表**把一个未改的函数当作已核实的防护**写了进去——按
  §9 自审【文档对齐】模式 B（"✅/⏸ 是否逐条有对应证据，而非把计划当作已做"），
  这条不成立，需要在下一轮申请里避免把"我记得应该有防护"当"我去读了这个具体
  函数"。
- **两轴**：`VERIFIED`（CODE：`idle_sleep_timer_cb` 原文 + `git diff --name-only`
  确认文件未被触碰）/ `CONTRADICTED`（申请 §1.1 第 6 行的核销声明）。
- **最小修复**：`idle_sleep_timer_cb` 顶部加
  ```c
  extern bool ota_busy(void);
  if (ota_busy()) { s_last_activity_ms = millis(); return; }
  ```
  与我上一轮给出的最小修复完全相同（一行判断 + 重新打时间戳，不放弃计时窗口，
  与现有 `audio.isRunning()` 分支写法一致）。修复后登记 `issue_list.md`，下批
  核销时同样需要真的去读这个函数一次，不能只读 `sleep_do_enter()`。

---

## 已核实成立的修复（独立复核，构造反例未能击穿）

### P1：`verifyRollbackLater()` override — 成立

`factory.ino` 新增
```c
extern "C" bool verifyRollbackLater() { return true; }
```
位于文件级作用域（`listDir()` 函数结束之后、`setup()` 之前），弱符号覆盖在链接期
可见，位置正确。与既有的 boot-WDT 窗口（`:673` `esp_task_wdt_init`）、自证点
（`loop()` 内 `esp_ota_mark_app_valid_cancel_rollback()`）三件套配齐——本轮独立
重读这三处确认没有互相矛盾或遗漏。issue_list §22 附带记录的真机验证（otadata
`state=VALID` 态下 override 为空操作、30s 零复位正常启动）诚实地标注了"这不等于
PENDING_VERIFY 真实回滚路径已测"，没有过度声称——符合 §3.4 ⏸ 纪律。**回滚矩阵
真机测试仍是遗留项**（申请 §3② 已自陈，TODO 已登记），本轮不重复要求，但也不能
因为"编译过 + 一次空操作验证"就当作层 2 已经端到端证明。

### P2-1：语音上下文 role/content — 成立

`vai_ctx_build` 改为每个存储的 turn 对拆成两条消息（`role="user"`/`role=
"assistant"` 字面量，`content` 取对应文本），与 `openai_api.h:14` 的字段契约
（`role` 只应是 `"user"`/`"assistant"`）一致。本轮独立核对了防越界写：`if (n + 2
> max_msgs) break` 在写入前检查，配合两处调用方 `ai_message_t ctx[VAI_CTX_MAX_
MSGS]` + `vai_ctx_build(ctx, VAI_CTX_MAX_MSGS)`（`:388-389`、`:480-481`），缓冲区
16 个元素、`max_msgs` 传 16，写入不会越界——修复本身没有引入新的溢出风险。

### P2-2：`s_ota_inflight` 递增时机 — 成立

`ota_check_async`/`ota_start_async` 均改为**先**判空**先**置 1，`xTaskCreate`
失败才回滚为 0，与 PenPal 先例（`ui_penpal.cpp:529-538`）逐行同构。我在
`71c09e7` 评审里指出的"非原子绝对赋值导致不可交换、可能永久卡死"这条机制性
风险，因为现在写者时序已经理顺（UI 线程单入口先写 1，worker 唯一出口写 0，
不再存在"worker 先写 0、UI 后写 1"的反向覆盖窗口），结构上已经不成立——`s_ota_
inflight` 仍是普通 `volatile int` 而非 `__atomic_*`，但既然只剩单一方向的写序列
依赖，这不再是阻断项（Nit 级：改成 `__atomic_store_n` 会更自文档化，比照 PenPal，
不强制）。

### P2-3：`xQueueOverwrite` 覆盖丢失 — 成立

`ota_send_result` 改为覆盖前先 `xQueuePeek` + `xQueueReceive` 取出旧指针并
`delete`，直接堵上了我上一轮反例里"两次 overwrite 之间 poll 从未跑过一次"的
丢失路径。逻辑读了一遍没发现新的双重释放风险（`xQueuePeek` 只读不取，真正
取出用 `xQueueReceive`，取出后立即 `delete`，随后才 `xQueueOverwrite` 写新值，
顺序正确）。

### P2-4：OTA TLS 继承 Trust 开关 — 成立

两个下载点（manifest + 固件）都直接 `secure.setCACertBundle(CA_BUNDLE_MOZILLA)`，
不再经过共享的 `http_apply_tls()`/`s_tls_mode`；明文 HTTP 收在编译期
`OTA_ALLOW_PLAINTEXT`（默认 0）后面。本轮独立核对两处调用点均已替换，没有遗漏
第三处（`rg -n "http_apply_tls" examples/pda2/ota_update.cpp` 本轮未见命中，
确认清干净了）。

### P2-6：契约表 — 申请如实自陈未闭合，属实

`rg -n "ota|whoami" docs/async_ipc_contract.md -i` 本轮独立重跑仍为 0 命中，与
申请 §1.1 最后一行的自我评价一致，不是虚假核销，登记 §3 遗留合理。

---

## v1.0 新批次（301c571）独立复核

- **版本号体系**：`fw_version.h` 定义 `FW_VERSION_MAJOR/MINOR`，`fw_version_
  string()`（`ui_deckpro_port.cpp`）用 `__DATE__` 生成 build 号，`static char
  v[28]` 缓冲区在单线程（loopTask）调用场景下没有重入风险（splash 在 `setup()`
  期间调一次，SF Version/Whoami FW label 之后在 UI 线程按需调用，不存在跨任务
  并发写这个静态缓冲区的路径）。`sscanf(__DATE__, "%3s %d %d", mon, &dd, &yy)`
  对 `mon[4]` 缓冲区大小匹配 `%3s` 格式，不溢出。
- **Whoami Me 页 NVS 缓存**：`pp_profile_t`（`penpal_api.h:198-204`）是纯 POD
  （5 个定长 `char[]` 字段，无 `std::string`/指针），`nvs.getBytes`/`putBytes`
  做整块字节拷贝对这个类型安全——不是 §5.2 那类"结构体含 std::string 却被当
  字节 blob 处理"的地雷。`getBytesLength("me_prof") == sizeof(p)` 正确处理了
  开发者自评①提到的"跨版本 blob 尺寸变化"场景（尺寸不符直接判定未命中，走
  自动重拉，不会把旧布局的字节硬拆进新结构体）。`s_cache_read` 门控"每 boot
  只读一次 NVS"，避免了每次进屏都打开 Preferences。
- **Cfg 保存后的重拉时机**：`wa_cfg_save_cb` 清缓存 + 置 `s_profile_valid=false`
  + 立即 `wa_me_render()`（渲染出"Profile not loaded / Press Refresh"占位文案，
  独立核对了 `wa_me_render()` 对 `!s_profile_valid` 的分支，占位文案合理，不会
  显示陈旧数据）——但**自动重拉只在下次 `wa_entry()`（重新进入 Whoami 屏）时
  触发**，若用户保存后不离开屏幕、只是把 Tab 从 Cfg 切回 Me（`wa_me_tab_cb` 只
  做可见性切换，不触发任何拉取逻辑），会先看到占位文案而不是自动刷新的资料，
  需要用户手动按 Refresh 或先 Back 再重进。这与申请 §1.3 自己的描述"Me 页
  **下次进屏**重新拉取"一致，不是未披露的行为，按 §7.3 "不报风格/完备性问题
  为缺陷"——**列为 Nit，不占编号**：如果想要"保存后立刻在 Me 标签页看到新数据"
  的体验，可以在 `wa_cfg_save_cb` 里对已配置好的 base/key 直接触发一次后台拉取
  （复用 `wa_entry()` 后半段的自动拉取逻辑），而不是仅仅依赖用户手动操作。
- **布局改动**（FW label 加入、Save/Test/status 下移）：本轮未真机验证像素级
  对齐，静态读值核对 `y=212`（FW label）→`232`（Save/Test）→`268`（status，
  高度收窄到 50px）互不重叠，与开发者自评③一致承认的"3 行文本贴边"风险
  一并列入 §3 遗留即可，不单独升级。

---

## 验证说明

- 本环境无 `pio`、无设备：BUILD/HW 证据一律沿用申请方给出的记录（编译 size、
  flash_verified.py 5/5 块校验 + 整段回读 MD5、25s 开机冒烟无 rst），标
  `PARTIALLY_VERIFIED`（申请方证据链完整、有具体串口/工具输出摘要，但本轮环境
  未独立重跑，不能记满分 `VERIFIED`）。
- 独立读取的仓库源码位置：`factory.ino`（P1 override 位置 + `setup()`/`loop()`
  首尾）、`ota_update.cpp` 全量 diff（P2-2/2-3/2-4）、`ui_voice_ai.cpp:330-395`/
  `:475-485`（P2-1 + 调用方缓冲核对）、`ui_deckpro.cpp:4815-4994`（P2-5 反例，
  两个函数都重读）、`ui_whoami.cpp` 全量 diff（v1.0 缓存批次）、
  `penpal_api.h:198-204`（`pp_profile_t` POD 确认）、`docs/async_ipc_contract.md`
  （P2-6 grep 复核）。
- `git diff --name-only 2c86eca..301c571` 本轮独立重跑，确认 11 个文件与申请
  头部自校一致，`ui_deckpro.cpp` 不在其中——这是 P2-5 finding 的直接证据来源。

---

## 对称三格（§7.2）

1. **全区间**：11 个文件均核对了归属（申请头部自校表本身已列全，本轮独立重跑
   `git diff --name-only`/`git rev-list --count` 复算一致，未发现遗漏文件）。
2. **独立复跑**：无 pio/无设备，SIM/CODE 级复核为主。对每条"已修"声明都重新
   读了具体行号原文，而不是信任申请的转述——P2-5 那条正是这样被击穿的。
3. **申请"最没把握"处反例**：① NVS blob 跨版本——本轮认可代码层面的防护
   （`getBytesLength` 校验）成立，未构造出新反例，未实测环境不做进一步要求；
   ② 空命名空间首跑——`Preferences::begin(ns, true)` 对不存在命名空间返回
   `false` 是 ESP-IDF NVS 文档化行为，代码推演成立，本轮未独立烧一台"干净"
   设备复测，遗留同申请自陈；③ Cfg 状态区 50px 贴边——纯显示风险，未强化。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 2 busy 释放：OTA `s_ota_inflight`/`s_ota_hw_lock` 时序已理顺（P2-2 核实
      成立）；`idle_sleep_timer_cb` 的 `ota_busy()` 门控**仍缺失**（P2-5）。
- [x] 3 worker `new` 结果 UI `delete`：P2-3 覆盖丢失路径已补 Peek+Receive+delete，
      核实成立。
- [x] 7 未见新增 `memset` 误用；`pp_profile_t` 走字节 blob 是安全的（纯 POD）。
- [x] 15 P2-5 finding 给了具体 `file:line` + 反例（`idle_sleep_timer_cb` 原文 +
      `git diff --name-only` 证明未改）。
- [x] 16 申请 HW 证据（flash_verified.py 校验、25s 冒烟）附了具体工具输出/时长，
      非空泛"测过了"，标 `PARTIALLY_VERIFIED`（未独立重跑）而非 `CLAIM_ONLY`。
- [x] 20 结论未与"四方一致认可 P1"这类票数对抗；P2-5 的判定完全基于本轮独立
      重读代码，不是重复上一轮结论。
- [x] 23（文档对齐模式 B）申请 §1.1 表的"✅ 已修"/"核实…已防"逐条核对是否有
      对应证据——5/6 行属实，1 行（P2-5）证据不成立，已单列 finding。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订（推倒批次）
- [x] **C. 部分接受**

**Required action**：`idle_sleep_timer_cb` 补 `ota_busy()` 门控（P2-5 最小修复，
一行判断）；下一轮核销表更正这一行，不要用 `sleep_do_enter()` 的防护代替对
`idle_sleep_timer_cb` 本身的核实。登记 `issue_list.md`，不要求推倒本批（P1 与
其余四项 P2 修复扎实，v1.0 新批次质量良好）。

**保留**：P1 override、P2-1/2-2/2-3/2-4 四项修复、v1.0 版本号体系、Whoami NVS
缓存（含 POD 安全性、跨版本尺寸校验）、`flash_verified.py` 刷机纪律、issue_list
§22 事故复盘。

**回退项**：无（P2-5 是一行修复，不影响本批其余改动，不建议整批或单 commit
回退）。
