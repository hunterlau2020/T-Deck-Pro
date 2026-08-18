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

### 1.4 `test_keypad` 原始坐标与驱动坐标是列镜像 ⬜→✅

- **差异**：`examples/test_keypad/test_keypad.ino` 打印 raw row/col；`peri_keypad.cpp` 内部 `col = 9 - raw_col`。两者列方向相反，直接用 raw 值对照 keymap 会完全错位
- **影响**：排查键位时易误导（本清单 1.1 的实测解码即靠此换算得出）
- **修复**：`980b6df` — test_keypad 示例加换算提示注释；`README.md` §2 键盘节加镜像关系说明

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

## 5. 第二批发现（时钟 / 信任库 / AI 屏，2026-08-16 下午）

### 5.1 状态栏时间硬编码 "10:19" ✅

- **差异**：`ui_deckpro.cpp::create0` 把任务栏时间写死为 10:19，定时器从不更新；电量显示经核实为 **BQ27220 实时读数**（10s 周期），非写死
- **修复**：`9551bd7` — 实时本地时间（CST-8），NTP 未同步显示 `--:--`；随后并入电量 10s 刷新周期（`6be70eb`）

### 5.2 时区继承出厂固件的 PST8PDT ✅

- **差异**：三处 `configTzTime` 沿用美国太平洋时区，NTP 同步后本地时间偏 16 小时——"时钟错误"的根因，无独立时间设置功能（也不需要）
- **修复**：`d8f0ab7` — 全部改 **CST-8**（cn.pool.ntp.org 优先）；连网成功自动校时 + Time Sync 手动按钮

### 5.3 CA 信任库缺根 ✅

- **差异**：代码只内置 ISRG Root X1 一个根（注释声称 3 个）；ifconfig.me 走 LE 2026 新层级（YR1←ISRG Root YR），openrouter.ai 链已从文档记录的 GTS R3 变为 **WE1←GTS Root R4**
- **后果**：WiFi Test "HTTP -1"（-8576 证书验证失败）；与"系统时间未同步"症状相同，难区分
- **修复**：`23942f6`/`d8f0ab7` — bundle 扩为 5 根（X1/YR/DigiCert G2/GlobalSign R3/GTS R4）+ HTTPS 前时间校验与 NTP 重试 + `http_response_t.error` 透出具体原因
- **经验**：换端点前必须 `openssl s_client` 抓链核对根覆盖（ifconfig.me 的 YR 根 2026-05-13 生效，notBefore 很近，对设备时钟敏感）

### 5.4 AI 配置屏交互问题（4 项，用户反馈）✅

- label 与输入框内容重复、Model/Key 无独立输入框、Base 单行放不下长 URL → 重构为三独立输入框（Base 多行 52px），label 只留字段名 + 标记（`6be70eb`）
- Key 框"写死的默认值"实为 **NVS 存量配置**（此前保存的 Key，非源码硬编码）；用户后续要求做成固件默认 → `AI_KEY_DEFAULT`（`9b104d1`，NVS 优先；⚠️ Key 已入仓库）
- Save/Test 按钮点击"无反应"：flex 内容高度推算导致命中区与视觉位置偏差 → **FLOATING 钉底 + move_foreground + 高度 34**（`e5b109d`）；反馈从小状态行升级为 **msgbox（倒计时 + Close）**（`d4ccf28`）
- Model 默认值 `AI_MODEL_DEFAULT = deepseek/deepseek-v4-flash-0731`（`e5b109d`）

### 5.5 AI Text 屏（用户需求 4 项）✅

- 输入框多行（64px/200 字符）；Send/Clear 按钮（FLOATING 钉底）；请求体按 OpenRouter curl 范例（system 提示 + temperature 0.7 + reasoning.exclude）；user 内容经 cJSON 自动 JSON 转义（请求体是 JSON，非 URL 编码，转义语义已与用户澄清）；发送异步化（任务 + 轮询，不冻结 UI）（`9b104d1`）

