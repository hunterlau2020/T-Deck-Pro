# 设计评审申请书：OTA 固件远程升级

- **申请人**：Claude（pda2，用户 2026-09-11 决策"开工 OTA，先做设计"）
- **申请日期**：2026-09-11
- **关联分支**：`HD-V2-250915`
- **状态**：**待评审——本文档为设计稿，未写任何实现代码**
- **关联背景**：issue_list §16/§16.1（双机硬件档案）、build 文档坑 6
  （USB 分块烧录——OTA 可绕开的痛点）、CHANGELOG 2026-09-10/11（全量
  CA 包、双机同步现状）

---

## 0. 结论先行

硬件与分区布局（A/B 双 6.2MB app 槽 + otadata）天然支持 OTA；预编译
bootloader 已启用 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`（sdkconfig
第 60 行，已核实），自动回滚开箱即用。方案 = 用户在 Settings 手动触发
的 HTTPS OTA：清单 JSON + HTTPUpdate 流式写入 + 电量前置检查 + 新固件
健康自证。全程不碰 NVS/SPIFFS/凭据。

## 1. 现状盘点（已核实的事实）

| 项 | 状态 |
|---|---|
| 分区表 | app0 6.2MB@0x010000（运行中）/ app1 6.2MB@0x650000（空闲）/ otadata 8KB@0xE000（机 #2 整片备份实测） |
| 回滚开关 | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 预编译已含 |
| 固件体积 | 2.1MB（槽 6.2MB，余量充足） |
| 可复用件 | `http_get()`（http_utils.h:79）、cJSON 解析模式（penpal_api）、电量 `ui_battery_27220_get_percent()`（port.cpp:549）、等待框 UI 模式（ai_chat chat_waitbox）、任务+队列模式（ai_voice_task） |
| 现有 OTA 代码 | 无（全树 grep 干净）——本方案全部为新增 |

## 2. 更新源与清单协议

- `/env.cfg` 新增 `OTA_URL=<清单 JSON 的 URL>`（env.cfg.example 加注释
  模板；HTTPS 推荐，HTTP 仅供局域网测试）
- 清单（小 JSON，`http_get` + cJSON 解析，参照 penpal_api 的 j_str 模式）：

```json
{
  "version": "v2.5-260912",
  "url":    "https://host/path/firmware.bin",
  "notes":  "PenPal TTS; voice fixes"
}
```

- 版本比对：清单 `version` 与 `UI_T_DECK_PRO_VERSION`（utilities.h:12）
  字符串比对，**不同即可更新**（用户手动决定，不做自动强更；版本号格式
  非 semver，不做大小判断，避免误判）

## 3. 新模块 `examples/pda2/ota_update.h/.cpp`

```c
const char *ota_current_version(void);            /* UI_T_DECK_PRO_VERSION */
bool ota_fetch_manifest(ota_manifest_t *out, char *err, int err_len);
bool ota_precheck(char *err, int err_len);        /* 电量/URL 检查 */
typedef void (*ota_progress_cb)(int percent);
bool ota_run(const char *bin_url, ota_progress_cb cb, char *err, int err_len);
```

- `ota_run` 用框架自带 `libraries/HTTPUpdate`（流式写 flash，RAM 占用小；
  核实存在于 arduino-esp32 2.0.14）：
  - HTTPS：`http_apply_tls(client)`（全量 Mozilla CA 包，§16 已验）；
    `http://` 前缀走明文 client（局域网测试）
  - `rebootOnUpdate(false)`——完成后回到 UI 由用户确认重启
  - `onProgress` 回调 → FreeRTOS 队列 → UI 线程刷新进度（ai_voice_task
    同款跨线程模式，规避 LVGL 线程约束）
- 任务栈沿用 16KB 惯例；OTA 写 flash 期间主循环保持泵 `lv_timer_handler`

## 4. UI（Settings 第二页新入口）

