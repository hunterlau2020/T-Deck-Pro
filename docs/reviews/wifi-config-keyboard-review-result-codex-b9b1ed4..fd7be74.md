# 第 28 轮整改评审结果（Codex）— AI Config Provider 下拉 + base 后缀 + usage 月度清零

- **评审日期**：2026-08-18
- **评审申请书**：[wifi-config-keyboard-review-request-b9b1ed4..fd7be74.md](wifi-config-keyboard-review-request-b9b1ed4..fd7be74.md)
- **关联代码范围**：`b9b1ed4..fd7be74`（申请书首次按 README 规则 4 使用短范围命名，✅）
- **本次评审对象**：
  - `fd7be74` — AI Config provider 预设下拉 + base 自动补 `/chat/completions` + usage V3 月度清零（用户需求）
  - doc-only：`52e5719`（申请注册 + 第 27 轮结果归档）
- **评审结论**：**A 全量接受**（附 2 项 Low + 3 项 Info 观察，均不阻断）

---

## 1. Findings

### 1.1 Provider 下拉与预设填充 — ✅ 通过

- **位置**：`examples/pda2/ui_ai_cfg.cpp`（s_providers 表 / ai_provider_apply / ai_provider_next / create 匹配段 / poll `\t` 分支）
- **机制核查**：
  - 6 项预设表 + custom；点击行与 Alt+Enter（`'\t'`，原 AI Cfg poll 中为 no-op，无既有语义冲突）均走 `ai_provider_next`：
    **先 `ai_cfg_sync_draft()` 保住离场字段编辑，再循环 idx，再 apply**——顺序正确
  - apply：预设 base/model 同写 textarea 与 draft（strncpy n-1 + 显式终止，`ai_base[160]` 容最长预设 URL 绰余）；
    custom（`base[0]=='\0'`）不动框 ✓；`s_ai_test_passed = false` 使 Test 结果即时失效 ✓
  - openrouter 且 Key 框空 → `env_get("OPENROUTER_KEY")` 回退 `"AI_KEY"` 填框 + 串口日志——
    仅补 NVS/配置链皆空的情形，与 SECURITY.md 链架构一致
  - 进屏按已存 base 精确匹配预设**仅显示**（不动已存值）；未匹配落 custom ✓
  - 按钮+子 label 点击模式与既有全局按钮一致（LVGL v8 事件冒泡，真机已验证模式）
- **观察**：用户手改 base 后 provider 标签不跟随更新——纯显示层，申请已声明"仅显示"，可接受

### 1.2 base 存根地址 + 调用时补后缀 — ✅ 通过

- **位置**：`examples/pda2/openai_api.cpp` openai_chat_impl 入口；`openai_api.h` AI_BASE_DEFAULT
- **机制核查**：
  - `ep.rfind("/chat/completions") == npos` 才追加，**legacy NVS 全路径值原样工作** ✓
  - 追加前 strip 尾部 `/` → `.../v1/` 不会产生双斜杠 ✓
  - `base_url = ep.c_str()`：ep 为函数内局部 string，生存期覆盖全部使用点 ✓
  - 下游 `http_post(base_url, ...)` 直接接收 std::string，**无定长 URL 缓冲溢出面** ✓
  - `AI_BASE_DEFAULT` 同步改为根地址，与后缀逻辑自洽；V1 迁移/默认路径不受影响
- **结论**：新旧 NVS 值双向兼容，用户"存根地址、调用补全"的需求精准落地

### 1.3 usage 月度清零（V3 blob）— ✅ 通过

- **位置**：`examples/pda2/openai_api.cpp` ai_stats_t/ai_stats_v2_t/ai_stats_load_locked
- **迁移核查（逐字节）**：
  - V3 = V2 布局尾部追加 `uint32_t reset_month`，两结构 sizeof 必然不同，分支按 size 判定不会误配；
    V3 分支仍带 magic 双保险 ✓
  - V2→V3：`memcpy(&s_ai_stats.p_tok, &v2.p_tok, &v2.t_cost+sizeof- &v2.p_tok)` 恰好覆盖
    chat+test 两组全部计数器，reset_month 保持 0（BSS 初值）→ **迁移不丢数、不误清零** ✓
  - V1→V3：沿用逐字段搬移（copilot 1.4 模式），日志文案同步 V3 ✓
- **清零逻辑核查**：
  - `time(nullptr) > 1700000000`（2023-11）作 NTP 同步哨兵——冷启动（epoch≈0）**绝不触发清零** ✓
  - `prev != 0 && prev != ym` 才 memset 清零并置 dirty 立即落盘；清零后回填当月 ym ✓
  - 新装/迁移后首月 prev=0 → 只记录月份不清零 ✓
