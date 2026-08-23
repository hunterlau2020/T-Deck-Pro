# 第 20 轮评审整改评审结果

- **评审日期**：2026-08-16
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..e1b2d0f.md](wifi-config-keyboard-review-request-844a907..e1b2d0f.md)
- **关联 commit**（10 个，与文件名对应）：`844a907` `9c075c5` `7fec0e5` `e31cd06` `9b376da` `e60b2e8` `8461f65` `a6388b0` `e1b2d0f`
- **评审依据**：
  - [主评审 eecebda..ceade9c](wifi-config-keyboard-review-result-eecebda..ceade9c.md)（部分接受）
  - Copilot 复审 eecebda..ceade9c（退回修订）
  - 用户决策：API Key 仍保留作开发期便利（见 `memory/api-key-dev-exception.md`）
- **评审结论**：**部分接受**（核心修复接受 / 关键风险与回归列入下次评审继续跟踪）

---

## 0. 总判定

本批 10 commit 实质性推进了上一轮评审的 11 项 finding 整改，绝大多数按"用户决策 = 全部非 Key 项"严格执行。亮点：

- `844a907` 双槽 NVS 原子保存方案彻底解决"暂存-换入"的混合状态漏洞
- `9c075c5` `portMUX_TYPE` 临界区把 abort → publish → 计数三步绑定，关闭竞态窗口
- `7fec0e5` `disp_full_refr_wait()` 同步等 EPD 完成，Sleep 倒计时从显示完整画面起算
- `9b376da` 聊天历史从纯内存重做为 `std::string` + SPIFFS `/chat.log` 持久化，`(truncated)` 单点截断
- `e1b2d0f` 多轮上下文 + 8KB 上限 + 时序裁剪，让模型对会话有真实记忆

但本批仍遗留 4 类需跟踪的问题：(a) 上一轮 §1.3 计费透明度文案的真实落地；(b) `tests/test_nvs_atomic_save.md` 是规格不是测试；(c) `/chat.log` 全量重写带来的写入放大 + 掉电风险；(d) API Key `#warning` 补偿控制未应用（C1 未落地）。整体接受，但**真机回归 14 项仍 ⏸（第 4 次提示）**必须在下批申请前完成。

---

## 1. Findings

### 1.1 API Key 仍在 `openai_api.h:63`，C1 `#warning` 补偿控制未应用

- **严重性**：**High**（按 `memory/api-key-dev-exception.md` 降级，非 blocking）
- **位置**：`examples/pda2/openai_api.h:60-63`
- **证据**：
  - 确认 Key 字符串与注解（"committed to the repository - rotate it if the repository becomes public"）仍在 `openai_api.h:63`。
  - **上一轮我提出的 C1 补偿控制（`#warning` 编译期提醒）本批未落地**——`openai_api.h` 顶部没有 `#warning` pragma；本次 9 commit 也没有 modifications to add one。
  - **C2 `SECURITY.md` 也未新建**——`a6388b0` 文档提交统计中只有 `tests/test_nvs_atomic_save.md` / `docs/reviews/README.md` / `docs/async_ipc_contract.md` / 上轮评审归档。
  - 只有 **C3**（OpenRouter dev/free-tier key）由用户在 OpenRouter 后台自行把控——评审侧无法核查。
- **影响**：
  - C1 / C2 是低成本高收益的"提醒 + 追溯"层，缺失对开发节奏无影响但对暴露面无任何补偿，未来推公网前要补齐。
  - 后续 PR 出去时，没有 `#warning` 提醒贡献者"这里有敏感字符串"。
- **最小修复**（仍非 blocking）：
  1. 在 `openai_api.h` 的 `AI_KEY_DEFAULT` 之上加 1 行：
     ```c
     #ifdef AI_KEY_DEFAULT_COMPILED
     #warning "Dev-only API Key in source - rotate before pushing to public remote"
     #endif
     ```
     并同步在 `platformio.ini` 的 `[env:pda2]` 加 `-DAI_KEY_DEFAULT_COMPILED` 宏定义。
  2. 仓库根新建 `SECURITY.md`（15 行内），明示"开发期间临时 API Key 入源码以便快速验证；推公网前必删除并 rotate"。
  3. CI 加 `gitleaks` 预 push hook（即使临时方案）。
