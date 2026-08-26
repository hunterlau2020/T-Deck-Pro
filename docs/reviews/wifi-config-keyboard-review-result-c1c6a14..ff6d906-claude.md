# 评审结果：WiFi Config 5 记忆槽 + PenPal 共享 AI Config provider（Claude · 第二轮独立评审）

- **评审人**：Claude（独立评审，第二轮；第一轮结果见 `2026-08-25-review-result-c1c6a14..ff6d906-claude.md`，本文件不覆盖之）
- **评审日期**：2026-08-26
- **对应申请**：[wifi-config-keyboard-review-request-c1c6a14..ff6d906.md](wifi-config-keyboard-review-request-c1c6a14..ff6d906.md)
- **评审范围**：`c1c6a14`（WiFi 槽位 + AI cfg 修复 + HTTP 健壮性）、`136069a`（provider 共享）、`ff6d906`（CLAUDE.md docs-only）
- **同范围既有结果**：claude（C）/ codex（C）/ gemini（A）/ opencode（C），及修复批次申请 [`3d2a23d..9eeaa5a`](wifi-config-keyboard-review-request-3d2a23d..9eeaa5a.md)（HEAD = `f12b209`）
- **评审方法**：本环境无 git CLI。改用直接解码 `.git/objects`（zlib → commit/tree/blob）+ 工作区文件做 git blob SHA1 比对，**逐字节重建了 `ff6d906` 提交时点的 5 个关键源文件状态**（`ui_deckpro.cpp`=fe7c0ba0、`openai_api.cpp`=0459fcc7、`ui_ai_cfg.cpp`=4bc4d2d9、`http_utils.cpp`=5ef5f727、`ui_penpal.cpp`=782210a5），所有针对原提交的 finding 均在该重建状态上核实；修复批次另按 `3d2a23d`/`9b1af11`/`9eeaa5a` 各提交 blob 逐一比对确认。评审期间工作区正在被修复批次改写（文件时间戳 20:49-21:08），两代状态已分别锁定核对，无混淆。

## 结论：**C 部分接受**

申请范围内的 4 项 P 级缺陷（槽位切换丢草稿 / provider 状态行旧值 / 两步保存混合状态 / 中文文案 tofu）均真实存在，但已全部在后续批次 `3d2a23d..9eeaa5a` 修复（该批次自带申请、待批 + 真机回归 ⏸）。本申请范围内**未发现四方既有结果之外的新阻塞项**；原范围内无缺陷的其余改动（5 槽模型、集中式注册表、HTTP 健壮性、Fix/Polish/Tips 参数传递）独立复核通过。

---

## Findings（针对 `c1c6a14..ff6d906` 原提交；证据均为 `ff6d906` 重建状态）

### P1：切换 WiFi 记忆槽静默丢弃未保存的 SSID/Pass 草稿 —— `c1c6a14`

- **位置**：`ui_deckpro.cpp` `wifi_cfg_set_slot()`（重建状态 2015-2030 行）。
- **事实**：`wifi_cfg_sync_draft()` 只把**当前聚焦**框同步进 `wifi_ssid`/`wifi_pass`，两行后 `wifi_slot_load()` 用新槽数据覆盖同一对缓冲——同步结果从未落盘，等于死代码；另一（未聚焦）框的内容连缓冲都没进。`wifi_slot_save()` 仅被 Connect 成功路径与 Save 按钮调用，切槽（音量键 `\v` / 触摸 `<`/`>`）不在其列。
- **影响**：槽 2 输入一半误触音量键 → 内容无声丢失、不可恢复。单槽时代无"切换"概念，为本轮**新引入**行为。与第一轮 Claude P1 结论一致，独立复核成立。
- **状态**：已由 `3d2a23d` 修复（切换前读两个 textarea 直接 `wifi_slot_save()` 回旧槽，"切换 = 自动保存"语义写入设计文档 §3.5）。

