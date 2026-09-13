# 设计评审申请书：OTA 固件远程升级（v5 修订稿）

- **申请人**：Claude（pda2）
- **申请日期**：2026-09-13
- **关联分支**：`HD-V2-250915`
- **评审要求**（对全体评审方）：按 [`docs/review_guide.md`](review_guide.md)
  执行——L1 DOC-ALIGNED（§2.1）+ A/B/C；findings 按 §4.2 定级留坐标；
  DOC/SPEC/CODE/SIM 为本档证据（§3.1）；结果写入
  `docs/reviews/ota-update-design-review-result-<本稿锚>-<reviewer>.md`
  （锚 = 本稿 commit SHA，见 git log 最新一条；**新建文件绝不覆盖**）；
  报告含对称三格与 §9【D】条目自审。
- **状态**：**v5 修订稿，送复审**——v4（`3eb6f7e`）复审：Codex **C** /
  Grok **C** / Claude **C** / Qwen **C**（接近 L1，无 P1）。四方独立命中
  同一 P1：**签名对象把 `sig` 自身编入，自指不可构造**（v3 本是对的
  显式六字段枚举，v4 措辞退化）。v5 修复该 P1 并处置 4 P2 + 3 P3，
  对照见文末 §13。
- **开发者自评（§7.1 强制）**：
  - **最有把握**：① 签名对象恢复 v3 式**显式六字段枚举**（v3 轮四方
    已验证自洽，本轮只是找回）；② `s_ota_inflight` 单飞守卫（PenPal
    `s_pp_inflight` 同款先例，`acc3893` 已真机验证）；③ 快照所有权链
    （launch-time copy 是契约 §2.5 既有语义，非新造）。
  - **最没把握（≤3）**：① **喂狗点覆盖完备性**——T 改为约束"最长喂狗
    间隔"后，安全性取决于"所有长阻塞段都有喂狗点"这一审计结论；漏标
    一处（如未来新增的外设探测）即开窗。缓解：§10 加"新增 setup 长
    阻塞必须同步插喂狗点"的实现合同 + §8.1b 回滚矩阵兜底验证。②
    `esp_task_wdt_init(T,true)` re-init 时 **idle 任务订阅的处理**——
    Grok 已核框架语义（再调用即更新 timeout/panic，无需 deinit），但
    两台机无真机复跑，返回值/边界按 IDF 文档推演。③ 自证门控依赖
    EPD 首帧完成——若新固件**显示硬件失效**（首帧永远不完成），WDT
    会把"屏坏但系统健康"的固件也回滚；这是**有意的设计决策**（自证
    语义 = "用户可见可用"，屏坏即不可用），请评审确认接受。
  - **知道没测到的**：全部真机项（§8）；`esp_task_wdt_*` 返回值按
    IDF 头文件推演。

---

## 0. v5 变化点（相对 v4）

1. **签名对象修正**（Codex/Grok/Claude/Qwen 四方共同 P1）：显式
   **六字段**枚举（version, url, size, sha256, seq, notes），**`sig`
   不在签名对象内**——它是对该六字段字节串的签名本身；保留 v4 的两条
   改进（JSON 值直接拼接、notes 禁换行）。
2. **TWDT 可实施合同**（Codex P2 / Grok P2-1）：句柄用
   `esp_task_wdt_add(NULL)`（当前任务 = loopTask，**禁止**把函数名
   `loopTask` 当句柄写）；喂狗点由 `s_boot_wdt_subscribed` 标志守卫
   （自证后 EPD 正常刷新**不再** reset——否则稳定 `ESP_ERR_NOT_FOUND`）；
   窗口关 = `delete(NULL)` + `init(5, true)`；失败处理写入合同。
3. **取号副作用隔离**（Grok P2-2）：`ui_disp_full_refr_seq()` 有副作用
   （触发整刷 + seq++），**只调用一次**存入变量；自证轮询只读
   `ui_disp_flush_done_seq()` 比较（复用 repo 内 WiFi 覆盖层/Sleep
   两处既有惯用法，`ui_deckpro.cpp:2808/2830`、`:4518/4494`）。
