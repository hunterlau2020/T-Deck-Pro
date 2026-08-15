# 评审申请书：WiFi Test 入口位置调整（第七次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `c34164e` — `pda2: move WiFi Test from Wifi Scan screen to the WIFI page`
- **历史文档**（保留不覆盖）：第一至六轮申请与结果文档见 `docs/reviews/`（以 `wifi-config-keyboard-review-request*.md` / `wifi-config-keyboard-review-result*.md` 命名）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

第六轮实现的 WiFi Test 按钮位置放错（用户反馈）：原放在 **Wifi Scan 屏幕（4.2）内部**，正确位置应为 **WIFI 列表页（屏幕 4）中 "WIFI Scan" 条目下方**，作为第三个列表项。本轮调整并申请评审。

## 2. 变更明细（`examples/pda2/ui_deckpro.cpp`，commit `c34164e`）

- **移除** 屏幕 4.2 内的 WiFi Test 按钮及弹窗代码（4.2 屏恢复为纯扫描结果列表）
- **新增** 屏幕 4 列表第三项 `"- WIFI Test"`（`scr4_item_create("- WIFI Test", scr4_list_event)`，位于 "- WIFI Config"、"- WIFI Scan" 之下）；`scr4_list_event` 按标签匹配后直接执行 `wifi_test_run()`，不进入独立屏幕
- `wifi_test_run()`：`http_require_wifi()` 预检 → 弹信息层显示 "Testing..." → `http_get("https://ifconfig.me/", 15000)` → 成功弹 "WiFi Test OK / Public IP: ..."，失败弹 "Request failed / HTTP <code>"；信息层带 **Close 按钮**
- 生命周期：新增 `wifi_test_active` 标志（`entry4` 置位、`exit4`/`destroy4` 清零并关闭弹窗）；阻塞请求返回后若用户已离开 WIFI 页则丢弃结果不弹窗，避免信息层残留到其他屏幕
- 串口 `[WiFiTest]` 日志保留

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| WIFI 页第三个列表项 | ⏸ 待测 | WIFI 页应显示 Config / Scan / **Test** 三项 |
| 点击 Test → 信息层 | ⏸ 待测 | "Testing..." → 公网 IP 信息层 + Close 关闭 |
| 请求中退出页面 | ⏸ 待测 | 退出 WIFI 页后不应出现残留信息层 |
| Wifi Scan 屏无按钮 | ⏸ 待测 | 4.2 屏仅扫描列表，无 Test 按钮 |

## 4. 回滚方案

```bash
git revert c34164e
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