### P2：PenPal Cfg provider 状态行显示"已保存值"而非当前下拉选择 —— `136069a`

- **位置**：`ui_penpal.cpp` `pp_cfg_status_text()`（重建状态 977-992 行）。
- **事实**：函数内重新 `penpal_load_ai_provider()` 读 NVS，与 `s_cfg_provider_idx` 无关；选新 provider 不按 Save，状态行不跟随，与 Cfg 页"状态行即时反映当前编辑"的既有契约不一致。同路径 `ai_provider_enum(ai_provider_find(name), &p)` 返回值未检查（当前控制流下 find 必然 ≥0，不可实际触发，但脆弱——Gemini M1 同）。与 Codex/第一轮 Claude 结论一致。
- **状态**：已由 `9b1af11` 修复（按 `s_cfg_provider_idx` 即时预览 + enum 返回值检查 + custom 显式分支）。

### P2：Server 配置与 AI Provider 两步 NVS 写，部分失败报笼统错误 —— `136069a`

- **位置**：`ui_penpal.cpp` `pp_cfg_save_cb()`（重建状态 1035-1054 行）。
- **事实**：两次独立写非原子；第二步失败时 server 已落盘、provider 仍为旧值，界面只报 `save failed (NVS)`，混合状态被伪装成一次性失败。与 Codex/第一轮 Claude 结论一致。
- **状态**：已由 `9b1af11` 修复（分区报告 `server saved; AI provider save failed`）。

### P2-1：中文错误文案在 montserrat_14 下渲染为方块（tofu）—— `c1c6a14`

- **位置**：`openai_api.cpp` `out = "读取响应超时";`（空响应体路径）与 `ui_ai_cfg.cpp` `ai_msgbox_set_text("等待返回超时");`（Test 倒计时耗尽路径）。
- **事实**：两处文案最终都进 `ai_msgbox`（正文字体 `lv_font_montserrat_14`，无 CJK 字形）。申请 §2 只实测了 Test **成功**路径，超时路径一旦上机必现方块。与 opencode P2-1 一致。
- **状态**：已由 `9eeaa5a` 修复（`Empty response (timeout?)` / `Request timeout\n(check network)`，附防再犯注释）。

### Low 组（全部与 opencode 结果一致，逐条独立复核成立）

1. **UTF-8 不安全截断**（`c1c6a14`）：`openai_api.cpp` `out.resize(200)` 按字节硬切可断码点；`ui_ai_cfg.cpp` `fail_buf[192]` 二次截断同款。→ `9eeaa5a` 已修（码点边界回退 + `fail_buf[240]`）。
2. **`\b` 在 provider 焦点下被吞**（`136069a`，重建状态 1140-1157 行：provider 分支只处理 `+/-` 后直接 `return`）：键盘退出 Cfg 需先 `\t` 两次回输入框。→ `9b1af11` 已修（`\b` 返回 HOME）。
3. **`[HTTP] status= ct= ce=` 日志恒为空**（`c1c6a14`）：`http.header()` 只返回经 `collectHeaders()` 登记的键——已对照本机框架源码（`.platformio/.../HTTPClient`）核实：`_headerKeysCount` 默认 0，未登记即返回空串；同库 `penpal_api.cpp:pp_request` 本就有正确的 `collectHeaders` 用法，形成对照。→ `9eeaa5a` 已修（登记 Content-Type/Content-Encoding）。
4. **结果弹窗关闭后残留 FIFO 键注入**（`c1c6a14`）：弹窗分支每 poll 只消费一个字符，排队在其后的键会在关闭后流入正常处理器。**机制补充**：`wifi_cfg_connect()` 的 `keypad_clear_chars()` 位于 15s 连接循环之后、`wifi_time_sync()`（最长 ~8s 阻塞）**之前**——NTP 同步期间按下的键仍会积压并经单键关弹窗泄漏（极端情况下泄漏的 `Enter` 在 Pass 字段可再次触发连接）。`3d2a23d` 把清理点放在**弹窗关闭时**，是正确的收口位置，两个积压窗口一并覆盖。
5. **文档低估 `\v` 适用范围**（申请 §1.1 / 设计 §3.3 语境）：`\v` 分支位于扫描判断之前，任意模式（含扫描进行中）都切槽，并经 `wifi_scan_gen++` 作废在途扫描（行为安全）。→ 维持 opencode 的"文档登记"处理即可。

