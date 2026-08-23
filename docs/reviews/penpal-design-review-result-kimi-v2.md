# 笔友（PenPal）App 设计文档 v2 复审结果（kimi）

- **评审日期**：2026-08-22
- **设计稿**：[penpal-design.md](../penpal-design.md)（**v2 修订稿**，commit `97e5d2f`）
- **前序评审**：v1 两轮——[penpal-design-review-result.md](penpal-design-review-result.md)
  （Codex，C 部分接受）、[penpal-design-review-result-kimi.md](penpal-design-review-result-kimi.md)
  （Kimi，C 部分接受，3 项 High 前置 + 建议同批项）
- **评审范围**：v2 相对 v1 的全部修订点逐条核验（前置条件是否真正落实、引用准确性、
  修订是否引入新矛盾），并复核两轮前序评审的遗留项在 v2 中的去向
- **评审人**：Kimi
- **评审结论**：**A. 全量接受**——3 项 High 前置全部正确落实，kimi 跟踪项 T1-T5
  全部落入正文且与代码事实一致；附 4 项 Low 登记（实现期补登，不阻塞）。

---

## 1. 前置条件核验（3 项 High，全部通过）

### 1.1 §6 菜单页数公式 — ✅ 落实正确

- **位置**：`docs/penpal-design.md:387`（§6 集成表 ②）
- **验证**：
  - 明确**保持最大下标语义** `page_num = (MENU_BTN_NUM-1)/9`，并显式禁用
    `(MENU_BTN_NUM+8)/9`（门控 `page_curr < page_num` 会放行到不存在的第 4 页）——
    与 kimi §1.1 的分析逐字对应；
  - 连带改动补登齐全：按钮创建循环 `i/9` 三路分派（`ui_deckpro.cpp:453-458`）、
    `menu_get_gesture_dir` 加 `page_curr==2` 分支、`ui_Panel4` 页点 2→3
    （child 下标寻址 :306-307）——kimi §1.1 指出的两处遗漏均已入文；
  - 正确标注 18 项存量 off-by-one 已由 `de78338` 独立修复（本评审人已另行
    验收，A 接受）；§7 回归清单新增"第 3 页继续左滑不越界"（:408-409）。

### 1.2 §2 LLM 超时 120s → 180s — ✅ 落实正确

- **位置**：`docs/penpal-design.md:26-29`（§2）
- **验证**：180s 与服务端上界证据（demo `timeout=180`，`remote_api_demo.py:47`）
  对齐，推断方向已纠正并保留了"超时宁可偏长"的论证；全文口径一致——§3.2
  waitbox 取消理由（:140）、§4.4"异步 180s"（:304）、R6（:422）同步更新，
  无残留 120s。

### 1.3 §3.2 `s_pp_busy_gen` + busy 释放规则 — ✅ 落实正确

- **位置**：`docs/penpal-design.md:123-127, 135-136`（§3.2）
- **验证**：
  - 新增 `s_pp_busy_gen`（契约规则 3），释放规则"结果仅在 `res->gen ==
    s_pp_busy_gen` 时才允许清 `s_pp_busy`"，先例引用准确
    （`s_wifi_test_busy_gen`，`ui_deckpro.cpp:1479`）；
  - "页面代次不匹配一律丢弃，且**不得**触碰 `s_pp_busy`"——正确区分了页面代次
    （丢弃依据）与 busy 代次（释放依据）两个机制；
  - 取消路径（waitbox Close：`s_pp_gen++`、`busy=false`、任务不杀，:139-140）
    与释放规则自洽：取消后旧结果到达时 busy_gen 已被新请求更新（或 busy 已为
    false），迟到结果无法误释放——kimi §1.3 针对的竞态闭环。

## 2. 建议同批项核验（kimi T1-T5，全部落实）

| 来源 | v2 落点 | 核验 |
|---|---|---|
| §1.4 结果 `type` 字段 + 单队列偏差登记 | §3.2（:131-134, 143-145）：第二字段 `type` 枚举 8 类 + 偏差说明引契约表格 :13-18 | ✅ 先例引用准确 |
| §1.5 env.cfg 8 槽容量 + 95/96 对齐 | §3.4（:174-181）容量说明（静默丢弃 `:48`、理论 9>8、NVS 优先缓解）+ §4.7 base 限长 ≤95（:339） | ✅ `env_secrets.cpp:21` 引用准确 |
| §1.6 数据模型三处 | §5：`last_sender[24]`（:355）、null→0 哨兵（:361-362）、16KB 逐出最旧 + 状态行提示（:363-365） | ✅ 与 §2.1 schema/§4.4 只读判定自洽 |
| §1.8 ⑩ profile 端点登记 | §2 端点表 ⑩（:42），"v1 暂不接入"取舍写明理由 | ✅ demo `:51` 引用准确 |
| §1.9 waitbox 首例标注 | §3.2（:140-142）显式声明"全仓首例 waitbox 可取消交互"，commit 4 申请书复标 | ✅ |
| §1.10 §3.2 草稿文字定稿 | :146-147 直陈句 | ✅ 自问自答痕迹已清 |
| §1.11 五处措辞 | README 归因 `examples/pda2/README.md:78`（:105-106）、gen 脚本"新增"（:380）、≥50 统一为字符（:248-249）、HOME 未配置行为（:224-226）、chat 指代落实 `ui_ai_chat.cpp`（全文） | ✅ 全部命中 |
| §1.7 R3 降级 | §3.4（:182-187）+ R3（:419）改"遵循既有单槽先例"，weather/provider key 先例与 SECURITY.md 仅 `ai` 双槽的勘误写入正文 | ✅ `ui_weather.cpp:294-295`、`ui_ai_cfg.cpp:316-321` 引用准确 |

变更历史（:437-452）声明"跟踪项 T1-T5 全部落入正文，T6 已由 kimi 文件本身登记"
——经逐条比对，**属实**。

## 3. 新发现问题（均 Low，实现期补登，不阻塞）

### 3.1 Codex 前次评审的两项前置在 v2 仍未细化

- **严重性**：Low
- **位置**：`docs/penpal-design.md:147`（§3.2 末）、:294-295（§4.4 底部按钮）
- **证据**：
  1. HOME 串行两段 Sync 的状态行**文案**仍未给出（v2 仅"状态行提示两步进度"）——
     前次评审 §1.9 要求明确（如 `PALS OK, syncing mailbox…` → `Mailbox OK`）；
  2. Fix/Polish 按钮"我的信才有"的 UI 行为仍是"才有"二字，hide / disable / 灰显
     未定——前次评审 §1.10 要求确定（其倾向 disable + 提示）。
- **影响**：两项均会滑进实现期由实现者临场决策，可能返工。
- **最小修复**：§3.2 补一行状态行两段文案；§4.4 补一句"非我信 → Fix/Polish
  `LV_STATE_DISABLED`（位置稳定），Reply 在只读线程隐藏"。

### 3.2 §4.5 FB 页未写明按结果 type 选 layout

- **严重性**：Low
- **位置**：`docs/penpal-design.md:309-326`（§4.5）
- **证据**：§3.2 已有结果 `type` 字段（:131-134），但 §4.5 未声明 FB 页据
  FIX/POLISH 选渲染分支（correction 列表 vs polish 三段）——前次评审 T3 的
  跟踪项，v2 未收口。
- **最小修复**：§4.5 加一句"FB 页 `entry()` 按结果 `type` 选择布局"。

### 3.3 §6 集成表缺 `config_keys.h.example` 两行注释定义

- **严重性**：Low
- **位置**：`docs/penpal-design.md:389-391`（§6 集成表）
- **证据**：`env.cfg.example` 有追加行（:389），但 `config_keys.h.example` 没有
  对应条目——首次 clone 的开发者若实现代码直接引用 `PENPAL_*_DEFAULT_DEV`，
  首编即失败（前次评审 §1.14）。若实现以 `#ifdef` 防护则无碍，但设计稿未声明。
