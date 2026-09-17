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

> **⚠ 2026-09-11 推翻**：本条结论错误，保留原文仅作过程记录。机 #1
> （4G/A7682E 批次）在 `pcm5102a_init()`（含 **GPIO41/BOARD_6609_EN 拉高**）
> 运行的主固件下，Test→PCM5102A 与 PCM5102 app 播放 `/pcmtone.wav`
> **耳机实测出声**（当日 A7682E 因无 SIM 初始化失败，菜单门控显示出
> PCM5102 入口）。8 月探针无声的真正原因：`test_i2s_probe` 没有调用
> `pcm5102a_init`，**音频电源轨从未打开**——不是芯片缺失。GPIO41 在
> 4G 批次上同时是模组电源与音频段电源（§16.1 规则 1 对两台机均成立）。
> 产品含义：**两台机都支持 TTS 朗读/音频输出**，allinone §4 被取消的
> MP3 屏可重新评估。

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

## 15. PenPal 点击死机根因：LVGL 48K 池耗尽（2026-08-28..29 定位，修复 `721e04a`）

**现象**（真机多轮上报）：进入 PenPal 后点任何链接（Sync / 邮件行 / topic
pick）死机或重启；1269bf7 时代已有偶发 topic 选择死机（当时无日志）。

**定位过程**（设备三段 bisect + 池水位探针）：
- `1269bf7` 整刷实测**稳定**；`f8d73f6`（邮件行表头拆两 label，净 +10 个
  LVGL 对象）进列表后**一点即崩**。回溯解码：
  `indev_proc_release → pp_home_sync_cb → pp_start → pp_waitbox_show →
  lv_btn_create → lv_obj_mark_layout_as_dirty`，LoadProhibited `0x22`
  （对象树父链已坏，等待框按钮创建时走到 garbage parent）。
- **Step B 探针**：1269bf7 + 10 个"只创建、不写文本"的空 label（渲染逻辑
  完全不动）**同样崩** → 排除渲染逻辑，锁定"多 10 个对象"本身。
- 探针带 `pp_dbg_pool()` 池水位（[PD] 串口行）实锤：

  | 检查点 | 48K 池剩余 |
  |---|---|
  | `create`（7 页控件树建完） | 2172 B |
  | `cached`（pals+mailbox 缓存渲染后） | 540 B |
  | `wbshow`（点击 Sync、建等待框前） | **524 B** |

  等待框（box + 2 label + btn + 文本缓冲）需要数百字节 → 分配失败 →
  冻结/panic（本版表现为死机无 panic，与"分配失败后 LVGL 内部断言死循环"
  相符；f8d73f6 版本表现为 LoadProhibited——TLSF 边缘行为不定）。

**根因**：PenPal 7 页控件树 + pals/mailbox 渲染把 48K LVGL 池吃到 ~0.5K。
`1269bf7` 的"稳定"只是 ~1K 余量**勉强塞下**等待框——早已在悬崖边，任何
多分配一点的路径（topic 列表、行表头 +10 label）都翻车。诊断构建里崩溃点
漂移（`lv_mem_buf_get` 挂死 / layout-dirty panic）同样是池耗尽的不同表现，
不是独立 bug。

**修复**（`721e04a`）：`LV_MEM_SIZE` 48K→64K（`config/lv_conf.h`），app RAM
50.1%→55.1%；同检查点水位 ~17K。`pp_dbg_pool()` 留作观测点
（create/cached/wbshow）。真机复测：邮件列表 + Sync + 开信 + topic pick
全稳定（用户确认 2026-08-29）。

**教训**：① 改 `config/lv_conf.h` 必须先 `pio run -t clean`——它经
`-include` 进编译，**不被依赖跟踪**，增量编译会用旧 .o 假装生效（build
文档坑 7）；② "稳定版"池余量 ~1K 就应视为临界——水位观测点进主线，别再
靠"没崩"判断健康。

## 16. 测试机 #2 入池：V1.1 音频选配版（2026-09-10 判定）

