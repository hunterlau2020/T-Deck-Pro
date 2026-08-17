# 第 21/22 轮第四次整改评审结果（Codex）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..3cdff38.md](wifi-config-keyboard-review-request-844a907..3cdff38.md)
- **关联代码范围**：`844a907^..3cdff38`（申请人登记 23 commit；实际 git 范围含 4 个 doc-only 扩展 commit）
- **本次重点整改范围**：`867435e` + `3cdff38`（真机回归修复 + Copilot 复审 844a907..8d273cd 的剩余 8 项）
- **评审结论**：**C 部分接受**（22 个 non-Key commit 接受；P0 已合并前置满足，P1/P2 真机回归 ⏸ 项必须在本批合并到 master 前补登）

---

## 1. Findings

### 1.1 双槽原子保存与可执行测试配套到位 — 11/11 PASS 已实测

- **严重性**：✅ 通过
- **位置**：`844a907` + `8d273cd` + `538e6d0` + `tests/test_nvs_atomic_save.md` + `scripts/test_nvs_atomic_save.py`
- **验证**：
  - 本评审 host 实跑 `python scripts/test_nvs_atomic_save.py` → `PASS: all 11 cases`
  - 算法：非活动槽 stage → 读回校验 → 单次 `putUChar("active")` 翻转
  - 失败注入用例（首 put 失败、第 3 put 失败、写截断、提交失败、掉电状态、槽交替、legacy 回退、空 Base）全部覆盖
  - copilot finding 1.5 用 `fail_at = 当前+3` 精确注入第 3 次 put，并断言部分槽+active 未翻转——确认用例 4 不再误报
- **观察**：
  - `openai_load_config` 的"已初始化槽 = 任一键存在"语义对空 Base 场景正确（用例 10 验证）
  - legacy 平键回退仅在双槽均未初始化时触发——首次 save 后即不再走 legacy 路径（用例 9b 验证）
  - `Preferences` 不是可重入的，注释明示"all current callers run on the UI thread"—— 当前调用点（`ui_ai_cfg::ai_cfg_save`）均为 UI 线程 ✅
- **遗留**（Copilot 1.6 已声明）：Python 模型与 C++ 实现在不同文件，编译器不强制同步；docstring 已写明"修改 C++ 实现时必须同步更新脚本中的算法模型并重跑"。建议下一轮把状态机提取为无 Arduino 依赖的 C++ 单元并直接测试（TODO）。
- **结论**：算法正确，测试可执行，作为合并前置已满足。

### 1.2 Sleep frame-wait 帧序号方案（9a89cdd）干净解决了"重入 LVGL + 旧屏帧"陷阱

- **严重性**：✅ 通过（High 项已闭合）
- **位置**：`9a89cdd` `examples/pda2/ui_deckpro.cpp:3941-3958`、`ui_deckpro_port.{cpp,h}`、`examples/pda2/factory.ino:785-794,228`
- **机制**：
  - `disp_full_refr()` 递增 `disp_flush_req_seq`；flush 回调（`factory.ino:228`）在 FULL 模式完成时把 `disp_flush_done_seq` 写到该值——`done` 是"该请求的 flush 已上屏"的单调计数器
  - `ui_disp_full_refr_seq()` 返回本次请求的序号；`ui_disp_flush_done_seq()` 返回上屏序号
  - `entry11` 取本次请求序号 → 50ms watcher timer 比较"done >= seq"才启动 1s 倒计时
  - 3s 兜底（60 ticks × 50ms）：未上屏 → 取消深睡并显示 `Display sync failed - sleep cancelled`
- **设计正确性**：
  - 帧绑定到本次请求的序号 → 旧屏帧 / 中间 partial flush 不会误触发
  - watcher timer 在 LVGL tick 上下文跑，不在 `entry()` 内同步等待 → 避开了 `scr_mgr_push` 中 `entry()` 先于 `lv_scr_load()` 触发的"看旧屏"陷阱
  - `entry11` 注释明示 `lv_timer_del + NULL` 防 double-free（主评审 1.7）
- **真机回归（§4 P0 Sleep）**：✅ 1/2/3 全部通过（2026-08-17 用户实测）
- **结论**：本批合并前置条件已满足。

