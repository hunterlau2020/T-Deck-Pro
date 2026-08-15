# 评审申请书：ifconfig.me 请求携带 curl User-Agent（第十次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `8001ff1` — `pda2: send curl User-Agent to ifconfig.me in WiFi Test`
- **历史文档**（保留不覆盖）：前九轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户要求：请求 ifconfig.me 时设置 `User-Agent: curl/8.5.0`，避免服务端按未知/浏览器 UA 返回 HTML 格式输出（curl UA 返回纯文本公网 IP）。

## 2. 变更明细

- `http_utils.h/.cpp`：新增 `http_get_ua(url, user_agent, timeout_ms)`——与 `http_get` 同逻辑（时间校验、CA bundle、错误透出），额外在 `http.addHeader("User-Agent", user_agent)`；`http_get` 改为 `http_get_ua(url, NULL, ...)` 的薄封装，既有调用方零改动；desktop stub 同步补充
- `ui_deckpro.cpp::wifi_test_run`：改用 `http_get_ua("https://ifconfig.me/", "curl/8.5.0", 15000)`；响应体做尾部空白裁剪（`find_last_not_of(" \t\r\n")`）后再拼 "Public IP:\n<ip>" 展示

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| WiFi Test 返回纯文本 IP | ⏸ 待测 | 信息层应显示 "Public IP: x.x.x.x" 而非 HTML |

## 4. 回滚方案

```bash
git revert 8001ff1
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
