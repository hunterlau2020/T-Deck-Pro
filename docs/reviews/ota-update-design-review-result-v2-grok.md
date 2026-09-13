# 设计评审结果：OTA 固件远程升级 v2（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)（文内标明 v2 修订稿）
- **对照 v1 结果**：[ota-update-design-review-result-grok.md](ota-update-design-review-result-grok.md)（**C**，3×P1 挡开工）
- **评审结论**：**A 全量接受**。v1 三条 P1 均已闭合，架构可据以开工。下列 P2
  必须写进实现（或补一小段设计约束），不构成再一轮 v3 的挡板。
- **算法裁定**：接受 ECDSA P-256，**不要求**捆绑 Ed25519。

---

## v1 P1 闭合核对

| v1 条目 | v2 处置 | 状态 |
|---|---|---|
| P1-1 `initArduino()` 自动自证使层 2 空转 | §5 覆盖 `extern "C" bool verifyRollbackLater() { return true; }`；自证改 `lv_async_call` 于 loop 首个 `lv_timer_handler`；`drv.begin()` 的 `while(1)` 改为记日志继续；层 2 文案改为「上一张已验证槽」 | **闭合** |
| P1-2 继承 Trust / 复用 `http_get` | §3.1 直接 `setCACertBundle(CA_BUNDLE_MOZILLA)`，不走 `http_apply_tls`；`OTA_ALLOW_PLAINTEXT` 默认 0 且 env.cfg 打不开，清单与 bin 一起挡 | **闭合** |
| P1-3 HTTPUpdate 超时与写 flash 不受控 | §3.2 弃用 HTTPUpdate，自管流式 `HTTPClient` + `Update.write` + 增量 SHA-256；写槽期无取消按钮 | **闭合**（残余见 P2-3/P2-4） |

四方其余承重项：清单四字段必填 + 签名（Codex/Qwen P1）、契约行（Qwen P2）、分区/bootloader 受控化（Codex P1）、回滚真测前置（Grok/Claude/Qwen）均已写入。方向与 v1「手动 + HTTPS + 三层失败」一致。

---

## 算法裁定：ECDSA P-256，不要 Ed25519

Arduino-ESP32 2.0.14 qio_qspi `sdkconfig.h` 已核：`CONFIG_MBEDTLS_ECDSA_C=1`、
`CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=1`。`CURVE25519` 是 ECDH，不是
Ed25519 签名。mbedtls 2.28 无现成 Ed25519 验签，捆绑约 30KB 无收益。

Codex v1 要求的安全性质是「设备固化公钥 + 签名覆盖 version/url/size/sha256」，
算法可替换。P-256 满足该性质。**请勿为对齐 Codex 原文再引入第三方 Ed25519。**

---

## Findings（均为 P2，开工时落地）

### P2-1：清单 URL 配置源在 v2 消失

- **位置**：v1 §2 的 `/env.cfg` `OTA_URL`；v2 全文无配置源。`ota_fetch_manifest`
  / Check 按钮未说明 URL 从哪来。
- **证据**：对 `docs/ota-update-design.md` 检索 `OTA_URL` / `env.cfg` 仅出现在
  「明文开关不能被 env.cfg 打开」，没有读取端。
- **最小修复**：恢复 v1：`env.cfg` `OTA_URL=`（`env_get`，value ≤160，
  `ENV_MAX_ENTRIES=12` 够用）；缺省或非 `https://`（宏关闭时）拒绝。
  `env.cfg.example` 去掉过时的「Max 8 / 95 chars」。可选
  `config_keys.h` 编译期默认，仍低于 env。

### P2-2：公钥被当成秘密；签名编码未钉死

- **位置**：§2.1「真实公钥进 gitignored `config_keys.h`」；`sig` = 64 字节 r\|\|s
  base64。
- **证据**：公钥不是秘密。只放 gitignored 头会导致：clone 后空公钥则 Check
  全失败；CI/发布机与设备公钥不一致。`mbedtls_ecdsa_read_signature` 吃的是
  ASN.1 DER，不是 raw r\|\|s；对不上则所有升级验签失败。
- **最小修复**：
  1. 公钥进 **tracked** 头（例如 `examples/pda2/ota_pubkey.h`，或
     `config_keys.h.example` 里未注释的测试向量 + 发布用 tracked 常量）。
     私钥才 gitignore。换公钥只能 USB 刷，写进 §7。
  2. 钉死：**IEEE P1363 大端 r\|\|s 各 32 字节**（共 64），base64 无 PEM 头。
     设备用 `mbedtls_mpi` 拆 r/s 调 `mbedtls_ecdsa_verify`，**禁止**
     `mbedtls_ecdsa_read_signature`。发布脚本必须产出同一格式，并在 §8.3
     加一条「故意用 DER 签名 → 拒绝」。

### P2-3：契约超时与 2.1MB 下载冲突

- **位置**：§6「超时 = HTTPClient 45s + 总体 deadline 由用户重启兜底」。
- **证据**：`async_ipc_contract.md` 规则 10 要求 UI **绝对 deadline** 覆盖
  NTP+连接+读取全程。45s 作为读空闲超时可以（流式持续有数据则总时长可超过
  45s）；作为总时限则 2.1MB 在慢 WiFi 上必失败。「用户重启兜底」不是绝对
  deadline，waitbox 又无取消，会一直停在 *do not power off*。