**背景**：第二台设备接入（COM6，ESP32-S3 MAC `10:20:ba:34:19:ec`），跑
**卖家出厂固件**（SPIFFS 仅 LilyGo demo 素材 6 个 MP3，无 `/env.cfg`；
无本项目的 `[EPD] reset mode` 打印）。本节记录其硬件身份的判定证据链。
（注：判定当日为 2026-09-10；此前 CHANGELOG 曾误记 08-29，已更正。）

**证据链**（三步，全部闭环）：
1. **V1.1 批次**：I2C 扫描发现 **DRV2605 (0x5a)**——LilyGo 官方版本表：
   V1.0（HD-V1-250326 分支）无 DRV2605，V1.1（**HD-V2-250915**，即本项目
   分支）有。机 #1（COM5）无此芯片 = V1.0 批次跑 EPD 兼容模式。
2. **无 4G 模组**：启动日志 `[A7682E] Init Fail`（`modem.testAT()` 6 次
   无响应 + PWRKEY 断电重启重试——SIM/天线缺失不会导致 AT 握手失败，
   见 factory.ino A7682E_init）；且原厂固件**菜单硬件门控**：
   `ui_test_a7682e()==false` 时 A7682E 菜单项整体替换为 PCM5012 app——
   用户在菜单看到该 app = 固件自身判定无模组。
3. **PCM5102A 在板且工作**：用户插耳机实测 PCM5012 app 出声
   （`audio.connecttoFS(SPIFFS, "/iphone_call.mp3")`，I2S GPIO 7/8/9）。

**结论**：V1.1 **音频选配版**（PCM5102A 出厂可选购配置之一，与 4G 版
互斥，§3.3）。本机音频硬件路径：ESP32-S3 I2S → PCM5102A → 3.5mm 耳机孔
（无扬声器/功放，只能耳机）。PDM 麦克风（官方引脚表 GPIO17/18）是否在板
未验证。

**产品含义**：
- **读信 TTS 可行**（本机）：服务端合成 MP3 → 设备 SPIFFS → Audio 库
  （`Audio audio`/`pcm5102a_init()` 已在树）→ 耳机。
- 机 #1（4G 版）无音频出口（§3.3），同一固件两台设备音频能力不同——
  运行时可用"无 A7682E 应答"判音频版（原厂门控同款逻辑）。
- **移植前置工作**：本项目固件若刷到本机，触摸需接 CST328 驱动
  （现树只有 hyn/CST66xx 路径；CST328 与 CST3530 同地址 0x1a，初始化时
  需辨别，LilyGo issue #37，且 CST3530 建议中断而非轮询）。

### 16.1 麦克风判定：在板、低灵敏度（2026-09-10 晚定案，`test_pdm_mic` v1..v11）

初判"不可用/未焊"后，用户在回放里听到了**房间外放音乐**，追加持续
元音对照实验后修订为：**麦克风在板、声学通路存在，但灵敏度比正常麦
低约 20~30dB**。

- **证据**（v11，逐秒 VU）：环境底噪 150~650；持续大声"啊——"近距离
  拖音：整窗抬到 **1450~3394**（峰值首破 3000，判定链首次触发：
  `SPEECH captured (gain 8.8x)` + 三声滴 + 回放，日志行 2364-2374）；
  外放音乐经 16×增益回放可闻（用户亲耳证实）。
- **此前误判的成因**：正常音量对话（600~1300）与底噪重叠——11 版里
  所有"安静窗口"皆由此；"语音从未超过底噪"只在非拖长音/非贴脸条件下
  成立。v1 时代巨峰（20000~34000）与"手持贴脸说话"相符，撤回"机械
  耦合"解释。
- **可用性（终判，2026-09-10 深夜）**：大声 + 近距（对嘴）+ 数字增益
  （200Hz 高通 + 24× 归一链，探针已实现）条件下可拾音，且**通过 STT
  实测**：跟读录音导出 PC（`script/probe_capture.py`，DTR 关断开串口防
  复位覆盖 + base64 重试），GLM-ASR-2512（`script/stt_test.py`）对跟读
  "The quick brown fox jumps over the lazy dog." **逐字转写正确**（仅尾
  部多一个 "one" 瑕疵）。人耳验收与机器识别双通过——**语音输入功能在
  本机可立项**（交互前提：对嘴大声说话）。