---

## 本轮新增（四方既有结果未覆盖；均为 Low/备注，不阻塞）

- **N1（文档，`136069a`）**：`openai_api.h` 中 `ai_provider_get()` 的注释 "Resolution order (later wins): 1. Registry defaults → 2. active slot → 3. env.cfg → 4. AI_KEY_DEFAULT_DEV" 与实现不符：当活动槽 base 与注册表匹配时，**槽内 key 优先且 env.cfg 根本不被查询**（不存在 "later wins" 覆盖）。`design-penpal-ai-provider-link.md` §3.1 的表述才是正确的。建议把头注释改成与 §3.1 一致的分支描述，防止后续维护者按注释"修正"实现。
- **N2（文档，`c1c6a14`）**：`ui_ai_cfg.cpp` 文件头注释仍写 "Save validates all fields and **requires a successful Test** since the last edit"、"the status line states why **Save is blocked**"，`ai_cfg_create()` 尾部注释仍写 `"Run Test to enable Save"`——与本提交**用户明示的 Save/Test 解耦**直接矛盾（实际状态提示已是 "Save / Test"）。注释应与解耦决策同步，避免下一位维护者照注释恢复门禁（opencode 已在"登记备注"要求未来不得恢复该门禁，注释是该要求的落点）。
- **备注 1**：修复后的 `wifi_cfg_set_slot()` 每次切槽都无条件写一次 NVS（即使内容未变）——磨损可忽略，语义所需，不需处理；仅登记。
- **备注 2**：**退出屏幕**（Back/`\b` 空框退出）同样不保存草稿。这与全应用"离开屏 = 放弃未保存编辑"（AI Config 同）惯例一致，且设计 §3.5 新语义只承诺"切换 = 保存"；但因 P1 修复把槽位变成"持久草稿"心智，建议确认该路径是有意识保留的惯例而非遗漏。

---

## 独立复核通过项（`c1c6a14..ff6d906` 重建状态）

1. **WiFi 5 槽**：键名生成 ≤11B 安全；`wifi_slot_load` 越界归 0；`migrate_legacy` 幂等（无 legacy 也落哨兵）；`factory.ino` 开机读 `active_slot`、空槽跳过、与 `ui_deckpro.h` 声明链接一致（`.ino` 局部 `extern` 重复但无 ODR 风险）；Connect 成功才 `save`+`set_active` 次序正确；结果弹窗挂 `lv_layer_top()` 且 `exit4_1` 关闭（round 25 模式）。
2. **HTTP 健壮性**：`http_post` 委托 `http_post_with_headers` 旧调用点零改动；HTTP/1.0 + `Accept-Encoding: identity` + `Connection: close` 只影响 POST 消费者（AI Chat/Test；weather 走 GET、PenPal 自带传输）；`openai_chat_impl` 所有早退路径均落 `out`；`reasoning.exclude` 收敛到 OpenRouter；base 已带 `/chat/completions` 的旧 NVS 值有归一化分支。
3. **Provider 单源化**：`s_providers[]` 迁至 `openai_api.cpp`（附 `env_key` 列），AI Config 下拉动态构建、`custom` 清空三框、Save 时回写 `key.<name>`（custom 跳过，无 dead store）；`ai_provider_get` 回退链（活动槽 base 匹配 → `key.<name>` NVS → env.cfg → openrouter 编译期兜底 → 空）自洽，无跨 provider 串 key；API key 不上串口（只打 provider 名）。
4. **异步契约**：`pp_start` 在 UI 线程解析 provider（worker 不碰 Preferences）；`rq->ai_provider/ai_model` 为快照 `std::string`（`new pp_task_req_t(*tmpl)` 拷贝），随 `delete rq` 释放；inflight 原子计数先于 `xTaskCreate`；`penpal_correction/polish/tips` 增量扩参，值初始化规则保持（无 memset 回潮）。
5. **URL 构造**：`pp_llm_path` 全 `std::string` 拼接；provider 为空不附加参数（旧服务端兼容）；不编码为设计 §6.3 既有登记限制。
6. **`ff6d906`**：CLAUDE.md docs-only，无代码影响。
7. 申请 §2 的编译数字由 opencode 本机复现（本环境未装 pio，未复跑）。

