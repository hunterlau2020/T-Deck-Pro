# 设计评审申请书：OTA 固件远程升级（v2 修订稿）

- **申请人**：Claude（pda2）
- **申请日期**：2026-09-13
- **关联分支**：`HD-V2-250915`
- **状态**：**v2 修订稿，待复审**——v1（`36e88df`）经四方评审：
  Codex **C** / Grok **C** / Qwen **C** / Claude **A**；本稿逐条处置全部
  P1/P2 后改写。评审结果文件：
  [`ota-update-design-review-result-{codex,grok,qwen,claude}.md`](reviews/)
- **v1→v2 修订总览**：见文末 §11；每条标注处置的评审方与 finding。

---

## 0. 结论先行（v2 变化点）

v1 的"手动触发 + HTTPS + 三层失败模型"方向获四方一致认可；v2 按评审
**收紧五个安全/生命周期缺口**：

1. **TLS 隔离**：OTA 用专用强制 CA 校验通道，绝不继承 AI Config 的
   `tls_insecure` 开关；生产构建拒绝 `http://`（Codex P1 / Grok P1-2 / Qwen P1）
2. **签名清单**：发布端 ECDSA P-256 签名，设备固化公钥验签；不依赖
   "HTTPS = 发布方可信"（Codex P1 / Qwen P1）
3. **回滚真实生效**：覆盖 Arduino 框架的 `verifyRollbackLater()`——v1 的
   "setup() 末尾自证"在默认框架行为下是**空操作**（框架在 `setup()` 之前
   就把 pending 固件标为 VALID，Grok P1-1，已对照
   `esp32-hal-misc.c:207-235` 实锤）；自证点后移至 UI 主循环起来之后
   （Qwen P2）；自证前禁止任何 `while(1)` 等待（`drv.begin()` 的
   `while(1) delay(10)` 是现有雷，同批拆除）
4. **流式下载自管**：弃用 `HTTPUpdate`（无法注入签名/SHA 校验、无取消
   语义、2.1MB 体不受控），改为 HTTPClient 流式 → `Update.write` +
   边下边算 SHA-256（Grok P1-2/P1-3）
5. **契约登记**：OTA 任务按 `async_ipc_contract.md` 登记（gen/busy/队列/
   取消语义），消除 NO_CONTRACT（Qwen P2）

## 1. 现状盘点（v2 补两条受控事实）