### 1.3 WiFi 扫描临界区读写双侧都已加固（9c075c5 + ba31181）

- **严重性**：✅ 通过（High 项已闭合）
- **位置**：`examples/pda2/ui_deckpro.cpp:1796-1856,2337-2357`
- **观察**：
  - 共享变量 `s_scan_done_cnt` / `s_scan_release_pending` / `s_scan_release_target` 全部在 `portMUX_TYPE s_scan_mux` 临界区内读写
  - 所有读取（包括 `scan_release_is_pending()`）走 helper 函数——把"读也要进临界区"作为不可绕过的契约
  - abort 路径"先发布 target+pending，再重查计数"的顺序封闭了"SCAN_DONE 落在发布前"的窗口
  - 注释明示 Serial 不在临界区内使用（避免长临界区）
- **结论**：上轮 Cop 1.3 High 已真正落地。

### 1.4 SPIFFS rename 三步舞（867435e）修复了第二次保存静默失败的根因

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:213-230,244-247`
- **机制**：
  - 老路径存在 → `official → bak`（先于 tmp 落位）
  - `tmp → official`（若失败，从 bak 还原）
  - 成功后 `bak` 删除；任何一步失败保留旧 official 不动
  - 启动时若只有 bak 没有 official，自动 promote
  - 写入 + 校验和失败的 tmp 立即 `remove`，不留垃圾
- **真机回归（§4）**：第一轮重启恢复 ❌ → `867435e` 已修；待复测 ✅
- **观察**：diag 串口已经补上，下次回归失败可直接定位是哪一步（bak 失败 / rename 失败 / 校验和不匹配）
- **结论**：根因已修复，机制稳健。

### 1.5 chat.log CHL2 升级 + V1 识别（3cdff38）正确解决"升级丢历史"误报

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:64-69,260-275`
- **机制**：
  - 新格式 magic = `0x324C4843u`（"CHL2"），头部增加 4B 记录数；loader 严格按 `count` 解析 N 条 + 4B checksum
  - 旧 `CHAT_LOG_MAGIC_V1`（"CHL1"）识别后显式串口 `old-format log (CHL1) ignored - history starts fresh` —— 不再误判为损坏
  - 空 / 单条 / 多条日志均能恢复（c90307f 修复）
- **观察**：
  - 上轮 Cop 1.3（"格式升级会丢历史"）已解决
  - 已升级到 c90307f 的设备再次升级会丢历史——但 c90307f 距 3cdff38 时间很短，且 Serial 明确告警；可接受
- **结论**：升级路径清晰，行为可解释。

### 1.6 usage 统计生命周期节流 + V1→V2 迁移（3cdff38 + 0328cd2）

- **严重性**：✅ 通过
- **位置**：`examples/pda2/openai_api.cpp:131-308`
- **机制**：
  - 静态 `xSemaphoreCreateMutexStatic`（无 lazy-create race）
  - chat / test 两组独立计数（主评审 1.4）
  - 节流：每 20 次响应或 60s 一次 blob 提交；持久化失败保留 dirty 计数并以 10s 退避重试（不再重置整个窗口）
  - V1 magic（`STAT`）识别后迁移进 chat 组并写回 V2（`STAV`）—— 不再清零
  - `openai_stats_flush()` 在 sleep_do_enter / chat_destroy / chat_clear_history（New 路径）三个生命周期点显式落盘
- **观察**：低频使用（开机一次响应后关机）也保证落盘——上轮 Cop 1.1 已真正闭合
- **结论**：本批合并后 usage 账本可保留到下一个 release。

