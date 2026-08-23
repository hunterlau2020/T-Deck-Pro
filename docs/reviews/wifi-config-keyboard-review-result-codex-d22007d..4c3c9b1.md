# 第 29 轮整改评审结果（Codex）— 原生下拉 Provider + 每供应商独立 Key + custom 清空 + OPENROUTER_KEY 改名

- **评审日期**：2026-08-18
- **评审申请书**：[wifi-config-keyboard-review-request-d22007d..4c3c9b1.md](wifi-config-keyboard-review-request-d22007d..4c3c9b1.md)
- **关联代码范围**：`d22007d..4c3c9b1`
- **本次重点新增范围**（`fd7be74` 已由第 28 轮全量接受，见
  [评审结果](wifi-config-keyboard-review-result-codex-b9b1ed4..fd7be74.md)，本轮沿用不再重审）：
  - `d22007d` — AI Config 原生 lv_dropdown provider 选择器 + 每供应商独立 Key（用户反馈）
  - `da0217f` — secrets：env 默认 Key 改名 `AI_KEY` → `OPENROUTER_KEY`（用户反馈）
  - `4c3c9b1` — custom 清空三框 + openrouter Key 链补 config_keys.h 兜底（用户反馈）
  - doc-only：`1de71c9`、`1e71aab`（注册 + 第 28 轮结果归档）
- **评审结论**：**A 全量接受**（附 2 项 Info 观察 + 1 项 Low 文档项，均不阻断）

---

## 1. Findings

### 1.1 d22007d 原生下拉 + 每供应商独立 Key — ✅ 通过

- **位置**：`examples/pda2/ui_ai_cfg.cpp`（s_providers 增 key_name 列 / ai_provider_apply 重写 /
  ai_provider_select / ai_provider_dd_cb / ai_cfg_save 增 per-provider 落盘 / create 下拉段）
- **初始化顺序核验（关键点）**：create 中先 `lv_dropdown_set_selected(idx)` **再**
  `lv_obj_add_event_cb(VALUE_CHANGED)`——LVGL v8 的 set_selected 会发 VALUE_CHANGED，
  回调未挂时不触发 apply，**进屏绝不覆盖已存值**；与 commit 声明一致 ✓
- **键盘/触摸单一事实源**：Alt+Enter → `ai_provider_next` → `lv_dropdown_set_selected`
  → VALUE_CHANGED → `ai_provider_dd_cb` → `ai_provider_select`（先 sync_draft 再切换）——
  触摸与键盘走同一分派路径，无并行逻辑 ✓
- **每供应商 Key 链**：`NVS ai:key.<name>` → `/env.cfg <NAME>_KEY` → 空框；
  切换预设时 Key 框整体替换为该供应商的值（含清空）——正是"每家自己的 Key"语义 ✓
- **NVS 键名长度核验**：NVS key 上限 15 字符——`key.openrouter`=14 为最长，全部合规
  （超限会静默失败，故逐项核过）✓
- **Save 侧**：`openai_save_config` 成功后把当前 Key 额外存 `key.<provider>`；
  与 dual-slot 主配置（AI Chat 实际使用）互不干扰，切走切回恢复 ✓
- **Info 观察**：
  - custom 状态下 Save 会写 `key.custom`（name 非空过判）——该键永不被读回，
    属死存储（数十字节级），无害；可选在判空条件排除 custom
  - 下拉 options 字符串列表与 `s_providers` 数组顺序靠手工保持一致
    （dd_cb 直接以 selected 索引数组）；目前一致，未来改表需两处同步——Info 提示

### 1.2 da0217f env Key 改名 AI_KEY → OPENROUTER_KEY — ✅ 通过

- **一致性核验（评审方全库扫描）**：`openai_load_config` 实际读取、`env.cfg.example`、
  `env_secrets.{h,cpp}` 注释全部同步；HEAD 已**无任何** `"AI_KEY"` env 字面量残留；
  SECURITY.md / CLAUDE.md / TODO / CHANGELOG 无陈旧引用 ✓
- **动机成立**：provider 链读 `OPENROUTER_KEY`，旧名导致 openrouter 预设永远取不到
  env 值——改名后两条链（openai_load_config 与 provider apply）命名统一 ✓
- **运维提示（Info）**：设备侧 `/env.cfg` 与开发备份 env.cfg 中的条目名需随改为
  `OPENROUTER_KEY=`（属申请人/用户现场操作，代码侧已齐）

