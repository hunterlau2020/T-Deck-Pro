# 评审申请书：第 20 轮评审整改（第二十一次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `844a907` — `pda2: openai config - dual-slot storage with atomic active-slot commit`
  - `9c075c5` — `pda2: wifi scan - publish release target under a critical section`
  - `7fec0e5` — `pda2: sleep countdown starts when the frame reached the panel`
  - `e31cd06` — `pda2: wifi page - clear busy on leave (async IPC contract rule 8)`
  - `9b376da` — `pda2: AI Chat - dynamic bodies + SPIFFS persistence + retry reuse`
  - `e60b2e8` — `pda2: AI Config - billing transparency + save failure reason`
  - `8461f65` — `pda2: ca_bundle_check - fail fast when openssl is missing`
  - `a6388b0` — `docs: review workflow README + nvs atomic save test spec + archive results`
  - `e1b2d0f` — `pda2: AI Chat - send recent history as multi-turn API context`（用户追加需求）
  - `06f9235` — `pda2: accumulate response usage into NVS ai_stats`（用户追加需求）
  - `23063b0` — `pda2: AI Chat - rename Hist button to New`（用户追加需求）
  - `5dd8e32` — `pda2: API key dev exception - compile-time warning (C1) + SECURITY.md (C2)`
  - `9a89cdd` — `pda2: sleep countdown waits for ITS OWN frame via flush sequences`
  - `ba31181` — `pda2: wifi scan - reads of shared state also go through the critical section`
  - `35e9eae` — `pda2: AI Chat - atomic log + turn pairing + New confirmation`
  - `74c24ff` — `pda2: AI usage stats - mutex-guarded single-blob accounting`
  - `538e6d0` — `tests: align nvs atomic save spec with the dual-slot implementation`
  - `c90307f` — `pda2: AI Chat - fix log record count, turn pairing, draft sync`
  - `0328cd2` — `pda2: usage stats - static mutex, chat/test split, throttled persist`
  - `4c1ceda` — `pda2: sleep - cancel instead of deep-sleeping on frame-wait timeout`
  - `8d273cd` — `tests: executable state-machine test for the dual-slot NVS save`
  - `867435e` — `pda2: AI Chat - fix log swap-in rename conflict + load diagnostics`（真机回归发现）
  - `3cdff38` — `pda2: persistence lifecycle + format migration + trim visibility`
  - `55054fa` — `pda2: menu - AI-first first page (user-requested order)`（用户追加需求）
  - `75dd255` — `pda2: AI Chat - send UX: clear on send + waiting overlay`（用户追加需求）
  - `dbe15ed` — `pda2: AI Chat - no rq use after task handoff + dual CHL1 parse + bak fallback`
  - `aedd401` — `pda2: usage stats - loop-tick 60s persist + AI Cfg checkpoint + V2 commit on migration`
  - `76bc321` — `docs: inline the api-key dev-exception decision into SECURITY.md`
  - `156732c` — `pda2: AI Config - Usage button shows chat/test statistics`（用户追加需求）
  - `e08bdac` — `pda2: AI Config - Usage breakdown includes cached_tokens details`（用户追加需求）
  - `8770a41` — `pda2: wifi scan - hold the overlay for a minimum visible time`（用户反馈修复）
  - `f4449c3` — `pda2: AI Chat - two-tab layout: full-screen Chat + big Input`（用户追加需求）