- **最小修复**：HTTP 读空闲 45s；绝对 deadline **10 分钟**（或按 size/最低速率
  计算）；超时与其它任务一样 gen+1 + 提示失败 + 若已 `Update.begin` 则
  `Update.abort()`。§6 登记为契约例外（深度 1、栈 16KB）时把这条一并写上。

### P2-4：写 flash 期间未抑制关机 / 未吞键 / 未关 WiFi sleep

- **位置**：§4 只写无取消按钮；全局 `low_voltage_timer`（`ui_deckpro.cpp:35`）
  仍会在任意屏倒计时 `ui_shutdown_on()`；Sleep 屏 `esp_deep_sleep_start()`。
- **最小修复**：OTA busy 期间：waitbox 吞全部按键（照 `chat_waitbox`）；暂停
  低电关机定时器；禁止进入 Sleep；`WiFi.setSleep(false)`。结束（成功/失败/
  超时）一律恢复。电量计无效时不要用 SOC=0 误拦，改为要求
  `ui_battery_27220_get_input()`。

### P2-5：§8.1 回滚用例 1 的期望与物理矛盾

- **位置**：§8「刷带 `verifyRollbackLater` 覆盖但 **setup() 死循环（无 WDT
  挂载前）** → 必须观察到 bootloader 自动回滚」。
- **证据**：不重启则 bootloader 不跑。`while(1) delay(10)` 让出 CPU，idle
  WDT 被喂，loop WDT 还没挂上 → **卡死、不回滚**（正是 v1 拆 `drv.begin`
  雷的原因）。按该步骤测会把设备留在 pending 新固件上，只能 USB，并可能误判
  「层 2 坏了」。
- **最小修复**：拆成两条：
  1. setup **忙等不 yield**（`while(1){}`）→ 期望 idle WDT 复位后回滚；
  2. 主循环在自证点之前死循环 → 回滚；自证点之后死循环 → **不**回滚（登记为
     已知边界）。`while(1) delay` 列为「不回滚、层 3 USB」，拆雷后不应再出现。

### P2-6：实现合同洞（短，同批写进代码即可）

1. Check 验签后把 `ota_manifest_t`（url/size/sha256）**快照**进任务，Update
   不要再拉清单（避免确认后被换包；签名私钥失守除外）。
2. `Update.begin` 前拒绝 size > 空闲 OTA 槽；`Content-Length` 与清单 size
   不一致则不开写。发布静态服务必须给 `Content-Length`（不要 chunked）。
3. size/sha 失败走 `Update.abort()`，不得 `end()`。
4. `SCREEN2_2_ID` **追加在枚举末尾**，不要插在 `SCREEN2_1` 与 `SCREEN3` 之间。
5. USB 回退写死「擦 otadata @ `0xE000` + 写 app0 @ `0x10000`」——OTA 后运行槽
   可能是 app1，只写 `firmware.bin` 不够。
6. 新屏 `create` / 进度覆盖层打一行池水位（`721e04a` 教训）。
7. 按模块拆提交（ota_update + env 模板；UI；`verifyRollbackLater`/自证/拆
   `while(1)` + 文档），不要 §9 的「一次提交」。
8. 修正悬空引用「§2.4 行 6」（文中无 §2.4）。
9. 明文宏打开时清单/bin 走 **plain `WiFiClient`**，不是 `WiFiClientSecure`。

---

## 已通过项

- 专用 TLS、强制 CA、明文仅编译期宏。
- 清单签名覆盖四字段；notes 不签名可接受（展示用）。
- 弃用 HTTPUpdate，边下边哈希，先校验再 `Update.end(true)`。
- 取消 = gen+1，写槽不可中止，waitbox 无取消——与契约规则 9 / PenPal SEND 同款。
- `verifyRollbackLater` + 自证两行必须同时保留；自证不绑 WiFi。
- 手动触发、字符串版本不等、分区/bootloader 受控化方向。
- 发布脚本从 `utilities.h` 读版本写入清单（Qwen P3）。

## 验证说明

- 对照 v2 全文与 v1 Grok/Codex/Qwen/Claude 结果、`factory.ino` setup/loop、
  `http_apply_tls`、`async_ipc_contract.md` 规则 6/7/10、`env_secrets.cpp`
  容量、`ui_deckpro.cpp` 低电定时器、2.0.14 qio_qspi `sdkconfig.h` 的
  ECDSA/P-256。
- 无 `pio`、无真机。本文只评设计。

## 审批意见

- [x] A. **全量接受**（P2 作实现约束，不挡开工）
- [ ] B. 退回修订
- [ ] C. 部分接受

**接受理由**：v1 挡开工的三条 P1 在 v2 都有对得上源码的修复；P-256 是本框架
上正确的签名选择。剩下是配置源遗漏、验签编码、超时/关机、回滚用例写法——
实现第一批就能收口，不必再开 v3 设计轮。

**开工顺序**：先 §8.1 修正后的回滚真测（忙等 WDT），再首次真实 OTA。P2-1/2
（URL 配置 + 公钥/编码）与模块代码同批落地。
