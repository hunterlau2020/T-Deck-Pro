# 评审结果：OTA 固件远程升级设计稿（qwen）

- **评审日期**：2026-09-13
- **评审对象**：[`docs/ota-update-design.md`](../ota-update-design.md) **v1 设计稿 @ `36e88df`**（状态"待评审"，无实现代码）。本结果针对 **v1**；该稿后已修订为 **v2 @ `be6a52c`**（"four-review P1s folded in"）并由 codex/grok/claude 复审——本文件是 v1 轮的 qwen 评审，按 §7.2 commit 锚规则保留为历史记录，**不与 v2 轮文件互相覆盖**（v2 如需 qwen 复审另起 `ota-update-design-review-result-be6a52c-qwen.md`）。
- **评审类型**：设计稿对齐评审（review_guide §1.1 / §2.1 **L1 DOC-ALIGNED** track）
- **评审依据**：`docs/review_guide.md` v1.0；上位准则 `CLAUDE.md`、`docs/async_ipc_contract.md`、`docs/issue_list.md`
- **评审范围**：§1 现状盘点（逐条核验"已核实的事实"）、§2 清单协议、§3 模块设计、§4 UI、§5 三层失败/回滚模型、§6 发布流程、§8 六项请评审重点。
- **评审结论**：**C 部分接受**。§1 事实根基罕见地扎实（每条"已核实"都对得上框架/repo），3 层失败模型成立；但 **1×P1 安全交互 + 3×P2 + 2×P3 须在开工前关闭**——设计稿本身可接受为方向，**实现前必须修订**下列条目（尤其 P1）。

---

## §1"已核实的事实"逐项核验（review_guide §4.4 声明矩阵）

参照系（§3.5）= **pda2 实际使用的框架预编译产物**，非 repo 内无关 sdkconfig/csv：

| # | 设计声明 | 核验方式 | 状态 |
|---|---|---|---|
| ① | 双槽分区 app0 6.2MB@0x10000 / app1 6.2MB@0x650000 / otadata 8KB@0xE000 | 读框架 `…/framework-arduinoespressif32/tools/partitions/default_16MB.csv`（board JSON `"partitions":"default_16MB.csv"` 选定）：`otadata,data,ota,0xe000,0x2000` / `app0,app,ota_0,0x10000,0x640000` / `app1,app,ota_1,0x650000,0x640000` | ✅ VERIFIED（"6.2MB"实为 0x640000=6.25MiB/6.55MB，无害取整） |
| ② | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`（sdkconfig 第 60 行，已核实） | 读框架 `…/tools/sdk/esp32s3/sdkconfig:60` = `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` | ✅ VERIFIED（**参照系正确**：repo 内 line-60 sdkconfig 是 `lib/XPowersLib/.../sdkconfig`"is not set" + `examples/touch_hyn_core/esp_idf/sdkconfig:444`"not set"，二者均**非** pda2 的 bootloader；设计引的是框架预编译 sdkconfig，对） |
| ③ | `http_get()` @ http_utils.h:79 | `rg`：`http_utils.h:79 http_response_t http_get(const char*, uint32_t=10000)` | ✅ VERIFIED |
| ④ | 电量 `ui_battery_27220_get_percent()` @ port.cpp:549 | `rg`：`ui_deckpro_port.cpp:549 uint16_t ui_battery_27220_get_percent(void){return bq27220.getStateOfCharge();}` | ✅ VERIFIED（设计写"port.cpp:549"= 该文件，行号精确） |
| ⑤ | `UI_T_DECK_PRO_VERSION` @ utilities.h:12 | `rg`：`examples/pda2/utilities.h:12 #define UI_T_DECK_PRO_VERSION "v2.4-260320"` | ✅ VERIFIED（现值 v2.4-260320；清单示例 v2.5-260912 是"未来可更新版本"示意，合理） |
| ⑥ | `http_apply_tls(client)` 可复用 | `rg`：`http_utils.h:62 void http_apply_tls(WiFiClientSecure&)`（+ `http_ensure_time` @ :71） | ✅ VERIFIED |
| ⑦ | HTTPUpdate 存在于 arduino-esp32 2.0.14 | 框架 `libraries/HTTPUpdate/` 目录存在 | ✅ VERIFIED |
| ⑧ | 现有 OTA 代码：无（全树 grep 干净） | `rg "HTTPUpdate\|esp_ota\|ota_update\|Update.h" examples/` = 0 命中 | ✅ VERIFIED |
| ⑨ | 可复用 UI/任务模式（chat_waitbox / ai_voice_task / lv_async_call） | `rg`：命中 `ui_voice_ai.cpp`/`ui_ai_chat.cpp`/`ui_penpal.cpp` | ✅ VERIFIED |
| ⑩ | SCREEN2_1（About System）模式可照搬 | `rg`：`ui_deckpro.h:54 SCREEN2_1_ID`、`:103 UI_SETTING_TYPE_SUB` | ✅ VERIFIED（新增 SCREEN2_2_ID follow 该模式可行） |
| ⑪ | 固件体积 2.1MB（槽 6.2MB 余量充足） | `.pio/build/pda2/firmware.bin` 当前未构建，无法独立测 | ◐ CLAIM_ONLY（**不影响结论**：2.1MB 在 6.25MB 槽内余量结论恒成立） |