| 项 | 状态 |
|---|---|
| 分区表 | app0 6.2MB@0x010000 / app1 6.2MB@0x650000 / otadata 8KB@0xE000（机 #2 整片备份实测；**board 定义引用框架包 `default_16MB.csv`，未版本化——见 §7 受控化**，Codex P1） |
| 回滚开关 | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`（`tools/sdk/esp32s3/sdkconfig:60`，已核实；**bootloader 产物同样未版本化——§7**） |
| **Arduino 自动自证行为** | `initArduino()`（`esp32-hal-misc.c:225`）在 `setup()` 之前对 `ESP_OTA_IMG_PENDING_VERIFY` 调用 `esp_ota_mark_app_valid_cancel_rollback()`，除非 weak 函数 `verifyRollbackLater()` 被覆盖为 true（`esp32-hal-misc.c:207-208`）——**v1 设计因此失效，v2 修复见 §5**（Grok P1-1） |
| 签名算法底座 | 预编译 mbedtls：`CONFIG_MBEDTLS_ECDSA_C=y` + `CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y`（sdkconfig:1688/1692）；**无 Ed25519**（mbedtls 2.28 系不含）——签名选型见 §2.1 |
| 固件体积 | 2.1MB（槽 6.2MB）；`ui_battery_27220_get_percent()`（port.cpp:549）电量前置 |
| 可复用件 | `http_apply_tls` 不可用于 OTA（见 §3.1）；cJSON、`chat_waitbox` UI 模式、任务+队列模式可复用 |

## 2. 更新源与清单协议（v2：签名强制）

### 2.1 清单格式与签名

```json
{
  "version": "v2.5-260912",
  "url":    "https://host/path/firmware.bin",
  "size":   2100240,
  "sha256": "<hex, 64 chars, of firmware.bin>",
  "notes":  "PenPal TTS; voice fixes",
  "sig":    "<base64 ECDSA-P256/SHA-256 signature>"
}
```

- **签名对象** = 恰好四行的规范化字节串（UTF-8，`\n` 结尾）：
  `version + "\n" + url + "\n" + size(十进制) + "\n" + sha256 + "\n"`；
  `sig` 为对该字节串的 ECDSA P-256/SHA-256 签名（64 字节 r||s，base64）。
  四字段**全部必填**，缺一或验签失败 → 拒绝更新（Codex P1"SHA 可选"已废）。
- **算法选型**：ECDSA P-256（设备预编译 mbedtls 原生支持，零新增依赖）；
  Ed25519 在 mbedtls 2.28 不可用，如评审坚持 Ed25519 需捆绑第三方实现
  （约 30KB flash）——**请复审裁定接受 P-256 或要求捆绑 Ed25519**。
- **设备侧公钥**：`config_keys.h.example` 登记占位，真实公钥进 gitignored
  `config_keys.h`（与现有秘密链同构：NVS 无关、不入 tracked 源码）；
  发布私钥永不落设备/仓库。
- 版本比对：`version != UI_T_DECK_PRO_VERSION` 即"可更新"（用户手动决策，
  不做自动强更——四方认可保留）。

### 2.2 发布端流程（受控）

签名由发布脚本完成（本地 `scripts/` 新增，私钥文件 gitignored）：算
sha256 → 组规范字节串 → 签名 → 产出清单 JSON。**bin、清单、签名三者
同源同批**；替换 bin 必须重新签。

## 3. 新模块 `examples/pda2/ota_update.h/.cpp`

```c
const char *ota_current_version(void);
bool ota_fetch_manifest(ota_manifest_t *out, char *err, int err_len);  // 含验签
bool ota_precheck(char *err, int err_len);          // 电量 <30% 拒绝 + URL 检查
typedef void (*ota_progress_cb)(int percent, ota_phase_t phase);
bool ota_run(const char *manifest_url, ota_progress_cb cb,
             char *err, int err_len);               // 下载+验签+写槽（可被 gen 取消）
```

### 3.1 专用 TLS 通道（Codex P1 / Grok P1-2 / Qwen P1）

- OTA 一律新建 `WiFiClientSecure` 并**直接** `client.setCACertBundle(
  CA_BUNDLE_MOZILLA)`——**不经过** `http_apply_tls()`（该函数读全局
  `tls_insecure`，AI Config 的 Trust 开关一旦打开就变 `setInsecure()`，
  OTA 绝不继承）。
- **URL scheme 策略**：生产构建（`OTA_ALLOW_PLAINTEXT` 编译期宏默认 0）
  拒绝 `http://` 清单与 bin；该宏**只能改代码打开，env.cfg 无法打开**
  （Codex"实验室明文须编译期开关"已采纳；局域网联调时手动开宏重编）。
- `http_ensure_time(5000)` 前置（TLS 证书时间有效性）。

### 3.2 流式下载与写入（Grok P1-2/P1-3）

- **不用 `HTTPUpdate`**（无法注入 sha256/签名校验、无取消点、写 flash 期间
  内部循环不受本应用控制）：改用 `HTTPClient` + `getStreamPtr()` 流式读，
  边写 `Update.write()` 边增量算 SHA-256（`mbedtls_sha256` 增量 API）。
- 写完依次校验：实际 size == 清单 size、SHA-256 == 清单 sha256、
  `Update.end(true)`（镜像完整性）→ 全过才算成功。
