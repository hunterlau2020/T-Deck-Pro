# 设计评审申请书：OTA 固件远程升级（v6 修订稿）

- **申请人**：Claude（pda2）
- **申请日期**：2026-09-13
- **关联分支**：`HD-V2-250915`
- **评审要求**（对全体评审方）：按 [`docs/review_guide.md`](review_guide.md)
  执行——L1 DOC-ALIGNED（§2.1）+ A/B/C；findings 按 §4.2 定级留坐标；
  DOC/SPEC/CODE/SIM 为本档证据（§3.1）；结果写入
  `docs/reviews/ota-update-design-review-result-<本稿锚>-<reviewer>.md`
  （锚 = 本稿 commit SHA，见 git log 最新一条；**新建文件绝不覆盖**）；
  报告含对称三格与 §9【D】条目自审。
- **状态**：**v6 修订稿，送复审**——v5（`e0ec3bb`）复审：Claude **A**
  （五轮首个 L1）/ Codex **C** / Grok **C** / Qwen **C**。残余 1 P1
  （结果通道表示）+ 1 P2（离屏排空/释放合同）+ 4 P3 + 1 Nit，全部为
  同步/生命周期收口项，架构不变。处置对照见文末 §13。
- **开发者自评（§7.1 强制；v6 按本轮 Qwen P2 修正语义声明）**：
  - **最有把握**：① 指针传递结果通道（PenPal `pp_result_t` 同款先例，
    五轮评审均确认其 new/delete 纪律）；② `esp_task_wdt_init(T,true)`
    re-init 更新超时/panic 的语义（qwen v5 轮已核框架头文件 DOC 级确
    证：`esp_task_wdt.h:28-30`）；③ 快照所有权链（launch-time copy，
    契约 §2.5 既有语义）。
  - **最没把握（≤3）**：① **喂狗点覆盖完备性**——T 约束最长喂狗间隔
    后，安全性取决于"所有长阻塞段都有喂狗点"；v5 漏了 EPD 库内
    `_waitWhileBusy`（不可插喂狗）与 `sd_care_init`，v6 已改：喂狗点
    清单加 SD 段、并把"库内不可喂狗段"明确为 T 的下界来源（§5.3）。
    仍存风险：未来新增未插喂狗点的阻塞段——§10 实现合同约束。②
    `esp_task_wdt_init` re-init 时 idle 订阅任务的跨段行为——框架头
    文件 DOC 确证，无真机复跑；§8.1a 真机复核兜底。③ **自证语义**
    （本轮按 Qwen P2 修正）：自证门控 = 首个 FULL flush 的**软件序
    列完成**；它捕获 setup/loop 首帧前的挂死与 LVGL 崩溃，但**不证明
    像素上屏**（`GxEPD2` busy 等待有界超时后 `done_seq` 照常更新）；
    屏坏固件**会自证、不回滚**——可接受（回滚修不好坏屏，层 3 USB
    兜底），见 §5.2 语义声明。
  - **知道没测到的**：全部真机项（§8）。

---

## 0. v6 变化点（相对 v5）

1. **结果通道改指针传递**（Codex P1）：FreeRTOS 队列按字节复制——
   v5"结果含 manifest 随队列移交所有权"的表述不成立（含 `std::string`
   的结构按值入队 = 悬空指针/堆损坏）。v6：队列只运**指针**（worker
   `new` → UI 排空后 `delete` 恰一次，全路径），manifest 全程堆对象
   随指针走——PenPal `pp_result_t` 同款先例。
2. **离屏排空点**（Grok P2 / Codex P2）：`factory.ino` loop()
   **无条件**调 `ota_result_poll()`（先排空、后屏活跃判断——PenPal
   `penpal_keyboard_poll` 的 drain-then-active 同款）；单飞守卫的
   释放与队列解耦（见 4）。
3. **单飞/锁生命周期合同**（Codex P2 / Qwen Nit）：`s_ota_inflight`
   在 `xTaskCreate` **成功后**递增（失败 → 删快照+报错，不递增）；
   递减在 worker **唯一出口**（成功/失败/取消/超时四路汇合），与
   `s_ota_hw_lock` 释放同点；结果发送用 **`xQueueOverwrite`**（永不
   阻塞——"worker 卡在满队列 → inflight 永不清零"结构上不可能）。
4. **TWDT add 失败分支**（Codex P3）：守卫标志**仅在 `add` 返回
   `ESP_OK` 后**置位；失败保持 false——喂狗点全部跳过，不留"对未订阅
   任务 reset"的错误路径。
