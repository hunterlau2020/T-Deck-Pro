# 评审申请书：AI Text 聊天界面 + 第 17/18 轮评审整改（合并申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（与文件名对应，共 9 个）：
  - `eecebda` — `pda2: restructure AI Text as a chat UI (history + input + side buttons)`（Part 1）
  - `2875efb` — round 19 申请文档
  - `bb1819b` — `pda2: sleep screen countdown + cancellable timer`
  - `6da7fe2` — `pda2: wifi scan release target + busy generation + queue checks`
  - `84090c1` — `pda2: rewrite CA bundle check in python (byte-exact PEM extraction)`
  - `fb2ac62` — `pda2: openai config - atomic NVS save, test timeout, wording`
  - `3bc255f` — `pda2: AI Config - test via minimal chat-completion + save UX`
  - `d21bd18` — `pda2: AI Chat - per-task request snapshot + UTF-8 truncation`
  - `e210b46` — `pda2: http_utils - document the User-Agent policy`
  - `ceade9c` — `docs: async IPC contract + archive review rounds into docs/reviews/`
- **合并说明**：按工作流约定，本申请合并吸收两份未评审申请——round 19（`wifi-config-keyboard-review-request-eecebda.md`，AI Text 聊天界面）与 round 20（`wifi-config-keyboard-review-request-bb1819b..ceade9c.md`，第 17/18 轮整改），评审一次覆盖。原文件随本申请删除，git 历史保留（不覆盖）。
- **评审依据**（Part 2 逐条对应的 4 份结果）：
  - [主评审 bcca4d2（Sleep 屏）](wifi-config-keyboard-review-result-bcca4d2.md) / [Copilot 复审](wifi-config-keyboard-review-result-bcca4d2-copilot.md)
  - [主评审 01f8eac..8b96656](wifi-config-keyboard-review-result-01f8eac..8b96656.md) / [Copilot 复审](wifi-config-keyboard-review-result-01f8eac..8b96656-copilot.md)
- **历史文档**：前十九轮申请与结果见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

# Part 1 — AI Text 聊天界面重构（`eecebda`）

## 1.1 变更明细（`examples/pda2/ui_ai_chat.cpp`）

### 布局（wireframe）

```
┌──────────────────────────────┐
│ back "AI Text"               │
│ ┌──────────────────────────┐ │  164px（2/3）
│ │ 历史记录（只读可滚动）    │ │
│ │ ┌────────────────┐       │ │
│ │ │ AI 回复（左对齐）│       │ │
│ │ └────────────────┘       │ │
│ │       ┌────────────────┐ │ │
│ │       │ 用户输入（右对齐）│ │ │
│ │       └────────────────┘ │ │
│ └──────────────────────────┘ │
│ 状态行（Thinking.../错误）    │  16px
│ ┌──────────────────────┐┌──┐│
│ │ 当前输入（多行 82px）  ││Send│ 44px 侧栏
│ └──────────────────────┘│──┤│
│                        │Clear│
└──────────────────────────────┘
```

- 历史容器：`LV_SCROLLBAR_MODE_AUTO` 垂直滚动（触摸原生滚动 + Sym/Alt `+`/`-` 键盘滚动 ±120px）
- 气泡：白底黑边圆角，宽 178px，label 170px `LV_LABEL_LONG_WRAP`（LVGL 按 UTF-8 码点自动换行）；用户消息 `ALIGN_TOP_RIGHT`、AI 回复 `ALIGN_TOP_LEFT`
- 渲染后 `lv_obj_scroll_to_y(LV_COORD_MAX)` 自动滚到最新
- Send/Clear：44×38 小按钮竖排在输入框右侧；输入框 176×82 多行（上限 200 字符）

### 数据与行为

- 历史：`chat_msg_t[40]` 固定数组滚动存储（满 40 条丢最旧），**内存态**（重启清空，未持久化——需要可后续加 NVS/文件）
- 发送：用户消息**立即进历史**（聊天语义）；发送为异步任务 + 队列 + 页面代次；**成功才清空输入框**，失败草稿保留可重试
- 键盘：`\n` 发送、`\b` 删字/空框退屏、`+`/`-` 滚历史、`\t`/`\v` 忽略；发送中吞键
- 删除的旧逻辑：分页浏览态、固定字节断行、页内回答标签——全部由滚动历史替代

