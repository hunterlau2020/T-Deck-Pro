# T-Deck-Pro 评审指南（pda2 固件 Review Guide）

> **版本**：v1.1（2026-09-13）
> **状态**：生效规范。适用于 `examples/pda2`（及其依赖的 `examples/factory`、
> `lib/`、`config/lv_conf.h`、`platformio.ini`、构建/探针脚本）的代码评审，以及
> `docs/` 下设计稿（OTA、PenPal、WiFi 记忆槽等）的设计评审。
> **改编自**：`docs/review_guide_reference/REVIEW_GUIDE.md`（后端评审指南 v2，
> 裁决 v3.1）与 `docs/review_guide_reference/MACAO_REVIEW_GUIDELINES.md`
> （MACAO 评审方法论 v1.1）。两份参考的方法论内核（证据先行、后果×可达性定级、
> 两轴标注、声明矩阵、反例库、门禁、修订治理）全部保留；**领域内容整体重建**为
> 本项目现实。
> **上位准则**：`CLAUDE.md`（项目硬规则）+ `docs/async_ipc_contract.md`（异步契约，
> 违反以契约为准）+ `docs/issue_list.md`（canonical 修复台账）。
>
> **本指南只保存稳定标准，不保存时点信息**（当前 HEAD、基线 SHA、开放 ⏸ 清单、
> 待结果申请列表一律看 `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` /
> `docs/reviews/`）。参考 GUIDE-1 教训：时点句会随轮次漂移，写进规范必过期。
>
> **本项目与两份参考的根本差异（贯穿全文）**：
> 1. **无自动化测试**（存在 PlatformIO 构建矩阵 CI，`.github/workflows/
>    platformio.yml` 逐 example 编译冒烟；无测试/静态检查 CI，issue_list
>    §7.1）——验收最终落在**真机手动回归**（hardware-in-the-loop）。
>    参考里的 `pytest`/`node --test`/多模型投票门禁在此**不存在**；本指南用
>    "证据类型 + 验证状态 + ⏸待用户实测"三件套替代（§3）。
> 2. **缺陷域是嵌入式**——变砖/重启/堆损坏/busy 卡死/LVGL 池耗尽/8KB UI 栈溢出/
>    凭据截断/EPD 阻塞实时路径/硬件变体差异，而非 IDOR/SQL 注入（§5、§6）。
> 3. **多 reviewer 是 AI 模型**（按模型名：Codex/Kimi/GPT/Gemini/Claude/Grok/Qwen…；
>    历史文件含 CLI 名 `opencode`/`copilot`，保留不改名），
>    属主是用户本人——"证据 > 票数"在此尤其重要，模型分歧常见且需登记（§0.2、§9）。

---

## 0. 评审原则（Principles）

1. **证据先行**：每条发现必须给出 `file:line` 定位 + 可复现的反例输入或调用/操作
   序列；没有证据的"闻起来不对"写入疑问清单，不占缺陷条目。
2. **证据 > 票数**（承自参考 R17）：合入决定是**证据标签的函数，不是 reviewer
   数量的函数**。任何多数派结论盖不过一个带反例的发现。多模型评审结果只作建议性
   输入，**属主（用户）签字环节兜底**。Codex 与 Kimi 对同一条目结论相反时，不比
   谁票多，比谁给了可复现证据（§0 冲突四步）。
3. **独立复现优先 + 冲突四步**（承自参考 R3）：两家结论相反——① 复现实验（能在
   开发机跑的：编译/size/静态走查；不能跑的：纸面时序推演 SIM）；② 事实与定级
   分离查表（§4.2）；③ 环境声明与 CODE 声明分开打标（§4.3）；④ 属主裁决。
4. **等价类闭合**（承自参考 R4）：修复验证轮关闭同一声明的等价绕过即闭合（有数学
   闭合的不得再以更花哨拼图否决）；**新的不同声明另开条目**，不得借旧条目重开。
   本项目典型：一条 busy 泄漏修复后，须确认所有 stale-drop 分支（离页/取消/超时/
   后台退出）都释放 busy，而非只修申请点名的那一条。
5. **前提重核 + 区间文件归属**（承自参考 R14）：评审基线若已越过申请基线，报告记
   `git rev-parse HEAD` + `git diff <base> HEAD --stat`；**评审区间内每个文件须
   归属**（受评 / 不评+理由 / 意外落入+处置），"非实现代码"（如 `*.md`、`*.py`
   脚本、`platformio.ini`）不是豁免理由。
6. **分级查表，不查观感**：按 §4.2 后果×可达性判定表 + 定级三步留坐标；废止一切
   "拿不准降一级"式散文规则。
7. **区分缺陷与决策**：已接受的 MVP/硬件边界（§4.4 + `issue_list.md`）不记为缺陷；
   不可接受时单列"决策项"由属主裁定，不得由代码默认行为或评审多数代替。
   典型决策项：是否给 PenPal 做可中止 HTTP 传输、OTA 是否强制 HTTPS/签名清单、
   是否后台静默检查更新。
7b. **多前提降级**（承自参考）：一个缺陷若必须**同时满足多个前提**才可达（如：需
   特定硬件变体 + 需缺外设 + 需特定操作时序），按"任一前提不满足即不可达"折算——
   取最难前提的可达性档入表；条件间非独立（满足 A 即自动满足 B）时不得重复折算。
8. **验证修复，而非重复发现**（承自参考 R8/MACAO §8.3）：标注"已修复"的条目，评审
   动作是**构造反例击穿修复**（突变对照优先），不是重报原缺陷。本项目无自动化回归，
   "击穿"= 纸面构造让修复失效的输入/时序（SIM），或要求作者补探针固件实测（HW）。
9. **硬件语境加权**（本项目专属，替代参考的"儿童安全加权"）：变砖、重启循环、堆
   损坏、凭据泄漏/截断、看门狗复位、EPD 刷新阻塞实时路径（音频泵/计时）权重高于
   一般 UI 缺陷——因为它们**不可远程恢复**（需 USB 分块烧录，见 build 文档坑 6）
   或**泄密不可逆**（秘密链见 §5.4）。
10. **Required action 可落地**：给出最小修复方向；接受权宜之计必须写明"生产化/
    真机验证前需补什么"，并登记到 `issue_list.md`（台账是"应修不阻断"的唯一承接面）。

### 冲突四步落地（本项目的"复现实验"指什么）

无测试 CI 的环境下，"独立复现"按可达性降级执行，能到哪级算哪级，**到不了 HW 就如实标
`PARTIALLY_VERIFIED` 而非假装 `VERIFIED`**：

| 想复现的东西 | 开发机可做（无需设备） | 必须真机（HW） |
|---|---|---|
| 编译是否通过 / 体积涨幅 | `python -m platformio run -e pda2`，看 size 报告 RAM/flash | — |
| 静态逻辑/时序/契约符合性 | 读 `file:line` + 纸面推演（SIM） | 涉及真实并发/外设时序时 |
| 跨核原子/竞态是否真发生 | 纸面推演 + 看 `__atomic_*` 用法 | 反复操作压测才暴露的 |
| EPD/音频/键盘/触摸/SD/4G 行为 | — | 必须真机 |
| 开机不重启/无 backtrace | — | 必须真机烧录 + 串口冒烟 |

---

## 1. 目的与边界

1. 防止把"文字上提到了""编译过了""作者说真机测了"与"可被唯一、可复现地推出同一
   行为"混为一谈；
2. 防止只看申请书的靶向清单而忽略全区间 diff 与承重邻居（§7.2）；
3. 覆盖正常路径 + 异常路径（离页/取消/超时/掉电/断网/缺外设/池耗尽/栈溢出/变体差异）
   + 历史遗留（很多"这不可能work"的注释早已被某 commit 解决——先查 `issue_list.md`）；
4. 使结论可复现、可反驳，可追溯到 `file:line` + commit + 证据类型；
5. 让 reviewer 自身的遗漏、模型间分歧与修正同样进入审计记录（`docs/reviews/`）。

### 1.1 适用范围按对象裁剪（承自 MACAO §1.2）