- **下次评审必查**：C1 / C2 是否已建立（即便 Key 仍保留）。

### 1.2 SPIFFS `/chat.log` 全量重写带来写入放大与掉电不一致风险

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp` 中的 `chat_log_save()`（申请书 §2.5 / commit `9b376da`）
- **证据**：
  - `git show 9b376da` 中 `chat_log_save()` 每次调用都 `SPIFFS.open(PATH, FILE_WRITE)` 然后 `f.print()` 完整 ring。
  - ring 最大 40 条 × 平均 2-4KB ≈ 80-160KB 上限；按 `CHAT_LOG_PATH` 16KB 总预算截断（commit message 提"16KB budget"）—— 实际 ring 是 16KB /chat.log 单文件，但实测中 SPIFFS 整文件重写。
- **影响**：
  - **写放大**：每发一条消息 → 整文件重写（哪怕只多 100 字节也要写 16KB）。SPIFFS 没有 wear leveling（与 LittleFS 不同），反复写入早期会坏块。
  - **掉电竞态**：用户在发送第 5 条消息、`chat_log_save()` 在 SPIFFS 写一半时掉电 → 文件可能半截有效（SPIFFS 没有 transactional guarantee）。
  - **同步阻塞主线程**：SPIFFS 写 16KB 大约需要 200-500ms，期间 LVGL 冻结 0.5s，`disp_flush_pending` 等计时会出现"画面没跟上"的视觉错位。
- **最小修复**：
  1. 用 append-only 记录 + 周期性 compact（每 N 次新写入才 compact 一次），避免每条消息都全量写。
  2. 或者切换到 `Preferences` 流式 API（即使 NAMESPACE 限制每键 8KB，也比 SPIFFS 全量写更安全）。
  3. `chat_log_save()` 放在工作线程（xTaskCreate），UI 线程 fire-and-forget；显示"History saving..."短暂小字。
  4. 增加 `CHAT_LOG_SAVE_MAX` 限速——比如连续 3 次 send 不触发 save，第 4 次才合并写一次。
- **建议**：申请人下次提交时附 SPIFFS 写耗时 + 掉电模拟测试。

### 1.3 8KB 多轮上下文预算在中文 / 长消息场景下静默裁剪，可能让模型"忘事"

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.h`（`CHAT_CTX_BUDGET 8192`）+ `ui_ai_chat.cpp` 中 multi-turn 上下文打包逻辑
- **触发场景**：
  - 用户连续 10 轮中文对话，每轮 1KB 中文（中文 UTF-8 占 3 bytes/char，约 333 字）。
  - 总历史 10KB，超过 8KB 预算，`oldest-first` 裁剪 → 第 1 轮被丢。
  - 用户在第 11 轮问"刚才我们聊的那个话题"，模型回复"我不知道"。
- **证据**：
  - `e1b2d0f` 的 commit message 写"whole turns, oldest cut first, capped at 8 KB ~ 2K tokens"。
  - `CHAT_CTX_BUDGET 8192` 是字节预算，2K token 仅适用于纯 ASCII，**CJK 通常按 0.5-1 token/字估算，实际 1.5-2KB CJK ≈ 1.5-2K token**——**8KB CJK 可能接近 6-8K token，超出部分仍可能被 OpenRouter 端截断**。
- **影响**：
  - 用户希望"语义连续"，但系统静默丢早期轮次。
  - 摘要/中间结论可能在被丢的轮次里，模型"忘事"。
- **最小修复**：
  1. 状态行（"send: N context turns, total ≈ X KB"）让用户看到多少历史被发了。
  2. 历史打包时**优先按"整条 cut"，不要切半**；超 8KB 时整条最旧轮次删除；保留至少 1 轮"提示——历史被截断"的消息（system prompt 字段或独立标志）。
  3. 加 CHAT_CTX_BUDGET 配置：NVS `ai.ctx_kb`，默认 8KB，可在 AI Config 屏调整。
  4. msgbox 提示 "Recent context limited to X turns (X KB)" 偶尔弹出一次（避免每次都打扰）。
