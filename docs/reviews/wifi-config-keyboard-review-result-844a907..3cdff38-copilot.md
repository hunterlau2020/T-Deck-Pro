# 持久化生命周期与格式迁移复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..3cdff38.md](wifi-config-keyboard-review-request-844a907..3cdff38.md)
- **关联代码范围**：`844a907^..3cdff38`
- **本次重点整改范围**：`8d273cd..3cdff38`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 发送任务启动后仍读取任务所有的 rq，存在跨核 use-after-free

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:515-547`
- **触发场景**：点击发送，`xTaskCreate()` 成功后新任务立即在另一核执行。
- **证据**：
  - `rq` 的所有权在任务创建成功后交给 `chat_send_task_func()`。
  - 任务完成 HTTP 调用后会执行 `delete rq`。
  - UI 路径在 `xTaskCreate()` 返回后仍通过 `rq->history.size()`生成 `Thinking · N ctx msgs`。
  - HTTP 快速失败（未联网、配置错误、分配失败等）时，任务可在 UI 读取前删除该对象。
- **影响**：可能读取已释放的 `std::vector`，导致错误消息数、堆损坏或崩溃。
- **最小修复**：创建任务前把 `history.size()` 和 trimmed 标志复制到 UI 局部变量；任务创建成功后 UI 不得再访问 `rq`。

### 1.2 CHL1 同时代表两种不同格式，当前“迁移”会丢弃上一版有效日志

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:65-68,245-254`
- **触发场景**：从 `c90307f..8d273cd` 固件升级到 `3cdff38`。
- **证据**：
  - `35e9eae` 的 CHL1 是无 record count 格式。
  - `c90307f` 增加 record count 后仍沿用 CHL1 magic。
  - 当前 loader 把所有 CHL1 都认定为“pre-count format”并直接忽略。
- **影响**：上一版已经能够正确恢复的聊天历史在升级后仍会全部消失；提交所称“format migration”并未迁移该实际部署格式。
- **最小修复**：根据文件结构/长度/checksum 尝试识别带 count 的 CHL1 并迁移为 CHL2；无法识别的旧无 count CHL1 再明确放弃。

### 1.3 bak 恢复只看文件是否存在，不验证 official 是否有效

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:215-284`
- **触发场景**：掉电发生在 tmp 换入 official 后、删除 bak 前，且新 official 写入未完全持久化。
- **证据**：
  - 保存关闭 tmp 后没有显式 `flush()/fsync` 再 rename。
  - 启动时只有 official 不存在才提升 bak。
  - official 存在但 magic/checksum 失败时直接放弃，不尝试仍存在的 bak。
- **影响**：明明保留了完整旧日志，恢复流程仍可能丢失全部历史。
- **最小修复**：先校验 official；失败时校验并恢复 bak。tmp 写完后显式 flush，再执行换入状态机。

### 1.4 usage 仍没有真正的“最迟 60 秒”落盘保证

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp:227-253,285-309`
- **触发场景**：
  - AI Config Test 完成后停留在配置页或直接复位；
  - AI Chat 请求在页面 destroy 的 flush 之后才迟到完成；
  - 用户长时间停留在聊天页且响应数不足 20。
- **证据**：
  - 仍没有 60 秒 timer；时间条件只在下一次响应时检查。
  - lifecycle flush 只有 New、AI Chat destroy 和 deep sleep。
  - AI Config destroy 未 flush。
  - Chat destroy 先 flush，迟到任务随后新增的 usage 不会被该 checkpoint 捕获。
- **影响**：低频统计仍可保持未落盘任意时长，硬复位时丢失，不符合申请中的 60 秒节流上界。
- **最小修复**：维护 dirty 标志并创建单一延迟 flush timer；AI Config destroy 也应 flush。迟到响应累计后重新安排 timer。

### 1.5 V1 stats 迁移只发生在 RAM，未立即提交 V2 schema

- **严重性**：Low
- **位置**：`examples/pda2/openai_api.cpp:180-253`
- **触发场景**：读取 V1 blob 后没有新的 usage，随后执行 lifecycle flush 或重启。
- **证据**：
  - loader 把 V1 内容复制到 V2 RAM，但 `s_stats_since_persist` 保持 0。
  - `openai_stats_flush()` 仅在该计数大于 0 时写入。
- **影响**：设备每次启动都会重复迁移旧 blob；无法确认 schema 升级已永久完成。
- **最小修复**：迁移成功后标记 dirty 并立即或在最近 checkpoint 写入 V2。

### 1.6 历史恢复修复尚未完成真机复测

- **严重性**：Medium
- **位置**：申请书 §3-§5
- **证据**：
  - 第一轮真机测试已证明重启恢复失败。
  - `867435e` 修复后仍标记待复测。
  - New、失败草稿、usage 重启累计、WiFi 离页和扫描退出重进也没有结果。
- **影响**：本轮核心目标就是持久化修复，在目标 SPIFFS 实现上尚无通过证据，不能关闭评审。
- **最小修复**：至少完成两轮消息保存→重启恢复→继续多轮上下文、New 后重启为空、掉电 bak 恢复及 usage 重启累计。

### 1.7 chat.log 仍在每次消息确认时整文件重写

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:169-222`
- **影响**：最多 16KB 的历史在每轮回复后被完整重写并执行多次 rename/remove，增加 SPIFFS 写放大和 UI 任务耗时。
- **最小修复**：后续改为 append journal + 周期 compact，或后台批量保存；本项可作为明确技术债跟踪，不阻断本轮逻辑修复。

## 2. 已通过项

- SPIFFS rename 目标冲突已通过 old→bak、tmp→official、删除 bak 的换入流程修正。
- 当前 CHL2 的 record count、checksum 和边界解析方向正确。
- NVS 测试用例 4 已精确注入第三次 put 失败，并验证非活动槽部分写入、active 不翻转。
- 本环境运行 `scripts/test_nvs_atomic_save.py`，11 项全部通过。
- usage 持久化失败不再清除 dirty count，并设置十秒重试退避。
- Chat/Test usage blob 已使用不同 V2 magic，并能识别 V1 结构。
- 上下文裁剪已在状态行显示消息数和 `trimmed`。
- P0 Sleep 三项及多轮记忆已有真机通过记录。

## 3. 已接受但未消除的安全风险

真实 API Key 仍在源码和 Git 历史中。本轮继续按用户明确的开发期例外处理；外发固件、公共远端或正式发布前必须删除、轮换并处理历史。

## 4. 验证说明

- `python scripts\test_nvs_atomic_save.py`：11 项 PASS。
- `git diff --check 8d273cd..3cdff38`：通过。
- 当前环境没有 `pio` 可执行文件，无法独立复现固件编译。
- 结果文档未包含 API Key 正文。

## 5. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

新增的 request use-after-free 是 High 阻断项；历史格式兼容、bak 回退和 usage 定时落盘仍未闭环。修复并完成持久化真机复测后再提交。