**小结**：§1 表 11 条，10 条 `VERIFIED`、1 条 `CLAIM_ONLY`（且无关紧要）。设计的事实功课做得扎实，参照系（框架预编译产物 vs repo 内同名干扰文件）引对——这点尤其值得肯定，因为 repo 内确有两个"not set"的 sdkconfig 容易误引。

---

## Findings

### P1：OTA 继承全局 TLS Trust 开关 × 无签名 = MITM 推送任意固件（持久 RCE / 变砖）

- **位置**：设计 §3（`ota_run` 用 `http_apply_tls(client)`）+ §8.4（"清单不做签名校验"）；交互对象 `examples/pda2/http_utils.cpp:25/27/40`（`s_tls_mode` 全局 + `setInsecure()`）、`docs/issue_list.md` §7.4。
- **证据**：
  1. `http_utils.cpp:25 static http_tls_mode_t s_tls_mode = HTTP_TLS_CA_VERIFY;`，`:40 client.setInsecure();`（insecure 模式时）——`http_apply_tls` 读**全局** `s_tls_mode`。
  2. issue_list **§7.4** 确认 AI-Config 屏的 "Trust self-signed" 开关（NVS `ai/tls_insecure`，`factory.ino` setup() 末尾 `openai_tls_apply()` 开机生效）作用于 **ALL HTTPS**（状态行文案 `TLS: ALL HTTPS trust self-signed`，串口 `(applies to ALL HTTPS)`）。
  3. 设计 §3 明确 OTA HTTPS 走 `http_apply_tls(client)` → **OTA 自动继承该全局开关**；§8.4 明确"本版不实现签名"。
  - **反例序列**：用户为连自签本地 LLM 端点把 Trust 开关打到 insecure（合法常见操作）→ 之后手动触发 OTA → OTA 的 HTTPS 连接 `setInsecure()`、不校验证书 → 同网在途攻击者（恶意 AP / ARP 欺骗）MITM 清单与 firmware.bin → 推送任意固件 → 设备持久 RCE 或变砖。§3 的 `http://` 明文 LAN 路径前提更少（连 insecure 开关都不需要），暴露面更大。