5. **自证语义声明修正**（Qwen P2）：撤销 v5 自评③/§11"屏坏固件回滚"
   的假声明——自证门控实测的是"LVGL→EPD **软件刷新序列完成**"
   （`done_seq` 在 nextPage 循环退出后无条件更新；`_waitWhileBusy`
   有界超时），**不证明像素上屏**；屏坏 → 会自证、不回滚（可接受，
   层 3 兜底）。本轮有代码级证据（`factory.ino:228-241` +
   `GxEPD2_EPD.cpp:137-151`）。
6. **规范化字节串编码钉死**（Grok P3-1）+ **EPD 库内等待与 SD 段**
   （Qwen P3-1 / Grok P3-2）+ **§8.3 用例重写**（Grok P3-3）——见
   §2.1/§5.3/§8.3。

---

## 1. 现状盘点（承 v5；本轮新增两条代码实锤）

承 v3/v4/v5 已核验项（分区表/回滚开关/TWDT 框架默认/`A7682E_init`
≈8.4s/EPD 序列号 API/Arduino 任务模型）。本轮新增：

- **`done_seq` 无条件更新**（Qwen P2 证据）：`factory.ino:235-241` 的
  `firstPage()/nextPage()` 循环退出后 `disp_flush_done_seq =
  disp_flush_req_seq`（注释 "THIS frame reached the panel" 是假设、
  非检查）；`GxEPD2_EPD.cpp:137-151` `_waitWhileBusy` 超时**有界**
  （`_busy_timeout`，超时打印 "Busy Timeout!" 继续）——故自证门控
  实测的是软件序列完成，不是像素上屏（§5.2 语义声明据此修正）。
- **PenPal 排空先例**（Grok P2 引）：跨页 worker 的存活依赖
  `factory.ino:828-829` 每拍调 `penpal_keyboard_poll()`，其
  `ui_penpal.cpp:1330` **先 drain 再** `if (!s_pp_active) return`；
  `s_pp_inflight` 在 send 返回后才减——OTA 结果通道同构，必须把
  排空点写进合同。

## 2. 清单协议（v6：钉死规范化字节串编码）

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

- **签名对象 = 显式六字段**（`sig` 不在内，它是对该字节串的签名本身）：
  `version`、`url`、`size`、`sha256`、`seq`、`notes` 按此顺序各值后接
  一个 `\n`。
- **编码钉死**（Grok P3-1）：`size`/`seq` = 十进制 ASCII、无符号、无
  前导零（`std::to_string`）；`sha256` = 64 位**小写** hex（发布端
  `hexdigest()`、设备端 `%02x` 同）；全部 UTF-8、无 BOM、无额外空白。
  示例字节串：
  `"v2.5-260912\nhttps://host/path/firmware.bin\n2100240\n<64 lowercase hex>\n42\nPenPal TTS; voice fixes\n"`。
  §8.6 加互测向量用例：发布脚本与设备端各自按本规范重建同一清单的
  字节串 → 逐字节相等（大小写/前导零分歧即在此暴露）。
- `notes` 含 `\n`/`\r` → 拒绝发布（值拼接按 JSON 字段取，不按行切分）。
- 验签：`mbedtls_mpi` 拆 r/s → `mbedtls_ecdsa_verify`；禁
  `read_signature`。
- **seq 时机**（v4 定、v5 复核）：Check 只比较；`last_seq` 仅
  `Update.end(true)` 后写。
- URL scheme 双查（`OTA_URL` 与签名 `url`，明文宏关闭时拒 `http://`）。

### 2.2 信任根 / 2.3 配置源（承 v4/v5，无变化）

`ota_trust_anchor.h`（tracked，65B 未压缩 SEC1，key_id 仅诊断）；
私钥 gitignored；换根仅 USB。`OTA_URL` 走 `/env.cfg`。

## 3. 模块 `examples/pda2/ota_update.h/.cpp`（v6：指针通道 + 生命周期合同）

```c
void ota_check_async(uint32_t gen);                              /* 网络在 worker */
void ota_start_async(uint32_t gen, ota_manifest_t *snap);        /* 接管快照所有权 */
void ota_result_poll(void);                                      /* factory loop 每拍调用 */
bool ota_precheck_ui(char *err, int err_len);                    /* 仅无网络项 */
const char *ota_current_version(void);
```

