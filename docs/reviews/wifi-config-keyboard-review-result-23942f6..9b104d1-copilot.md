# 第 8-16 轮合并申请复审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-23942f6..9b104d1.md](wifi-config-keyboard-review-request-23942f6..9b104d1.md)
- **关联代码范围**：`23942f6^..9b104d1`
- **既有结果**：[wifi-config-keyboard-review-result-23942f6..9b104d1.md](wifi-config-keyboard-review-result-23942f6..9b104d1.md)
- **评审结论**：**退回修订**

本文保留既有结果，补充对最终代码快照的实现级复审。凭据正文不在本文重复记录。

## 1. Findings

### 1.1 真实 OpenRouter API Key 被编译进固件并永久进入 Git 历史

- **严重性**：Critical
- **位置**：`examples/pda2/openai_api.h:27-30`
- **触发场景**：任意仓库读取者、固件提取者或公开 CI 日志接触该版本。
- **证据**：
  - `AI_KEY_DEFAULT` 是真实私有凭据，不是示例占位符。
  - `openai_load_config()` 在 NVS 不存在 `key` 时自动使用它。
  - 注释和申请书均已承认该 Key 进入仓库。
- **影响**：账户配额和费用可被第三方消耗；删除当前文件不能撤回 Git 历史中的凭据，烧录后的固件也包含该字符串。
- **最小修复**：
  1. 立即在 OpenRouter 后台撤销该 Key，并核查用量。
  2. 删除源码默认 Key；NVS 无 Key 时返回“未配置”。
  3. 使用首次配置、设备烧录时注入或不进入版本库的本地配置机制。
  4. 对仓库历史执行凭据清理，并增加 secret scanning。

### 1.2 CA bundle 中的 ISRG Root X1 仍是损坏证书

- **严重性**：High
- **位置**：`examples/pda2/http_utils.cpp:24-55`
- **触发场景**：HTTPS 端点的证书链需要回到 ISRG Root X1。
- **证据**：
  - 从最终代码拼接 `CA_BUNDLE` 后共得到五张 PEM。
  - 使用 `cryptography.x509.load_pem_x509_certificate()` 逐张解析时，第 1 张 ISRG Root X1 报 ASN.1 `ShortData`；其余 Root YR、DigiCert G2、GlobalSign R3、GTS R4 均可解析。
  - 这与早期 allinone 评审发现的“ISRG Root X1 PEM 不完整”是同一根因，本轮只是继续在损坏内容后追加其他证书。
- **影响**：依赖 X1 的站点仍可能 TLS 校验失败；“根证书一次性补全”的申请结论不成立。
- **最小修复**：从 CA 官方来源重新导入完整 X1 PEM；增加构建前逐张解析及目标端点握手测试，任何一张解析失败即阻止构建。

### 1.3 扫描中止超时后，下一次扫描仍会在迟到回调前执行 `scanDelete()`

- **严重性**：High
- **位置**：`examples/pda2/ui_deckpro.cpp:1803-1813`、`2212-2227`
- **触发场景**：
  1. 扫描中退出页面或倒计时到期；
  2. `SCAN_DONE` 三秒内未到，代码进入“deferred release”分支；
  3. 用户重新进入并立即再次扫描。
- **证据**：
  - 超时分支不调用 `scanDelete()`，但立即把 `wifi_scan_state` 设为失败。
  - 下一次 `wifi_cfg_scan_start()` 无条件先调用 `WiFi.scanDelete()`，没有等待上一代 `SCAN_DONE`。
  - 全局 `s_scan_done_ev` 也没有扫描代次；上一代迟到事件可能满足下一代中止等待。
- **影响**：仍可能与框架 `_scanDone()` 分配/填充结果的过程并发，重现原 High 级竞态；迟到事件还可能错误释放新扫描的数据。
- **最小修复**：为每代扫描维护明确状态；上一代未收到完成事件前禁止新扫描和 `scanDelete()`。事件通知必须绑定代次，或完全使用框架提供的串行事件/队列确认后再释放。

