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

### 3.3 4G 版无 PCM5102A DAC——耳机孔无 I2S 音频输出 ⬜（硬件不支持，非固件问题）

- **差异**：卖家宣传"支持 PCM5102A"，但 HD-V2（4G/A7682E 批次）板上**未见 PCM5102A 芯片**；耳机孔旁的芯片丝印 "QM-H693 GSM-V1.3" 为 4G 模组。引脚层面 I2S（BCLK=7/DOUT=8/LRC=9）与 A7682E 的 RI(7)/ITR(8)/RST(9) 完全重合——4G 版与非 4G 版是引脚互斥的硬件变体（`test_pcm5102a` 示例注释"If 4G version ignore"即源于此）
- **验证**：2026-08-19 `examples/test_i2s_probe` 探针固件两轮实测——① 1s 提示音：解码全程正常（ID3→syncword→EOF），无声；② 60s 音频 + 音量 0→21 渐变、串口 `running=1` 确认在播、耳机经电脑验证正常：**仍无声**。ESP32-S3 无内置 DAC，板上无 DAC 芯片 = 无模拟音频输出路径，I2S 信号无处可去
- **结论**：**MP3 播放屏在此板上不可行**（allinone 设计稿 §4 的 MP3 屏取消）；3.5mm 孔疑似接 4G 模组通话音频，与固件无关。测试遗留 `test_i2s_probe` 示例保留作硬件验证记录

### 3.4 SD 卡仅支持 FAT16/FAT32，exFAT 显示 0MB ✅

- **现象**：用户插入 120GB exFAT 卡，About System 屏 "SD total: 0MB"；串口
  `f_mount failed: (13) There is no valid FAT volume`——卡被硬件识别但 FATFS
  （ESP32 SD 库）不认 exFAT/NTFS
- **修复**：`a924c4e` — 挂载失败时区分"无卡/格式非 FAT"（`cardType()` 在 f_mount
  失败后仍保留检测类型），About System 屏加 `SD hint: need FAT32 / no card`，
  串口打印原因
- **跟进**（`c8f62f3`，Codex a924c4e 评审 P2）："有卡但挂载失败"不能断言为格式
  问题（SPI/初始化错误、FAT32 卡自身挂载错误同分支）——提示改两行
  `SD hint: mount failed` + `try FAT16/FAT32?`（事实 + 建议，不作诊断）；
  两处过度断言的注释同步改准确
- **用户操作**：>32GB 卡用 guiformat/Rufus 格成 FAT32（MBR 分区表）后重启机器；
  FAT32 上限 2TB，SDHC/SDXC 均可

---

## 4. 文档与构建环境偏差

### 4.1 `build-and-code-structure.md` §8 的 pio 路径是另一台机器 ✅（2026-08-22 复核闭合）

- **文档现状**：`C:\Users\asdfo\.platformio\penv\Scripts\pio.exe`；称"git 不在当前会话 PATH 中"
- **本机实际**：用户目录为 `hunter`；pio 未加入 PATH，用 `python -m platformio` 调用（PlatformIO Core 6.1.19，pip 安装）；git 可用
- **建议**：§8 改为通用说明（`python -m platformio` 或用户目录 `%USERPROFILE%\.platformio\penv\Scripts`）
- **状态**：§8 已于 2026-08-16 文档更新时改为通用说明（`python -m platformio` +
  hunter 缓存路径 + 备查注记），本条登记滞后于修复——2026-08-22 复核确认后闭合

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

## 7. 第三批评审修复（评审 `pda2-review-result-2026-08-07-20.md`，2026-08-21 处理）

该评审 4 项发现：P1 CA 证书早已修复（见 §5.3），其余 3 项 P2 于本批关闭。

### 7.1 CI 的 `PLATFORMIO_SRC_DIR` 被构建脚本覆盖 ✅

- **差异**：`.github/workflows/platformio.yml` 每个矩阵项 `export PLATFORMIO_SRC_DIR=examples/xxx` 后裸跑 `pio run`，但 `script/set_srcdir.py` 无条件 `Replace(PROJECT_SRC_DIR=...)`，默认环境 `T-Deck-Pro` 一律被改指 `examples/test_GPS`——矩阵"全绿"但实际没编 factory 等被选项
- **修复**：`3f654a5` — `set_srcdir.py` 优先尊重外部 `PLATFORMIO_SRC_DIR`（项目相对路径），未设置时才走 env→example 映射；本机已验证 `PLATFORMIO_SRC_DIR=examples/factory` 时 `T-Deck-Pro` 环境实际编译 `factory.ino.cpp.o`