- **评审依据**：
  - [主评审 eecebda..ceade9c](wifi-config-keyboard-review-result-eecebda..ceade9c.md)（部分接受，11 Findings）
  - [Copilot 复审 eecebda..ceade9c](wifi-config-keyboard-review-result-eecebda..ceade9c-copilot.md)（退回修订，10 Findings）
  - [主评审 844a907..e1b2d0f](wifi-config-keyboard-review-result-844a907..e1b2d0f.md)（部分接受；其 C1/C2 要求已由 `5dd8e32` 落地）
  - [Copilot 复审 844a907..23063b0](wifi-config-keyboard-review-result-844a907..23063b0-copilot.md)（退回修订；其 Findings 由 `9a89cdd..538e6d0` 整改）
  - [主评审 844a907..538e6d0](wifi-config-keyboard-review-result-844a907..538e6d0.md)（部分接受；前置条件由 `c90307f..8d273cd` 落地，P0 真机回归除外）
  - [Copilot 复审 844a907..538e6d0](wifi-config-keyboard-review-result-844a907..538e6d0-copilot.md)（退回修订；确定性逻辑错误由 `c90307f..8d273cd` 修复）
  - [Copilot 复审 844a907..8d273cd](wifi-config-keyboard-review-result-844a907..8d273cd-copilot.md)（退回修订；持久化升级兼容/落盘保证/测试注入由 `867435e`/`3cdff38` 修复）
- **历史文档**：前二十轮申请与结果见 `docs/reviews/`（合并流程见 `docs/reviews/README.md`）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

按上一轮两份评审结果的全部 Findings 逐项整改（**Key 项除外**，仍按用户决策延后）。用户已确认修复范围 = 全部非 Key 项；聊天历史方案 = SPIFFS 文件 + 动态正文。

## 2. 变更明细（Finding → commit 映射）

### 2.1 双槽原子保存（`844a907`）— Cop 1.2 High

上轮"暂存-换入"方案仍非原子：正式键逐项覆盖，中途掉电/失败会留混合配置，回滚还会把不存在的旧键写成空串。现改为：

- 整个配置（base/model/key）写入**非活动槽** `base.N/model.N/key.N`（N∈{0,1}）并读回校验
- 提交点 = **单次原子** `putUChar("active")` 翻转——翻转前任何失败都让旧槽原封不动，任何时刻 load 得到的都是某一次完整保存的三元组
- `openai_load_config` 回退链：活动槽 → 旧固件平键（兼容迁移）→ 编译期默认值
- 回滚不再需要（无半成品状态），`err` 出参区分 "NVS write failed" / "NVS commit failed"
- 规格：`tests/test_nvs_atomic_save.md`（10 条失败注入用例，含掉电提交点）

### 2.2 扫描临界区（`9c075c5`）— Cop 1.3 High

上轮修复仍有窗口：abort 先判断计数、后发布 pending，SCAN_DONE 落在两者之间时回调看不见（pending 还是 false）而目标事件已过，重试永久 "Scan busy"。现改为：

- abort **先**在临界区内发布 target+pending，**再**重查计数；回调在同一临界区内递增计数并自清 pending（回调运行于 WiFi 事件任务，与 UI 线程用 `portMUX_TYPE` 同步；临界区内不使用 Serial）
- 事件要么被发布后重查捕获，要么到达时由回调自清——窗口关闭

### 2.3 Sleep 倒计时从"显示完成"起算（`7fec0e5`）— Cop 1.4 Medium + 主 1.7 Low

- `ui_disp_full_refr()` 原本只置 FULL 标志，EPD 全刷波形耗时 1-2s 被计入倒计时。新增 `disp_full_refr_wait()`：置 FULL + `disp_flush_pending` 标志，`flush_epd_bitmap` 全刷完成后清标志；等待循环有界（5s）并泵 `lv_task_handler` 让 flush timer 运行。`entry11` 改用同步版本
- `entry11` 先 `lv_timer_del` + `NULL` 再 `lv_timer_create`，注释说明防 double-free 顺序（主 1.7）

### 2.4 WiFi 页离页清 busy（`e31cd06`）— Cop 1.5 Medium

`destroy4()` 补上契约第 8 条：页面代次 +1、`s_wifi_test_busy`/`s_time_sync_busy` 清零。离页后立即重进不再被静默拒绝 15s；在飞任务持有自有快照，`busy_gen` 仍保证旧代结果不能解锁新请求。

### 2.5 AI Chat 重做（`9b376da`）— 主 1.2 High、1.5 Medium + Cop 1.6/1.7/1.8/1.9 Medium

