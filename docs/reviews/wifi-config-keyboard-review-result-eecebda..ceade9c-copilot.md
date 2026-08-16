# AI Text 聊天界面及第 17/18 轮整改评审结果（Copilot）

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-eecebda..ceade9c.md](wifi-config-keyboard-review-request-eecebda..ceade9c.md)
- **关联代码范围**：`eecebda^..ceade9c`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 真实 API Key 仍在源码、Git 历史和固件中

- **严重性**：Critical
- **位置**：`examples/pda2/openai_api.h:33-36`
- **触发场景**：仓库、历史记录或固件可被读取。
- **证据**：本申请明确将该项继续遗留，NVS 无 Key 时仍使用源码默认凭据。
- **影响**：凭据及关联账户额度仍可被滥用；后台 revoke 不能消除源码和固件中的敏感字符串。
- **最小修复**：源码默认值改为空；撤销凭据并清理 Git 历史；启用 secret scanning。评审文档不得复制凭据正文。

### 1.2 “暂存后换入”仍不是原子保存，失败回滚也不可靠

- **严重性**：High
- **位置**：`examples/pda2/openai_api.cpp:26-69`
- **触发场景**：正式键逐项换入期间掉电、NVS 空间不足，或第二/第三次 `putString()` 失败。
- **证据**：
  - 临时键只验证写入能力，正式 Base、Model、Key 仍是三次独立覆盖。
  - 中途掉电仍可能留下新旧混合配置。
  - 失败回滚的三次写入结果未检查。
  - 原键不存在时，回滚会写入空字符串，后续读取不再采用默认值。
- **影响**：UI 显示 Save failed，但已知可用配置可能已经被部分破坏。
- **最小修复**：将完整配置保存为单个 blob；或使用两个完整 slot，全部写入并校验非活动 slot 后，只原子切换一个 active-slot/version 键。

### 1.3 SCAN_DONE 在超时判断与 pending 发布之间到达时，仍会永久卡住扫描

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1780-1790,2287-2307`
- **触发场景**：
  1. 中止等待刚超过三秒；
  2. `if (cnt != prev)` 已判断为 false；
  3. `SCAN_DONE` 在设置 `s_scan_release_pending=true` 前到达。
- **证据**：
  - 回调此时看见 pending 为 false，不会清除它。
  - UI 随后发布 pending 和旧 target，但目标事件已经过去。
  - pending/counter 还跨 WiFi 事件任务与 UI 任务无同步访问。
- **影响**：后续扫描持续显示 `Scan busy - retry`，直到另一个无关事件或重启。
- **最小修复**：先在临界区发布 target/pending，再停止扫描和等待；超时后重新检查计数。相关状态使用临界区、原子变量或事件组同步。

### 1.4 Sleep 倒计时仍从“请求全刷”而非“显示完成”开始

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:3863-3873`、`examples/pda2/factory.ino:762-765`
- **触发场景**：EPD 全刷耗时一至两秒。
- **证据**：
  - `ui_disp_full_refr()` 最终只把 `disp_refr_mode` 设为 FULL，不等待面板刷新完成。
  - timer 随即启动，刷新耗时计入三秒倒计时。
- **影响**：提示实际可见时间短于三秒；首个一秒 tick 可能在刷新完成后立即执行。
- **最小修复**：从 EPD flush 完成通知开始计时，或等待首次提示帧完成后再创建 timer。

### 1.5 WiFi 页离开后仍保留旧请求 busy，与异步 IPC 契约不一致

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:1462-1468,1731-1756`、`docs/async_ipc_contract.md:32-37`
- **触发场景**：WiFi Test 或 Time Sync 进行中离页并立即重新进入。
- **证据**：
  - 契约要求 `destroy()` 执行 generation+1、busy=false。
  - `destroy4()` 没有清除两项 busy。
  - 新页面在旧请求完成前点击对应功能会被静默拒绝。
- **影响**：最长约十至十五秒内功能无反馈；若任务异常不返回，则持续不可用。
- **最小修复**：离页时使旧代 busy 失效并清除；继续用 `busy_gen` 保证旧结果不能解除新请求。

### 1.6 AI Chat 重新进入页面时不会渲染仍保留的历史

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:274-359`
- **触发场景**：已有内存聊天历史，退出 AI Text 后再次进入。
- **证据**：`chat_history` 和 `chat_hist_cnt` 保留，但 `chat_create()` / `chat_entry()` 没有调用 `chat_history_render()`。
- **影响**：重新进入后历史区域为空；直到再次发送或收到回复，旧历史才突然出现。
- **最小修复**：历史容器创建完成后渲染现有历史，并保持滚动位置或明确滚到底部。

