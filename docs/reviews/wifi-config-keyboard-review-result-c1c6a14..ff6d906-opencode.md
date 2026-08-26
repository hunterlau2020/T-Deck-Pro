# 评审结果：WiFi Config 5 记忆槽 + PenPal 共享 AI Config provider

- **评审人**：opencode（Claude 系评审代理）
- **评审日期**：2026-08-26
- **对应申请**：[wifi-config-keyboard-review-request-c1c6a14..ff6d906.md](wifi-config-keyboard-review-request-c1c6a14..ff6d906.md)
- **评审范围**：`c1c6a14`（WiFi 槽位 + AI cfg 修复 + HTTP 健壮性）、`136069a`（provider 共享）、`ff6d906`（CLAUDE.md 一行，docs-only）
- **评审方法**：全量 diff 逐行 + HEAD 上下文核对（`pp_url`/`pp_task_func`/`ai_msgbox`/`wifi_cfg_load` 调用时序等）+ `docs/async_ipc_contract.md` 合规核查 + 本机编译复现
- **编译复现**：`.venv\Scripts\platformio.exe run -e pda2` → **SUCCESS**（RAM 50.1%，Flash 31.2%，与申请 §2 一致）

## 结论：**C 部分接受**

主体实现质量良好（契约合规、边界安全、单源化到位），但 **P2-1 中文错误文案在当前字体下必现方块**，需修复后闭环。Low 项不阻塞。

---

## Findings

### P2-1（Medium）：中文错误文案渲染为方块（tofu）——`c1c6a14`

- **位置**：
  - `examples/pda2/openai_api.cpp`：`out = "读取响应超时";`（空响应体路径）
  - `examples/pda2/ui_ai_cfg.cpp`：`ai_msgbox_set_text("等待返回超时");`（Test 50s 倒计时耗尽路径）
- **事实**：两处文案最终都进 `ai_msgbox`，其正文字体为 `lv_font_montserrat_14`（`ui_ai_cfg.cpp:198`），该字体**无 CJK 字形**——`读取响应超时` / `等待返回超时` 在墨水屏上显示为一排方块，用户不可读。此为项目已知约束（issue_list R4 / penpal 设计 §8-R4："montserrat_14 无 CJK 字形……显示为方块"）。
- **佐证**：申请 §2 验证状态仅覆盖 Test **成功**路径（openrouter/deepseek ✅），超时/空响应路径未真机验证——与本发现吻合（该路径上机必现）。
- **影响**：错误路径 UI 不可读。功能无损（不崩溃、状态机不受影响），但用户要求的"中文提示"实际交付形态是方块。
- **最小修复**：两处改回英文（与同屏其余 msgbox 文案一致，如 `Response timeout` / `Empty response`）。若坚持中文需引入含 CJK 的字体（Flash 成本高，不推荐在错误提示上花）。

### Low-1：错误文案截断无 UTF-8 边界处理——`c1c6a14`

- `openai_api.cpp`：`out.resize(200); out += "...";` 按字节硬切，可能切断多字节码点；下游 `ui_ai_cfg.cpp` `snprintf(fail_buf[192], "Test fail:\n%s", ...)` 二次截断同款。codebase 有 UTF-8 安全截断先例（`ui_ai_chat.cpp` 单点截断 + 码点回退）。错误路径、装饰性，顺手对齐即可。

### Low-2：CFG 页 PROVIDER 焦点下 `\b` 无响应——`136069a`

- `ui_penpal.cpp:1146-1156`：焦点在 AI Provider 下拉时 `\b` 被忽略。键盘退出 CFG 需先 `\t` 两次回到**空**输入框才能退——设计 §4.7 只定义了输入框的 `\b`，未定义 provider 行。建议 provider 焦点下 `\b` 同样返回 HOME（触摸 Back 不受影响，仅键盘路径）。

### Low-3：`[HTTP] status= ct= ce=` 日志恒为空——`c1c6a14`

- `http_utils.cpp`：`http.header("Content-Type")` / `http.header("Content-Encoding")` 在未调用 `collectHeaders()` 收集对应头时**恒返回空串**（ESP32 HTTPClient 语义）。该诊断行承诺了它给不了的信息。删除或在 `begin()` 后补 `http.collectHeaders()`。

### Low-4（文档）：申请/设计文档两处与代码不符

- 申请 §1.1 称 `\v` "**非扫描模式下**循环切换槽位"；实际代码 `\v` 分支在扫描分支**之前**，任意模式可用（切换会复位 `wifi_cfg_scan_mode` + `wifi_scan_gen++` 作废在途扫描——行为安全，文档低估了功能）。
- `design-penpal-ai-provider-link.md` §7 四项验收全 `[x]`，但申请 §2 自认 PenPal provider 共享真机 ⏸——`[x]` 语义应为"代码已实现"，真机项建议改 `[x](code) ⏸(device)` 之类的区分，避免后续读者误判。

### Low-5：WiFi 结果弹窗吞键后残留队列注入——`c1c6a14`

- `wifi_cfg_keyboard_poll` 弹窗分支每次 poll 只消费**一个**字符：弹窗开时 FIFO 里排着的第 2 个及以后的键会在弹窗关闭后流入正常处理器（Connect 是 15s 阻塞，阻塞期间键盘 FIFO 可能积压）。先例：chat 发送完成后清 FIFO（体验评审 §1.6）。建议弹窗关闭时顺带 `keypad_clear_chars()`。

