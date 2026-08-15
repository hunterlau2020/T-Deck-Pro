# 评审申请书：AI Test msgbox 反馈（第十五次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `d4ccf28` — `pda2: AI Test feedback as msgbox with countdown and Close`
- **历史文档**（保留不覆盖）：前十四轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户反馈：点击 AI Config 的 Test 按钮"没有反应"。串口日志证实按钮回调**已触发**（`[AICfg] Test button clicked`，上一轮底部钉住修复生效），但反馈只写在一条细小的灰色状态行上，且校验失败（如未连 WiFi）时无醒目提示。用户要求：点击后弹出 **msgbox** 显示"正在请求，倒计时 10s..."，获取到数据则替换 msgbox 内容，msgbox 带 Close 按钮。

## 2. 变更明细（`examples/pda2/ui_ai_cfg.cpp`，commit `d4ccf28`）

- **msgbox 组件**：top layer 居中弹窗（220×160，白底黑边）＝自动换行正文 + **Close 按钮**；`ai_msgbox_show(text)` / `ai_msgbox_set_text(text)` / `ai_msgbox_close_cb`
- **Test 点击流程**：字段校验（WiFi/Base/Model/Key）失败时**在 msgbox 中显示原因**（不再只有状态行）；通过后弹 `"Testing... 10s"` 并启动异步测试任务（沿用既有 FreeRTOS 任务 + 结果轮询，UI 不冻结）
- **倒计时**：msgbox 打开且任务进行中，`ai_cfg_keyboard_poll` 每秒更新 `Testing... Ns`（**仅秒数变化时改文本**，避免墨水屏每 loop 重绘）；10s 未出结果则显示 "Request timeout (check network)"（任务 15s 超时，迟到结果仍可替换内容）
- **结果替换内容**：成功 → `Test OK: <data[0].id>`；失败 → `Test fail: HTTP <码> + 错误详情`；JSON 异常 → `Test fail: bad JSON`——均替换 msgbox 正文，Close 按钮保留
- **输入屏蔽**：msgbox 打开期间键盘字符被消费并丢弃（防止在弹窗后面误编辑）；Close 或退出屏幕即恢复；`ai_cfg_destroy` 销毁弹窗防 top layer 残留

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| Test 点击弹 msgbox + 倒计时 | ⏸ 待测 | 应见 "Testing... 10s→9s→..." 每秒递减 |
| 结果替换内容 + Close | ⏸ 待测 | 成功后显示模型 ID；Close 关闭 |
| 缺字段/断网提示 | ⏸ 待测 | msgbox 显示对应原因 |

## 4. 回滚方案

```bash
git revert d4ccf28
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
