# WiFi Test 入口位置调整评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-c34164e.md](wifi-config-keyboard-review-request-c34164e.md)
- **关联 commit**：`c34164e`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 WiFi Test 仍请求网页根路径，成功响应不是公网 IP

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:1443-1450`
- **触发场景**：WiFi 已连接，在 WIFI 列表页点击 `- WIFI Test`。
- **证据**：
  - 请求地址仍为 `https://ifconfig.me/`。
  - ESP32 `HTTPClient` 默认 User-Agent 为 `ESP32HTTPClient`；该端点会对其返回 HTML 页面，而不是纯 IP 文本。
  - 代码只检查 HTTP 200 和非空正文，随后把完整正文拼入 `Public IP`。
- **影响**：测试会把 HTML 页面误报为成功的公网 IP，并带来不必要的网络流量、堆内存和 LVGL 文本布局负担。
- **最小修复**：改用 `https://ifconfig.me/ip`，去除首尾空白，并校验正文为合法 IPv4 或 IPv6 后再显示成功。

### 1.2 断网时点击 WiFi Test 仍没有用户反馈

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:1437-1440`、`examples/pda2/http_utils.cpp:85-88`
- **触发场景**：设备未连接 WiFi 时点击 `- WIFI Test`。
- **证据**：
  - `wifi_test_run()` 在 `http_require_wifi()` 返回 false 后直接返回。
  - `http_require_wifi()` 实际只返回连接状态，不会履行头文件所述的“show popup if disconnected”契约。
- **影响**：列表项无状态变化、无横幅、无弹窗，用户无法判断点击是否生效。
- **最小修复**：调用方显示 `"WiFi not connected"` 信息层，或修复 `http_require_wifi()` 的统一提示契约。

### 1.3 同步 HTTP 请求冻结 UI，“请求中退出页面”保护实际不可触发

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:1441-1445`
- **触发场景**：公网不可达、TLS 握手缓慢或服务端直到 15 秒超时才返回。
- **证据**：
  - `http_get()` 直接在 LVGL 点击事件回调中同步执行。
  - 请求返回前，同一 UI 线程无法处理返回按钮或页面切换事件。
  - 因此请求后的 `if (!wifi_test_active)` 不能实现申请书所述的“请求中退出页面后丢弃结果”；正常用户输入无法在阻塞期间改变该标志。
- **影响**：界面最长约 15 秒无响应，用户无法关闭 `Testing...` 信息层或离开 WIFI 页；申请书中的对应验证项不可按当前线程模型完成。
- **最小修复**：在工作任务中执行 HTTP 请求，通过线程安全消息把结果送回 LVGL 线程；完成时再检查页面代次或活动标志后更新信息层。

## 2. 通过项

- WiFi Test 已从 Wifi Scan 屏移除，4.2 屏恢复为扫描结果列表。
- WIFI 列表页按 `Config`、`Scan`、`Test` 的顺序创建第三个列表项。
- 点击事件能精确匹配 `- WIFI Test` 并调用测试逻辑。
- 正常离开或销毁 WIFI 页时会关闭已有信息层。

## 3. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

入口位置调整可以保留；重新申请前应修复纯 IP 端点、断网反馈和同步请求冻结 UI 三项问题。