### 7.2 GPS 写侧未与快照锁同步 ✅

- **差异**：`gps_get_snapshot()` 持 `s_gps_snapshot_mux` 读全部字段，但 `gps_task` 的 `displayInfo()` 在另一核**无锁写**同一批 `gps_*` 全局——双核下快照仍可能混合两次定位的读数，原子快照契约形同虚设
- **修复**：`11b7ec3` — `displayInfo()` 先在局部变量里组装本次更新（Serial 打印全部走局部变量，锁内无慢操作），最后**一次临界区**发布全部 11 个字段；旧的 5 个 `gps_get_*()` 单字段 getter 也补上同一把锁；顺手补上从未被写入的 `gps_altitude`（此前永远是 0）

### 7.3 `http_utils.h` 宣传的 "AI Cfg 信任自签开关" 不存在 ✅

- **差异**：`http_utils.h` 注释指引自签/私有 CA 用户去 "AI Cfg screen 'Trust self-signed' toggle"，但该屏根本没有这个控件，也无人调 `http_set_tls_mode()`——自签端点永远走 CA 校验、必然失败
- **修复**：`a06a1f9` — AI Config 标题栏右上新增 Trust 开关（`lv_switch`，触摸/音量键 `\v` 均可切换）；`openai_api` 新增 `openai_tls_insecure()/openai_tls_apply()/openai_tls_set()`，存 NVS `ai`/`tls_insecure`（独立单键，**不进**双槽——它是设备级传输设置，不应跟随 Test 门控的 Save 流程）；`factory.ino` setup() 末尾 `openai_tls_apply()` 使开机即生效；NVS 写失败时开关回滚到持久值并弹错误框

### 7.4 Trust 开关影响面未在 UI 注明 ✅（2026-08-22 `a58a73c`）

- **来源**：Kimi 双评审 `wifi-config-keyboard-review-result-kimi-3f654a5..4c3a331.md`
  §1.3 影响面提示（A 全量接受，不阻塞）
- **差异**：开关作用于**全设备所有** http_utils 消费者（天气、词典、WiFi Test
  等）——ON 即全设备 HTTPS 放弃 CA 校验，影响面大于控件所在屏（AI Config）的
  直觉范围，而 UI 文案仅 "Trust"
- **修复**：`a58a73c` — 状态行文案改为 `TLS: ALL HTTPS trust self-signed` /
  `TLS: ALL HTTPS CA verify`（拼出作用域），串口日志同步注明
  `(applies to ALL HTTPS)`

## 8. 菜单翻页 off-by-one（kimi 设计评审 §1.1 顺带揭出，2026-08-21 修复）

### 8.1 18 项菜单存在"幽灵页" ✅

- **差异**：`ui_deckpro.cpp` 的 `page_num = MENU_BTN_NUM / 9` 算的是**页数**，但手势门控 `if(page_curr < page_num)` 把它当**最大下标**用——18 项时 page_num=2，第 2 页再左滑 `page_curr` 进入不存在的页 2：分支只处理 `==0/==1`，画面停在原处，且下一次右滑先被"空滑"用于把状态收回下标 1
- **修复**：`de78338` — 改为 `(MENU_BTN_NUM - 1) / 9`（最大下标语义），18 项 → 下标 0..1，幽灵页不可达；`MENU_BTN_NUM <= 9` 单页早退不变。Setting/Test/A7682/PCM 页同款 `n/9` 写法未动（各自条目数未踩中 9 的整倍数），penpal 菜单第 3 页批次再统一换共享 ceil helper
- **第 3 页落地后的统一**（`5329383`，2026-08-22）：菜单自身改为页数无关写法——`menu_page_apply()` 单点切页（手势 cb 与 create0 初始态共用）、页点按 `page_num` 循环创建、按钮分派 `i<9/<18/else` 三路；19 项 → 下标 0..2，`page_num` 公式不动（de78338 语义已被复用）。顺带修复存量小瑕疵：初始两页点全黑（首次手势前 active 点未画）。Setting/Test/A7682/PCM 屏的同款写法维持原判不动。真机 19 项三页往返回归见 `docs/penpal-design.md` §7-4（⏸ 待实测）

## 9. 第四批评审发现（GPT 跟进评审 `pda2-review-result-2026-08-07-21-gpt.md`，2026-08-21 到达；9.1-9.4 已于 2026-08-22 全部修复）