### 1.7 AI Chat 多轮上下文 — 整轮配对 + 状态行可见 + (failed)/(truncated) 剔除

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_chat.cpp:436-484, 419-434`
- **观察**：
  - 快照按整轮（user+assistant 成对）选择；孤立 assistant 停止窗口；孤立 confirmed user 尾部允许
  - 预算 8KB 按整轮字节计入，一轮放不下即停
  - `(truncated)` 标记在 `chat_ctx_body()` 中剥离再入上下文；`(failed)` 在 `chat_history_snapshot()` 中通过 `chat_pending_idx` 跳过
  - 状态行 `Thinking · N ctx msgs · trimmed` 在裁剪时显式提示（Cop 1.8）
  - 串口 `send: N context msgs`——上轮已修条件取反（Cop 1.2）
- **真机回归（§4）**：✅ 多轮记忆通过（用户实测）
- **结论**：单轮 → 多轮语义转变已在产品 + 文档层面对齐（`allinone-design.md` 第 6/27/162/253 行同步）。

### 1.8 AI Config Test 计费透明 + Save 失败原因 msgbox

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_ai_cfg.cpp:175-208,301,340-356`
- **观察**：
  - Test msgbox 起始 `Testing... 15s\ncosts ~1 token\n(network+auth only)`（主评审 1.3/1.4）
  - 倒计时中显示 `Testing... Ns\ncosts ~1 token`
  - 成功：`Test OK: <reply>... (billed ~1 token)`
  - 失败：`Save failed:\n<NVS write failed / NVS commit failed>` —— 不再只写灰色状态行（主评审 1.9）
- **观察**：Close 在 busy 期间 = Cancel（`s_ai_test_req_gen++` + busy=false），迟到结果被丢弃并打 `[AICfg] stale test result dropped`
- **结论**：与申请书 §2.6 claim 一致。

### 1.9 文档与请求合并流程同步落地

- **严重性**：✅ 通过
- **位置**：`docs/reviews/README.md` `docs/async_ipc_contract.md`
- **观察**：
  - `docs/reviews/README.md`（`a6388b0`）：合并流程、命名约定、`git show` 追溯、≥10 commit 分段评审条款（Copilot §1.11 + 主评审 §1.11）
  - `docs/async_ipc_contract.md` 顶部新增"适用范围"（主评审 §1.8）
  - 契约新增第 11 条"多轮上下文轮次配对规则"（主评审 §1.7）
  - 申请/结果归档 commit 配合本 README 执行
- **结论**：文档化清晰，可作为下一轮评审的依据。

### 1.10 C1/C2 补偿控制落地但 SECURITY.md 引用断裂

- **严重性**：Low
- **位置**：`5dd8e32` `examples/pda2/openai_api.h:55-66` `SECURITY.md:8` `platformio.ini [env:pda2]`
- **观察**：
  - C1：`-DAI_KEY_DEFAULT_COMPILED` 在 `[env:pda2]` 已设置；编译期 `#warning` 触发；实测可观察
  - C2：`SECURITY.md` 完整列出推公网 4 步（删 Key / 移宏 / 轮换 / filter-repo）
- **问题**：
  - `SECURITY.md:8` 写 `see \`memory/api-key-dev-exception.md\``，但 `memory/` 目录在仓库中不存在（git ls-tree 验证）
  - `examples/pda2/openai_api.h:61` 同样引用 `memory/api-key-dev-exception`
  - 多份历史评审结果文件（`wifi-config-keyboard-review-result-844a907..e1b2d0f.md` 等）通过链接引用此文件
- **影响**：跨文档链接断裂；评审人 / 未来开发者无法追溯用户原始决策的文档
- **最小修复**：
  1. 创建 `memory/api-key-dev-exception.md` 并把决策正文（来源：历史评审 §1.10）固化进去
  2. 或修改 SECURITY.md 措辞，把"决策正文"内联进来

### 1.11 23 commit 申请体量超过 §1.11 分段评审阈值（申请人已自承认）

- **严重性**：Low（流程观察）
- **位置**：申请书 §6 "按评审 §1.11 约定，下轮起单次申请 ≥10 commit 时分段评审"
- **观察**：
  - 本批 23 个 commit（`844a907`..`3cdff38`），远超 §1.11 的 10 commit 阈值
  - 申请人虽意识到，但本批仍合并提交
  - 模块拆分清晰（NVS / WiFi 扫描 / Sleep / WiFi busy / AI Chat / AI Config / 文档 / 多轮 / usage / New / Key / frame-wait / 临界区 / atomic log / stats / 测试 / 真机回归修复 / 持久化迁移），每 commit 对应单一主题
  - 拆分会破坏"Finding → commit 映射"的连贯性，且前 4 轮整改之间有强依赖（如 `c90307f` 修复 `35e9eae`，`8d273cd` 配套 `844a907`）
- **结论**：本批整体可接受，但申请人应在下轮评审前主动拆分（建议每 5-7 commit 一段）。

