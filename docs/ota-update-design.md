# 设计评审申请书：OTA 固件远程升级（v4 修订稿）

- **申请人**：Claude（pda2）
- **申请日期**：2026-09-13
- **关联分支**：`HD-V2-250915`
- **评审要求**（对全体评审方）：按 [`docs/review_guide.md`](review_guide.md)
  执行——L1 DOC-ALIGNED（§2.1）+ A/B/C；findings 按 §4.2 定级留坐标；
  DOC/SPEC/CODE/SIM 为本档证据（§3.1）；结果写入
  `docs/reviews/ota-update-design-review-result-<本稿锚>-<reviewer>.md`
  （锚 = 本稿 commit SHA，见 git log 最新一条；**新建文件绝不覆盖**）；
  报告含对称三格与 §9【D】条目自审。
- **状态**：**v4 修订稿，送复审**——v3（`45d11cf`）复审：Grok **C** /
  Codex **C** / Qwen **C** / Claude **A**。三方独立收敛到同一 P1
  （WDT 自证窗口数值与现有 `setup()` 阻塞 profile 冲突），v4 重写该
  机制并处置全部 7 P2 + 5 P3/Nit，对照见文末 §13。
- **开发者自评（§7.1 强制）**：
  - **最有把握**：seq 提交时机改到 `Update.end(true)` 之后（三方同判，
    修法唯一）；公钥格式钉死 65B 未压缩 SEC1（SEC1 规范唯一解）；
    Check 移入 worker（与现有五个 HTTPS 消费者同构）。
  - **最没把握（≤3）**：① 校准后的 WDT 窗口 T 是否在两台机的**所有**
    启动场景（含 SD 卡慢卡、GPS 冷启动重试）下都有 ≥2× 边际——依赖
    实现期打点数据，设计只能定"先测后填"程序；② `esp_task_wdt_init(T,
    true)` 在 Arduino 2.0.14 的 re-init 语义（是否影响已订阅的 idle
    任务超时）——Grok/Codex 对 API 行为的描述一致但均无真机复跑；
    ③ EPD 首帧序列号作自证门控在**首帧刷新失败**（not busy 永等）时
    是否仍能靠 WDT 兜底——依赖 T 标定覆盖该路径。
  - **知道没测到的**：一切真机项（§8 全部）；`esp_task_wdt_*` 返回值
    行为按 IDF 文档推演。

---

## 0. v4 变化点（相对 v3）

1. **自证窗口重设计**（Grok P1-1 / Qwen P1-1 / Claude P2-A/P2-B /
   Codex P1-B——四方合围）：废除拍脑袋的"8s"；改为**先校准后定值**
   （两机打点 → T = max(整段实测)×2，下限 30s）+ `esp_task_wdt_init(T,
   panic=true)` 显式 re-init + **长阻塞函数内部**喂狗（A7682E/GPS/EPD/
   PCM 四处具名）+ 自证后恢复框架默认 5s。
2. **自证点改 EPD 首帧完成序列号**（Codex P1-A）：`lv_async_call` 触发
   ≠ 首帧真正上屏；改为 `ui_disp_full_refr_seq()` 取号 → 自证点核对
   `ui_disp_flush_done_seq()` 达标（两 API 已核实存在于
   `ui_deckpro_port.h:42-43`、`factory.ino:864/:241`）。
3. **seq 提交时机**（Grok P2-1 / Codex P2 / Qwen P2-1 三方同判）：
   `last_seq` **只在 `Update.end(true)` 成功后**写 NVS；Check 阶段只
   比较；失败/取消/掉电保持原值——同一版本可重试。
4. **公钥格式钉死**（Grok P2-2 / Codex P2）：65B 未压缩 SEC1
   （`0x04 || Qx || Qy`）唯一格式；压缩点 33B / 无前缀 64B 一律拒绝。
5. **Check 移入 worker**（Grok P2-3）：UI 线程零网络；Check/Update 共用
   任务状态机。
6. **进度与结果通道分离**（Grok P2-4 / Qwen P3-2）：原子百分比 + UI
   定时器轮询；队列只运最终结果；写循环内禁止 `portMAX_DELAY` 发送。