- **探针副产物**（对后续音频功能全部适用，均已固化在
  `examples/test_pdm_mic`）：
  1. **GPIO41（BOARD_6609_EN）= 音频输出段电源**，拉高才有声——出厂
     `pcm5102a_init()` 就是这么做的；4G 版上该脚是模组电源（复用）。
  2. **Audio 库驱动重建须逐字段复刻构造配置**（16000Hz、
     `tx_desc_auto_clear=true`）——自拟配置（44100/无 auto-clear）会
     "解码流水线全跑但无声"。
  3. 库的 I2S 驱动只在构造时装一次；PDM 录音必须借走 I2S0（S3 的
     I2S1 不支持 PDM），录完需按第 2 条重建才能再播。
  4. 所有实证可听的播放均为 44.1k 立体声；16k 单声道 WAV 播放路径
     可疑（从未被清楚听到过），录音回放先上采样到 44.1k 立体声。

## 17. 键盘/触摸焦点路由缺陷（PenPal CFG 与 COMPOSE，2026-09-13 修复，`7ee150b`/`cfe993d`/`54cfc79`）

**现象**（用户报告两轮）：① CFG 页内部焦点在 Server Key 时，⌫ 删的是
Server URL 的字符；② COMPOSE 页触摸 Title 框后打字，文字落进 Body。
两处共同点：**用户看到的输入位置（LVGL 光标/触摸点）与键盘路由变量
（`s_cfg_focus`/`s_focus_title`）不一致**。

**根因**（两类，对应两个修法）：

1. **焦点状态跨页面残留**：`s_cfg_focus` 是 file-static，离开 CFG 页时
   不重置，而 `pp_cfg_prefill()`（每次进页必跑）只重填输入框内容。
   上次访问焦点停在 KEY → 本次重进，内部焦点仍是 KEY，⌫ 即作用于
   URL 框。**修复**：prefill 首行 `pp_cfg_focus_set(PP_CFG_FOCUS_BASE)`
   （`7ee150b`）。
2. **触摸不动键盘路由**：textarea 挂 `LV_EVENT_FOCUSED` 回调后，触摸
   只移动 LVGL 光标（视觉），键盘路由变量不跟随——用户"看到光标在
   Key 框"，⌫ 实际删 URL。**修复**：加 FOCUSED 回调同步内部变量
   （CFG：`pp_cfg_base_focus_cb`/`pp_cfg_key_focus_cb`；COMPOSE：
   `ppw_title_focus_cb`/`ppw_body_focus_cb`，`cfe993d`）。

**审查同类页面**：wifi_cfg（`wifi_ssid/pass_focus_cb`）与 ai_cfg
（`ai_ta_focus_cb`）**均已有触摸同步**——PenPal 是唯一缺口（CFG 两框 +
COMPOSE 两框共四处）。已补齐。

**经验沉淀**（适用一切"标签 + 输入框 + 物理键盘"的屏）：
1. **任何"选中态"必须有三个同步点**：页面进入时重置、Tab 循环时更新、
   **触摸点选时更新**——第三点最容易漏（LVGL 光标自动跟触摸，制造
   "已切换"的假象）。
2. **无可见焦点指示 = 用户必然误判**。wifi_cfg 用 ">" 标记前缀；
   PenPal CFG 当时无任何标记（后按需补）——纯键盘驱动的屏必须给
   焦点做视觉锚。
3. **路由变量的生命周期审查**：file-static 的"当前 X"变量，每次进入
   相关页面时应显式重置或与持久化状态对齐（对照 `pp_cfg_prefill`/
   `wifi_cfg_set_field(0)` 两处先例）。
4. Tab 循环经过下拉/按钮类字段时，⌫ 在该字段上应有明确语义（PenPal
   CFG：PROVIDER 上 ⌫ = 返回 HOME，与空框退出一致）。

## 18. V1.0 面板假死事件（2026-09-15 定位/修复，机 #3 `28:37:2f:91:2c:20`）

**现象**：机 #3（V1.0 批次，无 DRV2605）在当日 OTA 构建 + 出厂固件实验后
"无法启动"——屏幕永远停在 Sleep 界面；换任何固件（OTA 构建 / 仓库 HEAD /
V1.1 预 OTA 镜像 / 出厂 v1.2）表现一致：`ink_screen_init()` 起每页 EPD 刷写
"Busy Timeout!"（每页约 10s，启动累计 ~65s），I2C 扫描缺 0x1a/0x5a（该机
常态）→ 判 V1.0 → soft-only 复位路径。曾误诊为"V1.0 硬件天生慢启动"。

