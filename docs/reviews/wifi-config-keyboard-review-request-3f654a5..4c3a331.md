# 评审申请书：2026-08-07-20 评审遗留 3 项 P2 修复

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-21
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 4 个）：
  - `3f654a5` — `build: honor external PLATFORMIO_SRC_DIR in set_srcdir`（评审 P2）
  - `11b7ec3` — `gps: publish all fields under the snapshot mux`（评审 P2）
  - `a06a1f9` — `ai-cfg: add the advertised Trust self-signed TLS toggle`（评审 P2）
  - `4c3a331` — `docs(issue_list): section 7`（上述三项的 issue_list 台账）
- **背景**：远程同步带来的 `pda2-review-result-2026-08-07-20.md` 共 4 项发现；
  P1（CA 证书）早前已由 `23942f6` 修复（issue_list §5.3），本轮关闭其余 3 项 P2，
  该评审全部出列。
- **已出列范围**（均 Codex 全量接受）：
  - 第 29 轮 `d22007d..4c3c9b1`、第 30 轮 `a2ecd7b`、第 31 轮 `764e7bf..980b6df`（明细见各自结果文件）
- **命名说明**：文件名 = 正文"关联 commit"的首末 id（**含两端**，非 git 区间记法）；
  评审范围以正文列表为准。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 CI 源目录覆盖失效（`3f654a5`，评审 P2 "Preserve CI's selected source directory"）

- **问题**：CI workflow 每个矩阵项 `export PLATFORMIO_SRC_DIR=examples/xxx` 后裸跑
  `pio run`，但 `script/set_srcdir.py` 无条件 `Replace(PROJECT_SRC_DIR=...)`，默认环境
  `T-Deck-Pro` 一律被改指 `examples/test_GPS` —— 矩阵全绿但没编被选项
- **修复**：`_example_dir()` 先查外部 `PLATFORMIO_SRC_DIR` 环境变量（项目相对路径，
  `os.path.join($PROJECT_DIR, ext)`；绝对路径时 join 语义自动生效），有则直接采用；
  无则走原 env→example 映射（交互构建行为不变）
- **验证**：`PLATFORMIO_SRC_DIR=examples/factory pio run -e T-Deck-Pro` → 构建产物
  出现 `src/factory.ino.cpp.o`（修复前为 `test_GPS.ino.cpp.o`）；无变量时 `pio run -e pda2`
  仍正常解析 `examples/pda2`

### 1.2 GPS 写侧临界区（`11b7ec3`，评审 P2 "Synchronize GPS writers with the snapshot lock"）

- **问题**：`gps_get_snapshot()` 持 `s_gps_snapshot_mux` 读，但 `gps_task` 的
  `displayInfo()` 在另一核**无锁写**同一批 `gps_*` 全局——双核下快照仍可能混合
  两次定位的读数，原子快照契约形同虚设
- **修复**：
  - `displayInfo()` 改为**局部变量组装 + 打印**（Serial 全走局部变量，锁内无慢操作），
    末尾**一次临界区**发布全部 11 个字段；invalid 字段保持上次值（语义不变）
  - 5 个旧 `gps_get_*()` 单字段 getter 补同一把锁（同一 bug 类，读写两侧现在对称）
  - 顺手补上从未被写入的 `gps_altitude`（声明了、快照也拷贝了，但 displayInfo 一直
    没写 → 永远 0）；`gps.altitude.meters()`，invalid 保持
- **注意**：锁声明从 snapshot 函数前移到文件顶部（getter 们也要用）

### 1.3 Trust 自签 TLS 开关（`a06a1f9`，评审 P2 "Implement the advertised TLS bypass control"）

- **问题**：`http_utils.h` 注释指引自签/私有 CA 用户去 "AI Cfg screen 'Trust
  self-signed' toggle"，但该控件不存在、无人调 `http_set_tls_mode()` —— 自签端点
  永远走 CA 校验必然失败
