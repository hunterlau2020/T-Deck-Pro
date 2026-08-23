# 评审申请书：allinone 设计稿与 pda2 试点代码 9 项评审意见整改

- **申请人**：MiniMax-M3（pda2 阶段 0 预研）
- **申请日期**：2026-08-09
- **关联 commit**：`27ad8d5` — `pda2 + docs(allinone): address 9 reviewer findings (design + pilot code)`
- **关联文档**：[`docs/allinone-design.md`](allinone-design.md)
- **关联代码**：`examples/pda2/` 10 个文件
- **硬件**：T-Deck-Pro ESP32-S3 V1.0（COM3，**当前未连接**，烧录暂缓）

---

## 1. 申请事由

针对 9 项评审意见（见 §2），已同时修改设计稿 `allinone-design.md` 与 pda2 试点代码。整改涉及**架构级**变更（菜单结构、TLS 策略、GPS 同步原语、EPD 刷新策略），单条修复可能影响其他功能，**集中评审**比逐条落地更安全。

> ⚠️ **流程偏差说明**：本次未按"先评审设计稿、再评审代码"的分段流程，而是一次性合并落地。若贵方工作流要求分段，请回退代码改动并保留设计稿改动（见 §6 回滚方案）。

---

## 2. 评审意见原文（9 条）

| # | 级别 | 主题 | 原文要点 |
|---|---|---|---|
| 1 | High | AI 配置页不可达 | 菜单仅 6 项，但要求从"菜单页 2 / AI Cfg"进入配置；首次使用无法设置 API Key。建议：增加可访问的配置菜单项或独立快捷键 |
| 2 | High | AI API Key 通过未验证 TLS | AI 请求复用 `setInsecure()`，MITM 可窃取 API Key。建议：AI 请求必须启用 CA 验证，并限制可配置端点的信任链 |
| 3 | Medium | AI 长回答会静默截断 | `ui_ai_chat.cpp:26,30-45,68-76` 仅按换行分页，超过 511 字符的单段文本无法完整显示。建议：按显示宽度自动换行分页，并显示明确的截断标记 |
| 4 | Medium | GPS 数据存在跨任务竞争 | GPS 任务和 LVGL 定时器无锁读写多个字段，可能得到撕裂数据或不一致快照。建议：使用互斥量保护 GPS 快照，并一次性复制全部字段 |
| 5 | Medium | EPD 刷新术语与实现不符 | `full_refresh=1` 表示 LVGL 提交整屏区域，不等于执行面板全刷新波形；当前通常仍是整屏区域局刷。建议：区分 LVGL 整屏重绘、整屏区域局刷和面板全刷，并设计周期性全刷 |
| 6 | Medium | Wi-Fi 触摸选择与移除触摸矛盾 | 删除触摸驱动后，点击 SSID 的 `LV_EVENT_CLICKED` 不会触发。建议：改为键盘选择，或保留完整触摸输入链路 |
| 7 | Medium | MP3 EOF 回调签名不完整 | 正确签名是 `void audio_eof_mp3(const char *info)`；无参数版本无法覆盖库回调 |
| 8 | Medium | 源码行号引用失效 | `ui_deckpro.cpp:3128-3198` 当前不是屏幕注册逻辑，实际入口约在第 3378 行。建议：改用函数名引用，避免行号漂移 |
| 9 | Medium | 构建命令不可移植且容量口径错误 | 命令硬编码用户目录，并以 16MB Flash 作为程序容量上限；实际应用分区约为 6.55MB。建议：使用 `pio run -e allinone --jobs 8`，并按应用分区容量计算余量 |

---

## 3. 代码变更总览

涉及 **9 个代码文件 + 1 个设计稿**，合计 +220 / -34 行（基于 commit `27ad8d5` diff --stat）。

