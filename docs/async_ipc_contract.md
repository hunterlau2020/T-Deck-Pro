# 异步 IPC 契约（pda2）

> 本文档是 WiFi 页（WiFi Test / Time Sync）、AI Config、AI Chat 四处异步任务的统一合同。
> 评审要求来源：`wifi-config-keyboard-review-result-01f8eac..8b96656.md` 主评审 §1.3。
> 任何新增异步任务必须遵守本契约；违反时以本文件为准。
>
> **适用范围**（主评审 §1.8 补充）：本契约只约束**发起异步 HTTP 请求的屏**。
> Sleep 屏、Keys 屏、GPS 屏、计算器、字典等纯本地屏不创建任务，不适用本契约；
> 它们仍遵守各自的销毁前清理原则（如 Sleep 屏的 timer 句柄保存 + NULL 守卫）。

## 1. 适用范围

| 任务 | 工作函数 | 队列 | UI busy 标志 | 代次 |
|---|---|---|---|---|
| WiFi Test | `wifi_test_task_func` | `s_wifi_test_q` | `s_wifi_test_busy` + `s_wifi_test_busy_gen` | `s_wifi_page_gen` |
| Time Sync | `time_sync_task_func` | `s_time_sync_q` | `s_time_sync_busy` + `s_time_sync_busy_gen` | `s_wifi_page_gen` |
| AI Test | `ai_test_task_func` | `s_ai_test_q` | `s_ai_test_busy` | `s_ai_test_req_gen` |
| AI Chat Send | `chat_send_task_func` | `s_chat_q` | `s_chat_send_busy` | `s_chat_page_gen` |

## 2. 硬性规则

1. **所有权转移**：工作任务 `new` 结果结构体 → `xQueueSend` → 所有权交给 UI 线程；
   UI 线程消费后 `delete`。任何线程不得 `delete` 对方创建的对象。
2. **结果携带代次**：结果结构体第一字段必须是 `uint32_t gen`（页面代次或请求代次）。
3. **busy 仅 UI 线程读写**：任务线程禁止访问 busy 标志。busy 必须携带所属代次
   （`*_busy_gen`），只有代次匹配的结果才能释放 busy —— 旧代结果不得解锁新请求。
4. **队列必须先建后 busy**：`xQueueCreate` 返回值必须检查，失败立即提示且
   不得置 busy、不得启动任务。队列为 `NULL` 时任务不得启动。
5. **每任务独占请求快照**：任务所需的 prompt / base / model / key 等参数在启动前
   复制进任务自有结构体（`new` → 任务内 `delete`）；任务禁止读 UI 拥有的可变缓冲。
6. **任务栈与优先级一致**：栈 1024×8（Time Sync 例外 1024×4，仅做 NTP），
   优先级 1（仅高于 IDLE）；无看门狗依赖。
7. **阻塞策略**：`xQueueSend(..., portMAX_DELAY)` 无限等待。深度恒为 4，且 UI 在
   busy 期间拒绝新请求，故同一时刻最多 1 个在飞结果 —— 队列不会积压，无限等待
   不会实际发生。若未来允许并发请求，必须先重审队列深度与阻塞策略。
8. **页面生命周期**：
   - `destroy()`：`kbd_active=false`、页面代次 +1、busy=false（安全：在飞任务持有
     自己的快照，其迟到结果会被代次校验丢弃）、弹窗关闭（Close 语义同步触发请求
     代次 +1）。
   - `entry()`：页面代次 +1，使上一次访问的迟到结果全部失效。
9. **取消语义**：UI 侧"取消"= 请求/页面代次 +1 + busy=false，任务本身不强制终止
   （HTTPClient 有自身的超时上限）；迟到结果到达后因代次不匹配被丢弃并释放内存。
10. **超时语义**：UI 倒计时必须是**绝对 deadline**，覆盖 NTP 等待（≤5s）+ 连接 +
    读取全程；超时触发时同样递增请求代次，保证"超时后迟到结果不覆盖状态"。

## 3. 已验证路径

- 结果正常到达 → UI 消费 → 释放 busy（路径 1）
- 离页后旧结果到达 → 代次不匹配 → 丢弃（路径 2）
- 请求中 Close/Cancel → 代次 +1 → 迟到结果丢弃（路径 3）
- 队列创建失败 → 提示、不置 busy、不启动任务（路径 4）

## 4. 变更历史

- 2026-08-16：初版。随"WiFi busy 代次 / AI Test 最小 chat-completion /
  AI Chat 每任务快照"整改落地；此前的 `busy` 无代次、任务读全局缓冲等
  违反项已修复（commit 见各模块提交记录）。
