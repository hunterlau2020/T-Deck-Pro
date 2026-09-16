# Session batch review result — 71c09e7 (GPT)

- 评审日期：2026-09-16
- 评审人：GPT（Codex）
- 评审申请：[session-batch-review-request-71c09e7.md](session-batch-review-request-71c09e7.md)
- 评审范围：`6eb8245..71c09e7`（27 个文件）
- 依据：[review_guide.md](../review_guide.md) v1.3

## 结论

**C（部分接受）；当前不通过 G-合入。**

本批次包含 CA 根证书、语音上下文、自动休眠、PenPal、Whoami、TTS 与 OTA 等八项主题。未发现 P0；存在一项会使 OTA 回滚保护失效的 P1，须在合入前修复。其余为实现契约 P2，应纳入后续 L2/G-合入核验，不要求为此重开设计评审。

## 必须修复（P1）

### OTA 回滚机制实际未生效

`factory.ino` 未实现 `verifyRollbackLater()`。Arduino 框架会在 `setup()` 前执行其默认路径并把 pending OTA 镜像标为有效，因此新镜像若在 `setup()` 前崩溃，当前 loop 中的自证和 WDT 逻辑无法触发回滚；这与 OTA 设计的延迟验收要求不符。

- 证据：设计要求见 [ota-update-design.md](../ota-update-design.md) 第 305 行；现有自证逻辑见 [factory.ino](../../examples/pda2/factory.ino) 第 662 行与第 870 行；工程内没有 `verifyRollbackLater()` 的实现。
- 修复：实现 `extern "C" bool verifyRollbackLater() { return true; }`，仅在运行时自证完成后调用 `esp_ota_mark_app_valid_cancel_rollback()`；以写入 OTA 槽后的自毁镜像完成真机回滚验证。

## 实现契约（P2）

1. **语音上下文的 API role 非法。** 当前把用户文本填进 `role`，第二轮起不能形成 `user/assistant` 历史。每轮应写入两条标准 role 消息，并以成对消息执行长度裁剪。[ui_voice_ai.cpp](../../examples/pda2/ui_voice_ai.cpp) 第 315、329、353 行。

2. **OTA 单飞计数存在创建后置位竞态。** `xTaskCreate` 成功后才置 `s_ota_inflight`；另一核心上的 worker 可先退出，再被调用方置为 busy，造成后续 OTA 永久拒绝。应在创建前递增，创建失败再回滚，模式可对照 PenPal。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 536–580 行。

3. **覆盖 OTA 结果时泄漏旧指针。** 单元素队列使用 `xQueueOverwrite` 覆盖旧结果指针，却未释放旧对象；消费者无法再取得该对象。覆盖前应取出并删除旧指针，或改用有界、可控的队列策略。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 270 行。

4. **OTA 错误继承 AI 的 TLS 绕过开关。** 用户开启 “Trust self-signed” 后，OTA 也会使用 `setInsecure()`，不再校验证书。OTA 应使用独立 HTTP 客户端并固定 CA bundle 校验策略。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 301、387 行；[http_utils.cpp](../../examples/pda2/http_utils.cpp) 第 37 行。

5. **OTA 期间自动休眠破坏下载界面。** 超过五分钟的下载仍会压入 Sleep 页面；即使之后拒绝深睡，原 OTA UI 的状态已被打断。`ota_busy()` 时应不触发自动休眠。[ui_deckpro.cpp](../../examples/pda2/ui_deckpro.cpp) 第 4977 行。

6. **新增异步任务未登记 IPC 契约。** Whoami 与 OTA 引入任务、队列和结果对象，但 [async_ipc_contract.md](../async_ipc_contract.md) 未覆盖它们。应补齐所有权、排水、销毁与页面生命周期规则。[ui_whoami.cpp](../../examples/pda2/ui_whoami.cpp) 第 199 行；[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 262、270、583 行。

## 已核验的正向项

- PenPal 错误弹窗的 CJK 字体、Reply 标题锁定，以及 Whoami 队列的无条件排水与页面层级，静态检查均合理。
- 菜单 ID 为追加式变更；TTS 开关的默认值、持久化与样式方向一致。
- CA 根证书生成和随附校验脚本结构合理；未在本次离线环境中验证外部站点完整链路。

## 验证边界

- `git diff --check 6eb8245 71c09e7` 通过。
- 本机未安装 PlatformIO，未执行编译；无目标硬件，未执行 OTA、回滚、TLS、休眠或外设真机验证。上述真机行为须在修复后补测。