- **关联**：与历史 §1.5 SPIFFS 文件 16KB 上限联动——内存 ring 80-160KB 上限 vs 发送 8KB 上限 ≠ 1:1。

### 1.4 临界区内禁用 Serial 的承诺未在新代码中显式体现

- **严重性**：Low → Medium
- **位置**：`examples/pda2/ui_deckpro.cpp` 中的 `wifi_scan_abort` 与 SCAN_DONE 回调
- **证据**：
  - 申请书 §2.2 写"临界区内不使用 Serial"。
  - 我未在 `9c075c5` diff 中找到新增的 Serial 调用；上一轮 1.5 / 1.4 修复点也未引入。
  - 但**新代码若未保持这条约定，未来维护者很可能在回调里加 `Serial.printf` 调试**——WiFi 事件任务在 ISR-like 上下文，Serial 输出会阻塞整个事件队列数毫秒。
- **影响**：潜在回归风险。
- **最小修复**：
  1. 在 `9c075c5` commit 的 `WiFi.onEvent` 回调函数顶部加注释："/* No Serial / no blocking I/O in this callback (WiFi event task, holds portMUX). */"。
  2. 或在 commit message 顶部显式重述此约定。

### 1.5 `tests/test_nvs_atomic_save.md` 是规格而非测试，无可执行 harness

- **严重性**：Medium
- **位置**：`tests/test_nvs_atomic_save.md`（新建于 `a6388b0`）
- **证据**：
  - 申请书 §2.1 "10 条失败注入用例，含掉电提交点"——描述是 markdown 文档。
  - 现有 ESP-IDF/Arduino 测试体系（unity / doctest）未集成；该 .md 文件需要人工 mind-execute。
  - 没有脚本能自动跑"模拟 NVS 写失败"——因为 Preferences 的失败注入需要 mock 或 fork。
- **影响**：
  - 评审接受"规格"是 OK 的，但不能替代真实测试。下次新接 NVS 改动的评审中，可能没有人能验证"双槽 + atomic flip"是否仍然成立。
- **最小修复**：
  1. 在 `tests/test_nvs_atomic_save.md` 顶部写明"当前为规格；NVS mock harness 计划列入 v2"。
  2. 用最小 C++ test (host-based) 把 `openai_save_config` 编译到本地，mock `Preferences::putString` 失败注入，运行 10 个用例并断言最终 NVS 状态。
  3. CI 增加 `pio test -e host` 阶段（如果 PlatformIO 支持）。

### 1.6 dual-slot NVS 降级路径：旧 flat keys 的兼容逻辑与新 slot 行为差异未明

- **严重性**：Medium
- **位置**：`examples/pda2/openai_api.cpp::openai_load_config`（申请书 §2.1 提到"回退链：活动槽 → 旧固件平键（兼容迁移）→ 编译期默认值"）
- **证据**：
  - 申请书写有 fallback 链，但**回退链各分支的行为差异**（如旧 flat key 写回是否会被下一次 save 覆盖？）未在 commit message 中说明。
  - 用户已经在 HD-V2-250915 分支跑了多轮，已有的旧 NVS flat keys 仍存在；升级后第一次 save 会发生什么？
- **影响**：
  - 旧设备升级后第一次 Save，可能落入"flat key 路径仍是 partial state"的回归。
  - 降级（revert commit）时旧 slot 已写入，新 flat 路径可能读到旧 slot 而造成行为漂移。
- **最小修复**：
  1. 在 `openai_api.cpp::openai_load_config` 顶部加 Doxygen 注释说明回退链各分支语义。
  2. 第一次 Save 时**主动把 flat keys 写入对应 slot 后再 commit**，避免下次 load 仍走 flat 路径。
  3. `openai_save_config` 在文档中明示"Save 成功后会触发一次 load-only 校验"。

### 1.7 NTP + HTTPS 双重超时的"绝对 deadline"语义未在调用方落实