### 3.1 结果通道 = 指针传递（Codex P1——v5 的值传递语义不成立）

- 队列 `xQueueCreate(1, sizeof(ota_result_t *))`，**只运指针**；
  worker `new ota_result_t{...}`（内含 manifest/字符串等堆成员）→
  `xQueueOverwrite(&p)`（深度 1 + overwrite = 永不阻塞；被覆盖的旧
  指针由"每 tick 排空"兜底，泄漏窗口一个结果结构，见 §3.3）→ UI
  `delete` **恰一次**（成功消费 / stale / 离页 / 覆盖丢失四类路径的
  delete 纪律 = 契约新增行）。
- **禁止**把含 `std::string` 的结果/manifest **按值**入队（FreeRTOS
  字节复制 → worker 栈对象析构后 UI 副本持悬空指针——Codex P1 反例）。
- 静态断言：`static_assert(std::is_trivially_copyable_v<ota_task_req_t>)`
  只用于请求快照中的 POD 部分；结果/manifest 走指针，不复制。

### 3.2 快照所有权链（v5 链 + v6 指针语义落实）

```
Check worker：new ota_check_result_t{gen, ok, manifest, err}
              → xQueueOverwrite(&p) →【worker 出口：减 inflight、（成功时）
              不持锁】
ota_result_poll：gen 匹配且屏活 → UI 接管指针（CONFIRM_WAIT 期 UI 持有）
                ：不匹配/离屏 → delete（丢弃）
用户确认    ：ota_start_async(gen, manifest_ptr)——**所有权移交** Update
              任务（launch-time 交接，无复制）→ 任务退出 delete
```

- 全程同一堆对象从 worker → UI → Update 任务移交，无共享可变缓冲、
  无按值复制；"确认的包 = 刷入的包"（Codex P1 修复 + Qwen P2-2 链
  闭合）。

### 3.3 生命周期合同（Codex P2 / Grok P2 / Qwen Nit）

- **inflight**：`s_ota_inflight`（atomic，cap 1）在 `xTaskCreate`
  **成功后**递增；**失败 → 立即 delete 快照 + 报错返回，不递增**；
  递减在 worker **唯一出口**（成功 end / 失败 abort / 取消 abort /
  超时四路汇合），与 `s_ota_hw_lock` 释放**同点**；10min 绝对 deadline
  保证 worker 终将退出（Qwen Nit）。inflight 未归零 → 新 Check/Update
  拒绝（"previous OTA op closing"）。
- **结果发送**：`xQueueOverwrite`（不阻塞）——"worker 卡在满队列发送
  → inflight 永不清零"结构上不可能（Codex P2 反例拆除）。
- **排空点**：`factory.ino` loop() **无条件** `ota_result_poll()`——
  先排空、后屏活跃判断（Grok P2；PenPal `penpal_keyboard_poll`
  drain-then-active 同款，`ui_penpal.cpp:1330`、`factory.ino:828-829`）。
- **离屏路径澄清**（Grok P3-3）：下载/Check 阶段覆盖层**吞键盘**且
  **全屏吸收触摸**（v6 新增，见 §4）→ 用户操作上**不可能**离页；
  无条件排空是纵深防御（覆盖 contract §2.8 destroy 等非用户路径）。

### 3.4 TLS/流式/超时（承 v4/v5 已闭合项）

专用 `setCACertBundle`；明文宏 + scheme 分支；流式
`HTTPClient → Update.write` + 增量 SHA-256；读空闲 45s + 绝对 deadline
10min（任务本地）；size>空槽拒、`Content-Length` 强制；失败
`Update.abort()` 不 `end()`；快照进任务不复拉清单。

## 4. UI 与全局互斥（v6：覆盖层全屏吸收触摸）

- SCREEN2_2（枚举追加末尾 + 注释）：Check（worker 等待层）→ 清单展示
  → 确认 → 下载覆盖层 → 完成/失败。
- **覆盖层 = 全屏透明吸收容器** + 居中信息框（v6 新增）：下载/Check
  阶段**键盘与触摸全部被吸收**（此前 waitbox 220×130 非全屏——waitbox
  外触摸可打到底层控件，Grok P3-3 指出的矛盾根源）；Check 阶段含
  Cancel 按钮（令牌 + gen+1）；下载阶段无按钮（"do not power off"）。
