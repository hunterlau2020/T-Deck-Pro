# 评审结果：2026-08-07-20 评审遗留 3 项 P2 修复（3f654a5..4c3a331）

- **评审日期**：2026-08-22
- **申请书**：[wifi-config-keyboard-review-request-3f654a5..4c3a331.md](wifi-config-keyboard-review-request-3f654a5..4c3a331.md)
- **hash 映射**（filter-repo 重写，当前 HEAD 为准）：
  `3f654a5`→**`6d26699`**（set_srcdir）、`11b7ec3`→**`52f709e`**（GPS）、
  `a06a1f9`→**`950fcfe`**（TLS 开关）、`4c3a331`→**`1473ef9`**（issue_list 台账）
- **评审人**：Kimi
- **评审结论**：**A. 全量接受**（含 1 项已修复缺陷登记、1 项 Low 影响面提示）

---

## 1. Findings

### 1.1 `6d26699` set_srcdir 外部 PLATFORMIO_SRC_DIR 优先 — 通过

- **严重性**：✅ 通过
- **位置**：`script/set_srcdir.py:27-29`
- **验证**：外部变量优先于 env→example 映射，无变量时回退原路径，交互构建行为
  不变；`os.path.join($PROJECT_DIR, ext)` 在 ext 为绝对路径时 join 语义自动生效
  （POSIX/Windows 均如此），与注释声明一致。修复直指 7.1 根因（矩阵全绿但编错
  源目录），最小改动。

### 1.2 `52f709e` GPS 写侧临界区 — 通过

- **严重性**：✅ 通过
- **位置**：`examples/pda2/peri_gps.cpp:27-30, 98-135, 159-264`
- **验证**：
  - displayInfo() 局部组装 + 末尾单临界区发布 11 字段，Serial 慢操作全在锁外——
    锁内仅 ~11 次赋值（µs 级），无毛刺风险；
  - 局部变量以全局当前值播种（无锁读）——最坏情况是"保持上次值"语义用到略旧的
    种子，无害；
  - 5 个旧 getter 补同一把锁，读写对称；
  - `gps_altitude` 从未写入的空洞（声明+快照拷贝但永远 0）确被补上；
  - 锁声明前移文件顶部，无重复定义。
- **登记（非缺陷）**：旧 getter 逐个加锁，跨 getter 的一致性仍须走
  `gps_get_snapshot()`——API 注释已明示，调用方（GPS 屏）后续应迁快照。

### 1.3 `950fcfe` Trust 自签 TLS 开关 — 通过，含 1 项已修复缺陷

- **严重性**：✅ 通过（1 项 Low 缺陷已由后续 commit 修复，登记不追溯）
- **位置**：`examples/pda2/openai_api.cpp:117-148`、`ui_ai_cfg.cpp:338-362, 552-562, 605-619`、`factory.ino:754-758`
- **验证**：
  - NVS 单键 `ai/tls_insecure` 独立于双槽配置——理由成立（设备级传输设置、不应
    跟随 Test 门控 Save；双槽每次 toggle 需重 staging+flip）。单槽写法与
    weather / per-provider key 先例一致；
  - 键盘 `\v` 手动 `lv_event_send(VALUE_CHANGED)` 的 LVGL 8 语义说明正确
    （程序化 state 变更只发 STATE_CHANGED），触摸与键盘同走一条持久化路径；
  - NVS 失败回滚开关 + msgbox，回滚用 add/clear_state 不再触发事件，无递归；
  - 开机 `openai_tls_apply()` 置于 setup() 末尾、任何 http_utils 消费者之前。
- **缺陷登记（已修复）**：`factory.ino` 局部声明误写 `extern bool
  openai_tls_apply(void);`，与 `openai_api.h` 的 `void` 返回不兼容（跨 TU 声明
  不一致）。**已由 `3475c9b`（GPT 批次 P2-3，已同批评审通过）修复**，当前 HEAD
  无此问题。
- **影响面提示（Low）**：该开关全局作用于**所有** http_utils 消费者（天气、词典、
  WiFi Test 等），但 UI 位于 AI Config 屏且文案仅 "Trust"。ON 时全设备 HTTPS 放弃
  CA 校验，影响面大于控件所在屏的直觉范围。建议后续在开关旁或文档中明示
  "affects all HTTPS requests"。不阻塞本批。

### 1.4 `1473ef9` issue_list 台账 — docs-only

- 与三个修复 commit 一一对应，出列声明与申请书一致。

## 2. 验证说明

- 本评审为静态代码复核（diff 级），未独立编译/烧录；编译、CI 路径编译、烧录、
  开机冒烟、NVS 算法测试 11/11、CA bundle 6 证书均采信申请书 §2 证据。
- 申请书 §1.3"已知行为"（`ai` 命名空间不存在时开机一行 nvs NOT_FOUND 噪音）与
  weather/holidays 首开先例一致，定性准确。
- 真机回归 5 项（Trust 开关三态、GPS 刷新、串口格式）仍为 ⏸，逐轮回填。

## 3. 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

三项 P2 修复均直击根因、改动最小、错误路径完备；唯一代码缺陷（extern 返回类型）
已在 HEAD 由 3475c9b 修复。评审 2026-08-07-20 可出列。
