# 设计方案：WiFi Config 5 记忆槽

> 状态：**已实现**（见 `examples/pda2/ui_deckpro.cpp` / `ui_deckpro.h` / `factory.ino`）。

## 1. 背景与目标

当前 `WiFi Config` 只保存一组 SSID/Password（NVS namespace `"wifi"`，键 `ssid`/`pass`）。用户希望支持 **5 个记忆槽**，每个槽独立保存 SSID/Password，并可选择其中一个作为“激活槽”用于连接。

### 目标
- 5 个独立槽位（slot 0..4，界面显示 1..5）。
- 每个槽保存 SSID + Password。
- 用户选择一个“激活槽”，Connect 后使用该槽连接并保存。
- 开机自动连接激活槽。
- 兼容旧版单组 SSID/Password 配置（迁移到 slot 0）。

## 2. 数据模型

NVS namespace `"wifi"`：

| Key | 类型 | 含义 |
|---|---|---|
| `slot0_ssid` .. `slot4_ssid` | String | 各槽 SSID |
| `slot0_pass` .. `slot4_pass` | String | 各槽 Password |
| `active_slot` | uint8 | 当前激活槽 0..4 |
| `legacy_migrated` | uint8 | 1 = 旧版 `ssid`/`pass` 已迁移 |

旧版 key：
- `ssid`、`pass`：仅在首次启动时读取，迁移到 slot 0 后删除。

## 3. 推荐架构

```text
WiFi Config 屏
    │
    ▼
slot_selector (0..4)  ──▶  加载该槽 SSID/Pass 到输入框
    │
    ▼
用户编辑 / 扫描选择 SSID
    │
    ▼
Connect ──▶ wifi_connect_slot(slot)
            成功 ──▶ 保存到当前槽 + 设 active_slot = slot
            失败 ──▶ 状态行显示原因
```

### 3.1 新增 API（位于 `ui_deckpro.cpp`，并在 `ui_deckpro.h` 暴露给 `factory.ino`）

```cpp
#define WIFI_SLOT_COUNT 5
#define WIFI_SLOT_NONE  255

/* 读取指定槽；空槽返回空字符串 */
void wifi_slot_load(int slot, char *ssid, int ssid_len, char *pass, int pass_len);

/* 保存指定槽 */
static void wifi_slot_save(int slot, const char *ssid, const char *pass);

/* 清空指定槽 */
static void wifi_slot_clear(int slot);

/* 获取/设置激活槽 */
int  wifi_slot_get_active(void);
static void wifi_slot_set_active(int slot);

/* 旧版单组配置迁移到 slot 0 */
void wifi_slot_migrate_legacy(void);
```

连接仍复用原有的 `wifi_cfg_connect()`，它读取当前草稿缓冲区 `wifi_ssid`/`wifi_pass`；Connect 成功后再调用 `wifi_slot_save()` + `wifi_slot_set_active()`。

### 3.2 屏幕 UI 改造

当前 `create4_1()` 布局（`ui_deckpro.cpp` 约 2269 行起）：

```
[Wifi Config]
SSID  [                ]
Pass  [                ]
status line
hint
[Connect] [Clear]
```

建议新布局：

```
[Wifi Config]
[<] Slot 1/5 [>]  [Active]
SSID  [                ]
Pass  [                ]
status line
[Connect] [Save] [Clear]
```

- **槽位切换**：顶行 `<` / `>` 按钮（触摸），或键盘音量键循环切换。
- **Active 指示**：当前激活槽在槽位标签旁显示 `*`。
- **Connect**：用当前槽草稿连接；成功后将该槽保存并设为激活槽。
- **Save**：仅保存当前输入到当前槽，不连接、不改激活槽。
- **Clear**：清空当前槽的 SSID/Password；激活标记保留，开机时若激活槽为空则跳过自动连接。

### 3.3 键盘交互

保留现有扫描行为：`Alt+Enter`（`\t`）在 SSID 字段触发扫描，`+/-` 在扫描模式下选择结果。

新增：

| 按键 | 行为 |
|---|---|
| `\v`（音量键） | 循环切换槽位 0..4 |
| `Alt+Enter`（`\t`） | 在 SSID 字段触发扫描 |
| `+/-` | 扫描模式下选择 AP |
| `Enter`（Pass 字段） | Connect 当前槽 |
| `Backspace`（空框） | 返回上一字段 / 退出 |

### 3.4 状态行文案

- `Slot 1/5 (active) · IP: 192.168.x.x`
- `Slot 3/5 · Not connected`
- `Slot 2/5 empty`

### 3.5 扫描与槽位