- **任务形态**（遵守 `async_ipc_contract.md`，Qwen P2-契约）：
  - `xTaskCreatePinnedToCore`（1024×16，优先级 1，core 0——同现有惯例）；
  - 请求快照任务自有（manifest URL + gen），任务禁读 UI 缓冲；
  - 结果结构 `ota_result_t { uint32_t gen; bool ok; string err; }` 走
    队列 → UI `xQueueReceive` → `delete`（所有权转移惯例）；
  - **busy/gen**：Settings 屏新增 `s_ota_busy` + `s_ota_busy_gen`，仅 UI
    线程读写，仅 gen 匹配的结果释放（§6 契约行同批补入
    `async_ipc_contract.md`，消除 NO_CONTRACT——Qwen P2）；
  - **取消语义**：**不提供下载中途强制中止**（与 PenPal 发信同款取舍：
    HTTP 传输不可安全中止）；UI 的"取消"= gen+1 + busy=false，迟到结果
    被 gen 门控丢弃；写槽**不可取消**（半途 `Update.abort()` 留半截 app1
    无害但不允许用户触发）——UI 上"取消"按钮只在前两阶段（检查/确认）
    有效，进入下载后 waitbox 显示"updating... do not power off"且无取消
    按钮（Qwen P2-cancel 的回答：不做中断，做免打扰继续 + gen 丢弃）。
- 进度回调：每 10% 经队列发 UI 刷新（EPD 友好）；主循环持续泵
  `lv_timer_handler`（写 flash 分块 ≤4KB/次天然让出）。

## 4. UI（不变 + 一处文案）

SCREEN2_2 "Firmware Update"：当前版本 + Check → 清单展示（版本/notes/
电量）→ Update 二次确认 → 覆盖层（无取消按钮，"do not power off"）→
完成弹窗"Reboot to apply?" → 用户确认 `ESP.restart()`。失败弹窗显示
err + "still on old version, safe"。电量 <30% 拒绝；未充电提示插 USB。

## 5. 回滚与自证（v2 重写：Grok P1-1 + Qwen P2）

**层 2 机制真相**（Grok P1-1）：框架 `initArduino()` 在 `setup()` 之前
自动把 pending 固件标 VALID——v1 的"setup() 末尾 mark"到那时镜像早已
VALID，崩溃不会回滚。v2：

1. **覆盖 weak 函数**（`factory.ino` 顶部，C++ 链接单元）：
   ```cpp
   extern "C" bool verifyRollbackLater() { return true; }
   ```
   （定义在 `.c`，无 `extern "C"` 盖不住——Grok 特别标注。）
   此后 `initArduino()` 不再自动自证，pending 态得以保持到自证点。
2. **自证点后移**：`setup()` 完成且**首帧 UI 已渲染**（`ui_deckpro_entry`
   之后、WiFi 成败之前）调用 `esp_ota_mark_app_valid_cancel_rollback()`——
   覆盖"setup OK 但 UI/主循环挂了"的失效类（Qwen P2）。实现为
   `lv_async_call` 一次性触发，从 loop 首个 `lv_timer_handler` 内执行。
3. **自证前禁止无限等待**（同批拆现有雷）：`factory.ino` 中
   `drv.begin()` 失败的 `while (1) delay(10);` 改为记日志继续（马达缺失
   不阻断启动）；全 setup 无任何 `while(1)`——否则 loop WDT 尚未挂载、
   不重启也不回滚，恰好绕过层 2（Grok 同簇悬挂）。
4. **层 2 文案修正**（Grok）：回滚目标是"**上一张已验证槽**"，不保证是
   app0；"强制回 app0"的唯一手段是 USB 清 otadata（§6）。
5. **保留警示**：`factory.ino` 注释声明——任何未来固件必须同时保留
   `verifyRollbackLater` 覆盖**和**自证调用两行，缺一即升级即回滚或
   回滚失效。

## 6. 契约登记（Qwen P2）

`async_ipc_contract.md` 新增 OTA 行（与代码同批入库，§11 规则）：
消费者 Settings(SCREEN2_2)、任务 ota_run、gen 语义（页面代次）、busy
（s_ota_busy/busy_gen，UI 线程）、队列深度 1（并发上限 1，重复请求拒绝）、
取消=gen+1+busy=false（不强制中止传输）、超时=HTTPClient 45s + 总体
deadline 由用户重启兜底。

## 7. 分区表 / bootloader 受控化（Codex P1）

- 两台机读回实际分区表 + bootloader（0x0000..0x8000）各存
  `backups/`，记录 SHA-256；确认与 `default_16MB.csv` 一致。