### 1.12 真机回归 P1/P2 仍有多项 ⏸（部分接受的前置条件）

- **严重性**：Medium（合并阻塞级别）
- **位置**：申请书 §4
- **当前状态**：
  - **已通过**：P0 Sleep 1/2/3、多轮记忆（2026-08-17 用户实测）
  - **已修复待复测**：重启恢复（`867435e`）
  - **⏸ 关键项**：New 确认（键盘 + 触摸）、usage 累加正确性、轮次配对串口偶数、SPIFFS RAM-only 状态可见、重试草稿、WiFi 离页重进
- **影响**：
  - P1/P2 多数是 UX 细节，但 §4 #11（usage 累加）和 #15（轮次配对）是多轮 / 账本两大功能的核心验证——若串口 `send: N` 不为偶数，多轮会引入 user-only 上下文污染
  - §4 #10/11（WiFi 离页重进）牵涉扫描临界区修复（Cop 1.3）—— 是真机回归对修复有效性的最终判定
- **最小修复**（合并前必须）：
  1. §4 #4（Test msgbox 文案） / #5（Close 取消） / #7（发送 / 重试） / #11（usage 累加） / #12（New 确认） / #15（轮次配对）—— 6 项 P1 中至少 4 项回填结果（成功 + 串口片段）
  2. §4 #10 / #11（WiFi 离页重进）—— 2 项 P2 必须回填（验证 9c075c5 / ba31181 / e31cd06 三 commit 的有效性）
- **建议**：申请人第二轮实测后把视频或 3 张图 + 串口片段补登到 §4；评审人在最终合并前再次审查。

### 1.13 Test ping 计入 usage 累计未在 AI Config 屏明示

- **严重性**：Low
- **位置**：`06f9235` + `e60b2e8` + `ui_ai_cfg.cpp`
- **观察**：
  - chat / test 用量已分组存储（主评审 1.4 已落地）
  - Test msgbox 文案明示 `costs ~1 token` / `billed ~1 token`，但这是单次提示
  - AI Config 屏 Test 按钮旁无"Test counts to usage"持续提示
  - usage 统计展示屏（`TODO.md` 已登记）尚未实施——届时区分 chat / test 是必须的
- **影响**：用户反复 Test 时，chat 累计不会增长（已分组），但 test 累计会持续上升；用户若只看 chat 会困惑"为什么我问了 5 次只累计了 3 次"
- **最小修复**：
  1. 在 Test 按钮旁加一行小字（"Test counts to usage"），或
  2. 等待 usage 统计展示屏实施时一并明示 chat / test 分组（推荐）

### 1.14 SPIFFS /chat.log 整文件重写仍是性能跟踪项

- **严重性**：Low
- **位置**：`ui_ai_chat.cpp:172-231` + `TODO.md`
- **观察**：
  - 每次确认消息后整文件重写（最多 16KB）—— 已 atomic 安全但写放大明显
  - `TODO.md` 已登记"append+compact 或后台保存线程"作为阶段 0 收尾项
- **影响**：长会话（>50 条消息）后 SPIFFS 寿命 / 用户感知延迟可见
- **结论**：不在本批解决，跟踪至阶段 1（allinone）实施。

### 1.15 AI Stats 节流的"重启丢失一个窗口"边界

- **严重性**：Low
- **位置**：`examples/pda2/openai_api.cpp:288-303`（节流计算）
- **观察**：
  - 当前实现：每 20 次响应或 60s 一次 blob 提交
  - 边界：上次落盘后立刻断电 → 最多丢失 19 次响应 / 60s 的 RAM 增量
  - 三个 lifecycle checkpoint（sleep_do_enter / chat_destroy / New）已覆盖"主动离开"路径
  - 仅"意外掉电"边界仍有窗口——这是 RAM-only 状态的固有限制
- **结论**：与设计目标一致；可选改进是新增周期 timer（每 60s 主动落盘），但当前实现已满足"低频使用不丢"。

---

## 2. 已通过项汇总