扫描结果写入当前 **SSID 输入框**，但不立即保存到槽。只有用户按 Connect/Save 时才把当前框内容持久化到当前槽。这样扫描不会影响其他槽。

**切换槽位 = 自动保存草稿（2026-08-26 评审订正，Claude P1）**：原实现切换时
静默丢弃未保存的 SSID/Pass 草稿（`wifi_cfg_sync_draft()` 的结果被
`wifi_slot_load()` 立即覆盖，等于死代码）。现改为切换前把**两个**输入框的
当前内容自动保存回旧槽（等价"槽位 = 持久草稿"），再加载新槽。Connect/Save
语义不变；Clear 后切换 = 保存空槽（与 Clear 等价）。

## 4. 开机自动连接

`factory.ino` 约 742 行的自动连接逻辑改为：

```cpp
{
    Preferences wifi_pref;
    wifi_pref.begin("wifi", true);
    int active = wifi_pref.getUChar("active_slot", 0);
    wifi_pref.end();

    char ssid[65] = {0}, pass[65] = {0};
    wifi_slot_load(active, ssid, sizeof(ssid), pass, sizeof(pass));

    if (ssid[0] != '\0') {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        WiFi.begin(ssid, pass);
        Serial.printf("[WiFi] Connecting slot %d to %s...\n", active, ssid);
    }
}
```

并调用 `wifi_slot_migrate_legacy()` 一次（可在 `setup()` 中）。

## 5. 迁移与兼容性

在 `setup()` 中：

```cpp
void wifi_slot_migrate_legacy(void)
{
    Preferences p;
    p.begin("wifi", false);
    if (p.getUChar("legacy_migrated", 0) == 1) {
        p.end();
        return;
    }
    String ssid = p.getString("ssid", "");
    String pass = p.getString("pass", "");
    if (ssid.length() > 0) {
        p.putString("slot0_ssid", ssid);
        p.putString("slot0_pass", pass);
        p.putUChar("active_slot", 0);
        p.remove("ssid");
        p.remove("pass");
    }
    p.putUChar("legacy_migrated", 1);
    p.end();
}
```

- 老用户升级后首次开机：旧 WiFi 凭据进入 slot 0 并设为激活，无需重新输入。
- 清空所有槽后 `active_slot` 可能指向空槽，开机不会尝试连接，状态行显示 `No active slot`。

## 6. 改动文件清单

| 文件 | 改动 |
|---|---|
| `examples/pda2/ui_deckpro.cpp` | 新增 slot API；改造 `create4_1` UI（槽位行 + Save 按钮）；修改 `wifi_cfg_keyboard_poll`；修改 `wifi_cfg_load/save` 为 slot 版本 |
| `examples/pda2/ui_deckpro.h` | 暴露 `wifi_slot_migrate_legacy` / `wifi_slot_load` / `wifi_slot_get_active` 给 `factory.ino` |
| `examples/pda2/factory.ino` | 自动连接改为读取 active slot；调用 legacy migration |
| `docs/build-and-code-structure.md` | 如有 WiFi 配置说明，更新 |

## 7. 风险与注意事项

1. **NVS 容量**：5 组 SSID/Password 最多约 5×(32+64)=480 字节，NVS 足够。
2. **按键冲突**：扫描模式下的 `+/-` 已经用于选择 AP。槽位切换应仅在非扫描模式下响应，或使用独立的音量键。
3. **EPD 刷新**：顶行槽位信息每次切槽要刷新。建议把槽位行做成独立容器，避免整屏重排。
4. **WiFi.setAutoReconnect(true)**：仍保持；断线后会自动重连激活槽。切换激活槽时不需要额外调用 `WiFi.reconnect()`，因为 Connect 会重新 `WiFi.begin`。
5. **密码明文**：与现有实现一致，NVS 中仍明文保存；如需加密可后续统一处理，不在本方案范围内。
6. **Clear 激活槽**：当前实现保留 `active_slot` 不变；开机自动连接时会检查 SSID 是否为空，空槽直接跳过，不会尝试连接。

## 8. 验收标准

- [x] 5 个槽位可分别保存不同 SSID/Password。
- [x] 切换槽位时输入框正确显示对应槽内容。
- [x] 切换槽位时旧槽草稿自动保存（2026-08-26 订正后；真机复测 ⏸）。
- [ ] Connect 成功后当前槽设为激活槽，状态行显示 `active`。
- [ ] 开机自动连接激活槽。
- [ ] 老版单组 SSID/Password 自动迁移到 slot 0。
- [ ] Clear 槽位后该槽内容为空，重新进入 WiFi Config 不恢复。
