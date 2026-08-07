# 设计方案：allinone 整合固件（GPS + MP3 + 键盘 + 网络词典）

> 状态：**待评审**（未开始实现）
> 日期：2026-08-07
> 相关文档：[代码结构与编译方法](build-and-code-structure.md)

## 1. 背景与目标

仓库现有十几个**相互独立**的测试示例（`test_GPS`、`test_pcm5102a`、`test_keypad`、`test_wifi` 等），每个示例都是一个独立 sketch（`setup()`/`loop()`），只能单独烧录。用户希望把其中 4 类功能合并进**一个固件**，烧录一次即可通过菜单切换使用：

| # | 功能 | 来源示例 | 说明 |
|---|---|---|---|
| 1 | GPS 定位 | `test_GPS` | TinyGPSPlus，UART Serial2（rx44/tx43），38400 8N1，u-blox UBX 恢复 |
| 2 | MP3 播放 | `test_pcm5102a` | ESP32-audioI2S，I2S BCLK7/LRC9/DOUT8，PCM5102A DAC |
| 3 | 键盘输入 | `test_keypad` | Adafruit TCA8418，I2C 0x34，4×10 矩阵，轮询 |
| 4 | 网络查询 | `test_wifi` | 在线词典 `https://dictionaryapi.dev/` |

### 已确认的需求决策

- **MP3 来源**：SD 卡本地文件（非 SPIFFS、非网络流）。
- **网络功能形态**：调用在线词典接口 `api.dictionaryapi.dev/api/v2/entries/en/<word>`。
- **组织方式**：新建独立示例 `examples/allinone`（不动现有 `pda2`/`factory`）。

## 2. 现有架构调研结论（复用基础）

对 `examples/pda2` / `examples/factory` 及其端口层做了深入调研，结论是 pda2 已经实现了本需求所需的所有基建，可直接复用：

### 2.1 屏幕管理器（scr_mgr）
- `ui_scr_mrg.h` / `ui_scr_mrg.c`：`scr_lifecycle_t {create(parent), entry(), exit(), destroy()}`，`scr_mgr_register/push/pop/switch`。
- 每个 App 一个 `.cpp`，导出全局 `scr_lifecycle_t screen_xxx`，在 `ui_deckpro_entry()`（pda2 `ui_deckpro.cpp:3128-3198`）里 `extern scr_lifecycle_t screen_xxx; scr_mgr_register(SCREEN_XXX_ID, &screen_xxx);`，最后 `scr_mgr_switch(SCREEN0_ID, false)`。

### 2.2 键盘输入分发
- `peri_keypad.cpp` 在 `loop()` 中被 `keypad_loop()` 轮询，填充全局键值。
- 每个需要键盘的 App 暴露 `void xxx_keyboard_poll(void)`，由 `loop()` 无条件调用，内部用静态 `xxx_kbd_active` 标志门控（`create`/`entry` 置 true，`exit`/`destroy` 置 false）。参考 pda2 `ui_weather.cpp:427`、`ui_gps_enhanced.cpp:344-359`。
- 读取方式：`keypad_get_val(&c)` + `keypad_set_flag()`。

### 2.3 HTTP / JSON
- `http_utils.h/.cpp`：`http_response_t http_get(url, timeout_ms)`（`WiFiClientSecure setInsecure` + `HTTPClient`）、`http_require_wifi()`。
- JSON 用 cJSON（ESP32 SDK 自带，`#include <cJSON.h>`），仓库无 ArduinoJson。
- **词典接口已现成实现**：`examples/pda2/dict_lookup.cpp:701` `dict_lookup_online(word, result)` —— WiFi 检查 → `snprintf` 拼 URL → `http_get(url, 8000)` → cJSON 解析，UI 为 `ui_dictionary.cpp`。

> ⚠️ **HTTPS 安全注意**：`http_get` 内部用 `WiFiClientSecure::setInsecure()`，会**关闭 TLS 证书校验**，HTTPS 流量可被中间人（MITM）读取/篡改。本固件是开发/验证用途、词典为公开 HTTPS 端点，v1 沿用 pda2 行为可接受；但**不应**在不可信网络或正式产品中使用。如需加固，后续可用 `setCACert()` 加载 `dictionaryapi.dev` 的 CA 根证书做校验。

