# 评审申请：v1.22 批次——71c09e7 收口 + Voice AI 稳定性/滚动 + PenPal 草稿保护（4d27849..d68289e）

> **⚠ 已取代（2026-09-27）**：本申请发出后无评审结果到达，同会话又落
> 了 PenPal 方向/缓存修复批次（2760655/77e0634/4ca5874/bf0aa72）。
> 请评审**合并后的新申请**
> [`session-batch-review-request-4d27849..bf0aa72.md`](session-batch-review-request-4d27849..bf0aa72.md)
> （覆盖本申请全部内容 + 新增 §1.4/§1.5）。本文件保留存档，勿单独评审。

- **申请人**：Claude（ZCode 代理）；**申请日期**：2026-09-26
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **关联 commit**（5 个，首末含两端）：
  - `4d27849` — 71c09e7 收口：OTA 清单 4KB 封顶（qwen P3-2）+ Whoami
    忙句柄 UI 线程所有（Nit-1）+ `docs/ota-baseline.md`（WDT T 校准首版）
  - `01ba2e0` — platformio.ini 注释乱码清理（**纯文档，无构建变更**）
  - `0fd2043` — Voice AI：MIC-during-TTS 崩溃修复 + <1s 录音筛选 +
    堆诊断日志 + 任务栈 16K→12K
  - `a37face` — Voice AI 对话区可滚动（触摸 redraw-on-release + `+/-`
    键 + 4000 字符显示帽；删死代码分页）
  - `d68289e` — PenPal COMPOSE 草稿放弃确认框
- **背景**：上批 `71c09e7` 四方评审（Claude/GPT/Grok C、Qwen A）的
  代码级发现已由 `095e41a`（P1+P2×4）与本批 `4d27849`（P3-2/Nit-1）
  全部闭合；本轮同时包含 9-26 真机故障驱动的 Voice AI 修复与两个
  用户需求功能。**版本 v1.21 → v1.22**。
- **硬件**：机 `10:20:ba:34:18:5c`（V1.1，COM5）已烧录本批构建
  （v1.22，编译零警告级通过、4/4 分区哈希校验）；机 `28:37:2f`（V1.0）
  与 `10:20:ba:34:19:ec`（V1.1）仍为 v1.21。

## 头部自校

- `git rev-list --count c531d66..d68289e` = **5**
- `git diff --name-only c531d66..d68289e`：
  `ota_update.cpp` / `ui_whoami.cpp` / `ui_voice_ai.cpp` / `ui_penpal.cpp`
  / `ui_penpal.h` / `ui_penpal_write.cpp` / `fw_version.h`（**7 代码文件**）
  + `docs/ota-baseline.md`（新）/ `CHANGELOG.md` / `platformio.ini`
  （3 文档/构建）= **10 文件**

### 区间文件归属表

| 文件 | 归属 | 内容 |
|---|---|---|
| `examples/pda2/ota_update.cpp` | **评** | 4d27849 清单 4KB 封顶 |
| `examples/pda2/ui_whoami.cpp` | **评** | 4d27849 忙句柄 UI-owned |
| `examples/pda2/ui_voice_ai.cpp` | **评** | 0fd2043 崩溃修复/筛选/堆日志/12K 栈；a37face 滚动重构 |
| `examples/pda2/ui_penpal.cpp` | **评** | d68289e 键盘分支（leave 对话框按键） |
| `examples/pda2/ui_penpal.h` / `ui_penpal_write.cpp` | **评** | d68289e 确认框 + over-lays 清理 |
| `examples/pda2/fw_version.h` | 评（低） | 1.21→1.22 + 版本史补记 |
| `docs/ota-baseline.md` | 评（文档） | T=60s 依据（库实证 + 启动日志） |
| `CHANGELOG.md` / `platformio.ini` | 不评 | 时点记录 / 注释乱码（无构建变更） |

## §1 变更明细与自评

### §1.1 4d27849（评审收口）

- **P3-2**：`ota_check_task` 在 `getString()` 前校验
  `Content-Length ∈ (0, 4096]`，否则 "manifest size missing/oversize"
  拒绝（`ota_update.cpp:354-364`）。
- **Nit-1**：`s_wa_task` 只在 `wa_consume`（UI 线程）清；worker 投递改
  **阻塞直到入队**（队列每拍排水，实际不阻塞）——消除"投递失败滞留
  busy"路径，且跨线程写句柄彻底消失（`ui_whoami.cpp:291-303`）。