# Part 2 — 第 17/18 轮评审整改（`bb1819b..ceade9c`）

## 2.1 Sleep 屏（`bb1819b`）— 对应 bcca4d2 全部 Findings

| 评审项 | 处理 |
|---|---|
| 主 1.1 无倒计时 | 1s tick 倒计时：`Sleep in: 2` → `1` → 深睡；仅秒变化更新文本（EPD 友好） |
| 主 1.2 ext0/ext1 注释不一致 | 删除误导性 ext0 注释行；代码与注释统一为 `esp_sleep_enable_ext1_wakeup(1UL << BOARD_BOOT_PIN, ESP_EXT1_WAKEUP_ANY_LOW)`（BOOT 键） |
| 主 1.3 timer 竞态 | 回调顶部 `if (sleep_timer == NULL) return;` 守卫；exit11/destroy11 先置 NULL 再 `lv_timer_del` |
| 主 1.4 修饰键 sleep→wake 残留 | **结构性免疫**：唤醒 = 深睡复位 → 冷启动 → `setup()` 重建一切、TCA8418 重新初始化，修饰键状态不可能跨深睡存活（代码注释已写明） |
| 主 1.6 任务栈说明 | Sleep 屏不创建 FreeRTOS 任务，commit message 已说明 |
| Cop 1.1 句柄未保存 | `entry11`：`sleep_timer = lv_timer_create(...)`；进入前先删旧 timer |
| Cop 1.2 非一次性 | `lv_timer_set_repeat_count(sleep_timer, 4)` 兜底；进 sleep 前 `sleep_timer = NULL; lv_timer_del(t)` |
| Cop 1.3 计时从 create 开始 | timer 改在 `entry11`（`ui_disp_full_refr()` 之后）启动 |

## 2.2 WiFi 扫描生命周期 + Test/Time Sync busy 代次（`6da7fe2`）

- **Cop 1.3 迟到 SCAN_DONE 永久 pending**：abort 超时记录目标计数 `s_scan_release_target`；事件回调中 `cnt > target` 自行清除 pending，重试不重建基线
- **Cop 1.4 旧代结果释放 busy**：busy 携带代次（`*_busy_gen`），只有代次匹配的结果才能释放 busy
- **Cop 1.5 队列 NULL**：两处 `xQueueCreate` 返回值检查，失败即提示且不置 busy、不启动任务

## 2.3 CA 检查脚本（`84090c1`）— 对应 Cop 1.2

- shell/awk 版在跨 shell 反斜杠转义下提取到零张证书（本地复现）→ 重写为 Python：`chr(10)` 构造换行，逐张 PEM 用 `openssl x509` 解析，任一失败即中止；实测 5 根证书（ISRG X1/YR、DigiCert G2、GlobalSign R3、GTS R4）全部 PASS

## 2.4 openai 配置层（`fb2ac62`）— 对应 Cop 1.8、主 1.6/1.9

- **Cop 1.8 NVS 非原子写**：暂存-校验-换入三步（`*.tmp` 键写读回校验 → 覆盖正式键 → 失败回滚旧值），保存失败不再产生混合配置
- **主 1.6 拼写**：`examer` → `examiner`；注释明示 v1 固定 system prompt
- **主 1.9 schema**：`openai_load_config` 顶部 TODO(cfg_version)
- `openai_chat` 增加显式 `timeout_ms` 参数（Test 10s / Chat 30s）

## 2.5 AI Config 屏（`3bc255f`）— 对应 Cop 1.7/1.9/1.5、主 1.4（主 1.5 随之消解）

- **Cop 1.7 Test 不验 Model**：Test 改为对草稿 base/model/key 发起**最小 chat-completion**（`openai_chat("ping", ...)`），回复到达即证明三元组可用；msgbox 显示回复前 70 字符。**主 1.5 的 /models URL 推导 fallback 随该方案整体消失**
- **Cop 1.9 超时**：UI 倒计时改为**绝对 deadline = 15s**（HTTP 10s + NTP 最坏 5s）；超时时**递增请求代次**，迟到结果丢弃
- **Cop 1.5**：Test 队列创建 + 检查
- **任务快照**：Test 任务持有草稿自有副本，请求中编辑不污染在飞请求
- **主 1.4 Save UX**：状态行始终说明 Save 为何被挡（`Run Test to enable Save` / 编辑后失活 / `Test OK - ready to Save`）

