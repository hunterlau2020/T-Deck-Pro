# 评审申请：v1.22–v1.23 批次——评审收口 + Voice AI 稳定性/滚动 + PenPal 修复链（4d27849..bf0aa72）

> **取代** `session-batch-review-request-4d27849..d68289e.md`（2026-09-26
> 版，无任何评审结果到达）——该申请发出后同一会话又落了 PenPal 方向/
> 缓存修复批次，按提交人决定合并为一份重新申请。旧申请文件保留并标注
> 取代，评审方只需处理本文件。

- **申请人**：Claude（ZCode 代理）；**申请日期**：2026-09-27
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **关联 commit**（评审 9 个；另有 2 个纯文档/构建不评，见归属表）：
  - `4d27849` — 71c09e7 收口：OTA 清单 4KB 封顶（qwen P3-2）+ Whoami
    忙句柄 UI 线程所有（Nit-1）+ `docs/ota-baseline.md`
  - `0fd2043` — Voice AI：MIC-during-TTS 崩溃修复 + <1s 录音筛选 +
    堆诊断日志 + 任务栈 16K→12K
  - `a37face` — Voice AI 对话区可滚动（触摸 redraw-on-release + `+/-` 键
    + 4000 字符显示帽；删死代码分页）
  - `d68289e` — PenPal COMPOSE 草稿放弃确认框
  - `67acddf` — PenPal 数据排查工具（服务器 mailbox/thread 直查、
    SPIFFS 缓存代际扫描）
  - `2760655` — PenPal 缓存 v2：绑定服务器 base + 封堵读/写侧时钟洞
  - `77e0634` — 信件方向按**本人 user id** 判定（真实用户笔友修复）
  - `4ca5874` — 本人 id **NVS 持久化** + 缓存 v3（base+key 双绑定）
  - `bf0aa72` — 未读线程打开时自动后台刷新
- **版本**：v1.21 → v1.22（`8d82043`）→ **v1.23**（本申请合并时补升，
  对应 PenPal 修复链；`fw_version.h` 同步）
- **硬件**：三台全部烧录至 `bf0aa72`（esptool 4/4 分区哈希校验）。
  carlos 机（`…18:5c`）为主验证机：方向判定/缓存迁移/自动刷新真机过。

## 头部自校

- `git rev-list --count c531d66..bf0aa72` = **11**（9 评审 + 2 文档）
- `git diff --name-only c531d66..bf0aa72` = **19 文件**（下表全覆盖）

### 区间文件归属表

| 文件 | 归属 | 内容 |
|---|---|---|
| `ota_update.cpp` | **评** | 4d27849 清单 4KB 封顶 |
| `ui_whoami.cpp` | **评** | 4d27849 忙句柄；4ca5874 uid 持久化/换 key 清除 |
| `ui_voice_ai.cpp` | **评** | 0fd2043 崩溃修复/筛选/堆日志/12K 栈；a37face 滚动 |
| `ui_penpal.cpp` | **评** | d68289e 键盘分支；2760655 stale 刷新；bf0aa72 自动刷新 + uid 懒加载 |
| `ui_penpal.h` / `ui_penpal_write.cpp` | **评** | d68289e 确认框 |
| `penpal_api.cpp` / `penpal_api.h` | **评** | 2760655 缓存 v2；77e0634 方向判定；4ca5874 uid NVS + 缓存 v3 |
| `openai_api.cpp` | **评** | 0fd2043 堆诊断日志 |
| `fw_version.h` | 评（低） | 1.21→1.22→1.23 + 版本史 |
| `docs/ota-baseline.md` | 评（文档） | WDT T 依据 |
| `scripts/{inspect_mailbox,scan_spiffs_json,scan_mailbox_gen,whoami_check,mb_check}.py` | 评（工具） | 一次性排查工具，含 key/gate-pin 常量 |
| `CHANGELOG.md` / `platformio.ini` / 旧申请文件 | 不评 | 时点记录 / 乱码清理 / 本申请取代注记 |

## §1 变更明细

### §1.1 4d27849（评审收口）— 承旧申请 §1.1

P3-2 清单封顶（`getSize` 预检 ∈(0,4096]）；Nit-1 句柄 UI-owned（worker
阻塞投递）；ota-baseline.md（T=60s=2×T_min，库实证 `_busy_timeout`=10s）。