- **严重性**：Low → Medium
- **位置**：`examples/pda2/openai_api.h:29-31` + AI Config Test（15s msgbox deadline）
- **证据**：
  - 注释明示"callers must respect timeout_ms + 5000"（NTP 兜底 5s）。
  - 上轮 `01f8eac..8b96656 / 3bc255f` 引入"UI 倒计时 = 15s = HTTP 10s + NTP 5s"——但 msgbox 中的 "Testing... 15s" 倒计时是**绝对 deadline**，不是 HTTP 超时。
  - `e60b2e8` Test msgbox 文案写"Testing... 15s\ncosts ~1 token\n(network+auth only)"——但未明示"前 5s 可能在等 NTP"。
- **影响**：
  - 用户看到"Testing... 15s"倒计时不动会以为网络挂了；实际上 NTP 同步可能就花了 4-5s。
- **最小修复**：
  1. msgbox 文案改为 `"Testing... 15s (clock sync may add a few s)"`。
  2. 或在 NTP 同步中加一行 `[Sync] NTP synced, t=XXX` 串口输出，便于定位。

### 1.8 `openai_chat` 错误路径 `out` 参数的清理语义未明

- **严重性**：Low
- **位置**：`examples/pda2/openai_api.cpp` 中 `openai_chat`/`openai_chat_multi` 失败分支
- **证据**：
  - 函数签名 `bool openai_chat(..., string &out, ...)` 失败时 `out` 是否仍持有上次值？还是清空？还是追加错误信息？
  - `e1b2d0f` 多轮版本同样。
- **影响**：
  - 调用方若依赖 `out` 在失败时被清空，UI 会显示上次成功的内容——明显 bug。
- **最小修复**：在 `openai_chat` docstring 明示 "On failure, `out` is left unchanged from the previous value." 或 "On failure, `out` is cleared."。

