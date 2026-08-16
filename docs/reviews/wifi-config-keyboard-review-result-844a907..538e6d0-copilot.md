# 第 20/21 轮整改复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..538e6d0.md](wifi-config-keyboard-review-request-844a907..538e6d0.md)
- **关联代码范围**：`844a907^..538e6d0`
- **本次重点整改范围**：`9a89cdd^..538e6d0`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 chat.log 加载器会把尾部校验和当作下一条消息头，完整日志也必然校验失败

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:190-247`
- **触发场景**：保存任意聊天历史后重启或重新初始化 RAM 历史。
- **证据**：
  - 文件格式为 `magic + records + 4B checksum`，但没有记录数或正文总长度。
  - 加载循环条件是 `f.available() >= 3`，剩余四字节 checksum 时仍进入循环，并先消耗其中三字节作为 `flag + len`。
  - 随后的 checksum 读取只剩一字节，必然进入 mismatch 分支并清空全部已加载历史。
  - 空历史文件同样是 `magic + checksum`，也会走相同错误路径。
- **影响**：申请书所述“重启后历史完整恢复”和 New 后空日志恢复均无法成立；每个合法日志都会被当作损坏日志忽略。
- **最小修复**：文件头加入 record count 或 payload length，并只解析指定数量/长度的 records，最后精确读取四字节 checksum；补充空日志、单条、多条、截断尾部和错误 checksum 的可执行测试。

### 1.2 多轮上下文的 user/assistant 配对条件写反，正常会话不会进入 API context

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:339-384`
- **触发场景**：历史为正常的 `user -> assistant`，再发送下一条消息。
- **证据**：
  - 当前项是 assistant 时，代码把 `chat_history[i - 1].from_user == true` 判为 orphan 并 `break`。
  - 正常 assistant 的前一项恰好应当是 user，因此首个完整轮次就终止窗口。
  - 反之，前一项不是 user 时才会被错误地当作配对成功。
- **影响**：第二问仍基本是单轮请求；申请书的“模型记住会话”“context msgs 为偶数”无法实现。
- **最小修复**：条件改为前一项**不是** user 才判 orphan；为零轮、一轮、多轮、尾部孤立 user、孤立 assistant 和 8KB 裁剪边界增加单元测试。

### 1.3 ai_stats mutex 惰性创建仍有首次并发竞态

- **严重性**：High
- **位置**：`examples/pda2/openai_api.cpp:132-210`
- **触发场景**：设备启动后，AI Config Test 与 AI Chat 的首批请求接近同时返回。
- **证据**：
  - 两个任务都可能同时看到 `s_ai_stats_mux == NULL`，分别创建 mutex 并无同步地覆盖全局句柄。
  - 两个调用随后可能各自持有不同 mutex，仍会并发执行首次 load、累加和 blob 覆盖。
- **影响**：本提交试图修复的首次并发丢计数仍然存在，并会泄漏一个 mutex。
- **最小修复**：在单线程初始化阶段创建 mutex；或用 FreeRTOS static mutex / `std::once_flag` 等一次性初始化机制，并明确初始化失败处理。

### 1.4 Sleep 等待超时后仍启动倒计时，不能保证警告帧实际可见

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:3907-3947`
- **触发场景**：EPD flush 卡住、面板忙超时或目标序号始终未完成。
- **证据**：watcher 在 `done_seq >= wait_seq` **或**三秒超时任一条件成立时都创建深睡倒计时。
- **影响**：恰在无法确认提示上屏时，设备仍会继续执行不可逆深睡；“提示完整显示后倒计时才开始”的 P0 要求没有被保证。
- **最小修复**：watch 超时应取消 Sleep 并显示/记录刷新失败，不能进入倒计时；只有目标帧完成事件才允许启动三秒 timer。

### 1.5 Retry draft 只在请求失败瞬间保存，Clear、编辑和离页不会同步持久化状态

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:250-278,432-461,523-528,659-668,776-787`
- **触发场景**：
  - 失败后编辑草稿再退出/重启；
  - 失败后点击 Clear，再退出/重启；
  - 请求进行中离页，迟到结果因页面代次被丢弃。
