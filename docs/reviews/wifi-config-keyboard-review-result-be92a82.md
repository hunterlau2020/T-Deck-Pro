# 扫描/连接结果反馈与 WiFi 连通性测试评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-be92a82.md](wifi-config-keyboard-review-request-be92a82.md)
- **关联 commit**：`be92a82`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 WiFi Test 请求根路径，对 ESP32 客户端返回整页 HTML 而非公网 IP

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:2146-2153`
- **触发场景**：WiFi 已连接，点击 `WiFi Test (ifconfig.me)`。
- **证据**：
  - 请求地址为 `https://ifconfig.me/`。
  - ESP32 `HTTPClient` 默认 User-Agent 为 `ESP32HTTPClient`。
  - 该端点对该 User-Agent 返回约 10 KB 的 HTML 页面且 HTTP 状态为 200；代码只检查 200 和非空正文，并将整个正文标记为 `Public IP`。
- **影响**：弹窗显示 HTML 而不是 IP 地址，同时产生不必要的网络、堆内存和 LVGL 文本布局负担。
- **最小修复**：改用 `https://ifconfig.me/ip`，去除首尾空白，并校验正文为合法 IPv4 或 IPv6 后再显示成功。

### 1.2 断网时点击 WiFi Test 没有任何用户反馈

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:2139-2142`、`examples/pda2/http_utils.cpp:85-88`
- **触发场景**：设备未连接 WiFi 时点击测试按钮。
- **证据**：
  - `wifi_test_btn_cb()` 在 `http_require_wifi()` 返回 false 后直接返回。
  - `http_require_wifi()` 实际只返回连接状态，不会像头文件注释所述显示 popup。
- **影响**：按钮无状态变化、无横幅、无弹窗，用户无法判断点击是否生效；申请书所述“断网时提示”未实现。
- **最小修复**：调用方在断网时显示 `"WiFi not connected"` 信息层，或修复 `http_require_wifi()` 使其履行统一提示契约。

## 2. 通过项

- 扫描和连接结果横幅不阻塞后续输入，并在页面销毁时清理。
- 扫描与连接生命周期日志覆盖启动、结果和失败状态。
- 正常完成的扫描会显示结果数量并填入首个候选 SSID。
- WiFi Test 弹窗具有关闭按钮并在 4.2 页面销毁时清理。

## 3. 继承风险

本提交未解决前序提交中的扫描中止回调竞态及连接期间硬件 FIFO 残留问题，这些风险仍存在于该提交快照。

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

重新申请前应改用纯 IP 端点并补齐断网反馈，同时完成实际公网 IP 格式校验。
