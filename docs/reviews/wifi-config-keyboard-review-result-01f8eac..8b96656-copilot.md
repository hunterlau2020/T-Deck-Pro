# 第 8-16 轮双评审整改复审结果（Copilot）

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-01f8eac..8b96656.md](wifi-config-keyboard-review-request-01f8eac..8b96656.md)
- **关联 commit**：`01f8eac` `cb8201f` `57356fa` `8b96656`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 真实 API Key 仍在源码和固件中

- **严重性**：Critical
- **位置**：`examples/pda2/openai_api.h:27-30`
- **触发场景**：仓库、Git 历史或固件可被读取。
- **证据**：本申请明确将该项延后，`openai_load_config()` 仍在 NVS 无值时使用源码默认 Key。
- **影响**：凭据、账户额度和费用仍可被滥用；该项仍是合并阻断项。
- **最小修复**：立即撤销凭据；源码默认值改为空；清理历史并启用 secret scanning。评审文档不得再次复制凭据正文。

### 1.2 新增 CA 检查脚本无法提取证书

- **严重性**：Medium
- **位置**：`examples/pda2/scripts/ca_bundle_check.sh:17-19`、`examples/pda2/http_utils.cpp:24-28`
- **触发场景**：运行 `ca_bundle_check.sh`。
- **证据**：
  - 脚本用 `awk '/static const char \*CA_BUNDLE =/,/;/'` 截取赋值。
  - CA_BUNDLE 后的第一段注释已包含分号：`failed; review finding...`。
  - awk 会在该注释行提前结束，尚未读取第一段 PEM。
- **影响**：脚本会得到零张证书并失败，不能成为证书回归门禁；申请书所述自动验证机制不可用。
- **最小修复**：使用明确的赋值结束标记或专用解析脚本，不得用任意分号作为结束条件；将脚本接入 CI 或构建前检查。

### 1.3 扫描中止事件在重试前到达时，会永久保持 release pending

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1768-1779,1843-1862,2272-2287`
- **触发场景**：
  1. 扫描中止三秒超时，设置 `s_scan_release_pending=true`；
  2. 迟到的 `SCAN_DONE` 在用户点击重试前到达并递增计数；
  3. 用户随后重试扫描。
- **证据**：重试时才读取新的 `prev = s_scan_done_cnt`，随后等待计数再次变化；已经到达的目标事件无法满足该等待。
- **影响**：每次重试都显示 `Scan busy - retry`，直到另一个无关 SCAN_DONE 或设备重启。
- **最小修复**：超时时保存待等待的目标计数/代次；事件回调到达时即可清除 pending，重试路径比较保存值而不是重新建立基线。

### 1.4 WiFi Test 与 Time Sync 的旧代结果不会释放 busy

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1480-1565,1680-1742`
- **触发场景**：请求中离开 WIFI 页，重新进入后旧结果到达。
- **证据**：
  - `destroy4()` 没有清除或按代次管理 `s_wifi_test_busy`、`s_time_sync_busy`。
  - timer 仅在结果代次匹配当前页面时把 busy 设为 false。
  - 旧代结果进入 stale 分支后只删除结果对象。
- **影响**：对应功能以后一直被 `if (busy) return` 拒绝，只能重启恢复。
- **最小修复**：busy 必须携带所属 request/page generation；旧代结果完成时释放其所属任务状态，或离页后保持不可重入直至旧任务结束并明确显示。

### 1.5 队列分配失败会让异步功能永久卡在 busy

- **严重性**：Medium
- **位置**：
  - `examples/pda2/ui_deckpro.cpp:1579,1620`
  - `examples/pda2/ui_ai_cfg.cpp:268`
  - `examples/pda2/ui_ai_chat.cpp:166`
- **触发场景**：低内存时 `xQueueCreate()` 返回 `NULL`。
- **证据**：代码没有检查返回值，仍设置 busy 并启动任务；任务发现 queue 为 NULL 后删除结果，没有 UI 完成消息。
- **影响**：WiFi Test、Time Sync、AI Test 或 AI Send 会永久无响应。
- **最小修复**：先创建并检查 queue，失败立即提示且不得设置 busy 或启动任务。

