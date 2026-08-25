# 评审结果：WiFi 记忆槽与 PenPal 共享 AI Provider（Claude）

- **评审日期**：2026-08-25
- **申请文件**：[wifi-config-keyboard-review-request-c1c6a14..ff6d906.md](wifi-config-keyboard-review-request-c1c6a14..ff6d906.md)
- **评审提交**：`c1c6a14`、`136069a`、`ff6d906`
- **评审结论**：**C 部分接受**。5 槽 NVS 模型、集中式 provider 注册表、
  Fix/Polish/Tips 参数传递方向均正确；但发现 1 项新的 WiFi 槽位切换
  数据丢失问题（本轮独立发现，Codex 结果未覆盖），加上 Codex 已提出的
  2 项 PenPal Cfg 一致性问题，均需修复。

## Findings

### P1（本轮新发现）：切换 WiFi 记忆槽会静默丢弃未保存的 SSID/Pass 草稿

- **位置**：`examples/pda2/ui_deckpro.cpp` `wifi_cfg_set_slot()`
  （约 2015-2030 行）。
- **证据**：
  ```c
  static void wifi_cfg_set_slot(int slot)
  {
      ...
      wifi_cfg_sync_draft();                 // 只把当前聚焦框同步进 wifi_ssid/wifi_pass
      wifi_cfg_slot = slot;
      wifi_slot_load(wifi_cfg_slot, wifi_ssid, sizeof(wifi_ssid),
                     wifi_pass, sizeof(wifi_pass));   // 立刻用新槽数据覆盖同一对缓冲区
      ...
  }
  ```
  `wifi_cfg_sync_draft()` 把"当前聚焦的那一个"文本框同步进
  `wifi_ssid`/`wifi_pass`，但**两行之后** `wifi_slot_load()` 立刻用新槽
  的 NVS 内容覆盖了这对缓冲区——`sync_draft` 的结果从未被读取或落盘，
  等于死代码。全库确认：只有 `wifi_cfg_save()`（`wifi_slot_save()` 的
  唯一调用点，位于 Connect 成功路径与新增的 Save 按钮 `wifi_save_btn_cb`
  三处）才会真正持久化槽位内容；`wifi_cfg_set_slot()`（音量键
  `\v`、触摸 `<`/`>` 按钮共用）不在其中。
- **触发路径**：用户在槽 2 输入新 SSID/密码，未按 Save/Connect 就误按
  音量键（设计文档明确把音量键定为槽位循环键，§`docs/design-wifi-memory-slots.md:110`）
  或点了 `<`/`>` 想瞄一眼槽 3——槽 2 刚输入的内容无声消失，无任何提示，
  且不可恢复（NVS 里也没有旧值以外的东西）。这是本次改动**新引入**的
  行为：单槽版本没有"切换"概念，因此不存在这个问题；`docs/design-wifi-memory-slots.md`
  §验证清单只检查"切换槽位时输入框正确显示对应槽内容"（第 202 行），
  未覆盖切出时未保存内容的去向，设计遗漏与实现遗漏是同一处。
- **最小修复**：`wifi_cfg_set_slot()` 切换前应先把**两个**字段都同步
  草稿后完整 `wifi_slot_save(wifi_cfg_slot, ...)` 落盘（等价于切换即
  自动保存），或者在检测到草稿与已保存值不同的情况下弹出确认/提示
  （类似已有的 `wifi_cfg_show_msgbox`）。二选一即可，但当前"静默丢弃"
  不应保留。

### P2（Codex 已发现，独立复核确认属实）：Provider 下拉状态行显示已保存值，而非当前选择

- **位置**：`examples/pda2/ui_penpal.cpp` `pp_cfg_status_text()` /
  `pp_cfg_provider_dd_cb()`。
- 复核：`pp_cfg_provider_dd_cb` 更新 `s_cfg_provider_idx` 后调用
  `pp_cfg_update_status()` → `pp_cfg_status_text()`，但后者内部重新
  `penpal_load_ai_provider()` 从 NVS `penpal:ai_provider` 读值，与
  `s_cfg_provider_idx` 完全无关。用户在下拉里选了新 provider 但没按
  Save 之前，状态行仍显示旧值，与 Cfg 页"选择预览"的既有契约（Base
  URL/Key 的状态行都是即时反映当前输入框内容）不一致。同意 Codex 的
  结论与最小修复方向：状态预览应改为按 `s_cfg_provider_idx` 枚举
  provider 并调用 `ai_provider_get()`；已保存状态只在重新进入/保存
  成功后展示。

