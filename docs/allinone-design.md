# 设计方案：allinone 整合固件（GPS + MP3 + 键盘 + 网络词典 + WiFi 配置 + AI 对话）

> 状态：**评审通过**（2026-08-08）；开始阶段 0：pda2 预研（见 `TODO.md`）
> 日期：2026-08-07
> 相关文档：[代码结构与编译方法](build-and-code-structure.md)

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
- **keypad 大写**：pda2 预研阶段已实现 **Shift→大写层**（`peri_keypad.cpp` 第三层 `keymap_shift`，按住 Shift(2,0) 出 A-Z、松开回小写），大写 SSID 可正常输入；**Sym 键仍管符号/数字层**。

## 2. 现有架构调研结论（复用基础）

对 `examples/pda2` / `examples/factory` 及其端口层做了深入调研，结论是 pda2 已经实现了本需求所需的所有基建，可直接复用：

### 2.1 屏幕管理器（scr_mgr）
- `ui_scr_mrg.h` / `ui_scr_mrg.c`：`scr_lifecycle_t {create(parent), entry(), exit(), destroy()}`，`scr_mgr_register/push/pop/switch`。
- 每个 App 一个 `.cpp`，导出全局 `scr_lifecycle_t screen_xxx`，在 `ui_deckpro_entry()`（pda2 `ui_deckpro.cpp` 函数定义）里 `extern scr_lifecycle_t screen_xxx; scr_mgr_register(SCREEN_XXX_ID, &screen_xxx);`（紧随其后 `scr_mgr_register(SCREEN_VOICE_AI_ID, &screen_voice_ai);` 等），最后 `scr_mgr_switch(SCREEN0_ID, false)`。

### 2.2 键盘输入分发
- `peri_keypad.cpp` 在 `loop()` 中被 `keypad_loop()` 轮询，填充全局键值。
- 每个需要键盘的 App 暴露 `void xxx_keyboard_poll(void)`，由 `loop()` 无条件调用，内部用静态 `xxx_kbd_active` 标志门控（`create`/`entry` 置 true，`exit`/`destroy` 置 false）。参考 pda2 `ui_weather.cpp::weather_keyboard_poll`、`ui_gps_enhanced.cpp::gps_keyboard_poll`。
- 读取方式：`keypad_get_val(&c)` + `keypad_set_flag()`。

### 2.3 HTTP / JSON
- `http_utils.h/.cpp`：`http_response_t http_get(url, timeout_ms)`、`http_post(url, body, content_type, auth_header, timeout_ms)`（`WiFiClientSecure setInsecure` + `HTTPClient`）、`http_require_wifi()`。
- JSON 用 cJSON（ESP32 SDK 自带，`#include <cJSON.h>`），仓库无 ArduinoJson。
- **词典接口已现成实现**：`examples/pda2/dict_lookup.cpp::dict_lookup_online(word, result)` —— WiFi 检查 → `snprintf` 拼 URL → `http_get(url, 8000)` → cJSON 解析，UI 为 `ui_dictionary.cpp`。

> ⚠️ **HTTPS 安全注意（评审 #2 修复）**：`http_get`/`http_post` 内部用 `WiFiClientSecure::setInsecure()`，**关闭 TLS 证书校验**，API Key 在 MITM 网络可被窃取（评审 High）。
>
> **allinone 策略**：新增 `http_tls_mode_t { HTTP_TLS_CA_VERIFY, HTTP_TLS_INSECURE }` + `http_set_tls_mode(mode)`。**默认 `HTTP_TLS_CA_VERIFY`**：内置 ISRG Root X1（Let's Encrypt）+ DigiCert Global Root G2 + GlobalSign Root R1，覆盖 99% 公网 HTTPS 证书（含 `openrouter.ai`、`dictionaryapi.dev`）。用户配置自定义端点时，如果证书不在内置列表，`http_require_wifi` 通过但请求会返回 `Failed to verify`；需在 AI Cfg 屏勾选 **"Trust self-signed"（置 NVS `ai_insecure=1`）** 后该次会话切到 `HTTP_TLS_INSECURE`，**屏幕提示"⚠️ TLS bypass"**。词典请求**强制 CA 验证**（无 API Key，风险面小，沿用安全路径）。

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

共 8 屏（菜单 + 7 个功能屏），全部由 keypad 驱动（触摸已移除 → 无 LVGL pointer indev，按钮点击事件不触发，导航走 `*_keyboard_poll`）：

