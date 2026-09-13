# 设计评审申请书：OTA 固件远程升级（v3 修订稿）

- **申请人**：Claude（pda2）
- **申请日期**：2026-09-13
- **关联分支**：`HD-V2-250915`
- **评审要求**（对全体评审方）：按 [`docs/review_guide.md`](review_guide.md)
  执行本轮评审——
  1. **结论与定级**：设计稿评审给 **L1 DOC-ALIGNED**（§2.1）+ A/B/C 审批；
     findings 按 §4.2 后果×可达性判定表定级并留坐标（如 `B/2/静默 → P1`）；
  2. **证据**：DOC/SPEC 为本档有效证据（§3.1/§3.3），每条发现给
     `file:line` + 反例/引用；无环境复现的如实标 `UNKNOWN`，作者声明默认
     `CLAIM_ONLY`；
  3. **自审**：报告末尾填 §9 清单的【D】/【ALL】条目（含文档对齐三条：
     字段/键名一致性、代码块可执行、✅≠证据）；
  4. **结果文件命名与存放**（§7.2，commit 锚强制）：本稿锚 = **`45d11cf`**，
     结果写入
     `docs/reviews/ota-update-design-review-result-45d11cf-<reviewer>.md`
     （`<reviewer>` = 评审模型名 codex/grok/claude/qwen…；**新建文件，绝不
     覆盖**既有结果——同一主题不同轮次各有锚）；报告含"对称三格"（核全
     区间 diff / 独立复跑 / 申请"最没把握"处反例）与"已核实到位项"。
- **状态**：**v3 修订稿，送三方复审**——v2（`be6a52c`）复审结论：
  Grok **A**（6 P2）/ Codex **C**（3 P1 + 1 P2）/ Claude **C**（2 P1 +
  1 P2）。本稿逐条处置全部 5 P1 + 9 P2（含 v2 轮新发现），处置对照见
  文末 §12。评审结果文件：
  [`*-be6a52c-codex.md`](reviews/ota-update-design-review-result-be6a52c-codex.md)、
  [`*-v2-grok.md`](reviews/ota-update-design-review-result-v2-grok.md)、
  [`*-v2-review-result-claude.md`](reviews/ota-update-design-v2-review-result-claude.md)
- **未变结论**（v2 轮已闭合，v3 仅保留）：TLS 隔离、ECDSA P-256 签名
  清单（Grok A + Claude 裁定接受，**不捆绑 Ed25519**）、自管流式下载、
  `verifyRollbackLater` 覆盖 + `drv.begin()` 拆雷、契约登记、手动触发。

---

## 0. v3 变化点（相对 v2）

1. **全局硬件锁 + 取消令牌**（Codex P1-A）：gen 不再是写 flash 的互斥——
   新增跨页面/跨代次的 `s_ota_hw_lock`（原子）与 `ota_cancel_token`；写
   槽期间第二个 OTA 任务拒绝启动；取消令牌在写入前/每块/提交前三处检查，
   取消 → `Update.abort()` 不切启动槽；UI 代次只管结果展示。
2. **自证窗口的看门狗保证**（Codex P1-B）：自证前把 loopTask 订阅进
   `esp_task_wdt`（8s 超时）——自证前任何死循环（含 `while(1) delay`）
   都会触发 WDT 复位 → 下次 boot 回滚；自证后退出订阅。v2"无复位保证
   时冻结不回滚"的漏洞由 WDT 关闭（详见 §5）。
3. **信任根进 tracked 源码**（Codex P1-C / Grok P2-2）：验签公钥 + key id
   + 编码格式入 `examples/pda2/ota_trust_anchor.h`（tracked），发布脚本
   校验指纹；**只有签名私钥 gitignored**。v2"公钥进 config_keys.h"作废
   （公钥不是秘密，是信任根，必须可审计、构建可复现）。
4. **低电关机 / Sleep 互斥**（Claude P1-A / Grok P2-4）：OTA 全程
   `low_voltage_timer_cb` 跳过 `ui_shutdown_on()`、waitbox 吞全部按键、
   禁止进入 Sleep、`WiFi.setSleep(false)`——结束（成功/失败/超时）一律
   恢复。
5. **签名对象扩展**（Codex P2）：`notes` 与单调递增 `seq` 纳入签名；
   默认拒绝回放/降级（NVS 记 last-seen seq；开发降级走编译期宏）。