4. **单飞守卫**（Qwen P2-1）：worker 自有 `s_ota_inflight`（原子，
   cap 1，`xTaskCreate` 前增、任务退出减），**同时覆盖 Check 与
   Update**，不依赖会被页面 gen-reset 的 `s_ota_busy`（PenPal
   `s_pp_inflight` 先例）。
5. **快照所有权链**（Qwen P2-2）：Check worker 把已验签 manifest
   **随结果队列交回 UI**（结果结构携带 manifest）→ CONFIRM_WAIT 期
   **UI 拥有** → 用户确认时 launch-time copy 进 Update 任务自有结构
   ——全程无共享可变缓冲，"确认的包 = 刷入的包"。
6. **数值与度量修正**（Qwen P3-1/P3-2 / Codex P3）：`A7682E_init`
   失败路径 **≈8.4s**（v3/v4 的 6.1s 低估 2 轮 testAT）；校准度量
   改为**最长喂狗间隔**（T 约束的是间隔不是总时长——具名喂狗使段长
   短化，T=30s 下限对 ~1s 级间隔余量巨大），测点延伸到自证点。

---

## 1. 现状盘点（承 v4；v4 复审核准 8/9 条引用，1 条数值修正）

- 分区表/回滚开关/固件体积/电量接口/TWDT 框架默认（5s、PANIC=y、
  loopTask 默认未订阅、`delay()` 不喂 loopTask）——承 v3/v4，§7.3
  基线复核。
- **修正**（Qwen P3-1）：`A7682E_init` 失败路径 ≈**8.4s**——
  `retry++ > 5` 后置自增使 `testAT(1000)` 实跑 **7 次**（7000ms）+
  break 分支 1100ms + 上电 70ms + 循环后 200ms；机 #2 每次开机必走满。
  非承重（T 约束喂狗间隔而非总时长），但"已核实"数字必须准确。
- **EPD 首帧序列号**：`ui_disp_full_refr_seq()`（`factory.ino:864`，
  **有副作用**：`disp_flush_req_seq++` 并触发整刷）/ `ui_disp_flush_
  done_seq()`（`:870` 只读；`:241` 在 `nextPage()` 后写入"THIS frame
  reached the panel"）。既有惯用法两处：WiFi 扫描覆盖层
  （`ui_deckpro.cpp:2808` 取号 → `:2830` 比对）、Sleep 倒计时
  （`:4518` → `:4494`）。
- **mbedtls 2.28.4 无 EdDSA**；框架另有 libsodium 提供 Ed25519——
  采用 mbedtls ECDSA 即避免引入 libsodium 签名依赖（Qwen Nit-1 精确
  措辞）。
- **Arduino 任务模型**（Codex/Grok 实锤）：`loopTask` 是入口**函数**
  `void loopTask(void*)`，句柄是 `loopTaskHandle`——TWDT API 处
  **不可写函数名**，用 `NULL`（当前任务）或句柄。

## 2. 清单协议（v5：签名对象显式六字段）

### 2.1 清单格式与签名

```json
{
  "version": "v2.5-260912",
  "url":    "https://host/path/firmware.bin",
  "size":   2100240,
  "sha256": "<64 hex>",
  "seq":    42,
  "notes":  "PenPal TTS; voice fixes",
  "sig":    "<base64, 64 bytes IEEE P1363 r||s>"
}
```

- **签名对象 = 显式六字段**：`version`、`url`、`size`（十进制）、
  `sha256`、`seq`（十进制）、`notes`——按此顺序各值后接一个 `\n`
  拼成规范化字节串（值直接取自 JSON 字段，不对 JSON 文本按行切分；
  `notes` 含 `\n`/`\r` → 拒绝发布）。
- **`sig` 不在签名对象内**——它是**对该六字段字节串的签名结果本身**
  （四方 v4 轮共同 P1：v4 的"七字段按序拼接"把 `sig` 计入，构成
  `sig = Sign(..., sig)` 自指，数学上不可构造）。JSON 有七个键，
  其中**六个**进签名字节串；设备端按同序六字段重建字节串验签。
- **验签 API**：`mbedtls_mpi` 拆 r/s → `mbedtls_ecdsa_verify`；
  **禁** `mbedtls_ecdsa_read_signature`（ASN.1 DER 路径）。
