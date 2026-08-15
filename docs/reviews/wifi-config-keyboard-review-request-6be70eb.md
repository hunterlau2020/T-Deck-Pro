# 评审申请书：AI 配置屏重构 + 状态栏时间刷新周期（第十三次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `6be70eb` — `pda2: rework AI config screen - per-field boxes, Save/Test buttons`
- **历史文档**（保留不覆盖）：前十二轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户真机反馈 AI 配置屏 4 项问题 + 状态栏刷新周期优化，本轮整改并申请评审：

| # | 用户反馈 | 整改 |
|---|---|---|
| a | Model/Key 没有对应输入框 | 每个字段独立输入框：Base 多行 / Model 单行 / Key 单行 |
| b | Base 旁边的 label 与输入框内容重复；URL 太长修改不便 | label 只显示字段名 + 激活标记（不再重复显示值）；Base 输入框改**多行**（52px，可整行查看长 URL） |
| c | Key 输入框有"写死"的默认值 | 排查结论：该值来自 **NVS 持久化配置**（此前测试保存的 Key），源码无硬编码；行为保留（进入屏幕加载已存配置），无代码改动 |
| d | 底部加"保存""测试"按钮；测试请求 `GET https://openrouter.ai/api/v1/models?limit=2` 带 `Bearer <KEY>`，把 `data[0].id` 显示到屏幕 | Save/Test 按钮已加；Test 在 FreeRTOS 任务中异步执行（不冻结 UI），cJSON 解析 `data[0].id` 显示在状态行 + 串口日志 |
| 5 | 状态栏时间不用每秒检查，跟电量一起刷新 | 时间刷新移入 `sec % 10 == 0` 分支（与电量同周期，10s） |

## 2. 变更明细

- `ui_ai_cfg.cpp` 重构：三输入框布局 + 草稿保留（`ai_cfg_sync_draft`/`ai_cfg_refresh_labels` 拆分，与 WiFi 配置屏同范式）+ 触摸焦点同步（`LV_EVENT_FOCUSED`）+ Save/Test 按钮 + 异步测试任务（8KB 栈，结果经 ready 标志由 `ai_cfg_keyboard_poll` 收取，屏幕非激活时丢弃）
- `http_utils.h/.cpp`：新增 `http_get_auth(url, auth_header, timeout)`（带 Authorization 头的 HTTPS GET，沿用时间校验/CA 验证/错误透出链路；desktop stub 同步）
- `ui_deckpro.cpp`：状态栏时间检查从每秒移入电量刷新周期（10s）
- 键盘语义：`\n` 提交当前字段跳下一字段（末字段保存）；`\b` 删字/回上一字段/首字段退出；`\t`/`\v` 忽略

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| 三输入框显示与编辑 | ⏸ 待测 | Base 多行可看全 URL；Model/Key 各自可编辑 |
| Save 按钮 | ⏸ 待测 | 点击后状态行 "Saved"，NVS 生效 |
| Test 按钮 | ⏸ 待测 | 有效 Key → 状态行 "Test OK: <data[0].id>"；无 Key → "Key empty"；失败 → HTTP 码 |
| 状态栏时间 10s 周期 | ⏸ 待测 | 分钟变化后最迟 10s 内更新 |

## 4. 回滚方案

```bash
git revert 6be70eb
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
