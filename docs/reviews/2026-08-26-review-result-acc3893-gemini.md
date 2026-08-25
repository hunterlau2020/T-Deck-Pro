# 评审结论：acc3893 — PenPal Codex C 三项修复

- **评审人**：Antigravity（Claude Sonnet 4.6 Thinking）
- **评审日期**：2026-08-26
- **评审申请**：[wifi-config-keyboard-review-request-acc3893.md](wifi-config-keyboard-review-request-acc3893.md)
- **关联 commit**：`acc3893` — `penpal: close Codex C findings (memset UB, entry auto-sync, worker cap)`
- **原始结果来源**：Codex `wifi-config-keyboard-review-result-b48f584..5329383-codex.md`（C 部分接受，2×P1 + P2）

---

## 结论：**A 全量接受**

---

## 1. P1-a：memset UB 修复（`penpal_api.cpp`、`ui_penpal_write.cpp`）

**根因核实**：
- `pp_polish_t` 含 `std::string`（`improved` 字段），`ppw_payload_build` 的 `pp_send_req_t` 含 `subject`/`content`——对含非 POD 成员的结构 `memset` 会破坏内部 std::string 指针，UB 属实。

**修复正确**：
- 全部改为值初始化 `*out = T{}`。
- `penpal_correction` / `penpal_tips` 虽本身是 POD（memset 原本安全），统一规则（无 memset）合理——规则越简单越不出错。
- 「全量复查」记录已做，仅剩 4 处 `memset` 均为安全场景（`lv_obj_t*` 指针数组）。

---

## 2. P1-b：create 期 auto-sync 被 entry gen++ 作废 + busy 泄漏（`ui_penpal.cpp`）

**根因核实**：
- `scr_mgr_register` 开机即调 `create()`，此时 gen=G；`pp_entry()` gen++ 后 G+1，create 期的请求结果到达时被判 stale 丢弃；旧 stale 分支 `return` 前没有清 busy——从首次进入即永久卡 busy。属实。

**修复路径正确**：
- auto-sync 移入 `pp_entry()`，在 `gen++`/`s_pp_active=true` 之后发起（携带将被消费的代次）。
- `s_pp_autosynced` 门控仅首次执行；`pp_destroy` 和 Cfg 保存成功均复位该标志。
- 时序：`entry(gen++) → auto-sync(gen=G+1) → 结果到达 → gen 匹配 → 正常消费 + 释放 busy`。

**自查顺带修复（新增）**：
- 后台 SEND 退出屏幕 busy 泄漏路径：stale drop 分支增加 `if (s_pp_busy && res->gen == s_pp_busy_gen) s_pp_busy = false;`——只有当该请求是当前 busy 持有者时才清；不会误清其他请求的 busy。正确。

**小瑕疵（可接受）**：
- `s_pp_busy` 为 `volatile bool`，对非原子读写在双核 ESP32 上理论上存在竞争，但与现有代码一致（该模式在 AI Chat 批次评审已接受），不新增风险。

---

## 3. P2：READ Close 后僵尸任务并发堆积（`ui_penpal.cpp`）

**方案选择**：
- 方案 B（并发上限 ≤ 2）对比方案 A（可中止传输）侵入更小，选择合理；可中止传输登记为后续可选。

**实现正确**：
- `__atomic_add_fetch` 在 `xTaskCreate` **之前** 递增：正确防止快速任务在另一核先于计数完成导致下溢。
- `xTaskCreate` 失败后立即 `__atomic_sub_fetch`：覆盖了创建失败路径，不漏。
- `pp_task_func` 末尾 `__atomic_sub_fetch` 在 `vTaskDelete(NULL)` 之前执行，不存在任务自删后继续访问变量的问题。
- chained legs 绕过闸门：HOME 链式脚属于同一单飞，不应被闸门拒绝，正确。

---

## 4. 遗留项

无新增遗留项。可中止传输（HTTP 层 cancel）已在申请书中登记为后续可选。

---

## 5. 审批

- [x] **A. 全量接受**

**审批人**：Antigravity  
**审批日期**：2026-08-26