- **seq 时机**（v4 已定，承前）：Check 只比较；`last_seq` 仅
  `Update.end(true)` 成功后写 NVS；失败/取消保持原值 → 同版本可重试。
- **URL scheme 双查**：`OTA_URL` 与已签名 `url` 都在明文宏关闭时拒绝
  `http://`。

### 2.2 信任根（承 v4）

`ota_trust_anchor.h`（tracked）：公钥 65B 未压缩 SEC1 唯一格式、
`key_id` 仅诊断、指纹脚本校验；私钥 gitignored；换公钥 = USB 刷机。

### 2.3 配置源（承 v3/v4）

`OTA_URL` 从 `/env.cfg`；宏关闭时非 https 的 OTA_URL 与签名 url 双拒。

## 3. 模块 `examples/pda2/ota_update.h/.cpp`（v5：单飞守卫 + 所有权链）

```c
void ota_check_async(uint32_t gen);            /* 清单+验签 → 结果(含 manifest) 队列 */
void ota_start_async(uint32_t gen, const ota_manifest_t *snap);  /* 快照 launch-time copy */
bool ota_precheck_ui(char *err, int err_len);  /* 仅无网络项（电量/URL 在场） */
const char *ota_current_version(void);
```

### 3.1 单飞守卫（Qwen P2-1——不依赖页面生命周期）

```c
static atomic_bool s_ota_inflight;   /* cap 1：xTaskCreate 前增、任务退出减 */
```

- **Check 与 Update 共用**：inflight 未归零时，新 Check/Update 一律
  拒绝（提示 "previous OTA op closing"）——**跨页面/跨代次生效**，
  页面 destroy 的 `busy=false`/`gen++` **不影响**它（这正是 v4 缺口：
  busy 会被 contract §2.8 复位）。
- 先例：PenPal `s_pp_inflight` 原子计数（`acc3893`，issue_list §10）。
- 页面级 `s_ota_busy`/`gen` 保留，仅管 UI 状态与结果展示门控。

### 3.2 快照所有权链（Qwen P2-2——消除 TOCTOU）

```
Check worker：验签通过 → ota_check_result_t{gen, ok, manifest(拷贝), err}
              → xQueueSend → 【worker 结束，所有权移交 UI】
CONFIRM_WAIT：UI 拥有该 manifest 快照（展示 version/notes/seq 的就是它）
用户确认    ：UI 把快照 **launch-time copy** 进 Update 任务自有结构
              （契约 §2.5 语义）→ ota_start_async(gen, &snap)
Update worker：只读任务自有副本，全程不回读任何共享/静态缓冲
```

- **无静态共享 `s_ota_manifest`**——v4 模糊地带（CONFIRM_WAIT 无任务
  期快照放哪）由"结果结构携带 manifest + UI 持有"显式关闭；
  P2-1 的陈旧 worker 也无法污染已确认快照（它根本碰不到 UI 持有的
  拷贝）。
- **用户确认的包 = 刷入的包**（签名验证 → 展示 → 确认 → 刷入四点
  同一快照），TOCTOU 关闭。

### 3.3 网络与流式（承 v4 已闭合项）

专用 `setCACertBundle`；明文宏 + `is_https` scheme 分支（plain
`WiFiClient`）；流式 `HTTPClient → Update.write` + 增量 SHA-256；
size > 空闲槽拒、`Content-Length` 强制；失败一律 `Update.abort()`；
`s_ota_hw_lock`（CAS）+ 取消令牌三检查点；读空闲 45s + 绝对 deadline
10min（任务本地）；**写循环禁 `xQueueSend`**——进度走
`static atomic_int s_ota_percent/phase`，UI 定时器轮询（结果通道与
进度通道分离，Grok P2-4/Qwen P3-2 承前）。

## 4. UI 与全局互斥（承 v4，互斥条件 `lock || busy` + 安全阀）

