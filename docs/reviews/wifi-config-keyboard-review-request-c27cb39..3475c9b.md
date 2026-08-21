# 评审申请书：第四批评审修复（GPT 跟进评审 3 项 P2）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个）：
  - `c27cb39` — `weather: do not cache a partial refresh as successful`（P2-1）
  - `153eef7` — `ci: run on script/** changes`（P2-2）
  - `3475c9b` — `factory: fix openai_tls_apply extern return type`（P2-3）
- **背景**：GPT 跟进评审 `pda2-review-result-2026-08-07-21-gpt.md`（2026-08-21 到达）
  3 项 P2，本批全部关闭，该评审出列。
- **已出列范围**（均 Codex 全量接受）：第 29-31 轮；`de78338`（菜单幽灵页）申请
  已递交待结果；`6d26699..1473ef9`（P2 三连）同。
- **命名说明**：文件名 = 正文"关联 commit"首末 id（**含两端**，非 git 区间记法）。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 Weather 部分刷新不再缓存为成功（`c27cb39`，P2-1）

- **问题**：fetch 任务结尾 `if (data_valid)` 只看一个标志——它既被**本次**解析
  置位、也被**更早的缓存加载**置位。current 失败但缓存有效、或 current 成功而
  forecast 失败，都会推进 `last_fetch_time` + `save_cache()` 新旧混合状态 →
  界面报成功、1 小时不再重试。
- **修复**：
  - `parse_current_weather` / `parse_forecast` 改为**返回 bool**（载荷是否解析成
    功）——HTTP 200 + 解析失败（垃圾正文）不算该端点成功；
  - fetch 任务跟踪 `cur_ok` / `fc_ok` 两结果：
    - **完整刷新**（双 ok）：推进时间戳 + 城市名解析 + 落盘（原行为不变）；
    - **部分刷新**（恰一个 ok）：不推进 `last_fetch_time`、不 `save_cache()`——
      时间戳原样保留，下次进屏 `start_fetch()` 即重试（评审要求的"更早重试"取
      最激进语义： freshness 立即失效）；`partial_refresh` 标志（任务线程写、
      LVGL 定时器读）让 refresh_cb 在 `update_ui()` 清空状态行**之后**补一行
      `Partial data - press r to retry`；
    - **全失败**：缓存不动（原行为），日志区分三种结局。
  - `partial_refresh` 生命周期：仅被下一次**完整刷新**清除——它描述的是
    "屏上数据是新旧混合"，跨屏保留、跨部分重试保留，语义自洽。
- **线程安全说明**：bool 标志任务写/UI 读，与现有 `data_valid`/`fetch_task` 的
  跨线程用法同款（单写单读、无读改写），未引入新机制。

### 1.2 CI 路径过滤补 `script/**`（`153eef7`，P2-2）

- **问题**：`set_srcdir.py` 决定矩阵实际编译哪个示例，但 workflow 的
  `on.push.paths` 不含 `script/**`——单独改选择器会整体跳过 CI，7.1 那类
  "矩阵全绿但编错源目录"的回归可静默复发。
- **修复**：paths 追加一行 `- "script/**"`（带缘由注释）。YAML 级改动；
  **本次推送即自验**——workflow 文件自身在 paths 内，本批推送会触发 CI 运行。

### 1.3 TLS extern 声明改 `void`（`3475c9b`，P2-3）

- **问题**：`factory.ino:757` 局部 `extern bool openai_tls_apply(void);` 与
  `openai_api.h:112` 的 `void` 返回类型不一致（`950fcfe` 笔误），跨翻译单元
  声明不兼容。
- **修复**：改 `extern void openai_tls_apply(void);`——沿用相邻
  `extern void openai_stats_poll();` 的局部 extern 风格（factory.ino 不 include
  openai_api.h，两种修法取改动更小者）。

### 1.4 台账

issue_list §9.1/9.2/9.3 状态 ⬜→✅ 并回填修复 commit；评审
`pda2-review-result-2026-08-07-21-gpt.md` 全部出列。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 pda2 | ✅ | `pio run -e pda2` → SUCCESS（30.5s，无新警告） |
| 烧录 | ✅ | COM5，SUCCESS |
| 开机冒烟 | ✅ | 串口 45s：菜单渲染（screen switch）、WiFi 自动连接 HONOR-60、无 panic |
| CI 触发自验 | ⏳ | 本批推送含 workflow 改动 → GitHub Actions 应运行且全绿（结果回填） |
| Weather 部分刷新场景 | ⏸ | 需人为制造"单端点失败"（如临时改错 forecast 路径），列入 §3 由用户定夺是否实测；正常路径回归如下 |
| 真机回归（§3） | ⏸ | 清单如下，逐轮回填 |

## 3. 真机回归清单

**本轮新增**
1. ⏸ Weather 正常完整刷新：进屏自动拉取/`r` 手动刷新，三页数据显示正常、
     状态行清空（完整路径行为不变）
2. ⏸（可选）部分刷新提示：临时把 `forecast` URL 改错烧录 → 进屏应显示
     `Partial data - press r to retry`，且重进屏会重试（不 等 1 小时）；
     验完还原
3. ⏸ TLS 开关不受影响：AI Config Trust 开关切换/重启保持（`3475c9b` 仅改声明）

**继承（未回归项顺延）**
4. ⏸ 菜单第 2 页左滑不再空滑（`de78338` §3）
5. ⏸ AI Config Trust 开关触摸/`\v` 键/重启保持（`6d26699..1473ef9` §3）
6. ⏸ SD FAT32 重格式化后 Setting 显示容量；回归 #6/#14/#15

## 4. 遗留项（简要）

- 三份申请待结果：`a924c4e`、`6d26699..1473ef9`、`de78338`
- 笔友 App：设计 v2（`97e5d2f`）待复审，复审通过后按 §9 预案实现
- Key：HEAD 无真实 Key；历史已清洗 + force-push（SECURITY.md checklist 全勾）

## 5. 回滚方案

```bash
git revert 3475c9b 153eef7 c27cb39
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
