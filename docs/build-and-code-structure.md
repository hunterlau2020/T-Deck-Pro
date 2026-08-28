# T-Deck-Pro 代码结构与编译方法

> 本文记录 T-Deck-Pro 仓库（[Xinyuan-LilyGO/T-Deck-Pro](https://github.com/Xinyuan-LilyGO/T-Deck-Pro)）的代码组织、构建系统配置、编译方法流程，以及构建过程中踩过的 PlatformIO 坑。
>
> 依据当前工作副本整理，构建环境：Windows 11 + PlatformIO Core 6.1.19（espressif32 platform 6.5.0，Arduino framework 2.0.14）。

## 1. 项目概述

- **MCU**：ESP32-S3 @ 240MHz，320KB SRAM，16MB Flash，8MB QSPI PSRAM
- **开发框架**：PlatformIO + Arduino（`framework = arduino`）
- **显示屏**：3.1" 墨水屏（GDEQ031T10，GxEPD2 驱动），`LV_COLOR_DEPTH = 1` 单色
- **外设**：TCA8418 键盘矩阵、CST226SE 触摸（HYN 驱动）、SX1262 LoRa、GPS、BHI260AP IMU、BQ25896 充电 / BQ27220 电量计、DRV2605 振动、A7682E 4G 模块、PCM5102A 音频、SD 卡
- **核心板配置**：`boards/t-deck-pro.json`（board 名 `T-Deck-Pro`）

## 2. 目录结构

```
T-Deck-Pro/
├── platformio.ini            # PlatformIO 工程配置（环境、flags、依赖）
├── boards/t-deck-pro.json    # 自定义板级定义
├── config/lv_conf.h          # LVGL 全局配置（factory/lvgl/pda 系列固件用它）
├── lib/                      # 第三方库全部 vendored 在此（离线可用）
│   ├── Adafruit BusIO / GFX / SH110X / TCA8418
│   ├── BQ27220 / XPowersLib / TinyGSM / TinyGPSPlus
│   ├── GxEPD2 / lvgl / RadioLib / SensorLib
│   ├── ESP32-audioI2S / ESP32-BLE-Keyboard / Adafruit_DRV2605 / U8g2_for_Adafruit_GFX
├── examples/                 # 每个示例一个目录，顶层一个 <name>.ino
│   ├── test_GPS / test_sd / test_wifi / test_bluetooth / test_EPD
│   ├── test_keypad / test_touchpad / test_BHI260AP / test_pcm5102a
│   ├── test_lora_send / test_lora_recv
│   ├── test_lvgl / factory / pda2          # LVGL 系列（含 src/ 图片资源）
│   ├── A7682E / bq27220 / bq25896_shutdown / test_motor/ / touch_hyn_core/
├── script/set_srcdir.py     # 自定义 pre-build 脚本（多示例构建核心，见 §5）
├── firmware/                 # 出厂烧录包（含 spiffs.bin 等）
├── docs/                     # 文档
└── .pio/build/<env>/         # 每个环境的独立构建输出（firmware.bin 等）
```

**关键点**：所有第三方库在 `lib/` 目录内，PlatformIO 默认把工程 `lib/` 加入搜索路径（配合 LDF 按 `#include` 按需链接）。因此 `lib_deps` 里只需内置库 `SPI/Wire/FS/SPIFFS/EEPROM`，示例所需的 GxEPD2、lvgl、TinyGPSPlus、RadioLib 等都能直接编译。

## 3. platformio.ini 配置详解

### `[platformio]` 段（工程级，全局生效）

```ini
[platformio]
boards_dir = boards
default_envs = T-Deck-Pro          ; 不带 -e 时默认构建的环境
src_dir = examples/test_GPS        ; 全局 src_dir（仅此段可设置，见 §5 坑 1）
```

### `[env]` 段（所有环境继承的公共设置）

- `platform = espressif32@6.5.0`、`board = T-Deck-Pro`、`framework = arduino`
- `build_flags`：`-DBOARD_HAS_PSRAM`、`-DARDUINO_USB_CDC_ON_BOOT=1`、`-DLV_LVGL_H_INCLUDE_SIMPLE`、`-DLV_CONF_INCLUDE_SIMPLE`、`-DTINY_GSM_MODEM_SIM7672`、`-DCORE_DEBUG_LEVEL=3` 等
- `extra_scripts = pre:script/set_srcdir.py` ← **多示例构建的关键**（见 §5）

### 环境列表（`[env:xxx]`）

| 环境名 | 对应示例 | 备注 |
|---|---|---|
| `T-Deck-Pro` | `examples/test_GPS` | 默认环境，含 `-include config/lv_conf.h` |
| `test_keypad` | `examples/test_keypad` | |
| `test_touchpad` | `examples/test_touchpad` | |
| `test_BHI260AP` | `examples/test_BHI260AP` | |
| `test_pcm5102a` | `examples/test_pcm5102a` | |
| `test_lora_send` | `examples/test_lora_send` | |
| `test_lora_recv` | `examples/test_lora_recv` | |
| `test_lvgl` | `examples/test_lvgl` | 含 `-include config/lv_conf.h` |
| `factory` | `examples/factory` | 工厂固件，含 `-include config/lv_conf.h` |
| `pda2` | `examples/pda2` | PDA 应用，含 `-DARDUINO_T_DECK_PRO` + lv_conf |

> 每个环境有**独立构建目录** `.pio/build/<env名>`，互不冲突，这也是可以并行编译的原因。

## 4. 编译方法

### 4.1 编译单个示例

```powershell
# 默认环境（test_GPS）
& "C:\Users\asdfo\.platformio\penv\Scripts\pio.exe" run -e T-Deck-Pro

# 指定环境，例如 factory
& "C:\Users\asdfo\.platformio\penv\Scripts\pio.exe" run -e factory
```

### 4.2 一次编译多个示例（并行）

```powershell
& "C:\Users\asdfo\.platformio\penv\Scripts\pio.exe" run `
  -e test_keypad -e test_touchpad -e test_BHI260AP -e test_pcm5102a `
  -e test_lora_send -e test_lora_recv -e test_lvgl -e factory -e pda2 `
  --jobs 8
```

`--jobs N` 控制并行任务数（默认=CPU 核数）。实测 9 个环境并行编译约 3.5 分钟。

### 4.3 烧录 / 串口监视

```powershell
pio run -e factory -t upload          # 烧录到设备（USB 连接）
pio run -e factory -t upload -t monitor   # 烧录 + 打开串口监视器（115200）
```

### 4.4 编译产物

每个环境输出到 `.pio/build/<env>/`：

| 文件 | 说明 |
|---|---|
| `firmware.bin` | 应用程序固件（烧录用） |
| `bootloader.bin` | 引导程序 |
| `partitions.bin` | 分区表 |
| `firmware.elf` / `firmware.map` | ELF 与内存映射（调试用） |

### 4.5 添加新示例

1. 在 `examples/` 下新建目录和 `<name>.ino`
2. `platformio.ini` 增加 `[env:<name>]` 环境（需要 LVGL 时加上 `-include config/lv_conf.h`）
3. `script/set_srcdir.py` 中环境名默认映射到同名目录，无需改动

## 5. PlatformIO 关键限制与踩坑（重要）

本次多示例构建踩过的坑，均经源码（`platformio/fs.py`、`builder/tools/piobuild.py`、`builder/tools/pioino.py`）确认：

### 坑 1：`src_dir` 是**全局**选项，不能放在 `[env:xxx]` 里

- 放在环境段会报警告 `Warning! Ignore unknown configuration option 'src_dir' in section [env:xxx]` 并被忽略。
- 且 `[platformio]` 段的 `src_dir` 会作用于**所有**环境——第一次尝试时 9 个环境全部编成了同一个 `test_GPS`。
- 本版本（6.1.19）`pio run` **没有** `-o/--project-option` 覆盖选项，无法用命令行覆盖。

### 坑 2：`build_src_filter` 对 `.ino` 无效

- `build_src_filter` 是合法的环境级选项（映射到 SCons 变量 `SRC_FILTER`），能过滤 `.c/.cpp` 等源文件。
- 但 PlatformIO 发现 `.ino` 走的是 `FindInoNodes`（`pioino.py`），只做 `$PROJECT_SRC_DIR/*.ino` **顶层** glob，**不递归、不读取 src_filter**。
- 因此「`src_dir = .` + 各环境 `build_src_filter = +<examples/xxx>`」的方案不可行：顶层找不到任何 `.ino`，报 `Error: Nothing to build`。

### 坑 3：解决方案 —— pre-build 脚本覆盖 `PROJECT_SRC_DIR`

- `main.py` 中 `PROJECT_SRC_DIR` 从配置读取（第 123 行），而 `extra_scripts` 的 **pre 脚本在其之后、`ConvertInoToCpp` 之前**运行（第 167 行），此时 `env.Replace(PROJECT_SRC_DIR=...)` 能生效。
- 项目已实现通用脚本 `script/set_srcdir.py`：按 `$PIOENV` 环境名推导示例目录并覆盖 `PROJECT_SRC_DIR`（`T-Deck-Pro` 特例映射到 `test_GPS`），已在公共 `[env]` 段通过 `extra_scripts = pre:script/set_srcdir.py` 挂载。
- 效果：每个环境编自己的示例、有独立 build 目录、可 `--jobs` 并行。

### 坑 4：`pda2` 需要 `examples/pda2/config_keys.h`

- `examples/pda2/factory.ino`、`ui_voice_ai.cpp`、`lunar_calendar.cpp` 等强制 `#include "config_keys.h"`。
- 仓库只提供 `config_keys.h.example`，该文件 gitignored。编译前需复制生成：
  ```powershell
  Copy-Item examples\pda2\config_keys.h.example examples\pda2\config_keys.h
  ```
- 所有值留空（注释掉）即可正常编译；填入 WiFi/Gemini/OpenWeatherMap 等凭据才启用对应功能。

### 坑 5：`src_dir` 需指向示例目录才能发现 `.ino`

- 不设置 `src_dir` 时默认指向 `<工程>/src`（不存在则报 `Nothing to build`）。
- 原仓库的做法是手动改 `[platformio] src_dir` 后 `pio run -e T-Deck-Pro`——每次只能编一个示例。本项目用 §坑3 的脚本替代了这个流程。

### 坑 6：COM5 烧录中途 USB CDC 失联（2026-08-28，分块烧录恢复）

- 现象：`-t upload` 固定在总进度 ~21-29% 处掉线，pySerial 报
  `PermissionError(13)`（"设备不识别此命令"/"连到系统上的设备没有发挥作用"）；
  降速 115200、`--before usb_reset`、`--no-stub` 均无效。当天首次整刷曾
  成功，之后设备侧（线缆/接口/供电）状态劣化。
- 危险点：esptool **先擦后写**——失败重试后 app 分区已被擦掉大半，设备
  无法正常启动（bootloader/分区表/NVS/SPIFFS 在别的分区，不受影响）。
- 恢复方案（无需重插线缆即可走通）：把 `firmware.bin` 切成 256KB 块，
  逐块 `write_flash`（每块独立连接、失败幂等重试，单块传输量低于失联
  阈值），最后一块换 `--after hard_reset` 启动：
  ```bash
  ESPTOOL=~/.platformio/packages/tool-esptoolpy/esptool.py
  python - "$ESPTOOL" <<'EOF'   # 切块：0x10000 起 8×256KB
  import sys, os
  data = open(".pio/build/pda2/firmware.bin","rb").read()
  os.makedirs(".pio/build/pda2/chunks", exist_ok=True)
  for n, off in enumerate(range(0, len(data), 0x40000)):
      open(f".pio/build/pda2/chunks/chunk{n:02d}.bin","wb").write(data[off:off+0x40000])
  EOF
  for i in 00 01 02 03 04 05 06 07; do
    off=$((0x10000 + 16#$i * 0x40000))
    for try in 1 2 3 4 5; do
      python "$ESPTOOL" --chip esp32s3 --port COM5 --baud 115200 \
        --before default_reset --after no_reset \
        write_flash $off .pio/build/pda2/chunks/chunk$i.bin && break
      sleep 3
    done
  done
  ```
  实测 8 块中 2 块各需重试一次，全部写入后设备正常启动。

## 6. 示例功能与依赖总览

| 示例 | 主要功能 | 外部库 |
|---|---|---|
| `test_GPS` | GPS 定位 | TinyGPSPlus |
| `test_sd` | SD 卡读写 | SD / SPI / FS |
| `test_wifi` | WiFi 扫描/连接 | WiFi（内置） |
| `test_bluetooth` | BLE | BLE（内置）、GxEPD2 |
| `test_EPD` | 墨水屏显示 | GxEPD2、Adafruit GFX |
| `test_keypad` | TCA8418 键盘 | Adafruit TCA8418、BusIO |
| `test_touchpad` | CST 触摸 | SensorLib（TouchDrvCSTXXX） |
| `test_BHI260AP` | IMU 气压计 | SensorLib |
| `test_pcm5102a` | 音频播放 | ESP32-audioI2S、SD |
| `test_lora_send` | LoRa 发送 | RadioLib |
| `test_lora_recv` | LoRa 接收 | RadioLib |
| `test_lvgl` | LVGL UI | lvgl、GxEPD2、SensorLib、Adafruit GFX |
| `factory` | 出厂固件（全套硬件自检） | lvgl、GxEPD2、TinyGPSPlus、SensorLib、XPowersLib、BQ27220、RadioLib 等 |
| `pda2` | PDA 应用（计算器/天气/日历/词典/Gemini AI） | factory 全家桶 + WiFi/HTTPClient/WiFiClientSecure/ESP32-audioI2S |

## 7. 常用命令速查

```powershell
$PIO = "C:\Users\asdfo\.platformio\penv\Scripts\pio.exe"

& $PIO run -e T-Deck-Pro                    # 默认 test_GPS
& $PIO run -e factory                       # 编译出厂固件
& $PIO run -e pda2                          # 编译 PDA
& $PIO run -e test_lvgl -e factory -e pda2 --jobs 4   # 并行多编
& $PIO run -e factory -t upload -t monitor  # 烧录 + 监视
& $PIO run -t clean -e factory              # 清理单个环境
```

## 8. 环境备注

> 2026-08-16 更新（当前开发机 = hunter 用户；原文 asdfo 路径属另一台机器，保留备查）

- **PlatformIO**：Core 6.1.19，pip 安装；未加入 PATH，统一用 `python -m platformio <命令>` 调用（如 `python -m platformio run -e pda2`）
- **平台缓存**：`C:\Users\hunter\.platformio`（espressif32@6.5.0 + 工具链 + Arduino 2.0.14 已缓存，首次构建无需再下载）
- **git**：当前机器 git 可用（原文"git 不在 PATH"为另一机器备注）
- **烧录坑（重要）**：后台运行 `pio device monitor` 时 COM 口被占用，`-t upload` 会失败——**烧录前先停监视器，烧完重启**；设备端口通常为 COM5（Espressif 303A:1001）
- **编译前准备**：pda2 需要 `examples/pda2/config_keys.h`（从 `.example` 复制留空即可，gitignored）；分支切换后注意检查
- 首次构建会下载/解包平台与工具链；本机已缓存，之后构建很快

## 9. Build & dev environment（CLAUDE.md 合并）

当前开发机：Windows 11，PlatformIO Core 6.1.19 经 pip 安装，**未加入 PATH**，统一用：

```bash
python -m platformio run -e pda2          # build pda2
python -m platformio run -e factory       # build factory
python -m platformio run -e <env>         # one env per examples/<name>
python -m platformio run -e pda2 -t upload --upload-port COM5
python -m platformio device monitor -p COM5 -b 115200
```

每个 `[env:xxx]` 映射到 `examples/xxx`（经 `script/set_srcdir.py`，env 名 == 目录名；`T-Deck-Pro` → `examples/test_GPS`），`default_envs = T-Deck-Pro`。

**Prerequisites**：
- `examples/pda2/config_keys.h` 必须存在（从 `config_keys.h.example` 复制；gitignored）。空值也可编译。
- `-t upload` 前先停掉后台 `device monitor`，否则 COM5 被占用会导致上传静默失败。

> 注意：前文 §4/§7 里引用的 `C:\Users\asdfo\.platformio\...\pio.exe` 是另一台机器的路径，当前机器不要使用；统一使用上面的 `python -m platformio`。

## 10. 架构（最少需要了解的子系统）

改动下列区域前先读对应文件：

1. **屏幕管理器（`scr_mgr`）** — `examples/pda2/ui_scr_mrg.{h,c}`。每个 app 暴露 `scr_lifecycle_t { create, entry, exit, destroy }`，通过 `scr_mgr_register(SCREEN_ID, ...)` 注册；导航使用 push/pop/switch。页面切换会调用 `keypad_clear_chars()` 清空键盘 FIFO（防止跨屏残留 Backspace）。
2. **键盘驱动** — `examples/pda2/peri_keypad.cpp`。TCA8418 4×10 矩阵，3 层（小写 / Shift 大写 / Sym 锁定）。修饰键状态（双 Shift 逻辑 OR、Alt 临时符号层、Sym 开关）在驱动内部维护。硬件 FIFO → 16 深软件字符 FIFO，由 `keypad_get_val()` 消费。**`INT_STAT` 是 W1C**（写 1 清除，不是读清除；旧代码曾搞反并在溢出时破坏修饰键状态，见 `docs/issue_list.md` §1.5）。
3. **异步 IPC 契约** — `docs/async_ipc_contract.md` 是所有发起 HTTP 请求屏（WiFi Test / Time Sync / AI Test / AI Chat Send）的规范模式。硬规则：结果结构由 worker task `new`，UI 线程 `xQueueReceive` 后 `delete`；busy 标志仅 UI 线程持有并带代次计数；task 拥有所有 UI buffer 的**副本**（不共享 `volatile`/`std::string`）。纯本地屏（Sleep、Keys、GPS）**不适用**该契约。

新增 app 四步清单（`examples/pda2/README.md` §Architecture）：写 `ui_myapp.cpp` 并实现 `scr_lifecycle_t` → 在 `ui_deckpro.h` 加枚举 → 在 `ui_deckpro.cpp` 注册 → 增加一个 `menu_btn`。
