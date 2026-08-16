# Sleep 屏休眠提示评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-bcca4d2.md](wifi-config-keyboard-review-request-bcca4d2.md)
- **关联 commit**：`bcca4d2`
- **评审结论**：**退回修订**（修订点均为可快速完成的代码级修复，建议申请人补正后重提申请）

---

## 1. Findings

### 1.1 提示与实际超时时间窗之内没有可见的状态指示器

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:3749-3761` —— `create11`
- **触发场景**：用户点击 Sleep → 屏幕显示 "Entering sleep..." 提示 → 等待 3 秒。
- **证据**：
  - commit 提示文本 `"Entering sleep...\n\nWake: press BOOT key."`，但**无倒计时**，用户不知道还有多久真的会进入休眠。
  - 申请书 §2 未说明是否使用 `LVGL` 自带的进度条 / 圆环 / 倒计时数字。
  - §3 验证表第 1 项"休眠提示显示 3s 后休眠"⏸ "待测"——无客观依据证明 3s 内提示持续可见。
- **影响**：
  - 用户在 3s 内可能误以为"还没生效"重复点 Sleep / 菜单，导致频繁进出 Sleep 屏影响修饰键状态。
  - 没有"X 秒后休眠"的明确剩余时间提示，普通用户难以判断 UX 意图。
- **最小修复**：
  1. 在 `create11` 增加 1s 周期的小数字倒计时（`Sleep in: 3` / `2` / `1` / `Sleep now`）。
  2. 或增加一条进度条（`lv_bar`，宽度 0% → 100%）。
  3. 注意：EPD 局部刷新 250ms × N 次 = 累积残影风险，建议只在秒数变化时触发局部刷新（与 `9551bd7` 状态栏策略一致）。

### 1.2 注释 "ext1 唤醒" 与代码 `esp_sleep_enable_ext0_wakeup` 不一致

- **严重性**：Medium
- **位置**：申请书 §2 "BOOT 键（ext1 低电平唤醒 GPIO0）" vs 代码 `examples/pda2/ui_deckpro.cpp` 中实际调用 `esp_sleep_enable_ext0_wakeup((gpio_num_t)ENCODER_KEY, 0)`
- **证据**：
  - `git show bcca4d2 -- examples/pda2/ui_deckpro.cpp` 确认运行时实际为 `ext0`，不是 `ext1`。
  - 申请书与 commit message 都写 "ext1" —— 注释错误。
  - `ext0` 与 `ext1` 在 ESP32 唤醒行为上不同：
    - `ext0` 仅支持 RTC 域的单个 GPIO（低/高电平触发），不能配置多源。
    - `ext1` 支持多个 GPIO mask，但只能 RTC 域。
    - 错误地用 `ext1` 描述实际 `ext0` 唤醒会在维护时产生误导。
- **影响**：
  - 未来若加入第二唤醒源（如 USB 接入），开发者可能照着 "ext1 唤醒" 注释继续封装，但实际上代码是 ext0，无法多源。
  - 唤醒异常时排查方向错误（先查 ext1 mask，但实际只需查 ext0 GPIO）。
- **最小修复**：
  1. commit message 与注释统一为 `ext0`。
  2. 在 `sleep_timer_event` 顶部加注释： `/* Wake source: ext0 on ENCODER_KEY falling edge (BOOT button). */`。

### 1.3 退出页和销毁页双重 `lv_timer_del` 缺少竞态保护

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:3767-3780` —— `exit11` 与 `destroy11`
- **证据**：
  - 两个函数都做了 `if (sleep_timer) { lv_timer_del(sleep_timer); sleep_timer = NULL; }`，对单线程安全。
  - 但**任务在退出后**仍可能触发 `sleep_timer_event`（如果 LVGL 在 `lv_timer_del` 之前的同一 tick 已经派发了 timer）→ 调度器在 timer 真正执行时 `sleep_timer` 已经为 NULL，但任务逻辑并不校验，会照常执行 `esp_deep_sleep_start()`。
  - LVGL 8.3.11 的 `lv_timer_del` 行为：**从 timer 链表移除**（不立即删除回调），若 timer 此刻正在调度队列中执行，下次执行时不会重复调用，但**已开始的当前 tick 内**仍会跑一次回调。
  - 申请人未在 commit message 或代码注释中说明这一边界。
- **影响**：
  - 极端时序：用户按 Back 的同一 tick 内 timer 已开始 sleep 流程 → 用户期望取消但实际已经进入休眠。
  - 概率低但非零，且 Sleep 是不可逆操作，回归测试必须覆盖。
- **最小修复**：
  1. 在 `sleep_timer_event` 顶部增加 `if (!sleep_timer) return;` 守卫：`sleep_timer = NULL` 抢先置位，确保即使被调度也已经退出。
  2. 或使用 `static bool s_cancelled = false;` flag + 在 timer 回调中校验。
  3. 验证表新增："LVGL 渲染 Back 事件与 timer 触发的同一 tick race" 测试用例。