### 2.4 外设 / 启动
- `peri_gps.cpp`：FreeRTOS 任务解析 NMEA + UBX 恢复握手，`gps_get_coord/time/satellites/speed` 等 getter，`gps_task_suspend/resume` 控制任务启停。
- `peri_keypad.cpp`：TCA8418 初始化 + 轮询 + keymap。
- 启动流程（pda2 `factory.ino:538-692`）：GPIO 上电 → SPI CS 拉高 + `shared_spi_bus_init()` → I2C 扫描 → SPIFFS → `SPI.begin` → `peri_init_st[]` 各外设 init → `lvgl_init` + `ui_deckpro_entry` + `disp_full_refr` → WiFi 自动连接（`#if defined(WIFI_SSID)`）。
- 引脚（`utilities.h`）：SDA13/SCL14、SPI SCK36/MOSI33/MISO47、GPS RXD44/TXD43/GPS_EN39、I2S BCLK7/DOUT8/LRC9、SD_CS48、EPD_CS34。
- **SD 与 EPD 共享 SPI 总线**：pda2 用 `shared_spi_lock()/unlock()` + `shared_spi_prepare_device()` 保护显式 SD 操作。

## 3. 总体方案

复制并**裁剪** pda2 得到自包含的 `examples/allinone`：

- **保留**：scr_mgr、peri_keypad、peri_gps、http_utils、dict_lookup、ui_gps_enhanced、ui_dictionary、EPD+LVGL 刷新链。
- **丢弃**：LoRa、BHI260AP IMU、触摸（CST）、BQ25896/BQ27220 电池管理、DRV2605 电机、A7682E 4G、语音 AI、计算器/天气/日历 App。
- **新写**：`ui_mp3.cpp`（SD 文件浏览 + 播放）、`ui_keypad.cpp`（键盘回显测试）。
- **字体**：不复用 `Font_Mono_Bold_*.c`（约 73KB），把 `ui_deckpro.cpp` 里的 `FONT_*` 宏改指内置 `&lv_font_montserrat_14`。
- **图标**：复用 pda2 `src/` 里现成的 4 个 50×50 图标 `img_GPS.c / img_dictionary.c / img_touch.c / img_SD.c`。

## 4. 屏幕设计

共 5 屏，全部由 keypad 驱动（触摸已移除 → 无 LVGL pointer indev，按钮点击事件不触发，导航走 `*_keyboard_poll`）：

| SCREEN ID | 内容 | 来源 |
|---|---|---|
| `SCREEN0_ID` | 菜单：4 个按钮（"1 GPS / 2 Music / 3 Dict / 4 Keys"），数字键 `1`-`4` 进入对应屏 | 裁剪 pda2 菜单 |
| `SCREEN_GPS_ID` | GPS 状态（坐标/速度/卫星/UTC 时间），沿用 3s 定时刷新；已有 `gps_keyboard_poll`（`\b` 返回） | 复制 `ui_gps_enhanced.cpp` |
| `SCREEN_MP3_ID` | SD `.mp3` 文件浏览器（分页）+ 播放控制 | 新写 `ui_mp3.cpp` |
| `SCREEN_DICT_ID` | keypad 输入英文单词 → 在线查询 → 显示音标 + 释义（前 3 条） | 复制 `ui_dictionary.cpp` + `dict_lookup.cpp` |
| `SCREEN_KEYPAD_ID` | 键盘回显测试：按键字符 + 0xHEX + 累计次数 | 新写 `ui_keypad.cpp` |

### MP3 屏交互
- 浏览态：`1`-`6` 选文件；`\n` 下一页（回卷）；`\b` 上一页，首页再按 `\b` 退出回菜单（`audio.stopSong()` + `scr_mgr_pop`）。
- 播放态：`' '` 暂停/继续；`n`/`\n` 下一首；`p` 上一首；`+`/`-` 音量（0-21）；`\b` 回浏览态。
- 扫描 `/music`（不存在则回退根目录），过滤 `.mp3`（上限 ~40 条）；`audio.connecttoFS(SD, path)`。
- 强定义弱回调 `audio_eof_mp3()`：自动切下一首（回卷）。

### 词典屏
- 完全复用 pda2 实现。可选增强：`create` 时若 `audio.isRunning()` 则 `audio.stopSong()`，避免 8s HTTP 阻塞期间音乐卡顿。

## 5. 文件清单

### 5.1 原样复制（来自 `examples/pda2/`）
```
utilities.h                  # 引脚定义
ui_scr_mrg.h / ui_scr_mrg.c  # 屏幕管理器
peri_keypad.cpp              # TCA8418 键盘
peri_gps.cpp                 # GPS 任务 + UBX 恢复
http_utils.h / http_utils.cpp# http_get / http_require_wifi
dict_lookup.h / dict_lookup.cpp  # dict_lookup_online（dictionaryapi.dev）
ui_gps_enhanced.cpp          # GPS 屏
ui_dictionary.cpp            # 词典屏
src/img_GPS.c  src/img_dictionary.c  src/img_touch.c  src/img_SD.c   # 4 个菜单图标
```