6. **配置源恢复**（Grok P2-1）：`OTA_URL` 从 `/env.cfg` 读取（v2 重写时
   丢失）。
7. **文档完整性**（Claude P1-B）：恢复 v1 的发布流程 + USB 回退命令正文
   （§7），修复 v2 两处悬空引用（§12 行 4）。

---

## 1. 现状盘点（承 v2，无变化项不再复述）

分区表/回滚开关/固件体积/电量接口同 v2 §1；补充两条 v2 复审新实锤：

- **Arduino 自动自证**：`initArduino()`（`esp32-hal-misc.c:207-235`）在
  `setup()` 之前对 `PENDING_VERIFY` 自动 `mark_app_valid`，除非覆盖
  weak `verifyRollbackLater()`——v2 已纳入（Grok 实锤 + Codex 复核）。
- **ESP-IDF OTA 状态机**：未确认镜像在**下一次 boot** 才
  `PENDING_VERIFY → ABORTED` 回滚（Codex 复审引官方文档）——**不重启就
  不回滚**，故自证窗口必须由 WDT 提供复位保证（§5，Codex P1-B）。

## 2. 清单协议（v3：五字段 + seq，全部入签名）

### 2.1 清单格式

```json
{
  "version": "v2.5-260912",
  "url":    "https://host/path/firmware.bin",
  "size":   2100240,
  "sha256": "<64 hex chars>",
  "seq":    42,
  "notes":  "PenPal TTS; voice fixes",
  "sig":    "<base64, 64 bytes IEEE P1363 r||s>"
}
```

- **签名对象** = 规范化字节串（UTF-8，每字段一行以 `\n` 结尾，按上表顺序）：
  `version \n url \n size(十进制) \n sha256 \n seq(十进制) \n notes \n`。
  七字段**全部必填**；验签失败/缺字段 → 拒绝（Codex P2：`notes` 纳入
  签名——防"签名固件 + 篡改说明"诱导；Grok 原认为可接受，Codex 更严，
  从严采纳）。
- **签名编码钉死**（Grok P2-2）：IEEE P1363 大端 `r||s` 各 32 字节共
  64 字节，base64 无 PEM 头。设备端用 `mbedtls_mpi` 拆 r/s 调
  `mbedtls_ecdsa_verify`，**禁止** `mbedtls_ecdsa_read_signature`（它吃
  ASN.1 DER——对不上则所有升级验签失败）；发布脚本产出同格式。
- **算法**：ECDSA P-256/SHA-256（Grok A + Claude 复审双双裁定接受，
  不捆绑 Ed25519——mbedtls 2.28 无原生支持，sdkconfig 已核）。
- **seq 单调性**（Codex P2）：NVS `ota` 命名空间存 `last_seq`；验签通过
  后若 `seq <= last_seq` → 拒绝（防回放/降级）；开发降级须编译期宏
  `OTA_ALLOW_DOWNGRADE`（默认 0，env.cfg 打不开，同明文宏策略）。
  每次实际开始下载前把 `seq` 写入 NVS。

### 2.2 信任根（v3 改 tracked：Codex P1-C / Grok P2-2）

- **公钥不是秘密，是信任根**：`examples/pda2/ota_trust_anchor.h`
  （**tracked**）内含：公钥（raw 65B 压缩点或 64B Q_x||Q_y，十六进制
  数组）、`key_id`（8 hex）、编码格式注释。发布脚本校验该文件指纹。
  v2 的"公钥进 config_keys.h"作废——gitignored 公钥使发布不可复现、
  无法审计设备实际固化的信任根。
- 签名**私钥**：gitignored 本地文件（发布机），永不落仓库/设备。
- **换公钥 = USB 刷机**（写进 §7 发布前提；无远程换根通道——信任根的
  远程更换本身就是一个必须单独设计的攻击面，v3 明确不做）。

### 2.3 配置源（Grok P2-1，恢复 v1）

- `OTA_URL=<清单 URL>`：`/env.cfg`（`env_get`，val ≤160 够用）；
  `env.cfg.example` 同步模板并**删除过时的"Max 8 entries / 95 chars"
  注释**（现行为 12 条/159 字符）。
- 宏关闭时非 `https://` 的 OTA_URL → Check 阶段即拒绝；宏打开时允许
  `http://`（局域网联调，见 §3.1 scheme 分支）。
- 可选 `config_keys.h` 编译期默认 URL，优先级低于 env（与既有键序一致）。