**根因**（三层，全部有实证）：
1. **墨水屏面板控制器假死**：某次硬复位撞上面板电源动作窗口（升压电路
   工作中被断控制）→ BUSY 永久不释放、玻璃画面冻结。GxEPD2 的
   `_waitWhileBusy` **超时后照常返回**、`done_seq` 照常推进——软件认为
   "画完了"，玻璃从未更新（与 OTA 设计稿 §5.2 自证语义声明同一事实）。
2. **唯一恢复手段是真断电，而电池一直托着面板供电轨**：拔 USB / 刷机 /
   软复位都不给面板断电；开盖**拔电池 10s** 后一切恢复正常（启动无任何
   Busy Timeout，用户验证）——修复即诊断实锤。
3. **触发源排除**：正常 Sleep→唤醒循环本身是干净的（先 `powerOff()` 再
   睡、唤醒走完整初始化），三台机含本机此前反复睡眠均无恙；当日异常点
   是诊断脚本在**启动中途**的 DTR/RTS 复位脉冲与 esptool 下载模式复位
   可能撞上唤醒后的面板上电窗口 + v1.6 出厂固件 2s 重启风暴多次打断
   面板初始化——正常使用不会产生这种时序。

**教训沉淀**：
1. "机器在跑（串口/USB 活）+ 屏幕不动" = 面板假死特征组合，直接拔电池
   10s，不要换固件排查（§19 的出厂固件矩阵实验即是教训代价）。
2. **复位/刷机避开唤醒后的面板上电窗口**（开机头几秒）；诊断脚本复位
   设备前先确认它不在 EPD 初始化段。
3. Busy Timeout 连续出现 = 面板没在真实刷新——不要把"每页超时但软件
   走完"当成"慢"，那是假死/半死的形态。
4. 我方固件侧无规避改动：V1.0 无面板复位线是硬件事实；正常操作频率下
   该窗口极窄，不值得为此禁用自动休眠（用户裁决：保留，观察）。

## 19. 出厂固件矩阵实测（2026-09-15，firmware/ 目录 @ master）

为排除固件因素对机 #3 做的交叉实验，结论供后续救砖参考：

- **v1.2 完整镜像**（`H693_factory_v1.2_20250412_fix.bin`，16MB @0x0，含
  bootloader+分区+SPIFFS）：能跑但**驱动不了 V1.0 面板**——V1.0/V1.1
  硬件判定是 v1.3（2025-10-15）才加入的，v1.2 只认一种板型（按有复位线
  初始化）→ 屏幕保持残影（易误判"没启动"；USB 稳定在线 + 串口静默 =
  出厂固件不开 USB CDC 控制台，非死机）。
- **v1.6 应用镜像 + v1.2 bootloader**：**重启循环**（`rst:0x15`/RTC_SW_SYS_RST
  每 2s，无任何输出）——跨一年核心版本不兼容（另有 v1.6 新增低电压自动
  关机嫌疑）；出厂目录只发布 v1.2 完整镜像，v1.3+ 均为裸应用，**不要混刷**。
- 出厂分区表 = `default_16MB`（与本仓 boards/T-Deck-Pro.json 相同），全片
  擦除后可直接 `pio run -t upload` 恢复本仓固件。
- 救砖路径实测：BOOT+RST 进 ROM 下载模式 → esptool 整片擦除 → 本仓固件
  + `uploadfs` → 正常（§18 记录的 SPIFFS/env 恢复见 CHANGELOG 当日条目）。

## 20. USB 枚举问题集（2026-09-15，VL817 集线器 + 深度睡眠 + 复位时序）

- **VL817 集线器端口枚举失败**："未知 USB 设备(端口重置失败)"，换线/
  换集线器下不同口无效；主板直出根集线器口（位置 `1-1`）全天可靠。
  电脑整机重启不清除该状态（集线器或有独立供电）。**操作规范：刷机一律
  用主板直出口**。