7. **低电互斥加安全阀**（Qwen P3-1）：抑制有界（一个绝对 deadline 窗
   口），临界电量强制放行关机。
8. 其余 P3/Nit 全收（§13 行 8–17）。

---

## 1. 现状盘点（v3 基础上新增的已核实事实）

承 v3 §1（分区表/回滚开关/体积/电量接口），v3 复审新增实锤：

- **TWDT 框架默认**（Grok，sdkconfig 2.0.14 esp32s3）：
  `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`、`CONFIG_ESP_TASK_WDT_PANIC=y`、
  idle CPU0 已订阅；**loopTask 默认未订阅**（`main.cpp`
  `loopTaskWDTEnabled=false`）；`delay()` 只喂 idle 不喂 loopTask。
  `CONFIG_ESP_TASK_WDT_PANIC=y` 即"超时 → panic → 复位"成立（Claude
  P2-A 要求的验证项，已在框架 sdkconfig 确认；仍入 §7.3 基线复核）。
- **现有 `setup()` 阻塞 profile**（Grok 静态加总，SIM）：
  `A7682E_init` 失败路径 ≈6.1s（机 #2 无模组**每次开机必走满**）、
  `GPS_Recovery` ≈3.2–6.4s、EPD 全刷 1–2s（两处）、`pcm5102a_init`
  写 WAV——单段即超 5s/8s，函数内部无喂狗点。
- **EPD 首帧完成序列号**：`disp_full_refr_seq()` / `ui_disp_flush_
  done_seq()` 已存在（`ui_deckpro_port.h:42-43`；`factory.ino:864`
  实现、`:241` 在 `flush_timer_cb→nextPage()` 后写入）——首帧完成
  判定不必新造机制。
- **mbedtls 2.28.4 无 EdDSA**（Qwen 核 `version.h`；框架另有 libsodium
  提供 Ed25519——采用 mbedtls ECDSA 正是避免引入 libsodium 签名依赖，
  Qwen Nit-1 措辞采纳）。

## 2. 清单协议（v4：格式钉死 + seq 时机修正）

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

- **签名对象** = 七字段按序拼接的规范化字节串（构建方式：**取 JSON 各
  字段值直接拼接**，每字段后一个 `\n`——不是对 JSON 文本按行切分）；
  **`notes` 含 `\n`/`\r` → 拒绝发布**（Grok P3-2）。`sig` = ECDSA
  P-256/SHA-256，IEEE P1363 大端 `r||s` 各 32 字节共 64B，base64 无
  PEM 头（Grok P2-2 钉死）。七字段全必填，缺一/验签失败/编码不符 → 拒绝。
- 设备验签：`mbedtls_mpi` 拆 r/s → `mbedtls_ecdsa_verify`；**禁**
  `mbedtls_ecdsa_read_signature`（ASN.1 DER 路径）。
- **seq 时机**（v4 核心 P2 修正）：
  - Check/验签阶段：**只比较** `seq > last_seq`，不写 NVS；
  - `last_seq` **仅在 `Update.end(true)` 成功后**写入；
  - 失败/取消/超时/掉电 → `last_seq` 保持原值 → **同一版本可重试**
    （Grok 反例"42 卡死重试"消除）；回滚后重装同一坏包被拒是期望行为
    （换包须 seq+1 或 `OTA_ALLOW_DOWNGRADE` 宏，默认 0）。
- **URL scheme 双查**（Grok P3-1）：`OTA_URL` 与清单内**已签名的
  `url`** 都在 `OTA_ALLOW_PLAINTEXT=0` 时拒绝 `http://`——签了名的
  明文 bin URL 同样挡（防发布脚本失手签出明文链）。

### 2.2 信任根（v4：格式唯一化）

- `examples/pda2/ota_trust_anchor.h`（tracked）：公钥 = **65B 未压缩
  SEC1**（`0x04 || Qx(32B) || Qy(32B)`，十六进制数组）——**唯一格式，
  禁止压缩点（33B）与无前缀 64B**（Grok/Codex：v3 的"65B 压缩点"自相
  矛盾，SEC1 压缩是 33B）；`key_id`（8 hex）仅诊断用，不进签名；
  编码格式注释写明"设备与发布脚本同用 `mbedtls_ecp_point_read_binary`
  （只认 33/65B）"。§8.3 加拒绝用例：压缩点、64B 无前缀。