### 1.4 没有针对按键修饰键状态的 `sleep → wake` 保护

- **严重性**：Low → Medium（取决于当前 keypad 状态机如何处理）
- **位置**：`create11` 创建屏时未显式 reset 修饰键
- **证据**：
  - `peri_keypad.cpp::peri_keypad_init` / TCA8418 描述双 Shift / Alt / Sym 修饰键跨页保留（见评审第 6/7 轮）。
  - 唤醒后 `setup()` 重新初始化 TCA8418，但是否会重新读出"上次 SHIFT 仍按住"假状态？
  - commit 没有涉及 `peri_keypad.cpp`。
- **影响**：
  - 唤醒后第一次按键可能误判为大写 / 符号层。
- **最小修复**：
  1. 在 `sleep_timer_event` 调 `esp_deep_sleep_start()` 之前，`keypad_clear_chars()` + 显式 `peri_keypad_reset_modifiers()`（如有此 API）或 `keypad.flush()`。
  2. 或者在 `setup()` 唤醒分支检查修饰键寄存器并清零。

### 1.5 3 项验证均处于 ⏸ 待测状态

- **严重性**：High（流程问题）
- **位置**：申请书 §3
- **证据**：
  - §3 三个 ⏸ 项：
    - 休眠提示显示 3s 后休眠
    - 窗口内点返回取消休眠
    - BOOT 键唤醒
  - 这是 Sleep 屏本批修复的核心三项，**全部未真机验证**。
  - 上两轮评审（`23942f6..9b104d1` / `01f8eac..8b96656`）已反复提示同类问题。
- **影响**：
  - 不可逆操作（深度休眠）有未验证代码路径直接 merge，风险大。
  - 与第 4-7 轮评审"先验证再 merge"原则不一致。
- **最小修复**：
  1. 申请人合并前至少完成以下真机验证：
     - Sleep 提示显示完整 3s 后，屏幕确实变黑进入深度休眠（监听串口 `setup()` 重启日志可作"已 wake"证据；休眠前串口应无输出，唤醒后 `setup()` 应有 ESP-ROM 启动日志）。
     - 在 1-2s 时点按 Back：屏幕弹出回菜单，并不进入休眠。
     - 唤醒路径（按下 BOOT 键 → `setup()` 运行）能正常进入菜单。
  2. 验证记录至少包括：申请人手测视频 / 串口日志 / 验证时间戳。

### 1.6 任务栈 / 优先级 / 看门狗设置未在 commit message 中说明

- **严重性**：Low
- **位置**：本 commit 不创建任务，与异步 IPC 链路无关，但是否会与 keypad / LVGL 抢资源需要评估。
- **最小修复**：commit message 增加 "no FreeRTOS task created" 标识，便于后续审计。

---

## 2. 通过项

- **正确把休眠逻辑移出 `create11` 并改成 3s 一次性 timer**：根因（屏幕未渲染已休眠）诊断准确，修复方向正确。
- **`exit11` 与 `destroy11` 双重清理 timer**：保守策略，避免单点退出遗漏。
- **取消语义清晰**：仅 Back 取消，符合"取消 = 用户主动"的典型产品模式。
- **唤醒源保留不变**：`BOOT 键 ext0(GPIO0)` 低电平，与硬件丝印一致。
- **改动范围小**：单文件 65 行差异，diff 易于复核；其它屏未受影响。

---

## 3. 关联阅读

- 与 [第 8-16 轮整改结果](wifi-config-keyboard-review-result-01f8eac..8b96656.md) §1.3 异步 IPC 边界条件文档化配合：Sleep 屏不需要异步任务，但应用同一 "销毁前清理 + flag 守卫" 原则（与 §1.3 中提到的 `s_cancelled` 一致）。
- 与 `docs/allinone-design.md` §9.3 EPD 刷新策略配合：倒计时更新涉及局刷，遵循"分钟变化时才刷"。

---

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. **退回修订**
- [ ] C. 部分接受（与 §1.1 / §1.2 / §1.3 / §1.4 修订后再 merge）

**优先修订**（建议合并前完成）：

1. **§1.5 真机验证三项**：与 Sleep 不可逆特性相关，不可推迟。
2. **§1.2 ext0/ext1 注释修正**：成本极低，立等可取。
3. **§1.3 竞态保护**：与 §1.5 一起修补，避免回归风险。

**可纳入下轮评审项**：

4. §1.1 倒计时指示器（产品体验加分项）
5. §1.4 修饰键 sleep → wake 守卫

---

**评审人**：Claude（allinone-design / pda2 评审视角），已交叉核对申请书 + commit `bcca4d2` 的实际 diff。