- **深度睡眠 = USB CDC 关闭**：自动休眠触发后 COM 口消失，插线不应答，
  症状与坏口相同——先按 RST/BOOT 唤醒再排查口。唤醒后 5 分钟内会再睡，
  刷机动作要抢在窗口内。
- **原生 USB-JTAG-Serial 的复位时序陷阱**：经典 DTR/RTS 脉冲
  （DTR=F→RTS=T→RTS=F）在这块板上是**进入下载模式**而非普通复位——
  `scripts/capture_boot.py` 首版因此每次把设备按回 download mode；纯复位
  用 `esptool <any> --after hard_reset`，无脉冲抓日志用
  `scripts/read_serial.py`。

## 21. 会话批次固件规则沉淀（2026-09-15，commit `71c09e7`，评审申请
`docs/reviews/session-batch-review-request-71c09e7.md`）

当日八组变更（CA 根修复 / 语音多轮上下文 / 自动休眠 / PenPal 三修复 /
Whoami App / 菜单重排 / TTS 开关 / OTA 实现）细节见评审申请；此处只记
三条踩坑换来的通用规则：

1. **结果队列的生命周期**：worker 存活可跨屏的异步框架，其结果队列
   **一次创建、永不删除**，排空放全局轮询先排后判（PenPal/Whoami 双
   先例；Whoami 首版 destroy 删队列 × 在途 worker `xQueueSend` 竞态 =
   `xQueueGenericSend (px_queue)` 断言重启，串口实证）。任务栈 ≥8KB
   （TLS+cJSON 链路 6KB 必溢出且**静默死**——"Test 卡在 Testing..."）。
2. **LVGL 全屏页面容器的 Z 序**：整页容器在顶栏按钮**之后**创建会把
   按钮整个盖住（后建兄弟在上层）——页面先建、按钮后建，或按钮挂到
   页面内部（PenPal 先例）。症状是"按钮点不到 + 无法退出"，易误诊
   触摸漂移。
3. **EPD 上 lv_switch 的状态样式**：默认态样式会被主题的 CHECKED 选择器
   覆盖——ON/OFF 配色必须绑 `LV_PART_INDICATOR | LV_STATE_CHECKED`
   选择器才生效（Trust/TTS 开关两处首版"改了没变化"的根因）。

---

## 22. "OTA 版本致死"假案：刷写不完整 + bootloader 哈希拒绝（2026-09-16，
机 `28:37:2f:91:2c:20` 与 `10:20:ba:34:18:5c`，commit `095e41a` 当天两台
同症状"变砖"）

**现象**：刷入 `095e41a` 后两台机同症状：`rst:0x3 (RTC_SW_SYS_RST)` 每
~0.4s 循环、屏幕死、点触无响应、**零应用串口输出**。一度怀疑与 §18 面板
楔死同类（要拔电池）。

**结论：固件无罪，是刷写不完整**。三层证据：

1. **循环日志里的 `entry 0x403c98d0` 是 2nd-stage bootloader 的入口**
   （0x403C8000 IRAM 窗），不是应用——应用从未启动，所以零输出。
   bootloader 校验镜像哈希失败即静默复位，形成死循环。
2. **二进制级 diff（map 汇总）**：`71c09e7`→`095e41a` 唯一实质差异 =
   `.flash.rodata` +0xDDAC（`ca_bundle_full.h` 的 static 常量被两个编译
   单元各包含一份，2×56.7KB flash；`.dram0.data`/`.dram0.bss`/
   `.iram0.text` 全部 ±0）。纯只读数据不触任何启动路径；app0=0x640000
   也远未溢出。
3. **决定性实验**：同一台"变砖"机用强制逐块校验流程完整刷入 `095e41a`
   → 30s 零复位、外设全链路正常启动。当天的"变砖"是分块刷写重试时
   **没有逐块核对 "Hash of data verified."**（只看了 "Leaving..." 尾行），
   而 USB CDC 在持续传输约 1/4 块后必然掉口（§20 同族问题，换波特率
   无效、失败偏移可复现），至少一块从未写入。

**恢复：不需要拔电池**。ROM 下载模式先于应用运行，esptool 永远够得着；
用 `scripts/flash_verified.py`（本次事件后入库：分块 + 每块强制哈希校验
+ 失败即中止退出 + 可选整段回读比对）重刷即可。

