# 第 30 轮整改评审结果（Codex）— key.custom 死存储闭合 + options 同步标注 + 真机回归 11/15 回填

- **评审日期**：2026-08-18
- **评审申请书**：[wifi-config-keyboard-review-request-764e7bf..980b6df.md](wifi-config-keyboard-review-request-764e7bf..980b6df.md)
  （评审时名为 `a2ecd7b.md`，随后扩展范围改名；`a2ecd7b` 本结果 A 接受后已出列）
- **关联代码范围**：`a2ecd7b`（单 commit；按 README 规则 4 修订版命名：首末 id 含两端，非 git 区间记法）
- **本次评审对象**：
  - `a2ecd7b` — AI Config：custom 不再写 per-provider key + KEEP IN SYNC 标注（第 29 轮两项 Info 闭合）
  - doc-only：`54e0572`（申请注册）、`9cb6d25`（fd7be74 出列 + 命名对齐 + README 规则 4 修订）、
    `20a2cdd`（真机回归 11/15 回填）
- **评审结论**：**A 全量接受**（附 1 项 Info 流程备注，不阻断）

---

## 1. Findings

### 1.1 a2ecd7b-A Save 跳过 custom 的 per-provider key — ✅ 通过

- **位置**：`examples/pda2/ui_ai_cfg.cpp:315`（ai_cfg_save 守卫）
- **核验**：守卫由 `p->name[0] != '\0'` 改为 `p->base[0] != '\0'`——与 `ai_provider_apply`
  的 custom 判别（`base[0]=='\0'` 清空分支）**完全同源**，两处不再可能语义漂移 ✓
- **行为分析复核**：custom 下 Save 仍走 `openai_save_config`（dual-slot 主配置照存，
  AI Chat 可用），仅不再写永远读不回的 `key.custom`；custom 进屏按已存 base 匹配不到
  预设 → 下拉显示 custom → 三框显示已存值（清空仅发生在"选中 custom"时）——
  申请书的"无回归"结论成立 ✓
- diff 范围核验：本 commit 仅 ui_ai_cfg.cpp 一个文件 +7/-2，无其它行为变更 ✓

### 1.2 a2ecd7b-B KEEP IN SYNC 标注 — ✅ 通过

- `s_providers[]` 定义处与 `lv_dropdown_set_options` 处各加一条互指注释，
  明示"名称与顺序一致、dd_cb 按 selected 索引数组"——纯注释，零行为变化 ✓
- 第 29 轮 Info ①② 双双闭合

### 1.3 流程核验（doc-only 三连）

- **README 规则 4 修订（9cb6d25）**：文件名改为"本轮实际覆盖 commit 首末 id（含两端，
  非 git 区间记法），不携带已接受边界 commit"。评审方知悉并接受该约定；实操含义：
  文件名**不可**直接当 `git diff a..b` 区间用（git 区间左开），本轮评审按逐 commit
  `git show` 执行，已在申请书"命名说明"中向后续评审人声明 ✓
- **第 29 轮结果归档（9cb6d25）**：原结果以 `d22007d..4c3c9b1` 名首次入库——
  评审方逐字节核验：UTF-8 无 BOM 完好、Findings/结论/评审人段落与评审方原稿一致，
  仅两处头部引用随申请改名同步（申请书链接、范围行）；**不存在对已入库结果的覆盖**
  （旧名从未入库，属首次归档时的链接维护）。Info 备注：后续如再遇结果文件改名需求，
  建议在 commit message 中如本次一样明示"align naming"，保持审计可溯 ✓（本次已做到）
- **真机回归回填（20a2cdd）**：15 项清单回填 **11 项 ✅**（含 Weather r 刷新/深圳城市名/
  Shutdown 四路径/USB 分支/三页翻页 UV:--/无 GPS 深圳回退/下拉六项之五）；
  第 13 项以"擦 NVS 连带丢 WiFi 配置"为由代码级验收——理由正当，接受；
  剩余 ⏸ 三项：#6（Save 后切走切回 key 恢复）、#14（失败重试路径）、#15（长回答 >4KB）
  ——第 27 轮 P1 流程项整改后，清单首轮回填落地，流程健康 ✓

---

## 2. 已通过项汇总

- **本轮新增**：`a2ecd7b` + doc-only `54e0572` / `9cb6d25` / `20a2cdd`
- **沿用**：d22007d..4c3c9b1（第 29 轮）、fd7be74（第 28 轮）、e08bdac..b9b1ed4（第 22–27 轮）、
  历史 844a907..156732c

---

## 3. 跟踪项（继承 + 本批状态更新）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| key.custom 死存储 | 第 29 轮 Info ① | ✅ a2ecd7b 闭合 |
| options/s_providers 同步标注 | 第 29 轮 Info ② | ✅ a2ecd7b 闭合 |
| 设备 /env.cfg 改名操作 | 第 29 轮 | ✅ 申请 §2 确认：设备 SPIFFS 无 /env.cfg（从未上传），备份已改名，无需操作 |
| 真机回归 #6 / #14 / #15 | 回填后剩余 | ⏸ 待用户下轮实测 |
| M1：3 份评审文档明文旧 Key 掩码 | 第 26 轮 | 推公网前必做 |
| wifi_scan_overlay exit4_1 hide；exit9/entry9 对称性；双 Enter 静默窗 | 第 25/27 轮 | 挂起 |
| O1/O2：月度清零 UTC 月界 / 仅 load 时评估 | 第 28 轮 | Low，接受 |
| SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | TODO | 阶段 1 |

## 4. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方本轮复跑）
- 静态复核：`git show a2ecd7b` 全 diff（单文件 +7/-2）；守卫与 apply 判别同源性核验；
  custom Save 行为路径走查（dual-slot 照存 / key.custom 跳过 / 进屏回显链路）
- 归档完整性：第 29 轮结果新旧名入库路径核查（旧名从未入库）、内容 UTF-8 逐段核验
- 编译：评审环境无 `pio`，采信申请人 `pio run -e pda2` SUCCESS + COM5 烧录 Hash verified（沿用前轮做法）
- 真机回归：11/15 ✅ 已回填；#6/#14/#15 待测
- 本结果文档不含任何 Key 正文

## 5. 审批意见

- [x] **A. 全量接受** — a2ecd7b + 3 个 doc-only commit 接受，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：第 29 轮两项 Info 以最小 diff 精准闭合，守卫与既有 custom 判别同源、
行为分析经评审方复核无回归；命名约定修订已在 README 与申请书双向声明；
第 29 轮结果归档内容完整、无覆盖；真机回归首轮回填 11/15，流程闭环健康。

**遗留项**：
- 真机 #6/#14/#15 下轮回填
- M1（3 份评审文档掩码）继续跟踪至推公网前

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：`git show a2ecd7b` 全 diff 追踪、
守卫同源性核验、custom Save 行为路径走查、归档结果逐字节完整性核验、NVS 测试复跑 11/11）