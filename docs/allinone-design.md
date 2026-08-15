# 设计方案：allinone 整合固件（GPS + MP3 + 键盘 + 网络词典 + WiFi 配置 + AI 对话）

> 状态：**评审未通过**（2026-08-09）— 7 项新 finding 待修订（见 [评审结果](allinone-design-review-result.md)）
> 原评审：2026-08-08 通过；2026-08-09 二次评审：5 项通过 / 4 项不通过 / 2 项缺证据；新增 2 High + 5 Medium
> 日期：2026-08-07；**2026-08-16 按 pda2 预研最新结论修订**（键盘修饰键模型、WiFi 配置屏设计、扫描生命周期、WiFi Test、AI 输入体验——预研经 7 轮评审 + 真机验证，记录见 `docs/reviews/`）
> 相关文档：[代码结构与编译方法](build-and-code-structure.md)、[评审申请书](allinone-design-review-request.md)、[评审结果](allinone-design-review-result.md)

## 1. 背景与目标

仓库现有十几个**相互独立**的测试示例（`test_GPS`、`test_pcm5102a`、`test_keypad`、`test_wifi` 等），每个示例都是一个独立 sketch（`setup()`/`loop()`），只能单独烧录。用户希望把其中 6 类功能合并进**一个固件**，烧录一次即可通过菜单切换使用：

| # | 功能 | 来源示例 | 说明 |
|---|---|---|---|
| 1 | GPS 定位 | `test_GPS` | TinyGPSPlus，UART Serial2（rx44/tx43），38400 8N1，u-blox UBX 恢复 |
| 2 | MP3 播放 | `test_pcm5102a` | ESP32-audioI2S，I2S BCLK7/LRC9/DOUT8，PCM5102A DAC |
| 3 | 键盘输入 | `test_keypad` | Adafruit TCA8418，I2C 0x34，4×10 矩阵，轮询 |
| 4 | 网络查询 | `test_wifi` | 在线词典 `https://dictionaryapi.dev/` |
| 5 | WiFi 配置 | `test_wifi` | 运行时 keypad 输入 SSID/密码 → NVS 存储 → 开机自动连接 |
| 6 | AI 文本对话 | 新写 | OpenAI 兼容接口（**OpenRouter** 等），端点/模型/Key 可配置，模型手动输入 |

### 已确认的需求决策

- **MP3 来源**：SD 卡本地文件（非 SPIFFS、非网络流）。
- **网络功能形态**：调用在线词典接口 `api.dictionaryapi.dev/api/v2/entries/en/<word>`。
- **组织方式**：新建独立示例 `examples/allinone`（不动现有 `pda2`/`factory`）。
- **AI 形态**：仅**文本对话**，不做语音（国内大模型聊天接口不收音频，ASR 成本高）。
- **配置方式**：WiFi 与 AI 端点（URL/模型/Key）全部**运行时 NVS 可配置**（`Preferences`），不再依赖编译期 `config_keys.h`。
- **AI 平台**：对接 **OpenRouter**（`https://openrouter.ai/api/v1/chat/completions`，OpenAI 兼容），**模型手动输入**（如 `deepseek/deepseek-chat`、`openai/gpt-4o`），不做厂商预置快捷项；端点保留可改（兼容国内厂商自建端点）。
- **keypad 修饰键（预研结论，2026-08-16 修订）**：HD-V2 硬件实测（串口矩阵解码）——**无 Ctrl 键**；Z 行最左键丝印为 **Alt(2,0)**，底行有两个 **Shift**（(3,5)/(3,9)），Sym(3,8)。语义（产品决策 B，评审采纳）：
  - **Shift（两键，独立状态取逻辑 OR）**：按住出大写层（`keymap_shift`）；
  - **Alt**：按住出**临时符号层**（`keymap_sym`，不锁存，松开复原）；**Alt+Enter 译为 `'\t'`**，作为 WiFi 配置扫描快捷键；
  - **Sym**：符号层**锁定开关**（按一下锁/再按解锁）；Sym 层音量键(2,8)发专用码 `'\v'`（文本输入屏显式忽略）、麦克风键(3,6)出 `'0'`；
  - 交付链：软件字符 FIFO（16 深，`keypad_get_val()` 逐个出队）+ `keypad_clear_chars()`（scr_mgr 切换/push/pop 时清空，防残留按键注入新页面）+ TCA8418 `OVR_FLOW_INT` W1C 清除与修饰键恢复。

## 2. 现有架构调研结论（复用基础）

对 `examples/pda2` / `examples/factory` 及其端口层做了深入调研，结论是 pda2 已经实现了本需求所需的所有基建，可直接复用：

### 2.1 屏幕管理器（scr_mgr）
- `ui_scr_mrg.h` / `ui_scr_mrg.c`：`scr_lifecycle_t {create(parent), entry(), exit(), destroy()}`，`scr_mgr_register/push/pop/switch`。
- 每个 App 一个 `.cpp`，导出全局 `scr_lifecycle_t screen_xxx`，在 `ui_deckpro_entry()`（pda2 `ui_deckpro.cpp` 函数定义）里 `extern scr_lifecycle_t screen_xxx; scr_mgr_register(SCREEN_XXX_ID, &screen_xxx);`（紧随其后 `scr_mgr_register(SCREEN_VOICE_AI_ID, &screen_voice_ai);` 等），最后 `scr_mgr_switch(SCREEN0_ID, false)`。

### 2.2 键盘输入分发
- `peri_keypad.cpp` 在 `loop()` 中被 `keypad_loop()` 轮询：**每轮排空 TCA8418 硬件 FIFO 并逐字符入软件字符 FIFO**（16 深，防积压字符互相覆盖）；修饰键（Alt/双 Shift/Sym）在驱动内独立维护（两个 Shift 取 OR），`OVR_FLOW_INT` 溢出时按 W1C 写回清除并重置全部修饰键（防丢失释放事件卡层）。
- 每个需要键盘的 App 暴露 `void xxx_keyboard_poll(void)`，由 `loop()` 无条件调用，内部用静态 `xxx_kbd_active` 标志门控（`create`/`entry` 置 true，`exit`/`destroy` 置 false）。参考 pda2 `ui_weather.cpp::weather_keyboard_poll`、`ui_gps_enhanced.cpp::gps_keyboard_poll`。
- 读取方式：`keypad_get_val(&c)` 每次出队一个字符；`keypad_set_flag()` 为兼容性空操作。
- **页面边界（预研真实 bug 修复）**：`ui_scr_mrg.c` 在 `scr_mgr_switch/push/pop` 调用 `keypad_clear_chars()`——退出页面时丢弃积压按键，防止连按 Backspace 等残留字符注入新页面（详见 `docs/reviews/` 第三轮 Finding 2.1）。

