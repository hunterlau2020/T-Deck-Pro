# 评审结果：WiFi 记忆槽与 PenPal 共享 AI Provider（Codex）

- **评审日期**：2026-08-25
- **申请文件**：[wifi-config-keyboard-review-request-c1c6a14..ff6d906.md](wifi-config-keyboard-review-request-c1c6a14..ff6d906.md)
- **评审提交**：`c1c6a14`、`136069a`、`ff6d906`
- **评审结论**：**C 部分接受**。WiFi 槽位、集中式 provider 注册表和请求参数传递方向正确；以下两个配置一致性问题需修复。

## Findings

### P2：Provider 下拉的状态行显示已保存值，而非当前选择

- **位置**：`examples/pda2/ui_penpal.cpp` 的 `pp_cfg_status_text()` 与
  `pp_cfg_provider_dd_cb()`。
- **证据**：下拉回调更新 `s_cfg_provider_idx` 后调用状态刷新，但状态刷新重新从
  NVS 读取 `penpal:ai_provider`。在用户尚未按 Save 时，该值仍是旧 provider。
- **影响**：状态行显示旧 provider/model/key 状态，而不是当前下拉选择，用户无法
  核对即将保存的 AI Provider，违背 Cfg 页的选择预览契约。
- **最小修复**：状态预览应依据 `s_cfg_provider_idx` 枚举 provider 并调用
  `ai_provider_get()`；只有重新进入或保存成功后的“已保存状态”才读取 NVS。

### P2：Server 配置与 AI Provider 分两次保存，可留下混合配置

- **位置**：`examples/pda2/ui_penpal.cpp` 的 `pp_cfg_save_cb()`。
- **证据**：先调用 `penpal_save_config(base, key)`，成功后才调用
  `penpal_save_ai_provider(provider_name)`；第二步失败时状态仅显示 Save failed，
  但 Server URL/Key 已经写入，Provider 保留旧值。
- **影响**：用户以为保存失败，实际设备持有新 Server 配置加旧 Provider 的混合状态；
  后续 Fix/Polish/Tips 可能使用与用户意图不一致的 provider。
- **最小修复**：将三项配置作为一个可验证的保存单元，或保存失败时恢复旧值；至少应
  明确报告部分保存，避免把不一致状态伪装成一次失败。

## 已通过项

- WiFi 五槽的 legacy 迁移、激活槽读取和开机自动连接路径一致；清空激活槽时跳过自动
  连接的行为与设计一致。
- Provider 注册表已从 `ui_ai_cfg.cpp` 集中到 `openai_api.cpp`，AI Config 与
  PenPal 均通过同一枚举/解析接口使用它。
- Fix、Polish、Tips 在 UI 线程解析 provider/model，并在 worker 请求 URL 中附带参数；
  custom/未配置时保持服务端默认路径。
- 差异格式检查通过。

## 验证说明

- 已静态核对 `c1c6a14` 至 `ff6d906` 的完整代码差异、设计文档与调用链。
- 本环境未安装 `pio`，未独立复跑 PlatformIO 编译。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**
