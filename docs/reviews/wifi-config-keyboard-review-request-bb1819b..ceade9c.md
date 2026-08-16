# 评审申请书：第 17/18 轮评审整改（第二十次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `bb1819b` — `pda2: sleep screen countdown + cancellable timer`
  - `6da7fe2` — `pda2: wifi scan release target + busy generation + queue checks`
  - `84090c1` — `pda2: rewrite CA bundle check in python (byte-exact PEM extraction)`
  - `fb2ac62` — `pda2: openai config - atomic NVS save, test timeout, wording`
  - `3bc255f` — `pda2: AI Config - test via minimal chat-completion + save UX`
  - `d21bd18` — `pda2: AI Chat - per-task request snapshot + UTF-8 truncation`
  - `e210b46` — `pda2: http_utils - document the User-Agent policy`
  - `ceade9c` — `docs: async IPC contract + archive review rounds into docs/reviews/`
- **评审依据**（4 份结果，全部 Findings 已逐条处理，Key 项除外）：
  - [主评审 bcca4d2（Sleep 屏）](wifi-config-keyboard-review-result-bcca4d2.md)
  - [Copilot 复审 bcca4d2](wifi-config-keyboard-review-result-bcca4d2-copilot.md)
  - [主评审 01f8eac..8b96656](wifi-config-keyboard-review-result-01f8eac..8b96656.md)
  - [Copilot 复审 01f8eac..8b96656](wifi-config-keyboard-review-result-01f8eac..8b96656-copilot.md)
- **历史文档**：前十九轮申请与结果见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

按上述 4 份评审结果的 Findings 逐项整改（共 20 项：Sleep 6 + 主评审 11 + Copilot 10，重叠项合并）。**Critical Key 项仍按用户决策延后**（见 §5 遗留项），其余全部在本批 8 个 commit 中关闭。

## 2. 变更明细（按模块，一个模块一个 commit）

### 2.1 Sleep 屏（`bb1819b`）— 对应 bcca4d2 全部 Findings

| 评审项 | 处理 |
|---|---|
| 主 1.1 无倒计时 | 1s tick 倒计时：`Sleep in: 2` → `1` → 深睡；仅秒变化时更新文本（EPD 友好，共 2 次更新，残影可忽略） |
| 主 1.2 ext0/ext1 注释不一致 | 删除误导性 `ext0(ENCODER_KEY)` 注释行；代码与注释统一为 `esp_sleep_enable_ext1_wakeup(1UL << BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ANY_LOW)`（BOOT 键） |
| 主 1.3 timer 竞态 | 回调顶部 `if (sleep_timer == NULL) return;` 守卫（评审建议的修复方案原文）；exit11/destroy11 先置 NULL 再 `lv_timer_del` |
| 主 1.4 修饰键 sleep→wake 残留 | **结构性免疫**：唤醒 = 深睡复位 → 冷启动 → `setup()` 重建一切、TCA8418 重新初始化，修饰键状态不可能跨深睡存活（代码注释已写明）；无需显式清空 |
| 主 1.6 任务栈说明 | Sleep 屏不创建 FreeRTOS 任务（LVGL timer 运行于 UI 线程），已在 commit message 说明 |
| Cop 1.1 句柄未保存 | `entry11`：`sleep_timer = lv_timer_create(...)`；进入前先删旧 timer |
| Cop 1.2 非一次性 | `lv_timer_set_repeat_count(sleep_timer, 4)` 兜底；回调进入 sleep 前 `sleep_timer = NULL; lv_timer_del(t)` |
| Cop 1.3 计时从 create 开始 | timer 改在 `entry11`（`ui_disp_full_refr()` 之后）启动，3s 从画面实际显示后开始计 |

### 2.2 WiFi 扫描生命周期 + Test/Time Sync busy 代次（`6da7fe2`）— 对应 Cop 1.3/1.4/1.5

- **Cop 1.3 迟到 SCAN_DONE 永久 pending**：abort 超时时记录目标计数 `s_scan_release_target = prev`；事件回调中 `cnt > target` 即自行清除 pending —— 重试路径不再重新建立基线，迟到事件在重试前到达也能解除阻塞
- **Cop 1.4 旧代结果释放 busy**：busy 携带代次（`s_wifi_test_busy_gen` / `s_time_sync_busy_gen`），只有代次匹配的结果才能释放 busy；旧代结果照常丢弃
- **Cop 1.5 队列 NULL**：WiFi Test 与 Time Sync 两处 `xQueueCreate` 返回值检查，失败即提示且不置 busy、不启动任务

### 2.3 CA 检查脚本（`84090c1`）— 对应 Cop 1.2

- 原 shell/awk 版在跨 shell 反斜杠转义下会提取到零张证书（本地 Git Bash 实测：`gsub(/\\n/, sprintf("%c",10))` 从文件读入时不生效）→ 重写为 Python：`chr(10)` 构造换行字节，逐张 PEM 用 `openssl x509` 解析，**任何一张失败即中止**
- 实测：5 张根证书（ISRG X1/YR、DigiCert G2、GlobalSign R3、GTS R4）全部 PASS（输出见 §3）

### 2.4 openai 配置层（`fb2ac62`）— 对应 Cop 1.8、主 1.6/1.9

- **Cop 1.8 NVS 非原子写**：改为"暂存-校验-换入"三步：新值先写 `*.tmp` 键并读回校验 → 全部成功才覆盖正式键 → 任一失败回滚旧值。保存失败不再产生混合配置
- **主 1.6 拼写**：`examer` → `examiner`；头注释明示 "v1 固定 system prompt，NVS 化随 cfg_version 迁移一起做"
- **主 1.9 schema**：`openai_load_config` 顶部 TODO(cfg_version) 注释
- 顺带：`openai_chat` 增加显式 `timeout_ms` 参数（Test 用 10s、Chat 用 30s）

