# 笔友（PenPal）App 设计文档复审结果（Codex，0ca4f4b）

- **评审日期**：2026-08-21
- **设计稿**：[penpal-design.md](../penpal-design.md)
- **评审版本**：`0ca4f4b` — `docs(penpal): v3.1 P2 settled by live test - hide null-pal rows, R9 ticket`
- **评审范围**：v3.1 修订稿；复核 Codex v3 复审 P1（后台 SEND 误清草稿）和 P2（null 笔友残留线程查询）的整改。
- **评审结论**：**A 全量接受**。

## 核对结果

### P1：后台 SEND 的草稿保护已闭合

- Send 启动即锁定 Title、Body、Pick 与 Tips；Close 仅收起 waitbox，不会解除该锁。
- 结果消费时再比对发送 payload 快照：相等才清空 COMPOSE；不相等则保留草稿并提示
  `previous send ok`。
- 失败或超时会保留草稿并解锁，真机回归已明确覆盖“Send → Close → 编辑受锁 → 旧结果
  成功”的完整时序。

### P2：null 笔友残留行的行为已与服务端能力对齐

- 文档记录了 GET-only 实测：`pen_pal_id` 缺失为 422、`pen_pal_id=0` 为 400，故该类
  行当前没有可读取通道。
- HOME 过滤 `pal_id == 0` 行，避免显示不能打开的线程；`pal_id=0` 哨兵仅保留作防御性解析。
- 服务端能力缺口已登记为 R9：支持当前用户授权范围内的 `thread_root_id` 单独读取；上线后
  恢复“显示 + 只读”并重验。

## 已通过项

- v3 的 `Idempotency-Key`、`thread_root_id` 精确锚定、56 字节 UTF-8 标题边界仍保持一致。
- R9 是已识别且不阻塞当前客户端基线的服务端跟踪项，不构成客户端设计缺陷。
- 设计稿差异格式检查通过。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 仅保留设计稿
- [ ] C. 部分接受
- [ ] D. 拆分提交

v3.1 可作为 PenPal 实现基线；R9 按服务端排期跟踪。