**规则**：
1. 手工 esptool 分块刷写时，**每块必须 grep "Hash of data verified."**，
   尾行 "Leaving..." 不构成成功证据；
2. CDC 掉口后重试前 `sleep 2` 等端口重枚举，重试上限 4–6 次；
3. 诊断"零输出复位循环"先看 `entry 0x403c…` 归属：bootloader 入口段
   （0x403C8000+小偏移）= 应用根本没跑，嫌疑在镜像/刷写，不在代码逻辑；
4. **串行单机刷写**（用户裁定 2026-09-16）：任何时候只对一台设备烧录/OTA，
   严禁两台并行——一个坏镜像（或刷写缺陷）不应有同时砖掉全部设备的机会；
   本事件两台同症状"变砖"正是该风险的现实演示。

**顺带核实**：Arduino 核心 sdkconfig `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`
属实；两台机 otadata 均 seq=1/state=VALID（71c09e7 每次开机自动标 valid），
`verifyRollbackLater()` override 在 VALID 态为空操作、真机验证无影响——
但 PENDING_VERIFY 真实路径（OTA 更新后首次启动）仍待回滚真测矩阵覆盖。
另：`ca_bundle_full.h` 双拷贝（114KB flash）非缺陷，去重留作低成本优化。

---

## 23. EPD 灰色文字系统性不可见 + WiFi 扫描列表中文名空洞（2026-09-16 真机
回归发现，v1.1 修复）

**症状**（用户真机回归 v1.0）：① Whoami Cfg 页看不到 "FW: vX.Y" label；
② Wifi Scan 列表屏无数据。②与 v1.0 无关（存量）；①是 v1.0 新增 label
用了项目惯用的灰色状态样式，踩中了同一个存量地雷。

**根因 1——EPD 硬阈值二值化**：`convert_lvgl_buf_to_epd_bitmap()`
（factory.ino）按 `lv_color_brightness < 128 → 黑、否则白` 转换。LVGL
`LV_PALETTE_GREY` = 0x9E9E9E（158 ≥ 128）→ **渲染为白色，白底上不可见**。
全项目 14 处灰色样式（PenPal/AI Chat/AI Cfg/Voice AI/Weather/Calculator/
Dictionary/PenPal Write/Wifi/Whoami/OTA 屏的状态行 + Weather 表格边框）
**全部从未显示过**——历轮"状态反馈缺失"类报告（含 OTA 下载状态、AI Test
状态）部分由此解释。修复：全部改 `lv_color_black()`（v1.1，两个 commit：
报告内三处 + 其余机械清扫）。

**根因 2——WiFi 扫描 CJK 过滤留空洞**：`ui_wifi_get_scan_info()`
（ui_deckpro_port.cpp）跳过中文名 SSID 时 `continue` 不压缩列表，
`show_wifi_scan()` 遇第一个空行即 `break`——扫描结果按 RSSI 排序，附近
最强的 AP 是中文名时**整个列表被清空**（环境相关，解释"好像失效了"的
间歇感）。顺带修掉 `strncpy(name, ..., 16)` 对 `name[16]` 无终止符隐患
与隐藏网络空 SSID 进列表问题。修复：写指针独立递增压缩 + NUL 安全拷贝。

**教训**：
1. 单色 EPD 上任何非黑样式（灰/淡色 palette）都是隐形炸弹——状态文字
   一律 `lv_color_black()`；新 UI 复查样式颜色与 `convert_lvgl_buf_to_
   epd_bitmap` 阈值的交互；
2. "过滤 + 不压缩"的列表模式遇"空行即停"的渲染约定 = 单条被滤条目截断
   整表——过滤必须伴随压缩（写指针独立于读指针）；
3. v1.0 新增 `[FW] <版本串>` 开机串口打印（factory.ino setup 首行后），
   任何串口抓取都能识别在跑的构建，本次诊断即受益。

---

## 24. WiFi 扫描 -2：自动重连循环与扫描互斥（2026-09-17 定案，v1.3 修复）