| 对象 | 最低适用章节 |
|---|---|
| 设计稿评审（OTA / PenPal / WiFi 记忆槽 / AI provider 关联等 `docs/*.md`） | §0–§4、§6、§9（【D】条目）、§10；结论用 **L1 DOC-ALIGNED**（§2.1），无需虚构 CODE/HW 证据 |
| 单模块代码修复（一处 bug / 一条评审发现） | 全文，重点 §4、§5、§6、§7 |
| 新屏 / 新异步任务 / 新 provider | 全文，重点 §5（承重不变量）、§6（反例库）、§4.4（声明矩阵新行） |
| 跨模块大批次（≥10 commit 全量评审） | 全文 + §8 分段评审（5–7 commit/段）+ 每段独立走流程 |
| 纯文档/构建脚本/探针固件 | §0、§1、§9（DOC-ALIGNED 或 BUILD-VERIFIED 即可，不套反例库） |

### 1.2 稳定规范与实时状态分离（承自 MACAO §1.3）

本文件只保存稳定标准。以下必须另存，**不得写入本文正文**：

```text
CHANGELOG.md                                              每批工作的时点记录（日期 + commit + 验证状态）
TODO.md                                                   当前阻塞项 / 待真机回归清单 / 待结果申请
docs/issue_list.md                                        canonical 修复台账（每条：状态 + 修复 committish）
docs/reviews/<主题>-review-request-<range>.md                评审申请（含 commit id，不可变归档，防覆盖）
docs/reviews/<主题>-review-result-<commit范围>[-<reviewer>].md   单次评审结论（commit 锚防跨轮覆盖；reviewer=模型名）
```

---

## 2. 四级结论与门禁（改编 MACAO §2）

### 2.1 四级结论（设计稿用 L1–L4；代码用 A/B/C 审批，见 §2.3）

| 级别 | 结论 | 最低条件 | 允许动作 |
|---|---|---|---|
| **L1 DOC-ALIGNED** | 设计稿内部 + 与 `CLAUDE.md`/`async_ipc_contract.md`/`issue_list.md` 一致 | 字段/术语/状态机可用同一张对照表逐条核验；所有伪代码/JSON/引脚表自洽；确定性用语（"100%""绝不重启"）已标注"目标"而非既成事实；硬件假设与 `issue_list.md` 不冲突 | 允许据此开工编码 |
| **L2 CODE-ALIGNED** | 实现与设计稿 + 契约一致 | L1 + `pio run -e pda2` SUCCESS（无新警告）+ 静态走查 §5 承重不变量全过 + 字段/函数与设计稿逐条对应 | 允许烧录冒烟，**不代表真机行为已验证** |
| **L3 SCENARIO-VERIFIED** | 关键场景在真机唯一推出预期结果 | L2 + §6 反例库适用场景真机回归通过（或纸面 SIM 推演 + 探针固件佐证）+ 开机冒烟串口无 panic/backtrace | 具备"合入主线 + 依赖"资格 |
| **L4 DEVICE-READY** | 可日常使用 / 可 OTA 推送 / 可整片刷机 | L3 + 双机（机#1 4G 版 / 机#2 音频版）各自回归无 P0/P1 + 电量/备份/分块烧录安全前置齐备 + 用户手册/`README` 同步 | 允许正式发布 / 推送 OTA |

`UNKNOWN` ≠ PASS；局部对齐不得外推为整机对齐。**本项目绝大多数代码评审止于 L2→L3
之间**（真机回归常 ⏸ 待用户实测），这是正常状态，不是失败——但须显式标注（§3）。

### 2.2 门禁（Phase Gate，改编 MACAO §2.2 + 参考 §6 三道门禁）

| 门禁 | 时点 | 判据 |
|---|---|---|
| **G-提交** | 每个 commit 落地前 | 按模块拆分（键盘/WiFi/AI/PenPal 各自独立 commit）；`Co-Authored-By: Claude <noreply@anthropic.com>`；无真实秘密入 tracked 文件（§5.4）；`config_keys.h` 不被提交（只提交 `.example`）；commit message 写清模块与动机 |
| **G-评审** | 提交评审申请前 | 申请文件齐全（§7.1 必备段落，缺一退回）；commit 范围 = 首末**含两端**（非 git 区间记法）；≥10 commit 分段（§8）；自审表填毕（§9）；连续两轮全 ⏸ 须解释（§3.4） |
| **G-合入** | reviewer 给结论后 | reviewer **A 全量接受**；或 **C 部分接受**且 P0/P1 已当批修复 + 补回归；无未绑门禁的阻断项；P0/P1 **不可**作为已接受风险（承自参考 R8） |
| **G-真机** | 声明 L3 / 依赖该改动前 | 设计稿 §7 回归清单 + 本批修复路径项在真机通过；⏸ 项登记 `TODO.md`/`issue_list.md` 并绑后续门禁；HW 证据附串口输出/可复现操作步骤（§3.5） |
| **G-发布/刷机** | OTA 推送 / 整片刷机 / 推公网 | G-真机 + 双机各自过 + 电量 ≥30%（OTA）+ NVS/SPIFFS 凭据分区已备份（`backups/`）+ 分块烧录兜底可用（build 文档坑 6）+ `SECURITY.md` 4 步（推公网时）+ STATUS（`TODO.md`）无 P0/P1 |

**实时门禁状态不记在本文件**——统一维护于 `TODO.md` 阻塞项 + `issue_list.md` 台账。

### 2.3 代码审批三态（本项目实际用法，承自 `docs/reviews/README.md`）

reviewer 结论 = **A 全量接受 / B 退回修订 / C 部分接受**（C 须注明保留/回退项 +
P0/P1 修复要求）。这是 G-合入 的输入。设计稿评审则用 L1–L4（§2.1）。

### 2.4 L4 / G-发布 OPS 必测矩阵（承自 MACAO §7.2，固件域重建）

申请 L4 或过 G-发布（含 OTA 推送、整片刷机）时，下表每行必须给出
`VERIFIED` / `PARTIALLY_VERIFIED(必写前提)` / `CONTRADICTED` / `NOT_APPLICABLE
(须写技术理由)` 之一；**缺行即运维就绪证据不完整**：

| # | 运维场景 | 预期 |
|---|---|---|
| 1 | USB 分块烧录演练（坑 6 流程） | 全程可照抄重放；失败块幂等重试；完成后设备正常启动 |
| 2 | 整片 16MB 备份 + 恢复演练 | 备份 md5 记录在案；恢复后设备可用、NVS/SPIFFS 完好 |
| 3 | NVS/SPIFFS 凭据分区备份新鲜度 | G-发布前存在（分区级或整片均可）且晚于上次凭据变更 |
| 4 | otadata 清除回退演练 | 清 0xE000 8KB 后重启回 app0 旧版本 |
| 5 | OTA pending→rollback 实测 | 刷自毁测试固件：新固件未自证 → bootloader 自动回滚 app0 |
| 6 | OTA 下载中断用例 | 断网/坏 bin/清单 404：otadata 不翻转，重启回旧版，零损伤 |
| 7 | 低电量前置检查 | 电量 <30% 拒绝更新并提示充电 |
| 8 | 覆盖性刷机前备份 | 探针/旧版固件刷入前 `backups/` 已有当期备份 |

---

## 3. 证据类型与验证状态（本项目核心，替代参考的 pytest/CI 模型）

### 3.1 证据类型（改编 MACAO §3.1 为固件域）

| 类型 | 含义 | 可达性 |
|---|---|---|
| **DOC** | 文档正文/表格/示例的直接引用（设计稿、`issue_list.md`、契约条款） | 开发机 |
| **SPEC** | 规范自身内部一致性（同一文档内伪代码/表格/正文是否互相矛盾） | 开发机 |
| **CODE** | reviewer 静态检查指定 commit 的 `.ino/.cpp/.h/.c`（`file:line`） | 开发机 |
| **BUILD** | `python -m platformio run -e pda2` SUCCESS + size 报告（RAM/flash 涨幅符合预期） | 开发机 |
| **SIM** | 纸面/脚本重放的时序推演（按状态机逐步推导某输入序列的结果，如"entry gen++ 后 create 期请求必判 stale"） | 开发机 |
| **PROBE** | 一次性探针固件实测（如 `test_pdm_mic`、`test_i2s_probe`、池水位 `pp_dbg_pool()`、空对象崩溃探针）——本项目"突变对照"的主要载体 | 真机 |
| **HW** | 真机手动回归（烧录 + 操作 + 串口观察）——**金标准**，但常 ⏸ 待用户实测 | 真机 |
| **OPS** | 刷机/备份/恢复演练（分块烧录、整片 16MB 备份、otadata 清除回退、NVS/SPIFFS 凭据分区备份） | 真机 |

