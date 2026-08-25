# 评审结论：c1c6a14..ff6d906 — WiFi 5 记忆槽 + PenPal AI Config Provider 共享

- **评审人**：Antigravity（Claude Sonnet 4.6 Thinking）
- **评审日期**：2026-08-26
- **评审申请**：[wifi-config-keyboard-review-request-c1c6a14..ff6d906.md](wifi-config-keyboard-review-request-c1c6a14..ff6d906.md)
- **关联 commit**（3 个）：
  - `c1c6a14` — `pda2: WiFi memory slots, AI config provider fixes, and HTTP robustness`
  - `136069a` — `feat(pda2): share AI Config provider with PenPal`
  - `ff6d906` — `docs: record PenPal AI provider sharing in CLAUDE.md memory`
- **关联设计文档**：`docs/design-wifi-memory-slots.md`、`docs/design-penpal-ai-provider-link.md`

---

## 结论：**A 全量接受**（含 3 项低严重度遗留 M1-M3、1 项待真机验证 M4）

| 子项 | 结论 |
|---|---|
| WiFi 5 记忆槽（硬件已验证） | ✅ 接受 |
| AI Config 修复（Save/Test 解耦、超时、response fix） | ✅ 接受 |
| HTTP 健壮性（OpenRouter headers、Accept-Encoding: identity） | ✅ 接受 |
| PenPal 共享 AI Provider（`136069a`） | ✅ 接受（M1/M3 低严重度遗留；M4 待真机回归） |
| CLAUDE.md 记忆（`ff6d906`） | ✅ 接受（文档） |

---

## 1. WiFi Config 5 记忆槽（`c1c6a14`，已硬件实测）

### NVS 模型

- namespace `"wifi"`，键名 `slot{N}_ssid`/`slot{N}_pass`（N = 0..4）、`active_slot`、`legacy_migrated`，与旧 `ssid`/`pass` 键隔离，设计清晰。

### Legacy 迁移

- `wifi_slot_migrate_legacy()` 先检查 `legacy_migrated` flag，避免重复迁移；先写 slot 0 数据、删除旧键，再写 flag（先数据后标记），顺序合理，不会在断电时产生双份数据。

### 槽位读写

- `wifi_slot_get_active()` 读取后有范围守卫（`< 0 || >= WIFI_SLOT_COUNT → 0`），防止 NVS 损坏值。
- `wifi_slot_load()` 内部同样有范围守卫，与上层守卫一致。

### UI 交互

- `wifi_cfg_set_slot()` 切换槽前调 `wifi_cfg_sync_draft()` 保存草稿，正确（防止未保存草稿丢失）。
- Connect 成功：`wifi_cfg_save()` 持久化 → `wifi_slot_set_active()` 设激活 → `wifi_cfg_show_msgbox()` 弹窗；失败同样弹窗显示原因，用户体验完整。
- `factory.ino` 开机：`migrate_legacy → get_active → slot_load → WiFi.begin`，空槽（`ssid[0] == '\0'`）跳过，流程简洁正确。

### 遗留项 M2（低）

`wifi_cfg_popup` 弹框挂在 `lv_layer_top()`，若用户在弹框开着时按 Back 退出 WiFi Config 屏，popup 会孤立于 top 层（现有 exit/destroy 回调未显式清理）。建议：在 WiFi Config 屏的 exit 或 destroy 回调中加 `wifi_cfg_popup_close_cb(NULL)`。不影响当前稳定性（用户可通过弹框上的 Close 按钮关闭），故不阻止接受。

---

## 2. AI Config provider 修复与 HTTP 健壮性（`c1c6a14`）

- **Save/Test 解耦**：允许先 Save 后 Test，key 长度下限改为 ≥15（兼容 deepseek 短 key）——合理，解除了先 Test 才能 Save 的不必要约束。
- **HTTP 超时延长 + 超时提示**：45s 超时 + "等待返回超时"中文提示，用户体验改善。
- **`Accept-Encoding: identity` + `HTTP/1.0`**：解决 ESP32 HTTPClient 与 DeepSeek 的 body-read 兼容问题，已验证有效。
- **OpenRouter attribution headers**（`HTTP-Referer`、`X-OpenRouter-Title`）：符合 OpenRouter API 最佳实践，无安全风险。