- build 文档新增节：OTA 发布前提 = 分区表/bootloader 版本 + SHA-256 与
  `backups/` 基线一致；框架包升级（esp32s3 sdkconfig/分区变更）视为
  OTA 兼容性破坏事件，须重新核验 §8 用例。

## 8. 验证计划（v2 强化）

1. **回滚真测（防假阴性，Grok P1-1）**：刷一个**带 `verifyRollbackLater
   覆盖`但 setup() 死循环（无 WDT 挂载前）**的测试固件 → 必须观察到
   bootloader 自动回滚 + 回滚后旧版本正常运行；再刷一个 `setup()` 正常、
   **主循环死循环**的固件 → 因自证点在主循环首帧之后，同样必须回滚
   （覆盖 Qwen"UI/主循环回归"类）。
2. 正向：局域网起服务放"版本+1"固件 → 全流程（Check→Update→重启→
   About 版本变化）。
3. 签名用例：正确签名通过；篡改 version/url/size/sha256 之一 → 拒绝；
   无 sig 字段 → 拒绝；错公钥 → 拒绝。
4. TLS 用例：设备开着 `tls_insecure` 时 OTA 仍强制 CA 校验（对自签
   HTTP 服务器证书的 HTTP 明文在宏关闭时被拒）。
5. 断电/中断：下载 50% 拔电 → 重启旧版本完好（§2.4 行 6）。
6. 低电量：模拟 <30% 拒绝。
7. 双机各过一遍（机 #1 先行）。

## 9. 工作量与顺序

签名脚本 + ota_update 模块 + SCREEN2_2 + factory.ino 三处（verifyRollback
覆盖 / 自证点 / drv.begin 拆雷）+ 契约行 + build 文档发布节，一次提交；
真机验证按 §8（回滚真测必须先于首次真实 OTA）。

## 10. v1 评审问题对照（§8 六问的最终答案）

| v1 §8 问题 | v2 裁定 |
|---|---|
| 1 自动更新 | 不做（四方认可手动） |
| 2 自证点 | 移至主循环首帧后（Qwen P2），且前置 `verifyRollbackLater` 覆盖（Grok P1-1） |
| 3 明文 HTTP | 编译期宏默认关、env.cfg 打不开（Codex 要求采纳） |
| 4 签名 | **v1 必做**：ECDSA P-256（Ed25519 需捆绑实现——请复审裁定） |
| 5 组件选型 | HTTPUpdate 弃用，改自管流式（Grok P1-2/3：校验注入+取消+超时受控） |
| 6 回滚实测 | 验证计划第 1 项，先于首次真实 OTA |

## 11. v1→v2 修订记录

| # | 处置 | 评审来源 |
|---|---|---|
| 1 | §3.1 专用 TLS 通道 + §4.3 式编译期明文开关 | Codex P1 / Grok P1-2 / Qwen P1 |
| 2 | §2 清单签名（ECDSA P-256，四字段必填，公钥走秘密链） | Codex P1 / Qwen P1 |
| 3 | §5 回滚三重修复（weak 覆盖 / 自证点后移 / 拆 while(1) 雷）+ 层 2 文案"上一张已验证槽" | Grok P1-1 / Qwen P2 |
| 4 | §3.2 弃用 HTTPUpdate，自管流式 + SHA-256 + size 校验 | Grok P1-2/P1-3 |
| 5 | §6 契约登记行（gen/busy/队列/取消语义） | Qwen P2 |
| 6 | §7 分区表/bootloader 受控化 + 双机读回 | Codex P1 |
| 7 | §8 回滚真测防假阴性用例前置 | Grok §8.6 / Qwen / Claude 补充用例 |
| 8 | Qwen P3：清单 version 与 bin 烘焙版本一致性 → 发布脚本内置校验（构建后从 utilities.h 读版本写入清单） | Qwen P3 |
| 9 | Qwen P3（x-MD5 头）不采纳：sha256 已入签名清单，传输完整性由 TLS+签名双覆盖 | Qwen P3 |