> **无 TEST 档**：本仓库无自动化测试。参考里的 `TEST = 已入仓用例 + 评审方本地重跑`
> 在此**不存在**。最接近的替代是 **PROBE（探针固件）+ SIM（纸面推演）+ BUILD（编译/
> size）**。任何"测试通过"声明在独立重跑/真机复现前一律 `CLAIM_ONLY`。

### 3.2 验证状态（承自 MACAO §3.2，叠加本项目符号）

```text
VERIFIED            已复现（开发机 BUILD/CODE/SIM，或真机 HW/PROBE 附证据）   ✅
PARTIALLY_VERIFIED  部分复现（如编译过但真机未测；或纸面推演成立但未压测）     ◐
CLAIM_ONLY          仅作者声明（"真机通过"无串口证据 / "应该没问题"）          ⏸ 待用户实测
UNKNOWN             未知（reviewer 无环境复现，如未装 pio / 无设备）           ?
CONTRADICTED        已证伪（构造出反例击穿声明）                              ❌
NOT_APPLICABLE      不适用（如纯文档改动谈不到 HW；须写技术理由）             —
```

**本项目惯例符号**：申请书 §验证状态表用 `✅`（已过）/ `⏸`（待用户实测）/ `⛔`
（受阻，如额度 403、服务器不可达）；与上表映射：`✅→VERIFIED`、`⏸→CLAIM_ONLY`、
`⛔→UNKNOWN(写明受阻原因)`。

### 3.3 证据最低要求（承自 MACAO §3.3，按四级结论）

- **L1**：DOC/SPEC 为 VERIFIED（确切文件路径 + 行号，交叉引用不矛盾）；
- **L2**：CODE 为 VERIFIED + BUILD 为 VERIFIED（编译过 + size 涨幅符合预期）；
- **L3**：SIM/PROBE/HW 覆盖所有适用 P0/P1 场景且为 VERIFIED；
- **L4**：OPS 为 VERIFIED + 双机 HW 回归完成。

"读了标题""作者说已烧录"不能使 HW 证据达到 VERIFIED；必须能指出串口输出/操作步骤/
探针读数。**改 `config/lv_conf.h` 后未 `-t clean` 的 BUILD 证据无效**（build 文档
坑 7：`-include` 无依赖跟踪，池"假扩容"——size 报告 RAM 不涨即穿帮）。

### 3.4 ⏸ 待用户实测的纪律（本项目专属，承自 `docs/reviews/README.md`）

- HW 类声明在用户真机复现前**默认 `CLAIM_ONLY`（⏸）**，不得记 VERIFIED；
- 申请提交前自查：真机回归清单是否仍**全 ⏸**？**连续两轮全 ⏸ 会被评审标 High
  流程问题**（承自 README 评审纪律）——须解释为何无法当批实测（缺设备/缺服务器/
  缺外设），并把 ⏸ 项登记 `TODO.md` + 绑 G-真机；
- ⏸ 不等于"没问题"，也不等于"已接受风险"——它是**未验证**，门禁到点须清零或显式
  风险接受（§4.5）。

### 3.5 复现证据归档（承自 MACAO §3.4/§3.5，适配无测试 CI）

- **参照系必须与结论一致**：谈"某 commit 编译过"→ 须在该 commit 干净 checkout 跑
  `pio run`，不能在含未提交改动的工作树跑后外推；谈"双机一致"→ 须两台各自实测，
  不能单机外推（机#1 4G 版与机#2 音频版硬件不同，§5.6）；谈"开机不重启"→ 须真机
  烧录 + 串口冒烟，不能纸面外推。
- **HW/PROBE/OPS 证据须可照抄复现**：申请书/结果里写清 ① 操作序列（按了哪些键/
  进了哪个屏/插拔了什么）② 串口输出摘要（关键行，如 `boot 40s 无 panic`、池水位
  读数、`send: N context msgs`）③ 所连设备（COM5 机#1 / COM6 机#2）+ 完整 commit SHA。
  只写结论不写操作/输出的 HW 声明视为 `CLAIM_ONLY`。
- **探针固件归档**：用于支撑 P0/P1 定级的探针（如 bisect 定位池耗尽的 `pp_dbg_pool()`、
  `test_pdm_mic` 判定链）须随报告登记所验证的 `issue_id`、执行时 commit、依赖前提
  （设备/外设/网络）、最后执行日期。探针刷机会覆盖出厂 demo——须先整片备份（`backups/`）。

---

## 4. 定级方法（改编参考 §0.2 + MACAO §8）

### 4.1 后果类（影响维度，重建为固件域）

| 类 | 后果 | 本项目典型案例 |
|---|---|---|
| **A** | 不可恢复 / 泄密 / 不能启动 | 变砖、重启循环、堆损坏、看门狗复位、API key 泄漏到 tracked 文件、key 被截断致凭据失效、NVS/SPIFFS 凭据分区损坏 |
| **B** | 完整性 / 契约破坏 / 状态机违约 | 违反 `async_ipc_contract.md`（busy 无 gen 释放、worker `new` 未被 UI `delete`、任务读共享 UI buffer）、含 `std::string` 结构被 memset、`= T()` 大聚合压爆 8KB UI 栈、SPIFFS 非原子写致日志损坏、幂等键失效致重复发信 |
| **C** | 安全控制失效无直接数据后果 / UI 卡死 | busy 永久卡死（屏不可用但可重启恢复）、TLS Trust toggle 影响面未声明、秘密掩码显示失效、僵尸 worker 堆积（可恢复）、LVGL 池耗尽致点击死机（可重启） |
| **D** | 算法 / 边界正确性 | 多轮上下文轮次配对错（assistant 开头）、月边界/时区（localtime CST-8）算错、天气部分刷新误存成功、forecast 零条仍判有效、键盘 INT_STAT 误当读清致溢出破坏修饰键 |
| **E** | 可用性 / 运维 / 可维护性 | 状态行文案误导、EPD 刷新策略次优、串口日志缺判据、注释失效、命名 |

### 4.2 后果×可达性判定表（承自参考 R2，可达性档重建为固件域）

**可达性档**：
- **1 开机即触发 / 正常操作单方可达**：进屏即崩、每次返回必重启、正常发信即堆损坏；
- **2 需特定输入序列 / 并发时序 / 误配置**：连续 Close→重试堆积、离页后旧结果到达、
  缺 `config_keys.h`/`env.cfg` 时回落空 key、特定键序触发控制字节入草稿；
- **3 需特定硬件变体 / 缺外设 / 刻意反常**：仅机#2 音频版可达（DAC/麦克风）、仅 4G
  版可达（A7682E）、仅 exFAT SD 卡、仅低电量、仅探针固件（生产无调用方）。

**降级须注明回升条件**（承自 MACAO §8.2）：因可达性档 2/3 下调的缺陷，条目必须
写明**回升触发条件**——如"接线即回升至 Px"、"机#1 刷音频变体固件后回升"、"上
游校验放宽即回升"。影响范围扩大（另一变体出现、新调用方接入）自动回升原级，不
得无感维持降级。

**有声表**（缺陷有错误信号/串口日志/显性症状，如 panic/backtrace/明显错文案）：

| 后果 \ 可达性 | 1 开机/正常操作 | 2 特定序列/并发/误配置 | 3 特定变体/缺外设/反常 |
|---|---|---|---|
| **A** 不可恢复/泄密/不能启动 | P0 | P0 | P1 |
| **B** 完整性/契约破坏/状态机违约 | P1 | P2 | P3 |
| **C** 安全控制失效/UI 卡死 | P2 | P2 | P3 |
| **D** 算法/边界正确性 | P2 | P3 | 观察 |
| **E** 可用性/运维/可维护性 | P3 | P3 | 观察 |