- **动态正文**：`chat_msg_t.text` 改 `std::string`（堆分配），固定 256B 硬切删除；总预算 16KB 从最旧淘汰（字符串安全赋值，不用 memmove）
- **单一截断机制**（主 1.5）：仅 `CHAT_MSG_MAX=4096` 单条上限会截断，UTF-8 码点回退 + `(truncated)` 标记——与气泡存储同一份代码产出
- **SPIFFS 持久化**（主 1.2）：`/chat.log` 二进制记录（1B 标志+2B 长度+正文），每次变更整写（16KB 上限内）；进屏时恢复；SPIFFS 不可用时降级 RAM-only（串口记录一次）；新增 **Hist 侧按钮**（后按用户要求改名 New，见 §2.11）清空历史（含日志截断）
- **重进渲染**（Cop 1.6）：`chat_create` 末尾渲染恢复的历史，不再空白
- **重试复用气泡**（Cop 1.7）：任务创建成功后才加入用户气泡；重试 drop-last+re-add 复用同一气泡；失败追加 `(failed)` 标记，草稿仍保留可重试
- **快照不截断**（Cop 1.8）：prompt 以完整 `std::string` 复制进任务快照，200 字符（≤800 UTF-8 字节）不受 255 字节切分
- 布局像素预算注释入头（主 1.6）：240×320 屏，容器 232×274 = 历史 160 + 状态 16 + 输入行 86（3×26px 按钮 + 2×4px 间距）

### 2.6 AI Config 文案（`e60b2e8`）— 主 1.3/1.4 合并条件 + 1.9 Medium

- Test msgbox 明示计费与范围：`"Testing... 15s\ncosts ~1 token\n(network+auth only)"`，倒计时与成功提示（`billed ~1 token`）同步保留
- Save 失败以 msgbox 显示原因（来自双槽保存的 err），不再只写灰色状态行

### 2.7 CA 脚本依赖检查（`8461f65`）— Cop 1.10 Medium

`shutil.which("openssl")` 启动即查，缺失时输出可操作的安装提示并退出，不再裸 traceback。

### 2.8 文档（`a6388b0`）— 主 1.8/1.9/1.11

- `docs/async_ipc_contract.md` 顶部标注适用范围：契约只约束异步 HTTP 屏，Sleep/Keys/GPS 等纯本地屏不适用
- 新建 `docs/reviews/README.md`：申请合并流程（何时合并、范围命名、`git show` 追溯方式）
- 新建 `tests/test_nvs_atomic_save.md`：双槽保存的 10 条失败注入规格
- 上轮两份评审结果归档

### 2.9 AI Chat 多轮上下文（`e1b2d0f`）— 用户追加需求（呼应主 1.2"单轮问答"实证）

此前历史只在本地渲染/持久化，API 请求固定为 `[system, 当前user]`，模型对会话无记忆。现改为：

- `openai_api` 新增 `openai_chat_multi(history, count, prompt, ...)`：messages = system + 历史轮次（时序）+ 当前 prompt；`openai_chat` 保留为单轮包装（Test ping 用）
- `ui_ai_chat` 发送时把**最近 8KB**（约 2K token）历史快照进任务自有结构：整条轮次不拆、最旧先裁、排除 pending 气泡（其内容就是当前 prompt）；角色按 from_user 映射 user/assistant
- 快照所有权仍归任务（契约 finding 1.6），串口打印 `[AIChat] send: N context turns` 便于核对

### 2.10 usage 用量统计（`06f9235`）— 用户追加需求

服务器 response 的 `usage` 块按**容错原则**解析（用户要求 2：任何字段可能缺失——缺失计 0，未知字段忽略；`cJSON_IsNumber` 双重检查），累加到 NVS `ai_stats`（8 个键，NVS 键名 ≤15 字符故用短名）：