**按键层说明**：keypad 有三层——普通层小写 a-z + 空格 + `\n`/`\b`（`peri_keypad.cpp::keymap`）；**Shift 层大写 A-Z**（按住 Shift(2,0) 临时切换，`peri_keypad.cpp::keymap_shift`）；**数字与 `+ - . : / _ @` 等符号在 Sym 层**（`peri_keypad.cpp::keymap_sym`，按 Sym(3,8) 锁定/解锁）。本文中所有"数字键 `1`-`6`"、"`+`/`-`"均指 **Sym 层对应键**——先按 Sym 进入符号层再按对应键，结束再按 Sym 退出。

| SCREEN ID | 内容 | 来源 |
|---|---|---|
| `SCREEN0_ID` | **菜单：8 个按钮**（"1 GPS / 2 Music / 3 Dict / 4 Keys / 5 WiFi / 6 WiFi Cfg / 7 AI / 8 AI Cfg"），单页 9 格布局（评审 #1 修复：原 6 项缺 WiFi Cfg 和 AI Cfg 入口，首次使用无法设 Key）。`menu_btn_list` 9×8 网格，"GPS"右上角显示 `!` 提示未配 WiFi，AI Cfg 同理；Sym 层 `1`-`8`（先按 Sym）进入对应屏 | 裁剪 pda2 菜单 |
| `SCREEN_GPS_ID` | GPS 状态（坐标/速度/卫星/UTC 时间），沿用 3s 定时刷新；已有 `gps_keyboard_poll`（`\b` 返回） | 复制 `ui_gps_enhanced.cpp` |
| `SCREEN_MP3_ID` | SD `.mp3` 文件浏览器（分页）+ 播放控制 | 新写 `ui_mp3.cpp` |
| `SCREEN_DICT_ID` | keypad 输入英文单词 → 在线查询 → 显示音标 + 释义（前 3 条） | 复制 `ui_dictionary.cpp` + `dict_lookup.cpp` |
| `SCREEN_KEYPAD_ID` | 键盘回显测试：按键字符 + 0xHEX + 累计次数 | 新写 `ui_keypad.cpp` |
| `SCREEN_WIFI_ID` | WiFi 配置：输入 SSID/密码 → 存 NVS → 连接 → 显示 IP/状态 | 新写 `ui_wifi_config.cpp` |
| `SCREEN_AI_ID` | AI 文本对话：输入问题 → 调 OpenAI 兼容接口 → 显示回答 | 新写 `ui_ai_chat.cpp` |
| `SCREEN_AI_CFG_ID` | AI 配置：端点 URL（预填 OpenRouter）/ 模型（手动输入）/ API Key → 存 NVS | 新写 `ui_ai_cfg.cpp` |

### MP3 屏交互
- 浏览态：Sym 层 `1`-`6` 选文件（先按 Sym）；`\n` 下一页（回卷）；`\b` 上一页，首页再按 `\b` 退出回菜单（`audio.stopSong()` + `scr_mgr_pop`）。
- 播放态：`' '` 暂停/继续；`n`/`\n` 下一首；`p` 上一首；Sym 层 `+`/`-` 音量（0-21）；`\b` 回浏览态。
- 扫描 `/music`（不存在则回退根目录），过滤 `.mp3`（上限 ~40 条）；`audio.connecttoFS(SD, path)`。
- 强定义弱回调 `audio_eof_mp3()`：自动切下一首（回卷）。

### 词典屏
- 完全复用 pda2 实现。可选增强：`create` 时若 `audio.isRunning()` 则 `audio.stopSong()`，避免 8s HTTP 阻塞期间音乐卡顿。