- 低电互斥（`lock || busy`）+ 10min 安全阀（Qwen P3-1 场景边界：
  抑制仅一个 deadline 窗口，超窗强制放行关机——宁打断可恢复写入，
  不深放电）；Sleep 入口 busy 拒绝；`WiFi.setSleep(false)` 成对恢复；
  电量 `<30%` 拒 / gauge 无效看 `get_input()`；池水位观测点。

## 5. 回滚与自证（v5 骨架 + v6 三处修正）

### 5.1 机制事实（承前，本轮新增 §1 两条代码实锤；2026-09-16 真机补三条）

2026-09-16 两台真机核实（issue_list §22 事故顺带）：

1. Arduino 核心 sdkconfig 确认 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`、
   `CONFIG_APP_ROLLBACK_ENABLE=y`——回滚机制真实在位，`verifyRollbackLater()`
   override 不是空操作。
2. 两台机 otadata 均 seq=1 / `ESP_OTA_IMG_VALID`（`71c09e7` 起每次开机
   initArduino 自动标 valid）；VALID 态下 override 为空操作，真机验证
   `095e41a`（override 在版）启动无影响。
3. **PENDING_VERIFY 真实路径未覆盖**：OTA 写入→重启→窗口内自证/超时的
   状态链仍待 §8 回滚矩阵真测（机 #1 先行）。

### 5.2 自证窗口与喂狗合同（v5 合同 + Codex P3 修正）

```cpp
/* 窗口开（setup() 首行附近；运行上下文 = loopTask） */
esp_err_t e = esp_task_wdt_init(T, true);      /* T 见 §5.3；panic 使能 */
/* 检查 e；失败 = 记日志 + 实现期阻断（窗口无层 2 兜底，禁止带病继续） */
e = esp_task_wdt_add(NULL);                    /* NULL = 当前任务 */
if (e == ESP_OK) s_boot_wdt_subscribed = true; /* 仅成功后置守卫（Codex P3）——
                                                * 失败保持 false，喂狗点全部跳过 */

/* 具名喂狗点（五处；每处先查 s_boot_wdt_subscribed）：
 *   A7682E_init  testAT 重试循环每轮
 *   GPS_Recovery/getAck 每轮
 *   EPD nextPage 循环每轮 nextPage() 之前（库内 _waitWhileBusy 不可喂——
 *                该库内等待本身就是最长间隔，见 §5.3）
 *   pcm5102a_init 写 WAV 每块
 *   sd_care_init SD 挂载/读写探测处（Grok P3-2：v5 漏列，v6 补） */

/* 自证点（主循环首帧 EPD 软件序列完成后）：
 *   取号：首帧整刷发起处 const uint32_t seq = ui_disp_full_refr_seq();
 *         （副作用：触发整刷 + req_seq++——只调这一次，存变量）
 *   ui_disp_flush_done_seq() >= seq 达成 →
 *   esp_ota_mark_app_valid_cancel_rollback();
 *   esp_task_wdt_delete(NULL);
 *   esp_task_wdt_init(5, true);                    /* 恢复框架默认 */
 *   s_boot_wdt_subscribed = false; */

/* 语义声明（v6 按 Qwen P2 修正——替换 v5 自评③/§11 的假声明）：
 * 自证门控实测的是"LVGL→EPD 软件刷新序列完成"（done_seq 在 nextPage
 * 循环退出后无条件更新；GxEPD2 _waitWhileBusy 有界超时），它捕获
 * setup/loop 首帧前的挂死与 LVGL 崩溃，但【不证明像素上屏】。屏坏
 * （BUSY 卡死到库超时）→ done_seq 照常更新 → 固件【会自证、不回滚】
 * ——可接受：回滚修不好坏屏，层 3 USB 兜底。此为设计决策，非已验证
 * 的面板健康检查。 */
```

### 5.3 校准与 T（v5 骨架 + Qwen P3-1 / Grok P3-2 修正）

- **T 约束最长喂狗间隔**。间隔清单（v6 补全）：
  `A7682E_init` testAT ~1s/轮；`getAck` ~800ms/轮；EPD 段 = 库内
  `_waitWhileBusy` 上限 `_busy_timeout`（**不可插喂狗**——实现期从
  GxEPD2 构造参数实测，Qwen P3-1）；`pcm5102a` 写 WAV ~块级毫秒；
  **SD 段**：`sd_care_init` 挂载/探测处插喂狗后，慢卡间隔同样被段内
  封顶（Grok P3-2：v5 误称"已封顶"却漏列，v6 补插点）。
- **T = max(实测最长间隔， 含 `_busy_timeout`) × 2`，下限 30s**；
  两台机各测，数据入 §7.3 基线。