## 3. 模块 `examples/pda2/ota_update.h/.cpp`

```c
const char *ota_current_version(void);
bool ota_fetch_manifest(ota_manifest_t *out, char *err, int err_len);  // 下载+验签
bool ota_precheck(char *err, int err_len);   // 电量/URL/槽容量检查
typedef void (*ota_progress_cb)(int percent, ota_phase_t phase);
bool ota_run(const ota_manifest_t *snap, ota_progress_cb cb,
             char *err, int err_len);        // 下载+写槽（受全局锁+令牌约束）
```

### 3.1 专用 TLS 与 scheme 分支（v2 闭合项 + Claude P2 补正）

- HTTPS：新建 `WiFiClientSecure` 直接 `setCACertBundle(CA_BUNDLE_MOZILLA)`
  ——不经 `http_apply_tls()`，不继承 `tls_insecure`。
- **明文宏打开时**（`OTA_ALLOW_PLAINTEXT`，默认 0，env.cfg 打不开）：
  `http://` 走**纯 `WiFiClient`**、`https://` 走 `WiFiClientSecure`——
  分支写法照 `penpal_api.cpp:pp_request()` 的 `is_https` 模式（:130-143）；
  用 Secure client 对纯 HTTP 服务器根本握手不上（Claude P2，实验室
  联调路径必须真能跑通）。
- `http_ensure_time(5000)` 前置。

### 3.2 流式下载与写入（v2 闭合项 + Grok P2-3/P2-6 收口）

- `HTTPClient` 流式（`getStreamPtr()`）→ `Update.write()` + 增量
  `mbedtls_sha256`；**不用 HTTPUpdate**。
- **超时模型**（Grok P2-3）：HTTP 读**空闲**超时 45s（流式持续有数据则
  总时长可超）；**绝对 deadline 10 分钟**（约 2.1MB @ 3.5KB/s 下限）；
  二者任一触发 = 任务失败：gen+1、`Update.abort()`（若已 begin）、
  释放锁、报错——**不留在"do not power off"死等**。
- **校验顺序**：清单验签 → `Update.begin(size)`（**先拒** size > 空闲槽
  容量；`Content-Length` 与清单 size 不符则不开写——静态服务必须给
  `Content-Length`，不用 chunked）→ 流式写+算哈希 → size/SHA-256 比对
  → 通过才 `Update.end(true)`；**任何失败一律 `Update.abort()`，不得
  `end()`**（Grok P2-6）。
- **快照**（Grok P2-6）：Check 验签后把整个 `ota_manifest_t` 复制进任务
  私有结构，Update 阶段不再触碰清单/网络上的元数据（防"确认后被换包"）。

### 3.3 全局硬件锁与取消令牌（Codex P1-A，v3 新增核心）

```c
static atomic_bool s_ota_hw_lock;    /* 任一时刻至多一个任务碰 Update/flash */
static atomic_bool s_ota_cancel;     /* 取消令牌，UI 线程置位 */
```

- 任务在 `Update.begin()` **之前**取 `s_ota_hw_lock`（CAS）；锁被占 →
  直接失败返回（**跨页面/跨代次生效**——页面 gen 与 busy 不能约束硬件
  操作，离开 Settings 页再进也不会启动第二个写槽任务）。
- **取消令牌检查点**（三处，Codex 要求）：`Update.begin` 前、每个写入
  块边界、`Update.end`（boot 切换提交）前。任一处令牌为真 →
  `Update.abort()` + 释放锁 + 返回"已取消"，**不切换启动槽**。
- UI 的"取消"= 令牌置位 + gen+1 + busy=false（令牌让在飞任务在下个块
  边界自杀；gen 只防迟到结果污染 UI）。
- **UI 呈现与机制解耦**：进入下载阶段后 waitbox 无取消按钮、吞全部按键
  （防误触），但**离开 Settings 屏（Back 键路由）或用户等待期按下的
  按键不会置令牌**——只有显式 Cancel 入口（检查/确认两阶段）与未来
  的超时路径置令牌。任务结束后锁必释放（成功 end / 失败 abort / 取消
  abort 三条路径同一出口）。

### 3.4 任务形态（契约行见 §6）

16KB 栈、优先级 1、core 0；请求快照任务自有；结果
`ota_result_t{gen, ok, err}` 走队列（深度 1）→ UI delete；进度每 10%
经队列刷 UI（EPD 友好）；主循环持续泵 `lv_timer_handler`。