### 1.7 失败重试会重复插入用户消息，任务创建失败也留下未发送气泡

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:185-220`
- **触发场景**：网络/API 请求失败后保留草稿并重试，或 `xTaskCreate()` 失败。
- **证据**：每次调用 `chat_send()` 都在任务创建和请求成功前立即 `chat_history_add(true, prompt)`。
- **影响**：每次重试都会增加相同用户气泡；任务根本没有启动时，历史仍显示消息已发送。
- **最小修复**：任务成功创建后再加入 pending 气泡；重试复用同一个 pending 消息，成功后转为完成，失败时标记失败而不是重复追加。

### 1.8 任务请求快照仍可能截断为非法 UTF-8

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:39-52,172-175`
- **触发场景**：textarea 中输入较多中文或 emoji，使 UTF-8 字节数超过 255。
- **证据**：
  - textarea 上限是 200 个字符，可能远超 255 字节。
  - `chat_send_req_t.prompt` 只有 256 字节。
  - `strncpy()` 截断后直接补 NUL，没有回退到 UTF-8 码点边界。
- **影响**：发往 API 的 JSON 字符串可能包含非法 UTF-8，且用户输入被静默缩短。
- **最小修复**：任务快照使用动态字符串或与输入上限匹配的 UTF-8 字节容量；若必须截断，回退到完整码点并在发送前明确提示。

### 1.9 历史消息固定 256 字节，常规长回复会永久丢失大部分内容

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:37-102`
- **触发场景**：模型返回超过约 243 字节的回答。
- **证据**：每条历史记录固定为 256 字节；超出部分被截断后只追加 `(truncated)`。
- **影响**：聊天界面虽然改为可滚动历史，但无法查看常见长度的完整 AI 回答，属于明显功能退化。
- **最小修复**：使用动态消息正文或独立的大型回答存储；根据总内存限制淘汰最旧消息，而不是把每条回答硬截断到约 243 字节。

### 1.10 CA 检查脚本缺少 OpenSSL 依赖检查，当前 Windows 环境直接 traceback

- **严重性**：Medium
- **位置**：`examples/pda2/scripts/ca_bundle_check.py:43-48`
- **触发场景**：系统 PATH 中没有 `openssl`。
- **证据**：脚本直接调用 `subprocess.run(["openssl", ...])`；当前项目环境运行后抛出 `FileNotFoundError`，没有可操作提示。
- **影响**：检查无法作为可重复的构建或 CI 门禁；申请书所述“五张证书检查通过”依赖未记录的本机环境。
- **最小修复**：启动时用 `shutil.which("openssl")` 检查并输出明确安装说明；在 CI 固定依赖版本，或改用项目声明的 Python X.509 依赖。

## 2. 通过项

- Sleep timer 句柄保存、一次性兜底、返回/销毁取消及回调自删除已修复。
- WiFi Test、Time Sync 的 queue 创建检查和 `busy_gen` 结果匹配已实现。
- AI Config 使用草稿 Base、Model、Key 发起最小聊天请求，能够验证实际三元组。
- AI Test 使用任务自有快照；Close 和十五秒 deadline 会使请求代次失效。
- AI Chat 已使用每任务独占的 prompt/config 快照，旧代结果不能解除新请求 busy。
- 历史消息自身的截断会回退 UTF-8 continuation byte 并显示截断标记。
- 新 CA 脚本能够按当前源码结构准确提取 PEM；问题仅在运行依赖和门禁集成。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

Critical Key 项仍未关闭；扫描边界同步和配置原子提交仍为 High 阻断项。其余问题应在真机回归前修复，避免聊天重入、重试和长回答体验继续返工。