- 签名私钥 gitignored；换公钥 = USB 刷机（无远程换根，§7.3）。

### 2.3 配置源（承 v3）

`OTA_URL` 从 `/env.cfg`（`env_get`，≤160）；宏关闭时非 https 的
OTA_URL **与签名 url** 都拒绝；`env.cfg.example` 模板同步（过时的
"Max 8 / 95 chars"注释已删）。

## 3. 模块 `examples/pda2/ota_update.h/.cpp`（v4：全异步 + 通道分离）

```c
/* 全部网络操作只在 worker 任务内；UI 线程零 HTTPS（Grok P2-3） */
void ota_check_async(uint32_t gen);            /* 清单+验签 → 结果队列 */
void ota_start_async(uint32_t gen);            /* 下载+写槽 → 结果队列 */
bool ota_precheck_ui(char *err, int err_len);  /* 仅无网络项：电量/URL 在场 */
const char *ota_current_version(void);
```

- **任务状态机**：IDLE → CHECKING → CONFIRM_WAIT（UI 确认，无任务）→
  DOWNLOADING → DONE/FAILED；Check 与 Update 共用同一 worker 任务与
  `s_ota_busy`（同一时刻只有一个 OTA 网络操作）；`ota_fetch_manifest`/
  `ota_precheck` 网络部分只作任务内函数，不是 UI 可直接调的同步 API。
- **结果通道**：队列深度 1 **只运最终结果** `ota_result_t{gen, ok, err}`
  （Qwen P3-2 / Grok P2-4：进度不再走队列）。
- **进度通道**：`static atomic_int s_ota_percent` + `s_ota_phase`；UI
  定时器每秒轮询刷新覆盖层（EPD 每 10% 才真正刷）；**写循环内禁止
  `xQueueSend`**（更无 `portMAX_DELAY`）；绝对 deadline 用任务本地
  `millis()` 检查，不依赖 UI 消费速度（Grok P2-4 死锁链拆除）。
- **超时**：HTTP 读空闲 45s + 绝对 deadline 10min（任务本地判定）；
  触发 = 失败路径（gen+1 + abort-if-begun + 报错）。
- **快照**：Check 通过后整个 `ota_manifest_t` 复制进任务，Update 阶段
  不复拉清单。
- **TLS/流式/锁**（承 v3 已闭合项）：专用 `setCACertBundle` 不继承
  `tls_insecure`；明文宏走 plain `WiFiClient`（`pp_request` 的
  `is_https` 分支模式）；`s_ota_hw_lock`（CAS）+ 取消令牌三检查点
  （begin 前/每块/end 前）；失败一律 `Update.abort()` 不 `end()`；
  size > 空闲槽拒绝；`Content-Length` 缺失/不符不开写。

## 4. UI 与全局互斥（v4：互斥条件放宽 + 安全阀）

- SCREEN2_2（枚举**追加末尾**，注释注明原因——Qwen Nit-2a）：版本 +
  Check（worker 化，等待层吞按键）→ 清单展示 → Update 二次确认 →
  覆盖层 "Updating... N% / do not power off"（吞全部按键）→ 完成/失败。
- **低电互斥**（v4 修正）：`low_voltage_timer_cb` 在
  **`s_ota_hw_lock || s_ota_busy`** 时跳过 `ui_shutdown_on()`（Grok
  P3-4：锁在 `Update.begin` 前才取，Check/确认阶段用 busy 条件补位，
  防止以后有人把下载 GET 挪到取锁前）。
- **安全阀**（Qwen P3-1）：抑制**有界**——只覆盖一个绝对 deadline 窗口
  （10min）；超窗后无论锁/busy 状态，低电关机放行（宁可打断一次写入
  ——otadata 未翻转仍可恢复，也不深放电损电池）。§8.6 加用例。
- Sleep 入口 busy 时拒绝；`WiFi.setSleep(false)` 进入时设、结束
  （成功/失败/超时/取消）一律恢复。
- 电量前置：`<30%` 拒绝；gauge 无效不用 SOC=0 误拦，看
  `ui_battery_27220_get_input()`（外部供电即放行）。