- 双槽 NVS 原子保存 + 11/11 PASS（实测）
- Sleep frame-wait 帧序号 + 3s 兜底取消深睡
- WiFi 扫描临界区读写双侧加固（关闭"读也需进临界区"窗口）
- WiFi 屏离页清 busy + page_gen
- AI Chat 动态 body + UTF-8 截断 + 16KB 预算淘汰
- SPIFFS /chat.log CHL2 magic + 记录数 + 字节和校验 + 三步 rename
- SPIFFS 不自动格式化 + RAM-only 状态可见
- AI Chat 多轮整轮配对上下文 + 8KB 预算 + `(truncated)`/`(failed)` 剔除
- usage 静态 mutex + chat/test 分组 + 节流落盘 + V1→V2 迁移
- AI Config Test 计费透明 + Save 失败 msgbox 原因
- New 按钮确认框（Enter=OK / 任意键=Cancel）
- 重试复用 pending 气泡（drop-last + re-add）
- 重试草稿 /chat.draft 持久化
- Chat/Test pings 计入对应累计（chat/test 分组）
- API Key C1 `#warning` + C2 SECURITY.md
- 异步 IPC 契约适用范围标注 + 轮次配对规则文档化
- docs/reviews/README.md 申请合并流程 + 分段评审条款

## 3. 已接受但未消除的安全风险

- 真实 API Key 仍在源码与 Git 历史中。按 `api-key-dev-exception` 用户决策延后；C1/C2 已落地。
- 推公网 / 重大 release 前必须按 `SECURITY.md` 4 步处理（删 Key / 移 `-DAI_KEY_DEFAULT_COMPILED` / OpenRouter 轮换 / filter-repo 清理历史）。
- 本评审**不视为阻塞项**，但下次涉及发布 / 镜像 / OTA 的 commit 应重新升级为 Critical。

## 4. 阻塞清单（合并到 master 前必须满足）

| 阻塞项 | 严重性 | 必须满足 |
|---|---|---|
| §1.10 SECURITY.md 引用断裂 | Low | 创建 `memory/api-key-dev-exception.md` 或内联到 SECURITY.md |
| §1.12 真机回归 P1/P2 ⏸ 项 | Medium | 至少 6 项 P1（4/5/7/11/12/15）+ 2 项 P2（10/11）回填结果与串口片段 |
| §1.11 23 commit 体量 | Low | 已在 §6 自承认；下轮起按 5-7 commit 分段 |

## 5. 强烈建议（可纳入下轮评审）

- §1.13 Test ping 计入 usage 在 AI Config 屏明示
- §1.14 SPIFFS append+compact 或后台保存线程（已登记 TODO）
- §1.6 Python 模型与 C++ 实现同步：把状态机提取为无 Arduino 依赖的 C++ 单元并直接测试

## 6. 验证说明

- `python scripts/test_nvs_atomic_save.py` → 11 项 PASS（实测）
- `python examples/pda2/scripts/ca_bundle_check.py` → openssl 缺失时输出 actionable 提示（实测）
- 当前环境无 `pio` 可执行，未独立复现固件编译
- 申请人自测：`pio run -e pda2` SUCCESS；RAM 47.5% / Flash 30.1%；COM5 烧录 + Hash verified
- P0 Sleep 真机三项 + 多轮记忆 用户实测通过（2026-08-17）
- 结果文档未包含 API Key 正文（按 SECURITY.md 例外处理）

## 7. 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**

**接受范围**：本批 23 commit 中 22 commit 可接受（除 §1.3 中按用户决策延后的 Key 项）。

**前置条件（合并到 master 前必须满足）**：
- §1.10 SECURITY.md / openai_api.h 引用 `memory/api-key-dev-exception.md` 的链接修复
- §1.12 真机回归 P1/P2 至少 6 项关键用例补登（Test 文案 / Close 取消 / 重试 / usage 累加 / New 确认 / 轮次配对 / WiFi 离页重进）

**遗留项**：
- Key 项按 `api-key-dev-exception` 决策延后；跟踪至推公网 / 重大 release 前
- SPIFFS 整文件重写（§1.14）已登记 `TODO.md` 跟踪

---

**评审人**：Codex（第三方静态复核视角，已交叉核对 `git log 844a907..3cdff38` 的 23 个 commit 实际内容、运行 `scripts/test_nvs_atomic_save.py` 实测 11/11 PASS、并对照申请书 §2 的 Finding → commit 映射逐项验证）。