# OTA 基线数据（docs/ota-update-design.md §5.3 / §7.3）

> 目的：为 boot WDT 自证窗口的 T 值提供实测依据（评审 qwen P2-2）。
> 状态：**首版基线**——`_busy_timeout` 为库源码实证，分段耗时来自
> 2026-09-15 三台机器启动串口日志（带 `[ms]` 时间戳）；仪器化双机复测
> （每段独立打点）与"健康固件无 TWDT 误复位"确认仍属 G-真机项（见文末）。

## 1. 喂狗间隔清单（T 的下界来源）

| 段 | 喂狗点 | 最长不可喂狗间隔 | 数据来源 |
|---|---|---|---|
| **EPD 单页刷写** | 页间（`boot_wdt_feed()` 于 `nextPage()` 循环体内） | **`_busy_timeout` = 10.0 s**（库内 `_waitWhileBusy` 不可插喂狗） | 库源码实证：`lib/GxEPD2/src/gdeq/GxEPD2_310_GDEQ031T10.cpp:17` 传参 `10000000`（µs）；`GxEPD2_EPD.cpp:149` 以该值上界等待 BUSY |
| A7682E `testAT` 重试循环 | 每轮（`factory.ino` `boot_wdt_feed()`） | ~1 s/轮（`testAT(1000)`） | 设计稿 §1 实测 8.4 s 总段长 ÷ 重试结构 |
| GPS `getAck` / L76K 握手 | 循环体内每拍 | 0.8 s / 0.5 s 窗口 | `peri_gps.cpp`（两处喂狗点） |
| SD 挂载/探测 | 挂载前后 | 慢卡实测未超 2 s（本批设备均无卡/快速 FAT32） | 启动日志 `sd_diskio` 段 |
| PCM5102A WAV 写入 | 每 16384 帧一次 | 块级毫秒 | `factory.ino` `pcm5102a_init()` |
| SPIFFS 挂载/格式化 | **无喂狗点**（Grok IC-6 指出） | 实测 < 1 s（日志 `[ 26.0s] SPIFFS Already Mounted!`）；首刷 format 路径未单独计时 | 启动日志，标注为**待补** |

## 2. 实测分段耗时（V1.1 机 `10:20:BA:34:19:EC`，2026-09-15 启动日志）

| 段 | 起止（boot 后 ms） | 时长 |
|---|---|---|
| I2C 扫描 + 外设枚举 | ~1310 → ~1340 | < 0.1 s |
| LoRa SX1262 初始化 | PERI 段内 | ~1 s |
| BHI260AP 固件上传（103676 B） | 43807 → 53848 | **10.0 s**（段内毫秒级连续，无喂狗点——见 §4 待办） |
| A7682E init（无 SIM，6×testAT + 重试） | 26000 → 43000 | **8.4 s**（每轮已喂） |
| 触摸 hyn_init | 63900 → 64070 | 0.2 s |
| 首帧 EPD 全刷（V1.1 路径） | disp_flush:0 | ~0.5 s（正常路径无 timeout） |

V1.0（机 `28:37:2f`，EPD soft-only 兼容路径）单页 Busy Timeout 实测
**恰好 10 s 后打印 "Busy Timeout!" 继续**——与 `_busy_timeout=10s` 一致，
即面板假死（issue_list §18）时最坏情况也就是 10 s/页,不突破 T。

## 3. T 值核算

```
实测最长不可喂狗间隔 = EPD 单页 _busy_timeout = 10 s（库常量,非抽样）
T_min = max(实测最长 × 2, 30 s 下限) = max(20, 30) = 30 s
当前 T = 60 s = 2 × T_min
```

**结论**：当前 `OTA_BOOT_WDT_T_S 60` 满足 §5.3 约束,对已知最长间隔有
6× 余量、对 T_min 有 2× 余量。BHI260AP 上传段（10 s,无喂狗点）是除
EPD 外唯一逼近 T/4 的段,已列入 §4 补点清单。

## 4. 遗留（G-真机）

- [ ] BHI260AP 上传段与 SPIFFS 首刷 format 段补喂狗点（一行改动,防御
      慢路径；当前实测均 <10 s 不触发）。
- [ ] 仪器化双机复测：两台各跑一次带分段打点的启动,确认无 TWDT 复位
      （§8.2"健康固件正向"）。
- [ ] 自毁固件回滚真机验证（§8.1 行 5;`verifyRollbackLater` 已修,该项
      是 P1 闭合的收尾）。
