# 评审结果：OTA 固件远程升级 V3（Codex）

- **评审日期**：2026-09-13
- **申请文件**：[ota-update-design.md](../ota-update-design.md)
- **评审提交**：`45d11cf`
- **评审范围**：V3 设计稿及 `be6a52c..45d11cf` 全区间文档变更；当前 HEAD
  `a74f316` 对该设计稿仅新增评审要求，未改变 V3 技术方案。
- **设计结论**：**未达到 L1 DOC-ALIGNED；C 退回修订**。

## Findings

### P1：异步回调不能证明首帧 EPD 已完成渲染

- **位置**：[docs/ota-update-design.md:229](../ota-update-design.md#L229)、
  [examples/pda2/factory.ino:241](../../examples/pda2/factory.ino#L241)
- **证据（SIM）**：设计把 `lv_async_call` 在 loop 首个
  `lv_timer_handler` 内执行视为“首帧 UI 已渲染”。现有显示路径实际由
  `flush_timer_cb` 之后调用 `display->nextPage()`，再把
  `disp_flush_done_seq` 写为完成；异步回调没有等待该完成序列。
  因而新镜像可先被标 VALID，随后才在首帧 EPD 刷新卡死或崩溃，回滚窗口已关闭。
- **影响**：`A/3/静默 → P1`。故障新固件需要人工 USB 恢复。
- **两轴**：`PARTIALLY_VERIFIED(SIM；无真机)`；`CONFORMS`（当前无实现，
  不构成已实现的契约违例）。
- **最小修复**：保存 `disp_full_refr_seq()` 返回值，只有
  `disp_flush_seq_done()` 达到该值后才自证；真机增加首帧刷新失败/超时用例。

### P1：订阅 TWDT 不等于保证八秒后发生复位

- **位置**：[docs/ota-update-design.md:222](../ota-update-design.md#L222)
- **证据（DOC/SPEC）**：方案只写“loopTask 显式订阅
  `esp_task_wdt`（8s）”，未定义或重配 timeout、panic/reset 行为，也未要求检查
  init/add/reset/delete 的返回值。当前仓库没有 `esp_task_wdt_*` 实现，目标 SDK
  配置文件也未入库，实际配置为 `UNKNOWN`。TWDT 的订阅与超时触发重启是两个
  独立条件；ESP-IDF 默认可仅报告 watchdog 超时而不调用 panic/reset。
- **影响**：`A/3/静默 → P1`。setup 死循环可能仍保持 PENDING_VERIFY 而不重启，
  无法触发 bootloader 回滚。
- **两轴**：`PARTIALLY_VERIFIED(DOC/SPEC；目标 sdkconfig UNKNOWN)`；
  `NO_CONTRACT`（拟新增 OTA 任务的 WDT 生命周期尚未写入现有异步契约）。
- **最小修复**：按 Arduino-ESP32 2.0.14 的 API 显式初始化/重配为 8 秒且超时必
  panic/reboot，检查全部返回值；busy-loop、`delay()` loop 与外设初始化卡死均须
  真机记录 reset 来源与 pending → aborted → previous-valid 状态链。

### P2：下载开始前提升 `last_seq` 会永久拒绝失败下载的重试

- **位置**：[docs/ota-update-design.md:99](../ota-update-design.md#L99)
- **证据（SIM）**：Check 得到 seq=42 → 实际下载开始前写 `last_seq=42` → 断网、
  取消或掉电 → 旧固件仍运行 → 再次 Check 同一合法清单时 `42 <= last_seq`，
  被当作回放/降级拒绝。用户不能重试同一发行版。
- **影响**：`B/2/有声 → P2`。正常失败恢复路径被破坏。
- **两轴**：`VERIFIED(SIM)`；`NO_CONTRACT`（NVS 中 confirmed/pending 序列的
  原子状态机未定义）。
- **最小修复**：仅在新镜像自证成功后提升 confirmed sequence；必要时另存
  pending sequence，并在 abort、失败或旧镜像重新启动时清理。

### P2：P-256 公钥格式说明自相矛盾

- **位置**：[docs/ota-update-design.md:106](../ota-update-design.md#L106)
- **证据（SPEC）**：文中称“65B 压缩点或 64B Qx||Qy”；P-256 的 SEC1 压缩点是
  33B，65B 是 `0x04 || X || Y` 未压缩形式。按文字实现会造成发布端和设备端
  公钥解析不一致、全部验签失败。
- **影响**：`D/1/有声 → P2`。
- **两轴**：`VERIFIED(SPEC)`；`NOT_APPLICABLE`（设计尚未实现异步代码）。
- **最小修复**：固定一种唯一格式及长度，例如 64B raw `X||Y`，或 65B 未压缩
  SEC1，并在脚本和固件中断言长度/前缀。

### P3：USB 回退代码块无法直接执行

- **位置**：[docs/ota-update-design.md:275](../ota-update-design.md#L275)
- **证据（DOC）**：`python esptool.py write_flash 0xE000 <8KB 0xFF>` 中的
  `<8KB 0xFF>` 不是文件路径或有效 shell 参数，不能按文档重放。
- **影响**：`E/3/有声 → P3`。故障恢复时操作员无法直接执行兜底命令。
- **两轴**：`VERIFIED(DOC)`；`NOT_APPLICABLE`。
- **最小修复**：提供完整可复制命令，例如适用于已安装 esptool 的
  `esptool.py erase_region 0xE000 0x2000`，并以真机回退演练验证。

## 已核实到位项

- 专用 CA 验证通道和默认禁用明文，不继承 AI Config 的 `tls_insecure`。
- ECDSA P-256 + P1363 `r||s`、签名覆盖 manifest 的 version/url/size/hash/seq/notes。
  P-256 可接受，无需引入 Ed25519 第三方库。
- `s_ota_hw_lock`、取消令牌的块边界 abort、以及 tracked 信任根，已在设计层面
  关闭上一轮对应 P1；尚需实现和真机验证。

## 验证说明

- `git diff --check be6a52c 45d11cf` 通过；已核对完整区间 diff 与当前显示刷新
  序列、低电回调、异步契约。
- 本环境未安装 PlatformIO、无真机；BUILD/HW 均为 `UNKNOWN`。
- ESP-IDF 官方文档说明：未确认的 PENDING_VERIFY 镜像在**下一次 boot**才会回滚；
  TWDT 的订阅、超时和 panic/reset 配置必须分别成立。

## 对称三格

1. **全区间 diff**：已核对 `be6a52c..45d11cf` 的 9 个文件；除设计稿外均为
   评审/规范/台账文档，无 OTA 实现代码隐藏其中。
2. **独立复跑**：静态 DOC/SPEC/SIM 已完成；`pio` 与真机均不可用，未声称 BUILD/HW。
3. **最没把握处反例**：申请没有按 `review_guide.md` §7.1 提供“最没把握”自评，
   因而无法逐项核对；已对其核心新机制构造 WDT 不复位、首帧未完成、失败下载重试三类
   反例。申请应补齐验证状态表与该自评段落。

## 评审自审（适用【D】/【ALL】）

- [x] 15. 每项 P1/P2/P3 均有 `file:line` 和操作序列/规格反例。
- [x] 16–20. 评审范围、环境限制、SIM 边界、全区间 diff 和既有评审事实均已分开标注。
- [x] 21. JSON、NVS `last_seq`、`OTA_URL` 与正文逐项交叉核对。
- [x] 22. 核对 JSON、签名格式和 USB 命令的可执行性。
- [x] 23. 将所有真机/编译/回滚声明标为计划或 `UNKNOWN`，未将其当作证据。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 部分接受
- [x] C. **退回修订**