| 字段 | NVS 键 | 来源 |
|---|---|---|
| prompt_tokens | `p_tok` | usage.prompt_tokens |
| completion_tokens | `c_tok` | usage.completion_tokens |
| total_tokens | `tot_tok` | usage.total_tokens |
| cost | `cost` (double) | usage.cost |
| cached_tokens | `cached` | prompt_tokens_details.cached_tokens |
| cache_write_tokens | `cwrite` | prompt_tokens_details.cache_write_tokens |
| audio_tokens | `audio` | prompt_tokens_details.audio_tokens |
| reasoning_tokens | `reasoning` | completion_tokens_details.reasoning_tokens |

- 计数**不随 New 清空**（用量账本 ≠ 会话数据）；Test 的 ping 也计入（真实计费）
- 串口日志：`[AI] usage +p/c tok, cost +x | totals ...`；将来统计屏直接读 `ai_stats`

### 2.11 New 按钮（`23063b0`）— 用户追加需求

Hist 改名 **New**：语义 = 开启新会话——清空可见历史 + SPIFFS 日志（usage 计数保留）。行为与改名同步：UI 文案、代码注释、本申请回归清单全部改用 New。

### 2.12 Key 补偿控制 C1/C2（`5dd8e32`）— 主评审 844a907..e1b2d0f §1.1

按用户 `api-key-dev-exception` 决策补齐补偿控制：

- **C1**：`openai_api.h` 在 `AI_KEY_DEFAULT_COMPILED` 定义时每次编译发出 `#warning "Dev-only API Key in source - rotate before pushing to a public remote"`；`[env:pda2]` build_flags 加 `-DAI_KEY_DEFAULT_COMPILED`。已实测警告触发
- **C2**：仓库根 `SECURITY.md`：例外说明、推公网前必做 4 步（删 Key 字符串、去掉编译宏、OpenRouter 轮换、filter-repo）、当前控制状态

### 2.13 第二轮整改（`9a89cdd..538e6d0`）— 对应 Copilot 复审 844a907..23063b0 全部非 Key Findings

| 评审项 | 处理（commit） |
|---|---|
| **Cop 1.2 High** Sleep 等待在 entry() | `9a89cdd`：改**帧序号**机制——`disp_full_refr()` 递增请求序号，FULL 刷完成时复制到完成序号；entry11 只记录本次序号并起 50ms watcher timer（LVGL 正常 tick 上下文），Sleep 帧真正上屏后才启动倒计时（3s 兜底）。删除阻塞式 `disp_full_refr_wait()`，不再重入 LVGL、不再等待旧屏帧 |
| **Cop 1.3 High** 扫描临界区外读 | `ba31181`：新增 `scan_release_is_pending()` 在临界区内读；start/abort 的所有 if/while/终判全部走 helper，注释明示"读也要进临界区" |
| **Cop 1.4 High** chat.log 原地整写+自动格式化 | `35e9eae`：临时文件 + 每笔 write 检查 + 字节和校验 + rename 原子换入；load 校验 magic+校验和，撕坏的日志整体丢弃；`SPIFFS.begin(false)` 永不自动格式化，挂载失败降级 RAM-only |
| Cop 1.5 failed/pending 持久化 | `35e9eae`：pending 气泡**不进日志**；失败时草稿存 `/chat.draft`，重进恢复、成功/New 清除；上下文**剔除 UI 标记**（pending 跳过、`(truncated)` 截去、`(failed)` 永不选中） |
| Cop 1.6 轮次配对 | `35e9eae`：快照按**整轮**（user+assistant 成对）选择，孤立 assistant 停止窗口，孤立 confirmed user 尾部允许；串口改报 `context msgs` |
| Cop 1.7 New 确认 | `35e9eae`：New 先弹确认框（Cancel/OK），键盘可关（Enter=OK、任意其他键=Cancel）；Alt+Enter 键盘路径打开；申请与代码一致用 New |
| Cop 1.8 规格矛盾 | `538e6d0`：修正首次保存（写 slot 1）与空 Base（isKey 判定槽已初始化则原样读回）两用例 |
| Cop 1.9 统计并发 | `74c24ff`：8 项指标入单一 RAM 结构 + FreeRTOS mutex 串行累加 |
| Cop 1.10 NVS 写放大 | `74c24ff`：每次响应**一次** putBytes blob 提交（magic 校验），持久化失败记日志且 RAM 总数不丢；顺带 load 回退改 isKey 槽初始化判定（同 Cop 1.8） |
| Cop 1.11 申请未登记 | 本申请 §2.10-2.12 已登记 `06f9235`/`23063b0`/`5dd8e32`（`331c6f9` 完成，本扩展继续登记本轮 5 commit） |