### 1.3 4c3c9b1 custom 清空 + config_keys 兜底 — ✅ 通过

- **custom 清空**：`base[0]=='\0'` 分支清空三框 + 三个 draft 缓冲；
  `ai_provider_select` 先 `ai_cfg_sync_draft()` 再 apply——离场字段编辑先保住、
  随后 custom 清空，顺序正确 ✓
- **openrouter 链补全**：NVS `key.openrouter` → env `OPENROUTER_KEY` →
  `#ifdef AI_KEY_DEFAULT_DEV`（config_keys.h，gitignored）→ 空——修复无 /env.cfg
  设备切 openrouter 显示空 Key 的用户反馈；与 SECURITY.md 四级链完全对齐 ✓
- 原 `env_get("AI_KEY")` 兜底被 `AI_KEY_DEFAULT_DEV` 取代，与 da0217f 改名收敛一致 ✓

### 1.4 文档观察（Low，不阻断）

- 申请书头部"本轮 **4** 个"含 `fd7be74`——该 commit 已由第 28 轮接受（`1de71c9` 已归档），
  历史范围行应指向 `b9b1ed4..fd7be74` 结果而非 `e08bdac..b9b1ed4`（连续两轮历史引用未随
  归档前移，建议下轮修正）
- §3 回归清单出现两个编号 6（本轮新增与"上轮 b9b1ed4"段重复编号）；§1 小节仍倒序
  （1.2 在 1.1 前）——纯排版
- 回滚列表 4 commit 新→旧顺序正确 ✓

---

## 2. 已通过项汇总

- **本轮新增**：`d22007d`、`da0217f`、`4c3c9b1` + doc-only `1de71c9`、`1e71aab`
- **沿用**：`fd7be74`（第 28 轮）；e08bdac..b9b1ed4（第 22–27 轮）；历史 844a907..156732c

---

## 3. 跟踪项（继承 + 本批新增）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| key.custom 死存储（可选排除） | 本轮 §1.1 | Info |
| 下拉 options 与 s_providers 顺序手工同步 | 本轮 §1.1 | Info |
| 设备/开发 env.cfg 条目名改 OPENROUTER_KEY | 本轮 §1.2 | 现场操作 |
| 申请书历史引用未随归档前移 + 编号重复 | 本轮 §1.4 | Low，下轮修正 |
| M1：3 份评审文档明文旧 Key 掩码 | 第 26 轮 | 推公网前必做 |
| O1/O2：月度清零 UTC 月界 / 仅 load 时评估 | 第 28 轮 | Low，接受 |
| wifi_scan_overlay exit4_1 hide；exit9/entry9 对称性；双 Enter 边缘 | 第 25/27 轮 | 待办/挂起 |
| SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | TODO | 阶段 1 |

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方本轮复跑）
- 静态复核：`git show d22007d da0217f 4c3c9b1` 全 diff 逐段核查；LVGL v8
  set_selected/VALUE_CHANGED 语义与回调挂载顺序核验；NVS 键名长度逐项核算（≤15）；
  `"AI_KEY"` 字面量与 docs 引用全库扫描归零确认；下拉 options 与预设表顺序比对
- 编译：评审环境无 `pio`，采信申请人 `pio run -e pda2` SUCCESS + COM5 烧录 Hash verified（沿用前轮做法）
- 真机回归：§3 十五项 ⏸ 待用户（含本轮新增下拉六项）
- 本结果文档不含任何 Key 正文

## 5. 审批意见

- [x] **A. 全量接受** — d22007d / da0217f / 4c3c9b1 + 2 个 doc-only commit 接受，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：三项用户反馈精准闭合——原生下拉初始化顺序经 LVGL 语义核验无覆盖风险、
键盘触摸单一分派路径；每供应商 Key 链 NVS 键名合规、Save/恢复闭环；改名全库一致性扫描归零；
custom 清空与四级 Key 链补齐均与 SECURITY.md 架构对齐。观察项均为 Info/Low，不阻断。

**遗留项**：
- M1（3 份评审文档掩码）继续跟踪至推公网前
- 真机回归 15 项待用户实测回填

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：三个 commit 全 diff 追踪、
LVGL dropdown 事件语义与初始化顺序核验、NVS 键名长度核算、改名全库归零扫描、
NVS 测试复跑 11/11）