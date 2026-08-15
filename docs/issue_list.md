# 文档与实际硬件差异问题清单

> 记录项目文档/代码注释与实际硬件（HD-V2，25-09-15 批次）之间的差异，以及由此产生的固件问题。
> 状态标记：✅ 已修复（注明 commit）｜⏳ 处理中｜⬜ 待处理
>
> 建立日期：2026-08-16（依据 pda2 真机调试与 7+ 轮评审记录）

---

## 1. 键盘（文档/固件假设 vs HD-V2 实物）

### 1.1 没有 Ctrl 键，Z 行最左键是 Alt 而非 Shift ⬜→✅

- **文档现状（修订前）**：README 键位图与 `peri_keypad.cpp` 注释认为 Z 行最左 = Shift(2,0)，底行两端 = LCtrl(3,5)/RCtrl(3,9)
- **实际硬件**：HD-V2 **无 Ctrl 键**；Z 行最左丝印 **Alt**；底行两端是两个 **Shift**
- **后果**：按 Alt 出大写（固件把 (2,0) 当 Shift），按 Shift 无反应——用户现场报告的两个问题
- **修复**：实测 12 键矩阵解码（`test_keypad` + 串口）确认布局；修饰键模型重做（`3d98321`：双 Shift 独立状态取 OR；产品决策 B：Alt = 临时符号层）：
  ```
  Q   W   E   R   T   Y   U   I   O   P
  A   S   D   F   G   H   J   K   L   ⌫
  Alt Z   X   C   V   B   N   M   ♪   ⏎
  ⇧   Mic Space Sym ⇧
  ```

### 1.2 音量键（♪/$）Sym 层映射错误 ✅

- **文档现状（修订前）**：Sym 层 (2,8) = `'0'`
- **实际硬件**：该键丝印为音量/喇叭（正常层 `$`）；Sym 层应为**音量**功能
- **后果**：Sym 层按音量键在输入框打出 0
- **修复**：`6a9ab00` — Sym 层 (2,8) 改发专用码 `'\v'`（0x0B）；文本输入屏显式忽略。⚠️ 音量码暂无处理器（音量控制 UI 未实现，待需求）

### 1.3 麦克风键 Sym 层无映射 ✅

- **实际硬件**：麦克风键 (3,6)，正常层 = 麦克风（无功能），Sym 层丝印应为 `0`
- **后果**：Sym 层按麦克风键无反应（用户报告）
- **修复**：`6a9ab00` — Sym 层 (3,6) = `'0'`。正常层麦克风仍无功能（录音功能未接入）

### 1.4 `test_keypad` 原始坐标与驱动坐标是列镜像 ⬜

- **差异**：`examples/test_keypad/test_keypad.ino` 打印 raw row/col；`peri_keypad.cpp` 内部 `col = 9 - raw_col`。两者列方向相反，直接用 raw 值对照 keymap 会完全错位
- **影响**：排查键位时易误导（本清单 1.1 的实测解码即靠此换算得出）
- **建议**：test_keypad 示例加一行换算提示注释；README 键盘节注明镜像关系

### 1.5 TCA8418 溢出标志是 W1C 而非 read-to-clear ✅

- **文档现状（修订前）**：`peri_keypad.cpp` 注释称 `INT_STAT` read-to-clear，只读不写
- **实际硬件**：TI TCA8418 `INT_STAT` 需**写 1 清除**（库 `flush()` 用 `writeRegister(INT_STAT, mask)`）
- **后果**：一次溢出后 `OVR_FLOW_INT` 常驻，修饰键每轮被重置 → Alt/Shift/Sym 持续失效
- **修复**：`2e559ad` — 溢出后 `writeRegister(INT_STAT, OVR_FLOW_INT)`（W1C）+ 修饰键恢复

### 1.6 触摸焦点与键盘字段状态脱节 ✅

- **差异**：LVGL 8.3 指针点按会独立移动文本框焦点（`indev_click_focus`，无需 group），与固件键盘状态机（`wifi_cfg_field`）无关联
- **后果**：触摸点中 pass 框后按 ⌫，删除的是 SSID 框文字（用户报告）
- **修复**：`6c51964` — textarea `LV_EVENT_FOCUSED` 回调同步字段状态；键盘切字段时反向发 `LV_EVENT_FOCUSED` 移动光标

### 1.7 按键积压跨页面残留 ⬜→✅

- **差异**：墨水屏刷新慢 + 键盘 FIFO 引入后，连按产生的积压按键在页面切换后仍被消费
- **后果**：WiFi config 连按 ⌫ 退出后，残留 ⌫ 继续作用 → 无法重新进入该页面（用户报告）
- **修复**：`6a9ab00` — `keypad_clear_chars()` + scr_mgr 切换/push/pop 三处清空字符队列