**症状**（真机报告 v1.0 起两轮复测）：① "- WIFI Scan" 列表屏停滞无
反应、无数据；② "- WIFI Config" 清空 SSID 框回车**立刻**报
"scan start failed(-2)"。"立刻"是关键线索——不是扫描慢，是启动即被拒。

**根因**（串口抓取定案）：开机自动连 slot 0（factory.ino `WiFi.begin`
+ `setAutoReconnect(true)`），保存的 AP **不在场** → 每 2.4s 一轮
`Reason: 201 - NO_AP_FOUND` 无限重连，**STA 永远处于 connecting 态**。
ESP-IDF 规定 connecting 态下 `esp_wifi_scan_start()` 返回
`ESP_ERR_WIFI_STATE`，Arduino 包装为 `WIFI_SCAN_FAILED(-2)`：
- 4_1 异步路径有打点 → 屏显 "scan start failed(-2)"；
- 4_2 同步路径 `n = WiFi.scanNetworks() = -2`，`for(i<n)` 不执行，
  memset 后空表且**无任何提示** → 表现为"停滞"。

**为什么 §23 的 CJK 修复没治好**：那是渲染层第二道独立地雷，本条是
驱动层状态机拒绝——两道都存在时先撞哪道取决于环境。**为什么"好像
是 v1.0 弄坏的"**：环境触发（保存的 AP 离场才复现；此前 WiFi Test
能显示 LAN IP = AP 在场、已连接，连接态扫描合法）。存量缺陷，非回归。

**修复**（v1.3，`ui_wifi_scan_prepare()/ui_wifi_scan_reconnect()`）：
扫描前若 STA 未连接：`setAutoReconnect(false)`（否则 DISCONNECTED
事件立刻重 begin）+ `disconnect(false,false)`（断重连循环、保 NVS）+
100ms 落定 → idle 态扫描；已连接 STA 原地扫（合法，不折腾）。扫描
**终态成对恢复**（完成/失败/丢弃/中止/启动失败五出口）：读回保存
槽位 `begin` + 重开自动重连。4_2 空列表 + 失败时标题行显示
"Scan failed - retry every 10s"，不再无声。

**教训**：
1. "无反应"型症状先抓串口查驱动层状态机，渲染层修复在驱动拒绝面前
   是空转——两轮修复才碰到根因，代价本可一次付清（4_2 无打点是盲区，
   修复顺带补上失败可见性）；
2. ESP32 WiFi STA 的三种可扫态：**已连接（可扫）/ idle（可扫）/
   connecting（扫描必拒）**——后台无限重连的固件迟早把 UI 扫描锁死；
3. 开机无限重连本身值得收敛（退避/上限，省电+减少与扫描互斥窗口），
   登记为改进项（见 TODO），不在本修内。

**后续（v1.5，同日）**：用户复测指出"未连接状态 wifi app 仍慢"——
无限重连循环的持续信道扫描与 UI 竞争，且 v1.4 扫描结束的 reconnect
还会重启循环。落地**自动连接管理器**：最多 5 次、指数退避 2.5s→40s、
放弃后 STA 空闲直到显式连接/重启；UI 扫描周期挂起管理器（不误计
失败）；手动连接成功后交管理器接管。教训 3 的改进项就此核销。

---

## 25. 4_2 同步扫描阻塞（v1.3 副作用）+ scan-pick 冲掉已存槽（2026-09-17，v1.4 修复）

**症状 ①**（v1.3 复测）：Wifi app 内点 scan/config、scan 列表点
SSID、backspace 退出都明显变慢。**根因**：4_2 列表屏的 10s lv_timer
里是**同步** `WiFi.scanNetworks()`，主循环阻塞 2-3s。v1.3 修复 -2
之前它被驱动秒拒（瞬间返回）——**"流畅"其实是故障的伪装**；修好
-2 后扫描真的执行，潜伏的 UI 阻塞才显形。**修复**：port 层换成
`ui_wifi_scan_async_start()/ui_wifi_scan_collect()` 异步对（同步
getter 删除）；timer 10s→1s 只做非阻塞轮询、每 10s 节奏 kick 新扫
描；退出屏复用 4_1 abort 的 SCAN_DONE 释放协议（提取
`wifi_scan_stop_and_release()` 两屏共享）。

