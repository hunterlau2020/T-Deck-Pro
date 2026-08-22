# 2026-08 commits 全量评审 — 进度状态与恢复计划

- **整理日期**：2026-08-21
- **状态**：**中断，未完成**。5 批评审代理均在产出结论前被停止，无一批完成。
- **本文目的**：记录范围、批次划分与断点，供后续会话直接恢复，不重做已完成的清点工作。

## 1. 范围与统计

- 起点：`--since=2026-08-01`，共 **185 个 commit**。
- 纯文档类（`docs*` 前缀，review 请求/归档等簿记）：**89 个**，不需代码评审。
- 代码类：**96 个**（其中 90 个已编入下列 5 批；剩余 6 个为混入批次 4 的
  文档/构建类，恢复时按 `git show --stat` 过滤即可）。

## 2. 批次划分（已分配）

| 批次 | 主题 | commit 数 | 状态 |
|---|---|---|---|
| B1 | WiFi 配置/扫描/WiFi Test/TLS/CA/NTP + 异步 IPC 落地 | 20 | ⛔ 额度 403 两轮失败（2026-08-22），待额度恢复后重跑（agent-8） |
| B2 | AI Config 重做 / 双槽 NVS / usage 统计 / secrets 链 / TLS toggle | 25 | ⛔ 额度 403 两轮失败（950fcfe 已由申请评审单独通过）；待重跑（agent-9） |
| B3 | AI Chat 重构 / 任务快照 / SPIFFS 持久化 / 多轮上下文 | 21 | ✅ **完成**（2026-08-22，见 §2.3） |
| B4 | 睡眠倒计时 / 键盘 / 天气迁移 / 菜单 / GPS 快照 / 构建脚本 | 22 | ✅ **完成**（2026-08-22，见 §2.2） |
| B5 | phase-0 试点大 commit（78efe2e、9bffa78） | 2 | ⛔ 额度 403 两轮失败；待重跑（agent-12） |

### B1（20）
`1e8589b 484e617 a55014e c360315 9eb9373 ebc1030 3dda7d1 c416841 bc7fdd4 f51c83e a2bfc46 0bbf16a 71de56e cce04a9 a0f6f01 55b5046 6f2bbe7 d75e15a 5deb7d6 1ba2a4b`

### B2（25）
`6013bf1 6c3f79a 23e9615 472c51f 6135ec6 9251388 984949d 676a9f8 1f07dcb 6784852 d90d62a 9c29462 dddbd4a 7fc9adf 3e74131 0f73b85 2064934 21df8cd cb98b16 f17c383 8f17fa2 2e6ba8e 2a34416 5f359e6 950fcfe`

### B3（21）
`285c068 e3a6446 2a83d0d c91fcf2 327765f b189a01 04ef099 04baf20 22c149f 4327aec 47542a0 53a78e9 e4236db ddfee0a 901a5f8 b47dd4c 7142548 5f15676 a9873bd 955a492`

### B4（22，含少量 docs-only，按 --stat 过滤）
`ee14c44 2ef68f9 f38b2bc b768aca 98d370b 7d5aa8d 0a6b3c3 fa6b989 dac7902 42961e5 cedaee8 c4c44b6 c204d26 1a464b5 b7943e3 8ee3376 b7ee628 fbfc16c 23030c9 52f709e 6d26699`

### B5（2，大 commit）
`78efe2e 9bffa78` —— 重点：发现的疑似问题需先对照当前 HEAD 确认是否仍存活
（后续 ~70 个 commit 已大改这些屏），已被后续修复的标 "fixed-later" 不重复上报。

### 2.1 每批涉及的非文档文件（2026-08-22 回填）

`git show --name-only` 对批内全部哈希取并集、去重，滤除 `docs/`、`.github/`、
`*.md`、`*.py`。批次内无 merge commit，列表完整。**评审对象合计 26 个文件。**

- **B1（6）**：`examples/pda2/` 下 `factory.ino`、`http_utils.cpp/h`、
  `peri_keypad.cpp`、`ui_deckpro.cpp`、`scripts/ca_bundle_check.sh`
- **B2（14）**：`examples/pda2/` 下 `factory.ino`、`http_utils.cpp/h`、
  `openai_api.cpp/h`、`env_secrets.cpp/h`、`env.cfg.example`、
  `config_keys.h.example`、`ui_ai_cfg.cpp`、`ui_deckpro.cpp`、`ui_weather.cpp`；
  另 `platformio.ini`、`.gitignore`