### 9.1 Weather 部分刷新被缓存为成功 ✅

- **差异**：`ui_weather.cpp` fetch 任务结尾只看 `data_valid`——该标志既被本次
  解析置位、也被更早的缓存加载置位。current 请求失败但旧缓存保持
  `data_valid=true`、或 current 成功而 forecast 失败时，仍推进 `last_fetch_time`
  并 `save_cache()` 把新旧混合状态存盘 → 界面报成功、1 小时内不再重试
- **修复**：`c27cb39` — 两个端点结果分开跟踪（`parse_current_weather`/
  `parse_forecast` 改返回是否解析成功）：**完整刷新**才推进时间戳 + 城市名 +
  落盘；**部分刷新**不推进、不落盘（下次进屏即重试，不再等满 1 小时），并经
  `partial_refresh` 标志（任务写/LVGL 定时器读）在状态行保留
  `Partial data - press r to retry` 提示；**全失败**缓存不动（原行为）

### 9.2 CI 路径过滤不含 `script/**` ✅

- **差异**：`script/set_srcdir.py` 决定矩阵实际编译哪个示例（第三批 7.1 刚修过它的
  优先级 bug），但 `.github/workflows/platformio.yml` 的 `on.push.paths` 只有
  `examples/**`、workflow 自身、`platformio.ini`——单独改 set_srcdir.py 会**完全
  跳过 CI**，上次那类"矩阵全绿但编错源目录"的问题可再次静默发生
- **修复**：`153eef7` — paths 追加 `script/**`（带注释说明缘由）

### 9.3 `factory.ino` 的 TLS 初始化声明与头文件不一致 ✅

- **差异**：`factory.ino:757` 局部声明 `extern bool openai_tls_apply(void);`，而
  `openai_api.h:112` 实为 `void openai_tls_apply(void);`——跨翻译单元声明不兼容
  （`950fcfe` 引入的笔误；当前调用丢弃返回值通常能链接，但属 UB 邻域）
- **修复**：`3475c9b` — 局部声明改 `extern void openai_tls_apply(void);`
  （与相邻 `extern void openai_stats_poll();` 风格一致）

### 9.4 冷启动 + 仅 forecast 成功时无 partial 提示 ✅（2026-08-22 `71fa528`）

- **来源**：Kimi 双评审 `wifi-config-keyboard-review-result-kimi-c27cb39..3475c9b.md`
  §1.1 Low 登记（A 全量接受，不阻塞）
- **差异**：`data_valid` 只被 `parse_current_weather` 置位（`ui_weather.cpp:183`），
  `parse_forecast` 成功不置位——冷启动无缓存时若 current 失败、forecast 成功，
  `refresh_cb` 不进 `update_ui()`，`Partial data` 提示不显示，forecast 虽已解析
  但不上屏
- **影响**：需"设备首次使用 + current 端点单独故障"同时成立，概率低；时间戳
  未推进，下次进屏重试自愈
- **修复**：`71fa528` — `parse_forecast` 尾部置 `data_valid = true`（解析出的
  forecast 本身就是值得上屏的数据；partial 提示路径随之复通）
- **P2 跟进**（`141942d`，Codex 结果 `71fa528..a58a73c` **C 部分接受**）：
  首修复置位过宽——`list` 存在但为空/条目全过期/缺 `dt` 时两计数为 0，
  仍被判有效、零值天气上屏；改按 `hourly_count > 0 || daily_count > 0`
  门控（0 条返回 false → 归入 partial/失败，不推进时间戳不落缓存，
  下次进屏重试）

## 10. PenPal 实现评审发现（Codex 结果 `wifi-config-keyboard-review-result-b48f584..5329383-codex.md`，2026-08-22 到达；当日全部修复 `acc3893`）

- **P1-a**：`ppw_payload_build`/`penpal_polish` 对含 `std::string` 的
  `pp_send_req_t`/`pp_polish_t` 用 `memset` 清零——破坏已构造 string，
  首次 Send/Polish 即 UB。修复：一律值初始化 `*out = T{};`
  （`pp_fix_t`/`pp_tips_t` 本是 POD，顺带统一；规则=任何 `pp_*_t` 不 memset）