- **影响**：后果 **A**（不可恢复 + 远程代码执行 + 变砖）；可达性 **3**（需"Trust=insecure 或 http:// 明文" + "在途攻击者" + "用户手动触发 OTA"多前提，按 §0.7b 多前提折算取最难档）；**静默**（无错误信号，固件被静默替换）。坐标 **A/3/静默 → P1**。**P1 不可作为已接受风险**（review_guide §4.2）。
- **两轴**：`VERIFIED`（CODE：http_utils + issue_list §7.4 + 设计 §3/§8.4 三处对齐）/ `VIOLATES`（CLAUDE.md 第 2 条秘密链精神的延伸——传输信任被静默降级；且与 §7.4 既有"影响面"问题叠加放大）。
- **最小修复**（开工前必须定方案，三选一或组合）：
  - **(a) OTA 不继承 AI trust 开关**：`ota_run` 用一个**独立、硬编码 CA_VERIFY** 的 `WiFiClientSecure`（不调 `http_apply_tls`，或调一个 OTA 专用、忽略 `s_tls_mode` 的变体），把 OTA 信任锚与"为 LLM 临时开的自签信任"解耦；**+ (b) 拒绝降级**：`tls_mode==INSECURE` 或 `OTA_URL` 为 `http://` 时，OTA 直接拒绝并提示（明文仅限带编译期开关的 LAN 测试构建，默认禁）；
  - **(c) 签名清单+固件**（设计的"二期"ed25519）——是 (a)(b) 之外的纵深防御，但不应是 v1 唯一防线。
  - **推荐 v1 = (a)+(b)**（廉价、当批可落地），(c) 留二期。须补回归（SIM/纸面）：Trust=insecure 时 OTA 仍 CA_VERIFY / 拒绝 http://。

### P2：自证点在 setup() 末尾，覆盖不到 UI/主循环回归（layer 2 对此类失效）

- **位置**：设计 §5（自证点 = `factory.ino` setup() 末尾 `esp_ota_mark_app_valid_cancel_rollback()`）+ §8.2。
- **证据**：`mark_valid` 在"外设初始化全过后"触发，**早于主循环渲染任何屏**。一个"能完成 setup() 但首次交互即冻/崩"的固件——正是本轮同时评审的 `721e04a` 池耗尽那一类（PenPal 点击死机：boot 正常、点链接才崩）——会**自证通过、不回滚**，layer 2 对 UI/主循环回归形同虚设，只能靠 layer 3 USB 兜底。设计 §8.2 的论证（"不绑 WiFi 成功，避免网络差导致好固件被回滚的死循环"）对**网络维度**正确，但把自证点放在 setup() 末尾，连带把 **UI 维度**也排除在健康判据外了。
- **影响**：后果 **B**（回滚契约/状态机违约——"新固件起不来应回滚"的承诺对 UI 崩溃类不兑现）；可达性 **2**（需新固件恰有 boot-OK-but-UI-crash 回归）；半静默（用户看到冻屏，但不知是"升级失败本可回滚却没回滚"）。坐标 **B/2 → P2**。
- **两轴**：`VERIFIED`（SIM：按 setup()→loop() 时序推演，池耗尽类崩溃发生在 loop 首屏，晚于 mark_valid）/ `UNDECIDED`（自证点位置是设计决策，需属主裁定健康判据边界）。
- **最小修复**：把 `mark_valid` 推迟到**首屏成功绘制后**（或主循环跑过 N 次干净迭代后），仍**独立于网络**（保住 §8.2 避免网络死循环的正确意图）；或在 §5 显式登记残余风险"boot-OK-but-UI-crash 类回归不自证失败、靠 layer 3 USB 兜底"并由属主签字接受。推荐前者（一行位置移动，覆盖面显著扩大）。

### P2：下载/写 flash 期无 cancel/Close 语义

- **位置**：设计 §4（UI 流程步骤 3 "Updating... N%" 等待框，无取消路径）。
- **证据**：§4 只画了 Check→Update→下载中→完成→Reboot 的 happy path，未定义下载/写 flash 中途用户按 Back/Close 等待框、或离页会发生什么。本项目有等待框 Close 语义先例（PenPal 首个可取消等待框：SEND=后台续、读/算型=取消，见 CHANGELOG），OTA 须自定义其 Close 语义。中断 `Update.h` 写 flash 是危险态。
- **影响**：后果 **C**（UI 卡死 / 半写状态需 layer-1 重启回退恢复，可恢复）；可达性 **2**（需用户在下载期主动 Close/离页）。坐标 **C/2 → P2**。
- **两轴**：`VERIFIED`（CODE：设计 §4 流程图缺该分支）/ `NO_CONTRACT`（OTA 等待框 Close 语义未定义）。
- **最小修复**：明确"写 flash 期 Close/Back **被禁止**（等待框不可取消，按键忽略 + 提示 'updating, do not power off'）"，或"Close = 取消下载 → otadata 不翻转 → 提示重启回旧版（layer 1）"。二选一写进 §4，并对应到 async 契约的取消语义（见下条）。