### 5.6 烧录操作坑：串口监视器占用 COM 口 ⬜（操作规范）

- **现象**：`pio run -t upload` 在监视器后台运行时失败（端口被占），错误信息不明显
- **规范**：烧录前先停掉 `pio device monitor` 后台任务，烧完再重启监视器（已记入 build-and-code-structure.md §8 与本清单）

### 5.7 AI 语义与存储布局：设计稿落后于预研实现 ✅

- **差异**：`allinone-design.md` §1/§4/§10 原定 "v1 单轮问答、多轮上下文列 v2"（体验评审 §1.7 决策）；pda2 预研随后落地多轮上下文（整轮配对 8KB）、聊天历史 SPIFFS 持久化（`/chat.log` 原子换入 + `/chat.draft` 草稿）、New 会话按钮、usage 统计（NVS `ai_stats` 单 blob）、AI 配置双槽原子保存、Test 改最小 chat-completion + 计费提示——设计稿与实现脱节
- **修复（docs，2026-08-17）**：allinone-design 第三轮修订同步 §1 决策 / §4 AI 两节 / §2.3 CA（实装 5 根 + `ca_bundle_check.py`）/ §5.3 移植清单 / §10；CLAUDE.md 存储布局表更新（NVS `ai` 双槽 + `ai_stats` + `wifi`；SPIFFS `/chat.log` + `/chat.draft`）
- **经验**：设计稿"AI 形态"类产品语义必须随预研每轮评审回写，否则 allinone 实施时按旧决策移植会退回到单轮

### 5.8 SPIFFS rename 目标已存在时失败（真机回归发现）✅

- **差异**：`SPIFFS.rename()`（→ `spiffs_rename`）**不是 POSIX 语义**——目标文件已存在时返回失败（CONFLICTING_NAME），不会覆盖
- **后果**：`/chat.log` 原子换入第一次成功、**第二次起全部静默失败**，日志永远停留在第一条 → 重启后历史几乎全丢（2026-08-17 真机回归"重启恢复"失败的直接根因）
- **修复**：`867435e` — bak 三步换入：`remove(.bak)` → `rename(正式→.bak)` → `rename(tmp→正式)` → `remove(.bak)`；loader 启动时把遗留 `.bak` 提升回主路径（中断恢复）
- **经验**：SPIFFS/LittleFS 上的"原子替换"必须自带三步舞；凡 rename 都要先确认目标不存在或走备份

## 6. Shutdown 关机后的开机行为（2026-08-16 观察，⏳ 待继续观察）

- **机制**：菜单 Shutdown = XPowersLib `shutdown()` = `BATFET_DIS`（BQ25896 强制断开电池供电通路，整机断电）；库注释声明"只能通过按 PWR 键或接入电源开机"
- **实测（HD-V2）**：按电源键**无法**开机；**插 USB 即自动上电**（VBUS 清除 BATFET_DIS，无需按键）；固件启动时 `bq25896_apply_factory_profile()` 恢复电池通路，之后可拔 USB 正常使用
- **偶发现象（一次）**：插 USB 上电后卡在开机画面（屏幕保留关机前/开机画面，无背光易误判"没开机"）；按 **RESET 键**后正常启动进入首页。疑似冷启动外设初始化偶发卡死（候选：DRV2605 探测失败 `while(1)`），未复现、未定位
- **排查方法**：判断是否开机看 **USB 枚举（COM5, 303A:1001）** 而非屏幕（墨水屏断电保留最后一帧）；esptool `flash_id` 可确认芯片/Flash 正常并硬复位；开机日志丢失时按物理 RESET 抓取
- **待观察项**：① 下次 shutdown 后插 USB 是否直接进系统（卡死是否复现）；② 长按电源键 2-3s 能否唤醒；③ 复现卡死时保留串口日志定位初始化卡点
- **待决策**：是否把 Shutdown 改为深度休眠（BOOT 键唤醒，同 Sleep 屏机制）——用户暂定"先观察再决定"

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