- **B3（9）**：`examples/pda2/` 下 `factory.h`、`factory.ino`、`http_utils.h`、
  `openai_api.cpp/h`、`ui_ai_chat.cpp`、`ui_deckpro.cpp`、
  `ui_deckpro_port.cpp/h`
- **B4（17）**：`examples/pda2/` 下 `factory.h`、`factory.ino`、`http_utils.cpp`、
  `peri_gps.cpp`、`peri_keypad.cpp`、`peripheral.h`、`ui_ai_cfg.cpp`、
  `ui_ai_chat.cpp`、`ui_calculator.cpp`、`ui_deckpro.cpp`、
  `ui_deckpro_port.cpp/h`、`ui_scr_mrg.c`、`ui_weather.cpp`；
  另 `examples/test_keypad/test_keypad.ino`、`platformio.ini`、`.gitignore`
- **B5（15）**：`examples/pda2/` 下 `factory.ino`、`http_utils.cpp/h`、
  `openai_api.cpp/h`、`peri_gps.cpp`、`peri_keypad.cpp`、`peripheral.h`、
  `ui_ai_cfg.cpp`、`ui_ai_chat.cpp`、`ui_deckpro.cpp/h`、
  `ui_deckpro_port.cpp/h`

核心源码（.ino/.cpp/.h/.c）共 20 个，全部位于 `examples/pda2/`
（唯一例外 `examples/test_keypad/test_keypad.ino`），另含构建配置 2 个
（`platformio.ini`、`.gitignore`）、模板/脚本 3 个
（`env.cfg.example`、`config_keys.h.example`、`ca_bundle_check.sh`）。

> 工具坑（勿再踩）：多哈希喂 `git diff-tree` 会按"多父合并比较"语义输出空，
> 必须用 `git show --name-only --format=""`。B5 早先记录的 9 文件清单即此因
> 不全，上表 15 才是并集。

### 2.2 B4 评审结论（2026-08-22，Kimi）

18 个代码 commit 全部复核（23030c9/52f709e/6d26699 另经申请评审单独通过）。

- **净结论：当前 HEAD 状态健康**。批内 4 条"引入→修复"链均闭环：
  `2ef68f9`→`f38b2bc`（构建脚本曾未入库）、`b768aca`→`98d370b`（睡眠取消
  定时器句柄丢弃曾致取消无效）、`dac7902`→`cedaee8`（键盘突发丢字 +
  INT_STAT 误当读清）、`0a6b3c3`→`fa6b989`+`48fd761`（entry 内 LVGL 重入等待）。
  以上均**已被后续 commit 修复，登记不追溯**。
- **待办 2 项（Low）**：
  1. `7d5aa8d`（`ui_deckpro.cpp:2314`）：临界区内"事件抢先发布"复检为死代码
     （刚设 `target=cnt` 后立即判 `cnt>target` 永假）——建议删除或移到
     `esp_wifi_scan_stop()` 之后；
  2. `fbfc16c`（`ui_weather.cpp:576-579`）：'r' 键先清 `last_fetch_time` 再调
     `start_fetch()`，若 WiFi 断/key 缺/已有任务在飞，缓存永久过期、每次进屏
     空重试——建议只在任务真正创建后才清时间戳。
- **跨批登记（Medium，契约层）**：weather fetch 任务不在
  `async_ipc_contract.md` 契约表内也不遵守契约（无页面代次，
  `weather_cleanup()` 直接 `vTaskDelete` 强杀在飞任务——HTTPClient/Preferences
  自动对象栈上存活时被释放；此为**先于本批存在**的问题）。建议：天气纳入契约
  （代次 + busy_gen、任务跑完丢弃迟到结果），或在契约中显式登记为例外。

### 2.3 B3 评审结论（2026-08-22，Kimi；AI Chat 21 commit）

- **净结论：HEAD 无遗留 High/Medium**。契约是迭代收敛出来的：
  `2a83d0d`→`327765f`→`04baf20`→`ddfee0a` 每一跳都在修前一轮的真实竞态/生命周期
  bug（跨核 std::string 交接、busy 先于队列创建、陈旧结果误清 busy、共享 prompt
  buffer、移交后读 `rq` 的 UAF 等，共 9 项已全部被后续 commit 修复，登记不追溯）。