- **P1-b**：`scr_mgr_register` 在**开机注册时**就调 `create()`（本仓
  屏幕生命周期语义，penpal 首个依赖此事实的屏），create 期自动同步的
  代次必被 `pp_entry()` gen++ 作废，stale 分支又不释放 busy → 已配置
  设备每次进入永久卡 busy。修复：自动同步移 entry（gen++/active 之后），
  `s_pp_autosynced` 一次访问一次 + destroy/Cfg 保存复位；**同族路径**
  （后台 SEND 期间退屏）由 stale 丢弃分支补释放 busy
  - **根因表述勘误（2026-08-26，Claude acc3893 复核）**：`ui_scr_mrg.c:33`
    的 `create()` 位于 `scr_mgr_default_style()`，仅被
    `scr_mgr_active()`（push/switch 路径）调用；`scr_mgr_register()`
    只挂链表节点**不调 `create()`**。实际生命周期 = 每次 push 建
    create+entry、每次 pop 跑 exit+destroy 并删整棵控件树——"开机建
    一次"的描述有误。修复本身不受影响（entry 恒在 create 后、gen++
    恒先于自动同步，两种模型下时序等价；`s_pp_autosynced` 在每访问
    必重建的生命周期下为防御性冗余）。CLAUDE.md working notes 同步勘误
- **P2**：READ Close 不中止任务（LLM 最长 180s），连续 Close/重试无界
  堆积 8KiB 栈任务。修复：`s_pp_inflight` 原子计数 + 非链式请求上限 2
  （1 僵尸 + 1 新）；可中止传输登记为可选后续
- 修复路径真机回归（自动同步/busy 释放/并发上限/保存后重同步）⏸ 待用户
  实测；申请 `acc3893`

## 11. 8 月 commits 全量评审登记（Kimi，B3/B4 产出，2026-08-22）

> 来源：`docs/reviews/2026-08-commits-review-status.md` §2.2/§2.3。
> B3（AI Chat 21）/ B4（系统杂项 18+3）结论：HEAD 无遗留 High/Medium；
> 以下 5 项 Low + 1 项契约层 Medium 为待办登记。B1/B2/B5 因额度 403 未跑完。

- ⬜ **B4-L1（Low）**：`ui_deckpro.cpp:2314`（`7d5aa8d`）临界区内"事件抢先发布"
  复检为死代码（刚设 `target=cnt` 后判 `cnt>target` 永假）——删除或移到
  `esp_wifi_scan_stop()` 之后
- ⬜ **B4-L2（Low）**：`ui_weather.cpp` 'r' 键先清 `last_fetch_time` 再调
  `start_fetch()`（`fbfc16c`），WiFi 断/key 缺/任务在飞时缓存永久过期、进屏
  空重试——改为任务真正创建后才清。**注意**：weather 此后历经
  `c27cb39`/`71fa528`/`141942d` 改动，修复前先对照 HEAD 复核该路径是否仍存在
- ⬜ **B3-F12（Low）**：`chat_exit()` 隐藏 waitbox 但未隐藏 New 确认框
  （`chat_confirm_close()` 仅 destroy 调用）——push-away 泄漏同类，对称修复一行
- ⬜ **B3-F13（Low）**：Chat 页键盘分支无 `c >= ' '` 守卫（`b47dd4c`），
  `'\v'` 已修（`a9873bd`）但其余控制字节仍会写入草稿并发往 API
- ⬜ **B3-F14（Low）**：`disp_flush()`/`flush_timer_cb()` 抑制期强制
  `DISP_REFR_MODE_PART`（`955a492`），会降级触摸滚动期间其他屏发的全刷请求
  （自愈）——仅在非 FULL 时设 PART
- ⬜ **契约层（Medium，先于 8 月批次存在）**：weather fetch 任务不在
  `docs/async_ipc_contract.md` 契约表内也不遵守契约（无页面代次；
  `weather_cleanup()` 直接 `vTaskDelete` 强杀在飞任务，栈上 HTTPClient/
  Preferences 自动对象被连带释放）——建议天气纳入契约（代次 + busy_gen、
  任务跑完丢弃迟到结果），或在契约中显式登记为例外
- 注：PenPal v2 设计复审的 4 项 Low（`penpal-design-review-result-kimi-v2.md`
  §3）因实现已落地（§10 批次）不再另行登记，如需追踪对照实现复核即可

## 12. PenPal 首轮真机回归发现（2026-08-22，三处同日修复并复测通过）

> 首轮真机回归（acc3893 烧录后）暴露 1 崩溃 + 1 导航缺陷 + 1 易用性需求；
> 串口证据 + 用户复测全过。相关 commit：`423b312`（penpal）/ `e70b591`
>（wifi）/ `bfa7a16`（menu）。