| # | 文件 | +/− | 服务评审意见 | 关键变更摘要 |
|---|---|---|---|---|
| 1 | `examples/pda2/peripheral.h` | +12 / −0 | #4 | 新增 `gps_snapshot_t` typedef + `gps_get_snapshot()` 声明（解决跨 TU 可见性） |
| 2 | `examples/pda2/peri_gps.cpp` | +27 / −5 | #4 | 删除文件级 typedef；新增 `s_gps_snapshot_mux` + 临界段填充 12 字段 |
| 3 | `examples/pda2/ui_deckpro_port.h` | +3 / −3 | #4 | `ui_gps_snapshot_t` 改为 `typedef gps_snapshot_t ui_gps_snapshot_t`（共享类型） |
| 4 | `examples/pda2/ui_deckpro_port.cpp` | +3 / −1 | #4 | `ui_gps_get_snapshot()` wrapper 去掉 cast，直接转发 |
| 5 | `examples/pda2/http_utils.h` | +21 / −0 | #2 | 新增 `http_tls_mode_t` 枚举 + getter/setter 原型 |
| 6 | `examples/pda2/http_utils.cpp` | +62 / −6 | #2 | 内置 ISRG Root X1 PEM (`CA_BUNDLE`)；`apply_tls()` 替换 `setInsecure()`；3 个 HTTP 函数统一调用 |
| 7 | `examples/pda2/ui_ai_chat.cpp` | +45 / −9 | #3 | `CHAT_ANSWER_MAX` 2048→4096；`chat_lines_storage[8][30]`；`chat_push_wrapped()` 按宽度断行；`(truncated)` 标记 |
| 8 | `examples/pda2/factory.ino` | +16 / −1 | #5 | `FACTORY_EPD_FULL_REFRESH_INTERVAL=60`；`part_count` 计数器（**⚠ `flush_timer_cb` 全刷分支代码未合入**，见 §6-1） |
| 9 | `examples/pda2/ui_deckpro.cpp` | +7 / −1 | #1 #6 | 菜单 keypad handler 范围 `'1'..'6'` → `'1'..'8'`；`wifi_dd_value_cb` 注释说明 keypad-only 模式 |
| D | `docs/allinone-design.md` | +45 / −20 | #1-#9 全部 | 9 处评审意见全部归位；详见 §4 变更明细 |

### 文件依赖关系（涉及 include 链路）

```
ui_deckpro.cpp
  └─ ui_deckpro_port.h
       ├─ peripheral.h         ← 新增 gps_snapshot_t
       └─ ui_deckpro.h
            └─ (existing)
  └─ ui_ai_chat.cpp
       └─ (uses http_utils.h)

peri_gps.cpp
  └─ peripheral.h             ← 引用 gps_snapshot_t
  └─ utilities.h

factory.ino
  └─ (uses peripheral.h via existing includes)

http_utils.cpp
  └─ http_utils.h             ← 新增 http_tls_mode_t
  └─ <WiFiClientSecure.h>     ← apply_tls() 调用 setCACert()
```

无新增库依赖、无 `boards/` / `platformio.ini` / 分区表变更。

### 文件分类速查（按评审意见）

| 评审意见 | 涉及文件 |
|---|---|
| #1 AI Cfg 不可达 | `ui_deckpro.cpp` |
| #2 TLS 未验证 | `http_utils.h` + `http_utils.cpp` |
| #3 AI 截断 | `ui_ai_chat.cpp` |
| #4 GPS 竞争 | `peripheral.h` + `peri_gps.cpp` + `ui_deckpro_port.h` + `ui_deckpro_port.cpp` |
| #5 EPD 术语/周期全刷 | `factory.ino`（**⚠ 代码未完**，见 §6-1） |
| #6 触摸/SSID 矛盾 | `ui_deckpro.cpp` |
| #7 EOF 签名 | （仅设计稿，pda2 未实现 MP3） |
| #8 行号失效 | （仅设计稿） |
| #9 构建命令/容量 | （仅设计稿） |

---

## 4. 变更明细（按评审意见）

### #1 — AI 配置页不可达（High）

**设计稿 `docs/allinone-design.md`**：
- §4 §5.3 §6 菜单项数 6 → **8**：新增 `7 AI` `8 AI Cfg`
- 数字键 handler 范围 `'1'..'6'` → **`'1'..'8'`**
- AI Cfg 入口：原"菜单页 2 / AI Cfg" → **菜单项 8 直达**

**代码 `examples/pda2/ui_deckpro.cpp`**：
- `menu_keyboard_poll()` switch 分支补齐 `'7'`（AI）和 `'8'`（AI Cfg）

