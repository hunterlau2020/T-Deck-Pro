# 第 31 轮整改评审结果（Codex）— 收尾批次 1：本地月界 + 扫描覆盖层跨屏 + test_keypad 镜像注释

- **评审日期**：2026-08-19
- **评审申请书**：[wifi-config-keyboard-review-request-764e7bf..980b6df.md](wifi-config-keyboard-review-request-764e7bf..980b6df.md)
- **关联代码范围**：`764e7bf`、`6ce2b4b`、`980b6df`（首末 id 含两端，非 git 区间记法）
- **本次评审对象**：
  - `764e7bf` — stats 月度清零改本地月界（第 28 轮 O1 闭合）
  - `6ce2b4b` — WiFi 扫描覆盖层/banner 跨屏残留修复（第 25 轮跟踪项闭合）
  - `980b6df` — test_keypad raw/driver 列镜像注释（issue_list 1.4 闭合）
  - doc-only：`6cee775`（README 镜像说明 + issue_list 关单）、`3ee1c22`（第 30 轮结果原样归档 + 申请出列）
- **评审结论**：**A 全量接受**（附 1 项 Low 潜在集群备注 + 1 项 Info，均不阻断）

---

## 1. Findings

### 1.1 764e7bf stats 月度清零改本地月界 — ✅ 通过（第 28 轮 O1 闭合）

- **位置**：`examples/pda2/factory.ino:595+`（setup 早段 TZ 设置）、`examples/pda2/openai_api.cpp:246+`（gmtime→localtime）
- **机制核查**：
  - `setenv("TZ","CST-8",1)+tzset()`：POSIX 符号约定下 `CST-8` = UTC+8 无夏令时，写法正确；
    位于 setup() 早段（Serial 之后），先于任何 loop() 期的 stats 加载 ✓
  - NTP 哨兵 `time > 1700000000` 不变；`ym` 计算与清零/落盘逻辑不变，仅时间源换 localtime ✓
- **观察**：
  - **Info（冗余但无害）**：`factory.ino:753` 既有 `configTzTime("CST-8", ...)`（ESP32 core
    内部同样 setenv+tzset）——新增 setenv 与其同值幂等，属自文档化加固。推论：
    Calendar/Sleep 等 `localtime_r` 使用点（ui_calendar.cpp:152、ui_deckpro.cpp:1507/4247）
    **此前已按 CST-8 渲染**，申请书回归项 17 括注"此前 UTC 差 8 小时"与代码史不符——
    该项实测仍会通过（显示北京时间），但可见变化仅在 stats 月界。不阻断，建议下轮回填时注明
  - **迁移披露复核**：已存 `reset_month` 为 UTC 月号；升级后仅当恰处本地 1 号 00:00–08:00
    窗口才可能触发一次性多清零——申请已如实披露，统计归零重计，无害 ✓
  - `localtime` 与 weather `gmtime` 共享静态 tm 缓冲——第 28 轮 O3 既有备注，无新增风险

### 1.2 6ce2b4b 扫描覆盖层/banner 跨屏残留 — ✅ 通过（第 25 轮跟踪项闭合）

- **位置**：`examples/pda2/ui_deckpro.cpp` exit4_1（:2329+）+ 前置声明（:1895）
- **机制核查**：
  - `wifi_scan_overlay_hide` / `wifi_banner_hide` 实现均判空（lv_obj_del 前 if + 置 NULL +
    计时/帧序复位）——exit 时无条件调用安全 ✓
  - exit4_1 先隐藏再 `ui_disp_full_refr()`；destroy4_1 原清理保留（双保险）✓
  - **跨屏再显路径排查**：overlay/banner 的再显示只可能来自 `wifi_cfg_scan_poll`（扫描完成）
    与连接/失败分支，全部在 `wifi_cfg_keyboard_poll` 内——该函数以 `wifi_cfg_kbd_active`
    为门卫；当前导航拓扑中**不存在从 4_1 可达的 push 路径**（4_1/4_2 均由菜单 push，
    互不叠加；唯一遍历式 push 是 auto-demo 定时器 `ui_auto_timer_cb`，其表序为
    4_1→POP→4_2，pop 走 destroy4_1 全清理）——残留主诉路径已堵，再显路径当前不可达 ✓
- **Low 潜在集群备注**（不阻断，与第 27 轮 screen9 对称性同类，合并跟踪）：
  exit4_1 **未清** `wifi_cfg_kbd_active`（entry4_1 亦不恢复，靠 create4_1 置位）。
  当前无 push 路径故不可触发；若未来新增"从 4_1 push"的功能，覆盖期间 wifi_cfg poll
  会继续吃键并在扫描完成时于 top layer 再挂 banner——届时需 exit4_1 清标志 +
  entry4_1 恢复标志，与 screen9 项一并处理