### 2.14 第三轮整改（`c90307f..8d273cd`）— 对应 844a907..538e6d0 双评审 Findings

| 评审项 | 处理（commit） |
|---|---|
| **Cop 1.1 High** chat.log 校验和必失败 | `c90307f`：文件头加**记录数**，loader 精确解析 N 条后读校验和——旧循环条件会把尾部校验和当消息头吃掉，合法日志（含空日志）全部被误判损坏丢弃；现空/单条/多条日志均可恢复 |
| **Cop 1.2 High** 轮次配对条件写反 | `c90307f`：assistant 的前一条**是** user 才配对（原条件取反导致首轮即 break，多轮上下文从未生效） |
| **Cop 1.3 High** mutex 惰性创建竞态 | `0328cd2`：改**静态初始化** `xSemaphoreCreateMutexStatic`（加载期创建，无 first-use 竞态，不泄漏句柄） |
| **Cop 1.4 High** watcher 超时仍深睡 | `4c1ceda`：3s 超时改为**取消深睡**并显示 `Display sync failed - sleep cancelled`——只有目标帧完成事件才允许启动倒计时 |
| Cop 1.5 草稿生命周期不同步 | `c90307f`：exit() 按 textarea 实况保存/清除草稿；Clear 按钮同步清持久化文件 |
| Cop 1.6 / 主 1.2 测试不可执行 | `8d273cd`：`scripts/test_nvs_atomic_save.py` 可注入失败的 KV store 镜像算法，10 用例全部断言执行（**实测 11/11 PASS**，含 fail_at 提交点失败、掉电状态、槽交替、legacy 回退、空 Base）；spec 顶部引用脚本 |
| 主 1.3 NVS 写放大 | `0328cd2`：持久化**节流**——每 60s 或每 20 次响应最多一次 blob 提交；RAM 总数实时，掉电最多丢一个节流窗口 |
| 主 1.4 Test 混入 chat 统计 | `0328cd2`：blob 内 **chat/test 两组独立计数**，串口分别打印；Test ping 不再污染聊天 totals |
| 主 1.7 轮次配对文档化 | `docs/async_ipc_contract.md` 新增第 11 条"多轮上下文轮次配对规则" |
| 主 1.8 SPIFFS 状态可视 | `c90307f`：SPIFFS 不可用时状态行显示 `Storage: RAM-only (log not saved)` |
| 主 1.11 分段评审条款 | `docs/reviews/README.md` 加入"单次申请 ≥10 commit 应分段" |
| Cop 1.8 申请书旧名称 | 本申请 §4 回归项已统一为 New / `send: N context msgs` |

### 2.15 第四轮整改（`867435e`/`3cdff38`）— 真机回归发现 + Copilot 复审 844a907..8d273cd

**真机回归第一轮（2026-08-17，用户实测，结果已回填 §4）**：多轮记忆 ✅、P0 Sleep 三项 ✅、
重启恢复 ❌（历史未展示且 AI 无上下文）。定位：`SPIFFS.rename` 在目标已存在时失败
（spiffs_rename CONFLICTING_NAME）——第二次保存起全部静默失败，日志停留在第一条。
修复 `867435e`：换入改为 bak 三步舞（old→bak、tmp→official、删 bak），loader 启动时
把遗留 bak 提升回主路径；新增保存/加载诊断串口。

**Copilot 复审 844a907..8d273cd 的 8 项 Findings**（`3cdff38`）：