### P2：OTA 任务未声明遵守 async_ipc_contract（NO_CONTRACT）

- **位置**：设计 §3（`ota_run` + onProgress→FreeRTOS 队列→UI，"ai_voice_task 同款跨线程模式"）。
- **证据**：OTA 是**新的 worker task + 队列 + UI** 异步任务，但设计只说"同款跨线程模式规避 LVGL 线程约束"，未指明契约要素：结果所有权（worker `new`→队列→UI `delete`）、busy 代次（`*_busy_gen`）、队列先于 busy 创建（检 `xQueueCreate` 返回值）、取消 = gen+1、绝对 deadline 超时。review_guide §4.3 / §5.1：新型异步任务须同轮**加进契约表**或**显式登记例外**（注意 weather fetch 已是 issue_list §11 未登记例外，别再加第二个无主异步任务）。
- **影响**：后果 **B**（契约破坏——迟到结果/取消/队列失败路径未定义，重演 PenPal busy 泄漏类 bug 的风险）；可达性 **2**（离页后进度回调到达 / 取消时序）。坐标 **B/2 → P2**。
- **两轴**：`VERIFIED`（CODE：设计文本未含契约要素）/ `NO_CONTRACT`（OTA 任务不在 async_ipc_contract.md §1 表内）。
- **最小修复**：设计稿补一节"OTA 任务的契约符合性"——逐条说明所有权/代次/队列先建/取消/超时如何落地（即便 OTA 是单飞、用户全程等待，进度回调的跨线程交付与离页/取消路径仍须按契约）；实现后同轮把 OTA 加进 `async_ipc_contract.md` §1 表或登记例外。

### P3：完整性——要求服务器发 `x-MD5` 响应头

- **位置**：设计 §2 清单协议 + §5 layer 1 + §8.4。
- **证据**：§5 layer 1 称"坏包/校验失败 → otadata 不翻转"，但 §8.4 推迟 SHA256/签名。`Update.h`/`HTTPUpdate` **原生**校验响应的 `x-MD5` 头（若服务器提供），这是近乎零成本的完整性校验，能补上"截断/损坏下载"缺口（否则 layer 1 仅靠 Update.h 的 app-magic/size 头校验）。
- **影响**：后果 **B**（完整性——损坏但完整的下载可能通过 app-magic 校验）；可达性 **3**（需传输损坏且恰好 magic 合法，HTTPS 下罕见）。坐标 **B/3 → P3**。
- **两轴**：`VERIFIED`（框架行为已知）/ `CONFORMS`（建议性增强）。
- **最小修复**：§6 发布流程加一步"静态文件服务为 firmware.bin 配置 `x-MD5` 响应头"，或清单加 `md5` 字段由 `ota_run` 校验。

### P3：发布流程未保证 manifest.version 与 bin 内烘焙版本同步

- **位置**：设计 §6 发布流程。
- **证据**：§2 版本比对是"清单 `version` 与 `UI_T_DECK_PRO_VERSION` 字符串不同即可更新"。§6 流程"上传 firmware.bin + 更新清单 version/url/notes"未强制校验二者一致。若构建了 v2.5 的 bin 却忘改清单（仍 v2.4），设备看到 manifest==current → 不提供更新；或清单 version 与 bin 实际版本错位 → 刷入后 About 显示与预期不符。
- **影响**：后果 **E**（运维/发布流程易错）；可达性 **2**（人工发布疏忽）。坐标 **E/2 → 观察/P3**。
- **两轴**：`VERIFIED`（DOC：§6 流程缺该步）/ `CONFORMS`。
- **最小修复**：§6 加一步"确认 `manifest.version` == 新 firmware 的 `UI_T_DECK_PRO_VERSION`（构建时从 utilities.h 读出核对）"。