### 2.3 HTTP / JSON
- `http_utils.h/.cpp`：`http_response_t http_get(url, timeout_ms)`、`http_post(url, body, content_type, auth_header, timeout_ms)`（`WiFiClientSecure` + `HTTPClient`；**预研已改为默认 CA 验证**：内置 ISRG Root X1 / DigiCert Global Root G2 / GlobalSign Root R1，`http_set_tls_mode(HTTP_TLS_INSECURE)` 显式降级）、`http_require_wifi()`（未联网弹提示）。
- JSON 用 cJSON（ESP32 SDK 自带，`#include <cJSON.h>`），仓库无 ArduinoJson。
- **词典接口已现成实现**：`examples/pda2/dict_lookup.cpp::dict_lookup_online(word, result)` —— WiFi 检查 → `snprintf` 拼 URL → `http_get(url, 8000)` → cJSON 解析，UI 为 `ui_dictionary.cpp`。

> ⚠️ **HTTPS 安全注意（评审 #2 修复 + 二次评审 #1.1 修订）**：`http_get`/`http_post` 内部用 `WiFiClientSecure::setInsecure()`，**关闭 TLS 证书校验**，API Key 在 MITM 网络可被窃取（评审 High）。
>
> **allinone 策略**：新增 `http_tls_mode_t { HTTP_TLS_CA_VERIFY, HTTP_TLS_INSECURE }` + `http_set_tls_mode(mode)`。**默认 `HTTP_TLS_CA_VERIFY`**。
>
> **CA bundle 选型（评审 #1.1 修订 + #2.1 二次修订）**：内置 CA 必须能覆盖默认端点的**当前**签发链——gts 链上 certs 由多个 intermediate 颁发，每张 cert 必须能链回 bundle 中的某个 root。
> - 默认端点：`openrouter.ai` 与 `dictionaryapi.dev` 均使用 **GTS (Google Trust Services)** 签发。GTS 有三个 production roots：
>   - **GTS Root R1**（2013，cross-signed by GeoTrust Global CA → 旧设备/旧 OS 兼容需要）
>   - **GTS Root R3**（2016 起 active，签发 `GTS CA 1C3`/`GTS CA 1P5` 等当前 intermediates）
>   - **GTS Root R4**（2023 起 new active root，签发 `GTS CA 1C4` 等新 intermediates；**评审 #2.1 修订：R4 必须包含**，否则 2023 后签发的 cert 无法验证）
> - Let's Encrypt 端点（如自建 reverse proxy、Cloudflare 部分边缘节点）：**ISRG Root X1** + **IdenTrust Commercial Root CA 1**（ISRG X1 的 cross-sign，2024-09 之前是大多数 LE 中间链 root）。
> - GlobalSign：保留 **GlobalSign Root R3**（R1 cross-signed by GTS，R5/R6 较新但签发链短）。实际抓链时若发现 cert 用 GTS 中间签发，验证 GTS Root R1/R3/R4 即可。
>
> 最终 `CA_BUNDLE` 必须包含（顺序无关，mbedtls 会逐一尝试）：
> 1. `GTS Root R1`（pem）
> 2. `GTS Root R3`（pem）
> 3. `GTS Root R4`（pem，**#2.1 修订新增**）
> 4. `ISRG Root X1`（pem）
> 5. `IdenTrust Commercial Root CA 1`（pem，ISRG cross-sign）
> 6. `GlobalSign Root R3`（pem）
>
> **bundle 维护流程**：每次更换默认端点前必须用 `openssl s_client -connect <host>:443 -showcerts </dev/null` 抓取完整链，对照 [ccadb](https://ccadb.my.salesforce.com/) 与 [chrome root store](https://chromium.googlesource.com/chromium/src/+/main/net/data/ssl/chrome_root_store/root_store.md) 确认每个 intermediate 的 root 在 bundle 内；抓取脚本与最后验证日期写入 `examples/pda2/scripts/ca_bundle_check.sh`（**实施时新建**），作为 CI smoke test。**当前抓链结果（评审 #2.1 验收）**：
> - `openssl s_client -connect openrouter.ai:443 -showcerts` 链路：`*.openrouter.ai ← GTS CA 1C3 ← GTS Root R3`（同时也常伴随 `GTS CA 1P5 ← GTS Root R4`），bundle 中 R3 + R4 必须有
> - `openssl s_client -connect dictionaryapi.dev:443 -showcerts` 链路：`*.dictionaryapi.dev ← GTS CA 1C3 ← GTS Root R3`（或 `GTS Root R1` via cross-sign），R1 + R3 必须有
> - 抓链命令与最后验证日期（**实施时填**）：`date -u "+%Y-%m-%d"`，写入 `ca_bundle_check.sh` 注释
>
> **首次握手 smoke test（评审 #1.1 验收 + #2.1 修订）**：`allinone.ino::setup()` 末尾，若 WiFi 已配，串口打印两次握手结果：`[TLS] openrouter.ai → OK / FAILED (reason: <mbedtls err code>)` 与 `[TLS] dictionaryapi.dev → OK / FAILED (reason: ...)`。**两次均必须 `OK`**，否则 `WiFi` 屏右上角持续 `!`（即便 `WiFi.status()==WL_CONNECTED`）；失败原因若为 `-0x2180`（MBEDTLS_ERR_X509_CERT_VERIFY_FAILED）多为 bundle 不全，需要按上面抓链结果补 root。
>
> 用户配置自定义端点时，如果证书不在内置列表，`http_require_wifi` 通过但请求会返回 `Failed to verify`；需在 AI Cfg 屏勾选 **"Trust self-signed"（置 NVS `ai_insecure=1`）** 后该次会话切到 `HTTP_TLS_INSECURE`，**屏幕提示"⚠️ TLS bypass"**。词典请求**强制 CA 验证**（无 API Key，风险面小，沿用安全路径）。

### 2.4 外设 / 启动
- `peri_gps.cpp`：FreeRTOS 任务解析 NMEA + UBX 恢复握手，`gps_get_coord/time/satellites/speed` 等 getter，`gps_task_suspend/resume` 控制任务启停。
- `peri_keypad.cpp`：TCA8418 初始化 + 轮询 + keymap。
- 启动流程（pda2 `factory.ino::setup()`）：GPIO 上电 → SPI CS 拉高 + `shared_spi_bus_init()` → I2C 扫描 → SPIFFS → `SPI.begin` → `peri_init_st[]` 各外设 init → `lvgl_init` + `ui_deckpro_entry` + `disp_full_refr` → WiFi 自动连接。**差异点**：pda2 用编译期 `#if defined(WIFI_SSID)`（`config_keys.h`），allinone 改为 **NVS 运行时配置**（§4 WiFi 屏）。
- 引脚（`utilities.h`）：SDA13/SCL14、SPI SCK36/MOSI33/MISO47、GPS RXD44/TXD43/GPS_EN39、I2S BCLK7/DOUT8/LRC9、SD_CS48、EPD_CS34。
- **SD 与 EPD 共享 SPI 总线**：pda2 用 `shared_spi_lock()/unlock()` + `shared_spi_prepare_device()` 保护显式 SD 操作。

## 3. 总体方案

复制并**裁剪** pda2 得到自包含的 `examples/allinone`：

- **保留**：scr_mgr、peri_keypad、peri_gps、http_utils、dict_lookup、ui_gps_enhanced、ui_dictionary、EPD+LVGL 刷新链。
- **丢弃**：LoRa、BHI260AP IMU、触摸（CST）、BQ25896/BQ27220 电池管理、DRV2605 **电机驱动**（仅丢弃 `drv.begin()`/`selectLibrary` 等 haptic 初始化，pda2 `factory.ino::setup()` 内 DRV2605 初始化段）、A7682E 4G、语音 AI、计算器/天气/日历 App。
- **硬件版本探测必须保留**：pda2 `factory.ino::setup()` 内 I2C 0x5A 探测段，用 I2C 探测 0x5A（DRV2605 存在与否）得到 `isT_Deck_Pro_v1_1`，该标志**直接决定 EPD 显示配置**（`factory.ino` 顶部 `display` 指针选 `display_v1_1`/`display_v1_0`、RST 引脚 `BOARD_EPD_RST`/`BOARD_EPD_RST_UNUSED`；`Version_str1`/`Version_str2` 版本字串；motor pin 电平分支）。若连探测一起删，V1.1 会误用 V1.0 配置导致屏幕初始化/刷新失败。
- **新写**：`ui_mp3.cpp`（SD 文件浏览 + 播放）、`ui_keypad.cpp`（键盘回显）、`ui_wifi_config.cpp`（WiFi 配置）、`ui_ai_chat.cpp` / `ui_ai_cfg.cpp`（AI 文本对话 + AI 配置）、`openai_api.h/.cpp`（OpenAI 兼容客户端，替代 `gemini_api`）。
- **字体**：不复用 `Font_Mono_Bold_*.c`（约 73KB），把 `ui_deckpro.cpp` 里的 `FONT_*` 宏改指内置 `&lv_font_montserrat_14`。
- **图标**：复用 pda2 `src/` 里现成的 4 个 50×50 图标 `img_GPS.c / img_dictionary.c / img_touch.c / img_SD.c`。

## 4. 屏幕设计

共 **9 屏**（菜单 + **8** 个功能屏），全部由 keypad 驱动（触摸已移除 → 无 LVGL pointer indev，按钮点击事件不触发，导航走 `*_keyboard_poll`）：

> **评审 #1.5 修订**：之前版本写"8 屏（菜单 + 7 功能屏）"与菜单列出的 8 项入口（GPS/Music/Dict/Keys/WiFi/**WiFi Cfg**/AI/**AI Cfg**）自相矛盾——把 WiFi 与 WiFiCfg 算作 1 个、AI 与 AICfg 算作 1 个才会得到 7 功能屏，但菜单需要 8 个跳转目标。**修正**：WiFi 配置作为独立屏（与 WiFi 扫描/状态分开），AI 配置作为独立屏（与 AI 对话分开），所以是菜单 1 + 8 功能屏 = **9 个 `SCREEN_XXX_ID`**。评审 #1 修复（菜单扩到 8 项）保持不变。

**按键层说明（预研结论修订，HD-V2 物理布局实测解码，无 Ctrl 键）**：

```
Q   W   E   R   T   Y   U   I   O   P
A   S   D   F   G   H   J   K   L   ⌫
Alt Z   X   C   V   B   N   M   ♪   ⏎
⇧   Mic Space Sym ⇧
```

keypad 三层——普通层小写 a-z + 空格 + `\n`/`\b`（`peri_keypad.cpp::keymap`）；**Shift 层大写 A-Z**（按住任一 **Shift(3,5)/(3,9)** 临时切换，`keymap_shift`）；**符号/数字层 `keymap_sym`**（**Sym(3,8) 锁定**，或按住 **Alt(2,0)** 临时进入）。**Alt+Enter → `'\t'`**（扫描快捷键）；Sym 层音量键 → `'\v'`（保留码，输入屏忽略）、麦克风键 → `'0'`。本文中所有"数字键 `1`-`8`"、"`+`/`-`"均指 **Sym 层对应键**（先按 Sym 锁层，或按住 Alt 临时出符号）。

| SCREEN ID | 内容 | 来源 |
|---|---|---|
| `SCREEN0_ID` | **菜单：8 个按钮**（"1 GPS / 2 Music / 3 Dict / 4 Keys / 5 WiFi / 6 WiFi Cfg / 7 AI / 8 AI Cfg"），单页 9 格布局（评审 #1 修复：原 6 项缺 WiFi Cfg 和 AI Cfg 入口，首次使用无法设 Key）。`menu_btn_list` 9×8 网格，"GPS"右上角显示 `!` 提示未配 WiFi，AI Cfg 同理；Sym 层 `1`-`8`（先按 Sym）进入对应屏 | 裁剪 pda2 菜单 |
| `SCREEN_GPS_ID` | GPS 状态（坐标/速度/卫星/UTC 时间），沿用 3s 定时刷新；已有 `gps_keyboard_poll`（`\b` 返回） | 复制 `ui_gps_enhanced.cpp` |
| `SCREEN_MP3_ID` | SD `.mp3` 文件浏览器（分页）+ 播放控制 | 新写 `ui_mp3.cpp` |
| `SCREEN_DICT_ID` | keypad 输入英文单词 → 在线查询 → 显示音标 + 释义（前 3 条） | 复制 `ui_dictionary.cpp` + `dict_lookup.cpp` |
| `SCREEN_KEYPAD_ID` | 键盘回显测试：按键字符 + 0xHEX + 累计次数 | 新写 `ui_keypad.cpp` |
| `SCREEN_WIFI_ID` | WiFi 状态/扫描：显示当前 SSID/IP/RSSI，扫描周围 AP 列表；提供 **WiFi Test** 入口（ifconfig.me 公网 IP 测试 → 信息层展示 + 关闭；pda2 为列表项触摸入口，allinone 无触摸需键盘等价键，如 Sym 层数字或 `t`） | 新写 `ui_wifi_status.cpp` |
| `SCREEN_WIFI_CFG_ID` | WiFi 配置：SSID 编辑/扫描选择双模式 + 异步扫描覆盖层 + **连接成功才存 NVS** + Connect/Clear 按钮（交互细则见下） | 新写 `ui_wifi_config.cpp`（按 pda2 预研实现移植） |
| `SCREEN_AI_ID` | AI 文本对话：输入问题 → 调 OpenAI 兼容接口 → 显示回答 | 新写 `ui_ai_chat.cpp` |
| `SCREEN_AI_CFG_ID` | AI 配置：端点 URL（预填 OpenRouter）/ 模型（手动输入）/ API Key → 存 NVS | 新写 `ui_ai_cfg.cpp` |

### MP3 屏交互
- 浏览态：Sym 层 `1`-`6` 选文件（先按 Sym）；`\n` 下一页（回卷）；`\b` 上一页，首页再按 `\b` 退出回菜单（`audio.stopSong()` + `scr_mgr_pop`）。
- 播放态：`' '` 暂停/继续；`n`/`\n` 下一首；`p` 上一首；Sym 层 `+`/`-` 音量（0-21）；`\b` 回浏览态。
- 扫描 `/music`（不存在则回退根目录），过滤 `.mp3`（上限 ~40 条）；`audio.connecttoFS(SD, path)`。
- 强定义弱回调 `void audio_eof_mp3(const char *info)`：自动切下一首（回卷）。**评审 #1.7 修订**：统一为带参形式，与 `lib/ESP32-audioI2S/src/Audio.h` 一致；无参形式绑定不到符号、自动切歌不触发。

### 词典屏
- 完全复用 pda2 实现。可选增强：`create` 时若 `audio.isRunning()` 则 `audio.stopSong()`，避免 8s HTTP 阻塞期间音乐卡顿。

### WiFi 配置屏交互（预研结论修订——pda2 已实现并经 7 轮评审迭代，allinone 照此移植）

- **SSID 为可编辑 `lv_textarea`**（原 lv_dropdown 方案已废弃：不可手动输入、触摸展开后 Enter 会误选占位文字）。显式**双模式**状态机 `wifi_cfg_scan_mode`：

| 按键 | 手动编辑模式（默认） | 扫描选择模式（扫描有结果后自动进入） |
|---|---|---|
| 可见字符（含 `+ -`） | 追加到文本框 | 退出扫描模式回编辑并追加该字符 |
| Alt+Enter（`'\t'`）/ 空框 Enter | 开始**异步扫描** | 重新扫描 |
| Enter（框非空） | 提交 → 跳到密码框 | 选中当前候选 → 提交 → 跳到密码框 |
| `+` / `-`（Sym/Alt 层） | 追加为字符 | 循环切换扫描候选 |
| `\b` | 有字删字；为空退出本屏 | 取消选择，恢复扫描前内容回编辑模式 |

- **异步扫描**：`WiFi.scanNetworks(true)` + 每 loop 轮询 `WiFi.scanComplete()`（主循环持续排空键盘 FIFO，无阻塞丢事件）；扫描期间**置顶覆盖层**显示 "Scanning... Ns" 倒计时（10s）并**屏蔽全部输入**（键盘守卫 + 全屏可点击层吞触摸），完成/失败即隐藏，倒计时耗尽则中止卡住的扫描；结果用**横幅**明示（"Scan: N found" / "Scan: none found" / "Scan failed"，3s 自动消失，不阻塞输入）；错误码区分启动失败/运行失败/零结果三态。
- **扫描代次**：离开 SSID 字段、退出屏幕或重扫时 `wifi_scan_gen++` 失效在途结果——迟到的扫描结果**不得覆盖用户草稿**；退出屏幕时 `esp_wifi_scan_stop()` + 等待框架处理完 SCAN_DONE 后 `scanDelete()`（防与 `_scanDone()` 竞态）。
- **草稿保留**：`wifi_cfg_refresh_labels()`（仅标签/状态）与 `wifi_cfg_sync_draft()`（切换字段前把离场框内容同步进缓存）分离；字段切换**不重写**另一输入框；仅退出页面或显式取消丢弃草稿。
- **密码独立输入框**：字符键输入（Shift 大写 / Sym 数字符号）、`\n` = **连接（成功才保存）**、`\b` 退格（为空时返回 SSID 字段）。
- **Connect 语义（预研结论）**：`WiFi.begin` → 15s 内连接**成功才** `Preferences`（namespace `wifi`）写入 `ssid`/`pass`；失败**不保存**（NVS 旧配置不变），横幅/状态栏显示失败原因（No SSID found / Connect failed / Timeout）；连接期间积压按键在返回前丢弃。
- **触摸按钮（pda2 保留触摸）**：**Connect**（= 密码框 Enter）与 **Clear**（清空两框 + 缓存 + NVS 记录，下次开机不自动连旧凭据）。**allinone 无触摸 → 键盘路径必须完备**：Connect = 密码框 Enter；**Clear 需键盘等价键**（建议 Alt+Backspace 译 `'\x7f'`，实施时定夺并在屏底提示注明）。
- **焦点同步（pda2 有触摸）**：textarea `LV_EVENT_FOCUSED` 回调同步 `wifi_cfg_field`，触摸点框与键盘编辑目标一致；allinone 无触摸则无此需求。
- 开机：读 NVS，有凭据则自动 `WiFi.begin` + `setAutoReconnect(true)`；无凭据则菜单 WiFi 按钮旁显示 `!`，词典/AI 请求时 `http_require_wifi` 失败提示"先配置 WiFi"。

### AI 对话屏交互
- 输入态：字符键输入问题（数字/符号需先按 Sym）、`\b` 删除、`\n` 发送。**不用 `c` 作快捷键**（英文提问里 'c' 极常见，会误触配置），AI 配置从**菜单 "8 AI Cfg" 按钮**进入（评审 #1 修复：6 项菜单没有 AI Cfg 入口）。
- 发送：`openai_chat(prompt, cfg)`（`Authorization: Bearer <key>`），等待期间显示 `…`，返回后分页显示回答；`\b` 返回菜单。
- 无 WiFi / 未配置 Key：提示 `Set WiFi / Set AI Key`，引导到对应配置屏。

### AI 配置屏交互
- 三字段：端点 URL / 模型 / API Key，`\n` 确认当前字段并跳下一字段（末字段 `\n` 存 NVS 并退出回 AI 对话屏）；`\b` 退格（字段为空时返回上一字段）。
- 端点**默认预填 OpenRouter** `https://openrouter.ai/api/v1/chat/completions`（可改）；**模型手动输入**（如 `deepseek/deepseek-chat`、`qwen/qwen-2.5-72b-instruct`、`openai/gpt-4o`）；Key 标签掩码显示（`sk-or-v1-***`），编辑时文本框显示明文。
- 字段均为小写 + `0-9 . : / - _`（keypad 符号层），OpenRouter Key `sk-or-v1-...` 可直接输入。

## 5. 文件清单

### 5.1 原样复制（来自 `examples/pda2/`）
```
utilities.h                  # 引脚定义
ui_scr_mrg.h / ui_scr_mrg.c  # 屏幕管理器（已含页面切换 keypad_clear_chars()）
peri_keypad.cpp              # TCA8418 键盘（软件字符 FIFO + 修饰键状态机 + 溢出恢复，见 §2.2）
http_utils.h / http_utils.cpp# http_get / http_require_wifi
dict_lookup.h / dict_lookup.cpp  # dict_lookup_online（dictionaryapi.dev）
ui_dictionary.cpp            # 词典屏
src/img_GPS.c  src/img_dictionary.c  src/img_touch.c  src/img_SD.c   # 4 个菜单图标
```

### 5.2 复制后裁剪
| 文件 | 裁剪要点 |
|---|---|
| `factory.ino` → **`allinone.ino`** | setup()：去掉 BQ/DRV2605 驱动（**保留 I2C 0x5A 版本探测** → `isT_Deck_Pro_v1_1`，见 §3）/BHI260AP/A7682E，并删除全部触摸初始化/注册/轮询代码（见下方"触摸相关代码清理清单"）；**保留 `configTzTime("CST-8", "pool.ntp.org", "time.nist.gov", "cn.pool.ntp.org")`**（评审 #1.2 修订：ESP32 冷启动系统时间为 0 或编译时间，证书链 `notBefore`/`notAfter` 校验直接 fail → 所有 HTTPS 不可用，NTP 同步是 TLS 校验前提）；保留 GPIO 上电、SPI CS、`shared_spi_bus_init`、I2C 扫描、SPIFFS、`SPI.begin`；peri 初始化精简为 ink_screen / keypad / sd_care_init / gps_init / pcm5102a_init（无条件）；启动时从 NVS 读 WiFi 凭据（有则 `WiFi.begin`）。loop()：`lv_task_handler` + `keypad_loop` + **9** 个 `*_keyboard_poll`（menu/gps/mp3/dict/keypadtest/**wifi_status**/wifi_cfg/ai_chat/ai_cfg）（评审 #2.2 修订：`SCREEN_WIFI_ID` 是新增的 WiFi 状态/扫描屏，需要 `wifi_status_keyboard_poll` 驱动——否则进入该屏后无输入路径，`\b` 都不能触发，`scr_mgr_pop` 不执行）+ `audio.loop` |
| `factory.h` | 去掉 TinyGSM/BQ/DRV/TouchDrv；保留 `Audio audio`、`peri_init_st[]`、`shared_spi_*`、`disp_full_refr` |
| `peripheral.h` | `E_PERI_*` 裁为 `{KYEPAD, SD, GPS, PCM5102A, INK_SCREEN, NUM_MAX}`；保留 keypad/gps 原型 |
| `ui_deckpro.h` | `SCREEN_XXX_ID` 裁为 **9 个**（评审 #1.5 修订：`SCREEN0_ID` + `SCREEN_GPS_ID` + `SCREEN_MP3_ID` + `SCREEN_DICT_ID` + `SCREEN_KEYPAD_ID` + `SCREEN_WIFI_ID` + `SCREEN_WIFI_CFG_ID` + `SCREEN_AI_ID` + `SCREEN_AI_CFG_ID`）；保留 `struct menu_btn`、`scr_back_btn_create`、`ui_deckpro_entry` |
| `ui_deckpro.cpp` | 删除 low-voltage 块、screen1-12、taskbar/gesture/touch 定时器；保留 `scr_back_btn_create` + 菜单（`menu_btn_list` 裁为 8 项，`menu_btn_event_cb` 已存在）；`FONT_*` 宏改指 `&lv_font_montserrat_14`；`ui_deckpro_entry()` 只注册 **9 屏**（评审 #1.5 修订）；新增 `menu_keyboard_poll()`（`'1'`-`'8'` → `scr_mgr_push`，8 个目标对应 8 个非菜单 `SCREEN_XXX_ID`） |
| `ui_deckpro_port.h / .cpp` | 保留 `ui_disp_full_refr`、`ui_gps_task_suspend/resume`、`ui_gps_get_coord/data/time/satellites/speed`、`ui_input_get_keypay_val/set_flag`、**`ui_gps_get_snapshot(ui_gps_snapshot_t*)` + `typedef gps_snapshot_t ui_gps_snapshot_t;`**（评审 #2.3 修订：§5.2 `peri_gps.cpp` 行 + `ui_gps_enhanced.cpp` 行已强制使用 `ui_gps_get_snapshot()` 单一接口，若 port 层裁剪列表不含此函数与类型，链接阶段会报 `undefined reference to ui_gps_get_snapshot` / `ui_gps_snapshot_t has no member named ...`；旧 5 个 getter 可保留兼容其它屏，但 GPS 屏不调用）。`ui_other_get_gyro` 等与 allinone 无关的导出全部删除。 |
| `peri_gps.cpp` | `gps_task_suspend/resume`（`gps_task_suspend`/`gps_task_resume` 函数定义）加 **NULL 守卫**；`gps_init` 握手失败时不创建任务（`gps_init` 函数体），`gps_handle` 保持 NULL；**评审 #4 修复 + #1.3 修订**：12 个快照字段（`gps_lat/lng/alt/speed/year/month/day/hour/minute/second/vsat`）读写两端必须都受临界段保护。修复：<br>① 定义 `gps_snapshot_t { lat, lng, altitude, speed, year, month, day, hour, minute, second, vsat }`（**共享头**：`peripheral.h`，让 `ui_deckpro_port.cpp` 也能 `typedef gps_snapshot_t ui_gps_snapshot_t`，避免跨 TU 不可见）<br>② **写端**：`displayInfo()` 在 GPS 任务中先填一个**局部 `gps_snapshot_t snap`**，**整结构赋值完毕后** `taskENTER_CRITICAL(&mux)` 一次性 `memcpy(g_shared, &snap, sizeof(snap))`，`taskEXIT_CRITICAL`；**不在临界段内调用 `gps.encode()` 或任何可能阻塞/让出的操作**。<br>③ **读端**：`gps_get_snapshot(out)` 同样 `taskENTER_CRITICAL` 后整结构 `memcpy(out, g_shared, sizeof(*out))`，退出。<br>④ UI 侧：`ui_gps_enhanced.cpp::gps_keyboard_poll` 与 3s 定时器**统一**改为 `ui_gps_get_snapshot(&s)` 一次读取，**禁止**再调 5 个旧 getter（`ui_gps_get_coord/time/satellites/speed`）。<br>⑤ `peri_gps.cpp::gps_get_coord/data/time/satellites/speed` 旧 getter 标记 `__attribute__((deprecated))`（保留兼容 pda2 其它屏，但 GPS 屏不用）。 |
| `ui_gps_enhanced.cpp` | GPS 屏 `entry()` 先查 GPS 可用性（`gps_handle != NULL`），失败显示 "No GPS" 且不 `ui_gps_task_resume()`；3s 定时刷新直接 `ui_gps_get_snapshot()` |

**GPS 空句柄防护**（评审 High）：`gps_init()` 握手失败（`peri_gps.cpp::gps_init` 内 UBX 恢复分支）时不创建任务，`gps_handle=NULL`；进 GPS 屏仍 `ui_gps_task_resume()` → `vTaskResume(NULL)` 崩溃（`peri_gps.cpp::gps_task_resume` → `ui_deckpro_port.cpp::ui_gps_task_resume` → `ui_gps_enhanced.cpp::gps_screen_entry`）。复制时按上表两行修复。

#### 触摸相关代码清理清单（防未定义引用 / 残留无效输入设备）

触摸初始化、注册与轮询代码分散在 4 处，按下列位置**全部删除**：

| 位置 | 需删除内容 |
|---|---|
| `factory.ino::touchpad_read` | 静态函数（LVGL pointer 读回调，内部调 `hyn_touch_get_point`） |
| `factory.ino::lvgl_init` 内 pointer 块 | LVGL pointer 输入设备注册块（`lv_indev_drv_init` + `LV_INDEV_TYPE_POINTER` + `read_cb = touchpad_read` + `lv_indev_drv_register`） |
| `factory.ino::setup()` 触摸初始化 | `peri_init_st[E_PERI_TOUCH] = hyn_touch_init();`（触摸不进 peri 初始化表） |
| `factory.h::hyn_touch_init/hyn_touch_get_point` | 原型声明及触摸头文件 include |
| `ui_deckpro_port.cpp::ui_input_get_touch_coord` | 内部调 `hyn_touch_get_point`，以及手势/触摸相关端口函数 |
| `ui_deckpro.cpp` | taskbar/gesture/touch 定时器与触摸回调（见 §5.2 主表 `ui_deckpro.cpp` 行） |
| 文件 | `hyn_*`（`hyn_touch.cpp`、`hyn_cst66xx.c`、`hyn_core.*` 等）一律不复制 |

`peripheral.h` 的 `E_PERI_*` 已不含 `E_PERI_TOUCH`（见 §5.2 主表）；删除后 `loop()` 不再调用任何触摸轮询，LVGL 无 pointer indev，按钮点击事件不触发，导航全靠 keypad poll（见 §9 风险 4）。

### 5.3 新写
| 文件 | 内容 |
|---|---|
| `ui_mp3.cpp` | `ensure_sd()`（`shared_spi_lock` + `SD.begin(48)`）、`scan_files()`、分页列表渲染、播放/暂停/切歌/音量、`void audio_eof_mp3(const char *info)` 强定义（评审 #7 修复：ESP32-audioI2S 的 `audio_eof_mp3` 弱回调实际签名带 `const char *info` 参数，写成无参绑定不到符号）、`mp3_keyboard_poll`、`scr_lifecycle_t screen_mp3` |
| `ui_keypad.cpp` | 返回按钮 + 提示 + 回显标签、`keypadtest_keyboard_poll`（`\b` 返回菜单）、`scr_lifecycle_t screen_keypad` |
| `ui_wifi_config.cpp` | **SSID `lv_textarea` + 编辑/扫描选择双模式**（预研结论，交互细则见 §4 WiFi 配置屏）：Alt+Enter/空框 Enter 异步扫描（`scanNetworks(true)` + `scanComplete()` 轮询 + 扫描代次失效 + 置顶倒计时覆盖层屏蔽输入 + 结果横幅）、`+`/`-` 循环候选、`\n` 提交、`\b` 删字/取消/返回；**密码 `lv_textarea`**：`\n` = **连接成功才存 NVS**（`Preferences` namespace `wifi`）、`\b` 退格/回 SSID；草稿保留（`refresh_labels`/`sync_draft` 分离）；触摸按钮 Connect/Clear（allinone 无触摸需键盘等价：Connect=密码框 Enter、Clear=Alt+Backspace 待定）；`wifi_cfg_keyboard_poll`、`scr_lifecycle_t screen_wifi` |
| `ui_ai_chat.cpp` | 问题输入 + `\n` 发送 → `openai_chat()` → 分页显示回答、`ai_chat_keyboard_poll`（浏览回答时 `\n` 下一页/`\b` 回上一页或退出；**无 `c` 快捷键**，AI 配置走菜单）、`scr_lifecycle_t screen_ai_chat`。**预研补充（pda2 已实现）**：发送成功后清空输入框（浏览态 `\b` 语义无歧义）；浏览回答时按任意可见字符自动回输入模式并追加该字符（不静默丢弃）；`\t`（Alt+Enter）/`\v`（音量码）控制码显式忽略。**评审 #1.6 修订 + #2.4 二次修订 — UTF-8 安全分页**：必须按 **UTF-8 码点边界**断行，禁止 `strlen()/memcpy()` 字节切。<br>① 输入：`openai_chat()` 返回的字节流按 UTF-8 解码为 codepoint 序列；连续 ASCII 视为单列，连续 CJK/全角视为双列，emoji 按 `wcwidth()` 视为 2 列。<br>② 断行：从行首累计显示列数；下一个 codepoint 会让累计列数 **> 30 列**时，在该 codepoint 之前断行（即只切到 codepoint 边界，绝不在 continuation byte `0x80-0xBF` 中间断开）。<br>③ **LVGL 8.3.11 API 选型（评审 #2.4 修订）**：评审 #1.6 提到的 `lv_txt_get_next_line()` 在 LVGL 8.3.11 中**不存在**（实际是 private `_lv_txt_get_next_line()`，需 `#include "../src/misc/lv_txt.h"` 才能用）；`LV_LABEL_LONG_BREAK` 也是错记——**正确宏名是 `LV_LABEL_LONG_WRAP`**。<br>　　**推荐实现**：直接用 LVGL 公开 API：<br>　　```cpp<br>　　lv_obj_t *lbl = lv_label_create(parent);<br>　　lv_obj_set_width(lbl, LV_PCT(100));                    // 让 label 自适应父容器宽度<br>　　lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);        // 内部按字符宽度自动换行（含 CJK）<br>　　lv_label_set_text(lbl, full_answer);                    // 直接喂全文，LVGL 处理 wrap + UTF-8 码点边界<br>　　lv_label_set_recolor(lbl, false);                       // 关闭 recolor 避免 '$' 字符冲突<br>　　```<br>　　`LV_LABEL_LONG_WRAP` 在 LVGL 8.3.11 是稳定的 public API（声明于 `lvgl/src/widgets/lv_label.h`），不依赖任何私有符号；换行基于实际渲染字体像素宽度，中文/英文混合自动正确断行。<br>④ 显示宽度基准：EPD 240px ÷ 14pt 字体 ≈ 30 列（ASCII）/ 15 列（CJK），由 LVGL 根据 `lv_obj_set_width()` 自动决定每行字符数。<br>⑤ `(truncated)` 标记仍按字节截断位置放在末尾（4096B 缓冲满时）：检测 `body.size() >= CHAT_ANSWER_MAX` 时 `chat_truncated=true`，`chat_render` 末尾追加 `\n(truncated)`。 |
| `ui_ai_cfg.cpp` | 端点 URL（预填 OpenRouter）/ 模型（手动输入）/ API Key 三字段，`\n` 确认当前字段并跳下一字段（末字段 `\n` 存 NVS namespace `ai` + 退出）、`\b` 退格/回上一字段；Key 标签掩码显示（`sk-or-v1-***`）、`ai_cfg_keyboard_poll`、`scr_lifecycle_t screen_ai_cfg` |
| `openai_api.h/.cpp` | **新写 OpenAI 兼容客户端** `openai_chat(prompt, base_url, model, api_key)`：POST `{"model":..., "messages":[{"role":"user","content":...}]}`，**直接复用 `http_post`**（`http_utils.h::http_post` 声明已支持 `auth_header` → `Authorization: Bearer <key>`，`content_type=application/json`），解析 `choices[0].message.content`；默认端点 OpenRouter；TLS 策略见 §2.3 注 |
| `src/assets.h` | 仅 `LV_IMG_DECLARE` 4 个图标（`extern "C"` 包裹） |
| ~~`config_keys.h`~~ | **不再需要**：WiFi/AI 全部运行时 NVS 配置，`allinone.ino` 删除对它的 include |

**不复制**：`ui_sd_test.cpp`、`ui_weather.cpp`、`ui_calculator.cpp`、`ui_calendar.cpp`、`ui_voice_ai.cpp`、`gemini_api.*`、`pdm_recorder.*`（AI 改用新写 `openai_api`，不做语音录制）、`lunar_calendar.*`、`peri_lora.cpp`、`peri_gyroscope.cpp`、`hyn_*`（触摸）、全部 `Font_Mono_Bold_*.c` 及用不到的 `img_*.c`。

## 6. 构建配置

`platformio.ini` 追加（镜像 `[env:pda2]`，这是唯一改动示例目录之外的文件）：

```ini
[env:allinone]
build_flags =
    ${env.build_flags}
    -DARDUINO_T_DECK_PRO
    -include config/lv_conf.h
```

- `script/set_srcdir.py` **无需改动**：env 名 `allinone` 默认映射 `examples/allinone`。
- `-include config/lv_conf.h` 必须保留：`LV_COLOR_DEPTH=1` 是墨水屏单色渲染的前提。
- 全部第三方库 vendored 在 `lib/`，`lib_deps` 只需内置 SPI/Wire/FS/SPIFFS/EEPROM，无需新增依赖。

## 7. 实现步骤

0. **预研（先于本设计实现）**：在 `pda2` 上先实现并真机验证 WiFi 配置屏 + AI 对话/配置屏（OpenRouter），成熟后再移植到 allinone（详见 `TODO.md` 阶段 0）。
1. 建目录 `examples/allinone/` + `src/`（WiFi/AI 用 NVS 运行时配置，**不需要** `config_keys.h`）。
2. 复制 §5.1 清单与 4 个图标。
3. 写 `src/assets.h`。
4. 裁剪 `peripheral.h`、`factory.h`、`ui_deckpro.h`、`ui_deckpro_port.h/.cpp`（触摸原型/端口函数删除按 §5.2"触摸相关代码清理清单"；GPS 空句柄守卫按 §5.2）。
5. 裁剪 `ui_deckpro.cpp`（大文件，按 `#if 1 … #endif` 区块整块删除 screen1-12 与 low-voltage；改写 `ui_deckpro_entry`；新增 `menu_keyboard_poll`）。
6. `factory.ino` → `allinone.ino`，裁剪 setup/loop。
7. 新写 `ui_mp3.cpp`、`ui_keypad.cpp`、`ui_wifi_config.cpp`、`ui_ai_chat.cpp`、`ui_ai_cfg.cpp`、`openai_api.h/.cpp`。
8. `platformio.ini` 追加 `[env:allinone]`。
9. 编译并修复链接错误（见 §8）。

## 8. 验证

```bash
pio run -e allinone --jobs 8
```

- **构建**（评审 #9 修复：`boards/T-Deck-Pro.json` 的 `maximum_size:16777216` 是 16MB 芯片大小，不是程序分区；实测 ESP32-S3 `default_16MB.csv` 工厂 app 约 6.5MB，pda2 实测 `Flash: [=== ]  29.7% (used 1945329 bytes from 6553600 bytes)`）：`.pio/build/allinone/firmware.bin` 生成；无未定义引用（若出现 `bq27220`/`drv`/`modem`/`hyn_touch_*`/`lora_*` 说明有漏剪引用）。**容量基准按 6.5MB 计算余量**（pda2 1.9MB → allinone 估算 2.2-2.5MB，余量 50%+）。
- **烧录（有硬件时）**：`pio run -e allinone -t upload -t monitor` → 串口见 I2C 扫描到 `KEYBOARD 0x34`、SD 挂载、GPS UBX 握手成功；EPD 渲染菜单；`1`-`8` 进各屏、`\b` 返回；在 **WiFi 屏**配好 SSID/密码后词典/AI 可用；SD `/music` 的 MP3 可播放/暂停/切歌；AI Cfg 配 OpenRouter Key 后 AI 屏可发问。

## 9. 风险与注意事项

1. **SD 与 EPD 共享 SPI**：所有显式 SD 操作套 `shared_spi_lock()/unlock()`；`audio.loop()` 流式读不加锁；EPD 只在按键/进屏/GPS 3s 定时器到期时刷新，不做逐帧刷新，缩小冲突窗口。
2. **`audio.loop()` 必须每轮调用**：不能阻塞。词典 8s `http_get` 为同步阻塞（pda2 原样行为），词典屏与音乐屏互斥，可接受；v1 不做 weather 式后台任务。
3. **墨水屏刷新术语分层（评审 #5 修复 + #1.4 修订）**：
   - **LVGL 整屏提交**（`disp_drv.full_refresh=1`，`factory.ino::lvgl_init`）：LVGL 整屏 area 一次性 flush → EPD flush 走 `setFullWindow()`（GxEPD2 全屏波形）或 `setPartialWindow()`（EPD 局部波形，由 `disp_refr_mode` 决定）。
   - **EPD 局部波形**（`setPartialWindow` + `display->nextPage()` 默认）：GDEQ031T10 局刷 ≈ 250ms，适用于"按键触发的小改动"。
   - **EPD 全屏波形**（`setFullWindow` + `fillScreen(WHITE)`）：GDEQ031T10 全刷 ≈ 1-2s，对应**深色残留累积 → 必须周期性全刷才能将"鬼影"清除**。
   - **现状**：pda2 仅 `ui_disp_full_refr()` 触发 setFullWindow + fillScreen 一次；其他时间走 `setPartialWindow`。**长时间使用后局部刷新会留残影**——必须设计**周期性 EPD 全刷**才能清除。
   - **allinone 方案（评审 #1.4 修订）**：
     - **计数器位置必须选真实 flush 路径**。pda2 当前 `factory.ino:318` 把 `disp_drv.render_start_cb = dips_render_start_cb`（注：原代码有拼写 `dips` 应为 `disp`）**整行注释**，导致依赖此回调的 `flush_timer_cb` 永远不会被 LVGL 调用——**之前 commit `27ad8d5` 加的 `FACTORY_EPD_FULL_REFRESH_INTERVAL=60` 计数器因此永远不增长，周期全刷永远不会执行**。
     - **allinone 修复（任选其一）**：
       1. **方案 A（推荐）**：把计数器挪进 `disp_drv.flush_cb`（LVGL 真正调用的 EPD 输出回调），每次 flush 完成 `++part_count`，`part_count >= 60` 时下一次 flush 强制 `setFullWindow() + fillScreen(WHITE)` 并 `part_count=0`。此方案不依赖 render_start_cb 注册状态，最稳。
       2. **方案 B**：保留 `flush_timer_cb` 设计，但 allinone 必须在 `lvgl_init` 里**显式重新注册** `disp_drv.render_start_cb = disp_render_start_cb`（修拼写），且 `flush_timer_cb` 作为此回调内部逻辑。该方案依赖 LVGL 8.3.11 `render_start_cb` 实际触发频率（实测在 partial 模式下每帧调用）。
     - 同时 `ui_disp_full_refr()` 进屏时仍强制一次全刷。GPS 屏 3s 定时只写局部。
   - **实施时**：在 `allinone.ino::lvgl_init` 末尾加 `Serial.printf("[EPD] refresh strategy: %s\\n", strategy)` 打印所选方案，便于现场确认；pda2 现有 `factory.ino:318` 注释状态作为参考，不在 allinone 中保留。
4. **触摸移除**（删除清单见 §5.2）→ 无 pointer indev，按钮 `LV_EVENT_CLICKED` 不触发，所有导航必须靠 keypad poll。**预研补充**：pda2 的 WiFi 配置屏依赖触摸的交互（Connect/Clear 按钮、textarea 焦点同步、扫描覆盖层吞触摸）在 allinone 需键盘等价路径——Connect=密码框 Enter、Clear 键待定（§4）；覆盖层触摸屏蔽自然降级为仅键盘守卫。
5. **dictionaryapi.dev 仅英文**：UI 文案用英文（与 pda2 一致）；词不在库返回 404 → 显示 "Word not found"。
6. **SD 未插卡**：`sd_care_init` false → MP3 屏显示 "No SD"，词典本地扫描快速失败，不崩溃。
7. **GPS UBX 握手启动时阻塞 ~2.4s**（LVGL 初始化之前），仅延迟首屏。
8. **`.ino` 发现规则**：`allinone/` 顶层只能有一个 `allinone.ino`；已改用 NVS 运行时配置，`config_keys.h` 不再必需。
9. **keypad 输入限制**：普通层小写 a-z + 空格 + `\n`/`\b`；**Shift 层大写 A-Z**（按住任一 Shift(3,5)/(3,9)，`peri_keypad.cpp::keymap_shift`）；**数字与 `+ - . : / _ @` 等符号在 Sym 层**（按 Sym(3,8) 锁定，或按住 Alt(2,0) 临时进入，`peri_keypad.cpp::keymap_sym`），文档中数字/符号按键均需先按 Sym 或按住 Alt（见 §4 键层说明）；Alt+Enter → `'\t'` 扫描快捷键；Sym 层音量键 → `'\v'`（保留码）、麦克风键 → `'0'`。**完整修饰键模型已在 pda2 预研实现并经 7 轮评审 + 真机验证**（§1 决策）：大小写/符号 SSID 均可输入；OpenRouter Key `sk-or-v1-...`、模型 ID 多为小写，无需 Shift。
10. **WiFi/AI 配置屏**：所有联网功能依赖已配好的 WiFi（未配 → 提示先配 WiFi）；AI 请求与词典一样**同步阻塞**（`http_post` 默认 15s 超时，OpenRouter 模型响应可能更慢，可放宽到 30s），AI 屏与音乐屏互斥；Key/端点存 NVS 明文（`Preferences`，namespace `wifi`/`ai`），不打印到日志；OpenRouter 为 OpenAI 兼容接口，个别字段差异以实际响应调通。
11. **TLS 证书校验依赖系统时间（评审 #1.2 修订）**：ESP32 冷启动系统时间默认为 epoch（1970-01-01）或编译时间，远早于现代证书 `notBefore`（2024+），mbedtls 直接判 `certificate not yet valid` 并 reject。`allinone.ino::setup()` 必须先 `configTzTime()`，然后 `setup()` 末尾轮询 `time(nullptr) > 1700000000`（即 2023-11-14 之后），最多等 30s；超时仍未同步则在 `WiFi` 屏与 `AI Cfg` 屏均显示 `! time not synced`，**首次 HTTPS 请求返回 `Time not synced, retry after NTP sync` 而非 `Failed to verify`**。GPS 1PPS 校时作为 v2 增强（v1 仅 NTP）。

## 10. 待评审要点

- [x] AI 平台 → 已确认：**OpenRouter**（OpenAI 兼容），模型手动输入。
- [x] keypad 无大写 → 已改：pda2 预研实现完整修饰键模型（双 Shift 大写 / Alt 临时符号层 / Sym 锁定 / Alt+Enter 扫描），经 7 轮评审 + 真机验证（记录见 `docs/reviews/`）。
- [x] 实现顺序 → 已确认：先在 **pda2 预研** WiFi/AI 配置并真机验证，成熟后移植 allinone（见 `TODO.md` 阶段 0）。
- [x] MP3 扫描目录 → 已确认：优先 `/music`，不存在回退根目录。
- [x] 联网查询阻塞 → 已确认：接受 v1 同步阻塞（词典 8s / AI 15-30s）。
- [x] SPIFFS → 已确认：保留挂载。

设计评审通过（2026-08-08）。后续工作在 `TODO.md` 阶段 0 跟踪。
