# 第 27 轮整改评审结果（Codex）— Weather 手动刷新/城市跟随 + Shutdown 确认框

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..b9b1ed4.md](wifi-config-keyboard-review-request-e08bdac..b9b1ed4.md)
- **关联代码范围**：`e08bdac..b9b1ed4`
- **本次重点新增范围**（e08bdac..0e78025 的 12 个代码 commit 已由第 22–26 轮 Codex 结果
  全量接受，见 [e08bdac..1f46630](wifi-config-keyboard-review-result-codex-e08bdac..1f46630.md)、
  [e08bdac..0e78025](wifi-config-keyboard-review-result-codex-e08bdac..0e78025.md)）：
  - `b9b1ed4` — weather `r` 手动刷新 + 城市名每次 fetch 重查；Shutdown 盲关机改确认框（用户反馈）
  - doc-only：`0e84cf4`（reviews 注册 + 归档第 26 轮结果）
- **评审结论**：**A 全量接受**（1 个新代码 commit + 1 个 doc-only commit 接受；
  附 1 项 High 流程项 P1 + 3 项 Low 跟踪项，均不阻断代码合入）

---

## 1. Findings

### 1.1 沿用（前四轮已闭合，不再展开）

- e08bdac / 8770a41 / f4449c3 / 0b43685 / cc94452 / 06a2c13 / f3e1698 / 7ecebcd / 1f46630
  （第 22–25 轮）；b7c2a87 / ece4079 / 0e78025（第 26 轮，含 M1 掩码跟踪项）

### 1.2 b9b1ed4-A Weather `r` 手动刷新 + 城市名跟随坐标 — ✅ 通过

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_weather.cpp`（weather_keyboard_poll `r` 分支、weather_fetch_task 成功段）
- **机制核查**：
  - `r` → `last_fetch_time = 0; start_fetch();`。`cache_is_fresh()` 对 `last_fetch_time == 0`
    显式返回 false → 旁路 1h 缓存**必然生效**；`start_fetch` 内 WiFi/Key/`fetch_task` 三重守卫保留
  - 刷新失败时 `last_fetch_time` 保持 0 → 下次进屏自动重试——失败语义合理
  - 城市名改为**每次 fetch 无条件** `fetch_city_name(lat, lon, key)`（原 `location_name[0]=='\0'`
    条件移除）——根治"SF fallback 时代的 San Carlos 缓存名粘住深圳坐标"用户反馈
  - `fetch_city_name` 内 `strncpy(location_name, ..., 63)` 对 `char[64]` 静态零初始化安全
- **观察**（Low，不阻断）：geo 反查失败（HTTP/空数组）时旧 `location_name` 保留——
  极端场景（换坐标当次 geo 恰好失败）仍可能短暂显示旧城名，下次成功 fetch 自愈；
  主诉问题已闭合，可接受

### 1.3 b9b1ed4-B Shutdown 确认框替换盲式 2s 自动关机 — ✅ 通过

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_deckpro.cpp:3614+`（screen9 重写）、`examples/pda2/factory.ino:777`（loop 挂 poll）
- **机制核查**：
  - 非 USB 分支：启动图 + 返回键 + `lv_layer_top()` 确认框（OK/Cancel 按钮，
    文案 "Shut down now?\n(Enter=OK, any key=Cancel)"）+ `shutdown_kbd_active` 全局 poll
  - 键盘语义：Enter=accept（删框→`ui_shutdown_on()`）；**任意其它键=cancel（删框→pop 回菜单）**；
    触摸 Cancel/返回键=cancel，OK=accept——与"误入不断电"用户要求一致
  - **生命周期对照 `examples/factory/ui_scr_mrg.c` 逐路径核验**：
    - `scr_mgr_pop` → exit9 + destroy9（remove 对 ACTIVE 卡片两者皆调）→ 框删除 ✓
    - `scr_mgr_switch` → 全栈 remove → destroy9 → 框删除 ✓
    - push 路径：screen9 上**不存在** push 入口（屏内仅返回键/确认框按钮，键盘被
      shutdown_keyboard_poll 全量消费；Sleep 是菜单显式屏 screen11，不会盖到 screen9 上）
      → "exit9 不删框"在当前拓扑下不可达
  - **删除时机**：按钮回调内 `lv_obj_del` 祖先对象——与既有 msgbox 模式一致
    （`ui_ai_cfg.cpp:87` 同款），真机已验证模式，无新增风险
  - **低电紧急关机不受影响**：`ui_deckpro.cpp:172` 低压倒计时到点直接 `ui_shutdown_on()`，
    正确绕过确认（电池保护优先）✓
  - USB 分支保持原样（提示 + 返回键，无关机路径）✓；旧 `lv_timer_create(shutdown_timer_event, 2000, ...)`
    盲关机已彻底移除
  - `exit9`/`destroy9` 均清 `shutdown_kbd_active`；destroy9 兜底删框——无泄漏/无双删（删前判空+置 NULL）
- **观察**（均 Low，不阻断，进跟踪表）：
  1. **对称性隐患**：`exit9` 不删框、`entry9` 不恢复 `shutdown_kbd_active`。当前无 push 路径，
     不可触发；未来若新增"从 screen9 push"的功能，需同步补 exit9 删框/entry9 恢复标志
  2. **双 Enter 边缘**：进屏瞬间 FIFO 里若已有一颗 Enter（菜单连按），会被当作确认直接关机。
     已核查 `peri_keypad` 无 auto-repeat 配置，风险仅限用户快速双按；严格优于旧 2s 盲关机。
     可选加固：create9 后忽略首个键或加 200ms 静默窗——建议真机体验后决定
  3. **拼写**：USB 分支返回键标签仍为 `"Shoutdown"`（非 USB 分支已修正为 "Shutdown"）——
     纯外观，顺手可改