---

## 修复批次交叉核对（`3d2a23d..9eeaa5a`，HEAD `f12b209`）

| 修复项 | 提交 | 核对方式 | 状态 |
|---|---|---|---|
| 切槽自动保存两框草稿 | `3d2a23d` | 工作区 blob 哈希 = `05f69f88` + 直读 | ✅ |
| 弹窗关闭 `keypad_clear_chars()` | `3d2a23d` | 同上 | ✅ |
| provider 预览跟随下拉 + enum 检查 | `9b1af11` | blob 哈希 = `c8c8a0d0` + 直读 | ✅ |
| 两步保存分区报告 | `9b1af11` | 同上 | ✅ |
| provider 焦点 `\b` → HOME | `9b1af11` | 同上 | ✅ |
| 英文超时文案 ×2 | `9eeaa5a` | blob 哈希 = `567adb0f`/`843bb0ff` + 直读 | ✅ |
| UTF-8 截断回退 + `fail_buf[240]` | `9eeaa5a` | 同上 | ✅ |
| `collectHeaders` ct/ce | `9eeaa5a` | blob 哈希 = `935a558e` + 直读 | ✅ |
| 设计文档 §3.5 / §7 语义订正 | `f12b209` | 直读两份设计文档 | ✅ |

## 仓库卫生观察（非阻塞，供下一轮处置）

1. **工作区存在未提交改动**：`ui_deckpro.cpp` 在 `3d2a23d` 之上另有一处菜单布局对调（PenPal/Sleep/Shutdown 位置，注释日期 2026-08-26 21:08），不属于本申请也不属于修复批次范围——应单独提交并走申请，或明确暂留。
2. **行尾漂移**：5 个源文件在磁盘上为 CRLF，而仓库 blob 为 LF（无 `core.autocrlf`、无 `.gitattributes`）——`git status` 应显示这 5 个文件为已修改。疑似编辑工具回写所致；建议恢复 LF 或补 `.gitattributes` 约定，避免后续 diff 噪音。
3. 本评审因环境无 git CLI，采用对象解码重建提交状态；上述 1/2 两点建议在可用 `git status` 的环境复核一次。

## 处置建议

| 项 | 处置 |
|---|---|
| 原范围 4×P 级 | 已由 `3d2a23d..9eeaa5a` 修复；闭环依赖该批次申请获批 + 真机回归（槽位自动保存 / provider 预览与分区保存 / 英文超时文案 / PenPal provider 参数上机） |
| N1/N2（文档） | 随下一轮顺手修订即可，不阻塞 |
| 仓库卫生 1/2 | 提交菜单对调（走申请）+ 恢复 LF；不阻塞本轮 |

## 审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订**
- [x] **C. 部分接受** —— 保留项 = 4×P 级缺陷（均已转入修复批次 `3d2a23d..9eeaa5a` 处置，待其获批与真机回归后整体闭环）；其余全量接受

**评审人**：Claude（独立评审，第二轮）
**评审日期**：2026-08-26