- `ui_deckpro.h` 追加 `SCREEN2_2_ID`；Settings 列表追加 `"- Firmware
  Update"`（`UI_SETTING_TYPE_SUB`，照 About System/SCREEN2_1 模式；
  scr_mgr_register 于 ui_deckpro_entry）
- 屏幕流程：
  1. 显示当前版本 + `Check` 按钮
  2. 检查 → 显示新版本 + notes + 电量状态 → `Update` 按钮（二次确认）
  3. 下载中：chat_waitbox 同款覆盖层 "Updating... N%"（每 10% 刷一次，
     EPD 友好）
  4. 完成 → "Reboot to apply?" → 用户确认 → `ESP.restart()`
- **前置检查**：电量 <30% 拒绝并提示充电；未在充电时建议插 USB
  （BQ27220 现成接口，见 §1）

## 5. 失败/回滚三层模型（对应评审关注点：断电、坏包、变砖）

| 层 | 场景 | 结果 |
|---|---|---|
| 1 | 下载中断/坏包/校验失败 | otadata 不翻转，重启即回旧版，零损伤 |
| 2 | 新固件已切但起不来 | bootloader 回滚：pending 态新固件未自证 → 自动回 app0（开关已启用） |
| 3 | 全挂 | USB 分块烧录（坑 6 流程）+ backups/ 两机备份整片恢复 |

- **自证点**：`factory.ino` setup() 末尾（NVS/SPIFFS/外设初始化全过后）
  调 `esp_ota_mark_app_valid_cancel_rollback()`——"能完成基础启动"即
  视为健康；不绑定 WiFi 成功（避免网络差导致好固件被回滚的死循环）
- ota_update 头文件显式 `#include <esp_ota_ops.h>` 并在注释中说明：**任
  何未来固件必须保留这行自证调用**，否则升级即回滚

## 6. 发布流程（写入 build 文档新节）

```
pio run -e pda2                       # 产出 .pio/build/pda2/firmware.bin
上传 firmware.bin 到 OTA_URL 同目录
更新清单 JSON 的 version/url/notes
设备: Settings → Firmware Update → Check → Update → Reboot
```

回退（USB）：清 otadata（`write_flash 0xE000` 8KB 0xFF）即回 app0。

## 7. 验证计划

1. PC 起局域网 HTTP 静态服务放清单+bin；先做"版本号 +1"的测试固件真升
   级演练（app0→app1→重启确认 About 版本变化）
2. 反向用例：坏 bin（截断文件）、清单 404、下载中关 WiFi——确认三层防
   护各自生效、设备存活
3. 低电量用例：模拟 <30% 拒绝路径
4. 双机各过一遍（机 #1 先行，单机验证惯例）
5. 通过后：CHANGELOG + issue_list 新节 + 本文档状态改"已实施"

## 8. 已知取舍 / 请评审重点

1. **不做自动强更**：用户手动触发——是否需要后台静默检查+角标提示？
   （二期再议）
2. **自证点选在 setup() 末尾**（能启动+基础外设 OK），而非"WiFi 连上
   后"——网络依赖会让好固件在网络差时被回滚。是否接受？
3. **HTTP 明文仅限测试**：是否强制 HTTPS（生产）？建议 env.cfg 之外加
   编译期开关，默认禁明文。
4. **清单不做签名校验**：依赖 HTTPS 传输安全 + SHA256 可选字段（本版
   不实现签名；若要求防服务器被攻破的供应链攻击，需二期加 ed25519 签
   名清单）。请评审风险接受度。
5. **ESP-IDF esp_https_ota 组件未用**（Arduino 无封装、C API 与现有
   HTTPClient 模式不一致）；HTTPUpdate 内部即 Update.h 流式写，行为等
   价。请评审是否认可。
6. 回滚开关虽已启用，但**未实测** pending→rollback 路径（需要真机刷一
   个自毁固件验证）——已列入 §7 验证计划第 2 步的反向用例。

## 9. 工作量

ota_update 模块 + SCREEN2_2 + factory.ino 一行自证 + env.cfg.example
模板 + build 文档发布流程节，预计一次提交；真机验证一晚。
