# test_i2s_probe — 音频通路探针

## 目的

回答一个硬件问题：**这块板子的 3.5mm 耳机孔到底有没有接 PCM5102A DAC？**

背景：T-Deck Pro 的 4G（A7682E）版本与非 4G 版本引脚互斥——
I2S（BCLK=7/DOUT=8/LRC=9）与 A7682E 的 RI(7)/ITR(8)/RST(9) 重合，
`test_pcm5102a` 示例注释 "If it is the 4G version, please ignore this test
example" 即源于此。

## 用法

```bash
pio run -e test_i2s_probe
pio run -e test_i2s_probe -t uploadfs --upload-port COM5   # data/1-off.mp3 -> SPIFFS
pio run -e test_i2s_probe -t upload --upload-port COM5
```

烧录后自动播放 SPIFFS `/1-off.mp3`（约 1 秒提示音），插耳机听。

## 实测结果（2026-08-19，HD-V2 4G 批次）

| 环节 | 结果 |
|---|---|
| SPIFFS 读取 / ID3 解析 / MP3 解码 | ✅ 完整走通（串口日志：syncword → 44100Hz → EOF） |
| I2S 外设配置 | ✅ `setPinout` 成功 |
| **耳机孔出声** | ❌ **无声** |

**结论**：4G 版板上无 PCM5102A DAC 芯片（耳机孔旁丝印 "QM-H693 GSM-V1.3" 为 4G
模组）；ESP32-S3 无内置 DAC，I2S 数字信号无处转换。**MP3 播放屏在此硬件上不可行**
（见 `docs/issue_list.md` §3.3）。本示例保留作为硬件验证记录。
