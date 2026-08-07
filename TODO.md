# TODO

> 总目标：实现 `examples/allinone` 整合固件（GPS + MP3 + 键盘 + 词典 + WiFi 配置 + AI 对话）。
> 设计评审：`docs/allinone-design.md`（§10 待评审要点，通过后再进入阶段 1）。

## 阶段 0（当前）：pda2 预研 —— 真机验证 WiFi 配置 + AI 配置

> 目的：两个新功能先在**改动最小**的 pda2 固件上跑通并真机验证，成熟后再移植进 allinone，
> 避免「一次性大改 + 新功能」叠加导致问题难定位。

- [x] **运行时 WiFi 配置屏**（2026-08-08 代码完成，真机待验证）：`create4_1` 实现——
      **SSID 用 `lv_dropdown`（`WiFi.scanNetworks()` 结果）**，`\n` 扫描+展开、`+`/`-`(Sym 层) 移动、`\n` 选中跳密码；**密码独立 `lv_textarea`**；
      `Preferences`(namespace `wifi`) 存 NVS → `WiFi.begin` → 状态/IP/失败原因；替换 `factory.ino` 编译期 `#if defined(WIFI_SSID)` 连接块为 NVS 自动连接。
- [x] **AI 对话 + 配置屏（OpenRouter）**（2026-08-08 代码完成，真机待验证）：
      - `openai_api.*`：POST `https://openrouter.ai/api/v1/chat/completions`，
        `Authorization: Bearer <key>`，cJSON 组 `messages`，解析 `choices[0].message.content`（复用 `http_post`）。
      - 新增 SCREEN_AI_CHAT（`\n` 发送 → 分页回答）与 SCREEN_AI_CFG（端点预填 OpenRouter / 模型手动输入 / Key 掩码 → NVS(namespace `ai`)）。
      - 菜单页 2 新增 "AI Text"(95,189) / "AI Cfg"(167,189) 入口。
- [x] **keypad 大写层**（2026-08-08，用户确认）：`peri_keypad.cpp` 新增大写层 `keymap_shift`，
      (2,0) 键由 Alt(临时符号) 改为 **Shift（按住大写）**；Sym(3,8) 仍管符号/数字层。AI 屏去 `c` 快捷键（英文含 'c' 会误触配置）。
- [ ] **真机验证**：配 WiFi（下拉选 SSID + 密码，含 Shift 大写）、连接成功/失败路径、AI 问答正常；确认 keypad 输入够用（含 OpenRouter Key `sk-or-v1-...`）。
- [ ] **记录移植结论**：哪些可原样移植进 allinone、哪些需调整（UI 布局、超时、NVS 命名）。

## 阶段 1：实现 `examples/allinone`（设计评审通过后）

- [ ] 按 `docs/allinone-design.md` §7 实现（菜单 + GPS/MP3/词典/键盘/WiFi/AI 屏）。
- [ ] 移植阶段 0 验证过的 WiFi 配置 + AI 对话/配置屏。

## 阶段 2：编译与真机验证

- [ ] `pio run -e allinone --jobs 8` 编译通过、无未定义引用。
- [ ] 烧录真机：菜单切换、GPS/MP3/词典/键盘各功能、WiFi/AI 可用。