| 评审项 | 处理 |
|---|---|
| 1.1 usage 60s 节流无定时器 | 新增 `openai_stats_flush()`：深睡（sleep_do_enter）、chat destroy、New 三个生命周期检查点显式落盘 |
| 1.2 持久化失败重置节流 | 失败保留 dirty 计数并以 10s 退避重试，不再延迟整个窗口 |
| 1.3 chat.log 格式升级丢历史 | 新格式 magic 升 **CHL2**；loader 识别 CHL1 并明示"旧格式日志忽略"而非误报损坏 |
| 1.4 ai_stats blob 升级清零 | blob 升 **V2 magic** + 尺寸识别：读旧 V1（单组）blob 时迁移进 chat 组再写回 |
| 1.5 测试用例 4 注入错位 | 改用 `fail_at = 当前+3` 精确失败第 3 次 put，并断言部分槽 + active 未翻转 |
| 1.6 测试是手写镜像 | 已知限制（脚本 docstring 声明同步义务）；C++ 单测化列 TODO |
| 1.8 上下文静默裁剪 | 状态行显示 `Thinking · N ctx msgs · trimmed`（发生裁剪时） |
| 1.7 真机回归 | 见 §4 回填：P0 ✅、多轮 ✅、重启恢复 ❌→已修待复测 |

### 2.16 第五轮（`55054fa`/`75dd255`）— 用户追加需求（2026-08-17）

- **菜单第一屏重排**（`55054fa`）：AI 优先——`AI Cfg / AI Text / AI Chat / Dict / Weather / Calendar / Calc / Wifi / Sleep`；硬件/系统类条目（Lora/Setting/GPS/Test/Battery/Input/A7682E/Shutdown/Motor）移至第二屏
- **AI Text 发送交互**（`75dd255`）：点 Send 后**输入框立即清空**、用户气泡立即入历史、弹置顶等待层 `Waiting server reply... 10s`（按秒变化更新文本，到 0 显示 `still waiting...` 持续等待）；回复到达 → 等待层消失 + AI 气泡追加；失败 → 等待层消失 + pending 气泡标 `(failed)` + **文本回填输入框**可重试（草稿持久化同步）。等待层打开期间吞键、离屏销毁

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS；RAM 47.5% / Flash 30.1% |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| CA bundle 检查 | ✅ 通过 | 5 张根证书 openssl 解析 OK |
| NVS 双槽算法测试 | ✅ 通过 | `scripts/test_nvs_atomic_save.py` 11/11 PASS |
| 真机回归（§4） | 🔶 第一轮完成 | 2026-08-17 用户实测：P0 ✅、多轮记忆 ✅；重启恢复 ❌ → 已修（`867435e`）待复测 |

## 4. 真机回归清单（**第一轮结果已回填**，剩余 ⏸）

**P0 Sleep（5/6/7，不可逆操作）——✅ 全部通过（2026-08-17 用户实测）**
1. ✅ 点 Sleep → 提示画面完整可见（倒计时 2→1）→ 黑屏深睡
2. ✅ 倒计时内按 Back → 回菜单且不深睡
3. ✅ 按 BOOT 键唤醒 → 开机画面 → 版本号 → 主菜单，键盘/修饰键正常

**P1 AI Config（8/9/10）**
4. ✅ Test → msgbox 含 `costs ~1 token` 倒计时 15s → `Test OK: <回复> (billed ~1 token)`（2026-08-17 实测）
5. ✅ Test 中点 Close → 迟到结果丢弃（串口 `stale test result dropped`）
6. 改字段后 Save → `Run Test first`；Test 通过后 Save → `Saved`；保存失败场景（如需）显示原因

