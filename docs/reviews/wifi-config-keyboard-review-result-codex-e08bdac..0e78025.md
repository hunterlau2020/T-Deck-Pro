# 第 26 轮整改评审结果（Codex）— Weather 2.5 迁移 + TLS R46 + Secrets 配置链

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..0e78025.md](wifi-config-keyboard-review-request-e08bdac..0e78025.md)
- **关联代码范围**：`e08bdac..0e78025`
- **本次重点新增范围**（前 9 个代码 commit 已由第 22–25 轮 Codex 结果全量接受，见
  [e08bdac..0b43685](wifi-config-keyboard-review-result-codex-e08bdac..0b43685.md)、
  [e08bdac..f3e1698](wifi-config-keyboard-review-result-codex-e08bdac..f3e1698.md)、
  [e08bdac..1f46630](wifi-config-keyboard-review-result-codex-e08bdac..1f46630.md)）：
  - `b7c2a87` — weather 迁移免费 2.5 端点（用户反馈）
  - `ece4079` — CA bundle 追加 Sectigo Root R46 + Sym 层 +/- 翻页 + 无 GPS 回退坐标（用户反馈）
  - `0e78025` — secrets 配置链 NVS → /env.cfg → gitignored config_keys.h → 空默认（用户要求）
  - doc-only：`61b22c4`、`4e2a3e3`、`0645083`、`4af988d`（reviews 注册 + CLAUDE.md/CHANGELOG 同步）
- **分段评审说明**：申请书名义列 12 commit（≥10 应分段，评审要求 §1.11），实际已按
  增量轮次分段完成（e08bdac..0b43685 / ..f3e1698 / ..1f46630 三轮），本轮仅新增
  3 个代码 commit + 4 个 doc-only commit，符合纪律。
- **评审结论**：**A 全量接受**（3 个新代码 commit + 4 个 doc-only commit 接受；
  附带 1 项 Medium 卫生项 M1 与 3 项 Low 跟踪项，均不阻断）

---

## 1. Findings

### 1.1 沿用（前三轮已闭合，不再展开）

- **e08bdac** Usage 弹窗 6 行 breakdown；**8770a41 + f3e1698** 扫描覆盖层帧序号绑定；
  **f4449c3** 双 Tab 布局；**0b43685** chat_exit hide waitbox；**cc94452** Tab 移入顶栏；
  **06a2c13** Chat tab `\v` ignore；**7ecebcd** 突发按键合并渲染；**1f46630** redraw-on-release + Enter 换行

### 1.2 b7c2a87 Weather 迁移免费 2.5 端点 — ✅ 通过

- **严重性**：✅ 通过
- **位置**：`examples/pda2/ui_weather.cpp`（parse_current_weather / parse_forecast / weather_fetch_task / update_ui）
- **机制**：
  - One Call 3.0（付费、正被下线，免费 key 401）→ `/data/2.5/weather` + `/data/2.5/forecast` + `/geo/1.0/reverse`（不变）
  - `parse_forecast`：3h 槽位跳过 `ts < now`；前 12 槽（=36h）填 hourly 表；
    按本地 `tm_yday` 聚合成 daily（min/max 温度、平均湿度、max pop、首槽天气）；
    `daily_count >= MAX_DAILY` 先 flush 再 break，越界保护正确
  - `data_valid` 仅由 current 成功置位；`last_fetch_time`/`save_cache()`/城市名都挂在 `data_valid` 下
  - UV：免费端点无 uvi → `cur.uvi = -1` → UI 显示 `UV:--`（`>= 0` 分支保留兼容）
  - 缓存读写对 `hcnt`/`dcnt` 做 `MAX_HOURLY`/`MAX_DAILY` 钳制；渲染循环以 count 为上界
- **缓冲安全核查**：`hourly_entry_t.desc[16]`/`daily_entry_t.desc[16]` 配 `strncpy(..., 15)`，
  静态零初始化保证第 16 字节恒为 0；`day_desc[24]` 配 `strncpy(..., 23)`；`day_str[6]` 配
  snprintf `%s`（≤3 字符）——与既有代码模式一致，无溢出