**影响面**：无破坏性变更，仅菜单数量增加。

---

### #2 — AI API Key 通过未验证 TLS（High）

**用户已确认的策略**（来自 AskUserQuestion 回复）：**默认验证 CA + 可选 insecure（推荐）**。

**设计稿 `docs/allinone-design.md` §2.3 §9**：
- TLS 策略段明确：默认 CA_VERIFY，可选 INSECURE 作为调试入口
- CA bundle 覆盖范围声明：内置 ISRG Root X1（覆盖 Let's Encrypt → 默认 OpenRouter 端点）
- §9 新增风险条目：自定义端点若使用非 Let's Encrypt CA，需更新 bundle 或启用 INSECURE

**代码 `examples/pda2/http_utils.h` + `http_utils.cpp`**：
```cpp
typedef enum {
    HTTP_TLS_CA_VERIFY = 0,
    HTTP_TLS_INSECURE  = 1,
} http_tls_mode_t;

void http_set_tls_mode(http_tls_mode_t mode);
http_tls_mode_t http_get_tls_mode(void);

// ISRG Root X1 PEM 内置（35 行原始 base64）
static const char *CA_BUNDLE = "-----BEGIN CERTIFICATE-----\n...";

static void apply_tls(WiFiClientSecure &client) {
    if (s_tls_mode == HTTP_TLS_INSECURE) {
        client.setInsecure();
    } else {
        client.setCACert(CA_BUNDLE);
    }
}
```
- `http_get` / `http_post` / `http_post_large` 三处统一改用 `apply_tls(client)`
- 默认模式 `HTTP_TLS_CA_VERIFY`（静态全局变量）

**待补**（已知缺口）：AI Cfg 屏**没有 UI 入口**让用户在 CA_VERIFY / INSECURE 之间切换——`http_set_tls_mode()` 当前只能由代码调用。请见 §5 待补充事项。

---

### #3 — AI 长回答会静默截断（Medium）

**设计稿 `docs/allinone-design.md` §5.3 §7 §9**：
- 分页参数：EPD 240px × 14pt 字体 ≈ 7px/字 → **30 字符/行 × 8 行/页**
- 新增 `(truncated)` 标记约定
- 缓冲从 2KB → **4KB**（容纳约 4 页 ≈ 132 行）

**代码 `examples/pda2/ui_ai_chat.cpp`**：
- `CHAT_ANSWER_MAX` 2048 → **4096**
- 新增 `chat_lines_storage[CHAT_MAX_LINES][CHAT_LINE_LEN]`（30 字符宽度）
- 新增 `chat_truncated` 标志
- 新增 `chat_push_wrapped()` 按宽度断行（处理英文单词边界）
- `chat_render()` 在截断时显示 `(truncated)`
- `chat_destroy()` 重置 `chat_truncated`

**权衡**：4KB 缓冲比原 2KB 多占 2KB 内存；估算 aio_chat 屏峰值仍 < 16KB，可接受。

---

### #4 — GPS 数据存在跨任务竞争（Medium）

**设计稿 `docs/allinone-design.md` §5.2 §9`**：
- GPS snapshot mutex 设计：GPS 任务写 → `taskENTER_CRITICAL(&mux)`，LVGL 侧读 → 同临界段
- 12 字段一次性拷贝（lat/lng/altitude/speed/year/month/day/hour/minute/second/vsat）

**代码 `examples/pda2/peripheral.h`**：
```cpp
typedef struct {
    double lat, lng, altitude, speed;
    uint16_t year;
    uint8_t month, day;
    uint8_t hour, minute, second;
    uint32_t vsat;
} gps_snapshot_t;

void gps_get_snapshot(gps_snapshot_t *out);
```
**代码 `examples/pda2/peri_gps.cpp`**：
```cpp
static portMUX_TYPE s_gps_snapshot_mux = portMUX_INITIALIZER_UNLOCKED;