**P1 AI Chat（11/12）**
7. ⏸ 发送成功 → 输入框清空、回复左对齐追加、自动滚底；发送失败 → 草稿保留、气泡标 `(failed)`；重试**不产生重复气泡**
8. ✅ 重启/重进 AI Text → 历史完整恢复（SPIFFS）：第一轮失败 → `867435e` 修复 → **第二轮复测通过**（历史恢复 + 多轮上下文接续）
9. ⏸ 长回答 >4KB → `(truncated)` 无乱码；中文/emoji 正常
10. ✅ **多轮记忆**（2026-08-17 用户实测）：连续两问，第二问引用上文（"翻译成中文"）→ AI 正确接上（用户反馈"似乎解决了，继续观察"）
16. ✅ **发送交互**（2026-08-17 实测）：Send → 输入框立即清空 + 气泡立即出现 + 等待层 `Waiting server reply... 10s` → 回复到达自动消失
17. ✅ **菜单第一屏**：9 个条目按 AI 优先顺序显示，第二屏为硬件/系统类
18. ⏸ **失败重试路径**：关 WiFi（关热点/清凭据重启）后发送 → 等待层消失 + 文本回填 + 气泡标 `(failed)`——方法已告知用户，下次顺带验证
19. N/A **扫描中退出**：当前网络扫描 1-2s 完成，覆盖层一闪而过，无可见退出窗口

### 2.17 第六轮（`dbe15ed..76bc321`）— Copilot 复审 844a907..3cdff38 + Codex 复审 844a907..3cdff38

| 评审项 | 处理（commit） |
|---|---|
| **Cop 1.1 High** 任务创建后 UI 仍读 rq（跨核 use-after-free） | `dbe15ed`：任务创建前把 ctx_msgs/trimmed 复制为 UI 局部变量，handoff 后不再解引用 rq |
| Cop 1.2 CHL1 双义丢历史 | `dbe15ed`：loader 对 CHL1 双格式逐一尝试（带 count → 无 count），任一成功即恢复（下次存 CHL2） |
| Cop 1.3 official 无效不试 bak / tmp 无 flush | `dbe15ed`：official 解析失败时 promote bak 重试；tmp 写完后显式 `flush()` 再 rename |
| Cop 1.4 60s 落盘无定时器 / AI Cfg 不 flush | `aedd401`：`openai_stats_poll()` 挂入 loop() 周期检查 60s 窗口；ai_cfg destroy 加 checkpoint |
| Cop 1.5 V1 迁移不立即写 V2 | `aedd401`：迁移成功即标 dirty，下个 checkpoint 落 V2 |
| Cop 1.6 真机复测 | 待用户（§4 第二轮清单） |
| Cop 1.7 整文件重写 | 已登记 TODO（技术债，不阻断） |
| Codex 1.10 SECURITY.md 引用断裂 | `76bc321`：决策正文内联进 SECURITY.md，openai_api.h 仅引用 SECURITY.md |
| Codex 1.12 P1/P2 回归回填 | 待用户（至少 6 项 P1 + 2 项 P2） |
| Codex 1.11 分段评审 | 下轮申请按 5-7 commit 分段（docs/reviews/README.md 已立条款） |

### 2.18 第七轮（`156732c`）— 第二轮真机回归回填 + Usage 按钮

**第二轮真机回归（2026-08-17 用户实测，§4 已回填）**：
- ✅ (1) 重启恢复复测：历史恢复 + 多轮上下文接续（`867435e`/`dbe15ed` 修复验证通过）
- ✅ (2) AI Config Test：计费文案 + Close 取消（stale 丢弃）
- ✅ (5) New 确认框 OK/Cancel
- ✅ (6) 发送交互：Send 立即清框 + 气泡 + 等待层倒计时 → 回复到达自动消失
- ✅ (7) WiFi Test 进行中离页 → 重进可立即重试
- ⏸ (3) 失败重试路径：用户已掌握关 WiFi 方法（关热点/清凭据重启），待下次顺带验证
- ✅→N/A (8) 扫描中退出：实测当前网络扫描 1-2s 完成，无可见退出窗口（覆盖层一闪而过），按 N/A 记录
- ✅ (4) usage 累计：`156732c` 在 AI Config 加 **Usage 按钮**（Save/Test 旁第三按钮），msgbox 显示 chat/test 两组 tokens + USD 累计，串口 totals 同步可核

**Test 验证方式**：用户确认**保持现状**（最小 chat-completion，~1 token 计费已明示；/models 列表方案的"填错模型仍显示通过"缺陷不再回退）。