### 决策项（review_guide §10 登记）：无固件/清单签名

- **位置**：设计 §8.4。
- **证据/影响**：§8.4 自陈"依赖 HTTPS 传输安全 + SHA256 可选（本版不实现签名）；防服务器被攻破的供应链攻击需二期 ed25519"。对 **LAN 个人设备**这是可风险接受的边界，但它正是上面 **P1 的缓解前提**——一旦 P1（OTA 继承 insecure trust）未修，无签名 = 远程代码执行。若 OTA 面向公网或设备出货，签名从"二期"升为**必需**。
- **处置**：登记 `issue_list.md` 决策项：`scope=LAN 个人设备`、`expiry=任何公网 OTA 服务 / 公开发布前`、`risk_acceptor=用户`、`生产化前需补=ed25519 签名清单+固件`。**与 P1 联动**：P1 修复（OTA 不继承 insecure + 拒明文）是把"无签名"维持在可接受档的前提。

---

## 已通过项

- **3 层失败/回滚模型成立**：layer 1（下载中断/坏包→otadata 不翻转→重启回旧版，零损伤）+ layer 2（bootloader 自动回滚，开关已核实启用）+ layer 3（USB 分块烧录 + backups/ 整片恢复）覆盖了断电/坏包/变砖三类评审关注点，且 layer 3 复用了已验证的 build 文档坑 6 流程。
- **选型 HTTPUpdate 而非 esp_https_ota 正确**（答 §8.5）：esp_https_ota 是 ESP-IDF 组件、Arduino 无封装、C API 与现有 HTTPClient/`http_apply_tls` 模式不一致；HTTPUpdate（内部 Update.h 流式写）行为等价且与代码库一致。认可。
- **版本比对"不同即可更新"、不做 semver 大小判断**：对非 semver 的版本串（v2.4-260320）是稳妥选择，避免误判；手动触发契合本项目手动回归文化。
- **`rebootOnUpdate(false)` + 用户确认重启**：把重启决定权交用户，避免下载完突然重启丢状态，正确。
- **电量前置检查**（<30% 拒绝 + 建议插 USB）：复用 `ui_battery_27220_get_percent()`（已核实 @ port.cpp:549），防写 flash 中途掉电，正确。
- **全程不碰 NVS/SPIFFS/凭据**：OTA 只写 app1 槽 + otadata，凭据分区不动，与秘密链（CLAUDE.md 第 2 条）正交，安全边界清晰。

---

## 答 §8 六项请评审重点

1. **不做自动强更（手动触发）/ 是否需后台静默检查+角标**：手动触发对 v1 正确（契合手动回归文化、避免后台流量/电量 surprises）。后台静默检查+角标提示留二期合理——但二期若做，须同样受 P1 约束（检查请求也不得继承 insecure trust）。
2. **自证点选 setup() 末尾而非 WiFi 连上后**：见 **P2**——"不绑 WiFi"的意图正确（避免网络差回滚死循环），但 setup() 末尾**太早**，覆盖不到 UI/主循环回归（721e04a 池耗尽类）。建议移到首屏绘制后 / N 次干净 loop 后，仍独立于网络。
3. **HTTP 明文仅限测试 / 是否强制 HTTPS**：见 **P1**——是，必须强制：明文 `http://` 仅限带**编译期开关**（默认禁）的 LAN 测试构建；生产 HTTPS 且 OTA 不继承 AI trust 开关、insecure 时拒绝。
4. **清单不做签名校验 / 风险接受度**：见 **P1 + 决策项**——LAN 个人设备可接受"无签名"**当且仅当** P1 已修（OTA 恒 CA_VERIFY + 拒明文/拒 insecure）；否则无签名 + 可降级传输 = 远程代码执行，不可接受。公网/出货前签名升为必需。
5. **未用 ESP-IDF esp_https_ota 组件**：认可（见已通过项）。HTTPUpdate 是对的选型。
6. **回滚开关已启用但 pending→rollback 未实测**：标注正确。§7 step-2 的自毁固件反向用例是验证 pending→rollback 的对的方法，**保留为 G-发布 门禁项**（review_guide §2.2）——发布前必须真机刷一个"setup() 末尾不调 mark_valid 的自毁固件"验证自动回滚真发生，否则 layer 2 只是纸面承诺。