**静默表**（无错误信号、无串口日志、功能表面正常——B–D 类整体上移一级；A 类
两表相同（P0 已是下限，无上移空间，最高 P0）；E 类"静默"是常态，不上调）。本
项目静默缺陷尤其危险（无自动化测试兜底，真机才暴露）：

| 后果 \ 可达性 | 1 | 2 | 3 |
|---|---|---|---|
| **A** | P0 | P0 | P1 |
| **B** | P1 | P1 | P2 |
| **C** | P1 | P1 | P2 |
| **D** | P1 | P2 | P3 |
| **E**（不上调） | P3 | P3 | 观察 |

> 本项目典型**静默 A/B**：key 缓冲 96 字符把 126 字符 sk-api key 截成 95（无报错，
> 只在调用时 401）；`= pp_state_t()` 把 ~15KB 临时对象压上 8KB loopTask 栈（每次
> 返回必崩，但根因隐蔽）；create 期自动同步被 entry gen++ 作废（表面"同步完成"，
> 实则结果被丢 + busy 永久泄漏）。

**定级三步（每步留痕，承自参考）**：
1. 判根因：**当批引入或当批触及该路径** → 按症状查表（静默查静默表）；**根因既有
   且本批未触及** → 一律查有声表并标注"根因既有"（承自参考 R15）；
2. 报告留一行坐标（如 `B/2/静默 → P1`、`A/1/有声 → P0`），使定级可被一眼复算；
3. 表判不了的新型后果 → 决策项，属主裁一次，同轮把该情形补进表（承自参考 R12）。

**三级标签 + 绑定门禁**（承自参考 R1/R16）：结论 = **阻断**（本批必须修）/
**应修**（登记 `issue_list.md` 台账 + 绑 G-合入｜G-真机｜G-发布 之一，到门禁时升级
为阻断）/ **Nit**（≤3，不绑门禁）。"通过/A 全量接受"不得挂必修项。**P0/P1 不可作为
已接受风险**（承自参考 R8）。

### 4.3 两轴证据标注（承自参考 R13）

- **轴一 验证状态**（§3.2 六态）：`VERIFIED` / `PARTIALLY_VERIFIED(必写前提)` /
  `CLAIM_ONLY` / `UNKNOWN` / `CONTRADICTED` / `NOT_APPLICABLE`；
- **轴二 契约符合性**（四值）：`CONFORMS` / `VIOLATES(async_ipc_contract.md §章节
  或 CLAUDE.md 第 N 条)` / `UNDECIDED(决策项)` / `NO_CONTRACT`；
- **两轴独立标注、禁止互相推断**：BUILD 绿（编译过）只证明行为不证明合规——编译
  通过的代码照样可以 `VIOLATES` 契约（如 memset `std::string` 能编过但 UB）；HW 绿
  （真机这次没崩）不证明无竞态（可能只是没压到）；
- `VIOLATES` + 根因当批 → 阻断；根因既有 → 台账绑门禁；`NO_CONTRACT`（新型异步
  任务不在契约表内，如 weather fetch）→ 同轮补契约草案或显式登记为例外（承自参考
  R12；实例见 `issue_list.md` §11 weather 契约层 Medium）。

### 4.4 声明验证矩阵（承自参考 §0.6 + MACAO §4，重建为固件域）

含具体数字或绝对措辞的**强声明一律进矩阵**，引用契约/文档章节号；作者声明默认
`CLAIM_ONLY`，评审方独立重跑/真机复现后方可 `VERIFIED`：

| # | 矩阵行（本项目高频强声明） | 必须验证 |
|---|---|---|
| ① | "编译 SUCCESS / RAM x% / flash y%" | BUILD：评审方独立 `pio run -e pda2`，size 报告涨幅是否符合预期（改 lv_conf.h 须先 `-t clean`） |
| ② | "真机通过 / 全链路跑通 / 双机一致" | HW：操作序列 + 串口输出摘要 + 设备（COM5/COM6）；单机不得外推双机（§5.6） |
| ③ | "busy 正常释放 / 不卡死" | CODE+SIM：所有 stale-drop 分支（离页/取消/超时/后台退出）是否都释放 busy 且 gen 匹配（§5.1） |
| ④ | "无内存泄漏 / 无双重释放" | CODE：worker `new` 的结果 UI 是否 `delete`；任务自删请求快照；队列深度 vs 在飞数 |
| ⑤ | "池水位充足 / 不再耗尽" | PROBE：`pp_dbg_pool()` 读数；同点水位是否 ≥ 安全余量（§5.5） |
| ⑥ | "key 不被截断 / 秘密不入 tracked 文件" | CODE：缓冲长度 ≥ 最长 key（sk-api 126 字符）；`git grep` 真实 key = 0 hits（§5.4） |
| ⑦ | "gen 门控正确 / 迟到结果被丢弃" | SIM：entry gen++ 后 create 期请求是否必判 stale；取消 = gen+1 + busy=false |
| ⑧ | "开机不重启 / 无 backtrace / 看门狗不复位" | HW：烧录 + 串口冒烟（≥40s）；栈/池水位是否在安全区 |
| ⑨ | "原子写 / 掉电不损坏" | CODE+SIM：SPIFFS tmp→flush→bak→校验和链；NVS 双槽 `base.0/1` + `active` 翻转 |
| ⑩ | "EPD 不阻塞实时路径 / 音频不断粮" | CODE+HW：播放期是否抑制 EPD 刷新；光标闪烁是否关（§5.7） |

> 矩阵是**导航辅助，不缩小评审范围**；漏列视同未声明，按当批引入定级。纯文档档
> 豁免 ①–⑩，按 DOC 事实行组织（交叉引用 + 可验证陈述）。

### 4.5 风险接受登记（承自 MACAO §8.3，台账 = `issue_list.md`）

P2/P3 可在明确风险接受后延期，但"作者声明采纳 + reviewer 口头备注"**不构成**合规
接受。须在 `issue_list.md` 登记：`issue_id`、`severity`、`first_commit`、
`last_verified_commit`、`evidence(file:line 或探针读数)`、`risk_acceptor(用户，不可
留空)`、`scope(生效边界，如"仅机#2 音频版")`、`expiry(到期 commit 数/里程碑/日期)`、
`resolution_commit(未修复填 null)`、`status(RISK_ACCEPTED)`。**到期或影响范围扩大
自动重新变为阻断项**，严禁无感续期。P0/P1 不适用风险接受（§4.2）。

---

## 5. 承重不变量清单（本项目专属，替代参考的 IDOR 清单）

> 这是本项目评审的**第一焦点域**——相当于后端指南的 IDOR 复核清单。每条都来自
> `CLAUDE.md` / `async_ipc_contract.md` / `issue_list.md` 的真实事故。逐一确认改动
> 未破坏下列不变量；破坏即按 §4 定级（多数为 A/B 类）。

### 5.1 异步 IPC 契约（`docs/async_ipc_contract.md` 是权威，违反以契约为准）