---

## 核查通过项（摘要）

1. **WiFi 5 记忆槽**（`c1c6a14`）：
   - NVS 键生成/读写边界安全（`slot%d_%s` ≤11B；`strncpy` 全部带终止）；`wifi_slot_load` 越界 slot 归 0。
   - **Connect-成功-才持久化**次序正确（`wifi_cfg_save()` → `wifi_slot_set_active()`）；Clear 保留 active 标记、boot 空槽跳过（`factory.ino` 判 `ssid[0]`）——与设计 §3.2/§7.6 一致。
   - `wifi_slot_migrate_legacy` 幂等（哨兵早退；无 legacy 数据也落哨兵，一次性）。
   - 结果 msgbox 挂 `lv_layer_top()` 且 **`exit4_1` 关闭**——符合 round 25 确立的 push-away 泄漏防治模式。
   - 槽位切换 = `wifi_cfg_scan_mode=false` + `wifi_scan_gen++`（迟到扫描结果按代次丢弃，安全）；切换丢弃未保存草稿为**设计明文行为**（§3.5：只有 Connect/Save 持久化）。
   - `\v` 用独立音量键切槽 = 设计 §7.2 给出的两个备选之一，合规。
   - `ui_deckpro.h` 新声明置于 `extern "C"` 块外、`#ifdef __cplusplus` 内——C++ 链接与定义一致（`.ino` 内局部 extern 同为 C++ 链接）。
2. **HTTP 健壮性**（`c1c6a14`）：`http_post` 包装委托 `http_post_with_headers`，旧调用点签名零改动；HTTP/1.0 + `Accept-Encoding: identity` + `Connection: close` 影响**所有 POST 消费者**（AI Chat/Test）——申请已附 openrouter/deepseek 双 provider 真机验证；weather 走 GET、PenPal 自带传输，不受影响。`openai_chat_impl` 全部早退路径现在都落 `out`（此前静默空串）——错误可见性改进；`reasoning.exclude` 收敛到 OpenRouter 正确（OpenRouter 扩展字段）。
3. **Provider 注册表单源化**（`136069a`）：`s_providers[]` 从 `ui_ai_cfg.cpp` 迁至 `openai_api.cpp`，消除原 KEEP-IN-SYNC 双列表隐患；下拉选项由注册表动态构建。`ai_provider_apply` 经 `ai_provider_get` 在活动槽 base 匹配时回填**用户已保存的自定义 model/key**（较旧行为的改进，round-trip 保持自定义）。
4. **异步 IPC 契约合规**（对照 `docs/async_ipc_contract.md`）：
   - `pp_start` 在 **UI 线程**完成 provider 解析（Preferences 不入 worker，设计 §6.1 的循环依赖隔离同时满足）；`rq->ai_provider/ai_model` 为快照 `std::string` 拷贝，worker 内随 `delete rq` 释放——契约 §2.5（任务独占快照）合规。
   - inflight 原子计数仍在 `xTaskCreate` 之前（P2 上限语义未破坏）。
   - `penpal_correction/polish/tips` 签名扩展为增量参数，POD 结构维持值初始化规则（无 memset 回潮）。
5. **URL 构造**：`pp_llm_path`/`pp_url` 全 `std::string` 拼接，无定长缓冲溢出风险（query 加长后最长 ~180 字符，heap 分配）；provider/model 不做 URL 编码为**设计登记限制**（design §6.3），非新引入。
6. **NVS 写校验**：`penpal_save_ai_provider` 写后读回验证，失败上串口——与 codebase 既有模式一致。
7. **安全**：API key 全程不上串口（`[AICfg] key for %s loaded` 只打名字）；`AI_KEY_DEFAULT_DEV` 保持 `#ifdef` + gitignored 层级（secrets 链第 3 层，round 29 已确立）；OpenRouter 归因头无敏感信息。
8. **`ff6d906`**：CLAUDE.md 单行追加，docs-only，无代码影响。

## 登记备注（非缺陷）

- **Save 与 Test 解耦**逆转了此前"Test 通过才允许 Save"的门禁（allinone 设计稿 §4 曾将其列为移植原则、pda2 曾实现）。本处为**用户明示决策**（申请 §1.2），接受；仅登记：未来移植/重构不得凭旧设计稿"恢复"该门禁。
- AI Test 超时 45s+5s 后 msgbox 倒计时文案已动态化（`Testing... %lus`），与硬编码 15s 时代的旧文案无残留冲突。
- `pp_start` 对**所有** PenPal 请求（含 PALS/MAILBOX 等非 LLM 类）都做一次 provider 解析（2-3 次 NVS 读 + 可能的 env.cfg 读）——UI 线程、频度低，不构成问题；若追求精确可在 type 为 FIX/POLISH/TIPS 时才解析。

## 处置建议

| 项 | 处置 |
|---|---|
| P2-1 | **需修复**（改英文文案即可，两处一行改动）后本轮闭环 |
| Low-1..Low-5 | 建议随 P2-1 顺手处理；不阻塞 |
| PenPal provider 共享真机回归 | 维持 ⏸（申请已如实登记）；Cfg 下拉/保存/Fix/Polish/Tips 参数上机后再出列 |

## 审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订**
- [x] **C. 部分接受** —— P2-1 修复 + Low 项酌情；其余全量接受
