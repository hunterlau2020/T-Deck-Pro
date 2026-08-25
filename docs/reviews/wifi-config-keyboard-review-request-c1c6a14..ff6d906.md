# 评审申请书：WiFi Config 5 记忆槽 + PenPal 共享 AI Config provider

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-25
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个）：
  - `c1c6a14` — `pda2: WiFi memory slots, AI config provider fixes, and HTTP robustness`
  - `136069a` — `feat(pda2): share AI Config provider with PenPal`
  - `ff6d906` — `docs: record PenPal AI provider sharing in CLAUDE.md memory`
- **关联设计文档**：
  - `docs/design-wifi-memory-slots.md`
  - `docs/design-penpal-ai-provider-link.md`
- **背景**：用户连续提出两个产品方案。方案 2（WiFi 5 记忆槽）已在前一日实现并硬件实测；方案 1（PenPal 复用 AI Config provider）本日实现。两份设计文档随代码一起提交，现申请专家评审。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，COM5，**已连接**）

---

## 1. 变更明细

### 1.1 WiFi Config 5 记忆槽（`c1c6a14`）

对应设计文档 `docs/design-wifi-memory-slots.md`。

- **NVS 模型**：namespace `"wifi"` 新增 `slot0_ssid`..`slot4_ssid`、`slot0_pass`..`slot4_pass`、`active_slot`、`legacy_migrated`。
- **API**：`ui_deckpro.cpp` 新增 `wifi_slot_load/save/clear/get_active/set_active/migrate_legacy`，并在 `ui_deckpro.h` 暴露给 `factory.ino`。
- **UI 改造**：`create4_1()` 顶行增加槽位切换 `< Slot 1/5 >` 与 `Active` 指示；保留 SSID/Pass 输入框；底部改为 `[Connect] [Save] [Clear]`。
- **键盘交互**：
  - `\v`（音量键）非扫描模式下循环切换槽位。
  - `+/-` 仅在扫描 AP 列表时选择结果。
  - `Enter` 在 Pass 字段触发 Connect。
- **自动连接**：`factory.ino` 开机读取 `active_slot`，空槽跳过；调用 `wifi_slot_migrate_legacy()` 把旧版单组 `ssid`/`pass` 迁移到 slot 0。
- **Connect 行为**：连接成功后将当前草稿保存到当前槽并设为激活槽；失败时在状态行显示原因。

### 1.2 AI Config provider 修复与 HTTP 健壮性（`c1c6a14`）

同 commit 内顺带修复的问题，是后续 PenPal 共享 provider 的前置条件：

- AI Config Save 与 Test 解耦：允许先 Save 再 Test，key 长度下限从原值改为 `>=15`（兼容 deepseek 短 key）。
- 加长 HTTP 超时，真超时时 UI 显示“等待返回超时”。
- 修复 provider 为 deepseek/openrouter 时的响应读取问题，使 Test 能正确返回。

### 1.3 PenPal 共享 AI Config provider（`136069a` + `ff6d906`）

对应设计文档 `docs/design-penpal-ai-provider-link.md`。

- **集中式 provider 注册表**：把 `ui_ai_cfg.cpp` 中的 `s_providers[]` 迁移到 `openai_api.cpp`，并在 `openai_api.h` 暴露 `ai_provider_info_t` 与 `ai_provider_count/enum/find/get()`。
- **AI Config 屏**：改用新的集中式接口；下拉选项从注册表动态构建；最后一项仍为 `custom`。
- **PenPal Cfg 页**：
  - 标签改为 `Server URL` / `Server Key`。
  - 新增 `AI Provider` 下拉（来源 `ai_provider_enum()`，`custom` 表示不指定）。
  - 保存时把 provider 名写入 NVS `penpal:ai_provider`；不重复保存 base/model/key。
  - 状态行显示所选 provider 的 `label / model / key 是否存在`。
  - 键盘 `\t` 循环焦点；provider 焦点下按 `+/-` 切换选项。
- **运行时 Fix/Polish/Tips**：
  - `pp_task_req_t` 新增 `ai_provider` / `ai_model`。
  - `pp_start()` 在 UI 线程调用 `penpal_load_ai_provider()` + `ai_provider_get()` 解析 model，写入任务快照。
  - `penpal_api.cpp` 在 `/emails/<id>/correction|polish|tips` URL 后附加 `?provider=<name>&model=<model>`；provider 为空时不附加，保持服务端兼容。
- **文档与记忆**：更新 `docs/design-penpal-ai-provider-link.md`、`docs/penpal-design.md`，并在 `CLAUDE.md` working notes 中记录。

---

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` SUCCESS（RAM 50.1%，Flash 31.2%） |
| WiFi 记忆槽硬件实测 | ✅ | 已烧录 `c1c6a14`；5 槽切换、Connect 设激活、开机自动连接、legacy 迁移均经用户验证 |
| AI Config Test（openrouter/deepseek）| ✅ | 同 `c1c6a14` 烧录后，openrouter/deepseek Test 均返回正常 |
| PenPal provider 共享硬件实测 | ⏸ | 仅编译通过，尚未烧录；需真机验证 Cfg 下拉、保存后状态行、Fix/Polish/Tips 请求参数 |
| 代码静态检查 | ✅ | 无新增编译警告；`ui_ai_cfg.cpp` 已移除本地 `s_providers[]`，无悬空引用 |

---

## 3. 遗留项

1. **PenPal 服务端支持**：`?provider=&model=` 需要服务端解析；若服务端暂不支持，请求仍可成功，只是忽略参数。
2. **URL 编码**：当前 provider/model 直接拼接到 query 字符串。若未来 model id 出现需编码字符，应补充 `url_encode`。
3. **真机回归**：PenPal provider 共享需要在硬件上走一遍 Fix/Polish/Tips，确认 query 参数被服务端正确接收。

---

## 4. 回滚方案

```bash
git revert ff6d906
git revert 136069a
git revert c1c6a14
```

---

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________  
**审批日期**：________________