| 不变量 | 应有校验 | 违例后果 |
|---|---|---|
| 所有权转移 | worker `new` 结果结构 → `xQueueSend` → UI `xQueueReceive` 后 `delete`；任何线程不得 `delete` 对方创建的对象 | 双重释放 / 泄漏（B） |
| 结果携带代次 | 结果结构第一字段 `uint32_t gen`（页面代次或请求代次） | 迟到结果误覆盖新状态（B） |
| busy 仅 UI 线程读写 + 带 gen | 任务线程禁访问 busy；只有 gen 匹配的结果才释放 busy（`*_busy_gen`）；旧代结果不得解锁新请求 | busy 卡死 / 误解锁（C/B） |
| 队列先建后 busy | `xQueueCreate` 返回值必检；失败立即提示且不置 busy、不启动任务；队列 NULL 时任务不启动 | 任务向 NULL 队列发送 / busy 永久卡（C） |
| 每任务独占请求快照 | prompt/base/model/key 启动前复制进任务自有结构（`new`→任务内 `delete`）；任务禁读 UI 拥有的可变缓冲 | 跨核 UAF / 数据竞争（B） |
| 任务栈与优先级 | 栈 1024×8（NTP 例外 1024×4），优先级 1；无看门狗依赖 | 栈溢出 / 调度异常（A） |
| 取消语义 | UI"取消" = 请求/页面 gen+1 + busy=false；任务不强杀（HTTPClient 自身超时）；迟到结果 gen 不匹配被丢弃并释放内存 | 僵尸任务 / busy 泄漏（C） |
| 超时语义 | UI 倒计时是**绝对 deadline**，覆盖 NTP（≤5s）+ 连接 + 读取全程；超时同样递增请求 gen | 超时后迟到结果覆盖状态（B） |
| 生命周期 | `destroy()`：kbd_active=false、gen+1、busy=false、弹窗关闭；`entry()`：gen+1 使上次访问迟到结果失效 | 跨访问状态残留（B） |
| 多轮上下文轮次配对 | 一轮 = user + 紧随 assistant（前一条必须是 user 才配轮，否则停止窗口，绝不 assistant 开头）；尾部允许一条孤立 confirmed user；pending 跳过；UI 标记剔除；8KB 按整轮计入，最旧先裁；串口 `send: N context msgs` 恒偶（孤立 user 尾除外） | 上下文错乱 / 超预算（D） |

> **纯本地屏不适用契约**（Sleep、Keys、GPS、计算器、字典——不创建任务），但仍遵守
> 各自销毁前清理（如 Sleep 屏 timer 句柄保存 + NULL 守卫）。**新型异步任务**（如
> weather fetch）若不在契约表内，须同轮补契约或显式登记例外（§4.3 `NO_CONTRACT`）。

### 5.2 UI 线程内存纪律（本项目最贵的两条教训）

- **绝不对含 `std::string`（或非平凡 C++ 对象）的结构 `memset`**——一律值初始化
  `*out = T{};`。规则收敛为"任何 `pp_*_t` 一律值初始化，不再 memset"。违例 = 堆
  损坏/异常重启（A/B，静默）。POD 数组（`lv_obj_t*` 指针数组、memcpy 的 POD）可 memset。
- **绝不在 UI 线程 `= T()` 赋值一个肥聚合**——`pp_state_t`（~15KB）临时对象压爆
  8KB loopTask 栈 → 每次返回必崩（A，开机/正常操作可达）。改 per-field
  `pp_state_reset()` + `lv_async_call` 延迟 pop。**审计现存 `= T()`：剩余应 ≤400B**。
- **屏幕是每次访问重建的**（非 boot/register 期一次性）：`create()` 在每次
  `scr_mgr_push` 时跑（经 `scr_mgr_active`→`scr_mgr_default_style`，`ui_scr_mrg.c:33`），
  pop 跑 exit+destroy 并删除控件树。任何"create 期发起请求"都会被 entry 的 gen++
  作废（§5.1 实例）——自动同步等须移入 `entry()`，在 gen++/active=true **之后**发起。

### 5.3 LVGL / EPD 纪律

- **LVGL 内存池 64K**（曾 48K 耗尽致点击死机，`721e04a`）。对象 churn 多的屏（PenPal
  行表头、等待框 `lv_btn_create`）须留 `pp_dbg_pool()` 类观测点；同点水位须 ≥ 安全
  余量（修复后 ~17K）。池耗尽 = EXCVADDR 小地址崩溃（C，可重启恢复但影响大）。
- **改 `config/lv_conf.h`（或任何只经 `-include` 引入的头）必须 `-t clean` 再编**，
  否则增量编译"假生效"（build 文档坑 7）——BUILD 证据须看 size 报告 RAM 涨幅核对。
- **E-paper 渲染慢**（~0.3s 局部 / ~1–2s 全刷）：不要动画、不要滚动直播内容——用
  分页（`LV_OBJ_FLAG_HIDDEN` 切换）。`LV_COLOR_DEPTH=1`，所有图片必须单色。
- **EPD 刷新会阻塞主循环 → 音频泵断粮**（app 内 TTS 断续噪声、退出反而清晰）。
  播放期须抑制 EPD 刷新（退出钩子兜底释放）+ 关光标闪烁（`LV_PART_CURSOR`
  `anim_time`）。违例 = 实时路径被打断（C/D）。
- **非 UI 线程禁止调 LVGL**——跨线程用 FreeRTOS 队列 + `lv_async_call`（ai_voice_task
  同款）。触摸配置了但**键盘驱动所有导航**；别假设指针事件在纯改外观后可靠触发。

### 5.4 秘密链（`CLAUDE.md` 第 2 条，无真实 key 入 tracked 源码）

- 查找序：NVS → SPIFFS `/env.cfg`（`env_secrets.cpp` 解析）→ gitignored
  `config_keys.h` → 空默认。`env.cfg.example`/`config_keys.h.example` 入 tracked，
  真实文件**绝不**入库。
- **key 缓冲长度须 ≥ 最长 key**：sk-api key 126 字符——env 解析 val、`resolve_chat_cfg`
  的 `k[96]`、AI Config 输入框三处都曾是截断点（96/79 把 126 截成 95/79，静默 401）。
  改任何 key 路径须核对三处一致 + 判据日志（打印 key 长度）。
- 配置界面秘密**遮蔽显示**（中间 1/3 星号，首尾保留可辨认；WiFi pass 至少 4 星）+
  "影子缓冲"（真实值驻留，渲染走掩码）。违例 = 泄密（A）。
- 历史已 `git filter-repo` 清洗（key → `REDACTED-OPENROUTER-KEY`，全对象 0 hits）；
  该 key 永久作废。新发现真实 key 入 tracked = P0 阻断 + 走 `SECURITY.md` 轮换/清洗。
- 评审强声明 ⑥：`git grep` 真实 key = 0 hits 方可 `VERIFIED`。

### 5.5 键盘 / 外设驱动

- **TCA8418 `INT_STAT` 是 W1C**（写 1 清除，**不是**读清除）——旧代码搞反致溢出时
  破坏修饰键状态（`issue_list.md` §1.5）。3 层（小写/Shift 大写/Sym 锁定），修饰键
  状态（双 Shift OR、Alt 临时符号层、Sym 开关）在驱动内维护。硬件 FIFO → 16 深软件
  字符 FIFO，`keypad_get_val()` 消费。页面切换调 `keypad_clear_chars()` 清 FIFO（防
  跨屏残留 Backspace）。
- **麦克风键 = 按住说话**（键位 (3,6)，正常层专属码 `'\f'`，双机实测一致）；MIC 键
  按下录音提示、松开进 Waiting。
- 控制字节守卫：文本输入须有 `c >= ' '` 守卫（`'\v'` 已修，其他控制字节仍可能写进
  草稿并发往 API，D 类）。

### 5.6 硬件变体差异（双机档案，`issue_list.md` §16）

- **机#1**：4G/A7682E 版，COM5。**机#2**：V1.1 音频选配版，COM6，PCM5102A 在板。
- **GPIO41 = 音频段电源轨**（4G 批次兼模组电源）——`pcm5102a_init()` 须拉高 GPIO41，
  否则"板上无 DAC"假象（8 月误判，9 月推翻：两台机都可做 TTS 朗读）。
- 任何"仅某变体可达"的缺陷按 §4.2 可达性档 3 定级 + 注明"另一变体不可达"；任何
  "双机一致"声明须两台各自实测（§3.5），单机不得外推。
- 机#2 固件移植前置：触摸需接 CST328 驱动（现树只有 hyn/CST66xx 路径）。
- SD 卡必须 FAT16/FAT32（exFAT 显示 0MB）；挂载失败提示须是"事实 + 建议"
  （`SD hint: mount failed` + `try FAT16/FAT32?`），不得过度断言格式（`c8f62f3`）。
- TLS Trust toggle 影响**所有** HTTPS 消费者（不只 AI），UI 文案须说明（`issue_list.md` §7.4）。

### 5.7 实时路径 / 看门狗