- ✅ **P0 返回即重启（每次必现）**：串口 `Stack canary watchpoint triggered
  (loopTask)`，崩点在 `scr_mgr_pop` 链内（`[KBD] char fifo cleared` 已打印）。
  根因 = `pp_destroy` 的 `pp = pp_state_t()`：**~15KB 聚合临时对象整体压栈**
  （mailbox 24 行 + topics 16 + letters 64 + 杂项；loopTask 栈仅 8KB），任何
  调用深度必炸。修复 `pp_state_reset()` 逐字段原地复位（数组逐元素值初始化，
  最大元素临时 ~370B；字符串走元素析构，不 memset——§10 规则）；点击返回
  同时改为 `lv_async_call` 延迟 pop（点击栈先回退，双击有 top==root 保护）。
  **规则沉淀：UI 线程禁止对大聚合做 `= T()` 整体赋值**（全库扫描其余同款
  均为 ≤400B 小结构，安全）。复测：点击/键盘返回均正常，
  `[PenPal] destroy done` 打出，无重启。
- ✅ **菜单滑动跳页（1→3 跳过 2）**：手势轮询器每 30ms 读
  `lv_indev_get_gesture_dir`，而 LVGL 的 `gesture_dir` 从检测到下次按下前
  一直有效（`lv_indev.c` 按下起点才复位 + `gesture_sent` 单发）→ 一次滑动
  回调连发 N 次；两页时代被边界钳位掩盖，三页暴露。修复 = 边沿触发
  （NONE→方向 只发一次）+ 回调 NULL 防护。复测：逐页翻，双向正常。
- ✅ **WiFi Test 增加本机网卡 IP**（用户需求，排查 PenPal 服务器可达性）：
  结果框 `Public IP:` + `LAN IP:` 两段，串口同步输出。
- 📌 **配套设备侧操作（无跟踪文件变更）**：SPIFFS `/env.cfg` 注入
  `PENPAL_BASE=http://192.168.3.186:8000` + hunter 测试 key（一次性 writer
  固件 `other/env_writer/`，gitignored；不走 uploadfs 避免 SPIFFS 整区擦除，
  与 /chat.log 等共存）。**sync 真机回归仍 ⏸**：等服务器侧重启为
  `--host 0.0.0.0`（现为 127.0.0.1，设备连不进）+ 防火墙放行 8000 入站。

## 13. c1c6a14..ff6d906 四方评审修复批次（2026-08-26）

> 来源：Codex / Claude / Gemini / opencode 四份结果对同一申请
> `wifi-config-keyboard-review-request-c1c6a14..ff6d906.md`。
> 采纳：Claude P1 + Codex/Claude P2×2 + Gemini M1 + opencode P2-1/Low×4；
> 拒绝：Gemini M2（失实——`exit4_1` 早已调 `wifi_cfg_popup_close_cb`，
> 见 `ui_deckpro.cpp:2621`）、Gemini M3（与设计 §6.3 既有登记重复）。
> Gemini 对中文超时提示的正面评价不成立：montserrat_14 无 CJK 字形，
> 实际渲染为方块（opencode P2-1，已改英文）。

- ✅ **P1（Claude）WiFi 槽位切换静默丢草稿**：`wifi_cfg_set_slot()` 的
  `wifi_cfg_sync_draft()`（只同步聚焦框）结果两行后被 `wifi_slot_load()`
  覆盖 = 死代码，未保存的 SSID/Pass 切槽即丢。修复：切换前直接读
  **两个** textarea 自动保存回旧槽（`wifi_slot_save`），设计文档 §3.5
  同步改"切换 = 自动保存草稿"语义
- ✅ **P2（Codex/Claude）PenPal provider 状态行显示旧值**：
  `pp_cfg_status_text()` 从 NVS 读已保存 provider，下拉切换后预览不跟随。
  修复：按 `s_cfg_provider_idx` 即时预览（顺带修复 Gemini M1 的
  `ai_provider_enum` 返回值未检查）；重进屏时下拉先从 NVS 同步，语义不变
- ✅ **P2（Codex/Claude）两步保存混合状态**：Server 与 provider 分两次
  NVS 写，第二步失败只报笼统 `save failed`。修复：分区报告
  `server saved; AI provider save failed`
