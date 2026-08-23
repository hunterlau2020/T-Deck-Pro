# 评审申请书：71fa528..a58a73c 评审 P2 修复（forecast 零条不判有效）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个）：
  - `141942d` — `weather: reject zero-slot forecast parse as data_valid`
- **背景**：Codex 结果 `wifi-config-keyboard-review-result-71fa528..a58a73c-codex.md`
  （2026-08-21）**C 部分接受**，1 项 P2（weather）；TLS 文案项（`a58a73c`）
  已通过。本 commit 关闭该 P2，评审出列。同批 `c8f62f3` 结果 **A 全量接受**，
  a924c4e 环路闭合。
- **命名说明**：文件名 = 正文"关联 commit"（含两端，非 git 区间记法）。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 问题（评审 P2 原文要点）

`71fa528` 在 `parse_forecast()` 尾部**无条件**置 `data_valid = true` 并返回
true——函数只验证了 JSON 内存在 `list`。`list` 为空、条目全部 `ts < now`
（已过期）、或条目缺 `dt` 时，循环全部 `continue`/不计数，
`hourly_count` 与 `daily_count` 均为 0，仍被判为 forecast 成功。冷启动 +
current 失败场景下，UI 会把零初始化的 current 数据当有效值上屏并标
partial，而不是保留失败态。

### 1.2 修复（`141942d`，收窄置位条件，无调用方改动）

- `parse_forecast()` 尾部改按 **`hourly_count > 0 || daily_count > 0`** 门控：
  - 计数 > 0：置 `data_valid = true`、返回 true（§9.4 的 partial 上屏语义
    保持不变——有解析出的槽位才值得上屏）；
  - 计数 == 0：不置位、返回 false，串口补一行
    `[Weather] forecast parse: 0 usable slots - rejected`。
- 返回 false 由既有调用方语义接住（`fetch_task` 的 `fc_ok` 分支）：归入
  partial（current 成功）或完全失败（双败）——**不推进 `last_fetch_time`、
  不 `save_cache()`**，下次进屏重试；零值天气不再可能上屏。
- `parse_current_weather`、`refresh_cb`/`update_ui` 门控、缓存路径均未动。

### 1.3 关于"补充解析测试"的说明（验证缺口登记）

评审建议补空列表 / 全过期 / 缺 `dt` 三类解析测试。本项目无实机测试框架
（CLAUDE.md："No live tests"，依赖真机手工回归），且 `parse_forecast` 深嵌
于 `ui_weather.cpp` 的 LVGL 静态上下文（文件级 static 数组 + 回调引用），
抽出为 PC 侧可编译单元需引入 stub 层，收益/成本比低——本项登记为验证
缺口，以代码路径复核 + §3 真机注入项代替；若后续评审坚持，可评估建
`scripts/` 下 PC harness。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS（21.4s，无新警告） |
| 烧录 | ✅ | COM5，SUCCESS |
| 开机冒烟 | ✅ | 串口 40s 无 panic/boot loop |
| 正常路径（计数 > 0） | ✅ | 代码路径复核：门控只收紧零条分支，非零路径与 `71fa528` 前一致 |
| 零条分支 | ⏸ | 需构造空 `list` 响应注入，留真机回归 §3-1 |

## 3. 真机回归清单

**本轮新增**
1. ⏸ Weather 正常刷新（双端点 200）：行为与之前一致——forecast 上屏、
     `ok: N hourly, M daily` 串口、无 rejected 行
2. ⏸ （可选注入）forecast 返回空 `list`：串口出现
     `0 usable slots - rejected` + `partial refresh`，**不显示零值天气**；
     current 正常时 current 照常上屏 + Partial 提示；再次进屏触发重试

**继承（未回归项顺延，与 71fa528..a58a73c 申请 §3 一致）**
3. ⏸ （可选）current URL 改错触发 partial：forecast 上屏 + 提示
4. ⏸ AI Config Trust 新文案排版；触摸 / `\v` / 重启保持
5. ⏸ SD exFAT 两行提示 / 拔卡 no card；GPS 读数 + altitude；菜单第 2 页
     左滑；#6/#14/#15

## 4. 遗留项（简要）

- `docs/reviews/` 待结果申请：本份（141942d）——`c8f62f3` 已 A 闭合、
  `71fa528..a58a73c` 已 C（P2 由本申请关闭）。
- 笔友 App：Codex v2 复审 P1 幂等键等服务端定稿 → v3；Kimi v2 四 Low 已
  铺入设计稿。
- CI 矩阵日志抽查（set_srcdir 修复验证）仍待做（本机无 gh CLI）。

## 5. 回滚方案

```bash
git revert 141942d
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