### WiFi 配置屏交互
- **SSID 下拉选择**（`lv_dropdown`）：`\n` 触发 `WiFi.scanNetworks()` 扫描并展开列表；列表打开时 `+`/`-`（Sym 层）移动选中、`\n` 选中并跳到密码框、`\b` 取消。**纯 keypad 驱动**（评审 #6 修复：设计已移除触摸，dropdown 的 `LV_EVENT_VALUE_CHANGED` 由触摸释放触发，**allinone 不实现触摸回调**，keypad UI 自行读 `lv_dropdown_get_selected_str` 后关闭列表）。扫描无结果显示 "(none found - Enter:rescan)"。
- **密码独立输入框**：字符键输入（Shift 大写 / Sym 数字符号）、`\n` 保存 + 连接、`\b` 退格（为空时返回 SSID 字段）。无 `w`/`1` 保留键（SSID 常见字母 'w'）。
- 保存：`Preferences` namespace `wifi`，`putString("ssid"/"pass")`；随后 `WiFi.begin`，显示 `Connecting…` → 成功（IP）或失败原因（Auth fail / not found / timeout）。
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
ui_scr_mrg.h / ui_scr_mrg.c  # 屏幕管理器
peri_keypad.cpp              # TCA8418 键盘
http_utils.h / http_utils.cpp# http_get / http_require_wifi
dict_lookup.h / dict_lookup.cpp  # dict_lookup_online（dictionaryapi.dev）
ui_dictionary.cpp            # 词典屏
src/img_GPS.c  src/img_dictionary.c  src/img_touch.c  src/img_SD.c   # 4 个菜单图标
```

### 5.2 复制后裁剪
| 文件 | 裁剪要点 |
|---|---|
| `factory.ino` → **`allinone.ino`** | setup()：去掉 BQ/DRV2605 驱动（**保留 I2C 0x5A 版本探测** → `isT_Deck_Pro_v1_1`，见 §3）/BHI260AP/A7682E/`configTzTime`，并删除全部触摸初始化/注册/轮询代码（见下方"触摸相关代码清理清单"）；保留 GPIO 上电、SPI CS、`shared_spi_bus_init`、I2C 扫描、SPIFFS、`SPI.begin`；peri 初始化精简为 ink_screen / keypad / sd_care_init / gps_init / pcm5102a_init（无条件）；启动时从 NVS 读 WiFi 凭据（有则 `WiFi.begin`）。loop()：`lv_task_handler` + `keypad_loop` + 8 个 `*_keyboard_poll`（menu/gps/mp3/dict/keypadtest/wifi_cfg/ai_chat/ai_cfg）+ `audio.loop` |
| `factory.h` | 去掉 TinyGSM/BQ/DRV/TouchDrv；保留 `Audio audio`、`peri_init_st[]`、`shared_spi_*`、`disp_full_refr` |
| `peripheral.h` | `E_PERI_*` 裁为 `{KYEPAD, SD, GPS, PCM5102A, INK_SCREEN, NUM_MAX}`；保留 keypad/gps 原型 |
| `ui_deckpro.h` | `SCREEN_XXX_ID` 裁为 8 个；保留 `struct menu_btn`、`scr_back_btn_create`、`ui_deckpro_entry` |
| `ui_deckpro.cpp` | 删除 low-voltage 块、screen1-12、taskbar/gesture/touch 定时器；保留 `scr_back_btn_create` + 菜单（`menu_btn_list` 裁为 8 项，`menu_btn_event_cb` 已存在）；`FONT_*` 宏改指 `&lv_font_montserrat_14`；`ui_deckpro_entry()` 只注册 8 屏；新增 `menu_keyboard_poll()`（`'1'`-`'8'` → `scr_mgr_push`） |
| `ui_deckpro_port.h / .cpp` | 只保留 `ui_disp_full_refr`、`ui_gps_task_suspend/resume`、`ui_gps_get_coord/data/time/satellites/speed`、`ui_input_get_keypay_val/set_flag` |
| `peri_gps.cpp` | `gps_task_suspend/resume`（`gps_task_suspend`/`gps_task_resume` 函数定义）加 **NULL 守卫**；`gps_init` 握手失败时不创建任务（`gps_init` 函数体），`gps_handle` 保持 NULL；**评审 #4 修复**：12 个快照字段（`gps_lat/lng/alt/speed/year/month/day/hour/minute/second/vsat`）由 `displayInfo()` 在 GPS 任务中无锁写，LVGL 端 5 次 getter 组合成快照会被任务打断。修复：定义 `gps_snapshot_t` 结构 + `gps_get_snapshot(snapshot_t*)` 函数，函数内 `taskENTER_CRITICAL` 一次性拷贝所有字段（持有时间 < 1μs），退出临界区。`ui_gps_enhanced.cpp` 改用 `ui_gps_get_snapshot()` 一次读完 |
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
| `ui_wifi_config.cpp` | **SSID 为 `lv_dropdown`（`WiFi.scanNetworks()` 结果）**：`\n` 扫描+展开，`+`/`-`（Sym 层）移动、`\n` 选中跳密码、`\b` 取消（**评审 #6 修复：不挂 `LV_EVENT_VALUE_CHANGED` 触摸回调**，因 design §5.2 删触摸；选中读取走 `lv_dropdown_get_selected_str` + `lv_dropdown_close`）；**密码独立 `lv_textarea`**：`\n` 保存+连接、`\b` 退格/回 SSID → `Preferences`（namespace `wifi`）存 NVS → `WiFi.begin` 连接 → 显示状态/IP/失败原因、`wifi_cfg_keyboard_poll`、`scr_lifecycle_t screen_wifi`。无 `w`/`1` 保留键 |
| `ui_ai_chat.cpp` | 问题输入 + `\n` 发送 → `openai_chat()` → 分页显示回答、`ai_chat_keyboard_poll`（浏览回答时 `\n` 下一页/`\b` 回上一页或退出；**无 `c` 快捷键**，AI 配置走菜单）、`scr_lifecycle_t screen_ai_chat` |
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
3. **墨水屏刷新术语分层（评审 #5 修复）**：
   - **LVGL 整屏提交**（`disp_drv.full_refresh=1`，`factory.ino::lvgl_init`）：LVGL 整屏 area 一次性 flush → EPD flush 走 `setFullWindow()`（GxEPD2 全屏波形）或 `setPartialWindow()`（EPD 局部波形，由 `disp_refr_mode` 决定）。
   - **EPD 局部波形**（`setPartialWindow` + `display->nextPage()` 默认）：GDEQ031T10 局刷 ≈ 250ms，适用于"按键触发的小改动"。
   - **EPD 全屏波形**（`setFullWindow` + `fillScreen(WHITE)`）：GDEQ031T10 全刷 ≈ 1-2s，对应**深色残留累积 → 必须周期性全刷才能将"鬼影"清除**。
   - **现状**：pda2 仅 `ui_disp_full_refr()` 触发 setFullWindow + fillScreen 一次；其他时间走 `setPartialWindow`。**长时间使用后局部刷新会留残影**——必须设计**周期性 EPD 全刷**才能清除。
   - **allinone 方案**：新增 `epd_force_full_refresh()` 计数器，由 `flush_timer_cb` 每 60 次局部刷新（≈ 累计刷屏分钟级）触发一次 `setFullWindow` + 强制走 EPD 全屏波形；同时 `ui_disp_full_refr()` 进屏时仍强制一次全刷。GPS 屏 3s 定时只写局部。
4. **触摸移除**（删除清单见 §5.2）→ 无 pointer indev，按钮 `LV_EVENT_CLICKED` 不触发，所有导航必须靠 keypad poll。
5. **dictionaryapi.dev 仅英文**：UI 文案用英文（与 pda2 一致）；词不在库返回 404 → 显示 "Word not found"。
6. **SD 未插卡**：`sd_care_init` false → MP3 屏显示 "No SD"，词典本地扫描快速失败，不崩溃。
7. **GPS UBX 握手启动时阻塞 ~2.4s**（LVGL 初始化之前），仅延迟首屏。
8. **`.ino` 发现规则**：`allinone/` 顶层只能有一个 `allinone.ino`；已改用 NVS 运行时配置，`config_keys.h` 不再必需。
9. **keypad 输入限制**：普通层小写 a-z + 空格 + `\n`/`\b`；**Shift 层大写 A-Z**（按住 Shift(2,0)，`peri_keypad.cpp::keymap_shift`）；**数字与 `+ - . : / _ @` 等符号在 Sym 层**（按 Sym(3,8) 锁定，`peri_keypad.cpp::keymap_sym`），文档中数字/符号按键均需先按 Sym（见 §4 键层说明）。**大写已在 pda2 预研实现**（§1 决策）：大小写 SSID 均可输入；OpenRouter Key `sk-or-v1-...`、模型 ID 多为小写，无需 Shift。
10. **WiFi/AI 配置屏**：所有联网功能依赖已配好的 WiFi（未配 → 提示先配 WiFi）；AI 请求与词典一样**同步阻塞**（`http_post` 默认 15s 超时，OpenRouter 模型响应可能更慢，可放宽到 30s），AI 屏与音乐屏互斥；Key/端点存 NVS 明文（`Preferences`，namespace `wifi`/`ai`），不打印到日志；OpenRouter 为 OpenAI 兼容接口，个别字段差异以实际响应调通。

## 10. 待评审要点

- [x] AI 平台 → 已确认：**OpenRouter**（OpenAI 兼容），模型手动输入。
- [x] keypad 无大写 → 已改：pda2 预研实现 **Shift→大写层**（按住 Shift(2,0)，Sym 键仍管符号层）。
- [x] 实现顺序 → 已确认：先在 **pda2 预研** WiFi/AI 配置并真机验证，成熟后移植 allinone（见 `TODO.md` 阶段 0）。
- [x] MP3 扫描目录 → 已确认：优先 `/music`，不存在回退根目录。
- [x] 联网查询阻塞 → 已确认：接受 v1 同步阻塞（词典 8s / AI 15-30s）。
- [x] SPIFFS → 已确认：保留挂载。

设计评审通过（2026-08-08）。后续工作在 `TODO.md` 阶段 0 跟踪。