## 4. UI（SCREEN2_2）与全局互斥（Claude P1-A / Grok P2-4）

- 流程：版本 + Check → 清单展示（版本/notes/seq/电量）→ Update 二次
  确认 → 覆盖层 "Updating... N% / do not power off"（**吞全部按键**，
  `chat_waitbox` 同款键盘守卫 + 触摸忽略）→ 完成确认重启 / 失败显示
  err + "still on old version, safe"。
- **OTA busy 期间**（任一阶段，直至任务结束）：
  1. **低电自动关机互斥**：全局 `low_voltage_timer_cb` 检查
     `s_ota_hw_lock`——置位时跳过 `ui_shutdown_on()`（仅保留屏上低电
     提示；关机决策延后到任务结束后下一轮巡检重新评估）。该定时器是
     全局生命周期巡检（`ui_deckpro_entry` 创建后从不暂停），必须显式
     互斥而非依赖"用户停在 OTA 屏"（Claude P1-A：30% 电量检查挡不住
     下载途中继续放电到关机阈值）。
  2. **禁止进入 Sleep**：Sleep 屏入口（菜单）在 OTA busy 时拒绝并提示
     （Sleep 屏自己的 `esp_deep_sleep_start` 是屏幕级定时器，风险本不
     成立，入口拒绝为纵深防御——Claude 已澄清排除屏幕级路径）。
  3. `WiFi.setSleep(false)`；任务结束（成功/失败/超时/取消）**一律
     恢复**（modem sleep 与低电策略）。
- 电量前置：`<30%` 拒绝；gauge 无效时**不用 SOC=0 误拦**，改要求
  `ui_battery_27220_get_input()` 判外部供电（Grok P2-4）；未充电提示
  插 USB。
- `SCREEN2_2_ID` **追加在枚举末尾**（不插在 SCREEN2_1 与 SCREEN3 之间
  ——Grok P2-6）；新屏 create 与进度覆盖层各打一行池水位
  `pp_dbg_pool`（721e04a 教训）。

## 5. 回滚与自证（v3：WDT 提供复位保证——Codex P1-B）

**机制事实**：OTA 状态机在**下一次重启**才把 `PENDING_VERIFY` 判
`ABORTED` 回滚；不重启就冻结在坏固件上。v2 的"无 WDT 挂载前 while(1)
不回滚"是漏洞而非边界。v3：

1. `verifyRollbackLater()` 覆盖保留（v2 已定，`extern "C"`，缺它一切
   形同虚设）。
2. **自证窗口 = setup() 起点至自证点**，期间把 **loopTask 显式订阅
   `esp_task_wdt`（8s）**（setup 首行附近；delay 不喂 task WDT，只有
   `esp_task_wdt_reset()` 喂——自证前正常路径在几个关键初始化步骤间
   显式喂狗）。效果：自证前**任何形态**的死循环（忙等 `while(1){}`、
   让出型 `while(1) delay(10)`、卡在外设初始化）8 秒内 WDT 复位 →
   下次 boot 回滚（Codex"必须保证复位"闭环，且覆盖让出型——比
   Grok P2-5 只认忙等的方案恢复面更宽）。
3. **自证点**：首帧 UI 渲染后（`lv_async_call`，loop 首个
   `lv_timer_handler` 内）调用 `esp_ota_mark_app_valid_cancel_rollback()`
   并**退出 WDT 订阅**（恢复本应用现状：长阻塞 EPD/音频操作不受 8s
   约束——订阅常态化属独立改进，不在本设计范围）。自证点之后死循环
   = 冻结需人工复位，登记为已知边界（层 3）。
4. **三层模型精简版**（Claude P1-B：不能只存在于 v1 git 历史）：

   | 层 | 场景 | 结果 |
   |---|---|---|
   | 1 | 下载中断/坏包/验签失败/取消 | otadata 不翻转，重启回上一张已验证槽，零损伤 |
   | 2 | 新固件已切换但自证前挂死 | task WDT 8s 复位 → 下次 boot 自动回滚 |
   | 3 | 自证后挂死 / 全挂 | 人工复位或 USB：**分块烧录**（坑 6）；**强制回 app0 = 擦 otadata（`write_flash 0xE000` 8KB `0xFF`）+ 重刷对应槽**——OTA 后运行槽可能是 app1，须先读 otadata/boot 分区确认再写（Grok P2-6，v1 命令不完整） |

