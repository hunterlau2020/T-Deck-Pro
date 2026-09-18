# 评审申请：五方评审（1561b41..b92d021）修复轮 delta 复审（bdb75bf..7bab081）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-09-18
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）；修复轮 delta 复审按 §2.5
- **关联 commit**（3 个，区间 `bdb75bf..7bab081`，首不含）：
  - `0bc0fcc` — **v1.14 修复轮**：Claude/Gemini/GPT/Grok 四方结果处置（2×P1 + 2×P2 + Nit）
  - `ae39763` — **v1.15 修复轮**：ds4（第 5 份）结果处置（F1 终态快径 + F3/F5 勘误 + F4 状态行）
  - `7bab081` — docs：ds4 结果文件入库（纯文档）
- **背景**：上轮申请 `1561b41..b92d021`（14 提交）五方结论 = Claude A / Gemini C / GPT C /
  Grok A / ds4 C。v1.2 修复轮四项核销经三方反例复核成立（GPT 结果"已核销/通过"节）；
  本区间是 WiFi 全链路两项 P1 + ds4 新发现的**修复处置轮**。按指南 §2.5，
  修复轮安排 delta 复审——即本申请。v1.13（LevelTest 功能，`bdb75bf` 本身）不在此轮，
  见 `session-batch-review-request-leveltest-family.md`。
- **硬件**：机 `28:37:2f:91:2c:20`（COM5）现烧 **v1.19**（`d5755ae`，含本区间全部修复，
  flash_verified 6/6 块校验 + 串口自证 `mark valid ok`）。

## 头部自校

- `git rev-list --count bdb75bf..7bab081` = **3**（与上方列表一致）
- `git diff --name-only bdb75bf 7bab081 | wc -l` = **8**
- `git diff --stat bdb75bf 7bab081 | tail -1` = **654 insertions / 31 deletions**

### 区间文件归属表（8 文件）

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/ui_deckpro.cpp` | **评（重点）** | P1-1 `wifi_scan_event_ensure_registered()`（4_1/4_2 双入口幂等注册）；P1-2 `wifi_autoconn_event_ensure_registered()`（start/restart 双调用）；sticky dropped-link（Grok P2-1）；F1 终态快径调用点；F4 状态行 `const char *write` 分支 |
| `examples/pda2/ui_deckpro_port.cpp` | **评（重点）** | sticky `s_scan_dropped_link` 置位语义（`=(status==CONNECTED)` → 仅 disconnect() 清） |
| `examples/pda2/ui_whoami.cpp` | **评** | P2-2a：server_ok 即 ++gen + 缓存清 + 配置变更通知（不等 prov_ok） |
| `examples/pda2/fw_version.h` | 评（低风险） | v1.14/v1.15 版本链 |
| `CHANGELOG.md` / `docs/issue_list.md` | 不评（纯文档） | §27 台账（P2-5 欠账补录 + 五方处置表 + 流程项）、§23 计数勘误 |
| `docs/reviews/session-batch-review-result-1561b41..b92d021-ds4.md` | 不评（评审产物） | ds4 结论入库 |

## 请评审重点（修复核销 + 反例击穿）

1. **P1-1 核销**：`wifi_scan_event_ensure_registered()` 的 `s_scan_event_registered` 幂等
   是否覆盖所有可触发 abort/release 的扫描入口；回调前置声明与注册时序。
2. **P1-2 核销**：`wifi_autoconn_event_ensure_registered()` 在 `wifi_autoconn_start()` 与
   `wifi_autoconn_restart()` 都调用后，"首次手动配置→断链→第 5 次后 idle"路径是否闭合；
   65s guard 自愈与事件计数的相互作用。
3. **F1 核销**：`wifi_scan_stop_and_release()` 前置 `scanComplete() != WIFI_SCAN_RUNNING`
   早退的竞态论证（终态 scanComplete 蕴含 `_scanDone` 已填完结果）是否成立。
4. **F4**：过渡态只记已见不重写——是否还有用旧缓冲重写标签的残余路径。
5. **真机反例状态（诚实声明）**：以下三条反例在本申请时点仍为 **CLAIM_ONLY**，
   评审员不得视为已验证：
   - P1-1 反例：冷启动直进 "- WIFI Scan"（不先进 WiFi Config）→ 扫描中退格 → 再进应可重新扫描；
   - F1 反例：扫描完成瞬间（列表已出但 1s tick 未 collect 的窗口）退格 ×10 → 串口不应出现 `release deferred` 楔死；
   - F2 反例：4_2 停留 >30s 后退出 → 数秒内恢复 IP。
   修复代码静态论证完整，反例待真机补验（COM5 机已带全部修复代码，验收清单在 TODO）。

## 验证边界

- 编译：v1.14/v1.15 各自 `pio run -e pda2` SUCCESS；v1.19（超集）SUCCESS。
- 刷写：v1.14、v1.15、v1.19 均经 `flash_verified.py` 分块校验 + 串口自证。
- 上述三条真机反例：**CLAIM_ONLY**（见"请评审重点"第 5 条）。
- `git diff --check bdb75bf 7bab081`：通过。