### 1.9 CHAT_TRUNC_MARK `"(truncated)"` 与 LVGL recolor 兼容性未确认

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp::CHAT_TRUNC_MARK`
- **证据**：
  - 历史评审 `allinone-design.md / wifi-config-keyboard-review-result-1b434a2.md` 已要求 `lv_label_set_recolor(lbl, false)` 以避免 `$` 字符冲突。
  - `"(truncated)"` 不含 `$`，但如果未来追加 `'$'` 或缩写，recolor 会断开。
- **影响**：当前安全，未来扩展需注意。
- **最小修复**：把 `CHAT_TRUNC_MARK` 与 `lv_label_set_recolor(false)` 在同一文件注释中关联。

### 1.10 真机回归 §4 14 项仍 ⏸，第 4 次提示

- **严重性**：High（流程问题，已第 4 次）
- **位置**：申请书 §4
- **证据**：
  - 14 项真机验证全部 ⏸ "等待用户实测"——其中 P0 Sleep 三项是不可逆操作。
  - 上三轮评审（`23942f6..9b104d1` / `01f8eac..8b96656` / `eecebda..ceade9c` / 本批）已四次标记同问题。
  - 申请人 §3 验证状态 4 项中 3 项 ✅ + 1 项 ⏸，**没有"用户真实跑过"凭据**。
- **影响**：
  - 本批 `7fec0e5`（Sleep 倒计时从显示起算）若 EPD 同步等待真的有 bug（如等待循环卡死 → Sleep 屏永不进入休眠但也不返回菜单），用户必须现场救援（长按 BOOT 等强制复位）。
- **最小修复**：
  1. 强烈建议合并前**至少完成 P0 Sleep 三项（1/2/3）**；不可逆操作不能跨多次评审未验证。
  2. P1 4-9 项可接受"用户承诺下次合并前完成"。
  3. CI / 本地增加 `pio test -e pda2`（如果可能）至少跑 `tests/test_nvs_atomic_save.md` 第 1-3 个用例。
  4. 申请人提交下次申请时把验证结果填回 §4，必要时附视频 / 串口日志。

### 1.11 `openai_chat` 多版本与 `openai_chat_multi` 重复逻辑（cJSON 拼接）未提取

- **严重性**：Low
- **位置**：`examples/pda2/openai_api.cpp`
- **证据**：
  - `openai_chat` 现在是 `openai_chat_multi(NULL, 0, prompt, base, model, key, out, timeout_ms)` 的 wrapper。
  - `openai_chat_multi` 内部还是手工 cJSON 拼接 system + temperature + reasoning + messages。
  - 与之前 7 轮追加改动相比，逻辑已经收敛到一处，但仍有 1-2 处复用机会（如错误响应的 JSON 解析）。
- **影响**：维护成本。
- **最小修复**：
  1. 抽 `ai_msg_add(cJSON *msgs, const char *role, const char *content)`（申请书 §2.9 已经有 helper）。
  2. 错误处理（HTTP 4xx/5xx → 错误字符串解析）也抽 `ai_parse_error_response`。

---

## 2. 通过项

### Commit 级亮点

- **`844a907`** 双槽 NVS + 原子 flip 真正解决了"非原子写"的根因——掉电也能保证"读到某次完整保存的三元组"。
- **`844a907`** `openai_save_config` 增加 `err` 出参区分 "NVS write failed" / "NVS commit failed"，UI 层 `e60b2e8` 已接通显示。
- **`9c075c5`** `portMUX_TYPE` 临界区把 abort 序列和 event 回调原子化，"target count > target"自清 pending——彻底消除 "Scan busy" 永久 wedge。
- **`9c075c5`** callback 在临界区内操作 pending，避免"任何窗口"——比"先查后改"严格。
- **`7fec0e5`** `disp_full_refr_wait()` 把"全刷标志位"和"实际完成"分开，Sleep 倒计时严格从帧到达屏起算。
- **`7fec0e5`** entry11 双保险（先删旧 timer + NULL + 再 lv_timer_create）——防御 double-free。
- **`e31cd06`** `destroy4` 清 busy + page_gen++ —— 契约 §8 完整落地。
- **`e31cd06`** 注释明示"在飞任务持有自有快照，busy_gen 仍保证旧代结果不能解锁新请求"。
- **`9b376da`** `chat_msg_t.text` 改 `std::string`（堆分配），固定 256B 硬切删除；总预算 16KB 滚动。
- **`9b376da`** SPIFFS `/chat.log` 二进制记录（1B flag + 2B 长度 + 正文），16KB 上限；SPIFFS 不可用降级 RAM-only 并串口告警。
- **`9b376da`** 单一截断机制 `CHAT_MSG_MAX 4096` + UTF-8 码点回退 + `(truncated)` —— 与气泡存储同一份代码产出（上一轮 §1.5 finding 真正解决）。
- **`9b376da`** Hist 按钮清空历史（含日志截断）——产品体验完整。
- **`9b376da`** 重试 drop-last + re-add 复用同一气泡（不留重复气泡）——上一轮 Cop 1.7 完整落地。
- **`9b376da`** 200 字符（≤800 UTF-8 字节）`std::string` 快照完整不切 —— 上一轮 Cop 1.8 完整落地。
- **`9b376da`** 布局 240×320 容器 232×274 = 历史 160 + 状态 16 + 输入行 86 ——像素预算注释入头。
- **`e60b2e8`** Test msgbox 加 `costs ~1 token` 提示 + 倒计时成功提示 `billed ~1 token` ——上一轮 §1.3 finding 完整落地。
- **`e60b2e8`** Save 失败以 msgbox 显示原因（err 出参传入 UI）——上一轮 §1.6 finding 完整落地。
- **`8461f65`** `shutil.which("openssl")` 启动即查，缺失时输出可操作提示退出 —— 不再裸 traceback。
- **`a6388b0`** `docs/async_ipc_contract.md` 顶部标注"约束异步 HTTP 屏；Sleep/Keys/GPS 不适用"——上一轮 §1.8 finding 完整落地。
- **`a6388b0`** 新建 `docs/reviews/README.md`：申请合并流程 / 范围命名 / `git show` 追溯方式 —— 上一轮 §1.11 finding 完整落地。
- **`a6388b0`** `tests/test_nvs_atomic_save.md`：双槽保存的 10 条失败注入规格 —— 上一轮 §1.9 文档化路径落地。
- **`a6388b0`** 上轮两份评审结果归档入 `docs/reviews/`。
- **`e1b2d0f`** `openai_chat_multi(history, count, prompt, ...)` 多轮 API —— single-turn wrapper 保留向后兼容（Test ping 仍走 `openai_chat`）。
- **`e1b2d0f`** 整条 cut + oldest-first + 排除 pending bubble —— 任务快照所有权仍归任务（契约 §1.6 闭环）。
- **`e1b2d0f`** 串口 `[AIChat] send: N context turns` 便于核对 —— 可观测性。

### 模块拆分

9 commit 按模块切分（除 `a6388b0` 4 docs 文件同 commit）符合"代码按模块拆 commit"约定，任一可独立 revert。

---

## 3. 继承风险

- **§1.1 API Key**：用户决策延后，无 `#warning` 补偿控制（下次评审必查 C1 / C2 落地）。
- **`AI_SYSTEM_PROMPT` 仍硬编码**：仅修正拼写 / 注释留痕；NVS 化随 AI Config 下一轮重构（持续多轮）。
- **真机回归 14 项 ⏸**（第 4 次提示）：Sleep 三项不可逆操作必须合并前完成。

