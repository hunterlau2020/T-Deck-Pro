# 评审申请书：OpenRouter 根证书、连网自动校时与时区修正（第九次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `d8f0ab7` — `pda2: add GTS Root R4, NTP sync after connect, CST-8 timezone`
- **历史文档**（保留不覆盖）：前八轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户提出两项需求 + 现场观察到一个时钟问题，本轮一并处理：

| # | 需求/问题 | 实现 |
|---|---|---|
| 1 | 后续要用到的 openrouter.ai 的根证书提前补入 | 抓链确认 openrouter.ai 当前链为 `openrouter.ai ← GTS WE1 ← GTS Root R4`（原设计文档记录的 GTS CA 1C3/R3 链已过时）；bundle 新增 **GTS Root R4**（自签版，Mozilla bundle 2026-08-13 提取，ECC P-256，2016-06-22 → 2036-06-22） |
| 2 | WiFi 连接成功后自动做一次时间校准 | `wifi_time_sync()`：连接成功即触发 NTP（cn.pool.ntp.org 优先，≤8s 等待，期间 `lv_timer_handler` 刷屏），串口打印 `[WiFi] time sync ok/pending (epoch=...)`；此后 TLS 证书校验有时钟基础 |
| 3 | 硬件时钟显示错误 | 根因：固件沿用时区 **PST8PDT**（美国太平洋，继承自出厂固件）——NTP 同步的 epoch 正确，但本地时间偏 16 小时。**无独立时间设置功能**：三处 `configTzTime` 统一改为 **CST-8**（中国标准时 UTC+8，无 DST），配合"连网自动校时"即可自动正确显示，无需手工设置 |

## 2. 变更明细

- `examples/pda2/http_utils.cpp`：CA bundle 追加 GTS Root R4（PEM 完整、openssl 已验证）；`http_ensure_time` 时区改 CST-8
- `examples/pda2/ui_deckpro.cpp`：新增 `wifi_time_sync()`，在 `wifi_cfg_connect()` 成功分支（横幅之前）调用；时区 CST-8
- `examples/pda2/factory.ino`：开机 `configTzTime` 改为 CST-8 + 三个 NTP 服务器（cn.pool.ntp.org 优先）

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| GTS R4 证书有效性 | ✅ 已验证 | openssl 校验 subject/issuer/dates（自签根） |
| 连网后自动校时 | ⏸ 待测 | Connect 成功后串口应见 `[WiFi] time sync ok (epoch=1755...)` |
| 本地时钟正确 | ⏸ 待测 | 校时后界面显示的本地时间应为北京时间（UTC+8） |
| openrouter.ai HTTPS | ⏸ 待测 | 需在 AI 屏或 WiFi Test 配置 OpenRouter 后实测（当前 WiFi Test 仅测 ifconfig.me） |

## 4. 回滚方案

```bash
git revert d8f0ab7
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