- 新屏 create 与覆盖层各打一行池水位。

## 5. 回滚与自证（v4：校准定值 + EPD 首帧门控——四方 P1 合围的正面回答）

### 5.1 机制事实（v3 已核实，承前）

OTA 状态机在**下一次重启**才 `PENDING_VERIFY → ABORTED` 回滚；框架
`initArduino()` 在 `setup()` 前自动自证，须覆盖
`extern "C" bool verifyRollbackLater() { return true; }`；TWDT 框架
默认 5s/panic=y；loopTask 默认未订阅、`delay()` 不喂它。

### 5.2 自证窗口：先校准、后定值（Grok P1-1 / Claude P2-B / Qwen P1-1）

- **校准步（实现期第 0 件）**：当前固件两台机各打点——`setup()` 入口、
  每个 `peri_init_*`、`lvgl_init`、`ui_deckpro_entry`、`disp_full_refr`、
  自证点的 `millis()`；串口输出存 `docs/ota-baseline.md`。
- **T 的确定**：`T = max(整段 setup+首帧实测) × 2`，下限 **30s**；
  **禁止拍脑袋初值**（v3 的 8s 被三处静态加总证伪：`A7682E_init` 失败
  路径 6.1s、`GPS_Recovery` 至 6.4s、EPD 全刷各 1–2s——都发生在
  **函数内部**，外部喂狗填不上）。
- **re-init 语义**（Codex P1-B：订阅≠改超时≠保证复位）：窗口开始
  `esp_task_wdt_init(T, true)`（panic 使能，显式）+
  `esp_task_wdt_add(loopTask)`，**检查全部返回值**；效果表述为
  "预期 8s→T 秒复位（§8.1a 待验证）"——不复述为已核实（Claude
  P2-A 措辞采纳；`CONFIG_ESP_TASK_WDT_PANIC=y` 已核、仍入基线复核）。
- **具名喂狗点**（Grok：函数内部阻塞必须函数内喂）：`A7682E_init` 的
  `testAT` 重试循环每轮、`GPS_Recovery`/`getAck` 每轮、EPD
  `nextPage`/BUSY 等待循环每轮、`pcm5102a_init` 写 WAV 每块——各插
  `esp_task_wdt_reset()`。
- **自证点**（Codex P1-A）：**EPD 首帧完成** = `ui_disp_flush_done_seq()
  >= ui_disp_full_refr_seq()`（首帧进屏），不是 `lv_async_call` 触发。
  达标即 `esp_ota_mark_app_valid_cancel_rollback()` +
  `esp_task_wdt_delete(loopTask)` + `esp_task_wdt_init(5, true)` 恢复
  框架默认。自证后挂死 = 冻结需人工复位（已知边界，层 3）。

### 5.3 三层模型（精简版，完整命令在 §7.2）

| 层 | 场景 | 结果 |
|---|---|---|
| 1 | 下载中断/坏包/验签失败/取消/低电安全阀打断 | otadata 不翻转，重启回上一张已验证槽 |
| 2 | 新固件自证前挂死（任意形态） | task WDT（T 秒）复位 → 下次 boot 自动回滚 |
| 3 | 自证后挂死 / 全挂 | 人工复位或 USB（§7.2） |

## 6. 契约登记（v4：超时/队列语义按 Grok P2-3/P2-4 修订）

`async_ipc_contract.md` 同批新增：消费者 Settings(SCREEN2_2)；任务
ota_check/ota_start 共用 worker；结果队列深度 1 **只运结果**；busy
`s_ota_busy`+`gen`（UI 线程）；取消 = 令牌 + gen+1 + busy=false；超时
= 读空闲 45s + **绝对 deadline 10min（任务本地判定）**；进度走原子变
量轮询（**契约例外声明**：高频进度不走队列；栈 16KB；任务可跨页面存
活——硬件锁所致）。

## 7. 发布流程与受控基线

### 7.1 发布步骤

```
pio run -e pda2                     # firmware.bin（版本烘焙自 utilities.h）
python scripts/ota_sign.py          # sha256/size + utilities.h 版本 + 递增 seq
                                    # + 私钥签名 → manifest（notes 无 \n\r）
上传 firmware.bin + manifest 到 OTA_URL 同目录
设备: Settings → Firmware Update → Check → Update → Reboot
```

