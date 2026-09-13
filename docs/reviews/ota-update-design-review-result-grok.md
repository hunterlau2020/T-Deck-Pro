# 设计评审结果：OTA 固件远程升级（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **设计稿**：[../ota-update-design.md](../ota-update-design.md)
- **状态**：设计稿，无实现代码
- **同日另一份结果**：[ota-update-design-review-result-claude.md](ota-update-design-review-result-claude.md)（Claude **相当于 A，可开工**）
- **评审结论**：**C 部分接受**。硬件底座与手动 HTTPS 方案可保留；下列 3 项
  P1 不写入设计稿并实现之前，**不能按现状开工**。与 Claude 同日 A 结论
  **分歧**，见文末。

## 已核实、可保留的底座

- 板级 `boards/T-Deck-Pro.json`：`partitions: default_16MB.csv`，
  `memory_type: qio_qspi`，`maximum_ram_size: 327680`。Arduino-ESP32
  2.0.14 的 `default_16MB.csv` 为 app0/app1 各 `0x640000` @ `0x10000` /
  `0x650000`，otadata 8KB @ `0xE000`，与设计 §1 一致。
- 同核心 `tools/sdk/esp32s3/qio_qspi/include/sdkconfig.h` 含
  `#define CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 1`（设计写「sdkconfig
  第 60 行」——核对的是 sdkconfig 原文行号，`.h` 里该宏在靠前位置；语义
  成立）。`platformio.ini` 钉死 `espressif32@6.5.0` → Arduino 2.0.14。
- 固件约 2.1MB，槽 6.2MB 余量充足。全树无既有 OTA 代码。
- `libraries/HTTPUpdate` 在 2.0.14 存在：`rebootOnUpdate(false)`、
  `onProgress`、`update(WiFiClient&, url)`、`HTTPUpdate(int timeout)`
  均可用。禁止调用 `updateSpiffs()`。
- 手动触发、自证不绑 WiFi 成败、不碰 NVS/SPIFFS/凭据：同意。
- Settings 现 7 项、`SETTING_PAGE_MAX_ITEM=7`，`page_num = n/7` 已能翻到
  空的第 2 页；第 8 项会填进该页，与「Settings 第二页新入口」一致。
- `http_apply_tls` / `http_ensure_time` / `ui_battery_27220_get_percent` /
  `UI_T_DECK_PRO_VERSION` / `SCREEN2_1` 接入模式均在树中。

## Findings

### P1-1：Arduino 在 `setup()` 之前自动自证，第 2 层回滚默认是空操作

- **位置**：设计 §5、§8.2；Arduino-ESP32 2.0.14
  `cores/esp32/esp32-hal-misc.c` 的 `initArduino()`。
- **证据**：`verifyRollbackLater()` weak 默认 `false`，`verifyOta()` weak
  默认 `true`。`initArduino()` 在用户 `setup()` **之前**看到
  `ESP_OTA_IMG_PENDING_VERIFY` 就会调用
  `esp_ota_mark_app_valid_cancel_rollback()`。只在
  `examples/pda2/factory.ino` `setup()` 末尾再 mark 一次，到那时镜像已是
  VALID，崩溃也不会回滚。
- **影响**：§5 层 2「新固件已切但起不来 → bootloader 回滚」在默认
  Arduino 行为下不成立。§7 用自毁固件测回滚会得到假阴性。
- **最小修复**（必须写进设计稿）：
  1. 在 `factory.ino`（C++）覆盖
     `extern "C" bool verifyRollbackLater() { return true; }`
     （函数定义在 `.c`，无 `extern "C"` 盖不住）；
  2. 再在选定自证点调用 `esp_ota_mark_app_valid_cancel_rollback()`；
  3. 「任何未来固件必须保留」同时包含这两行，不是只保留 mark。
- **同簇悬挂**：`factory.ino:708-711` 在 V1.1（机 #2，有 DRV2605）上
  `drv.begin()` 失败则 `while (1) delay(10);`，位于拟议自证点之前。
  `delay` 喂 idle；loop WDT 通常在 `setup()` 返回后才挂上，此路径
  **不重启因此不回滚**。自证前禁止无限等；外设失败应记日志继续，或
  超时后 `esp_ota_mark_app_invalid_rollback_and_reboot()`。
- 层 2 文案应改为「回**上一张已验证槽**」，不是「自动回 app0」。擦
  otadata 才是强制回 app0。

### P1-2：不能直接复用 `http_get()`；OTA 不得继承 Trust 开关

- **位置**：设计 §2、§3；`examples/pda2/http_utils.cpp` 的 `http_get_ua`；
  issue_list §7.4。
- **证据**：`http_get()` / `http_get_ua()` **永远**构造
  `WiFiClientSecure`，没有 `http://` 分支。LAN 明文清单会失败。PenPal
  `pp_request` 已按 scheme 选择 plain / secure，清单 GET 与
  `HTTPUpdate.update(client, url)` 应复制该模式。
- Trust 开关经 `http_apply_tls()` 作用于全部 HTTPS 消费者。OTA 走同一
  函数时，AI Config 打开 Trust 等于固件下载可以不校证书。
- **最小修复**：OTA 传输独立选 client；生产强制 `https://` + CA 校验，
  禁止跟随 `HTTP_TLS_INSECURE`。明文仅编译期开关且默认关，开关必须同时
  挡住清单 URL 与 bin URL（同意 Claude：只挡 bin 不够，中间人改清单
  `url` 即可转向）。

### P1-3：2.1MB 下载超时与写 flash 期间的生命周期