- ✅ **P2-1（opencode）中文错误文案 tofu**：`读取响应超时`/
  `等待返回超时` 在 montserrat_14 下渲染为方块。修复：改英文
  `Empty response (timeout?)` / `Request timeout\n(check network)`
- ✅ **Low 批（opencode）**：① `out.resize(200)` 加 UTF-8 边界回退
  （penpal 同款模式）+ `fail_buf[192]→[240]`；② PenPal Cfg provider 焦点
  下 `\b` 返回 HOME；③ `http.header()` 诊断日志补 `collectHeaders()`
  （原日志恒空）；④ WiFi 结果弹窗吞键后 `keypad_clear_chars()` 清残留
  FIFO（先例：blocking connect 后清队）
- 真机回归 ⏸：槽位切换自动保存、provider 预览/分区保存、英文超时文案
  （原批次 PenPal provider 共享回归项一并跑）
- ⬜ **N1（Low，Claude 二轮）**：`openai_api.h` `ai_provider_get()` 头注释
  "Resolution order (later wins) 1→4" 与实现不符——活动槽 base 匹配时槽内
  key 优先，env.cfg 不被查询（无 "later wins"）。正确表述见
  `design-penpal-ai-provider-link.md` §3.1，头注释应同步
- ⬜ **N2（Low，Claude 二轮）**：`ui_ai_cfg.cpp` 文件头与 `ai_cfg_create()`
  尾注释仍写 "Save requires a successful Test" / "Run Test to enable Save"，
  与 c1c6a14 用户明示的 Save/Test 解耦矛盾，应同步注释防止日后恢复门禁
- ⬜ **仓库卫生（Claude 二轮，非阻塞）**：① `ui_deckpro.cpp` 工作区有未提交
  菜单布局对调（PenPal/Sleep/Shutdown，2026-08-26），不属任一申请范围；
  ② 5 个源文件磁盘为 CRLF、仓库 blob 为 LF（无 autocrlf/.gitattributes），
  git status 应显示已修改——建议恢复 LF 或补 `.gitattributes`

## 14. PenPal 响应缓存 + msgbox 触摸关闭（产品需求，2026-08-26）

> 需求（用户）：① 不点 Sync 时 HOME 从缓存读 mail list，点 Sync 删缓存
> 重拉；② 点行开线程先读缓存，THREAD 右上角加强制刷新按钮；③ 缓存
> 有效期 2 天；④ TIPs 错误弹窗无关闭按钮（触摸路径缺失）。

- ✅ **缓存层**（`penpal_api.cpp`）：SPIFFS `/penpal/pals.json` /
  `mailbox.json` / `th_<root_id>.json`，文件 = `<fetched_at>\n<原始响应体>`；
  getter 网络成功后写（worker 线程）；`penpal_cache_load_*` 读 + 复用
  拆出的 parse-only 解析（pals/mailbox/thread 三段从 getter 中拆出共享）。
  TTL 2 天；时钟未同步期写入（`fetched_at=0`）视为有效——显示旧数据优于
  空白，手动 Sync 总可强刷。缓存文件非关键数据，读/解析失败一律当 miss。
- ✅ **HOME**：`pp_entry` 自动同步前先试缓存（pals+mailbox **全命中**才免
  网络），直接解析进全局 `pp` 状态（**禁止栈中转**——24 行 mailbox
  ≈3.8KB，§12 规则；miss 时半更新状态被随后的网络 sync 覆盖，无害）；
  `pp_home_sync(manual=true)` drop home 缓存（键盘 `\n` 与触摸 Sync 同路）。
  发信后 auto-sync（`manual=false`）不 drop、走网络并覆盖缓存。
- ✅ **THREAD**：`pp_home_row_cb` 先试 `th_<root_id>` 缓存（升序数组原地
  反转，消费语义与 PP_RES_THREAD 一致）；标题行右端（`< Thread` 同行）新增
  **Sync 按钮**
  （44×26 文本——自定义粗体字体无 LV_SYMBOL 字形，图标不可用）强制重拉；
  消费端按 `s_cur_page` 分支：HOME→切页，THREAD（刷新场景）→
  `ppr_show_thread()` 重渲染。dropped 提示从计数标签挪进信头
  （长文本会压到 Sync 按钮）。
- ✅ **msgbox Close**：`pp_msgbox_show` 加 Close 按钮（waitbox 同款），
  修 TIPs 失败弹窗触摸不可关。
- 设计文档 §5/§4.1/§4.4/§7 + 变更历史同步；真机回归 ⏸（§7 缓存路径项）

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