### 5.2 复制后裁剪
| 文件 | 裁剪要点 |
|---|---|
| `factory.ino` → **`allinone.ino`** | setup()：去掉 BQ/DRV/BHI260AP/A7682E/`configTzTime`，并删除全部触摸初始化/注册/轮询代码（见下方"触摸相关代码清理清单"）；保留 GPIO 上电、SPI CS、`shared_spi_bus_init`、I2C 扫描、SPIFFS、`SPI.begin`；peri 初始化精简为 ink_screen / keypad / sd_care_init / gps_init / pcm5102a_init（无条件）；保留 WiFi 自动连接块。loop()：`lv_task_handler` + `keypad_loop` + 5 个 `*_keyboard_poll` + `audio.loop` |
| `factory.h` | 去掉 TinyGSM/BQ/DRV/TouchDrv；保留 `Audio audio`、`peri_init_st[]`、`shared_spi_*`、`disp_full_refr` |
| `peripheral.h` | `E_PERI_*` 裁为 `{KYEPAD, SD, GPS, PCM5102A, INK_SCREEN, NUM_MAX}`；保留 keypad/gps 原型 |
| `ui_deckpro.h` | `SCREEN_XXX_ID` 裁为 5 个；保留 `struct menu_btn`、`scr_back_btn_create`、`ui_deckpro_entry` |
| `ui_deckpro.cpp` | 删除 low-voltage 块、screen1-12、taskbar/gesture/touch 定时器；保留 `scr_back_btn_create` + 菜单（`menu_btn_list` 裁为 4 项，`menu_btn_event_cb` 已存在）；`FONT_*` 宏改指 `&lv_font_montserrat_14`；`ui_deckpro_entry()` 只注册 5 屏；新增 `menu_keyboard_poll()`（`'1'`-`'4'` → `scr_mgr_push`） |
| `ui_deckpro_port.h / .cpp` | 只保留 `ui_disp_full_refr`、`ui_gps_task_suspend/resume`、`ui_gps_get_coord/data/time/satellites/speed`、`ui_input_get_keypay_val/set_flag` |

#### 触摸相关代码清理清单（防未定义引用 / 残留无效输入设备）

触摸初始化、注册与轮询代码分散在 4 处，按下列位置**全部删除**：

| 位置 | 需删除内容 |
|---|---|
| `factory.ino:268-274` | `touchpad_read()` 静态函数（LVGL pointer 读回调，内部调 `hyn_touch_get_point`） |
| `factory.ino:310-318` | LVGL pointer 输入设备注册块（`lv_indev_drv_init` + `LV_INDEV_TYPE_POINTER` + `read_cb = touchpad_read` + `lv_indev_drv_register`） |
| `factory.ino:669` | `peri_init_st[E_PERI_TOUCH] = hyn_touch_init();`（触摸不进 peri 初始化表） |
| `factory.h:73-74` | `hyn_touch_init()` / `hyn_touch_get_point()` 原型声明及触摸头文件 include |
| `ui_deckpro_port.cpp:537-546` | `ui_input_get_touch_coord()`（内部调 `hyn_touch_get_point`），以及手势/触摸相关端口函数 |
| `ui_deckpro.cpp` | taskbar/gesture/touch 定时器与触摸回调（见 §5.2 主表 `ui_deckpro.cpp` 行） |
| 文件 | `hyn_*`（`hyn_touch.cpp`、`hyn_cst66xx.c`、`hyn_core.*` 等）一律不复制 |

`peripheral.h` 的 `E_PERI_*` 已不含 `E_PERI_TOUCH`（见 §5.2 主表）；删除后 `loop()` 不再调用任何触摸轮询，LVGL 无 pointer indev，按钮点击事件不触发，导航全靠 keypad poll（见 §9 风险 4）。

### 5.3 新写
| 文件 | 内容 |
|---|---|
| `ui_mp3.cpp` | `ensure_sd()`（`shared_spi_lock` + `SD.begin(48)`）、`scan_files()`、分页列表渲染、播放/暂停/切歌/音量、强定义 `audio_eof_mp3`、`mp3_keyboard_poll`、`scr_lifecycle_t screen_mp3` |
| `ui_keypad.cpp` | 返回按钮 + 提示 + 回显标签、`keypadtest_keyboard_poll`（`\b` 返回菜单）、`scr_lifecycle_t screen_keypad` |
| `src/assets.h` | 仅 `LV_IMG_DECLARE` 4 个图标（`extern "C"` 包裹） |
| `config_keys.h` | 从 `config_keys.h.example` 复制 stub（gitignored；填 `WIFI_SSID`/`WIFI_PASSWORD` 才联网） |

