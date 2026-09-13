# OTA 远程升级设计 V5 评审结果（Codex）

- **评审对象**：`docs/ota-update-design.md`（V5）
- **评审锚点**：`e0ec3bbf97cd2f2626d2cfd4bb0732b32b32020a`
- **对比范围**：`3eb6f7e..e0ec3bb`
- **评审级别**：L1 DOC-ALIGNED
- **结论**：**C（部分接受；修复以下阻断项后才可开工）**

V5 正确修复了 V4 的签名自指 P1：签名对象已明确为六个业务字段，`sig`
不再参与自身签名。TWDT 的当前任务句柄、订阅后守卫与 EPD 序号单次捕获也已
比 V4 可实施得多。但新的单飞和结果通道尚未给出可收敛的失败/所有权合同。

## Findings

### P2：单飞守卫未规定建任务失败与满结果队列时的释放路径

- **位置**：[docs/ota-update-design.md:140](../ota-update-design.md#L140)、
  [docs/ota-update-design.md:154](../ota-update-design.md#L154)、
  [docs/ota-update-design.md:250](../ota-update-design.md#L250)
- **证据/反例**：设计要求在 `xTaskCreate` 前置 `s_ota_inflight`，任务退出
  才清零，却没有规定 `xTaskCreate` 失败时回滚。另设结果队列深度为 1：Check
  完成后用户离页，旧结果留在队列；再次 Check 时新 worker 的最终结果无处入队。
  若用 `portMAX_DELAY` 发送，worker 卡在发送前，inflight 永不清零；若丢弃，
  又没有规定 UI 收敛/报错路径。之后所有操作都会稳定提示
  `previous OTA op closing`。
- **影响**：一次离页时序或任务资源不足即可让 Firmware Update 屏永久拒绝
  Check/Update，重启前不能恢复。
  `C/2/有声 → P2`。
- **两轴**：`VERIFIED`（DOC：cap=1、队列深度=1、任务退出减计数） /
  `NO_CONTRACT`（create-fail、stale-result、queue-full 未定义）。
- **最小修复**：以原子 compare-exchange 获取单飞；`xTaskCreate` 失败立即
  释放标志和快照。指定最终结果为有界发送或单槽 `xQueueOverwrite`，并要求
  键盘轮询在 active 判断前排空结果队列、丢弃 stale 结果并释放其资源；所有
  worker 退出路径均须释放 inflight/硬件锁。

### P1：通过 FreeRTOS 队列传递 manifest 的对象表示与所有权不成立

- **位置**：[docs/ota-update-design.md:153](../ota-update-design.md#L153)、
  [docs/ota-update-design.md:252](../ota-update-design.md#L252)
- **证据/反例**：文档把
  `ota_check_result_t{..., manifest, ...}` 交给 `xQueueSend` 后描述为
  “worker 结束，所有权移交 UI”。FreeRTOS 队列实际进行字节复制，不会移动
  C++ 对象的所有权。若 `ota_manifest_t` / result 含 `String`、`std::string`
  或指针，worker 栈对象析构后 UI 队列副本含悬空内部指针；若希望按值复制，
  则必须规定为固定长度、平凡可复制的 POD。当前文档没有定义其表示。
- **影响**：网络返回的正常清单即可触发悬空引用、堆损坏或刷入错误快照；没有
  错误提示且可能在确认/更新阶段才崩溃。
  `B/2/静默 → P1`。
- **两轴**：`VERIFIED`（SPEC：FreeRTOS queue 的字节复制语义） /
  `NO_CONTRACT`（manifest/result 的内存布局与析构责任未定义）。
- **最小修复**：二选一并固定到头文件/契约：
  1. 结果和 manifest 全部使用固定长度数组的 POD，加入
     `static_assert(std::is_trivially_copyable_v<...>)` 与长度上限；或
  2. 队列只传 `ota_check_result_t *`，worker `new` 后移交，UI 在所有
     正常/stale/离页分支恰好 `delete` 一次。补充成功、离页、队满、创建失败
     的所有权测试。

### P3：TWDT `add` 失败分支与示例状态更新不一致

- **位置**：[docs/ota-update-design.md:201](../ota-update-design.md#L201)、
  [docs/ota-update-design.md:223](../ota-update-design.md#L223)
- **证据/反例**：正文说 `esp_task_wdt_add(NULL)` 失败要记录并阻断，但示例
  紧随 add 无条件执行 `s_boot_wdt_subscribed = true`。失败时后续具名喂狗点
  会对未订阅任务调用 reset。
- **影响**：错误路径的日志和 WDT 状态不可信；实现/真机验证难以判定是否真正
  有 Layer-2 回滚保障。`E/2/有声 → P3`。
- **两轴**：`VERIFIED`（DOC 内部可直接推出） / `VIOLATES`（守卫语义与
  失败说明相矛盾）。
- **最小修复**：仅在 `add` 返回 `ESP_OK` 后设守卫；否则保持 false、停止
  后续启动或进入明确的不可发布诊断状态。

## 已通过项

- 签名对象恢复为固定顺序的六字段，`sig` 明确不参与输入，消除了 V4 的
  `sig = Sign(..., sig)` 不可构造问题。
- `ui_disp_full_refr_seq()` 被明确为只调用一次并保存的有副作用 API；轮询
  仅读 `ui_disp_flush_done_seq()`，与现有 Wi-Fi/Sleep 的序列模式一致。
- TWDT 调用已从错误的 `add(loopTask)` 改为在 `setup()` 上下文使用
  `add(NULL)` / `delete(NULL)`，并规定自证后关闭 EPD 的 reset 守卫。

## 验证说明与对称三格

| 检查 | 结论 |
| --- | --- |
| 全区间 diff | 已审 `3eb6f7e..e0ec3bb`；`git diff --check` 通过。改动为设计稿和 CHANGELOG，尚无 OTA 实现。 |
| 独立复跑 | 静态核对现有 PenPal 的任务创建/计数和队列消费模式、EPD 序列实现；本环境无 PlatformIO 与真机，BUILD/HW 为 `UNKNOWN`。 |
| 作者最没把握处反例 | 对喂狗路径补构造 `add` 失败后守卫仍为 true 的反例；对最有把握的 inflight/快照链构造离页留下单槽结果、C++ 非平凡对象按字节入队的反例。 |

## 评审自审（`docs/review_guide.md` §9）

- 【D】字段/键名一致性：**通过**（签名六字段与发布步骤一致）。
- 【D】代码块可照抄：**未通过**（TWDT add 失败后的守卫赋值矛盾，P3）。
- 【D】声明有证据：**通过并标注范围**（真机项仍为 `CLAIM_ONLY` /
  `UNKNOWN`）。
- 【ALL】P1/P2 含位置、反例、坐标及修复方向：**通过**。
- 【ALL】未把计划当作实现：**通过**。

**审批：C**。P1 必须当批修复；P2 须在实现合同与测试中闭合后再进入编码。