### 1.4 P1（High，流程项，不阻断代码）— 真机回归清单连续两轮未更新且全 ⏸

- 评审纪律（`docs/reviews/README.md`）明文："连续两轮全 ⏸ 会被评审标 High 流程问题"。
  上一轮（e08bdac..0e78025）§3 五项全 ⏸ 且已指出**缺 Weather/Secrets 待测项**；
  本轮申请书仅追加 §1.13 变更说明，§3 清单**未新增任何 b9b1ed4 项**且依旧全 ⏸：
  - 缺：`r` 手动刷新（状态条 Fetching → 数据更新）；城市名跟随深圳坐标（不再是 San Carlos）
  - 缺：Shutdown 确认框四路径（Enter 关机 / 任意键返回 / Cancel 钮返回 / 返回键返回）；
    USB 插着进 Shutdown 屏只见提示无关机
  - 缺（上轮遗留）：Weather 三页内容 + `+`/`-` 翻页 + UV:-- + 无 GPS 深圳回退；空 NVS 时 AI Key 走 config_keys.h
- **根因**：真机测试瓶颈在用户侧（设备已烧录，待用户按键），非申请人代码问题；
  但申请书提交前自查义务在申请人。按纪律标 **High 流程项**，要求下轮申请书 §3
  必须携带完整待测清单（含 b9b1ed4 与上两轮 Weather/Secrets 项）并逐轮回填实测结果
- 另：申请书头部"本轮 4 个"、§2 "CA bundle 5 根（沿用）"两处陈旧表述连续两轮未更新
  （实为 13 个 commit、6 根证书）；按 README 规则 4，本申请文件名宜为 `0e78025..b9b1ed4`

---

## 2. 已通过项汇总

### 本轮新增（第 27 轮）
- **b9b1ed4** Weather `r` 手动刷新（cache_is_fresh 旁路验证）+ 城市名每次 fetch 重查；
  Shutdown 确认框（scr_mgr 全路径生命周期核验通过；低电路径正确绕过）
- **0e84cf4** doc-only：申请范围重命名注册 + 第 26 轮结果归档

### 沿用
- e08bdac..0e78025（12 个代码 commit，第 22–26 轮接受）+ 历史 844a907..156732c（28 commit）

---

## 3. 跟踪项（继承 + 本批新增）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| **P1：回归清单连续两轮全 ⏸ 且缺新项** | 本轮 §1.4 | **High 流程项，下轮必须整改** |
| exit9/entry9 对称性（若未来出现 screen9 push 路径） | 本轮 §1.3 观察 1 | 挂起 |
| 双 Enter 即关机边缘（可选首键静默窗） | 本轮 §1.3 观察 2 | 真机体验后决定 |
| USB 分支 "Shoutdown" 拼写 | 本轮 §1.3 观察 3 | 顺手改 |
| M1：3 份评审文档明文旧 Key 掩码 | 第 26 轮 | 推公网前必做 |
| L1：config_keys.h.example 补 AI_KEY_DEFAULT_DEV；L2：TODO.md 46/47 打勾 | 第 26 轮 | 待办 |
| wifi_scan_overlay 跨 push 屏残留（exit4_1 hide） | 第 25 轮 | 待办 |
| SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | 主评审/TODO | 阶段 1 |

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方本轮复跑）
- 静态复核：`git show b9b1ed4 0e84cf4` 全 diff 逐段核查；`ui_scr_mrg.c` push/pop/switch
  三路径对 screen9 生命周期（create/entry/exit/destroy）逐一对照；`cache_is_fresh()` 旁路验证；
  loop() poll 顺序与守卫模式核查；低电关机路径（ui_deckpro.cpp:172）确认不受影响；
  keypad 无 auto-repeat 配置确认
- 编译：评审环境无 `pio`，采信申请人 `pio run -e pda2` SUCCESS + COM5 烧录 Hash verified（沿用前轮做法）
- 真机回归：全部 ⏸ 待用户（清单缺口见 P1）
- 本结果文档不含任何 Key 正文

## 5. 审批意见

- [x] **A. 全量接受** — b9b1ed4 + 0e84cf4 接受，关闭本轮代码评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：Weather 手动刷新旁路逻辑经 `cache_is_fresh()` 源码验证必然生效；城市名重查根治
缓存名粘滞用户反馈；Shutdown 确认框在 scr_mgr 全路径下生命周期闭合、低电保护不受影响、
删除模式与既有 msgbox 一致。两项用户反馈精准闭合。P1 为流程整改项，不阻断代码。

**遗留项**：
- P1：下轮申请书必须携带完整回归清单（含 b9b1ed4 四路径 + Weather/Secrets 继承项）并回填实测
- 第 26 轮 M1/L1/L2 继续跟踪

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：`git show b9b1ed4` 全 diff 追踪、
ui_scr_mrg.c 三路径生命周期对照、cache_is_fresh 旁路验证、低电关机路径排查、
keypad repeat 配置核查、NVS 测试复跑 11/11）