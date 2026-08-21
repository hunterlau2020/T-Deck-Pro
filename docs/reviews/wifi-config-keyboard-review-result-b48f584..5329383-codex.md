# 评审结果：PenPal App 实现全量（Codex）

- **评审日期**：2026-08-21
- **申请文件**：[wifi-config-keyboard-review-request-b48f584..5329383.md](wifi-config-keyboard-review-request-b48f584..5329383.md)
- **评审提交**：`b48f584`、`16c13e3`、`b231dd3`、`5329383`
- **评审范围**：PenPal API、R9 残留线程读取、PenPal UI 与菜单入口；按申请说明将首末 commit 都纳入核对。
- **评审结论**：**C 部分接受**。存在两个 P1 核心流程问题和一个 P2 资源耗尽风险，修复并完成相应回归后再接受。

## Findings

### P1：Send 和 Polish 将非平凡 `std::string` 对象清零

- **位置**：`examples/pda2/ui_penpal_write.cpp` 的 `ppw_payload_build()`；`examples/pda2/penpal_api.cpp` 的 `penpal_polish()`。
- **证据**：前者对含有 `subject`、`content` 两个 `std::string` 的 `pp_send_req_t` 使用 `memset`，后者对含 `improved` 的 `pp_polish_t` 使用 `memset`。随后代码继续对这些字符串赋值或追加。
- **影响**：字节清零会破坏已构造的 C++ 字符串对象，后续赋值、扩容或析构均为未定义行为；首次 Send 或 Polish 即可能导致堆损坏、异常重启或不可预测结果。
- **最小修复**：以值初始化/赋值重置对象，例如 `*out = pp_send_req_t{};` 和 `*out = pp_polish_t{};`；不得对任何含 `std::string` 的对象使用 `memset`。补充 Send、Polish 成功与失败路径的回归。

### P1：首次进入已配置的 PenPal 会将自动同步结果丢弃并永久保持 busy

- **位置**：`examples/pda2/ui_scr_mrg.c` 的生命周期调用顺序；`examples/pda2/ui_penpal.cpp` 的 `pp_create()`、`pp_entry()` 与 stale-result 分支。
- **证据**：屏幕管理器先执行 `create()`、后执行 `entry()`。`pp_create()` 已调用 `pp_home_sync(false)` 并以当前 generation 发起任务；紧接着 `pp_entry()` 增加 `s_pp_gen`。任务完成后结果 generation 必定不匹配，`pp_consume()` 直接丢弃结果；该分支不执行 `pp_release_busy()`。
- **影响**：配置完成后每次新建 PenPal 屏幕，自动同步都会被判定为过期，而 `s_pp_busy` 留为 true；用户无法 Sync、打开线程、取主题或发送信件。
- **最小修复**：将自动同步移至 `pp_entry()`，在设置 `s_pp_active = true` 且递增 generation 后启动；或调整 generation 生命周期，确保 create 阶段启动的请求不会被 entry 自行作废。补充“已配置首次进入 → 自动同步完成 → 可继续操作”的真机回归。

### P2：READ 型 Close 未实际取消任务，允许并发堆积最长 180 秒的请求

- **位置**：`examples/pda2/ui_penpal.cpp` 的 `pp_wait_close_cb()`、`pp_task_func()`、`pp_start()`。
- **证据**：READ Close 仅递增 generation 并清除 `s_pp_busy`，没有向既有任务或 HTTP 请求传递取消信号；用户随即可以再发起请求。每次请求都会创建一个 8 KiB worker task，LLM 端点超时可达 180 秒。
- **影响**：反复 Close 后重试可并发积压多个仍在运行的网络任务，占用任务栈、堆和连接资源，最终出现任务创建失败、内存不足或系统不稳定。
- **最小修复**：实现可中止的传输并等待旧任务退出，或在逻辑取消后仍维持单飞/并发上限，直至旧任务实际结束。应回归连续 Close/重试及断网下的资源占用。

## 已通过项

- `penpal_get_thread()` 对 R9 残留线程省略 `pen_pal_id` 的路径与本轮申请所述服务端契约一致；UI 对该类线程隐藏 Reply 的方向正确。
- Send 的 `Idempotency-Key` 设计、后台 Send 时的编辑锁及 payload 比对清稿思路保持正确，但当前 P1 必须先修复才能验证该流程。
- 菜单第三页及 PenPal 注册路径可达；范围内 `git diff --check` 通过。

## 验证说明

- 已静态核对 `b48f584` 至 `5329383` 的完整差异及调用链。
- 本环境未安装 `pio`，无法独立复跑申请中声明的 PlatformIO 编译；未对工作区作代码修改。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**