### 2.19 第八轮（`e08bdac..f4449c3`）— 用户反馈

- **Usage 明细**（`e08bdac`）：Usage 弹窗显示完整 breakdown——Chat 组 total/cached/
  cache_write/audio/reasoning/cost（用户点名要 `prompt_tokens_details.cached_tokens`）+ Test 组
- **扫描中间提示**（`8770a41`）：确认 overlay 在 `scanNetworks` 返回 RUNNING 后**立即**弹出
  （不是扫描完成后）；实测看不到的原因是快速扫描（<1s）在覆盖层 EPD 局刷上屏前就完成被
  hide——加 800ms 最短显示时间，扫描结束后覆盖层继续停留至满 800ms 再隐藏
- **AI Text 双 Tab**（`f4449c3`）：Chat/Input 两个 tab（Alt+Enter 切换）——Chat tab 历史
  **占满全屏**；Input tab 大输入框（176×~220）+ 大按钮（48×~74×3）；Send 后自动跳回
  Chat tab；Chat tab 按任意可见字符自动跳 Input 并追加；重试草稿恢复时默认开 Input tab；
  New 键盘路径改到音量键（Input tab 下）
11. **usage 统计**：一次对话后串口出现 `[AI] usage +232/215 tok, cost +...`；重启后再对话，totals 在上次基础上累加
12. ✅ **New 按钮**：确认框 OK/Cancel 行为正常（2026-08-17 实测）；usage 计数不受影响（Usage 按钮可核）
13. **Sleep 帧等待**：点 Sleep → 提示画面完整显示后倒计时才从 2 开始（旧固件会吃掉 1-2s 全刷时间）；倒计时内 Back 取消
14. **重试草稿**：发送失败后重启 → 进 AI Text → 输入框恢复草稿、状态行 `Retry draft restored`；重试成功后草稿清除
15. **轮次配对**：多轮对话后串口 `send: N context msgs` 恒为偶数（除尾部未回复的 user），上下文首条必为 user

**P2 其他**
10. WiFi Test/Time Sync 进行中离页 → 立即重进 → 可立即发起新请求（不再被 busy 拒绝）
11. WiFi 扫描中退出 → 重进扫描正常（不再永久 `Scan busy`）
12. 连按重试 5 次不崩溃

## 5. 遗留项（明示）

- **Key**：按用户 `api-key-dev-exception` 决策延后保留（非 blocking）；补偿控制 **C1/C2 已于 `5dd8e32` 落地**，C3（用户侧 free-tier 轮换）用户已承诺。推公网/重大 release 前重新升级为 Critical
- **真机回归 §4**：**第一轮已执行**（2026-08-17）——P0 Sleep 三项 ✅（合并前置条件满足）、多轮记忆 ✅；重启恢复 ❌ 已定位修复（`867435e`）待复测；其余 ⏸ 项待用户第二轮实测
- **SPIFFS 整文件重写写放大**（主评审 1.2 跟踪项）：已通过临时文件 + 校验和 + rename 保证安全，但 append+compact 或后台保存线程未实施；CJK 8KB 预算静默裁剪（主评审 1.3 跟踪项）的 UI 提示未实施——下轮评审继续跟踪

## 6. 回滚方案

```bash
git revert 76bc321 aedd401 dbe15ed 75dd255 55054fa 3cdff38 867435e 8d273cd 4c1ceda 0328cd2 c90307f 538e6d0 74c24ff 35e9eae ba31181 9a89cdd 5dd8e32 23063b0 06f9235 e1b2d0f a6388b0 8461f65 e60b2e8 9b376da e31cd06 7fec0e5 9c075c5 844a907
```

28 个 commit 按模块拆分，任一可独立 revert，中间态均可独立编译。（按评审 §1.11 约定，下轮起单次申请 ≥10 commit 时分段评审。）

## 7. 申请审批事项

- [ ] **A. 全量接受** — 保留全部 commit，关闭本轮评审循环（Key 项按 §5 单独跟踪）
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