- EPD 刷新、音频泵、计时（Sleep 倒计时、usage 月边界）共享主循环——任一阻塞都饿死
  其他。Sleep 倒计时须由**本屏自己的 EPD flush 序列**门控。
- 月边界用**本地时间**（setup 设 TZ=CST-8）；`localtime()` 用户（Calendar/Sleep
  时间戳）随之。
- OTA 写 flash 期间主循环须保持泵 `lv_timer_handler`；新固件须在 `setup()` 末尾
  （NVS/SPIFFS/外设初始化全过后）调 `esp_ota_mark_app_valid_cancel_rollback()` 自证，
  否则升级即回滚（`docs/ota-update-design.md` §5）。

---

## 6. 反例与边界场景库（本项目专属，承自 MACAO §6）

> 评审异步任务、生命周期、状态机、写路径时，至少对照下列场景逐一推演（SIM），并
> 记录预期结果能否从代码/契约唯一推出。**成功样本只证明 happy path，不证明全量
> 质量**——单次真机跑通不能代替反例库各异常分支的逐项击穿。

```text
【异步/生命周期】
离页后旧结果到达            → gen 不匹配 → 丢弃 + 释放内存（busy 若被该结果持有须一并清）
请求中 Close/Cancel         → gen+1 + busy=false；任务不强杀；迟到结果丢弃
队列创建失败                → 提示 + 不置 busy + 不启动任务
后台任务期间退出屏幕        → exit 收 waitbox 但 busy 须由 stale-drop 分支释放（否则永久卡死）
连续 Close→重试 ×N          → 并发上限（如 inflight>=2 拒绝"wait - previous closing"），不无界堆积
create 期发起请求 + entry gen++ → 请求必判 stale（自动同步须移入 entry，gen++ 之后发起）
结果到达时屏已被覆盖（push-away）→ 队列排空须在 kbd_active 早退之前跑（factory.ino 承重细节，勿动）

【内存/栈】
含 std::string 结构被 memset → 堆损坏/异常重启（须值初始化 *out = T{}）
= T() 肥聚合（~15KB）在 UI 线程 → 压爆 8KB loopTask 栈，每次返回必崩
LVGL 池 churn 至耗尽          → 点击 lv_btn_create 失败，EXCVADDR 小地址崩溃
worker new 结果 UI 未 delete   → 泄漏；UI delete 两次 → 双重释放

【掉电/原子写】
SPIFFS 写中途掉电            → tmp→flush→bak→校验和链；loader 提升 .bak；CHL1/CHL2 双解析
NVS 双槽写中途掉电           → base.0/1 + active 翻转，回退到上一完好槽
崩溃遗留 /chat.log.tmp        → 下次保存覆盖（无害残留，登记即可）
OTA 下载中途断电/误重启       → otadata 未翻转，重启回旧版；app1 半截包无害
新固件已切换但自毁            → pending 未自证 → bootloader 自动回滚 app0（§2.4 行 5）
清单 404 / bin 截断 / 校验失败 → 拒绝更新，无副作用（§2.4 行 6）
低电量启动更新                → <30% 拒绝并提示充电（§2.4 行 7）

【网络/超时】
断网 / WiFi 断 / key 缺       → 任务超时（HTTPClient 上限）；UI 绝对 deadline 覆盖 NTP≤5s+连接+读取
NTP 未同步即发请求            → http_ensure_time；时间错致 TLS/签名失败
LLM 端点 180s 超时            → busy 持续；取消 = gen+1，不强制中止传输（登记可选后续）
provider 默认 base 错（国内域 vs 国际域）→ sk-api key 在错域 401（minimax.io vs minimaxi.com）

【外设/变体】
缺 SIM（4G 版 A7682E 无 SIM）  → 初始化失败；菜单门控显示出 PCM5102 入口
exFAT SD 卡                    → 显示 0MB；提示"事实+建议"，不过度断言
GPIO41 未拉高                  → "板上无 DAC"假象，音频无声
仅机#2 音频版可达的缺陷        → 注明机#1 不可达（可达性档 3）
探针固件刷入                   → 覆盖出厂 demo（须先整片备份 backups/）

【实时路径】
EPD 全刷期间音频泵             → 断粮/TTS 断续（播放期须抑制 EPD + 关光标闪烁）
光标闪烁 anim                  → 阻塞主循环 → 音频泵断粮
Sleep 倒计时被别屏 EPD flush 干扰 → 须由本屏自己的 flush 序列门控

【边界值】
月边界 / 时区（localtime CST-8）→ usage 统计月重置、Calendar/Sleep 时间戳
多轮上下文 8KB 预算            → 整轮计入，最旧先裁；assistant 不得开头；孤立 user 尾部允许
forecast 零条                  → 不得仍判 data_valid（须显式无效）
天气部分刷新                   → 不得误推进 last_fetch_time/落盘（仅完整刷新才存）
key 126 字符                   → 三处缓冲（env val / k[96] / 输入框）都须容下
```

每个场景应保存：来源 `file:line`、预期结果、实际能否唯一推导、验证所需最小 SIM/PROBE/HW
步骤、**该场景失效时的影响**、**最后一次实际验证的 commit 与日期**。沿用历史轮次结论
须在"最后验证"列注明沿用来源，不得默认其在当前 commit 仍成立。

---

## 7. 评审申请与结果（承自本项目 `docs/reviews/README.md` 实际惯例）

### 7.1 申请文件结构（缺一即退回，G-评审）

文件名：`docs/reviews/<主题>-review-request-<commit范围>.md`
（**主题化前缀**是现行惯例——正例 `penpal-pool-freeze-review-request-721e04a.md`
与 `allinone-design-review-request.md`；`wifi-config-keyboard-*` 系列是历史文件
保留原名，新申请**不再**沿用该前缀。`<commit范围>` = 本轮实际覆盖 commit 的
**首末 id 含两端**，如 `d22007d..4c3c9b1`，**非 git 区间记法**；单 commit 直接写
id，如 `acc3893`）。**绝不覆盖旧申请**；被吸收的旧申请 `git rm` 删除并在合并申
请头注明"git 历史保留"。

| 段 | 内容 | 依据 |
|---|---|---|
| 头部 | 申请人 / 申请日期 / 关联分支（`HD-V2-250915`）/ 关联 commit（逐个 + 一句话）/ 背景（上轮结论文件 + 处置）/ **硬件**（哪台机、COM 口、是否已烧录） | 惯例 |
| 头部自校 | 粘贴 `git rev-list --count <base>..<sha>` = N 与 `git diff --name-only <base>..<sha> \| wc -l` = M，归属表行数 = M（每文件一行，纯文档"不评"也写）；基线 SHA == `TODO.md`/`CHANGELOG.md` 当前基线 | 承自参考 R14/R12 |
| §1 变更明细 | 每条发现/改动 → `file:line` + 改了什么 + 为什么（根因核实过程）；同族顺带修复单列 | 惯例 |
| §1.x 靶向清单 | 每行为级改动一行：`文件:行区间 \| 函数 \| 行为变化一句话 \| 波及面`——**行区间不得省**；"相邻但未改"高危邻居为**强制列**；被调叶子有行为变化时不得在调用方写"零改动" | 承自参考 §8.2 |
| §2 验证状态 | 表（项目/状态/证据）：编译✅+size、烧录✅、开机冒烟✅+串口摘要、静态复查✅、**真机回归 ⏸**（逐项操作序列）；强声明进 §4.4 矩阵 | 惯例 + R10 |
| §3 遗留项 | 可选后续 + 顺延的回归清单 + 待结果申请清点 | 惯例 |
| §4 回滚方案 | `git revert <sha>`（或分块烧录回退 / 清 otadata，OTA 类） | 惯例 |
| §5 申请审批事项 | A 全量接受 / B 退回修订（具体意见）/ C 部分接受（保留/回退项）+ 审批人/日期 | 惯例 |
| §6 开发者自评 | 最有把握 / **最没把握（≤3 处，强制，留空退回；写"无"须对照 §9 逐条论证）** / 我知道没测到的；最没把握只写条件交点，禁夹带未验证结论 | 承自参考 §8.4 |

### 7.2 结果文件结构（承自本项目实际 + 参考 §5）