- **修复**：
  - `openai_api` 新增 `openai_tls_insecure()/openai_tls_apply()/openai_tls_set(bool)`；
    存 NVS `ai`/`tls_insecure` 单键（uchar）——**有意不进双槽**：设备级传输设置、
    全局作用于所有 http_utils 消费者，不应跟随 Test 门控的 Save 流程（双槽每次
    toggle 都要重 staging+flip，且会被"未 Test 禁 Save"挡住）
  - `factory.ino` setup() 末尾 `openai_tls_apply()`：开机即生效
  - AI Config 标题栏右上 `lv_switch`（44×24）+ "Trust" 标签；进屏按持久值初始化；
    `VALUE_CHANGED` 回调持久化+应用+状态行反馈；NVS 写失败回滚开关到持久值并弹
    "Save failed: NVS error" 框
  - 音量键 `\v` 切换：`lv_obj_add/clear_state` + **手动** `lv_event_send(VALUE_CHANGED)`
    ——程序化改 state 不触发该事件（LVGL 8 只发 STATE_CHANGED），回调只挂
    VALUE_CHANGED，触摸与键盘走同一条持久化路径
- **已知行为**：设备的 `ai` 命名空间当前不存在时，开机 `openai_tls_insecure()` 的
  只读 `begin("ai", true)` 会打一行 ESP-IDF `nvs_open failed: NOT_FOUND`（与
  weather/holidays 首次打开同款噪音），默认值 0 = CA 校验（安全默认）；首次写开关
  后该行消失。本机烧录后串口已观察到这一行——说明该设备 NVS 当前无 `ai` 命名空间，
  AI 配置回落 secrets 链（env.cfg/config_keys.h）

### 1.4 文档台账（`4c3a331`）

issue_list 新增 §7（7.1/7.2/7.3 对应上述三项，含修复 commit id），评审
2026-08-07-20 全部出列。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 pda2 | ✅ | `pio run -e pda2` → SUCCESS（RAM 45.1% / Flash 30.5%） |
| CI 路径编译 | ✅ | `PLATFORMIO_SRC_DIR=examples/factory pio run -e T-Deck-Pro` → factory.ino.cpp.o |
| 烧录 | ✅ | COM5，Hard resetting via RTS |
| 开机冒烟 | ✅ | 串口 12s：菜单渲染（disp_flush×3）、WiFi 自动连接、无 panic |
| NVS 算法测试 | ✅ | `scripts/test_nvs_atomic_save.py` 11/11 PASS（沿用，双槽逻辑未动） |
| CA bundle | ✅ | 6 根证书 PASS（沿用，未动） |
| 真机回归（§3） | ⏸ | 清单如下，逐轮回填 |

## 3. 真机回归清单

**本轮新增**
1. ⏸ AI Config 标题栏 Trust 开关显示（右上，默认 OFF）+ 触摸切换 → 状态行
     `TLS: trust self-signed (ON)`，重启后保持 ON
2. ⏸ 音量键 `\v` 切换 Trust 开关（与触摸等效）；Save 流程不受开关影响
     （开关即时生效，不经 Test/Save）
3. ⏸ Trust=ON 时对接自签端点（如有条件）验证跳过校验；=OFF 恢复 CA 校验
4. ⏸ GPS 屏坐标/时间/卫星数正常刷新（无肉眼撕裂；锁语义代码级评审）
5. ⏸ 开机串口 GPS 行正常输出（displayInfo 打印路径改局部变量后格式不变）

**继承（未回归项顺延）**
6. ⏸ SD FAT32 重格式化后 Setting 显示容量（a924c4e/47c14a2 提示项）
7. ⏸ 回归 #6/#14/#15（第 29 轮申请遗留）

## 4. 遗留项（简要）

- 上一申请文件 `wifi-config-keyboard-review-request-a924c4e.md` 的 hash 因 rebase
  改写已过期（现为 `47c14a2`）——是否改名待用户定夺（结果文件永不改写）
- Key：HEAD 无真实 Key；git 历史残留 → 推公网前 filter-repo + 轮换（SECURITY.md）
- 笔友 App：设计评审（`penpal-design-review-result.md`，C 部分接受）前置条件未清，
  实现未开工

## 5. 回滚方案

```bash
git revert 4c3a331 a06a1f9 11b7ec3 3f654a5
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