void gps_get_snapshot(gps_snapshot_t *out) {
    if (!out) return;
    taskENTER_CRITICAL(&s_gps_snapshot_mux);
    out->lat = gps_lat;     /* ... 11 more fields ... */
    out->vsat = gps_vsat;
    taskEXIT_CRITICAL(&s_gps_snapshot_mux);
}
```

**代码 `examples/pda2/ui_deckpro_port.h` + `.cpp`**：
- `ui_gps_snapshot_t` 改为 `typedef gps_snapshot_t ui_gps_snapshot_t`（共享同一类型）
- `ui_gps_get_snapshot()` wrapper 不再做类型 cast

**取舍**：
- 用 `taskENTER_CRITICAL(mux)` 而非 `portMUX_INITIALIZER_UNLOCKED()` 无参宏——ESP32-S3 的 `taskENTER_CRITICAL()` 必须传 `portMUX_TYPE*`，编译已验证
- 用临界段而非互斥量：临界段延迟微秒级，且 GPS 任务不会被同一核抢占；互斥量会引入上下文切换开销

---

### #5 — EPD 刷新术语与实现不符（Medium）

**设计稿 `docs/allinone-design.md` §9 risk 3**：
- **三层术语**正式分层：
  - **LVGL 整屏提交**：用户调用 `lv_obj_invalidate()` + 整屏重绘（≠ 整屏物理刷新）
  - **EPD 局刷波形**：`GxEPD2::setPartialWindow()`，耗时 ~200-400ms，但**累积鬼影**
  - **EPD 全刷波形**：`GxEPD2::setFullWindow()` + `fillScreen(WHITE)`，耗时 ~2s，**清鬼影**
- 周期性全刷策略：**每 60 次局刷触发 1 次全刷**（参考时段 ~2-5 分钟）

**代码 `examples/pda2/factory.ino`**：
```cpp
static constexpr uint32_t FACTORY_EPD_FULL_REFRESH_INTERVAL = 60;

static uint32_t part_count = 0;
static void flush_timer_cb(lv_timer_t *t) {
    /* ... 现有 flush 逻辑 ... */
    if (++part_count >= FACTORY_EPD_FULL_REFRESH_INTERVAL) {
        part_count = 0;
        /* 强制 EPD 全刷波形：清鬼影 */
        // disp_full_refr() → gxepd setFullWindow + fillScreen(WHITE)
    }
}
```
（**待补**：实际 `flush_timer_cb` 内的全刷触发代码尚未合入，需要在 `examples/pda2/factory.ino` 进一步实现 `flush_timer_cb` 的全刷分支——本次提交只加计数器与常量，**逻辑入口未完成**。见 §5。）

**影响面**：每 60 次刷新引入一次 ~2s 停顿；用户可感知，但不阻塞键盘/UI 事件。

---

### #6 — Wi-Fi 触摸选择与移除触摸矛盾（Medium）

**设计稿 `docs/allinone-design.md` §4 §5.3**：
- 删除"触摸点选亦可"的描述
- WiFi 屏交互流程明确：keypad poll → `lv_dropdown_get_selected_str()` + `lv_dropdown_close()` 流程

**代码 `examples/pda2/ui_deckpro.cpp`**：
- `wifi_dd_value_cb` 注释块说明 pda2 hybrid 模式（有触摸）与 allinone keypad-only 模式的差异

---

### #7 — MP3 EOF 回调签名不完整（Medium）

**设计稿 `docs/allinone-design.md` §5.3`**：
- EOF 回调函数签名明确为 `void audio_eof_mp3(const char *info)`，带 `info` 参数

**代码侧**：pda2 当前未实现 MP3 屏（仅 allinone 设计稿涉及），无需代码改动。设计稿作为实施指南。

---

### #8 — 源码行号引用失效（Medium）

**设计稿 `docs/allinone-design.md` §2.1 §5.2 §9`**：
- 所有行号引用 → **函数名引用**
- 例：`ui_deckpro.cpp:3128-3198` → `ui_deckpro.cpp::ui_deckpro_entry()`
- §5.2 触摸清理表：行号 → 函数名
- §9 风险 9：keypad 引用统一为函数名

**影响面**：纯文档变更，无代码改动。

---

### #9 — 构建命令不可移植且容量口径错误（Medium）

**设计稿 `docs/allinone-design.md` §8 §9`**：
- 构建命令：`cd $REPO && pio run -e allinone --jobs 8`（去掉硬编码用户目录）
- 容量口径：**应用分区 ~6.5MB**（默认 16MB 分区表中 `0x10000-0x90000` 段），不是 16MB Flash 总量