脚本校验 `ota_trust_anchor.h` 指纹、清单 version == bin 烘焙版本。

### 7.2 USB 回退（可照抄版——Grok P3-3 / Codex P3）

```
# 0) 先读 otadata 确认运行槽（ota_0/app0 或 ota_1/app1）
python esptool.py --port COM5 read_flash 0xE000 0x2000 otadata.bin
# 1) 擦 otadata（8KB = 0x2000；擦后 bootloader 默认回 ota_0，未必是"另一张
#    已验证槽"——若 ota_0 恰是坏镜像，继续第 2 步重写好镜像）
python esptool.py --port COM5 erase_region 0xE000 0x2000
# 2) 目标槽镜像损坏时：分块烧录重写（坑 6 流程；app0@0x10000 / app1@0x650000）
```

### 7.3 受控基线 `docs/ota-baseline.md`（tracked；实现期首件）

承载表（Grok P3-5）：分区表来源与 SHA、bootloader SHA、otadata 偏移
0xE000/0x2000、槽偏移/容量、`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`、
`CONFIG_ESP_TASK_WDT_PANIC=y` + `TIMEOUT_S=5`（Claude P2-A：TWDT 复位
保证的开关证据）、两机读回摘要、`ota_trust_anchor.h` 指纹、**校准打点
数据**（§5.2）。框架升级 = 兼容性破坏事件，重跑 §8。

## 8. 验证计划（v4：加回滚矩阵之外的四条新用例）

1. **回滚矩阵**（每例记录 reset 来源 + pending→aborted→previous-valid）：
   a. setup 忙等 `while(1){}` → T 秒 WDT 复位 → 回滚；
   b. setup 让出型 `while(1) delay(10)` → T 秒 WDT 复位 → 回滚
      （v4 起可恢复——TWDT 不受 delay 影响）；
   c. 主循环自证点前死循环 → 回滚；
   d. 自证点后死循环 → 不回滚（已知边界，登记）；
   e. `drv.begin` 失败 → 记日志继续启动。
2. **健康固件正向**（Grok P1-1 要求）：两台机 OTA 后**必须**自证成功
   不回滚；记录 reset 原因（应无 TWDT）与 time-to-valid；**打点数据 ≥2×
   边际核对**（Claude P2-B）。
3. **硬件锁并发**（Claude P2-C）：下载中途 Back 离屏 → 重进 → 立即
   Update → 第二请求被 `s_ota_hw_lock` 拒绝（串口/UI 可见），原任务
   不受影响、状态正确。
4. 签名/完整性：篡改七字段任一 / DER 签名 / 压缩点公钥 / 64B 无前缀 /
   错公钥 / 缺字段 / seq 回放降级 → 全拒。
5. 传输：断网 45s 空闲超时 → abort + 报错 → **同 seq 立即重试必须成功**
   （P2-1 用例）；Content-Length 缺失/不符 → 不开写；404 → 拒绝。
6. **低电安全阀**：下载中触发低电锁存 → 不关机；**超一个 deadline 窗口
   后仍临界 → 强制关机放行**（Qwen P3-1 用例）；任务结束后恢复正常
   评估。
7. 按键吞没 + 池水位核对。
8. 双机各过一遍（机 #1 先行）。

## 9. 实施顺序（校准步前置）

0. **校准打点**（§5.2）+ 两机 time-to-valid → 定 T、填 §7.3 基线；
1. 回滚真测矩阵（§8.1，临时自毁固件）——先于首次真实 OTA；
2. commit 组 1：签名/发布脚本 + `ota_trust_anchor.h` + env 模板；
3. commit 组 2：`ota_update` 模块（worker 状态机 + 原子进度）+ 契约行；
4. commit 组 3：SCREEN2_2 UI + 全局互斥 + 安全阀；
5. commit 组 4：`verifyRollbackLater` + WDT 窗口 + 具名喂狗 + EPD 首帧
   门控 + 文档同步。

## 10. 实现合同清单（编码逐条核对）