SCREEN2_2（枚举追加末尾 + 注释）：Check（worker 化等待层）→ 清单展示
（展示的即 UI 持有快照）→ 确认 → 覆盖层（吞键）→ 完成/失败。
`low_voltage_timer_cb` 在 **`s_ota_hw_lock || s_ota_busy`** 时跳过
`ui_shutdown_on()`；**安全阀**：抑制超一个 deadline 窗口（10min）后
低电关机强制放行（宁可打断可恢复的写入，不深放电——Qwen P3-1 场景
的边界回答）；Sleep 入口 busy 拒绝；`WiFi.setSleep(false)` 成对恢复；
电量 `<30%` 拒 / gauge 无效看 `get_input()`；池水位观测点保留。

## 5. 回滚与自证（v4 骨架 + v5 可实施合同）

### 5.1 机制事实（承 v3/v4 已核验项）

OTA 回滚在下一次 boot 才判定；`initArduino()` 自动自证须以
`extern "C" bool verifyRollbackLater() { return true; }` 覆盖；TWDT
框架默认 5s/panic=y/loopTask 未订阅；`drv.begin()` 的 `while(1)` 已拆。

### 5.2 自证窗口与喂狗合同（v5：可照抄的调用序列——Codex P2 / Grok P2-1）

```cpp
/* 窗口开（setup() 首行附近；此时运行上下文 = loopTask） */
esp_err_t e = esp_task_wdt_init(T, true);          /* T 见 §5.3，panic 使能 */
/* 检查 e；失败 = 记日志 + 视为实现期阻断（此窗口无层 2 兜底，禁止带病继续） */
e = esp_task_wdt_add(NULL);                        /* NULL = 当前任务，勿写 loopTask */
s_boot_wdt_subscribed = true;                      /* 喂狗点统一守卫 */

/* 具名喂狗点（四处，均在 loopTask 上下文内；每处先查守卫标志）：
 *   A7682E_init  的 testAT 重试循环每轮
 *   GPS_Recovery/getAck 每轮
 *   EPD nextPage/BUSY 等待循环每轮
 *   pcm5102a_init 写 WAV 每块
 * if (s_boot_wdt_subscribed) esp_task_wdt_reset();  */

/* 自证点（主循环首帧 EPD 完成后）：
 *   取号：首帧整刷发起处 const uint32_t seq = ui_disp_full_refr_seq();
 *         （有副作用——只调用这一次，存变量；轮询只读 done）
 *   ui_disp_flush_done_seq() >= seq 达成 →
 *   esp_ota_mark_app_valid_cancel_rollback();
 *   esp_task_wdt_delete(NULL);
 *   esp_task_wdt_init(5, true);                      /* 恢复框架默认 */
 *   s_boot_wdt_subscribed = false;                   /* 此后 EPD 刷新不再 reset */
```

- 失败/边界：`add` 失败 → 记日志、窗口降级为无层 2（§8.1 须能观察到
  并阻断发布）；自证后正常 EPD 刷新**不再** reset（守卫标志关闭，
  避免 `ESP_ERR_NOT_FOUND` 稳定刷屏——Grok P2-1）。

### 5.3 校准与 T（v4 骨架 + Codex P3 度量修正）

- **度量对象修正**（Codex P3 / Qwen P3-2）：T 约束的是**最长喂狗间隔**
  （相邻两次 `esp_task_wdt_reset()` 之间），不是 setup 总时长——具名
  喂狗点把大段阻塞切短后，间隔都在秒级（testAT ~1s/轮、getAck
  ~800ms/轮）。校准打点仍测到**自证点**（Qwen P3-2：setup + 首帧 +
  EPD 完成），但用途是记录与回归比对。
- **T 的确定**：`T = max(实测最长喂狗间隔) × 2`，下限 **30s**；两台机
  各测；慢路径（GPS 冷启动、慢 SD）落入**喂狗点之间**的间隔被逐段
  封顶，不依赖"单次健康启动样本覆盖一切"（Codex P3 的正面回答）。
- **喂狗完备性审计**（自评①）：实现期按 §10 合同审计——setup/loop
  中任何 >T/4 的新增阻塞段必须同步插喂狗点（评审后补进实现 checklist）。

### 5.4 三层模型（承 v3/v4 精简版）

| 层 | 场景 | 结果 |
|---|---|---|
| 1 | 下载中断/坏包/验签失败/取消/低电安全阀 | otadata 不翻转，重启回上一张已验证槽 |
| 2 | 新固件自证前挂死（任意形态） | task WDT（T 秒）复位 → 下次 boot 自动回滚 |
| 3 | 自证后挂死 / 全挂 | 人工复位或 USB（§7.2） |