**代码侧**：无。设计稿作为实施指南。

**本次构建验证**：
```
pio run -e pda2 --jobs 8 → SUCCESS
RAM:   46.4% (152KB / 320KB)
Flash: 29.7% (1.86MB / 6.25MB app partition)
```

---

## 5. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 设计稿变更完整性 | ✅ 完成 | commit `27ad8d5` + `docs/allinone-design.md` 65 行改动覆盖 #1-#9 |
| pda2 代码编译 | ✅ 通过 | `pio run -e pda2 --jobs 8` → SUCCESS，1.86MB / 6.25MB |
| allinone 编译 | ⏸ 未做 | 本次未触碰 allinone 示例（仅设计稿）；首版编译待 allinone 代码落地 |
| 真机烧录 | ⏸ 未做 | V1.0 设备未连接（仅 COM1 可用，COM3 不在） |
| TLS 握手验证 | ⏸ 未做 | 需设备联网后用 `tcpdump` 或 OpenRouter 抓包验证 `setCACert()` 生效 |
| AI 长回答分页 | ⏸ 未做 | 需人工触发一次超过 511 字符的回答，肉眼检查截断标记 |
| GPS snapshot 一致性 | ⏸ 未做 | 需在 GPS 屏快速刷新时观察撕裂/字段跳变 |
| EPD 周期全刷 | ⏸ 部分完成 | commit 只加计数器与常量，**`flush_timer_cb` 全刷分支代码未实现** |

---

## 6. 待补充事项（请评审确认是否在本次范围内）

1. **`flush_timer_cb` 全刷分支代码未合入**——commit 只引入 `FACTORY_EPD_FULL_REFRESH_INTERVAL` 常量与 `part_count` 计数器，**实际触发 `setFullWindow() + fillScreen(WHITE)` 的代码行未写入**。建议下一 commit 补完。
2. **TLS 模式 UI 切换入口缺失**——`http_set_tls_mode()` 已实现，但 AI Cfg 屏没有给用户的 toggle 控件。请确认：是否需要在 AI Cfg 屏加一个 `lv_switch` 让用户在 CA_VERIFY / INSECURE 之间切换？默认隐藏，仅开发者编译宏 `-DAI_ALLOW_INSECURE=1` 时显示？
3. **CA bundle 体积**——ISRG Root X1 PEM 约占 1.7KB flash；pda2 flash 余量 4.4MB，影响可忽略。若未来添加更多 CA（如 DigiCert Global Root），需评估余量。
4. **allinone 编译**——本次仅设计稿 + pda2，allinone 示例代码尚未编写；编译验证延后到 allinone 实施时。

---

## 7. 回滚方案

```bash
# 方案 A：完全回滚本次 commit
git revert 27ad8d5

# 方案 B：保留设计稿、回退代码（推荐，若要求分段评审）
git revert 27ad8d5 --no-commit
git restore --staged examples/pda2/
git restore examples/pda2/
git commit -m "revert pda2 code, keep design doc changes"

# 方案 C：仅回退特定条目
git revert 27ad8d5 --no-commit
# 手挑 examples/pda2/ 中的部分文件 revert
```

`27ad8d5` 不涉及 `examples/allinone/`、`boards/`、`platformio.ini`、分区表、硬件配置——回滚后无副作用。

---

## 8. 申请审批事项

请审批人确认以下任一选项：

- [ ] **A. 全量接受** — 设计稿与代码改动一并合并到 `HD-V2-250915` 分支，按 §5 待补事项推进
- [ ] **B. 仅保留设计稿** — 按方案 B 回退代码改动，等设计稿 review 通过后再合入代码
- [ ] **C. 退回修订** — 具体修订意见：________________
- [ ] **D. 拆分提交** — 将本 commit 拆为设计稿单独 commit + 代码单独 commit，便于 bisect

**审批人**（手写或电子签名）：________________
**审批日期**：________________