- **HEAD 发送路径合规**：`new` 快照 → 任务自删请求 → 结果 `new` → `xQueueSend`
  → UI `delete`；gen 门控 busy；队列先于 busy 创建检查；取消 = gen+1。
- **承重细节（勿动）**：`factory.ino:773-790` 无条件调用所有屏的
  `*_keyboard_poll()`——chat 的队列排空在屏被覆盖时也跑，这是回复在
  push-away 期间到达时 busy 不锁死的原因；未来改动须保持队列排空在
  `chat_kbd_active` 早退**之前**。
- **待办 3 项（Low）**：
  1. F12 `chat_exit()` 隐藏 waitbox 但未隐藏 New 确认框（`chat_confirm_close()`
     仅 destroy 调用）——同类 push-away 泄漏，对称修复一行；
  2. F13 Chat 页 `b47dd4c` 分支无 `c >= ' '` 守卫，`'\v'` 已修（`a9873bd`）但
     其他控制字节仍会写进草稿并发往 API；
  3. F14 `955a492`：`disp_flush()`/`flush_timer_cb()` 在抑制期强制
     `DISP_REFR_MODE_PART`，会覆盖触摸滚动期间其他屏发的全刷请求（降级为局刷、
     自愈）——建议仅在非 FULL 时设 PART。
- SPIFFS 崩溃窗已完备（tmp→flush→bak→校验和，loader 提升 .bak、CHL1/CHL2 双解析）；
  残留无害项：崩溃遗留的 `/chat.log.tmp` 不清理（下次保存覆盖）。


## 3. 评审口径（恢复时沿用）

- 只报真实缺陷：跨任务/LVGL/定时器边界的竞态、use-after-free、gen/busy 处理
  是否符合 `docs/async_ipc_contract.md`、缓冲区溢出、非 LVGL 线程调 LVGL、
  逻辑错误、资源泄漏、错误路径。不报风格/命名/commit message 问题。
- 同批内后被修复的问题标注 superseded，不重复计。
- 产出格式：每 commit 一行结论（clean|issue|superseded|docs-only）→ Findings
  编号列表（severity + hash + file:line + 影响 + 最小修复）→ Cross-cutting。

## 4. 相关已完成的评审

- 笔友（PenPal）设计稿评审已完成并归档：
  [penpal-design-review-result-kimi.md](penpal-design-review-result-kimi.md)
  （结论 C 部分接受，3 项前置条件：§6 page_num 公式、§2 LLM 超时、§3.2 busy_gen）。
  该评审同时登记了与前次评审 penpal-design-review-result.md 的三处实质分歧。
- **v2 复审（2026-08-22）**：
  [penpal-design-review-result-kimi-v2.md](penpal-design-review-result-kimi-v2.md)
  ——**A 全量接受**，3 项 High 前置全部落实，附 4 项 Low 登记（实现期补登）。
- **4 份申请评审（2026-08-22，均 A 全量接受）**：
  `wifi-config-keyboard-review-result-kimi-a924c4e.md`（SD 提示）、
  `...-kimi-3f654a5..4c3a331.md`（P2 三连）、`...-kimi-de78338.md`（幽灵页）、
  `...-kimi-c27cb39..3475c9b.md`（GPT 批）；其中 950fcfe/52f709e/6d26699/
  23030c9 已覆盖，B2/B4 批内跳过。
- 注意：08-21 01:44 `5121ca1` 为 filter-repo 历史清洗记录，本文所有 commit 哈希
  以清洗后当前 HEAD 为准（清点于清洗之后，均有效）。

## 5. 恢复方式

**2026-08-22 断点**：B1/B2/B5 三轮代理均因额度 403 失败（无产出），代理上下文
保留——明天额度恢复后直接 `Agent(resume=...)` 即可，无需重新清点：

- B1（WiFi/TLS 20）→ resume **agent-8**
- B2（AI Config 24，批内跳过 950fcfe）→ resume **agent-9**
- B5（phase-0 试点 2 大 commit）→ resume **agent-12**

B3/B4 已完成（§2.2/§2.3），其 5 项 Low + 1 项契约层 Medium 已登记
`docs/issue_list.md` §11。全部批次完成后汇总出总结果文档
（建议命名 `2026-08-commits-review-result-kimi.md`），并把本状态文档收尾。

若决定缩小范围（只审未经过既有 codex/copilot 评审轮次的 commit），先对照
`docs/reviews/` 的 wifi-config-keyboard-review-request-*.md 覆盖区间剔除已审范围。