## 6. 契约登记（v5：并入单飞与所有权语义）

`async_ipc_contract.md` 同批新增：消费者 Settings(SCREEN2_2)；worker
ota_check/ota_start（共用）；**单飞 = `s_ota_inflight`（跨 gen/页面，
cap 1）**；结果 `ota_check_result_t{gen, ok, manifest, err}` /
`ota_result_t{gen, ok, err}` 队列深度 1 只运结果；快照所有权链
（worker→UI→Update 任务自有，§3.2）；取消 = 令牌 + gen+1 + busy=
false；超时 = 读空闲 45s + 绝对 10min（任务本地）；例外：栈 16KB、
任务可跨页面存活（硬件锁+inflight 所致）、进度走原子轮询不走队列。

## 7. 发布流程与受控基线（承 v4，命令可照抄）

### 7.1 发布步骤

```
pio run -e pda2                     # firmware.bin（版本烘焙自 utilities.h）
python scripts/ota_sign.py          # sha256/size + utilities.h 版本 + 递增 seq
                                    # + 私钥对六字段字节串签名 → manifest
上传 firmware.bin + manifest 到 OTA_URL 同目录
设备: Settings → Firmware Update → Check → Update → Reboot
```

### 7.2 USB 回退（可照抄）

```
python esptool.py --port COM5 read_flash 0xE000 0x2000 otadata.bin   # 确认运行槽
python esptool.py --port COM5 erase_region 0xE000 0x2000             # 擦 otadata
# 擦后 bootloader 默认回 ota_0（未必是"另一张已验证槽"）；ota_0 恰为坏镜像时：
# 分块烧录重写好镜像（坑 6 流程；app0@0x10000 / app1@0x650000）
```

### 7.3 受控基线 `docs/ota-baseline.md`（tracked；实现期首件）