### 1.3 980b6df + 6cee775 test_keypad 镜像注释 + 文档关单 — ✅ 通过

- **公式核验**：`peri_keypad.cpp:179` 实际换算 `col = (KEYPAD_COLS-1) - k % KEYPAD_COLS`，
  `KEYPAD_COLS=10`（:7）→ 恰为 `driver_col = 9 - raw_col`；注释所举 raw (R2 C9) = Alt =
  driver (2,0) 代入成立 ✓
- 注释位于 raw 坐标打印处（test_keypad.ino:43+），README §2 键盘节同步加引用块；
  `issue_list.md` 1.4 ⬜→✅ 并按规范补"修复：980b6df"字段 ✓（issue_list 为 canonical fix log，格式合规）

### 1.4 流程核验

- 第 30 轮结果 `wifi-config-keyboard-review-result-codex-a2ecd7b.md` **原样归档**
  （3ee1c22，A 状态入库，内容完整）✓；旧申请出列删除、git 历史可溯 ✓
- 验证状态如实：烧录 ⏸（COM5 当前不存在，设备未连接）——符合 CLAUDE.md
  "每个 commit 记录验证缺口"要求；连接后补烧 + 回归项 16/17 回填即可
- 回归清单继承完整（15 项回填状态保留 + 新增 16/17/18）；#18 代码级验收合理
  （本地月界等 9 月自然跨月自动验证，与 #5 同策略）
- 回滚顺序 `980b6df 6ce2b4b 764e7bf` 新→旧正确 ✓

---

## 2. 已通过项汇总

- **本轮新增**：`764e7bf` / `6ce2b4b` / `980b6df` + doc-only `6cee775` / `3ee1c22`
- **沿用**：a2ecd7b（第 30 轮）、d22007d..4c3c9b1（第 29 轮）、b9b1ed4..fd7be74（第 28 轮）、
  e08bdac..b9b1ed4（第 22–27 轮）、历史 844a907..156732c

---

## 3. 跟踪项（继承 + 状态更新）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| stats 月度清零 UTC 月界（O1） | 第 28 轮 | ✅ 764e7bf 闭合 |
| wifi_scan_overlay 跨 push 屏残留 | 第 25 轮 | ✅ 6ce2b4b 闭合 |
| issue_list 1.4 test_keypad 镜像 | 预存在 | ✅ 980b6df/6cee775 闭合 |
| exit4_1 kbd_active 覆盖期潜在集群（与 screen9 exit9/entry9 对称性合并） | 本轮 §1.2 | Low，挂起（当前无触发路径） |
| 回归项 17 括注"此前 UTC"与代码史不符 | 本轮 §1.1 | Info，回填时注明 |
| 烧录 ⏸ + 回归 #6/#14/#15/#16/#17 | 申请书 | 设备连接后补 |
| M1：3 份评审文档明文旧 Key 掩码 | 第 26 轮 | 推公网前必做 |
| 双 Enter 静默窗；O2（清零仅 load 时评估）；O4（可选迁移测试） | 第 27/28 轮 | 挂起/接受 |
| SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | TODO | 阶段 1 |

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方本轮复跑）
- 静态复核：`git show 764e7bf 6ce2b4b 980b6df 6cee775 3ee1c22` 全 diff 逐段核查；
  POSIX TZ 符号约定核验；configTzTime 既有调用点排查（factory.ino:753）；
  localtime_r/gmtime 使用点全量枚举；overlay/banner 再显路径（scan_poll 门卫链）排查；
  push 可达性分析（menu_buf/auto-demo 表序）；镜像公式对照 peri_keypad.cpp:179 逐字核验；
  第 30 轮归档完整性确认
- 编译：评审环境无 `pio`，采信申请人 `pio run -e pda2` SUCCESS（沿用前轮做法）；
  烧录待设备连接（申请已如实声明）
- 本结果文档不含任何 Key 正文

## 5. 审批意见

- [x] **A. 全量接受** — 3 个代码 commit + 2 个 doc-only commit 接受，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：收尾批次质量高——O1 以最小改动闭合且迁移影响如实披露；第 25 轮跨屏残留
经再显路径与 push 可达性双重排查确认闭合；镜像注释公式经驱动源码逐字核验。
新观察均为 Low/Info 潜在项，无当前可触发路径。

**遗留项**：
- 设备连接后补烧录 + 回归 #6/#14/#15/#16/#17 回填
- M1（3 份评审文档掩码）继续跟踪至推公网前

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：TZ/localtime 语义与既有 configTzTime
交叉核验、overlay 再显路径与 push 可达性分析、镜像公式驱动源码对照、issue_list 关单格式核查、
第 30 轮归档完整性确认、NVS 测试复跑 11/11）