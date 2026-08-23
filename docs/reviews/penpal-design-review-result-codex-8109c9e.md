# 笔友（PenPal）App 设计文档复审结果（Codex，8109c9e）

- **评审日期**：2026-08-21
- **设计稿**：[penpal-design.md](../penpal-design.md)
- **评审版本**：`8109c9e` — `docs(penpal): design v3 - idempotency key + thread anchor + subject byte budget`
- **评审范围**：v3 修订稿全文，重点复核 v2 Codex P1（创建型 POST 取消）与 P2（subject UTF-8/长度）整改，以及新增 `thread_root_id` 契约。
- **评审结论**：**C 部分接受**。幂等键、精确线程锚定和标题字节边界的整改方向正确；以下边界须在实现前定稿。

## Findings

### P1：SEND 收起等待框后，旧请求成功会清空用户新编辑的草稿

- **位置**：`docs/penpal-design.md` §3.2、§4.2。
- **证据**：创建型 SEND 的 Close 仅隐藏 waitbox、保持 busy、继续等待原任务结果；但
  成功结果按既有语义会清空 COMPOSE。设计未规定在后台 SEND 期间禁用 Title、Body、
  Topic 等编辑控件，也未规定消费结果时将当前草稿与发送 payload 快照比对。
- **影响**：用户 Close 后编辑草稿，旧请求随后成功时可清空新内容；状态 `sent ok` 也会
  被误理解为新草稿已发送。
- **最小修复**：SEND 未完成时锁定 COMPOSE 的编辑与 Topic 选择，或仅当当前 payload
  仍等于任务快照时才清空；若不相等，保留新草稿并单独提示旧投递成功。补充“Close →
  编辑 → 旧结果成功”的真机回归。

### P2：`pen_pal_id=null` 的只读线程没有已定义、可构造的精确查询

- **位置**：`docs/penpal-design.md` §2、§2.1、§4.4。
- **证据**：精确读取接口写为 `GET /emails?pen_pal_id&thread_root_id`，但 mailbox
  明确允许 `pen_pal_id=null`，并要求该残留行能够打开只读线程。设计把 null 映射为
  `pal_id=0`，但没有服务端契约或实测证明 `pen_pal_id=0` 能按 `thread_root_id`
  查询该线程。
- **影响**：删除笔友关系后的 Sophie 等残留邮件行可能仍显示在 HOME，却无法打开阅读，
  与“只读”承诺不符。
- **最小修复**：明确并实测服务端接受仅 `thread_root_id` 的、按当前用户授权的读取；或
  定义 deleted-pal 专用查询参数/保留真实关系 id，并把该场景加入 API 与真机回归。

## 已通过项

- 创建型 POST 使用 `Idempotency-Key`，重复投递可重放同一封信，消除了 v2 把客户端
  代次丢弃误称为服务端取消导致的双发风险。
- `thread_root_id` 取代 subject 作为线程读取和回信锚点，能区分同主题的独立线程。
- Title 改为 56 字节、UTF-8 边界截断；发送侧使用 canonical `std::string`，修复了
  60 字符与 64 字节显示缓冲不一致的问题。
- `scripts/remote_api_demo.py` 已通过 Python 语法检查。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 仅保留设计稿
- [x] C. **部分接受**
- [ ] D. 拆分提交

在 P1、P2 修订并完成相应回归后，v3 可作为 PenPal 实现基线。