## 6. 契约登记（Qwen P2 发起，v3 按 Grok P2-3 修订超时）

`async_ipc_contract.md` 与代码**同批**新增行（设计稿草案不替代契约表
本身——Claude 提醒）：

- 消费者 Settings(SCREEN2_2)；任务 ota_run；结果 `ota_result_t{gen,...}`
  队列深度 1；busy `s_ota_busy`+`s_ota_busy_gen`（UI 线程）；
- **取消** = 令牌置位 + gen+1 + busy=false；写槽不强制中止，令牌在块
  边界生效（§3.3）；
- **超时** = HTTP 读空闲 45s + **绝对 deadline 10 分钟**（覆盖 NTP≤5s +
  连接 + 读取全程，契约规则 10）；超时处理与其它任务一致（gen+1 +
  abort-if-begun + 报错），**例外声明**：栈 16KB（惯例 8KB）、队列深度
  1、任务可存活跨页面（硬件锁所致）。

## 7. 发布流程与受控基线（恢复 v1 正文 + Codex P2）

### 7.1 发布步骤（每次发版）

```
pio run -e pda2                          # 产出 firmware.bin（版本烘焙自 utilities.h）
python scripts/ota_sign.py               # 读 bin → sha256/size；读 utilities.h 版本；
                                         # 读递增 seq；私钥（gitignored）签名；
                                         # 产出 manifest JSON（version/url/size/
                                         # sha256/seq/notes/sig，规范字节串见 §2.1）
上传 firmware.bin + manifest 到 OTA_URL 同目录
设备: Settings → Firmware Update → Check → Update → Reboot
```

发布脚本同时校验 `ota_trust_anchor.h` 指纹、清单 version == bin 内烘焙
版本（结构性防错位，Qwen P3 处置）。

### 7.2 USB 回退（全挂兜底——命令完整版，Grok P2-6 修正）

```
# 1) 确认运行槽（OTA 后可能是 app1）：esptool read_flash 0xE000 8KB 看 otadata
# 2) 擦 otadata（回退到"另一张已验证槽"由 bootloader 决定）
python esptool.py write_flash 0xE000 <8KB 0xFF>
# 3) 若目标槽镜像也已损坏：分块烧录重写该槽（坑 6 流程，app0@0x10000 / app1@0x650000）
```

### 7.3 受控基线（Codex P2）

- 新增 **tracked** 文件 `docs/ota-baseline.md`：记录分区表来源
  （`default_16MB.csv`）、bootloader 产物 SHA-256、两台实机读回的分区
  表/bootloader 摘要、`ota_trust_anchor.h` 指纹——**实现期首件任务**
  （Codex"备份不能是唯一兼容性依据"）；整片 `backups/` 继续作为恢复
  物料（gitignored）。
- 框架包升级（esp32s3 sdkconfig/分区变更）= OTA 兼容性破坏事件，须重
  跑 §8 全部用例并更新基线。
- **换签名公钥 = USB 刷机**（无远程换根通道，§2.2）。

## 8. 验证计划（v3 重写——Grok P2-5 + Codex P1-B 修正用例物理）

全部在真机（机 #1 先行），每例记录 reset 来源与 pending→aborted→
previous-valid 状态链：

1. **回滚矩阵**（修正 v2 与物理矛盾的用例）：
   a. setup **忙等** `while(1){}`（无 yield）→ task WDT 8s 复位 → 回滚；
   b. setup **让出型** `while(1) delay(10)` → task WDT 复位 → 回滚
      （WDT 订阅使其可恢复——v2 误列为"不回滚"）；
   c. **主循环自证点前**死循环 → 回滚；
   d. **主循环自证点后**死循环 → **不**回滚（已知边界，登记）；
   e. `drv.begin` 失败路径（拆雷后）→ 记日志继续启动，不回滚不冻结。
2. 正向：局域网服务 + "版本+1"固件全流程（Check→Update→重启→About
   版本变化；seq 递增入 NVS）。
3. 签名/完整性用例：正确签名通过；篡改 version/url/size/sha256/seq/
   notes 任一 → 拒绝；**DER 编码签名 → 拒绝**（钉死 P1363）；错公钥 →
   拒绝；缺字段 → 拒绝；`seq` 回放/降级 → 拒绝。
4. TLS/传输：自签证书（`tls_insecure` 开着）→ OTA 仍强制 CA 校验失败；
   `Content-Length` 缺失/与 size 不符 → 不开写；404/断网 → 拒绝无副作用；
   下载中断网 → 45s 空闲超时 → abort + 报错。