- 喂狗完备性审计：setup/loop 中任何 >T/4 的新增阻塞段必须同步插喂狗
  点（§10）。

### 5.4 三层模型（承前）

| 层 | 场景 | 结果 |
|---|---|---|
| 1 | 下载中断/坏包/验签失败/取消/低电安全阀 | otadata 不翻转，重启回上一张已验证槽 |
| 2 | 新固件自证前挂死（任意形态） | task WDT（T 秒）复位 → 下次 boot 自动回滚 |
| 3 | 自证后挂死（含屏坏自证）/ 全挂 | 人工复位或 USB（§7.2） |

## 6. 契约登记（v6：并入指针通道 + 生命周期）

`async_ipc_contract.md` 同批新增：消费者 Settings(SCREEN2_2)；worker
ota_check/ota_start；**结果通道 = 指针**（worker new / `xQueueOverwrite`
/ `ota_result_poll` 每拍排空 / UI delete 恰一次——含 stale/离屏/覆盖
丢失路径）；**单飞** `s_ota_inflight`（cap 1，创建失败即回滚，唯一出
口递减）；快照所有权链（§3.2）；取消 = 令牌 + gen+1 + busy=false；
超时 = 读空闲 45s + 绝对 10min（任务本地）；例外：栈 16KB、任务跨页
面存活、进度走原子轮询。

## 7. 发布流程与受控基线（承 v4/v5，无变化）

发布步骤（`pio run` → `scripts/ota_sign.py` 六字段签名 → 上传 → 设备
Settings 流程）；USB 回退（先读 otadata → `erase_region 0xE000 0x2000`
→ 坏槽分块重写）；`docs/ota-baseline.md` 基线表（v6 增列：GxEPD2
`_busy_timeout` 实测值、SD 段间隔）。

## 8. 验证计划（v6：§8.3 重写 + 排空/生命周期用例）

> **刷写纪律（2026-09-16 起）**：本节所有真机用例的固件写入一律走
> `scripts/flash_verified.py`（每块强制 Hash verified + 失败即中止 +
> `--readback` 终检）——手写 esptool 分块曾因 USB CDC 掉口漏写一块，
> 两台机 bootloader 哈希拒绝"变砖"，详见 issue_list §22。

1. 回滚矩阵（承 v4/v5：a 忙等 / b 让出型 / c 自证点前 / d 自证点后
   不回滚（边界）/ e drv 失败继续；每例记录 reset 来源与状态链）。
2. 健康固件正向：两台机自证成功、time-to-valid、喂狗间隔打点 ≥2×
   核对、`_busy_timeout` 实测。
3. **覆盖层吸收**（v6 重写 §8.3——Grok P3-3）：下载中按 Back/任意键
   → 无 UI 副作用；触摸 waitbox 外区域 → 无反应（全屏吸收）；任务
   继续。（原"离屏重入"用例因离屏路径已被吸收容器关闭而作废；离屏
   排空改由下条 SIM 覆盖。）
4. **单飞/排空**（SIM + 代码走查）：Check 在飞时 destroy 路径推演 →
   poll 照常排空、inflight 归零、无指针泄漏；第二 Check 拒绝路径的
   返回值与文案核对。
5. 签名/完整性（承 v5 全套 + 编码互测向量：两端重建字节串逐字节相等；
   大写 hex / 前导零 → 拒）。
6. 传输：断网 45s → abort + 报错 → 同 seq 重试成功；Content-Length
   强制；404 拒绝。
7. 低电安全阀：下载中触发低电锁存 → 不关机；超窗 → 强制放行。
8. 按键吞没 + 池水位；双机各过一遍（机 #1 先行）。

## 9. 实施顺序（承 v5）

0. 校准打点（含 `_busy_timeout`、SD 段）→ 定 T、填 §7.3；
1. 回滚真测矩阵——先于首次真实 OTA；
2. commit 组 1：签名/发布脚本 + `ota_trust_anchor.h` + env 模板；
3. commit 组 2：`ota_update` 模块（指针通道 + inflight + 排空）+ 契约行；
4. commit 组 3：SCREEN2_2 UI + 全屏覆盖层 + 全局互斥 + 安全阀；
5. commit 组 4：`verifyRollbackLater` + WDT 窗口 + 喂狗点 + EPD 门控
   + 文档同步。