- **观察**：
  - **O1（Low）**：月份边界用 `gmtime`（UTC）——设备 TZ 为 CST-8，实际清零点是本地 1 号约 08:00
    而非本地子夜。纯语义细节；若要求本地月界可换 localtime
  - **O2（Low）**：清零检查在 `ai_stats_load_locked`（每 boot 一次，`s_ai_stats_loaded` 守卫，
    4 个 stats 入口共用）——连续开机跨月的设备到下次 load 才清零。PDA 使用形态下可接受
  - **O3（Info）**：`gmtime` 静态缓冲与 weather parse（core 0 fetch task）共享——既有模式，
    boot 期微秒窗口，无实际风险
  - **O4（Info）**：V3 迁移无自动测试覆盖（`test_nvs_atomic_save.py` 只管 dual-slot 保存）；
    本轮静态逐字节核验通过，可选补一个 V2→V3 布局测试

### 1.4 附带项核查 — ✅ 全部落实

- **`.gitignore` + env.cfg 备份**：新增 `env.cfg` 规则；`git check-ignore` 确认
  `examples/pda2/env.cfg` 被忽略、`git ls-files` 确认未跟踪、`git log --all` 确认**从未进过任何 commit** ✓
  （O5 Info：该文件含真实 Key，属 SECURITY.md 链内合法存在；注意勿随截图/打包外发）
- **config_keys.h.example 补 `AI_KEY_DEFAULT_DEV`** 占位 + 链说明 → 第 26 轮 L1 **闭合** ✓
- **TODO.md**：安全清单 1/2 项打勾并注 `0e78025`，重启恢复复测标 ✅ → 第 26 轮 L2 **闭合** ✓
- **"Shoutdown" → "Shutdown"**（USB 分支）→ 第 27 轮 Low **闭合** ✓
- **密钥扫描**：HEAD 全文 grep 无新增 Key；残留仍为 M1 所列 3 份历史评审文档（跟踪中）

### 1.5 流程整改确认 — P1 闭合

- 申请书首次满足：短范围文件名（规则 4）+ 历史范围一行指向已归档结果 + §3 携带完整 14 项
  回归清单（本轮 5 项 + b9b1ed4 4 项 + Weather/Secrets 继承 3 项 + AI 历史 2 项）
- 第 27 轮 **P1 High 流程项闭合**；待用户实测后逐轮回填结果即可

---

## 2. 已通过项汇总

- **本轮新增**：`fd7be74`（provider 下拉 + base 后缀 + V3 月度清零 + 附带卫生项）、`52e5719`（doc-only）
- **沿用**：e08bdac..b9b1ed4（13 个代码 commit，第 22–27 轮接受）+ 历史 844a907..156732c（28 commit）

---

## 3. 跟踪项（继承 + 本批新增）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| O1：月度清零按 UTC 月界（可换 localtime） | 本轮 §1.3 | Low，按需 |
| O2：清零仅在 stats load 时评估（跨月需重启） | 本轮 §1.3 | Low，接受 |
| O4：可选补 V2→V3 stats 迁移测试 | 本轮 §1.3 | 可选 |
| M1：3 份评审文档明文旧 Key 掩码 | 第 26 轮 | 推公网前必做 |
| wifi_scan_overlay 跨 push 屏残留（exit4_1 hide） | 第 25 轮 | 待办 |
| exit9/entry9 对称性、双 Enter 边缘 | 第 27 轮 | 挂起/真机后定 |
| SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | 主评审/TODO | 阶段 1 |

**已闭合**：P1（回归清单流程）；L1（example 补 AI_KEY_DEFAULT_DEV）；L2（TODO 打勾）；
"Shoutdown" 拼写

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方本轮复跑）
- 静态复核：`git show fd7be74 52e5719` 全 diff 逐段核查；V2→V3 memcpy 区间逐字节比对；
  后缀逻辑 legacy/尾斜杠/生存期三场景走查；env.cfg 三查（check-ignore / ls-files / log --all）；
  HEAD 密钥 grep 复查；`ai_stats_load_locked` 4 个调用点与单次加载守卫确认
- 编译：评审环境无 `pio`，采信申请人 `pio run -e pda2` SUCCESS + COM5 烧录 Hash verified（沿用前轮做法）
- 真机回归：§3 十四项 ⏸ 待用户（清单已完整，逐轮回填）
- 本结果文档不含任何 Key 正文

## 5. 审批意见

- [x] **A. 全量接受** — fd7be74 + 52e5719 接受，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：用户需求 3/4/5/6 全部精准落地——provider 下拉切换语义完整（sync→cycle→apply 顺序、
custom 保护、仅显示匹配）；base 后缀新旧兼容零破坏；V3 月度清零迁移路径逐字节核验无误、
NTP 哨兵杜绝冷启动误清。附带项把第 26/27 轮全部 Low 跟踪项一次闭合，申请书格式首次完全合规。
O1/O2 为语义细节观察，不阻断。

**遗留项**：
- M1（3 份评审文档掩码）继续跟踪至推公网前
- 真机回归 14 项待用户实测回填；下轮申请携带实测结果

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：`git show fd7be74` 全 diff 追踪、
V2→V3 迁移 memcpy 区间核算、后缀逻辑三场景走查、env.cfg 跟踪状态三查、HEAD 密钥扫描、
NVS 测试复跑 11/11）