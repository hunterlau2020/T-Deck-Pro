# 评审申请书：扫描/连接结果反馈与 WiFi 连通性测试（第六次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `be92a82` — `pda2: add scan/connect result feedback and WiFi Test button`
- **历史文档**（保留不覆盖）：
  - [`wifi-config-keyboard-review-request.md`](wifi-config-keyboard-review-request.md)（第一/二轮）
  - [`wifi-config-keyboard-review-request-2e559ad-5030566.md`](wifi-config-keyboard-review-request-2e559ad-5030566.md)（第三轮）
  - [`wifi-config-keyboard-review-request-6a9ab00-6c51964.md`](wifi-config-keyboard-review-request-6a9ab00-6c51964.md)（第四轮）
  - [`wifi-config-keyboard-review-request-1b434a2.md`](wifi-config-keyboard-review-request-1b434a2.md)（第五轮）
  - 评审结果：[`wifi-config-keyboard-review-result.md`](wifi-config-keyboard-review-result.md)、[`wifi-config-keyboard-review-result-2e559ad-5030566.md`](wifi-config-keyboard-review-result-2e559ad-5030566.md)
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

第五轮申请提交后，用户真机测试反馈两项问题，本轮实现并申请评审：

| # | 用户反馈 | 实现 |
|---|---|---|
| 1 | 扫描提示在倒计时结束前消失，但 SSID 未被填入输入框——无法判断扫描究竟成功还是失败 | ① 扫描/连接**全生命周期串口日志**（`[WiFi]` 标签：start/failed/done/N found/each SSID/connected ip/connect failed st）；② **结果横幅**（置顶、3 秒自动消失、不阻塞输入）："Scan: N found" / "Scan: none found" / "Scan failed" / "Scan start failed" / "Scan timeout"——扫描成功与否一目了然；串口日志（上一版测试抓取）显示设备此前自动连接 HONOR-60 后 ASSOC_LEAVE，提示早消失的原因是扫描快速完成（0 个网络/失败），此前无任何可见反馈导致误判 |
| 2 | 点击 Connect 后如何知道连接是否成功 | 连接结果横幅："Connected! IP: xxx" / "Connect failed" + 串口日志 `[WiFi] connected ip=...` / `[WiFi] connect failed st=...`；状态栏文案保留 |
| 3 | 在 Wifi Scan 页面（4.2）增加 WiFi 测试按钮，访问 https://ifconfig.me，返回信息用信息层展示，带关闭按钮 | 4.2 屏新增 **WiFi Test (ifconfig.me)** 按钮：先 `http_require_wifi()` 检查连通性，按钮文字变为 "Testing..."，`http_get("https://ifconfig.me/", 15000)` 请求；成功弹**信息层**（top layer 弹窗：标题 "WiFi Test OK" + "Public IP: <公网IP>" 自动换行 + **Close 按钮**）；失败弹信息层显示 "Request failed / HTTP <code>"；退出屏幕时弹窗自动销毁；串口打印 `[WiFiTest]` 日志 |

## 2. 变更明细（`examples/pda2/ui_deckpro.cpp`，commit `be92a82`）

### 2.1 结果横幅（屏幕 4.1）

- `wifi_banner_show(text)` / `wifi_banner_hide()` / `wifi_banner_update()`：置顶（`lv_layer_top()`）白底黑边标签，`WIFI_BANNER_MS = 3000` 自动隐藏；**不阻塞输入**（无全屏点击容器），扫描完成后用户可立即用 `+`/`-` 选择候选
- 触发点：扫描完成（N found / none / failed / start fail / timeout）、连接成功/失败
- 横幅与扫描覆盖层互斥：覆盖层仅在扫描进行中显示，横幅仅在出结果后显示
- 生命周期：`destroy4_1()` 隐藏横幅与覆盖层，不残留到其他屏幕

### 2.2 串口日志（诊断闭环）

- 扫描：`[WiFi] scan started (async)` / `scan start failed r=N` / `scan failed r=N` / `scan[0..n] <SSID>`（逐个结果）/ `scan done: no networks found` / `scan results dropped (superseded)`
- 连接：`[WiFi] connected ip=...` / `connect failed st=N (reason)`

### 2.3 WiFi Test 按钮（屏幕 4.2）

- 复用既有 `http_utils`（内置 CA bundle：ISRG Root X1 + DigiCert Global Root G2 + GlobalSign Root R1，默认 CA 验证）
- 信息层结构：220×250 居中弹窗（top layer）：标题标签 + 自动换行正文标签（flex_grow 占满剩余空间）+ 全宽 Close 按钮
- 请求阻塞（≤15s）前将按钮文字置 "Testing..." 并 `lv_timer_handler()` 刷新墨水屏
- 弹窗销毁：Close 按钮点击 / `destroy4_2()` 退出屏幕

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 扫描结果横幅三态 | ⏸ 待测 | 有网 → "Scan: N found" + 框内填入首个 SSID；无网 → "Scan: none found"；失败 → "Scan failed" |
| 扫描串口日志 | ⏸ 待测 | 监视器应见 `[WiFi] scan started` → `scan[i] <SSID>` 序列 |
| 连接结果横幅 + 日志 | ⏸ 待测 | 成功 "Connected! IP: ..."；失败 "Connect failed" |
| WiFi Test 按钮 | ⏸ 待测 | 点击 → "Testing..." → 信息层显示公网 IP + Close 关闭；断网时 `http_require_wifi` 提示 |

## 4. 回滚方案

```bash
git revert be92a82
```

不涉及 `boards/`、`platformio.ini`、分区表、硬件配置。

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