### P2（Codex 已发现，独立复核确认属实）：Server 配置与 AI Provider 分两次保存，可能落地成混合配置

- **位置**：`examples/pda2/ui_penpal.cpp` `pp_cfg_save_cb()`。
- 复核：
  ```c
  bool ok = penpal_save_config(base, key);
  if (ok) {
      ...
      ok = penpal_save_ai_provider(provider_name);
  }
  if (!ok) pp_status_set("save failed (NVS)"); else pp_status_set("saved");
  ```
  两次独立 NVS 写入非原子；第二次失败时 Server URL/Key 已经落盘，
  provider 仍是旧值，但界面只报一句笼统的 "save failed"，用户无法
  判断哪部分真正生效。同意 Codex 的结论：应作为一个可验证的保存单元，
  或至少明确报告"部分保存"而不是把不一致状态伪装成一次性失败。

## 已通过项（复核确认）

- `wifi_slot_migrate_legacy()`/`wifi_slot_get_active()`/`wifi_slot_load()`
  与 `factory.ino` 开机自动连接路径衔接正确；`legacy_migrated` 一次性
  迁移标记逻辑正确，不会重复迁移。
- provider 注册表已从 `ui_ai_cfg.cpp` 集中到 `openai_api.cpp`
  (`ai_provider_count/enum/find/get`)，AI Config 与 PenPal 通过同一套
  接口消费；`ai_provider_get()` 的"当前激活槽 base 匹配则视为该
  provider 的显式覆盖，否则走 `key.<name>` NVS → env.cfg → 编译期
  默认"回退链逻辑自洽，未发现串号（cross-provider key 泄漏）风险。
- AI Config Save/Test 解耦、key 长度下限改为 `>=15`、HTTP 超时延长、
  `Accept-Encoding: identity`/`Connection: close`/HTTP/1.0 等健壮性改动
  只通过 `http_post`/`http_post_with_headers` 影响 `openai_api.cpp`；
  另一个调用方 `gemini_api.cpp` 当前在 pda2 内无任何调用点（未被引用），
  不构成实际回归面。
- Fix/Polish/Tips 在 UI 线程（`pp_start()`）用 `ai_provider_get()`
  解析 provider/model 写入任务快照，工作线程不触碰 `Preferences`，
  与既有"NVS 只在 UI 线程访问"约束一致。

## 次要 / 不阻塞项

- `docs/merge-conflicts-hd-v2-250915.md`（124 行新文档）随 `c1c6a14`
  一并提交，但申请书 §1 变更明细未提及此文件，与该 commit 描述的
  WiFi/AI 功能无关（是 2026-08-24 一次历史 merge 冲突的记录）。建议
  后续申请遵循"变更明细需覆盖 commit 实际 diff 的全部文件"的评审纪律，
  或在下一份申请里补一句归属说明。
- `factory.ino` setup() 内对 `wifi_slot_migrate_legacy/wifi_slot_load/
  wifi_slot_get_active` 的局部 `extern` 声明与 `ui_deckpro.h`（已被
  `factory.ino` include）里的声明重复，签名一致、无 ODR 风险，纯冗余，
  不要求本轮修改。

## 验证说明

- 已静态核对 `c1c6a14..ff6d906` 的完整 diff（17 个文件）、`pp_state_t`/
  `pp_task_req_t` 相关结构体定义、`ai_provider_get()` 解析链与
  `wifi_cfg_set_slot()`/`wifi_cfg_sync_draft()`/`wifi_slot_save()` 的
  完整调用图。
- 本环境未安装 `pio`，未独立复跑 PlatformIO 编译。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] C. **部分接受**——WiFi 槽位切换数据丢失（P1，本轮新增）与
  Codex 已提出的两项 PenPal Cfg 一致性问题（P2×2）需修复后再次提交；
  其余改动（5 槽模型、集中式 provider 注册表、HTTP 健壮性、Fix/Polish/
  Tips 参数传递）可保留。