## 10. 实现合同清单（v6 增补加粗）

v4/v5 清单承前（快照不复拉；size>空槽拒；`Content-Length` 强制；失败
`abort()`；枚举追加末尾+注释；池水位；明文宏 plain client 且双查；
45s+10min 任务本地；低电 `lock||busy`+安全阀；`WiFi.setSleep` 成对；
P1363+`ecdsa_verify`；notes 无换行；换根仅 USB；last_seq 仅 end 后
写；TWDT 句柄 NULL；`s_boot_wdt_subscribed` 守卫；取号一次存变量；
`_busy_timeout` 实测；SD 段插喂狗）。**v6 新增**：**结果队列只运指
针（worker new / UI delete 恰一次 / 全路径覆盖）**；**`factory.ino`
loop 无条件 `ota_result_poll()`**；**inflight 创建失败回滚 + 唯一出口
递减**；**覆盖层全屏吸收容器**；**新增 setup/loop 长阻塞段必须同步插
喂狗点**。

## 11. 已裁决事项（v6 修正一条假声明）

ECDSA P-256（mbedtls 2.28 无 EdDSA、避免 libsodium）；手动触发；字符
串版本不等；自证不绑 WiFi；写槽不用户中止（令牌块边界）；`SCREEN2_2_ID`
追加末尾。**修正**（Qwen P2）：自证语义 = 首个 FULL flush **软件序列
完成**，不证明像素上屏；屏坏固件**会自证、不回滚**（可接受，层 3 兜
底）——v5 把"屏坏回滚"误列为已裁决事实，本轮撤销并按代码现实改正。

## 12. v4→v5→v6 遗留闭合核对

| 来源 | 条目 | v6 状态 |
|---|---|---|
| Codex v5 P1 | 队列值传递所有权不成立 | §3.1 指针通道 **闭合** |
| Codex v5 P2 | 创建失败/满队列/离屏释放路径 | §3.3 **闭合**（overwrite + 唯一出口 + 每拍排空） |
| Codex v5 P3 | add 失败守卫矛盾 | §5.2 **闭合**（仅 ESP_OK 置位） |
| Grok v5 P2 | 离屏排空点 | §3.3/§3.1 **闭合**（无条件 poll + overwrite） |
| Grok v5 P3-1 | 字节串编码 | §2.1 **闭合**（十进制/小写 hex/UTF-8 + 互测用例） |
| Grok v5 P3-2 | SD 段喂狗/虚标 | §5.3/§5.2 **闭合**（补插点 + 删虚标） |
| Grok v5 P3-3 | §8.3 vs 吞键矛盾 | §4/§8.3 **闭合**（全屏吸收 + 用例重写） |
| Qwen v5 P2 | 屏坏回滚假声明 | §5.2/§11 **闭合**（语义改正） |
| Qwen v5 P3-1 | EPD 库内等待间隔 | §5.3 **闭合**（T ≥ `_busy_timeout`，实现期实测） |
| Qwen v5 Nit | inflight 全退出路径 | §3.3 **闭合**（唯一出口 + deadline 保证） |

## 13. v5→v6 修订记录

| # | 处置（§节） | 来源 |
|---|---|---|
| 1 | §3.1 结果通道改指针传递（new/overwrite/delete 恰一次），禁含 string 结构按值入队 | Codex P1 |
| 2 | §3.3/§3.4 `factory.ino` loop 无条件 `ota_result_poll()`；overwrite 永不阻塞；inflight 唯一出口递减 | Grok P2 / Codex P2 / Qwen Nit |
| 3 | §3.3 inflight 创建失败回滚、锁同点释放 | Codex P2 |
| 4 | §5.2 add 失败仅 ESP_OK 置守卫（伪代码与失败分支一致） | Codex P3 |
| 5 | §2.1 规范化字节串编码钉死（十进制无前导零/小写 hex/UTF-8 + 互测用例） | Grok P3-1 |
| 6 | §5.3 EPD `_busy_timeout` = 最长不可喂狗间隔，T 下界纳入；SD 段补喂狗点 | Qwen P3-1 / Grok P3-2 |
| 7 | §4 覆盖层全屏吸收触摸 + §8.3 用例重写（离屏路径关闭，排空为纵深防御） | Grok P3-3 |
| 8 | §5.2/§11 自证语义声明修正（撤销"屏坏回滚"假决策，改"软件序列完成、屏坏不自证回滚可接受"） | Qwen P2 |
