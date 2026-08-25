# 评审结果：PenPal C 结论三项修复（Codex）

- **评审日期**：2026-08-25
- **申请文件**：[wifi-config-keyboard-review-request-acc3893.md](wifi-config-keyboard-review-request-acc3893.md)
- **评审提交**：`acc3893`
- **评审结论**：**A 全量接受**。

## 核对结果

- `ppw_payload_build()` 和 `penpal_polish()` 改为值初始化，不再对含
  `std::string` 的对象执行 `memset`，已闭合原 P1 的未定义行为。
- 自动同步已从 `create()` 移到 `entry()` 的 generation/active 设置之后；
  stale 结果若仍持有 busy 也会释放，闭合首次进入及后台 Send 退出后的 busy 泄漏。
- READ Close 后不可中止 worker 以原子 in-flight 计数限制为最多两个；计数在
  `xTaskCreate()` 前递增、创建失败回退，串行 HOME 第二腿不重复计数，满足单飞约束。

## 验证说明

- 已核对原评审提出的 2×P1、1×P2 与本提交的实际修改，修复与问题一一对应。
- 差异格式检查通过。本环境未安装 `pio`，未独立复跑 PlatformIO 编译。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受