### 1.6 AI Chat 重入会共享并覆盖请求正文，旧结果还会解除新请求的 busy

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:43-46,116-181,340-349`
- **触发场景**：发送 A 后离页，重新进入并发送 B；A 在 B 期间完成。
- **证据**：
  - 所有工作任务读取同一个全局 `chat_prompt_buf`。
  - `destroy()` 无条件把 `s_chat_send_busy` 设为 false，因此允许 B 启动并覆盖缓冲。
  - 收到任何结果时，在 generation 校验前就无条件 `s_chat_send_busy=false`。
- **影响**：A 可能发送 B 的正文；A 的旧结果会解除 B 的 busy，允许第三个并发请求，造成问题和回答错配。
- **最小修复**：prompt、配置和 generation 必须存入每任务独占参数；只有当前 request ID 的结果可以清除当前 busy。

### 1.7 AI Test 仍未验证草稿 Model

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_cfg.cpp:206-233,319-330`
- **触发场景**：Base 和 Key 有效，但填写不存在或无权访问的模型。
- **证据**：Test 只请求草稿端点的 `/models?limit=2`，并将任意 `data[0].id` 视为成功；草稿 Model 只检查非空。
- **影响**：Test 通过后允许 Save，但实际聊天仍会因目标 Model 错误失败。
- **最小修复**：验证模型列表包含草稿 Model，或用草稿 Base、Model、Key 发起最小 chat-completion 请求。

### 1.8 AI 配置写入不是原子的，失败会留下混合配置

- **严重性**：High
- **位置**：`examples/pda2/openai_api.cpp:26-34`
- **触发场景**：NVS 空间不足或第二、第三个 `putString()` 失败。
- **证据**：Base、Model、Key 依次覆盖旧值；返回 false 时不会回滚前面已经成功的写入。
- **影响**：UI 显示保存失败，但旧的可用配置已被部分替换。
- **最小修复**：写入临时 slot/namespace，全部成功后原子切换 active version；或可靠保存旧值并在失败时回滚。

### 1.9 UI 的 10 秒 timeout 仍未覆盖完整请求，也未使请求代次失效

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_cfg.cpp:295-317`、`examples/pda2/http_utils.cpp:242-268`
- **触发场景**：系统时间未同步，或 HTTP 接近十秒才返回。
- **证据**：
  - HTTP 调用前还可能在 `http_ensure_time()` 等待五秒，随后才进入十秒 HTTP timeout。
  - UI 到十秒只把 busy 清零，没有递增 request generation。
  - timeout 后关闭弹窗时 countdown 已关闭，也不会取消该代请求。
- **影响**：UI 先显示超时，迟到结果随后仍可覆盖状态；用户还可在旧任务运行时启动新 Test。
- **最小修复**：使用覆盖 NTP、连接和读取全过程的绝对 deadline；UI timeout 时立即递增 request generation。

### 1.10 UTF-8 换行修复后，历史消息缓冲仍可能从多字节字符中间截断

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:19-46,48-59`
- **触发场景**：AI 回复超过 255 字节，且第 255 字节位于中文或 emoji 的多字节序列中。
- **证据**：`chat_history_add()` 使用 `strncpy(..., CHAT_MSG_MAX - 1)` 后直接补 NUL，没有回退到 UTF-8 码点边界。
- **影响**：长回答仍可能在末尾出现乱码；同时全部超过 255 字节的回答都会静默截断。
- **最小修复**：扩大或动态保存回答，并在截断时回退到完整 UTF-8 码点边界，显示明确的 `(truncated)` 标记。

## 2. 通过项

- ISRG Root X1 当前 PEM 已替换为完整版本，五张证书可独立解析。
- 正常 queue 投递路径明确了结果对象所有权，UI 消费后正确释放。
- 扫描 pending 时拒绝新扫描的方向正确，只需修复迟到事件窗口。
- AI Save 已增加字段校验和 Test 门槛。
- AI Chat 普通网络失败会保留输入草稿。
- LVGL label 的自动换行避免了旧实现固定 30 字节切行的问题。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

Critical Key 项仍未关闭；此外扫描事件窗口、异步 busy/request 所有权、AI Model 测试和 NVS 原子写入仍需修订后重新申请。
