# PDA2 — T-Deck Pro PDA Firmware

A PDA (Personal Digital Assistant) firmware for the **LilyGo T-Deck Pro v1.1** built on top of the factory example. The factory provides proven hardware drivers (touch, E-Paper, keyboard, GPS, LoRa, BLE, etc.) and we incrementally add PDA apps on top.

## Hardware

- **MCU**: ESP32-S3 (240 MHz, 8MB PSRAM, 16MB Flash)
- **Display**: 3.1" E-Paper 240×320 monochrome (GDEQ031T10)
- **Keyboard**: TCA8418 4×10 matrix with sym/alt modifiers
- **Touch**: CST226SE capacitive (HYN driver)
- **Peripherals**: SX1262 LoRa, GPS, BHI260AP IMU, BQ25896/BQ27220 power, DRV2605 haptic, A7682E GSM, PCM5102A audio (**4G/A7682E 版无 PCM5102A DAC**——耳机孔无 I2S 输出，MP3 播放不可行，见 issue_list §3.3)
- **SD card**: FAT16/FAT32 only（exFAT/NTFS 挂载失败显示 0MB，About System 屏有 `SD hint` 提示）；FAT32 上限 2TB，>32GB 卡需第三方工具格式化

## Key Functions

### Existing (from factory)
- LoRa transceiver (send/receive/settings)
- WiFi page: **WIFI Config**（编辑/扫描双模式 + 异步扫描 + Connect 成功才保存 + Clear）、**WIFI Scan**（AP 列表）、**WIFI Test**（ifconfig.me 公网 IP，信息层 + Close）、**Time Sync**（NTP 校时，显示同步前后时间）
- GPS position and satellite view
- Battery monitoring (BQ25896 charger + BQ27220 fuel gauge)
- Hardware test suite
- IMU/gyroscope readout
- A7682E GSM modem AT interface
- Motor/haptic feedback test
- Shutdown / deep sleep