分区表来源与 SHA、bootloader SHA、otadata 0xE000/0x2000、槽偏移/容量、
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`、`CONFIG_ESP_TASK_WDT_PANIC=y`
+ `TIMEOUT_S=5`、两机读回摘要、`ota_trust_anchor.h` 指纹、**校准数据**
（最长喂狗间隔 + time-to-valid）。框架升级 = 兼容性破坏事件，重跑 §8。

## 8. 验证计划

1. **回滚矩阵**（记录 reset 来源 + pending→aborted→previous-valid）：
   a. setup 忙等 `while(1){}` → T 秒 WDT 复位 → 回滚；
   b. setup 让出型 `while(1) delay(10)` → T 秒 WDT 复位 → 回滚；
   c. 主循环自证点前死循环 → 回滚；
   d. 自证点后死循环 → 不回滚（已知边界）；
   e. `drv.begin` 失败 → 记日志继续启动。
2. **健康固件正向**：两台机 OTA 后必须自证成功；记录 reset 原因（应无
   TWDT）与 time-to-valid；喂狗间隔打点核对 T ≥ 2× 最长间隔。
3. **硬件锁并发**：下载中 Back 离屏 → 重进 → 立即 Update → 被拒且原
   任务不受影响。
4. **Check 并发**（Qwen P2-1 用例）：Check 在飞 → 离屏 → 重进 → 立即
   Check → 被 `s_ota_inflight` 拒（"previous OTA op closing"）。
5. **快照一致性**（Qwen P2-2 用例）：Check A 展示确认 → Update 刷入
   的 sha256 == A.sha256（串口比对）；Check 在飞时无法覆盖 UI 持有
   快照（结构上无共享缓冲）。
6. 签名/完整性：六字段正确签名 → 通过；篡改六字段任一 → 拒；**DER
   签名 → 拒**；压缩点(33B)/无前缀(64B) 公钥 → 拒；错公钥/缺字段 → 拒；
   seq 回放/降级 → 拒；**占位 sig + 六字段正确签名 → 通过**（P1 修复
   的可执行性钉死——Claude v4 轮建议）。
7. 传输：断网 45s → abort + 报错 → **同 seq 重试成功**；Content-Length
   缺失/不符 → 不开写；404 → 拒绝。
8. **低电安全阀**：下载中触发低电锁存 → 不关机；超一个 deadline 窗口
   仍临界 → 强制关机放行；任务结束恢复正常评估。
9. 按键吞没 + 池水位；双机各过一遍（机 #1 先行）。

## 9. 实施顺序（承 v4：校准前置 + 分组提交）

0. 校准打点（§5.3）+ 喂狗完备性审计 → 定 T、填 §7.3；
1. 回滚真测矩阵（§8.1，临时自毁固件）——先于首次真实 OTA；
2. commit 组 1：签名/发布脚本 + `ota_trust_anchor.h` + env 模板；
3. commit 组 2：`ota_update` 模块（inflight/所有权链/原子进度）+ 契约行；
4. commit 组 3：SCREEN2_2 UI + 全局互斥 + 安全阀；
5. commit 组 4：`verifyRollbackLater` + WDT 窗口合同 + EPD 首帧门控 +
   文档同步。

## 10. 实现合同清单（编码逐条核对）

v4 清单承前（快照不复拉；size>空槽拒；`Content-Length` 强制；失败
`abort()`；枚举追加末尾+注释；池水位；明文宏 plain client 且双查；
URL 源 env.cfg；45s+10min 任务本地；低电 `lock||busy`+安全阀；
`WiFi.setSleep` 成对；P1363+`ecdsa_verify`；notes 无换行；换根仅 USB；
last_seq 仅 end 后写），**新增**：TWDT 句柄用 `NULL` 禁写函数名；
喂狗点统一 `s_boot_wdt_subscribed` 守卫；`ui_disp_full_refr_seq()`
取号一次存变量（禁入比较式）；`s_ota_inflight` cap 1 覆盖双阶段；
快照所有权链（结果携带 manifest / launch-time copy）；新增 setup 长
阻塞段必须同步插喂狗点。

## 11. 已裁决事项

ECDSA P-256（mbedtls 2.28 无 EdDSA、避免引入 libsodium——Qwen Nit-1
精确措辞）；手动触发；字符串版本不等；自证不绑 WiFi；写槽不用户中止
（令牌块边界生效）；自证依赖 EPD 首帧（屏坏固件回滚是有意决策——
自证语义 = 用户可见可用，见自评③，请复审确认）；`SCREEN2_2_ID`
追加末尾。

## 12. v4→v5 修订记录（逐条对照）

| # | 处置（§节） | 来源 |
|---|---|---|
| 1 | §2.1 签名对象改**显式六字段枚举**、`sig` 排除在外（恢复 v3 自洽写法 + 保留 v4 两条澄清）；§8.6 占位 sig 用例 | Codex P1 / Grok P1 / Claude P1 / Qwen（四方共同） |
| 2 | §5.2 TWDT 可实施合同：`add(NULL)`/`delete(NULL)`（禁写函数名）、`s_boot_wdt_subscribed` 喂狗守卫（自证后 EPD 不再 reset）、窗口开失败=实现期阻断 | Codex P2 / Grok P2-1 |
| 3 | §5.2 取号副作用隔离：`ui_disp_full_refr_seq()` 单次调用存变量，轮询只读 done（复用 WiFi 覆盖层/Sleep 既有惯用法） | Grok P2-2 |
| 4 | §3.1 单飞守卫 `s_ota_inflight`（cap 1，覆盖 Check+Update，跨 gen/页面）；§8.4 Check 并发用例 | Qwen P2-1 |
| 5 | §3.2 快照所有权链（结果携带 manifest → UI 持有 → launch-time copy 进 Update 任务）；§8.5 一致性用例 | Qwen P2-2 |
| 6 | §1 `A7682E_init` 失败路径 6.1s → **8.4s**（后置自增边界 7 轮 testAT 逐行推算） | Qwen P3-1 |
| 7 | §5.3 校准度量改**最长喂狗间隔**（T 约束间隔非总时长）；测点延伸到自证点 | Codex P3 / Qwen P3-2 |
| 8 | 自评③（EPD 首帧门控对屏坏固件的取舍）显式提请裁定 | 自评诚实性（Claude v4 轮对齐要求） |
