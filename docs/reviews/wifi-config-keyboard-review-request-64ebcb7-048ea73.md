# 评审申请书：第 4-7 轮 Findings 整改 + 时间同步按钮（第十一次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `64ebcb7` — `pda2: flush hardware key FIFO on screen transitions`（第4轮1.1 + 第5轮1.1）
  - `048ea73` — `pda2: SCAN_DONE event sync, async WiFi Test, Time Sync button`（第4轮1.2、第6/7轮1.1/1.2/1.3 + 用户新增需求）
- **历史文档**（保留不覆盖）：前十一轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

按第 4-7 轮评审结论（均退回修订）完成全部 Findings 整改，并新增用户要求的"时间同步"按钮，重新申请。

## 2. 整改明细

### 2.1 硬件 FIFO 页面边界（第4轮 1.1 + 第5轮 1.1，commit `64ebcb7`）

- `keypad_clear_chars()` 升级：除清空软件字符 FIFO 外，**同时 `keypad.flush()` 排空 TCA8418 硬件 FIFO**——芯片内积压的旧按键不会在下一轮 `keypad_loop()` 重新入队注入新页面
- 瞬时修饰键（Alt/双 Shift）一并重置：按下/释放事件对被一起丢弃，不会卡层
- **产品语义（文档化）**：Sym **锁**跨页面保留（用户主动拨动的状态，非瞬时）；评审要求"Sym 锁是否跨页面保留应作为独立产品语义处理"——本申请明确声明此语义

### 2.2 SCAN_DONE 事件同步中止（第4轮 1.2 High，commit `048ea73`）

- 注册 `WiFi.onEvent(wifi_scan_done_cb, ARDUINO_EVENT_WIFI_SCAN_DONE)`（`create4_1` 首次创建时）
- 依据框架 `WiFiGenericClass::_eventCallback` 源码顺序：内部 `WiFiScanClass::_scanDone()`（结果分配+填充）**先于**用户回调执行——**回调触发即证明 `_scanDone` 已完成**，随后 `scanDelete()` 无竞态
- `wifi_cfg_scan_abort()`：清事件标志 → `esp_wifi_scan_stop()` → 等事件（≤3s）→ `scanDelete()`；**超时则推迟释放**（不再无条件释放；下次 `scanNetworks()` 开头自带 `scanDelete()`，无永久泄漏），并打日志

### 2.3 WiFi Test 异步化（第7轮 1.3，commit `048ea73`）

- HTTP 请求移入 **FreeRTOS 任务**（8KB 栈），UI 线程用 **LVGL 定时器（100ms）轮询结果**——请求期间 UI 完全可响应，"Testing..." 信息层可关闭、可离页（结果按 `wifi_test_active` 丢弃）
- 任务单飞守卫（重复点击忽略）、创建失败弹窗提示

### 2.4 WiFi Test 端点与校验（第6/7轮 1.1，commit `048ea73`）

- 端点改为 **`https://ifconfig.me/ip`**（纯文本 IP，非 HTML 页面）+ 保留 `curl/8.5.0` UA（双保险）
- 响应去首尾空白后经 **`inet_pton` 校验为合法 IPv4/IPv6** 才显示成功；否则弹 "Unexpected response (not an IP address)"

### 2.5 断网反馈（第6/7轮 1.2，commit `048ea73`）

- 点 WiFi Test / Time Sync 时未联网 → 信息层明确显示 "WiFi not connected / configure it first"
- `http_require_wifi()` 头注释修正为"仅返回状态，反馈由调用方负责"（契约与实现一致）

### 2.6 时间同步按钮（用户新增需求，commit `048ea73`）

- WIFI 页新增第四个列表项 **"- Time Sync"**（位于 WIFI Test 下方）
- 点击：异步 NTP 同步（FreeRTOS 任务 + 定时器轮询，UI 不冻结；cn.pool.ntp.org 优先，≤10s），信息层显示：
  - 成功：`Before: YYYY-MM-DD HH:MM:SS / After: YYYY-MM-DD HH:MM:SS`（CST-8 本地时间）
  - 失败：`Sync failed / before=<epoch> after=<epoch>`
- 与连网自动校时（`d8f0ab7`）互补：手动按钮供用户主动校准

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 页面切换后无残留按键 | ⏸ 待测 | 快速按键后触摸切页，新页面不应收到旧按键；串口 `[KBD] char fifo cleared (screen switch, hw flushed N)` |
| 扫描中退出 | ⏸ 待测 | 串口无竞态日志；退出后 4.2 可立即扫描 |
| WiFi Test | ⏸ 待测 | 信息层显示纯文本 IP；断网点按钮显示 "WiFi not connected" |
| 时间同步按钮 | ⏸ 待测 | 信息层显示同步前后时间对比（CST-8） |

## 4. 回滚方案

```bash
git revert 048ea73      # WiFi 侧
git revert 64ebcb7      # 键盘侧
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 两 commit 保留，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