## 2.6 AI Chat 屏（`d21bd18`）— 对应 Cop 1.6/1.10/1.5

- **Cop 1.6 共享 prompt 缓冲 / busy 错放**：发送任务持有 `chat_send_req_t`（prompt + base/model/key）自有快照，全局缓冲删除；**只有代次匹配当前页面的结果才能清 busy**
- **Cop 1.10 UTF-8 截断**：截断前回退到码点边界，追加显式 `(truncated)` 标记
- **Cop 1.5**：Chat 队列创建 + 检查

## 2.7 http_utils（`e210b46`）— 对应主 1.10

- `http_get_ua` 文档注释明示 UA 策略：UA 仅经此函数按端点注入，无全局 curl UA

## 2.8 文档（`ceade9c`）— 对应主 1.3

- 新建 `docs/async_ipc_contract.md`：队列深度/阻塞策略、destroy 与 Close 清理路径、每任务快照、busy 代次、任务栈/优先级一致（4 个任务表格化 + 10 条硬性规则）
- allinone 设计评审两文档移入 `docs/reviews/`；第 17/18 轮 4 份结果归档

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS；RAM 47.5% / Flash 30.1% |
| 烧录 | ✅ 完成 | COM5，Hash verified（Part 1 + Part 2 新固件均已上机） |
| CA bundle 检查 | ✅ 通过 | 5 张根证书逐张 openssl 解析 OK |
| 真机回归（§4） | ⏸ 待用户配合 | 主 1.11 要求；申请后由用户逐项实测 |

## 4. 真机回归清单（⏸，等待用户实测）

**AI Text 聊天界面（Part 1）**
1. 双框布局与按钮位置（历史 2/3 + 输入 1/3 + 右侧小按钮）
2. 消息左右对齐错开（AI 左、用户右）
3. 历史滚动（触摸 / `+`/`-` 键）
4. 发送 → 历史追加 → 回复追加（串口 `[AIChat] reply len=...`）

**第 17/18 轮整改（Part 2）**
5. **Sleep**：点 Sleep → `Sleep in: 2` → `1` → 黑屏深睡；串口静默
6. **Sleep 取消**：倒计时 1-2s 内按 Back → 回菜单且不深睡
7. **Sleep 唤醒**：按 BOOT 键 → 开机画面 → 版本号 → 主菜单（键盘/修饰键正常）
8. **AI Config**：进屏状态行 `Run Test to enable Save`；编辑任一字段 → Test 失活提示
9. **AI Config Test**：点 Test → `Testing... 15s` 倒计时 → `Test OK: <回复前70字>`；再 Save → `Saved`
10. **AI Config 取消**：Test 中点 Close → 迟到结果被丢弃（串口 `stale test result dropped`）
11. **AI Chat**：发送成功 → 输入框清空、回复左对齐、自动滚底
12. **AI Chat 失败保留草稿**：关 WiFi 后发送 → `AI error...`、草稿保留可重试
13. **长回答截断**：>255 字节回答 → 末尾 `(truncated)` 且无乱码
14. **WiFi Test 连按**：进行中连按 5 次重试 → 无崩溃、busy 正确拒绝

## 5. 遗留项（明示）

- **Critical：真实 API Key 仍在 `openai_api.h`**（主 1.1 / Cop 1.1）——**用户决策：暂不修复，用户已自行去 OpenRouter 后台 revoke 已泄露 Key**。下次评审必须见 Key 字符串从 git HEAD 移除（或占位符替代），并附 `git filter-repo` 计划 / 通告
- **主 1.6 system prompt NVS 化**：随 AI Config 下一轮重构一起处理（本批仅修拼写 + 注释留痕）
- **主 1.11 / bcca4d2 主 1.5 真机回归**：§4 清单待用户实测，结果写入后续申请

## 6. 回滚方案

```bash
git revert ceade9c e210b46 d21bd18 3bc255f fb2ac62 84090c1 6da7fe2 bb1819b 2875efb eecebda
```

commit 按模块拆分，任一 commit 可独立 revert，中间态均可独立编译。

## 7. 申请审批事项

- [ ] **A. 全量接受** — 保留全部 commit，关闭本轮评审循环（Key 项按 §5 单独跟踪）
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