- **位置**：设计 §3、§4、§7。
- **证据**：`HTTPUpdate` 可 `HTTPUpdate(int httpClientTimeout)`；默认
  HTTP 超时是秒级，2.1MB 在一般 WiFi 上会中途失败。Sleep 屏会
  `esp_deep_sleep_start()`，低电路径会 `PPM.shutdown()`，写 flash 中途
  睡眠/关机留下脏槽。
- **最小修复**：分钟级超时（或按块续传）+ `WiFi.setSleep(false)`；OTA
  期间禁止 Sleep / 低电关机。进度回调留在 worker，经队列回 UI（设计
  已写，保持）。

## P2（写入设计即可，不挡修订后开工）

1. **清单 v1 带 `size` + `sha256`。** ESP32 镜像头自带 SHA256，截断/坏包
   `Update.end()` 会失败；清单哈希防的是「另一个合法镜像被挂到 URL 上」。
   ed25519 可二期。与 Claude「SHA256 升为 v1 必做」同向，本处升为 P2
   而非 P1：无哈希时 HTTPS+CA 仍能开工，但设计应把该字段列为 v1。
2. `ota_precheck` 补 WiFi 已连接；HTTPS 补 `http_ensure_time`。
3. 电量计无效时不要用可能为 0 的 SOC 误拦，改为要求
   `ui_battery_27220_get_input()`（USB）。
4. `SCREEN2_2_ID` **追加在枚举末尾**（与 `SCREEN_PENPAL_ID` 相同），不要
   插在 `SCREEN2_1` 与 `SCREEN3` 之间。
5. USB 分块脚本固定写 `0x10000`（app0）。OTA 之后运行槽可能是 app1，只
   写 `firmware.bin` 而不擦 otadata，重启仍走 app1。发布/回退文档写死
   「擦 `0xE000` 8KB + 写 app0」。
6. `env.cfg` 代码已是最多 12 条、value 160 字节；`env.cfg.example` 仍写
   Max 8 / 95 chars。加 `OTA_URL` 时一并改；清单 URL 需短于 160。
7. 按仓库惯例拆提交：`ota_update` 模块 + env 模板；Settings/`SCREEN2_2`；
   `verifyRollbackLater` + 自证 + 文档。不要「一次提交」。
8. 版本字符串不等即可更新：可当 OTA 降级；同 version 重发不会提示，必须
   改 `UI_T_DECK_PRO_VERSION`。
9. 托管若 302，需 `setFollowRedirects`。OTA 新屏 `create` / 进度覆盖层
   打一行池水位（同意 Claude，承接 `721e04a` 教训）。

## 对设计 §8 六个问题

| # | 答复 |
|---|---|
| 1 自动强更 / 角标 | 不做。手动触发适合这台机器和 USB 坑 6。二期再说。 |
| 2 自证不绑 WiFi | 接受，**前提是 P1-1**：先 `verifyRollbackLater`，自证前不能 `while(1)`。放在 `lvgl_init` + `ui_deckpro_entry` 之后、WiFi 成败之前。 |
| 3 强制 HTTPS | 生产强制。明文仅编译开关，默认关；清单与 bin 一起挡。 |
| 4 无签名 | 个人设备 + 自建 HTTPS 可接受；v1 加 size/sha256，签名二期。 |
| 5 HTTPUpdate 而非 `esp_https_ota` | 认可，前提是自己选 client、设长超时、不用 SPIFFS 那个 API。 |
| 6 回滚未实测 | 必须测。没有 `verifyRollbackLater` 的自毁固件测了也是假阴性。另测 `setup()` 死循环是否真回滚（与 Claude 补充用例同向，本处视为 P1-1 的验证项）。 |

## 已通过项

- A/B 双槽 + otadata 布局、预编译 bootloader 回滚开关（硬件侧）。
- 手动触发、不自动强更、自证不依赖 WiFi。
- 流式 `HTTPUpdate`、进度走队列、EPD 每 10% 刷新。
- 层 1（未翻转 otadata）与层 3（USB 分块 + 整片备份）方向正确。
- `rebootOnUpdate(false)` 后由 UI 确认 `ESP.restart()`。

## 验证说明

- 核对设计引用的板级 json、`platformio.ini`、pda2 Settings 分页、
  `http_get` 无 HTTP 分支、`penpal_api` 的 scheme 分支、Trust 全局作用面、
  `factory.ino` DRV2605 `while(1)`、Sleep/低电路径。
- Arduino-ESP32 2.0.14 的 `initArduino()` / `HTTPUpdate.h` /
  `default_16MB.csv` / qio_qspi `sdkconfig.h` 经公开源码复核（本环境无
  本地 `~/.platformio` 包）。
- 本文只评设计，无实现代码。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订 / 仅保留设计稿
- [x] C. **部分接受**
- [ ] D. 拆分提交

P1-1、P1-2、P1-3 写入设计稿之前，**不要开始实现**。修订后可作为 v1
实现基线；真机验证至少覆盖：成功升级、截断 bin、清单 404、下载中关
WiFi、低电拒绝、**覆盖了 `verifyRollbackLater` 的自毁固件回滚**、双机。

## 与 Claude 同日结果的分歧

Claude 结论为「相当于 A，可以进入实现」，把 SHA256、明文覆盖清单 URL、
`setup()` 挂死不重启列为建议。本结果把 **Arduino 默认自动自证** 定为
P1：不覆盖 `verifyRollbackLater` 则层 2 在代码路径上就不存在，与「建议
补一条验证用例」不是同一严重度。SHA256 升 v1 与明文同时挡住清单 URL，
两边同向（本处分别为 P2 与 P1-2 的一部分）。建议以本结果的 P1 为开工
门闩，Claude 文中的 SHA256 / 池水位 / 挂死验证并入修订稿。