**症状 ②**：从 Scan 点 SSID 跳 Config，永远落 slot 0 并清密码——
已存配置被顶掉；且**不点 Save 也丢**：切槽是 "masked-aware
outgoing save"（切走即把编辑缓冲写回 NVS），pick 清空的密码随下次
切槽写入。**修复（三态）**：pick 的 SSID 已在某槽 → 跳该槽、密码
保留（"Scan pick - existing slot"）；否则 → 第一个空槽重输密码；
全满 → banner "Slots full - clear one first"，不动任何槽。

**教训**：
1. UI 线程（lv_timer/loop 回调）里禁止同步网络扫描/阻塞 IO——
   "现在没出问题"可能只是被上游故障掩盖（-2 秒拒伪装成快）；
2. **修复一个被掩盖的地雷时，把它的掩护范围内行为全部复测一遍**，
   v1.3 验证只测了"能扫出结果"，没测交互响应速度；
3. "选中即覆盖默认目标"类跳转必须找空位或匹配项，占位目标的
   写回语义（outgoing save）会把展示层的临时覆盖固化为持久破坏。

---

## 26. 4_1 双光标 + "not connect" 陈旧状态行（2026-09-17 真机三轮，v1.10–v1.12）

**症状**：scan-pick 跳进 WIFI Config 后 ①SSID/密码两个输入框都有
光标闪烁；②删密码字母时 SSID 同步掉字母；③（v1.9 后）已连接 →
scan → 进 Config 状态行一直 "Not connected"。

**三轮修复史（防复发参考）**：
- v1.10：确认本项目 LVGL 的 textarea **无 DEFOCUSED 光标处理**，
  FOCUSED 一发 blink 即启动永不停——set_field 只发不收，每次切字段
  泄漏一个闪烁。改 style 层（CURSOR part bg_opa 出/入框切换 +
  anim_time=0 静态）+ 空退格跳字段 banner 明示。**真机仍双闪**。
- v1.11：直接驱动 `cursor.show`（`draw_cursor()` 首行的绘制门，
  lv_textarea_t 公开字段）；exit4_2 改无条件恢复重连（原来被
  `inflight` 条件包裹，点 SSID 落在扫描间隔时恢复被跳过）。
  **真机报告"还是老样子"→ 逼出两个关键复核**。
- v1.12（定案）：①读 `ui_scr_mrg.c` 源码证实 push 时旧屏 exit
  **确实执行**、create 每次 push 重跑——v1.11 在路径上，现象必在
  别层；②"not connect"是**显示层陈旧快照**：状态行只在进屏写一次，
  管理器重连成功（GOT_IP）后无人更新屏幕——poll 加链路状态跟踪；
  ③光标**全灭**（两个 ta show=0 + TRANSP + 不再发 FOCUSED），
  焦点只靠 ">" 标签——真机验证通过。

**教训**：
1. "修复没生效"时先**验证修复代码会被执行**（读 scr_mgr 源码而非
   注释推断），再怀疑**现象的观测层**——屏幕可能显示的是陈旧状态；
2. 间接层（事件/style）在真机不表现时，**降级到最底层的确定性门**
   （绘制开关），或干脆移除依赖（无光标设计）；
3. EPD 输入框不必模拟 CRT 光标闪烁——焦点箭头标签是更合适的
   单色慢刷新交互原语。

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

**2026-09-11 机 #2（V1.1 音频版）复测**（同方法；MIC 键 '\f' 专属码绑定
AI Chat 语音前验证）：左Shift(3,4)→(3,5)、右Shift(3,0)→(3,9)、
Sym(3,1)→(3,8)、Alt(2,9)→(2,0)、**Mic(3,3)→(3,6)**、空格(3,2)→(3,7)、
♪=Alt+$（(2,1)→(2,8)，与设计一致：Alt 为临时符号层，'$' 位符号层即
音量 '\v'）、⏎(2,0)→(2,9)='\n'、⌫(1,0)→(1,9)='\b'、q/m 正常。
**结论：两台机键盘矩阵映射完全一致**，无需分板处理。异常记录：按 Q
时 TCA8418 单次报出矩阵外幽灵事件（R9 C6，行号越界 4×10），单发，
观察项（如复现查 §1.5 溢出标志路径）。
