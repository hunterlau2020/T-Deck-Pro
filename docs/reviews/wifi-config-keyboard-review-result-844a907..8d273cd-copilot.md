# 第 20/21 轮第三次整改复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..8d273cd.md](wifi-config-keyboard-review-request-844a907..8d273cd.md)
- **关联代码范围**：`844a907^..8d273cd`
- **本次重点整改范围**：`538e6d0..8d273cd`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 usage 的“每 60 秒持久化”没有 timer，低频使用可能永远不落盘

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp:222-251`
- **触发场景**：开机后 60 秒内产生少于 20 次响应，之后不再调用 AI，并关机、复位或进入深睡。
- **证据**：
  - 是否到达 60 秒只在下一次 `ai_usage_accumulate()` 调用时检查。
  - 没有周期 timer、页面退出或深睡前 flush。
  - 第一次响应若发生在 60 秒内且总响应不足 20 次，RAM 增量可无限期保持未保存状态。
- **影响**：实际丢失量不是申请书所称“最多一个 60 秒节流窗口”，而可能是设备整个低频使用周期。
- **最小修复**：增加周期/延迟 flush，或提供 `openai_stats_flush()` 并在深睡、重启及正常退出前调用；dirty 数据存在时保证最迟 60 秒落盘。

### 1.2 stats 持久化失败后仍重置节流状态，重试会再延迟一个完整窗口

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp:229-240`
- **触发场景**：NVS begin/putBytes 因空间或瞬时错误失败。
- **证据**：无论 `saved` 是否为 true，代码都会把 `s_stats_since_persist` 清零并把 `s_stats_last_persist_ms` 更新为当前时间。
- **影响**：失败后的 dirty totals 不会在下一次响应立即重试，断电丢失窗口进一步扩大。
- **最小修复**：仅成功后重置计数和时间；失败时保持 dirty，并采用有限退避重试。

### 1.3 chat.log 格式增加 count 却沿用 CHL1 magic，升级会丢弃旧版历史

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:64-67,169-272`
- **触发场景**：设备已运行 `35e9eae` 版本并保存旧格式日志，升级到 `c90307f`。
- **证据**：
  - 旧格式为 `magic + records + checksum`，新格式为 `magic + count + records + checksum`。
  - 两版仍使用同一个 `CHAT_LOG_MAGIC`（CHL1）。
  - 新 loader 会把旧日志的首个 record 字节当成 count，随后判坏并清空恢复结果，没有迁移路径。
- **影响**：固件升级后已有聊天历史静默消失，与“SPIFFS 持久化”用户预期冲突。
- **最小修复**：新格式使用 CHL2 magic/version；识别 CHL1 并迁移，或明确提示旧历史不兼容而不是按损坏日志处理。

### 1.4 ai_stats blob 扩展为 chat/test 双组但沿用相同 magic，旧统计会被清零

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp:133-179`
- **触发场景**：设备已有 `74c24ff` 单组 `ai_stats_t` blob，升级到 `0328cd2`。
- **证据**：
  - 新结构体尺寸和字段布局已变化。
  - magic 仍为相同的 `STAT`。
  - loader 要求读取长度等于新结构体尺寸，否则直接 memset 为零，没有旧结构迁移。
- **影响**：此前累计的真实聊天用量和费用在升级后丢失。
- **最小修复**：blob 增加明确 schema version；识别旧尺寸并迁移到 chat 组，再写入新版本。

### 1.5 “第 3 次 putString 失败”用例实际仍在第 1 次 putString 就失败

- **严重性**：Medium
- **位置**：`scripts/test_nvs_atomic_save.py:127-133`
- **触发场景**：运行测试用例 4。
- **证据**：
  - 用例设置 `fail_next_puts = 3`。
  - KV 的语义是“接下来的 N 次 put 都失败”，而保存表达式在第一次失败后因 `and` 短路立即退出。
  - 因此本用例只重复覆盖第一次写失败，并未到达第三次 key 写入。
- **影响**：申请书声称覆盖的“第 3 次 putString 失败”没有被实际验证，11/11 PASS 会产生错误完整性信心。
- **最小修复**：记录当前 `put_calls`，设置 `fail_at = current + 3`；同时断言 inactive slot 已有前两个字段但 active 未翻转。

### 1.6 可执行测试验证的是手写镜像，不会检测 C++ 实现与模型漂移

- **严重性**：Low
- **位置**：`scripts/test_nvs_atomic_save.py`
- **证据**：脚本自己实现了 `load_config()` / `save_config()`，没有编译或调用 `examples/pda2/openai_api.cpp`。
- **影响**：C++ 后续发生字段名、短路顺序或回退逻辑变化时，只要 Python 模型未同步，测试仍可能全绿。
- **最小修复**：将存储状态机提取为无 Arduino 依赖的 C++ 单元并直接测试；当前 Python 模型可保留为补充规格测试。

### 1.7 P0/P1 真机回归第五次仍全部待执行

- **严重性**：Medium
- **位置**：申请书 §3-§5
- **证据**：
  - Sleep、重启恢复、New、失败草稿、扫描退出重进和多轮上下文仍无实测结果。
  - 申请书再次声明 P0 Sleep 是合并前置条件。
- **影响**：代码静态复核无法证明 EPD 完成序号、深睡取消、SPIFFS rename 和实际键盘组合在目标硬件上的行为。
- **最小修复**：在本申请回填 P0 Sleep 三项及关键 P1/P2 的结果、串口片段和失败项，不应继续顺延到下一轮。

### 1.8 整文件日志重写和上下文静默裁剪仍是未关闭的体验/寿命问题

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:56-67,169-215,381-426`
- **证据**：
  - 每次确认消息仍重写最多 16KB 的完整日志。
  - 超过 8KB 的旧轮次被静默排除，UI 不提示模型已失去更早上下文。
- **影响**：长期聊天增加 SPIFFS 写放大；用户可能误以为模型仍掌握界面可见的全部历史。
- **最小修复**：采用 append + 周期 compact 或后台批量保存；状态行显示本次发送的 context 消息数及是否发生裁剪。

## 2. 已通过项

- chat.log 新格式已加入 record count，当前格式的空、单条和多条日志可在边界上正确分离 checksum。
- 多轮配对条件已修正，正常 user/assistant 轮次可以进入上下文。
- stats mutex 已改为静态创建，消除了首次惰性初始化竞态。
- Sleep watcher 超时后会取消深睡，只有目标帧完成才启动倒计时。
- Clear 和页面 exit 已同步 retry draft，旧草稿复活问题得到修正。
- Chat/Test usage 已分组统计；正常节流提交由单 blob 完成。
- NVS 状态机脚本可执行，本环境运行得到 11 项 PASS。
- WiFi scan 的共享 pending 读取继续受临界区保护。

## 3. 已接受但未消除的安全风险

真实 API Key 仍在源码和 Git 历史中。编译警告及 `SECURITY.md` 仅是流程补偿控制；本轮按用户明确的开发期例外不作为新增阻断，但外发固件、公共远端或正式发布前必须删除、轮换并处理历史。

## 4. 验证说明

- `python scripts\test_nvs_atomic_save.py`：11 项 PASS，但用例 4 的注入目标不正确。
- 当前环境没有 `pio` 可执行文件，无法独立复现固件编译。
- 结果文档未包含 API Key 正文。

## 5. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

上一轮四个 High 逻辑错误已关闭；本轮主要阻断为持久化升级兼容、usage 落盘保证、测试覆盖真实性，以及长期拖延的真机门禁。修订和回填实测后再关闭本轮。