- **观察**（均 Low/Info，不阻断）：
  - current 失败但 forecast 成功时 `data_valid` 不置位 → 两表已解析但不显示、不存缓存。
    语义合理（Current 页为锚），已加串口日志可诊断
  - "5-Day" 页在傍晚时段可跨 6 个本地日历日（今日残段 + 5 天），`daily_count` 最大 6 < MAX_DAILY 8，纯显示层标题措辞问题
  - `tm_yday` 判日在 ≤6 天窗口内无碰撞（含跨年 364/365→0 场景值互异），安全
- **结论**：端点迁移正确、解析防御充分、失败路径有日志。根治"免费 key 401"用户反馈。

### 1.3 ece4079 CA bundle 追加 Sectigo R46 + 翻页键 + 回退坐标 — ✅ 通过

- **严重性**：✅ 通过
- **位置**：`examples/pda2/http_utils.cpp:145+`（CA_BUNDLE 第 6 张证书）、`examples/pda2/ui_weather.cpp`（keyboard_poll / fetch_task else 分支）
- **证书独立核验**（评审方复核，非仅采信申请人声明）：
  - 从 `CA_BUNDLE` 提取最后一张 PEM，certutil 解析确认：
    Subject = `Sectigo Public Server Authentication Root R46`（O=Sectigo Limited, C=GB），
    Issuer = `USERTrust RSA Certification Authority`，有效期 2021-03-22 ～ 2038-01-18，
    BasicConstraints CA:true、KeyUsage certSign、sha384RSA——即 OWM 活链顶端的交叉签名证书，与 commit message 描述一致
  - 本机无 openssl（`ca_bundle_check.py` 无法直接跑），改用 python `cryptography` 复刻其逻辑：
    **6/6 证书全部可解析、当前时间全部有效**；`[1] Root YR ← ISRG Root X1` 束内签名验证 OK
  - 信任锚机制正确：mbedtls 以束内证书为锚，服务器链 `*.openweathermap.org ← Sectigo OV R36 ← 交叉签名 R46`
    顶端与束内第 6 张完全一致即可收敛
- **运维观察**（Info，不阻断）：锚点是**交叉签名版** R46（非自签名 R46 根）。若 OWM 未来改链到
  自签名 R46 且不再下发交叉证书，TLS 将再次失败——与既有 5 根锚点同风险级别，
  处置方式不变（活链提取 + ca_bundle_check 复核）。建议在 SECURITY.md/TODO 里保留该操作的记录入口
- **翻页键**：Enter/Space/+ 前进回绕、- 后退回绕，分支互斥正确，与其它屏 +/- 约定一致
- **回退坐标**（本 commit 版本）：`WEATHER_DEFAULT_COORDS` sscanf + `clat!=0&&clon!=0` 守卫；
  已被 0e78025 的完整配置链取代，演进路径干净

### 1.4 0e78025 Secrets 配置链 — ✅ 通过（附 M1/L1/L2 跟踪项）

- **严重性**：✅ 通过（代码本身）；**M1** 为卫生跟踪项
- **位置**：`examples/pda2/env_secrets.{h,cpp}`（新）、`openai_api.{h,cpp}`、`ui_weather.cpp`、`platformio.ini`、`SECURITY.md`、`env.cfg.example`
- **关键核验**（评审方独立执行）：
  - `git grep 'sk-or-v1'` @ HEAD：源码仅剩占位符（`ui_ai_cfg.cpp` placeholder 文案、
    `env.cfg.example`/`env_secrets.cpp` 注释示例、测试脚本 `sk-or-v1-default`）——**跟踪源码无真实 Key** ✓
  - `openai_api.h::AI_KEY_DEFAULT` 现为 `""`；`-DAI_KEY_DEFAULT_COMPILED` 已从 `platformio.ini` 移除，
    `#warning` 宏一并删除；HEAD 无残留引用（仅 TODO/历史评审文档提及旧宏名）
  - `.gitignore:17 config_keys.h` 生效（`git check-ignore` 确认）；`config_keys.h.example` 在跟踪内；
    工作区真实 `config_keys.h` 未跟踪，含 `AI_KEY_DEFAULT_DEV`/`OWM_API_KEY`/`WEATHER_DEFAULT_COORDS`（内容已脱敏核验，不引用）