---

## 验证说明（评审方环境）

- 评审方在开发机（Windows，repo 工作副本 + `~/.platformio` 框架缓存）独立核验：
  - 读框架 `default_16MB.csv` 确认双槽几何（① 逐字段比对）；
  - 读框架 `…/esp32s3/sdkconfig:60` 确认 rollback=y（②），并排除 repo 内两个"not set"的同名干扰文件（XPowersLib 例程 / touch_hyn_core ESP-IDF 子项目）；
  - `rg` 确认 http_get/电量/版本/http_apply_tls/HTTPUpdate/SCREEN2_1/无现存 OTA 码（③-⑩）；
  - `rg` 确认 `s_tls_mode` 全局 + `setInsecure()`（P1 证据）+ issue_list §7.4 Trust 开关 ALL HTTPS 文案。
- **未独立复跑** `pio run -e pda2`：固件 2.1MB（⑪）标 `CLAIM_ONLY`（pda2 当前未构建），但与"槽 6.25MB 余量充足"结论无关，不影响定级。
- **无设备**：layer 2 pending→rollback、OTA 真机升级演练均无设备复现，按 §3.4 标 `UNKNOWN`（设计稿阶段本就未实现，§7 验证计划已列入，符合 L1 track 不要求 HW 证据）。
- 未对工作区作任何代码修改（设计稿评审，无实现可改）。

## 对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：N/A→已尽力。本对象是**设计稿（无代码 diff）**；评审方未止于设计自述，而是把 §1 全部 11 条"已核实事实"拉到框架/repo 实物逐条核验（含排除同名干扰文件），并主动追到设计**未提及**的交互面（http_utils 全局 trust 模式 × issue_list §7.4）——P1 即由此挖出，非申请靶向清单内。
2. **快照是否独立复跑**：部分。框架 csv/sdkconfig、repo 源码 `file:line` 均独立读取比对（已记命令与命中行）；`pio` 构建产物未重跑（⑪ 标 CLAIM_ONLY）；HW/rollback 路径无设备，标 UNKNOWN。
3. **申请"最没把握"各处是否构造反例**：是。设计 §8 自陈的 6 个疑点逐条回应；并对最承重的两条声明构造反例——① "回滚开箱即用"：反例 = 自毁固件（setup 末尾不 mark_valid），确认 §7 step-2 已含该反向用例（反例被设计自己覆盖，成立）；② "HTTPS 传输安全足够"：反例 = Trust 开关打到 insecure + 在途攻击者，**击穿**该假设（P1，设计未覆盖）——这是本轮最重要的反例命中。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受**

> **合入/开工前置条件**（review_guide §2.2 G-评审→G-合入）：
> - **P1（阻断，不可风险接受）**：OTA 不得继承全局 TLS Trust 开关——定方案 (a) 独立 CA_VERIFY client + (b) 拒 insecure/拒 http:// 明文（编译期开关默认禁）。
> - **P2×3（开工前补设计）**：自证点移到首屏绘制后（仍独立网络）；定义下载期 Close/cancel 语义；补 OTA 任务的 async_ipc_contract 符合性节。
> - **P3×2（实现期带）**：要求服务器 x-MD5；发布流程加 manifest.version 同步校验。
> - **决策项**：无签名登记 issue_list（scope=LAN 个人 / expiry=公网或出货前 / 与 P1 联动）。
> - **G-发布门禁**：§7 step-2 自毁固件验证 pending→rollback 真发生（layer 2 不可只停在纸面）。
>
> 上述关闭后，设计稿可达 **L1 DOC-ALIGNED** 并据以开工（实现后另走 L2/L3 代码评审）。
