# 评审工作流（docs/reviews/）

## 目录约定

- 申请文件：`wifi-config-keyboard-review-request-<commit范围>.md`
  - 新申请**必须**带 commit id 范围；**绝不覆盖**旧申请文件。
- 结果文件：`wifi-config-keyboard-review-result-<commit范围>.md`
  - 双评审时加后缀 `-copilot.md`。
- 归档规则：评审结果由评审方直接放入本目录；申请人把设计评审类文档
  （allinone 系列）也在评审后移入本目录。

## 申请合并流程（2026-08-16 起）

1. **何时合并**：同一时间存在 ≥2 份未评审的申请时，申请人应把它们合并为
   一份"合并申请"，避免评审人重复评审重叠范围。
2. **合并文件命名**：用完整 commit 范围，如
   `wifi-config-keyboard-review-request-eecebda..ceade9c.md`；
   文件内按 Part 拆分（Part 1 = 旧申请内容，Part 2 = 新申请内容），
   验证清单与遗留项合并列出。
3. **原文件处理**：被吸收的申请文件用 `git rm` 删除，并在合并申请头注明
   "原文件随本申请删除，git 历史保留（不覆盖）"。
4. **如何追溯**：任何历史申请都可通过 git 找回，例如
   `git show fa6f830~1:docs/reviews/wifi-config-keyboard-review-request-eecebda.md`
   （fa6f830 为合并 commit，`~1` 是合并前的 tree；替换为具体 hash 即可）。
   合并 commit message 必须写清"吸收的申请文件名"。

## 评审纪律

- 评审申请必须列出：关联 commit id、变更明细、验证状态（含证据）、
  回滚方案、审批事项（A 全量接受 / B 退回修订 / C 部分接受）。
- 申请人提交前自查：真机回归清单是否仍全 ⏸？连续两轮全 ⏸ 会被评审标
  High 流程问题。