### 2.5 AI Config 屏（`3bc255f`）— 对应 Cop 1.7/1.9/1.5、主 1.4（主 1.5 随之消解）

- **Cop 1.7 Test 不验 Model**：Test 改为对草稿 base/model/key 发起**最小 chat-completion**（`openai_chat("ping", ...)`），回复到达即证明三元组可用；msgbox 显示回复前 70 字符。**主 1.5 的 /models URL 推导 fallback 问题随该方案整体消失**（不再派生 URL）
- **Cop 1.9 超时不完整 / 不失效代次**：UI 倒计时改为**绝对 deadline = 15s**（HTTP 10s + 内部 NTP 等待最坏 5s，`http_ensure_time` 已同步时 0 开销）；超时触发时**递增请求代次**，迟到结果进入 stale 分支丢弃，不覆盖超时提示
- **Cop 1.5**：Test 队列创建 + 检查
- **任务快照**（Cop 1.6 同款范式）：Test 任务持有草稿 base/model/key 的自有副本，请求中编辑不污染在飞请求
- **主 1.4 Save UX**：状态行始终说明 Save 为何被挡——未测过 `Run Test to enable Save`、编辑后 `Test stale`（编辑任意字符立即提示）、通过后 `Test OK - ready to Save`

### 2.6 AI Chat 屏（`d21bd18`）— 对应 Cop 1.6/1.10/1.5

- **Cop 1.6 共享 prompt 缓冲 / busy 错放**：发送任务持有 `chat_send_req_t`（prompt + base/model/key）自有快照，全局 `chat_prompt_buf` 删除；离页重入时旧任务读不到新正文；**只有代次匹配当前页面的结果才能清 busy**，旧代回复解锁新请求的路径关闭
- **Cop 1.10 UTF-8 截断**：`chat_history_add` 截断前回退到码点边界（越过 continuation byte），并追加显式 `(truncated)` 标记
- **Cop 1.5**：Chat 队列创建 + 检查

### 2.7 http_utils（`e210b46`）— 对应主 1.10

- `http_get_ua` 文档注释明示 UA 策略：UA 仅经此函数按端点注入；其余调用发送 HTTPClient 默认头，无全局 curl UA

### 2.8 文档（`ceade9c`）— 对应主 1.3

- 新建 `docs/async_ipc_contract.md`：队列深度/阻塞策略、destroy 与 Close 的清理路径、每任务快照、busy 代次、任务栈/优先级一致（4 个任务表格化）、10 条硬性规则 —— 后续任何异步任务以此为准
- allinone 设计评审两文档移入 `docs/reviews/`；第 17/18 轮 4 份结果归档

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS；RAM 47.5% / Flash 30.1% |
| 烧录 | ✅ 完成 | COM5，Hash verified（新固件已上机） |
| CA bundle 检查 | ✅ 通过 | 5 张根证书逐张 openssl 解析 OK（PASS 输出见脚本运行） |
| 真机回归（清单见 §4） | ⏸ 待用户配合 | 主 1.11 要求；本次申请后由用户逐项实测 |

## 4. 真机回归清单（⏸，等待用户实测）

1. **Sleep**：点 Sleep → 显示 `Sleep in: 2` → `1` → 黑屏深睡；串口静默
2. **Sleep 取消**：倒计时 1-2s 内按 Back → 回菜单且不深睡
3. **Sleep 唤醒**：按 BOOT 键 → 开机画面 → 版本号 → 主菜单（键盘/修饰键正常）
4. **AI Config**：进屏状态行显示 `Run Test to enable Save`；编辑任一字段 → 状态行变 `Run Test to enable Save`（Test 失活）
5. **AI Config Test**：点 Test → msgbox `Testing... 15s` 倒计时 → `Test OK: <回复前70字>`；Save 前状态行 `Test OK - ready to Save` → Save → `Saved`
6. **AI Config 取消**：Test 进行中点 Close → 回配置屏；迟到结果被丢弃（串口 `stale test result dropped`）
7. **AI Chat**：发送成功 → 输入框清空、AI 回复左对齐追加、自动滚底
8. **AI Chat 失败保留草稿**：关 WiFi 后发送 → 状态行 `AI error...`、草稿仍在输入框可重试
9. **长回答截断**：>255 字节回答 → 末尾 `(truncated)` 且无乱码
10. **WiFi Test 连按**：进行中连按 5 次重试 → 无崩溃、busy 正确拒绝

## 5. 遗留项（明示）

- **Critical：真实 API Key 仍在 `openai_api.h`**（主 1.1 / Cop 1.1）——**用户决策：暂不修复，用户已自行去 OpenRouter 后台 revoke 已泄露 Key**。本批继续延后；评审要求：下次必须见 Key 字符串从 git HEAD 移除（或占位符替代），并附 `git filter-repo` 计划 / 通告
- **主 1.6 system prompt NVS 化**：评审建议随 AI Config 下一轮重构一起处理（本批仅修拼写 + 注释留痕）
- **主 1.11 / bcca4d2 主 1.5 真机回归**：§4 清单待用户实测，结果将写入后续申请

## 6. 回滚方案

```bash
git revert ceade9c e210b46 d21bd18 3bc255f fb2ac62 84090c1 6da7fe2 bb1819b
```

8 个 commit 按模块拆分，任一 commit 可独立 revert，中间态均可独立编译。

## 7. 申请审批事项

- [ ] **A. 全量接受** — 保留全部 commit，关闭本轮评审循环（Key 项按 §5 单独跟踪）
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