**不复制**：`ui_sd_test.cpp`、`ui_weather.cpp`、`ui_calculator.cpp`、`ui_calendar.cpp`、`ui_voice_ai.cpp`、`gemini_api.*`、`pdm_recorder.*`、`lunar_calendar.*`、`peri_lora.cpp`、`peri_gyroscope.cpp`、`hyn_*`（触摸）、全部 `Font_Mono_Bold_*.c` 及用不到的 `img_*.c`。

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

1. 建目录 `examples/allinone/` + `src/`；复制 `config_keys.h.example` → `config_keys.h`。
2. 复制 §5.1 清单与 4 个图标。
3. 写 `src/assets.h`。
4. 裁剪 `peripheral.h`、`factory.h`、`ui_deckpro.h`、`ui_deckpro_port.h/.cpp`（触摸原型/端口函数删除按 §5.2"触摸相关代码清理清单"）。
5. 裁剪 `ui_deckpro.cpp`（大文件，按 `#if 1 … #endif` 区块整块删除 screen1-12 与 low-voltage；改写 `ui_deckpro_entry`；新增 `menu_keyboard_poll`）。
6. `factory.ino` → `allinone.ino`，裁剪 setup/loop。
7. 新写 `ui_mp3.cpp`、`ui_keypad.cpp`。
8. `platformio.ini` 追加 `[env:allinone]`。
9. 编译并修复链接错误（见 §8）。

## 8. 验证

```powershell
& "C:\Users\asdfo\.platformio\penv\Scripts\pio.exe" run -e allinone --jobs 8
```

- **构建**：`.pio/build/allinone/firmware.bin` 生成；无未定义引用（若出现 `bq27220`/`drv`/`modem`/`hyn_touch_*`/`lora_*` 说明有漏剪引用）。Flash 余量充足（pda2 仅占 1.9MB / 16MB）。
- **烧录（有硬件时）**：`pio run -e allinone -t upload -t monitor` → 串口见 I2C 扫描到 `KEYBOARD 0x34`、SD 挂载、GPS UBX 握手成功；EPD 渲染菜单；`1/2/3/4` 进各屏、`\b` 返回；配置 WiFi 后词典查询可用；SD `/music` 的 MP3 可播放/暂停/切歌。

## 9. 风险与注意事项

1. **SD 与 EPD 共享 SPI**：所有显式 SD 操作套 `shared_spi_lock()/unlock()`；`audio.loop()` 流式读不加锁；EPD 只在按键/进屏/GPS 3s 定时器到期时刷新，不做逐帧刷新，缩小冲突窗口。
2. **`audio.loop()` 必须每轮调用**：不能阻塞。词典 8s `http_get` 为同步阻塞（pda2 原样行为），词典屏与音乐屏互斥，可接受；v1 不做 weather 式后台任务。
3. **墨水屏全刷慢（`full_refresh=1`）**：pda2 显示驱动设置 `disp_drv.full_refresh = 1`（`factory.ino:306`），LVGL 每次 flush 都整屏 Paged 输出到墨水屏，**没有真正的局部刷新**。GPS 屏每 3s 定时（`ui_gps_enhanced.cpp:424`）更新一次，即每 3s 一次全刷（GDEQ031T10 全刷约 1-2s，是 SPI 冲突窗口最长的一段）；MP3/键盘屏仅在按键时触发刷新。分页不用滚动（仓库惯例）。若 v2 想减少全刷，可拉长 GPS 刷新间隔或改为事件驱动。
4. **触摸移除**（删除清单见 §5.2）→ 无 pointer indev，按钮 `LV_EVENT_CLICKED` 不触发，所有导航必须靠 keypad poll。
5. **dictionaryapi.dev 仅英文**：UI 文案用英文（与 pda2 一致）；词不在库返回 404 → 显示 "Word not found"。
6. **SD 未插卡**：`sd_care_init` false → MP3 屏显示 "No SD"，词典本地扫描快速失败，不崩溃。
7. **GPS UBX 握手启动时阻塞 ~2.4s**（LVGL 初始化之前），仅延迟首屏。
8. **`.ino` 发现规则**：`allinone/` 顶层只能有一个 `allinone.ino`；`config_keys.h` 必须在编译前存在（stub 即可）。

## 10. 待评审要点

- [ ] 目录/env 命名 `allinone` 是否可接受？
- [ ] 4 屏 + 菜单的划分是否满足需求？是否需要加第 5 个功能（如 WiFi 信号状态页）？
- [ ] MP3 扫描目录 `/music`（回退根目录）是否符合预期？
- [ ] 是否接受"词典查询同步阻塞 8s"的 v1 取舍？
- [ ] 是否保留 SPIFFS 挂载（audio 已不需要，但部分库依赖）？
