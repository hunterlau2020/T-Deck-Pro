# 评审申请书：第三批评审修复（review 2026-08-07-20 剩余 3 项 P2）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-21（批次完成时间 01:21；本文件为补写，见下方"补写说明"）
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 4 个，连续区间）：
  - `6d26699` — `build: honor external PLATFORMIO_SRC_DIR in set_srcdir`（P2 CI 源目录）
  - `52f709e` — `gps: publish all fields under the snapshot mux`（P2 GPS 快照锁）
  - `950fcfe` — `ai-cfg: add the advertised Trust self-signed TLS toggle`（P2 TLS 开关）
  - `1473ef9` — `docs(issue_list): section 7`（台账，docs-only）
- **背景**：评审 `pda2-review-result-2026-08-07-20.md` 共 4 项发现——P1（CA 证书损坏）
  已由更早的 §5.3 修复出列，本批关闭其余 3 项 P2；至此该评审全部出列。
- **已出列范围**（均 Codex 全量接受）：第 29-31 轮（最近一轮 `764e7bf..980b6df`）。
- **命名说明**：文件名 = 正文"关联 commit"首末 id（**含两端**，非 git 区间记法）。
- **补写说明**：本申请文件在批次完成时漏写（簿记疏漏），`de78338`、
  `c27cb39..3475c9b` 两份后续申请及 CLAUDE/TODO 均已按本文件名引用；
  2026-08-21 深夜补齐后引用闭合。批次内容、commit 时间均以 01:21 原批为准。
- **对应结果**（补写后查收）：[wifi-config-keyboard-review-result-3f654a5..4c3a331-codex.md](wifi-config-keyboard-review-result-3f654a5..4c3a331-codex.md)
  —— **A 全量接受**（2026-08-21 到达）。结果文件按**清洗前旧哈希区间**命名
  （`3f654a5..4c3a331`），对应本申请 `6d26699..1473ef9`，即同一批次。
- **哈希映射提示**：issue_list §7.1/7.2/7.3 回填的修复 id
  （`3f654a5`/`11b7ec3`/`a06a1f9`）为 **filter-repo 历史清洗前**的旧哈希，
  按 `.git/filter-repo/commit-map` 分别映射到本批 `6d26699`/`52f709e`/`950fcfe`，
  同一改动、两个名字，评审时以本文件的新哈希为准。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，已连接）

---

## 1. 变更明细

### 1.1 set_srcdir 尊重外部 `PLATFORMIO_SRC_DIR`（`6d26699`，P2-1）

- **问题**：CI 矩阵每个 job `export PLATFORMIO_SRC_DIR=examples/xxx` 后裸跑
  `pio run`，但 `script/set_srcdir.py` 无条件 `Replace(PROJECT_SRC_DIR=...)`，
  默认环境 `T-Deck-Pro` 一律被改指 `examples/test_GPS`——矩阵"全绿"但实际
  没编被选中的示例。
- **修复**：外部传入的 `PLATFORMIO_SRC_DIR`（项目相对路径）优先；未设置时
  才走 env→example 映射（交互式构建行为不变）。
- **验证**：`PLATFORMIO_SRC_DIR=examples/factory pio run -e T-Deck-Pro`
  生成 `factory.ino.cpp.o`（修复前为 `test_GPS.ino.cpp.o`）——commit message
  记录在案。

### 1.2 GPS 写侧并入快照锁（`52f709e`，P2-2）

- **问题**：`gps_get_snapshot()` 读侧持 `s_gps_snapshot_mux`，但 `gps_task` 的
  `displayInfo()` 在另一核**无锁写**同一批 `gps_*` 全局——双核 ESP32-S3 下
  快照仍可能混合两次定位的读数，原子快照契约形同虚设。
- **修复**：
  - `displayInfo()` 先在局部变量组装本次更新（Serial 打印全走局部变量，
    **锁内无慢操作**），最后一次临界区发布全部 11 个字段；
  - 无效字段保持上次值（语义不变）；
  - 旧的 5 个 `gps_get_*()` 单字段 getter 补上同一把锁；
  - 顺手补上从未被写入的 `gps_altitude`（此前恒 0——声明了、也复制进快照，
    但没人写）。