- **机制核查**：
  - `env_secrets.cpp`：懒加载 + 缓存；`readStringUntil('\n')` + `trim()` 兼容 CRLF；首个 `=` 分割
    （值可含 `=`，`WEATHER_COORDS=lat=..&lon=..` 场景正确）；`#` 注释与空行跳过；
    上限 8 条 × key[24]/val[96]，strncpy 均 n-1 + 显式终止——无溢出
  - `openai_load_config`：else 分支 `env_get("AI_KEY") → AI_KEY_DEFAULT_DEV → ""`，
    `k = env_key` 先拷入 String 再使用，**无栈生命周期问题**；与注释声明的链序一致
  - `weather_owm_key`/`weather_coords`：NVS `weather.owm_key`/`coords` → env → config_keys.h →
    SF 兜底（注释标明 last resort）；`start_fetch` 无 key 时 UI 提示
    `No API key.\nSet OWM_KEY in /env.cfg or config_keys.h`——可诊断性好
  - `env_get` 双任务首调竞态：后到者见 `s_env_loaded=true` 直接返回 false → 落到更低优先级层，
    结果等价或降级，无安全/正确性影响（Info）
  - SPIFFS 兼容：与 `/chat.log` 共用挂载，重复 `begin(false)` 幂等
- **M1（Medium，卫生，不阻断）**：SECURITY.md 声称"跟踪源码无真实 Key"，但 HEAD 的 3 份历史评审
  文档仍含**完整明文旧 Key**（引用当年 `#define` 原行）：
  - `docs/reviews/wifi-config-keyboard-review-result-01f8eac..8b96656.md:22`
  - `docs/reviews/wifi-config-keyboard-review-result-23942f6..9b104d1.md:16`
  - `docs/reviews/wifi-config-keyboard-review-result-eecebda..ceade9c.md:27`
  处置：Key 已按泄露处理（轮换为发布前置条件），故不阻断本轮；但推公网前除 filter-repo 清历史外，
  **这 3 个文件的 Key 字符串也须一并掩码**（`sk-or-v1-****`），否则 filter-repo 后仍从 HEAD 泄露。
  评审结果"永不覆盖"规则在此让位于密钥卫生——建议下轮以一次性 redaction commit 处理并在 SECURITY.md 清单中记名
- **L1（Low）**：`config_keys.h.example` 未含 `AI_KEY_DEFAULT_DEV` 条目（仅 WiFi/Gemini/OWM/Calendarific/coords），
  从 example 复制的新用户无法发现 AI key 编译期钩子；建议补注释行 + 一句配置链说明
- **L2（Low）**：`TODO.md:46-47`（删 Key 字符串 / 移除 `-DAI_KEY_DEFAULT_COMPILED`）已由本 commit 完成，
  清单未打勾；建议标 [x] 并注 `0e78025`，保留轮换 + filter-repo 两项

### 1.5 申请书文档质量（流程观察，Low）

- 头部"本轮 **4** 个，全部未评审"为陈旧表述（实列 12 个，其中 9 个已被前三轮 Codex 结果接受）
- §2 验证表"CA bundle ✅ 5 根证书 PASS（沿用）"未随 `ece4079` 更新为 6 根（§1.11 正文已写 6/6，两处矛盾）
- §3 真机回归清单**未包含本批新增行为**：Weather 三页内容/翻页键/UV:--/无 GPS 深圳回退、
  空 NVS 下 AI Key 走 config_keys.h 的链路——建议申请人补 2 项待测
- 按 docs/reviews/README.md 规则 4（已通过范围出列），本轮申请文件名宜为
  `1f46630..0e78025` 且正文不再携带已接受 commit；本结果文档以"沿用 + 新增"结构兼容处理，不退回

---

## 2. 已通过项汇总

### 本轮新增（第 26 轮）
- **b7c2a87** Weather 迁移 2.5 免费端点（current + forecast 聚合 + UV:-- + 失败日志）
- **ece4079** CA bundle 第 6 张证书（交叉签名 R46，独立核验通过）+ Sym 层 +/- 翻页 + 配置坐标回退
- **0e78025** Secrets 配置链（真实 Key 移出跟踪源码；env_secrets 解析器；SECURITY.md 重写）
- **61b22c4 / 4e2a3e3 / 0645083 / 4af988d** doc-only：reviews 注册、CLAUDE.md 第 2 条改链、CHANGELOG 条目