5. **低电互斥**（Claude P1-A）：OTA 下载中人为触发低电锁存阈值 →
   确认不执行 `ui_shutdown_on()`、任务完成后再评估。
6. 按键吞没：更新中按 MIC/Back/Sym/触摸 → 无 UI 副作用、不中断任务。
7. 双机各过一遍。

## 9. 实施顺序与提交拆分（Grok P2-6.7：不再"一次提交"）

1. 回滚真测先行（§8.1 a–e，需要临时自毁测试固件）——**先于首次真实
   OTA**；
2. commit 组 1：签名/发布脚本 + `ota_trust_anchor.h` + env 模板；
3. commit 组 2：`ota_update` 模块 + 契约行；
4. commit 组 3：SCREEN2_2 UI + 全局互斥（低电/Sleep/吞键）；
5. commit 组 4：`verifyRollbackLater` 覆盖 + WDT 窗口 + `drv.begin`
   拆雷 + 文档同步；
6. 每组独立走 §8 对应用例。

## 10. 实现合同清单（Grok P2-6，编码时逐条核对）

快照进任务不复拉清单；size > 空闲槽拒绝；`Content-Length` 强制且匹配；
失败 `abort()` 不 `end()`；枚举追加式；池水位观测点；明文宏走 plain
client；URL 配置源 = env.cfg `OTA_URL`；绝对 deadline 10min；低电定时
器互斥；`WiFi.setSleep` 恢复；公钥 tracked/私钥 gitignored；P1363 签名
编码 + `mbedtls_ecdsa_verify`；换根仅 USB。

## 11. 已裁决事项（不再开放）

- 签名算法 = ECDSA P-256（Grok A 裁定 + Claude 裁定接受；Codex v1 的
  "Ed25519"字样由 Grok 逐字放宽为"算法可替换"，性质要求已满足）。
- 手动触发 / 字符串版本不等即更新 / 自证不绑 WiFi / 写槽不可用户中止
  （令牌在块边界生效）/ `notes` 从展示升级为签名对象——均定案。

## 12. v2→v3 修订记录（逐条对照）

| # | 处置（§节） | 来源 |
|---|---|---|
| 1 | §3.3 全局硬件锁 + 取消令牌（三检查点，abort 不切槽；跨页面/代次） | Codex P1-A |
| 2 | §5.2 task WDT 8s 自证窗口（自证前任何死循环可恢复）；§5.4 层 2 文案 | Codex P1-B / Grok P2-5 |
| 3 | §2.2 公钥入 tracked `ota_trust_anchor.h`（key_id+编码+指纹校验），私钥才 gitignored | Codex P1-C / Grok P2-2 |
| 4 | §4.1 低电关机互斥 + §4.2 Sleep 禁入 + `WiFi.setSleep(false)` + waitbox 吞键 | Claude P1-A / Grok P2-4 |
| 5 | §7.1/§7.2/§5.4 恢复发布步骤 + USB 回退完整命令（修正 v1 命令的 app1 缺口）+ 三层表精简版；修复 v2 两处悬空引用（原 §5.4"（§6）"、§8.5"（§2.4 行 6）"） | Claude P1-B / Grok P2-6 |
| 6 | §2.1 `notes`、`seq` 入签名；§2.1 seq 单调 + NVS last_seq + 降级宏 | Codex P2 |
| 7 | §7.3 tracked 基线文件 `docs/ota-baseline.md` | Codex P2 |
| 8 | §2.3 `OTA_URL` 配置源恢复 + env.cfg.example 过时注释修正 | Grok P2-1 |
| 9 | §2.1 签名编码钉死 IEEE P1363 + `mbedtls_ecdsa_verify`（禁 read_signature）+ §8.3 DER 拒绝用例 | Grok P2-2 |
| 10 | §3.2 超时模型（读空闲 45s + 绝对 deadline 10min）+ §6 契约超时例外 | Grok P2-3 |
| 11 | §3.1 明文宏的 scheme 分支（plain `WiFiClient`） | Claude P2 / Grok P2-6 |
| 12 | §10 实现合同清单（快照/槽容量/Content-Length/abort/枚举追加/池水位/拆提交） | Grok P2-6 |
| 13 | Qwen P3 两项维持 v2 处置（版本一致性入发布脚本；x-MD5 不采纳——sha256 已入签名） | Qwen P3 |