### 1.4 四条异步路径用 `volatile bool` 传递含 `std::string` 的结果，存在跨核数据竞争

- **严重性**：High
- **位置**：
  - `examples/pda2/ui_deckpro.cpp:1443-1466`
  - `examples/pda2/ui_ai_cfg.cpp:38-41,153-159`
  - `examples/pda2/ui_ai_chat.cpp:86-123`
- **触发场景**：FreeRTOS 网络任务在一个核写入 `http_response_t` / `std::string`，LVGL 线程在另一核看到 ready 标志后读取或复制。
- **证据**：
  - `volatile` 只阻止部分编译器优化，不提供 C++ happens-before、互斥或完整内存屏障。
  - `wifi_test_result`、`ai_test_result` 和 `chat_send_reply` 都是非平凡对象，由两个任务无锁读写。
  - 任务句柄也由工作任务与 UI 线程无同步读写。
- **影响**：可能读取尚未完成构造/赋值的字符串，导致错误正文、堆损坏或偶发崩溃；真机单次通过不能证明不存在竞态。
- **最小修复**：使用 FreeRTOS queue、task notification 配合拥有所有权的结果对象，或受 mutex 保护的固定 POD 缓冲；UI 线程只消费已完整投递的消息。

### 1.5 页面活动布尔值不能区分页面代次，旧请求会污染重新进入的页面

- **严重性**：High
- **位置**：
  - `examples/pda2/ui_deckpro.cpp:1470-1507,1681-1704`
  - `examples/pda2/ui_ai_cfg.cpp:227-263,413-417`
  - `examples/pda2/ui_ai_chat.cpp:153-169,308-315`
- **触发场景**：请求中退出页面，在请求完成前重新进入同一页面。
- **证据**：
  - 代码只检查当前 `*_active`，没有请求 ID 或页面 generation。
  - 重新进入后 active 再次为 true，因此上一页面实例发起的结果会显示在新实例。
  - `destroy()` 只隐藏对象，没有取消任务或使旧任务代次失效。
- **影响**：旧 WiFi Test、Time Sync、AI Test 或 AI 回答会突然覆盖新页面状态；用户可能把旧配置的测试结果误认为当前配置结果。
- **最小修复**：创建请求时同时记录页面 generation 和 request ID；完成消息只有在二者仍匹配时才能应用。离页应取消请求或至少使该代结果永久失效。

### 1.6 AI Test 固定测试 OpenRouter，未测试用户填写的端点和模型

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_cfg.cpp:153-156,175-190`
- **触发场景**：用户把 Base 改为 OpenAI、自建代理或其他兼容端点，或填写了不存在的模型。
- **证据**：
  - Test 无条件 GET `https://openrouter.ai/api/v1/models?limit=2`。
  - `ai_base` 和 `ai_model` 只做非空检查，没有参与请求。
  - 成功条件只是返回数组中存在任意 `data[0].id`。
- **影响**：
  - 自定义端点和 Key 即使完全有效，也可能因拿去访问 OpenRouter 而测试失败。
  - OpenRouter Key 有效但用户填写的 Base 或 Model 错误时，测试仍显示成功。
  - “Test 通过”不能证明保存后的聊天配置可用。
- **最小修复**：针对草稿 Base 构造其模型查询端点，或更可靠地向草稿聊天端点发送最小、低成本请求并验证草稿 Model；不同供应商不兼容 `/models` 时应明确选择测试策略。