文件名：`docs/reviews/<主题>-review-result-<commit范围>[-<reviewer>].md`
（**主题化前缀**与 §7.1 申请一致——正例 `penpal-pool-freeze-review-result-721e04a-qwen.md`、
`ota-update-design-review-result-36e88df-codex.md`；`wifi-config-keyboard-*` 系列是历史
文件保留原名，新结果**不再**沿用该前缀。`<commit范围>` = 被评审对象的首末 commit
**含两端**，单 commit/单设计稿直接写 id——**强制不可省**：它是**跨轮防覆盖的锚**，
同一主题的不同评审轮（如设计 v1 `36e88df` 与 v2 `be6a52c`）各得独立文件，绝不互相
覆盖。`<reviewer>` = **评审模型名**（codex/kimi/gpt/gemini/claude/grok/qwen…，**不用
CLI 名**；既存 `-opencode.md` 是历史 CLI 名文件，保留不改名），单评审可省后缀。
**评审结果由评审方直接放入，绝不覆盖**（superseded 申请可合并进 range 申请，但结果
永存；同一 reviewer 复审新版本 = 带新 commit 锚的**新文件**，不改旧文件）。

```markdown
# 评审结果：<对象>（<reviewer>）
- 评审日期 / 申请文件链接 / 评审提交（逐个 SHA）/ 评审范围 / 评审结论（A/B/C 或 L1–L4）
## Findings
### P0/P1/P2/P3：<一句话标题>
- 位置：file:line
- 证据：<反例输入/调用序列/串口输出/探针读数>
- 影响：<后果 + 可达性 + 坐标，如 B/2/静默 → P1>
- 两轴：验证状态(§3.2) + 契约符合性(§4.3)
- 最小修复：<方向 + 须补的回归（SIM/PROBE/HW）>
## 已通过项
## 验证说明（评审方环境：是否装了 pio / 有无设备 / 复跑了什么 / 未独立复现的标 UNKNOWN）
## 对称三格（强制，承自参考 §5）
  ① 是否核了全区间 diff（不只申请的靶向清单）
  ② 快照是否独立复跑（记命令 + 输出摘要；无设备/无 pio 如实写"无法复现"非"证伪"）
  ③ 申请"最没把握"各处是否构造了反例
## 审批意见：[ ] A 全量接受  [ ] B 退回修订  [ ] C 部分接受
```

**入库轮义务**（承自参考）：结果入库的那一轮必须把上一轮全部"应修"逐条落
`issue_list.md` 台账或标已修——台账是"应修不阻断"的唯一承接面，漏收即丢发现。

### 7.3 评审口径（承自 `2026-08-commits-review-status.md` §3）

- **只报真实缺陷**：跨任务/LVGL/定时器边界的竞态、UAF、gen/busy 是否符合契约、缓冲
  溢出、非 UI 线程调 LVGL、逻辑错误、资源泄漏、错误路径、§5 承重不变量破坏。
  **不报风格/命名/commit message 问题**（那些是 Nit 或不报）。
- 同批内后被修复的问题标 `superseded`/`fixed-later`，不重复计。
- 产出格式：每 commit 一行结论（`clean`/`issue`/`superseded`/`docs-only`）→ Findings
  编号列表（severity + hash + file:line + 影响 + 最小修复）→ Cross-cutting（跨批/契约层）。
- 大 commit 早批（如 phase-0 试点）：疑似问题先对照当前 HEAD 确认是否仍存活（后续
  ~70 commit 已大改这些屏），已被后续修复的标 `fixed-later` 不重复上报。

---

## 8. 分段评审（≥10 commit，承自 `docs/reviews/README.md` §1.11）

- **单次申请 commit 数 ≥ 10 时应分段评审**：按 5–7 个 commit 一段拆成多个 doc 段，
  各段独立走 review 流程，避免单 reviewer 认知负担与超长回滚列表。
- 多哈希取文件并集用 `git show --name-only --format=""`（**勿用** `git diff-tree`
  喂多哈希——会按"多父合并比较"语义输出空，文件清单不全，工具坑见
  `2026-08-commits-review-status.md` §2.1）。
- 段内无 merge commit 时 `git show --name-only` 并集即完整评审对象；滤除
  `docs/`/`.github/`/`*.md` 后为代码评审面，但**归属表仍须列每个文件**（§7.1 头部自校）。
- 全量评审中断（如额度 403）须留断点状态文档（范围/批次划分/resume 上下文），供后续
  会话直接恢复，不重做清点（实例：`2026-08-commits-review-status.md`）。

---

## 9. 评审自审清单（承自参考 §0.7 + MACAO §9，重建为固件域）

> 安全·异步·生命周期·写路径档**强制**，报告末尾填表。同一 reviewer 连续几轮对同一
> 类盲点漏审须显式登记并激活对应项。**条目适用档**：【C】=代码评审、【D】=设计
> 稿评审（DOC/SPEC 证据即可）、【ALL】=两者；按 §1.1 的对象裁剪执行——设计稿
> 评审只需过【D】/【ALL】条目，代码评审需过【C】/【ALL】条目。

```text
【异步/契约】【C】
□ 1.  非 UI 线程是否调了 LVGL？（须队列 + lv_async_call）
□ 2.  busy 是否只被 gen 匹配的结果释放？所有 stale-drop 分支（离页/取消/超时/后台退出）都释放 busy？
□ 3.  worker new 的结果 UI 是否 delete（无泄漏/无双重释放）？任务是否自删请求快照？
□ 4.  队列是否先建（且检返回值）后置 busy？NULL 队列时是否不启动任务？
□ 5.  任务是否持有 UI buffer 的副本（非共享 volatile/std::string）？
□ 6.  迟到结果三条路径（离页/取消/超时）是否都丢弃且释放内存？gen 是否携带在结果第一字段？

【内存/栈】【C】
□ 7.  含 std::string/非平凡对象的结构是否被 memset？（须 *out = T{}）
□ 8.  UI 线程是否有 = T() 赋值肥聚合（>400B）？（压爆 8KB loopTask 栈）
□ 9.  LVGL 池 churn 是否留观测点？同点水位是否 ≥ 安全余量？改 lv_conf.h 是否 -t clean？

【秘密】【C+D】
□ 10. key 缓冲三处（env val / resolve_chat_cfg k[] / 输入框）是否都 ≥ 最长 key（sk-api 126）？
□ 11. 真实 key 是否 0 hits 入 tracked 文件（git grep）？config_keys.h 是否未被提交？掩码显示是否生效？

【硬件/实时】【C】
□ 12. 硬件变体差异（机#1 4G / 机#2 音频 / GPIO41 / DAC / SIM / SD 格式）是否考虑？"双机一致"是否两台各自实测？
□ 13. EPD 刷新是否阻塞关键实时路径（音频泵/看门狗/计时）？播放期是否抑制 EPD + 关光标闪烁？
□ 14. 原子写链（SPIFFS tmp→bak→校验和 / NVS 双槽+active）是否掉电安全？

【方法论自检】【ALL】
□ 15. 每条 REJECT/P0/P1 是否给了可复现 file:line + 反例/操作序列？（无证据 → 疑问清单，不占缺陷）
□ 16. 作者"真机通过/编译过"声明是否附串口证据/可复现步骤，还是 CLAIM_ONLY？我是否独立重跑（BUILD）或如实标 UNKNOWN（无 pio/无设备）？
□ 17. 我复跑的每条命令，参照系与作用域是否与结论一致？（commit 干净 checkout vs 工作树；单机 vs 双机；§3.5）
□ 18. 本轮新增/修改的每个核心函数，在 examples/ 中是否有真实生产调用方？（grep -rn 排除定义处自身；零调用方 = CLAIM_ONLY，"已实现"≠"已接线"，承自 MACAO 模式 E）
□ 19. 不止查 happy path：§6 反例库适用场景是否逐一推演？
□ 20. 结论相反时是否核对对方事实（而非比票数）？契约/指南"已写但未实现"条目是否被当已知简化跳过？

【文档对齐】【D】（承 MACAO §9 漏审模式 A/D/B，设计稿评审每轮必查）
□ 21. 字段/键名的声明位置 vs 实际读取位置是否一致？（env.cfg 键名、OTA 清单
      JSON 字段、NVS 键——两处各写一套即漏审模式 A；minimax 域名 401 即前车）
□ 22. 设计稿/申请里每个代码块（JSON/YAML/命令行）是否真的可解析、可照抄执行，
      字段名与正文一致？（模式 D——"示意"代码会被照抄成无法解析的产物）
□ 23. 申请/文档里的 ✅/⏸/[x] 是否逐条有对应证据（串口行/命令输出/探针读数），
      而非把计划当作已做？（模式 B；结合 §3.2/§3.4）
```