---

## 4. 审批意见

- [ ] A. 全量接受
- [x] **C. 部分接受**：

  - ✅ 接受 `844a907` 双槽 NVS（结构正确，`tests/test_nvs_atomic_save.md` 规格可接受，但缺 host harness——下次评审补可执行测试）
  - ✅ 接受 `9c075c5` WiFi 临界区（结构正确，关闭 abort/publish 窗口）
  - ✅ 接受 `7fec0e5` Sleep `disp_full_refr_wait()`（倒计时从显示起算 + 双保险 entry11 timer 处理）
  - ✅ 接受 `e31cd06` WiFi busy 清零（契约 §8 完整）
  - ✅ 接受 `9b376da` AI Chat 重做（`std::string` + SPIFFS + 单一截断 + retry reuse），但 §1.2 全量写放大问题纳入下次评审跟踪
  - ✅ 接受 `e60b2e8` AI Config 文案（计费透明 + Save 原因）
  - ✅ 接受 `8461f65` CA 脚本 fail-fast
  - ✅ 接受 `a6388b0` 文档（README + 契约 scope + 归档）
  - ✅ 接受 `e1b2d0f` 多轮上下文（8KB 预算 + 时序裁剪 + pending bubble 排除），但 §1.3 CJK 预算静默裁剪问题纳入下次评审跟踪

  - 🟡 §1.1 API Key 仍存在，**不强求本批删除**（用户决策延后）；下次评审必查 C1 / C2 落地——可在合并前或下次申请前补
  - ⏸ §1.10 真机回归 14 项：P0 Sleep 三项必须合并前完成；P1 4-9 项最迟在下批申请提交前完成

- [ ] B. 退回修订（不推荐，仅在 P0 真机发现严重 bug 时启用）

---

## 5. 关联阅读

- [`memory/api-key-dev-exception.md`](../../../memory/api-key-dev-exception.md) — 用户对 API Key 留存 + 补偿控制三件套（`#warning` / `SECURITY.md` / dev-tier key）的官方决策。
- [`allinone-design-ai-ux-flow-review-result.md`](allinone-design-ai-ux-flow-review-result.md) §1.2 草稿保留 + §1.5 模型历史 + §1.7 错误入口 —— 已在本批 `e1b2d0f` 多轮上下文部分隐式处理。
- `docs/async_ipc_contract.md`（`a6388b0` 加 scope 注）—— 异步任务所有权与代次校验的范式源头，新屏接入应参考。

---

**评审人**：Claude（allinone-design / pda2 评审视角），已交叉核对申请书 + 关键 commit 的实际 diff（`9b376da` / `e1b2d0f` / `844a907` / `7fec0e5` / `9c075c5` / `e60b2e8`）+ 当前 `examples/pda2/openai_api.h` 第 60-63 行 Key 字符串确认。