### 1.7 Save 无校验、无测试门槛，错误草稿会覆盖旧的可用配置

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_cfg.cpp:135-141,202-207,280-287`
- **触发场景**：字段为空、URL 拼写错误、Key 输入错误，随后点击 Save 或在最后字段按 Enter。
- **证据**：
  - `ai_cfg_save()` 直接调用 `openai_save_config()`。
  - Test 中的字段校验没有被 Save 复用。
  - Test 成功状态也没有成为 Save 的前置条件。
- **影响**：一次误操作即可覆盖 NVS 中已知可用配置；与申请书“失败不覆盖旧值”的持久化原则不一致。
- **最小修复**：Save 先执行统一本地校验，并要求当前草稿对应的 Test 成功；保存前保留旧配置，写入失败或测试失败时不得替换。

### 1.8 AI Test 的 10 秒倒计时不是请求超时，关闭弹窗也不会取消请求

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_cfg.cpp:51-58,153-159,189-199,210-263`
- **触发场景**：请求耗时超过 10 秒，或用户在 Testing 弹窗中点击 Close。
- **证据**：
  - UI 在 10 秒时显示 `Request timeout`，但 HTTP timeout 是 15 秒，TLS 时间同步还可能额外等待。
  - 工作任务继续运行，稍后结果会再次调用 `ai_msgbox_show()`，覆盖“超时”状态或重新弹出用户已关闭的弹窗。
- **影响**：用户看到互相矛盾的超时与成功结果；Close 的产品语义实际只是隐藏，不是取消。
- **最小修复**：倒计时必须绑定真实请求 deadline；Close 明确区分 Hide 与 Cancel。若取消，增加 request generation 并永久丢弃该结果。

### 1.9 AI 发送失败或任务创建失败会丢失用户问题

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:126-149,153-169`
- **触发场景**：任务创建失败、无网络、TLS/认证错误、限流或服务端超时。
- **证据**：
  - 创建任务前就执行 `lv_textarea_set_text(chat_ta, "")`。
  - `xTaskCreate()` 失败只显示 `Cannot start task`，没有恢复输入。
  - 网络请求失败只显示通用错误，也没有把 `chat_prompt_buf` 恢复到输入框。
- **影响**：用户必须重新输入最多 200 字的问题，无法直接重试。
- **最小修复**：发送成功后才清空持久草稿；所有失败和取消路径恢复原问题并提供 Retry。

### 1.10 AI 回答仍按固定字节切行，会破坏 UTF-8

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:62-78`
- **触发场景**：回答包含中文、emoji 或其他多字节字符，字符跨越 30 字节边界。
- **证据**：`strlen()`、固定 `take = 30` 和 `memcpy()` 都按字节工作。
- **影响**：可能产生非法 UTF-8、乱码或丢字；当前 `allinone-design.md` 已明确要求使用 UTF-8 安全和字体宽度换行，但 pda2 参考实现仍不符合。
- **最小修复**：直接让固定宽度的 LVGL label 使用 `LV_LABEL_LONG_WRAP`，分页时按实际渲染高度或 UTF-8 码点边界切分，不得在 continuation byte 中间截断。

## 2. 通过项

- WiFi Test 已改用 `/ip`，裁剪尾部空白并验证 IPv4/IPv6 格式。
- 断网点击 WiFi Test 和 Time Sync 均有明确弹窗反馈。
- WiFi Test、Time Sync、AI Test 和 AI Send 已不再直接阻塞 LVGL 事件回调；总体异步方向正确。
- 页面切换时软件字符 FIFO 与 TCA8418 硬件 FIFO 都会清理，瞬时 Alt/Shift 状态会复位。
- 状态栏使用本地时间，未同步时显示 `--:--`。
- AI 配置改为三个独立输入框，触摸焦点与键盘编辑字段同步。
- OpenAI 请求体使用 cJSON 构造，用户文本中的引号、反斜杠和换行不会破坏 JSON。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

重新申请前至少应完成：撤销并移除真实 Key、替换损坏的 X1 证书、修复扫描代次释放、用队列传递异步结果、为页面和请求增加 generation、让 AI Test 验证实际草稿配置，并保证失败不覆盖旧配置或丢失用户问题。