### 沿用（第 22–25 轮已接受）
- e08bdac、8770a41、f4449c3、0b43685、cc94452、06a2c13、f3e1698、7ecebcd、1f46630（9 commit）
- 历史 844a907..156732c（28 commit）

---

## 3. 安全状态（更新）

- **进步**：真实 Key 已从 HEAD 跟踪**源码**移除（`AI_KEY_DEFAULT=""`、C1 宏删除）；
  配置链 NVS → /env.cfg → gitignored config_keys.h → 空默认落地；`config_keys.h` gitignore 生效
- **残留 1（已知已接受）**：git 历史仍含旧 Key——按 SECURITY.md 推公网前 filter-repo，用户决策延后，不阻断
- **残留 2（本轮新发现 M1）**：HEAD 3 份评审文档含完整旧 Key，须与 filter-repo 一并掩码
- **前置条件不变**：OpenRouter/OWM Key 推公网前轮换；本评审不视为阻塞项

## 4. 跟踪项（继承 + 本批新增）

| 跟踪项 | 来源 | 状态 |
|---|---|---|
| M1：3 份评审文档明文旧 Key 掩码 | 本轮 §1.4 | **新增，推公网前必做** |
| L1：config_keys.h.example 补 AI_KEY_DEFAULT_DEV | 本轮 §1.4 | 新增 |
| L2：TODO.md 清单 46/47 打勾 | 本轮 §1.4 | 新增 |
| 申请书补 Weather/Secrets 真机回归项 | 本轮 §1.5 | 新增（流程） |
| wifi_scan_overlay 跨 push 屏残留（exit4_1 hide） | 第 25 轮 | 待办 |
| suppress_flush 解除依赖后续 invalidate（理论 ≤30ms） | 第 25 轮 | 实际不可见，挂起 |
| SPIFFS /chat.log append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 | 主评审/TODO | 阶段 1 |

## 5. 验证说明

- `python scripts/test_nvs_atomic_save.py` → **11/11 PASS**（评审方复跑）
- CA bundle：本机无 openssl，`ca_bundle_check.py` 无法直接执行；评审方用 python `cryptography`
  复刻其全部逻辑（同源提取 + 逐张解析）→ **6/6 可解析、时间有效**，另对束内可配对的
  YR←X1 做了签名验证；追加的 R46 证书经 certutil 完整 dump 核对 Subject/Issuer/有效期/CA 标志
- 编译：评审环境无 `pio`，未独立复现；采信申请人 `pio run -e pda2` SUCCESS + COM5 烧录 Hash verified
  （与前三轮处理方式一致）
- 静态复核：`git show b7c2a87 ece4079 0e78025` 全 diff 逐段核查；HEAD `git grep` 密钥残留扫描；
  `.gitignore`/`git check-ignore`/`git ls-files` 核验跟踪状态
- 真机回归：申请 §3 五项 ⏸ 待用户；另建议补 §1.5 所列 Weather/Secrets 两项
- 本结果文档不含任何 Key 正文

## 6. 审批意见

- [x] **A. 全量接受** — 3 个新代码 commit + 4 个 doc-only commit 全部接受，关闭本轮评审循环
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：b7c2a87 根治免费 key 401（用户反馈闭合）；ece4079 的 TLS 修复经评审方独立证书核验
（锚点选择正确、有效期充分）；0e78025 兑现"真实 Key 移出跟踪源码"的用户要求，配置链实现稳健、
文档（SECURITY.md/CLAUDE.md/CHANGELOG/env.cfg.example）同步完整。M1/L1/L2 均不阻断合入。

**遗留项**：
- M1（3 份评审文档掩码）与 L1/L2 建议下一轮顺手提交；M1 列入推公网前必做清单
- Key 轮换 + filter-repo 仍按 SECURITY.md 作为发布前置条件跟踪

---

**评审人**：Codex（第三方静态复核视角；本轮独立执行：NVS 测试复跑、CA bundle python 复刻校验、
R46 证书 certutil dump 核验、HEAD 密钥残留 git grep 扫描、gitignore/跟踪状态核验；
已交叉核对 `git show b7c2a87 ece4079 0e78025` 及 4 个 doc-only commit 的实际 diff）