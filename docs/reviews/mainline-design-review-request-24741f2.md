# 评审申请（复审）：mainline 设计 v3 —— P1-1 修正 delta 复审（L1 / G-开工）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-10-23
- **关联 commit**：`24741f2` — `docs: mainline design v3 - P1-1 fix (per-type
  field matrix + dual type keys) + qwen P2/P3 batch closure`
- **关联文档**：[`docs/mainline-design.md`](../mainline-design.md)
  （**v3**，本次评审对象 = v2→v3 delta）
- **前置结果**（`5b32605` 五方）：claude **C**（改判）/ codex **C** /
  qwen **C** / deepseek **A** / grok **A**——2-3 分歧，C 方证据为后端
  源码逐行核实，本处置按 C 方 required action 修正。
- **评审依据**：`docs/review_guide.md` v1.3 设计稿口径 + **§2.5 修复轮
  delta 复审**。**复审范围（codex 划定）**：P1-1 所涉 §3/§4/§5 的字段
  矩阵与题型映射 delta + qwen P2/P3 同批项；**不重开全稿**。

## 1. 修正对照表（发现 → v3 落点）

| 发现（出处） | v3 落点 | 状态 |
|---|---|---|
| **P1-1** 白名单与题型载荷自相矛盾（qwen/codex/claude 同源） | §3.2 条件化显示矩阵（五题型行：允许集/丢弃集逐型定义，UI 与解析器共用一张表） | ✅ |
| **判型双键**：输出题 = `segment=="output"` + `task`，非 `task_type`（codex/claude） | §3.1 判型双键节 + §10 固定判型顺序 | ✅ |
| **P2-1** cloze 高频题型（25-50%）被"未知→skipped"系统性压分（qwen） | §2/§3.2/§4.4 完形=textarea 一等题型（与拼写组件同构） | ✅ |
| **P2-2** finish 409 空表 `missing_item_ids:[]` = 输出强制律，"取表首"未定义（qwen） | §3.3 finish 行：空表→显示判据 message + 跳回 `segment="output"` 项 | ✅ |
| **P2-3** submit/finish 同样抛 409 `plan_superseded`（qwen） | §3.3 两行补 409 列：会话作废 + 回 HOME | ✅ |
| **P3-1** `recommend=null` 第四态（qwen） | §3.3/§4.1 HOME 四态 + "create a plan on web" 指引 | ✅ |
| **P3-2** `passed/quality` nullable（qwen） | §3.3 submit 行标注 + 状态行 combined/score_write 兜底 | ✅ |
| **P3-3** start/review-start 载荷超枚举（qwen） | §3.2 枚举外字段一律丢弃（已核实无答案泄漏面） | ✅ |
| scene_words 半答案疑虑（申请焦点 1） | qwen 焦点答复采纳：完课判据要求引用且必须可见（`studyplan.py:1888-1897`），**合法脚手架**入渲染集 | ✅ |

## 2. v2→v3 结构变化

- §3 重构为三小节（判型双键 / HW-MAINLINE-1 矩阵 / 端点表修订）；
- §4.4 作答枚举加完形；§4.1 HOME 四态；§7 回归清单对应加项；
- §8 风险表把已闭合项划掉（留痕）；§10 实现合同加"矩阵落地为共享
  常量表 + 判型顺序固定 + JSON 容错矩阵交付物"。

## 3. 开发者自评（最没把握 ≤3）

1. 矩阵逐题型字段集抄自评审给出的服务端 builder 证据，我未独立复核
   `studyplan.py:1439-1540/1683-1695` 的最新工作树状态（后端仓库有
   未提交改动的报告）——若服务端后续改装配，防线是"枚举外丢弃 +
   未知降级"，但矩阵本身可能滞后。
2. cloze 作答语义（`cloze_forms` 判分=服务端）设备端只送裸文本——
   词形变化（如三单）是否该本地提示，v1 不做是否影响完课率。
3. 空表 409 跳回 output 项后，若用户改用 skipped 提交输出项，输出
   强制律是否放行（服务端 skipped 是否豁免引用判据）——未核实，
   已列入 §8/demo 验证项。

## 4. 审批（设计稿口径，delta 复审）

- [ ] **L1 DOC-ALIGNED 达标**（G-开工）
- [ ] A. 接受，据此开工（v1.30 mainline 编码）
- [ ] B. 退回重构
- [ ] C. 部分接受（列残余，不倒逼 v4）
