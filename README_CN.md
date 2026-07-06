<h1 align = "center">🏆T-Deck-Pro V1.0🏆</h1>

<p> 
<img src="https://img.shields.io/badge/PlatformIO-6.5.0-ff7f00" height="20px"></a>
<img src="https://img.shields.io/badge/Arduino-2.0.14-008284" height="20px"></a>
</p>

![Build Status](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/actions/workflows/platformio.yml/badge.svg?event=push)

**[English](./readme.md) | 中文**

![alt text](./docs/README_img/image.png)

## :zero: 版本 🎁

T-Deck-Pro 硬件版本

不同硬件版本的代码可能不兼容。请确认你的硬件版本，并切换到对应的 `git branch`。

各硬件版本的差异可在对应的 `readme` 和原理图中查看。

|        名称         |                                                      git 分支                                                      |                                                  变更说明                                                   |                                                      原理图                                                       |                                   固件                                    |     状态     |
| :-----------------: | :------------------------------------------------------------------------------------------------------------------: | :-------------------------------------------------------------------------------------------------------: | :------------------------------------------------------------------------------------------------------------------: | :---------------------------------------------------------------------------: | :-----------: |
|   T-Deck-Pro V1.0   |     [HD-V1-250326](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V1-250326?tab=readme-ov-file#t-deck-pro)     | [readme](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V1-250326?tab=readme-ov-file#zero-version-) |     [SCH](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V1-250326/hardware/T-Deckpro%20v1.0%2024-05-16)       | [V1](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V1-250326/firmware) |   可用   |
|   T-Deck-Pro V1.1   |   [HD-V2-250915](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V2-250915?tab=readme-ov-file#t-deck-pro-v11)   | [readme](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V2-250915?tab=readme-ov-file#zero-version-) |     [SCH](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V2-250915/hardware/T-Deckpro%20v1.1%2025-09-15)       | [V2](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V2-250915/firmware) |   可用   |
|   T-Deck-Pro MAX    |                               [MAX](https://github.com/Xinyuan-LilyGO/T-Deck-Pro-MAX)                                |                 [readme](https://github.com/Xinyuan-LilyGO/T-Deck-Pro-MAX#zero-version-)                  |                     [SCH](https://github.com/Xinyuan-LilyGO/T-Deck-Pro-MAX/tree/master/hardware)                     |  [V3](https://github.com/Xinyuan-LilyGO/T-Deck-Pro-MAX/tree/master/firmware)  |   -   |


注意：

`T-Deck Pro v1.1` 原始触摸芯片供应商 `CST328` 已停止生产，我已将其替换为 `CST3530` 触摸芯片。
触摸面板已由 `CST328` 更换为 `CST3530`。由于两者使用相同的设备地址，因此在初始化过程中需要确定触摸芯片的型号。

由于 `CST3530` 具有自动休眠功能，建议使用中断而非轮询方式；否则可能会出现 I2C 访问错误。我们提供了兼容新旧触控技术的驱动程序，可检测是否为新型触控。[touch_hyn_core](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/HD-V2-250915/examples/touch_hyn_core)
 注意：
- 新：`CST3530` 使用 `cst66xx_fuc` 初始化流程  
- 旧：`CST328` 使用 `cst3xx_fuc` 初始化程序

详情见 [#37](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/issues/37)

🟢🟢🟢

如何区分不同版本的 T-Deck-Pro：

可通过检测设备的 I2C 功能来区分不同版本。

|        名称         | DRV2605 （0x5A） | XL9555 (0x20) |
| :-----------------: | :------------: | :-----------: |
|   T-Deck-Pro V1.0   |       ❌        |       ❌       |
|   T-Deck-Pro V1.1   |       ✅        |       ❌       |
|   T-Deck-Pro MAX    |       ✅        |       ✅       |

下载 [WireScan](./firmware/examples/WireScan.bin) 固件后打开串口进行确认。

如何下载固件？- [点击此处](./firmware/)

### 1、版本说明

T-Deck-Pro 有两个版本，一个搭载音频模块 PCM512A，另一个搭载 4G 模块 A7682E。

如下图所示，两个版本的标注模块有所不同：

![alt text](./docs/README_img/image-1.png)

### 2、购买链接

[LilyGo 官方店铺](https://lilygo.cc/products/t-deck-pro)

## :one: 产品参数 🎁

|       H693       |           T-Deck-Pro           |
| :--------------: | :----------------------------: |
|       主控       |            ESP32-S3            |
|  Flash / PSRAM   |            16M / 8M            |
|       LoRa       |             SX1262             |
|       GPS        |            MIA-M10Q            |
|      显示屏      |      GDEQ031T10 (320x240)      |
|    4G 模块       |       A7682E  🟢可选       |
|      音频        |       PCM512A 🟢可选       |
|    电池容量      |        305070 (1400mAh)        |
|    电池芯片      | BQ25896 (0x6B), BQ27220 (0x55) |
|      触摸        |         CST328 (0x1A)          |
|      陀螺仪      |        BHI260AP (0x28)         |
|      键盘        |         TCA8418 (0x34)         |

🟢🟢🟢

其他与 T-Deck-Pro 相关的项目：

### 1. meshtastic

T-Deck-Pro 支持 Meshtastic，相关固件位于 [firmware/meshtastic](./firmware/meshtastic/)。

已测试的固件版本：
~~~
T-Deck-Pro V1.0  <---  firmware-t-deck-pro-2.7.10.94d4bdf.bin 
T-Deck-Pro V1.1  <---  firmware-t-deck-pro-2.7.13.597fa0b.bin
~~~
如何下载固件？- [点击此处](./firmware/)

关于 `T-Deck-Pro V1.x` 的视频：[在 LILYGO T-Deck Pro 上运行 Meshtastic](https://www.youtube.com/watch?v=qfrOp8PxDvA)

更多 Meshtastic 相关信息：
[github](https://github.com/meshtastic/firmware) 、
[Meshtastic 烧录工具](https://flasher.meshtastic.org/)


### 2. Launcher

适用于 M5Stack、Lilygo、CYDs、Marauder 和 ESP32 设备的应用启动器。

T-Deck-Pro 设备已获得支持。

参考：
[github](https://github.com/bmorcelli/Launcher)、
[Launcher](https://bmorcelli.github.io/Launcher/)

### 3. bb_epaper

一个适用于 Arduino、Linux 或各种无 OS 嵌入式系统的无障碍电子纸库。

作者已添加对 `T-Deck-Pro V1.0` 的支持。

更多信息：
[github](https://github.com/bitbank2/bb_epaper)

## :two: 模块依赖 🎁

芯片数据手册位于 [./hardware](./hardware/) 目录。
~~~
zinggjm/GxEPD2@1.5.5
jgromes/RadioLib@6.4.2
lewisxhe/SensorLib@^0.2.0
mikalhart/TinyGPSPlus @ ^1.0.3
vshymanskyy/TinyGSM@^0.12.0
lvgl/lvgl @ ~8.3.9
lewisxhe/XPowersLib @ ^0.2.4
adafruit/Adafruit TCA8418 @ ^1.0.1
adafruit/Adafruit BusIO @ ^1.14.4
olikraus/U8g2_for_Adafruit_GFX@^1.8.0
adafruit/Adafruit GFX Library@^1.11.10
esphome/ESP32-audioI2S#v3.0.12
~~~

### 1. A7682E

A7682E https://en.simcom.com/product/A7682E.html

使用 [`examples/A7682E/test_AT`](https://github.com/Xinyuan-LilyGO/T-Deck-Pro/tree/master/examples/A7682E/test_AT) 测试 A7682E 功能。

A7682E 是支持 LTE-FDD/GSM/GPRS/EDGE 无线通信模式的 LTE Cat 1 模块，最大下行速率 10Mbps，上行速率 5Mbps，支持多种内置网络协议。

通过 AT 命令控制：
~~~
频段 LTE-FDD B1/B3/B5/B7/B8/B20
GSM/GPRS/EDGE 900/1800 MHz
供电电压 3.4V ~ 4.2V，典型值：3.8V
LTE Cat 1（上行最高 5Mbps，下行最高 10Mbps）
EDGE（上下行最高 236.8Kbps）
GPRS（上下行最高 85.6Kbps）
支持 USB/FOTA 固件升级
支持语音通话
支持收发短信
网络协议（TCP/IP/IPV4/IPV6/Multi-PDP/FTP/FTPS/HTTP/HTTPS/DNS）
RNDIS/PPP/ECM
SSL
~~~

## :three: 快速开始 🎁

🟢 推荐使用 PlatformIO，因为这些示例均基于 PlatformIO 开发。🟢 

### 1、PlatformIO

1. 安装 [Visual Studio Code](https://code.visualstudio.com/) 和 [Python](https://www.python.org/)，并克隆或下载本项目；
2. 在 `VisualStudioCode` 扩展中搜索 `PlatformIO` 插件并安装；
3. 安装完成后需要重启 `VisualStudioCode`；
4. 打开本项目后，PlatformIO 会自动下载所需的第三方库和依赖，首次下载过程较长，请耐心等待；
5. 所有依赖安装完成后，打开 `platformio.ini` 配置文件，在 `example` 中取消注释以选择示例，然后按 `ctrl+s` 保存 `.ini` 配置文件；
6. 点击 VScode 底部的 :ballot_box_with_check: 编译项目，然后插入 USB 并在 VScode 中选择对应 COM 口；
7. 最后点击 :arrow_right: 按钮将程序下载到 Flash；

### 2、Arduino IDE

1. 安装 [Arduino IDE](https://www.arduino.cc/en/software)；

2. 将 `本项目/lib/` 下的所有文件复制，粘贴到 Arduino 库路径下（通常为 `C:\Users\YourName\Documents\Arduino\libraries`）；

3. 打开 Arduino IDE，点击左上角 `文件->打开`，打开 `本项目/example/xxx/xxx.ino` 中的示例；

4. 按照下表配置 Arduino，配置完成后点击 Arduino 左上角按钮进行编译和下载：

| Arduino IDE 设置                     | 值                                 |
| ------------------------------------ | ---------------------------------- |
| Board                                | ***ESP32S3 Dev Module***           |
| Port                                 | 你的端口                           |
| USB CDC On Boot                      | Enable                             |
| CPU Frequency                        | 240MHZ(WiFi)                       |
| Core Debug Level                     | None                               |
| USB DFU On Boot                      | Disable                            |
| Erase All Flash Before Sketch Upload | Disable                            |
| Events Run On                        | Core1                              |
| Flash Mode                           | QIO 80MHZ                          |
| Flash Size                           | **16MB(128Mb)**                    |
| Arduino Runs On                      | Core1                              |
| USB Firmware MSC On Boot             | Disable                            |
| Partition Scheme                     | **16M Flash(3M APP/9.9MB FATFS)**  |
| PSRAM                                | **OPI PSRAM**                      |
| Upload Mode                          | **UART0/Hardware CDC**             |
| Upload Speed                         | 921600                             |
| USB Mode                             | **CDC and JTAG**                   |

3. 目录结构：
~~~
├─boards  : 板卡信息，用于 platformio.ini 配置项目；
├─docs    : 存放相关文档；
├─data    : 程序使用的图片资源；
├─example : 示例程序；
├─firmare : `出厂` 编译固件；
├─hardware: 板卡原理图、芯片资料；
├─lib     : 项目使用的库；
~~~

## :four: 引脚定义 🎁

~~~c
// 串口
#define SerialMon   Serial      // 
#define SerialAT    Serial1     // 
#define SerialGPS   Serial2     // 

// IIC 地址
#define BOARD_I2C_ADDR_TOUCH      0x1A // 触摸        --- CST328
#define BOARD_I2C_ADDR_LTR_553ALS 0x23 // 光线传感器  --- LTR_553ALS
#define BOARD_I2C_ADDR_GYROSCOPDE 0x28 // 陀螺仪      --- BHI260AP
#define BOARD_I2C_ADDR_KEYBOARD   0x34 // 键盘        --- TCA8418
#define BOARD_I2C_ADDR_BQ27220    0x55 // BQ27220
#define BOARD_I2C_ADDR_BQ25896    0x6B // BQ25896

// IIC
#define BOARD_I2C_SDA  13
#define BOARD_I2C_SCL  14

// 键盘
#define BOARD_KEYBOARD_SCL BOARD_I2C_SCL
#define BOARD_KEYBOARD_SDA BOARD_I2C_SDA
#define BOARD_KEYBOARD_INT 15
#define BOARD_KEYBOARD_LED 42

// 触摸
#define BOARD_TOUCH_SCL BOARD_I2C_SCL
#define BOARD_TOUCH_SDA BOARD_I2C_SDA
#define BOARD_TOUCH_INT 12
#define BOARD_TOUCH_RST 45

// LTR-553ALS-WA 光线传感器
#define BOARD_ALS_SCL BOARD_I2C_SCL
#define BOARD_ALS_SDA BOARD_I2C_SDA
#define BOARD_ALS_INT 16

// 陀螺仪
#define BOARD_GYROSCOPDE_SCL BOARD_I2C_SCL
#define BOARD_GYROSCOPDE_SDA BOARD_I2C_SDA
#define BOARD_GYROSCOPDE_INT 21
#define BOARD_GYROSCOPDE_RST -1

// SPI
#define BOARD_SPI_SCK  36
#define BOARD_SPI_MOSI 33
#define BOARD_SPI_MISO 47

// 显示屏
#define LCD_HOR_SIZE 240
#define LCD_VER_SIZE 320
#define DISP_BUF_SIZE (LCD_HOR_SIZE * LCD_VER_SIZE)

#define BOARD_EPD_SCK  BOARD_SPI_SCK
#define BOARD_EPD_MOSI BOARD_SPI_MOSI
#define BOARD_EPD_DC   35
#define BOARD_EPD_CS   34
#define BOARD_EPD_BUSY 37
#define BOARD_EPD_RST  -1

// SD 卡
#define BOARD_SD_CS   48
#define BOARD_SD_SCK  BOARD_SPI_SCK
#define BOARD_SD_MOSI BOARD_SPI_MOSI
#define BOARD_SD_MISO BOARD_SPI_MISO

// Lora
#define BOARD_LORA_SCK  BOARD_SPI_SCK
#define BOARD_LORA_MOSI BOARD_SPI_MOSI
#define BOARD_LORA_MISO BOARD_SPI_MISO
#define BOARD_LORA_CS   3
#define BOARD_LORA_BUSY 6
#define BOARD_LORA_RST  4
#define BOARD_LORA_INT  5

// GPS
#define BOARD_GPS_RXD 44
#define BOARD_GPS_TXD 43
#define BOARD_GPS_PPS 1

// A7682E 模块
#define BOARD_A7682E_RI     7
#define BOARD_A7682E_ITR    8
#define BOARD_A7682E_RST    9
#define BOARD_A7682E_RXD    10
#define BOARD_A7682E_TXD    11
#define BOARD_A7682E_PWRKEY 40

// PCM5102A
#define BOARD_I2S_BCLK 7
#define BOARD_I2S_DOUT 8
#define BOARD_I2S_LRC 9

// Boot 引脚
#define BOARD_BOOT_PIN  0

// 马达引脚
#define BOARD_MOTOR_PIN 2

// 使能引脚
#define BOARD_GPS_EN  39  // 使能 GPS 模块
#define BOARD_1V8_EN  38  // 使能陀螺仪模块
#define BOARD_6609_EN 41  // 使能 7682 模块
#define BOARD_LORA_EN 46  // 使能 LORA 模块

// 麦克风
#define BOARD_MIC_DATA        17
#define BOARD_MIC_CLOCK       18
// -------------------------------------------------
~~~

## :five: 测试 🎁

休眠功耗。

![alt text](./docs/README_img/image-2.png)

## :six: 常见问题 🎁

Q：屏幕显示超时，即使下载出厂固件后屏幕仍无法显示。断开电池，等待约 10 秒后重新连接，能否解决此问题？

A：这可能是由于屏幕的 `rst` 引脚未连接到硬件，导致屏幕无法复位。可以尝试下载 [firmware/H693_factory_xxxxx_fix.bin](./firmware/) 来解决此问题。


## :seven: 原理图 & 3D 🎁

更多信息请查看 `./hardware` 目录。
