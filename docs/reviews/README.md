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
4. **已通过范围出列（2026-08-17 起，用户要求）**：一旦某范围被评审**接受**，
   后续新申请**只覆盖其后未评审的 commit**——文件名用新的短范围
   （如 `e08bdac..0b43685`），正文不再重复携带已通过的历史 commit 清单、
   映射表和长回滚列表，只保留一行指向已归档的接受结果。避免申请随轮次无限膨胀、
   干扰评审专家。
5. **如何追溯**：任何历史申请都可通过 git 找回，例如
   `git show fa6f830~1:docs/reviews/wifi-config-keyboard-review-request-eecebda.md`
   （fa6f830 为合并 commit，`~1` 是合并前的 tree；替换为具体 hash 即可）。
   合并 commit message 必须写清"吸收的申请文件名"。

## 评审纪律

- 评审申请必须列出：关联 commit id、变更明细、验证状态（含证据）、
  回滚方案、审批事项（A 全量接受 / B 退回修订 / C 部分接受）。
- **单次申请 commit 数 ≥ 10 时应分段评审**（评审要求 §1.11）：按
  5-7 个 commit 一段拆成多个 doc 段分别走 review 流程，避免单 reviewer
  认知负担与超长回滚列表。
- 申请人提交前自查：真机回归清单是否仍全 ⏸？连续两轮全 ⏸ 会被评审标
  High 流程问题。