---

## 3. PenPal 共享 AI Config Provider（`136069a`，仅编译验证，尚未真机）

### 集中式注册表架构

- `s_providers[]` 从 `ui_ai_cfg.cpp` 迁移至 `openai_api.cpp`，通过 `ai_provider_count/enum/find/get()` 暴露只读接口。
- `ui_ai_cfg.cpp` 和 `ui_penpal.cpp` 均改用新接口，无悬空引用，静态检查通过。

### `ai_provider_get()` 实现

- AI Config 槽 `base_url` 匹配时优先用用户自定义（base/model/key 全套）。
- 不匹配时用注册表默认值 + NVS provider-specific key（`key.{name}`）→ env.cfg 环境变量 → 编译期 dev key（仅 openrouter）→ 空的回退链，逻辑完整，无死路。

### UI 线程解析（正确）

- `pp_start()` 在 UI 线程调 `penpal_load_ai_provider()` + `ai_provider_get()`，解析结果写入任务快照 `rq->ai_provider/ai_model`。
- worker 任务不再访问 NVS/Preferences——符合 Preferences 非线程安全约束，与 AI Chat/AI Config 既有模式一致。

### URL 构建

- `pp_llm_path()` 仅在 provider/model 非空时附加 `?provider=…&model=…`；provider 为空（custom）时不附加，服务端使用默认，向后兼容。

### 遗留项 M1（低）

`pp_cfg_status_text()` 中调用 `ai_provider_enum(ai_provider_find(name), &p)` 但未检查返回值：若 `name` 不在注册表（custom/空），`ai_provider_find` 返回 -1，`ai_provider_enum(-1, &p)` 返回 false 且不写 `p`，后续 `p.label`/`p.model` 读取未初始化值。该函数仅用于 UI 状态栏显示（不影响请求逻辑），最坏情况是显示乱码，不崩溃。建议：增加 `idx >= 0` 检查，`custom` 走单独显示分支（如 `"AI: custom (server default)"`）。

### 遗留项 M3（低）

`pp_llm_path()` 中 provider/model 直接拼接到 query 字符串，未做 percent-encode。当前注册表内 provider 名为纯 ASCII（`openrouter`/`deepseek` 等），model 名（如 `deepseek/deepseek-v4-flash-0731`）含 `/`，但 `/` 在 query value 中是合法字符（RFC 3986），无需编码。当前注册表内无 `+`/`&`/`=`/`#` 等需编码字符，实际安全。若未来追加 model 名含上述字符，需补充 `url_encode`。申请书已登记此项，知悉即可。

### 遗留项 M4（待真机验证）

PenPal provider 共享功能仅编译验证，尚未烧录真机回归。需在硬件上走一遍：
1. PenPal Cfg 页 AI Provider 下拉显示正常
2. 选择 provider 保存后状态行显示 `AI: <label> / <model> / key: <exists/missing>`
3. Fix/Polish/Tips 请求携带 `?provider=…&model=…` 参数被服务端正确接收

---

## 4. 遗留项汇总

| ID | 严重度 | 位置 | 描述 | 建议 |
|---|---|---|---|---|
| M1 | Low | `ui_penpal.cpp` `pp_cfg_status_text()` | `ai_provider_find` 返回 -1 时 `p.label/model` 读取未初始化值（仅影响状态栏显示） | 增加 `idx >= 0` 检查，`custom` 走单独分支 |
| M2 | Low | `ui_deckpro.cpp` WiFi Config exit/destroy | `wifi_cfg_popup` 弹框可能孤立于 `lv_layer_top()` | exit/destroy 回调中加 `wifi_cfg_popup_close_cb(NULL)` |
| M3 | Low | `penpal_api.cpp` `pp_llm_path()` | query 参数未 percent-encode（当前注册表内无需编码字符，暂时安全） | 追加含特殊字符的 model 名时补充 url_encode |
| M4 | ⏸ 待验 | 硬件 | PenPal provider 共享未真机验证 | 下次烧录后走回归清单三项 |

---

## 5. 审批

- [x] **A. 全量接受**（M1/M2/M3 低严重度遗留，不阻止接受；M4 待真机回归后闭合）

**审批人**：Antigravity  
**审批日期**：2026-08-26