---

## 2. WiFi / 网络

### 2.1 CA 信任库内容与头注释不符 ✅

- **文档现状（修订前）**：`http_utils.h` 注释声称内置 ISRG Root X1 + DigiCert Global Root G2 + GlobalSign Root R1 三个根
- **实际代码**：`CA_BUNDLE` 只有 **ISRG Root X1 一个根**；且 GlobalSign R1 已被 Mozilla 根存储移除（现役为 R3/R6/E46/R46）
- **后果**：ifconfig.me 走 Let's Encrypt **2026 新层级（YR1 ← ISRG Root YR）**，验证失败 → WiFi Test 报 "HTTP -1"（-8576 CERT_VERIFY_FAILED）
- **修复**：`23942f6` — bundle 扩为 4 根：ISRG X1、ISRG Root YR（ifconfig.me 实际链提取，跨签版）、DigiCert G2（官方）、GlobalSign R3（Mozilla bundle 2026-08-13）

### 2.2 TLS 验证依赖系统时间，CN 网络 NTP 不可达 ⬜→✅

- **差异**：文档（allinone-design §9 风险 11）已预警该问题，但 pda2 固件未落实——`pool.ntp.org` 在 CN 网络常不可达，冷启动时间停在 1970，证书 `notBefore` 校验失败同样报 -8576，与"缺根"难以区分
- **修复**：`23942f6` — 每次 HTTPS 前 `http_ensure_time(5000)`：未同步则重试 NTP（**cn.pool.ntp.org 优先**），失败明确报 "Time not synced"
- **遗留**：开机流程未做时间同步等待（设计稿建议 setup() 末尾轮询 30s，未实施）⬜

### 2.3 开机自动重连与扫描互斥（评审 §5.3）⬜

- **现象**：NVS 存有旧凭据（如已关闭的热点 HONOR-60）时开机持续重连（`ASSOC_LEAVE` 循环）；此状态下 `scanNetworks()` 行为未真机验证
- **状态**：评审明确**不接受**无条件 `disconnect()`；已实现错误码三态区分（`f3f2a58`/`6c51964`），待真机回归确认后决定是否在配置页生命周期内暂停自动重连

### 2.4 框架 `scanDelete()` 不能中止在途扫描 ⬜→✅

- **差异**：文档/代码曾假设 `WiFi.scanDelete()` 可取消异步扫描；实际 ESP32 Arduino 框架只清标志、释放结果，不调 `esp_wifi_scan_stop()`
- **后果**：扫描中退出页面 → 后台扫描残留，其他页面 `scanNetworks()` 直接返回 `WIFI_SCAN_RUNNING`
- **修复**：`5030566`/`6c51964` — `esp_wifi_scan_stop()` + 等待 SCAN_DONE 处理后释放；扫描代次失效在途结果

---

## 3. 其他硬件观察

### 3.1 墨水屏局部刷新积累鬼影 ⬜（设计已规划）

- 文档（allinone-design §9 风险 3）已规划"每 N 次局刷强制一次全刷"；pda2 当前实现中 `render_start_cb` 被注释导致周期全刷计数器不工作（设计稿方案 A/B 待实施到 allinone，pda2 未改）

### 3.2 触摸坐标在未触摸时的噪声输出 ⬜ 待确认

- **现象**：串口观察触摸驱动周期性打印 `x = 1, y = 6` 等坐标（未验证是否真实触摸或 CST 驱动噪声）
- **影响**：待确认；若为噪声，可能干扰 LVGL 指针事件

---

## 4. 文档与构建环境偏差

### 4.1 `build-and-code-structure.md` §8 的 pio 路径是另一台机器 ⬜

- **文档现状**：`C:\Users\asdfo\.platformio\penv\Scripts\pio.exe`；称"git 不在当前会话 PATH 中"
- **本机实际**：用户目录为 `hunter`；pio 未加入 PATH，用 `python -m platformio` 调用（PlatformIO Core 6.1.19，pip 安装）；git 可用
- **建议**：§8 改为通用说明（`python -m platformio` 或用户目录 `%USERPROFILE%\.platformio\penv\Scripts`）

---

## 附：键盘实测记录

2026-08-16 使用 `examples/test_keypad`（原始矩阵示例）+ 串口监视器，用户按键实测解码（列镜像换算后）：

| 按键 | 原始坐标 | 固件坐标 |
|---|---|---|
| Alt | R2 C9 | (2,0) |
| Shift 左 | R3 C4 | (3,5) |
| Shift 右 | R3 C0 | (3,9) |
| Sym | R3 C1 | (3,8) |
| Mic | R3 C3 | (3,6) |
| Space | R3 C2 | (3,7) |
| Q / Z / M / ♪ / ⏎ / ⌫ | — | 与固件映射一致 |