---

## 10. 已知简化与决策项（承自 MACAO §5.2，缺陷与阶段性边界分离）

为避免每轮重复争论既定边界，维护常设"已知简化"表（置于 `issue_list.md` 或本 §10
附表）。**已知简化不记为缺陷**，不占 P0–P3 编号；reviewer 认为不再适用 → 单列
**决策项**（阐明业务代价/安全隐患/修改建议），与纯代码缺陷严格隔离。增删改须按 §11
修订程序（触发问题 + 正反例 + 独立复核），禁止单轮单方面放宽；每项须写"生产化/真机
验证前需补什么" + expiry；到期或影响扩大自动失去豁免、转回门禁。

当前已知简化（示例，权威表在 `issue_list.md`/`TODO.md`，本处不保时点）：

| 项 | 现状 | 接受理由 | 生产化前需补 |
|---|---|---|---|
| 无自动化测试 | 全靠真机手动回归 + 探针固件 | 嵌入式硬件在环，CI 无法覆盖外设/EPD/音频 | 关键纯逻辑（NVS 状态机、上下文裁剪）可提取无 Arduino 依赖单测（TODO 已登记） |
| PenPal 无可中止 HTTP 传输 | 用并发上限（inflight>=2）替代 | HTTPClient 中止需改传输层，收益/侵入比不划算 | 登记为可选后续（`acc3893` §3） |
| weather fetch 不在契约表 | 无页面代次，`vTaskDelete` 强杀在飞任务 | 先于契约存在 | 纳入契约（gen + busy_gen）或显式登记例外（`issue_list.md` §11） |
| SPIFFS `/chat.log` 整文件重写 | 已原子安全但写放大 | 当前可用 | 改 append+compact 或后台保存线程（TODO 已登记） |
| OTA 设计稿未实施 | `docs/ota-update-design.md` 待评审 | 设计阶段 | 6 项请评审重点（自动更新/自证点/明文限制/签名/组件选型/回滚实测）裁定后开工 |

---

## 11. 生效与修订（承自参考 §7 + MACAO §11）

修订本指南/判定表/承重不变量清单：

1. 指明**触发问题**（哪类漏审/事故导致了什么后果，引用 `issue_list.md` 条目或 commit）；
2. 提供至少**一正例一反例**；
3. **独立复核**（一份评审在修订后文本上通过）；不要求两名 reviewer；
4. 检查代码块可执行性（命令能照抄）与 Markdown 完整性；
5. 版本记录（下方修订表 + 文首版本号）。

**契约与代码同批或登记**：行为变更触及 `async_ipc_contract.md` 条款时，改文与代码
同一批入库；来不及时在 `issue_list.md` 登记"契约待改"——**不得留契约说 A、代码做 B**
（承自参考 F2 教训：测试钉死违约行为）。

**doc/假设 vs HD-V2 实物不符**：别从第一性原理论证——往 `issue_list.md` 加条目
（canonical 修复台账，每条：状态 + 修复 committish）。很多"这不可能 work"的注释
早已被某 commit 解决。

### 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.2 | 2026-09-13 | 结果文件名格式补全（触发问题：本轮 qwen 对 OTA 设计的评审结果落成 `ota-update-design-review-result-qwen.md`——**无 commit 锚**，而该设计已有 v1 `36e88df` / v2 `be6a52c` 两轮、codex 同轮文件为 `-36e88df-codex.md`，无锚文件名将与未来 v2-qwen 复审碰撞/覆盖，违反"绝不覆盖"）。修订：① §1.2 + §7.2 结果文件名由遗留 `wifi-config-keyboard-review-result-` 前缀改为与 §7.1 申请一致的**主题化前缀** `<主题>-review-result-<commit范围>[-<reviewer>].md`——**反例**：v1.1 只把 §7.1 申请改主题化、§7.2 结果仍留旧前缀，请求/结果命名不同步；**正例**：`penpal-pool-freeze-review-result-721e04a-qwen.md`、`ota-update-design-review-result-36e88df-codex.md`。② 明确 `<commit范围>` 为**强制 commit 锚**（跨轮防覆盖：v1/v2 各独立文件）。③ `<reviewer>` 改为**评审模型名**（codex/kimi/gpt/gemini/claude/grok/qwen…），**不用 CLI 名**——反例：旧列 `opencode`（CLI 名）；既存 `-opencode.md` 标为历史文件保留不改名。④ 同 reviewer 复审新版本 = 带新 commit 锚的新文件，不改旧文件。配套：把本轮无锚的 `ota-update-design-review-result-qwen.md` 经 `git mv` 改名 `…-36e88df-qwen.md` 并在 评审对象 行注明"针对 v1，v2 另起新文件"。 |
| v1.1 | 2026-09-13 | 九条修订（触发问题均为"声明/纪律与仓库现实或参考源不符"，每条含正反例；独立复核 = 提出九条意见的三文档对比评审本身，2026-09-13）：① §0 差异 1 + §3.1："无 CI"改为"无自动化测试（构建矩阵 CI 仅编译冒烟）"——反例：旧文声明与 issue_list §7.1 的 `.github/workflows/platformio.yml` 矛盾；② §7.1 文件名改主题化前缀惯例——反例："所有主题沿用 wifi-config-keyboard-"与 `penpal-pool-freeze-review-request-721e04a.md` 矛盾；③ §4.2 补 MACAO §8.2"降级须注明回升触发条件"（正例：仅机#2 可达缺陷在机#1 刷音频变体后须回升）；④ 新增 §2.4 L4/G-发布 OPS 必测矩阵 8 行（承 MACAO §7.2——旧版 L4 仅一句"OPS 为 VERIFIED"，无逐行证据要求）；⑤ §9 补【文档对齐】三条（字段/键名一致性、代码块可执行、✅≠证据，承 MACAO §9 模式 A/D/B——正例：minimax 域名 401 即模式 A 实例）；⑥ §9 各块标注适用档（C/D/ALL）+ §1.1 设计稿行同步（旧版要求设计稿走全部 20 条，busy/队列等条目对 DOC 对象不可执行）；⑦ §0.3 小节去编号并修正引用（原编号序列 0.1/0.2 断档）；⑧ §4.2 静默表描述修正："A–D 整体上移一级、封顶 P1"对 A 行不成立（两表 A 行相同，最高 P0）；⑨ §6 反例库补 OTA 块 4 行（先行登记，实施后绑 §2.4 行 5/6/7 的 OPS 证据）。 |
| v1.0 | 2026-09-13 | 首版。改编自 `docs/review_guide_reference/` 下后端评审指南 v2（裁决 v3.1）+ MACAO 评审方法论 v1.1：保留方法论内核（证据先行、证据>票数、后果×可达性定级、两轴标注、声明矩阵、反例库、门禁、自审、修订治理），领域内容整体重建为本项目现实——无 CI/无自动化测试（证据类型改 BUILD/SIM/PROBE/HW/OPS + ⏸待用户实测纪律）、嵌入式后果类（变砖/堆损坏/busy 卡死/池耗尽/8KB UI 栈/凭据截断/EPD 阻塞实时路径）、承重不变量清单（异步 IPC 契约 + UI 内存纪律 + LVGL/EPD + 秘密链 + 键盘/外设 + 硬件变体 + 实时路径）、反例库（异步/内存/掉电/网络/外设/实时/边界值）、申请/结果模板对齐 `docs/reviews/README.md` 实际惯例（commit 范围含两端、A/B/C 审批、≥10 分段、连续两轮全 ⏸ 标 High）。 |