### 1.3 Trust 自签 TLS 开关落地（`950fcfe`，P2-3）

- **问题**：`http_utils.h` 注释指引自签/私有 CA 用户去 "AI Config screen
  'Trust self-signed' toggle"，但该控件不存在、也无人调 `http_set_tls_mode()`——
  自签端点永远走 CA 校验、必然失败。
- **修复**：
  - `openai_api`：`openai_tls_insecure()/openai_tls_apply()/openai_tls_set()`
    持久化为**独立单键** NVS `ai`/`tls_insecure`——刻意不进双槽
    base/model/key 配置：这是设备级传输设置、作用于所有 http_utils 消费方，
    不应跟随 Test 门控的 Save 流程；
  - `factory.ino`：setup() 末尾 `openai_tls_apply()`，开机即生效；
  - AI Config 屏：标题栏右上 `lv_switch`，初始值取持久状态；VALUE_CHANGED
    即时持久+应用+状态行回报；NVS 写失败回滚到持久值并弹错误框；音量键
    `\v` 可切（状态变更 + 手动 VALUE_CHANGED 事件——程序化 `lv_obj_add_state`
    不触发事件，需手动 send）；
  - 首次读取（`ai` 命名空间尚无该键）日志出现惯常的一行 nvs NOT FOUND
    噪声（与 weather/holidays 同款），首次写入后消失；缺省回落 CA 校验。

### 1.4 台账（`1473ef9`，docs-only）

issue_list 新增 §7，7.1/7.2/7.3 三项 ⬜→✅ 并回填修复 id（当时写入的是
清洗前旧哈希，映射见文件头）；评审 `pda2-review-result-2026-08-07-20.md`
全部出列。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| set_srcdir 本机验证 | ✅ | 强制源目录构建产出 `factory.ino.cpp.o`（1.1，commit message 记录） |
| 编译 + 烧录 | ✅ | 本批 4 个 commit 已包含在后续第四批评审批（`c27cb39..3475c9b`）的 `pio run -e pda2` SUCCESS + COM5 烧录 + 45s 开机冒烟中（菜单渲染、WiFi 自动连接、无 panic）——设备现跑固件含本批全部代码变更 |
| CI 矩阵日志抽查 | ⏳ | **"矩阵全绿"不构成 1.1 的证明**（全绿正是原缺陷的表象）——需抽查任一 job 日志确认编译对象是对应示例的 `.ino.cpp.o` 而非 `test_GPS`，结果回填 |
| GPS 真机回归 | ⏸ | §3 第 2 项 |
| Trust 开关真机回归 | ⏸ | §3 第 1 项（被 `de78338`、`c27cb39..3475c9b` 两份申请继承顺延） |

## 3. 真机回归清单

1. ⏸ **AI Config Trust 开关**：触摸切换 / 音量键 `\v` 切换 / 重启后保持；
     切到 Trust 后对自签端点请求不再报证书错（如有测试端点），切回 CA 后恢复校验
2. ⏸ **GPS 屏**：定位后读数正常刷新（快照锁不引入死锁/丢更新）；
     `gps_altitude` 不再恒 0（有高度输出的场景）
3. ⏸（继承）SD FAT32 重格式化后 Setting 显示容量；#6/#14/#15

## 4. 遗留项（简要）

- 四份申请待结果：`a924c4e`、本份、`de78338`、`c27cb39..3475c9b`
- GPT 跟进评审 3 项 P2 已由第四批（`c27cb39..3475c9b`）修复出列
- 笔友 App：设计 v2（`97e5d2f`）待复审

## 5. 回滚方案

```bash
git revert 1473ef9 950fcfe 52f709e 6d26699
```

注意：revert `6d26699` 会使 CI 矩阵回到"全绿但编错源目录"状态——如仅回退
固件部分，可单独 revert `52f709e`/`950fcfe`。

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
