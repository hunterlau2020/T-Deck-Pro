# 评审申请书：扫描进度提示层与 Connect 语义（第五次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `1b434a2` — `pda2: add scan progress overlay and connect-then-save semantics`
- **历史文档**（保留不覆盖）：
  - 第一/二轮申请：[`wifi-config-keyboard-review-request.md`](wifi-config-keyboard-review-request.md)
  - 第三轮申请：[`wifi-config-keyboard-review-request-2e559ad-5030566.md`](wifi-config-keyboard-review-request-2e559ad-5030566.md)
  - 第四轮申请：[`wifi-config-keyboard-review-request-6a9ab00-6c51964.md`](wifi-config-keyboard-review-request-6a9ab00-6c51964.md)
  - 评审结果：[`wifi-config-keyboard-review-result.md`](wifi-config-keyboard-review-result.md)（第二轮）、[`wifi-config-keyboard-review-result-2e559ad-5030566.md`](wifi-config-keyboard-review-result-2e559ad-5030566.md)（第三轮）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

第四轮申请提交后，用户真机测试提出两项可用性需求，本轮实现并申请评审：

| # | 用户需求 | 实现 |
|---|---|---|
| 1 | 扫描功能不稳定：扫描期间应有**置顶提示 + 倒计时**，提示显示期间**禁止输入**；扫描完成或倒计时结束提示消失 | 扫描覆盖层（LVGL top layer）：全屏透明可点击容器屏蔽触摸 + 键盘轮询守卫屏蔽按键；带边框的置顶标签每秒更新 "Scanning... Ns"（10 秒倒计时）；扫描完成/失败即隐藏，倒计时耗尽时中止卡住的扫描并隐藏 |
| 2 | Save 按钮改为 **Connect**：尝试连接，**成功才保存** ssid/pass，失败不保存 | 按钮更名 Connect；`wifi_cfg_connect()` 返回连接结果，仅成功时调用 `wifi_cfg_save()` 写 NVS；键盘路径（密码框 Enter）语义一致 |

## 2. 变更明细（`examples/pda2/ui_deckpro.cpp`，commit `1b434a2`）

### 2.1 扫描进度覆盖层

- `wifi_scan_overlay_show()`：在 `lv_layer_top()` 上创建
  - 全屏透明容器（`LV_OBJ_FLAG_CLICKABLE`）——**触摸被吞掉**，扫描期间无法点任何控件
  - 置顶标签（白底、黑边、圆角，`lv_align(TOP_MID, 0, 60)`）初始显示 "Scanning... 10s"
- `wifi_scan_overlay_update()`：挂在 `wifi_cfg_keyboard_poll()` 入口**每 loop 周期**执行
  - 扫描结束（`wifi_scan_state != WIFI_SCAN_RUNNING`）→ 立即隐藏
  - 倒计时耗尽（10s）且扫描仍在跑 → `wifi_cfg_scan_abort()` 中止 + 隐藏
  - 每秒仅在**秒数变化时**刷新文本（避免 EPD 每 loop 重绘）
- **键盘屏蔽**：`wifi_cfg_field == 0` 时 `wifi_scan_state == WIFI_SCAN_RUNNING` 即忽略按键（原逻辑保留，覆盖层期间必然处于扫描中）
- 生命周期：`destroy4_1()` 先隐藏覆盖层再中止扫描，避免 top layer 对象残留到其他屏幕

### 2.2 Connect 语义

- `wifi_cfg_commit()` 重构为 `bool wifi_cfg_connect()`：连接成功返回 true（状态栏 "OK IP: ..."），失败返回 false（"Connect failed/No SSID found/Timeout (码)"）
- `wifi_connect_btn_cb`（原 `wifi_save_btn_cb`）与键盘密码框 Enter 路径统一改为：
  ```cpp
  wifi_cfg_sync_draft();
  if (wifi_cfg_connect()) {
      wifi_cfg_save();        /* persist only on success */
  }
  wifi_cfg_refresh_labels();
  ```
- 连接失败时 NVS 中旧配置**保持不变**（不写入失败凭据）；连接尝试阻塞期间按键被暂存，`wifi_cfg_connect()` 返回前调用 `keypad_clear_chars()` 丢弃，防止"连接期间按的键"在连接后生效
- 按钮文案 Save → **Connect**；屏底提示同步说明（键盘路径 = 密码框 Enter）

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 扫描覆盖层显示/倒计时/消失 | ⏸ 待测 | 触发扫描后应见置顶 "Scanning... Ns" 每秒递减，约 2–4s 后随扫描完成消失 |
| 覆盖层期间输入被屏蔽 | ⏸ 待测 | 提示显示期间按键/触摸均应无效果 |
| 倒计时耗尽处理 | ⏸ 待测 | 可人为观察（正常扫描远短于 10s，耗尽路径需异常环境触发） |
| Connect 成功后保存 | ⏸ 待测 | 用正确凭据 Connect → "OK IP: ..." 且重启后自动连接（NVS 已存） |
| Connect 失败不保存 | ⏸ 待测 | 用错误密码 Connect → 失败文案；重启后不应尝试该错误凭据 |
| 连接期间按键丢弃 | ⏸ 待测 | 连接 15s 内按键，返回后不应有字符注入输入框 |

## 4. 回滚方案

```bash
git revert 1b434a2
```

不涉及 `boards/`、`platformio.ini`、分区表、硬件配置。

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
