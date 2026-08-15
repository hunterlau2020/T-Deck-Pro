# 评审申请书：CA 信任库补全与 NTP 时间校验（第八次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `23942f6` — `pda2: fix WiFi Test TLS failure - complete CA bundle + NTP time check`
- **历史文档**（保留不覆盖）：前七轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户真机测试 WiFi Test（ifconfig.me）返回 "Request failed, HTTP -1"。串口日志定位：

```
[WiFi] connected ip=192.168.3.204
[E][WiFiClientSecure.cpp:144] connect(): start_ssl_client: -8576   ← MBEDTLS_ERR_X509_CERT_VERIFY_FAILED
[WiFiTest] request failed code=-1
```

两项根因，均已修复：

1. **CA bundle 不完整**：固件实际只内置 **ISRG Root X1** 一个根（头注释声称 3 个，代码只写 1 个）。`openssl s_client -connect ifconfig.me:443` 抓链显示该站走 **Let's Encrypt 2026 新层级 YR1 中间证书 ← ISRG Root YR**（2026-05-13 生效的新根，跨签回 X1）。仅信任 X1 时验证失败。
2. **系统时间可能未同步**：NTP 在 CN 网络可达性差；时间停在 1970 时证书 `notBefore` 校验同样报 -8576，难以与"信任库缺根"区分。

## 2. 变更明细

### 2.1 CA bundle 补全（`http_utils.cpp`）

`CA_BUNDLE` 由 1 个根扩充为 4 个（WiFiClientSecure 接受串联 PEM）：

| 根 | 覆盖 | 来源 |
|---|---|---|
| ISRG Root X1 | Let's Encrypt 既有层级 | 原有 |
| **ISRG Root YR**（跨签版，签发者 X1） | Let's Encrypt 2026 层级（ifconfig.me 等） | ifconfig.me 实际链第 3 张（openssl 抓链提取） |
| **DigiCert Global Root G2** | Cloudflare Universal SSL 链 | cacerts.digicert.com 官方 |
| **GlobalSign Root CA - R3** | 商业端点（注：Mozilla 已移除 R1，故用 R3） | curl.se Mozilla bundle（2026-08-13 版） |

### 2.2 HTTPS 前时间校验 + NTP 重试（`http_utils.cpp`）

- 新增 `http_ensure_time(5000)`：`time() > 1700000000`（2023-11-14 之后）视为已同步；未同步则 `configTzTime(...)` 重试 NTP（**cn.pool.ntp.org 优先**，pool.ntp.org / time.nist.gov 兜底），最多等 5s
- `http_get` / `http_post` / `http_post_large` 在 CA_VERIFY 模式下先调 `http_ensure_time()`；失败返回 `status_code=-3` + `error="Time not synced - retry after NTP"`（INSECURE 模式不受影响，跳过校验）
- 串口日志 `[HTTP] system time not synced - requesting NTP` / `NTP retry ok|failed (epoch=...)`

### 2.3 错误详情透出

- `http_response_t` 新增 `string error` 字段（http_utils.h）；连接/请求失败时经 `WiFiClientSecure::lastError()` 填充 mbedtls 具体原因（如 `X509 - Certificate verification failed`），无 TLS 错误时回退 `errorToString`
- WiFi Test 信息层失败文案由 "HTTP -1" 改为展示 `resp.error` 实际原因

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 根证书有效性 | ✅ 已验证 | openssl 逐张校验 subject/dates；YR 为 ifconfig.me 实际链提取 |
| WiFi Test 真机通过 | ⏸ 待测 | 设备点击 WIFI Test → 应显示公网 IP；失败则弹具体原因 |
| NTP 时间校验路径 | ⏸ 待测 | 断网重启后点 WiFi Test → 应显示 "Time not synced" 而非 -8576 |

## 4. 回滚方案

```bash
git revert 23942f6
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
