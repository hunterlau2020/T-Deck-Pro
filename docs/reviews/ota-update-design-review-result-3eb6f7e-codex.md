# OTA 远程升级设计 V4 评审结果（Codex）

- **评审对象**：`docs/ota-update-design.md`（V4）
- **评审锚点**：`3eb6f7e093da1c43e9de80733bc3124c6fa87243`
- **对比范围**：`45d11cf..3eb6f7e`
- **评审级别**：L1 DOC-ALIGNED
- **结论**：**C（需修订后再进入实现）**

V4 已将 V3 中“`lv_async_call` 不代表首帧上屏”、8 秒 TWDT 与现有
`setup()` 阻塞路径冲突，以及下载失败后 `seq` 不能重试等问题写入正文并给出
正确方向；但清单签名对象仍自包含 `sig`，使任何正常清单都无法验签，故不能
达到 L1 门槛。

## Findings

### P1：签名对象包含 `sig` 本身，形成不可构造的自指验签输入

- **位置**：[docs/ota-update-design.md:96](../ota-update-design.md#L96)
- **证据/反例**：示例清单只有 `version`、`url`、`size`、`sha256`、`seq`、
  `notes`、`sig` 共七项；正文同时规定“签名对象 = 七字段按序拼接”与
  “`sig` = ECDSA 签名”。因此发布端必须在签名前知道最终 `sig`，而最终
  `sig` 又由该签名输入决定。若对空值/占位 `sig` 签名，设备读取最终 base64
  值后拼出的输入不同，验签必失败。
- **影响**：正常用户 Check 即验签拒绝，OTA 完全不可用。
  `B/1/有声 → P1`。
- **两轴**：`VERIFIED`（DOC：字段表与签名定义可直接推出） /
  `VIOLATES`（签名/验签协议不可满足）。
- **最小修复**：签名对象严格列为六个业务字段
  `version,url,size,sha256,seq,notes`，明确 **不含 `sig`**；再固定 UTF-8、
  整数十进制、SHA-256 hex 大小写等字节编码，并加入一组发布脚本与设备端
  互验的测试向量。

### P2：TWDT API 的订阅对象和生命周期未形成可直接实现的合同

- **位置**：[docs/ota-update-design.md:195](../ota-update-design.md#L195)、
  [docs/ota-update-design.md:200](../ota-update-design.md#L200)、
  [docs/ota-update-design.md:206](../ota-update-design.md#L206)
- **证据/反例**：Arduino-ESP32 2.0.14 中 `loopTask` 是
  `void loopTask(void *)` 的入口函数，而任务句柄是 `loopTaskHandle`；
  `esp_task_wdt_add(loopTask)` 不能作为 `TaskHandle_t` 参数直接编译。
  在 `setup()` 内应以 `NULL` 表示当前任务，或明确使用
  `loopTaskHandle`。同时正文要求自证后 `delete(loopTask)`，却要求在
  EPD `nextPage` 循环持续插入 `esp_task_wdt_reset()`；自证后正常 EPD 刷新
  会对未订阅任务 reset，返回 `ESP_ERR_NOT_FOUND`。
- **影响**：实现可能直接编译失败；即使人工改过句柄，后续 EPD 正常刷新会
  持续走错误 API 路径，WDT 自证合同不能被稳定验证。
  `B/2/有声 → P2`（需按设计实施该路径才触发；尚未落入代码）。
- **两轴**：`VERIFIED`（SPEC：TWDT API；CODE：Arduino 2.0.14 的任务函数/
  句柄定义） / `NO_CONTRACT`（订阅期和 reset 期未一致）。
- **最小修复**：写出精确调用：在 `setup()` 使用
  `esp_task_wdt_add(NULL)` / `esp_task_wdt_delete(NULL)`；以
  `s_pending_verify && s_loop_wdt_subscribed` 守卫所有具名 reset 点，或在
  自证后停止这些 reset 调用。把失败返回值的处理、日志及回滚行为写入实现合同。

### P3：两机单次校准不能成为所有正常启动场景的 WDT 上界

- **位置**：[docs/ota-update-design.md:188](../ota-update-design.md#L188)、
  [docs/ota-update-design.md:191](../ota-update-design.md#L191)、
  [docs/ota-update-design.md:269](../ota-update-design.md#L269)
- **证据/反例**：规则仅为“两台机各打点”后取整段最大值乘二；文首自己列出
  慢 SD、GPS 冷启动重试等尚未测到的变量。一次健康启动样本不是这些路径的
  上界，慢卡或外设重试仍可能使健康新镜像超过 T、被 TWDT 重启并回滚。
- **影响**：特定外设/存储状态下健康镜像升级后被错误回滚，且必须改发新 `seq`
  才能重试。
  `B/3/有声 → P3`；接入更多硬件组合或常态出现慢启动时回升为 P2。
- **两轴**：`PARTIALLY_VERIFIED`（DOC/SIM：现有阻塞路径已盘点） /
  `NO_CONTRACT`（缺少场景覆盖和合格阈值）。
- **最小修复**：列出两机、无/有 4G、GPS 冷启动、慢 SD、失败重试等矩阵；
  每格规定重复次数、记录格式、最大 time-to-valid 和 T 的接受判据。真机
  证据写入 `ota-baseline.md` 后才能启用真实 OTA。

## 已核实到位项

- 将自证门控改为一次捕获 `ui_disp_full_refr_seq()`、后续轮询
  `ui_disp_flush_done_seq()`，与现有 Wi-Fi 扫描覆盖层的首帧门控模式一致。
- `esp_task_wdt_init(T, true)` 在 ESP-IDF 4.4 中对已经初始化的 TWDT 会更新
  超时与 panic 配置；它不是“重复初始化必失败”。
- `last_seq` 从 Check 阶段移至 `Update.end(true)` 成功后，已经消除下载/
  取消/掉电后同一清单无法重试的旧问题。

## 验证与对称三格

| 检查 | 结论 |
| --- | --- |
| 全区间 diff | 已审 `45d11cf..3eb6f7e`；`git diff --check` 通过。V4 的技术变更集中在设计稿，未包含 OTA 实现。 |
| 独立复跑 | 静态核对现有 EPD 序列实现、Arduino-ESP32 2.0.14 与 ESP-IDF 4.4 TWDT API；本环境无 PlatformIO 与真机，BUILD/HW 均为 `UNKNOWN`。 |
| 作者最没把握处反例 | 针对 WDT 窗口构造慢 SD/GPS 冷启动超过单次实测 ×2 的健康启动反例；针对 EPD reset 点构造自证后未订阅仍持续刷新反例。 |

## 评审自审（`docs/review_guide.md` §9）

- 【D】字段/键名一致性：**未通过**（签名“七字段”与 `sig` 自身矛盾，P1）。
- 【D】代码块可照抄：**未通过**（`esp_task_wdt_add(loopTask)` 不是明确可用的
  `TaskHandle_t`，P2）。
- 【D】声明有证据：**通过并标注范围**（硬件校准与回滚均仍为 `CLAIM_ONLY` /
  `UNKNOWN`，没有当作真机已验证）。
- 【ALL】每条 P1/P2 有坐标与反例：**通过**。
- 【ALL】未将计划视为实现：**通过**。

**审批：C**。P1 修复并补全 TWDT 生命周期合同后，才可进入实现阶段。