- **最小修复**：§6 表加一行 `config_keys.h.example`：追加两行注释掉的
  `// #define PENPAL_BASE_DEFAULT_DEV ...` 模板；或 §3.4 声明实现用 `#ifdef`
  防护、无需默认值。

### 3.4 §3.2 串行两段的 busy 保持窗口未声明

- **严重性**：Low
- **位置**：`docs/penpal-design.md:146-147`（§3.2 末）
- **证据**："先 PALS，结果回来后再发 MAILBOX"——若实现为"PALS 结果消费时先清
  busy、再发起 MAILBOX"，两清之间存在时间窗，用户此刻发第三请求（如再按 Sync）
  会占用 busy，导致 MAILBOX 链发起被拒、HOME 停在有 pals 无列表状态。
- **最小修复**：§3.2 补一句"串行两段期间 busy 保持到 MAILBOX 结果消费完毕才释放
  （第二段在 PALS 结果消费路径内直接链式发起，busy 不清）"。

## 4. 前次评审遗留项去向核对

| 项 | 来源 | v2 状态 |
|---|---|---|
| HOME 两段状态行文案 | Codex §1.9（前置） | **未细化** → 本评审 §3.1-1 |
| Fix/Polish 按钮行为 | Codex §1.10（前置） | **未细化** → 本评审 §3.1-2 |
| gen_img 脚本 README 说明 | Codex §1.8/T1 | v2 已定性"新增脚本"，README 说明留 commit 3，可接受 |
| last_at 长度防御（strlen≥16） | Codex §1.12/T2 | 未入 §4.1，留实现期随 R1 防御式解析一并处理，可接受 |
| FB 页布局切换 | Codex §1.13/T3 | **未收口** → 本评审 §3.2 |
| config_keys.h 新人引导 | Codex §1.14/T4 | **未收口** → 本评审 §3.3 |
| COM5 跨平台注记 | Codex §1.15/T5 | §7 仍硬编码 COM5（:399），Low，登记即可 |

## 5. 验证说明

- 本评审为文档级复核：v1→v2 全部修订点逐条比对，所有 `file:line` 引用抽验
  与当前 HEAD 一致（`env_secrets.cpp:21/:48`、`ui_deckpro.cpp:1479/:306-307/
  :453-458`、`ui_weather.cpp:294-295`、`ui_ai_cfg.cpp:316-321`、
  `remote_api_demo.py:47/:51`、`async_ipc_contract.md:13-18`、
  `examples/pda2/README.md:78`）。
- v2 未改动 §2 实测 schema、§3.1/§3.3 架构、§7 验证计划主体——沿用 v1 两轮评审
  的已通过结论，不重审。
- `de78338`（幽灵页修复）已由本评审人单独验收（A），与 v2 §6 的引用一致。

## 6. 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

3 项 High 前置全部正确落实，v2 达到实施基线，可按 §9 commit 拆分预案开工。
§3 的 4 项 Low 登记至 issue_list，在 commit 1/2 落地时顺手补登设计稿即可；
其中 §3.1 两项若能在开工前补一句话则更稳（避免实现期临场决策）。