- **证据**：
  - `/chat.draft` 仅在 `chat_mark_pending_failed()` 中写入。
  - Clear 只清 textarea，不删除 draft 文件。
  - 普通输入修改和 destroy 不保存最新草稿。
  - 在飞请求离页前尚未写 draft；重进后保留 pending bubble，却没有可重试文本。
- **影响**：用户明确清空或修改后的旧草稿会复活；离页可留下无法直接重试的 pending 状态。
- **最小修复**：将草稿文件与 textarea 的实际状态同步：Clear 删除、编辑后节流保存、destroy 保存；离页时把在飞 prompt 保存为 retry draft，或取消 pending bubble。

### 1.6 NVS 原子保存仍只有伪测试规格，没有可执行失败注入验证

- **严重性**：Medium
- **位置**：`tests/test_nvs_atomic_save.md`
- **触发场景**：验证各 `putString`、读回校验、active 提交及掉电边界。
- **证据**：`538e6d0` 修正了文档预期，但文件仍明确是“伪测试”，没有 mock、测试入口、断言或执行结果。
- **影响**：双槽设计的关键原子性声明仍依赖代码推理，无法防止后续迁移和空值策略回归。
- **最小修复**：实现可执行 Preferences mock 测试，并在申请中附命令及结果；掉电提交点保留真机失败注入。

### 1.7 P0/P1 真机回归仍未执行，已违反申请自身的合并门禁

- **严重性**：Medium
- **位置**：申请书 §3-§5
- **触发场景**：本轮申请被接受或合并。
- **证据**：
  - 申请书继续将全部真机回归标为待用户配合。
  - §5 又明确规定 P0 Sleep 三项“必须在合并前完成”。
  - 本轮涉及深睡、EPD 时序、SPIFFS 断电一致性和按键确认，编译/烧录不能替代行为验证。
- **影响**：不可逆深睡和数据持久化路径没有实测证据，无法支持关闭评审循环。
- **最小修复**：至少回填 Sleep P0、历史重启恢复、失败草稿、New 确认、扫描退出重进和多轮上下文串口证据。

### 1.8 申请书真机步骤仍使用旧接口名称和旧日志格式

- **严重性**：Low
- **位置**：申请书 §4
- **证据**：历史清理步骤仍写 Hist；多轮步骤仍要求 `context turns`，代码已改为 New 和 `context msgs`。
- **影响**：执行者可能找不到控件或误判日志结果。
- **最小修复**：统一为 New 和 `send: N context msgs`，并修正重复的步骤编号。

## 2. 已通过项

- Sleep 已删除生命周期中的阻塞等待和 `lv_task_handler()` 重入，改为异步 watcher。
- WiFi scan 对 `s_scan_release_pending` 的读取已统一经过临界区 helper，上一轮数据竞争已修复。
- SPIFFS 挂载已改为 `begin(false)`，不会因瞬时挂载失败自动格式化。
- chat.log 保存已检查每次 write，并使用临时文件和 checksum；写入侧方向正确。
- pending bubble 不再写入正式历史日志；New 已增加触摸确认和 Alt+Enter 键盘路径。
- AI 配置双槽 load 已用 `isKey` 区分“已保存空值”和“槽未初始化”，规格描述与实现一致。
- usage 指标已合并为单 blob，每个响应由八次 NVS 写降低为一次；正常串行路径正确。
- CA 检查脚本缺失 OpenSSL 时会给出明确错误。

## 3. 已接受但未消除的安全风险

真实 API Key 仍在源码和 Git 历史中。`#warning` 与 `SECURITY.md` 只提供流程提醒，不降低仓库或固件泄露后的凭据暴露程度。本轮按用户明确的开发期例外不重复作为退回阻断，但推送公共远端、外发固件或正式发布前必须删除、轮换并处理历史。

## 4. 验证说明

- 通过格式模拟确认：合法 `magic + records + checksum` 文件经过当前加载循环后只剩一字节，无法读取 checksum。
- 当前环境没有 `pio` 可执行文件，无法独立复现申请书中的固件编译。
- 代码差异和结果文档未包含 API Key 正文。

## 5. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

chat.log 恢复、多轮配对和首次并发统计均存在确定性核心逻辑错误；Sleep P0 失败路径也仍可能在未确认提示上屏时进入深睡。修复并补齐真机证据后再提交复审。