### §1.2 0fd2043（Voice AI）— 承旧申请 §1.2

崩溃链：TTS 播放中按 MIC → `pdm_init` 卸载 I2S0 → `audio.loop()` 写死
驱动 → LoadProhibited（设备日志 + addr2line 实证）。修复 = 先
`stopSong()`。筛选 `VAI_REC_MIN_MS 1000`（录音器自身 700ms 早停门 ⇒
误碰必落筛选区）。堆日志三值（free/largest_int/psram）。

### §1.3 a37face / d68289e — 承旧申请 §1.3/§1.4

滚动容器 + redraw-on-release；草稿确认框（SEND 飞行直退语义不变）。

### §1.4 PenPal 数据修复链（2760655 → 77e0634 → 4ca5874，真机驱动的三步收敛）

**起点**（用户报告 + 分区取证）：detail 全显 `To:`；SPIFFS 扫出**跨代
缓存**（旧库 pal3="hunter" vs 现库 pal4="carlos"；更早 pal7=terry 时代）。

1. **缓存 v2**（2760655）：头 `<fetched> <base>`；base 变化即失效；
   读侧时钟洞（NTP 前 TTL 检查被短路 → 永远"新鲜"）改**命中仍发起网络
   刷新**（stale_clock 标志）；写侧洞（fetched=0 永不过期）改为同步后
   判过期。
2. **方向判定**（77e0634）：旧规则"sender_user_id 非零=我写的"只在 NPC
   时代成立；真实用户笔友的**来信**带对方非零 id → 全被误判 mine。改为
   `== 本人id`（profile `id` 字段，实测设备账号=carlos）。
3. **uid 持久化 + 缓存 v3**（4ca5874)：发现 RAM uid 有时序洞——先进
   PenPal 后进 Whoami 时 uid 仍为 0 → 回退旧规则照错。NVS `my_uid`
   持久化（Whoami 拉到即存、PenPal 进入即载、Cfg 换 key 即清）；缓存头
   v3 `<fetched> <base> <key>`——**同域名换账号/服务端重建库**也失效
   （v2 只防换域名，本例域未变库变了，盲区实证）。v1/v2 头自动作废。

**真机收口**：三台刷 bf0aa72 后 carlos 机验证方向正确（hello=From:
hunter 等）；旧缓存迁移 v3 完成。

### §1.5 bf0aa72（自动刷新）

`pp_home_row_cb` 缓存命中渲染后，行 `unread>0` → 复用页内 Sync 的
PP_RES_THREAD 请求后台刷新（消费分支原地重渲染）；无未读=纯缓存；
cache-miss 照旧走网络。

## §2 已知取舍与"最没把握"（≤3）

1. **12K 栈未打水印**（承旧申请）——同链路 PenPal 8K 先例。
2. **缓存 v3 的 key 明文入 SPIFFS**：缓存头带 key（16 字符），SPIFFS
   分区转储即可见。威胁模型内可接受（转储需物理接触，env.cfg 本就同
   分区同 Key 存 key）；替代方案（key 哈希）多一层间接收益有限，提请
   评审裁定是否改。
3. **j_mine 回退分支仍存在**（uid 未知时"非零=mine"）：仅在全新刷机 +
   从未拉过 profile + 直接进 PenPal 看线程的组合下可达（cache-miss 打
   开走网络前也会先 parse？——不会，parse 在 worker；真链路是 cache
   命中才 parse 旧方向）。可接受窗口，评审如认为应改为"uid 未知时不
   显示方向"请提。

## §3 验证状态与遗留

- 编译零错误（RAM 57.7% / Flash 44.2%）；三台真机刷入 + 哈希校验。
- 真机已验：方向判定（carlos 机，多线程核对）、缓存 v1→v3 自动迁移、
  未读自动刷新、TTS-MIC 崩溃不复现、滚动/草稿确认（用户使用窗口）。
- 遗留（跨轮）：自毁固件回滚真测（需局域网台架 + `OTA_ALLOW_PLAINTEXT`
  台架版）、双机 WDT 仪器化校准、BHI260AP/SPIFFS 段补喂狗点、SEND
  飞行退出链路回归。

## §4 审批请求

按 §2.3 代码口径；P1/P2 留 `file:line` + 反例；结果写
`docs/reviews/session-batch-review-result-4d27849..bf0aa72-<reviewer>.md`
（新建文件绝不覆盖）。