### Added PDA Apps
- **Calculator** — Scientific calculator with Shunting-Yard expression parser. Supports sin/cos/tan/log/sqrt/exp/fact and full operator precedence.
- **Weather** — OpenWeatherMap One Call API 3.0. Current conditions, 12-hour hourly, 8-day daily forecast. Caches data in NVS for 1 hour. Uses GPS or cached coordinates.
- **Calendar** — lunar calendar + holiday lookup.
- **Dictionary** — online dictionaryapi.dev lookup.
- **AI Voice Chat** — voice AI screen (TTS reply).
- **AI Text** — WeChat 式聊天界面 via OpenRouter（OpenAI 兼容）：上 2/3 可滚动历史（AI 左/用户右气泡）+ 下 1/3 多行输入 + Send/Clear/**New** 侧按钮；**多轮上下文**（最近 8KB 整轮配对，`openai_chat_multi`）；历史持久化 SPIFFS `/chat.log`（原子换入 + 校验和），失败草稿存 `/chat.draft` 重启可恢复；发送异步（任务快照 + 页面代次）；usage 统计入 NVS `ai_stats`。
- **AI Config** — Base URL（多行）/ Model / Key 各自输入框 + Save / Test 按钮；**Test = 最小 chat-completion**（msgbox 计费提示 + 15s 绝对 deadline + Close 取消）；Save 门槛 = Test 通过（状态行明示原因）；**NVS 双槽原子保存**（暂存非活动槽 + 单键 active 翻转）；Base/Model/Key 均有固件默认值（NVS 优先）。

### Planned
- Voice Recorder
- Internet Radio
- Microphone (modified)

## Building

```bash
# Build (platformio.ini has the [env:pda2] environment;
# script/set_srcdir.py maps the env name to examples/pda2)
python -m platformio run -e pda2

# Flash + monitor (stop any running serial monitor first: it holds COMx)
python -m platformio run -e pda2 -t upload --upload-port COM5
python -m platformio device monitor -p COM5 -b 115200
```

Secrets follow the chain **NVS → SPIFFS `/env.cfg` → gitignored `config_keys.h` → empty default** (SECURITY.md). No real keys in tracked source.

## Architecture

The factory uses a **screen manager** (`scr_mgr`) with push/pop navigation. Each app is a screen with lifecycle callbacks:

```c
typedef struct scr_lifecycle {
    void (*create)(lv_obj_t *parent);   // Build UI widgets
    void (*entry)(void);                // Screen becomes visible
    void (*exit)(void);                 // Screen leaves foreground
    void (*destroy)(void);              // Cleanup
} scr_lifecycle_t;
```

To add a new app:
1. Create `ui_myapp.cpp` with a `scr_lifecycle_t` struct
2. Add `SCREEN_MYAPP_ID` to the enum in `ui_deckpro.h`
3. Register with `scr_mgr_register()` in `ui_deckpro.cpp`
4. Add a `menu_btn` entry for the home screen grid

Keyboard input is polled from the main `loop()` via `keypad_get_val()` / `keypad_set_flag()`. Each app that needs keyboard defines a `xxx_keyboard_poll()` function called from `loop()`.

## Key Tips

### 1. Use pagination, not scrolling

E-Paper refresh is slow (~1-2 seconds for full, ~0.3s for partial). Continuous scrolling produces terrible UX — the screen can't keep up.

**Do this:** Break content into fixed pages. Use show/hide (`LV_OBJ_FLAG_HIDDEN`) to swap pages. Use Enter/Space for next page, Backspace for previous.

```c
// Example: 3-page app
static lv_obj_t *pages[3];
static int cur_page = 0;

void show_page(int idx) {
    for (int i = 0; i < 3; i++) {
        if (i == idx) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    cur_page = idx;
}
```

### 2. Keyboard map

The T-Deck Pro keyboard is a 4×10 matrix read by TCA8418. The driver (`peri_keypad.cpp`) translates raw matrix positions to characters with sym/alt modifier support.

> **Raw vs driver coordinates are column-mirrored**: the driver maps
> `driver_col = 9 - raw_col` (e.g. raw `(R2 C9)` = Alt = driver `(2,0)`).
> `examples/test_keypad` prints the RAW row/col - convert the column before
> comparing with the maps below.

**Normal layer:**
```
q   w   e   r   t   y   u   i   o   p
a   s   d   f   g   h   j   k   l   ⌫
Alt z   x   c   v   b   n   m   $   ⏎
     ⇧  Mic Space Sym ⇧
```

**Shift layer** (hold either `⇧` for uppercase, releases back to lowercase):
```
Q   W   E   R   T   Y   U   I   O   P
A   S   D   F   G   H   J   K   L   ⌫
Alt Z   X   C   V   B   N   M   $   ⏎
     ⇧  Mic Space Sym ⇧
```

**Sym layer** (press Sym to toggle/lock):
```
#   1  2  3  (  )  _  -  +  @
*   4  5  6  /  :  ;  '  "  ⌫
Alt 7  8  9  ?  !  ,  .  Vol ⏎
     ⇧  0  Space Sym ⇧
```

Special keys:
- `'\b'` (0x08) — Backspace/Delete (⌫)
- `'\n'` (0x0A) — Enter (⏎)
- `'\t'` (0x09) — Alt+Enter combo (WiFi-config scan shortcut)
- `'$'` — Volume/speaker key (app-specific; ignored by text inputs)
- `'\v'` (0x0B) — Volume key on the Sym layer (reserved, no handler yet; ignored by text inputs)
- Shift keys: **two** Shift keys (bottom row, left and right) — hold for uppercase
- **Alt** (Z-row left): hold for the **momentary sym layer** (digits/symbols, same map as Sym but not latched; release to return to normal)
- Sym: toggles the sym-layer lock; on the sym layer the Mic key emits `'0'`
- Mic key is on the TCA8418 matrix; its normal layer has no keymap function

### 3. Image/icon format

The display uses `LV_COLOR_DEPTH = 1` (monochrome). Icons must match.

**Format:** `LV_IMG_CF_TRUE_COLOR_ALPHA`, 50×50 pixels, 2 bytes per pixel:
- Byte 0: color (0x00 = black, ignored for depth 1)
- Byte 1: alpha (0xFF = opaque/visible, 0x00 = transparent)

**Example C structure:**
```c
const uint8_t my_icon_map[] = {
    0x00, 0x00, 0x00, 0xFF, ...  // transparent, then black opaque
};
const lv_img_dsc_t my_icon = {
    .header.w = 50,
    .header.h = 50,
    .data_size = 2500 * LV_IMG_PX_SIZE_ALPHA_BYTE,
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = my_icon_map,
};
```

To generate icons programmatically, use a Python script that outputs the 2-byte-per-pixel C array (see `img_calculator.c` and `img_weather.c` for examples).

**Do NOT use:**
- RGB565 (3 bytes/pixel) — wrong for `LV_COLOR_DEPTH=1`
- Icons larger than 50×50 — factory menu buttons are 50×50
- `lv_img_set_zoom()` for menu icons — doesn't render well on monochrome E-Paper