- **ota-baseline.md**：T=60s = 2×T_min(30s)；最长不可喂狗间隔 =
  EPD 单页 `_busy_timeout`=10s（库常量实证）；BHI260AP 上传段
  （10s，无喂狗点）与 SPIFFS 首刷 format 已列为补点待办。

### §1.2 0fd2043（Voice AI 崩溃修复 + 筛选 + 诊断）

- **崩溃修复**：设备日志 + addr2line 回溯——`audio.loop() → i2s_write`
  写已被 `pdm_init()` 卸载的 I2S0（TTS 朗读中按 MIC）。修复 =
  `start_voice_record`/`start_tts` 检 `tts_playing` 时先
  `audio.stopSong()` + `ui_disp_suppress_flush(false)` + 清标志。
- **过短筛选**：`VAI_REC_MIN_MS 1000`；`dur_ms = (wav_len-44)/32`
  （16kHz/16bit/mono）；低于阈值 free(wav) + 屏显
  `[Voice too short (Nms) - not sent]`，**不触 ASR/服务器**。录音器
  自身 700ms 早停门 ⇒ 误碰必落筛选区。
- **堆诊断**：`openai_chat_impl` POST 前打印
  `free / largest_int / psram`（SSL alloc-failure 归因用；健康基线
  实测 free≈106K / largest_int≈59K）。
- **栈 16K→12K**：同链路 PenPal worker 8K 先例；语音任务额外承载
  ASR multipart（堆上）+ hex 解码（堆上），栈型与 PenPal 相当。

### §1.3 a37face（滚动）

- `resp_cont`（可滚动容器，LV_DIR_VER）包住 transcript label；
  `LV_EVENT_SCROLL_BEGIN/END` → 触摸滚动抑制 EPD + 抬手
  `ui_disp_full_refr()`（AI Text 同款）；`chat_append` 显示
  chat_history **尾部 4000 字符**（UTF-8 续字节回退）并滚底；
  `+`/`-`（空输入框）`lv_obj_scroll_by ±120`（indev==NULL 正常刷）。
- 删除死代码分页（`show_response_page` 无调用方）。

### §1.4 d68289e（PenPal 草稿确认）

- COMPOSE 返回（触摸标题栏 back）时：title/body **任一非空** → 弹
  "Abandon this draft?"（Abandon=清空+HOME / Keep=留下）；键盘
  Enter=放弃、任意键=保留（`ppw_leave_open/key` 在
  `penpal_keyboard_poll` 的 msgbox 分支之前消费）；空框直退；
  **`pp.send_lock`（SEND 飞行）维持直退**——清框会破坏
  `ppw_payload_eq` 幂等对比；`ppw_overlays_close` 关框=保留。

## §2 已知取舍与"最没把握"（≤3）

1. **12K 栈未打水印**：语音任务 TLS 峰值栈未实测（依据 = PenPal 8K
   同链路先例 + 崩溃前 16K 无 canary 记录）。残余风险：ASR+chat 全链
   峰值若 >12K 会 canary 重启——复现即回滚 16K（一行）。
2. **滚动抑制与 append 竞态**：触摸滚动抑制期间 worker 追加消息
   （`chat_append` 内 `scroll_to_y`，programmatic 无 indev）会正常
   flush 一帧——后果是抬手前多一次重绘，非正确性问题；未真机压测。
3. **SEND 飞行直退路径**：`send_lock` 期退出不清框的设计依赖后台
   发送 + 幂等键语义（§3.2 既有），本轮未真机复测"飞行中退出 →
   后台成功 → 重进 COMPOSE"链路（上轮 ⏸ 项延续）。

## §3 验证状态与遗留

- 编译：v1.22 构建零错误（RAM 57.8% / Flash 44.2%）；机 `…18:5c`
  已烧录（esptool 哈希校验过）。
- 真机已验：TTS 中按 MIC 修复后未再复现崩溃（用户使用窗口）；
  过短筛选 + 滚动 + 草稿确认**待用户回归**（本轮刷录后）。
- 遗留（跨轮，非本批引入）：① 自毁固件回滚真测（层 2 唯一未触发
  路径）；② 双机仪器化 WDT 校准；③ BHI260AP/SPIFFS 段补喂狗点；
  ④ SEND 飞行退出链路回归。
- 自毁测试台架前置：`OTA_ALLOW_PLAINTEXT` 台架固件 + 局域网
  manifest 服务（`scripts/ota_sign.py`）。

## §4 审批请求

按 §2.3 代码口径评审；P1/P2 请留 `file:line` + 反例坐标；结果写
`docs/reviews/session-batch-review-result-4d27849..d68289e-<reviewer>.md`
（新建文件绝不覆盖）。
