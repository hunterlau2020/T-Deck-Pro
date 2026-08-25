# 评审结果：PenPal C 结论三项修复（Claude）

- **评审日期**：2026-08-25
- **申请文件**：[wifi-config-keyboard-review-request-acc3893.md](wifi-config-keyboard-review-request-acc3893.md)
- **评审提交**：`acc3893`
- **评审结论**：**A 全量接受**（另有一项文档性 Low 留痕，见下）。

## 核对结果

- **P1-a memset UB**：`ppw_payload_build()`/`penpal_polish()` 改为
  `*out = pp_polish_t{}`/`pp_send_req_t{}`；`penpal_correction()`/
  `penpal_tips()` 同步改为值初始化。复核 `pp_fix_t`/`pp_polish_t`/
  `pp_tips_t`/`pp_send_req_t` 均含 `std::string` 成员，`memset` 确实是
  UB，修复正确且已按"任何 `pp_*_t` 一律值初始化"规则统一收敛。
- **P1-b 自动同步被 gen++ 作废 + busy 泄漏**：自动同步已从 `pp_create()`
  移到 `pp_entry()` 的 `s_pp_gen++`、`s_pp_active=true` **之后**，由
  `s_pp_autosynced` 静态标志保证每次访问只发起一次，并在 `pp_destroy()`
  与 Cfg 保存成功时复位；`pp_consume()` 的 stale 分支现在会在丢弃结果
  持有 `s_pp_busy_gen` 时一并释放 busy，闭合后台 SEND 期间退出屏幕的
  busy 永久卡死路径。修复本身经复核是正确的。
  - **但根因表述有误，需要更正（Low，文档准确性）**：申请书 §1.2 与
    `CLAUDE.md` working notes 均称"scr_mgr 在**注册时（开机）**即调
    `create()`（`ui_scr_mrg.c:33`）"。实际读取 `ui_scr_mrg.c` 源码：
    `ui_scr_mrg.c:33` 的 `card->life->create(obj)` 位于
    `scr_mgr_default_style()`，只被 `scr_mgr_push()`/`scr_mgr_switch()`
    调用；`scr_mgr_register()`（真正的"注册"，:110-129）只挂链表节点，
    **不会**调用 `create()`。PenPal 通过菜单点击触发的是
    `scr_mgr_push(tgr->idx, false)`（`ui_deckpro.cpp:282`），且
    `scr_mgr_pop()` 会在 `scr_mgr_remove()` 里调用 `exit()`+`destroy()`
    并 `lv_obj_del()` 整棵屏幕树——也就是说 `create()`/`destroy()` 是
    **每次进出都跑一遍**，不是"开机建一次、之后复用"。
    - 这不影响本次修复的正确性：`entry()` 每次都紧跟在 `create()`
      之后执行、`gen++` 每次都先于自动同步发起，无论"一次性"还是
      "每次访问"的模型，时序关系都一样，`s_pp_autosynced` 这层静态
      标志在实际的每访问必重建生命周期下等价于"总是执行"（因为
      `pp_destroy()` 每次都会把它复位），只是防御性冗余，不构成 bug。
    - 但错误的根因已经写入 `CLAUDE.md` 的永久记忆，可能误导后续基于
      "PenPal 屏幕控件只建一次"的假设做设计决策（例如控件复用/性能优化
      方向）。建议在 `CLAUDE.md` working notes 补一条更正，或在
      `docs/issue_list.md` 登记，指向本条评审结果核实。
- **P2 READ Close 后僵尸任务堆积**：`s_pp_inflight` 原子计数在
  `xTaskCreate()` **之前**递增（含创建失败回退）、任务结束时递减，
  `pp_task_func()` 的唯一出口路径都会执行 `__atomic_sub_fetch`；
  `pp_start()` 非链式请求在 `inflight >= 2` 时拒绝，链式腿跳过闸门。
  复核逻辑与 Codex 原始 P2 建议方案 B 一致，能把僵尸 worker 堆积上限
  控制在 2 个，符合预期。

## 验证说明

- 已核对原 Codex C 结论的 2×P1 + 1×P2 与本提交实际改动逐一对应；
- 额外核实了 `ui_scr_mrg.c` 的 `scr_mgr_register`/`scr_mgr_push`/
  `scr_mgr_pop`/`scr_mgr_active`/`scr_mgr_remove` 完整状态机源码，
  确认根因表述与实际控制流不符（见上）。
- 本环境未安装 `pio`，未独立复跑 PlatformIO 编译。

## 遗留跟进（Low，不阻塞本次接受）

- 更正 `CLAUDE.md` 关于 "scr_mgr 在注册/开机时调用 create()" 的表述，
  改为准确描述："`create()`/`entry()` 在每次 `scr_mgr_push` 时执行，
  `exit()`/`destroy()` 在每次 `scr_mgr_pop` 时执行；PenPal 屏幕控件树
  每次进出都会完整重建，不是开机建一次"。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受
