# 评审申请书：c1c6a14..ff6d906 四方评审修复批次

- **申请人**：opencode（Claude 系现场代理）
- **申请日期**：2026-08-26
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个，文件名首末含两端）：
  - `3d2a23d` — `wifi: auto-save slot draft on switch + clear FIFO after popup dismiss`
  - `9b1af11` — `penpal: Cfg provider preview follows dropdown + split save reporting`
  - `9eeaa5a` — `ai/http: ASCII timeout copy, UTF-8 safe truncation, live header log`
- **上游评审**（本批次的触发源，四方对 `c1c6a14..ff6d906`）：
  - [2026-08-25-review-result-c1c6a14..ff6d906-codex.md](2026-08-25-review-result-c1c6a14..ff6d906-codex.md)（C：2×P2）
  - [2026-08-25-review-result-c1c6a14..ff6d906-claude.md](2026-08-25-review-result-c1c6a14..ff6d906-claude.md)（C：P1 + 2×P2）
  - [2026-08-26-review-result-c1c6a14..ff6d906-gemini.md](2026-08-26-review-result-c1c6a14..ff6d906-gemini.md)（A：M1-M3）
  - [wifi-config-keyboard-review-result-c1c6a14..ff6d906-opencode.md](wifi-config-keyboard-review-result-c1c6a14..ff6d906-opencode.md)（C：P2-1 + 4 Low）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，COM5）

---

## 1. 变更明细

### 1.1 WiFi 槽位（`3d2a23d`）

- **Claude P1**：`wifi_cfg_set_slot()` 切换前直接读**两个** textarea 并
  `wifi_slot_save()` 自动保存回旧槽。原 `wifi_cfg_sync_draft()` 只同步
  聚焦框、且结果两行后被 `wifi_slot_load()` 覆盖（死代码）——未保存的
  SSID/Pass 切槽即静默丢失。新语义 = "切换即自动保存"（设计文档 §3.5
  同步更新）。
- **opencode Low-5**：结果弹窗吞键后 `keypad_clear_chars()` 清掉排在
  后面的积压键（先例：`ui_deckpro.cpp:2194` blocking connect 后清队）。

### 1.2 PenPal Cfg（`9b1af11`）

- **Codex/Claude P2-a + Gemini M1**：`pp_cfg_status_text()` 改按
  `s_cfg_provider_idx` 枚举 provider 即时预览（原来重读 NVS，Save 前
  状态行不跟随下拉）；`ai_provider_enum` 返回值检查，custom 走显式分支
  `AI: custom\n(server default model)`。重进屏时下拉先从 NVS 同步
  （`pp_cfg_prefill` 既有逻辑），显示的仍是已保存状态。
- **Codex/Claude P2-b**：`pp_cfg_save_cb()` 分区报告——两步 NVS 写
  部分失败时显示 `server saved; AI provider save failed`，不再把混合
  状态伪装成一次性 `save failed`。
- **opencode Low-2**：provider 焦点下 `\b` 返回 HOME（与空输入框退出
  路径对齐；触摸 Back 不受影响）。

### 1.3 AI / HTTP（`9eeaa5a`）

- **opencode P2-1**：`读取响应超时` → `Empty response (timeout?)`、
  `等待返回超时` → `Request timeout\n(check network)`——montserrat_14
  无 CJK 字形，原中文文案在墨水屏上渲染为方块。
- **opencode Low-1**：`out.resize(200)` 加 UTF-8 码点边界回退
  （penpal `pp_trunc_mark` 同款）；`fail_buf[192]→[240]` 容纳
  203 字符截断回复 + 前缀。
- **opencode Low-3**：`http_post_with_headers` 补 `collectHeaders()`
  （Content-Type / Content-Encoding），`[HTTP] status=` 诊断行不再恒空。

### 1.4 评审分歧处置（issue_list §13 登记）

- **拒绝 Gemini M2**（失实）：`exit4_1` 早已调用 `wifi_cfg_popup_close_cb`
  清理弹窗（`ui_deckpro.cpp:2621`，`c1c6a14` 同 commit 引入）。
- **拒绝 Gemini M3**（重复）：URL 编码限制为设计 §6.3 既有登记。
- **Gemini 对中文超时提示的正面评价不成立**（tofu，见 1.3）。
- **acc3893 Claude Low（根因勘误）**：`create()` 实为每次 `scr_mgr_push`
  执行（`scr_mgr_active`→`scr_mgr_default_style`，`ui_scr_mrg.c:33`），
  非注册期；CLAUDE.md 与 issue_list §10 已订正。无代码影响。

---

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `.venv\Scripts\platformio.exe run -e pda2` SUCCESS（RAM 50.1%，Flash 31.2%） |
| 静态核对 | ✅ | 四份上游结果逐条对照 HEAD 核实（采纳 9 项 / 拒绝 2 项，含 Gemini M2 失实举证） |
| 槽位切换自动保存 | ⏸ | 需真机：槽 2 输入未保存 → 音量键切槽 3 → 切回槽 2 内容在 |
| PenPal provider 预览/分区保存 | ⏸ | 需真机（连同原批次 PenPal provider 共享回归项一并跑） |
| 超时文案英文显示 | ⏸ | 需真机（关热点触发 Test 超时路径） |

---

## 3. 遗留项

1. 真机回归三项（上表 ⏸），与 `c1c6a14..ff6d906` 批次的 PenPal provider
   共享回归（Cfg 下拉 / 保存状态行 / Fix/Polish/Tips 参数）合并执行。
2. `docs/async_ipc_contract.md` 契约层 Medium（weather 任务未纳契约，
   issue_list §11 既有登记，非本轮引入）。

---

## 4. 回滚方案

```bash
git revert 9eeaa5a   # ai/http 文案与日志
git revert 9b1af11   # penpal cfg 预览与保存报告
git revert 3d2a23d   # wifi 槽位切换保存
```

---

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
