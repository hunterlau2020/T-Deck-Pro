# 评审结果：Setting 屏 SD 挂载失败原因提示

- **对应申请**：[wifi-config-keyboard-review-request-a924c4e.md](wifi-config-keyboard-review-request-a924c4e.md)
- **评审专家**：Codex
- **评审日期**：2026-08-21
- **结论**：**C 部分接受**

## 结论

About System 屏增加 SD 失败提示、调用签名同步和零值初始化均可接受。

### P2 — 不应把所有已检测到卡的挂载失败断言为 FAT32 格式问题

**位置：** `examples/pda2/ui_deckpro_port.cpp:361-370`

`SD.begin()` 失败而 `SD.cardType() != CARD_NONE` 并不能证明卡使用了 exFAT 或 NTFS。`cardType()` 仅反映卡的类型或是否有响应；SPI 通信、初始化或 FAT32 卡本身的挂载错误同样会进入该分支。当前 UI 会把这些情况统一显示为 `SD hint: need FAT32`，可能误导用户格式化正常的 FAT32 卡。

**处理要求：** 将该状态改为中性的挂载失败提示（例如 `SD hint: mount failed`），或将 FAT32 仅表达为建议（例如 `try FAT32`），不要作为确定诊断。

## 验证边界

- 静态检查确认调用点和头文件签名一致。
- 真机的 exFAT、无卡和 FAT32 回归项仍由申请书 §2 清单跟踪。