快照进任务；size>空槽拒；`Content-Length` 强制且匹配；失败 `abort()`
不 `end()`；枚举追加末尾+注释；池水位观测点；明文宏 plain client 且
**清单 url 与 OTA_URL 双查**；URL 源 env.cfg；45s 空闲 + 10min 绝对
deadline（任务本地）；进度原子轮询、队列只运结果、写循环禁
`xQueueSend`；低电互斥条件 `lock || busy` + 10min 安全阀；`WiFi.setSleep`
成对；公钥 tracked 65B 未压缩 + DER/压缩点拒绝；P1363 + `ecdsa_verify`；
notes 无换行；换根仅 USB；last_seq 仅 end 后写。

## 11. 已裁决事项

ECDSA P-256（Grok/Claude 裁定，Qwen Nit-1 措辞修正为"mbedtls 2.28 无
EdDSA、避免引入 libsodium"）；手动触发；字符串版本不等；自证不绑
WiFi；写槽不用户中止（令牌块边界生效）；`SCREEN2_2_ID` 追加末尾
（语义位置 Nit-2a 以注释处置）。

## 12. v3→v4 修订记录（逐条对照）

| # | 处置（§节） | 来源 |
|---|---|---|
| 1 | §5.2/§1 自证窗口重设计：废除 8s；先校准（两机打点）→ T=实测×2 下限 30s；`esp_task_wdt_init(T,true)` 显式 re-init + 返回值检查；具名喂狗点四处（A7682E/GPS/EPD/PCM）；自证后恢复 5s | Grok P1-1 / Qwen P1-1 / Claude P2-B / Codex P1-B |
| 2 | §5.2 自证点改 EPD 首帧完成序列号（`ui_disp_flush_done_seq` ≥ `ui_disp_full_refr_seq`），弃 `lv_async_call` 语义 | Codex P1-A |
| 3 | §5.2/§7.3 `CONFIG_ESP_TASK_WDT_PANIC=y` 已核入基线；"复位"表述改"预期（§8.1a 待验证）" | Claude P2-A / Codex P1-B |
| 4 | §2.1 `last_seq` 仅 `Update.end(true)` 后写；Check 只比较；§8.5 同 seq 重试用例 | Grok P2-1 / Codex P2 / Qwen P2-1 |
| 5 | §2.2 公钥钉死 65B 未压缩 SEC1；§8.4 压缩点/64B 拒绝用例 | Grok P2-2 / Codex P2 |
| 6 | §3 Check/Update 共用 worker 状态机；UI 线程零网络（`ota_precheck_ui` 仅无网络项） | Grok P2-3 |
| 7 | §3.4/§6 进度原子轮询、队列只运结果、写循环禁 `xQueueSend`、deadline 任务本地 | Grok P2-4 / Qwen P3-2 |
| 8 | §4.1 互斥条件 `lock \|\| busy`；§4.1 安全阀 10min 后放行关机（深放电保护） | Grok P3-4 / Qwen P3-1 |
| 9 | §2.1 签名 url 双查 http；§2.1 规范串 JSON 值拼接 + notes 禁换行 | Grok P3-1 / Grok P3-2 |
| 10 | §7.2 USB 命令可照抄（`erase_region 0xE000 0x2000`）+ ota_0 默认语义 + 先读 otadata | Grok P3-3 / Codex P3 |
| 11 | §7.3 基线承载表 + `CONFIG_ESP_TASK_WDT_PANIC` 核验行 + 校准数据 | Grok P3-5 / Claude P2-A |
| 12 | §8.3 硬件锁并发用例（离屏重进拒第二请求） | Claude P2-C |
| 13 | §11 措辞："mbedtls 2.28 无 EdDSA（平台经 libsodium 有）——ECDSA 避免引入 libsodium 依赖" | Qwen Nit-1 |
| 14 | §4 枚举追加注释；子节引用改"§5 第 N 项"式（Qwen Nit-2b） | Qwen Nit-2 |

## 13. 申请自评核对（应 Codex v3 轮 meta 意见）

本申请已含 §7.1 强制的"开发者自评"段（头部）与 §2 验证状态表（各节
内嵌"预期/待验证"措辞）；v3 轮缺失即本轮已补。
