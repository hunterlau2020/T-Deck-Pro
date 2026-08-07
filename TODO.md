# TODO

> 总目标：实现 `examples/allinone` 整合固件（GPS + MP3 + 键盘 + 词典 + WiFi 配置 + AI 对话）。
> 设计评审：`docs/allinone-design.md`（§10 待评审要点，通过后再进入阶段 1）。

## 阶段 0（当前）：pda2 预研 —— 真机验证 WiFi 配置 + AI 配置

> 目的：两个新功能先在**改动最小**的 pda2 固件上跑通并真机验证，成熟后再移植进 allinone，
> 避免「一次性大改 + 新功能」叠加导致问题难定位。

- [ ] **运行时 WiFi 配置屏**：pda2 补实现 `create4_1`（Wifi Config，当前为空 stub）——
      keypad 输入 SSID/密码 → `Preferences`(namespace `wifi`) 存 NVS → `WiFi.begin` →
      状态/IP/失败原因；替换 `factory.ino:685` 编译期 `#if defined(WIFI_SSID)` 连接块。
- [ ] **AI 对话 + 配置屏（OpenRouter）**：
      - 新写 `openai_api.*`：POST `https://openrouter.ai/api/v1/chat/completions`，
        `Authorization: Bearer <key>`，cJSON 组 `messages`，解析 `choices[0].message.content`（复用 `http_post`）。
      - 新增 SCREEN_AI（输入问题 → 显示回答）与 SCREEN_AI_CFG（端点预填 OpenRouter / 模型手动输入 / Key → NVS(namespace `ai`)）。
- [ ] **真机验证**：配 WiFi（小写 + 符号）、连接成功/失败路径、AI 问答正常；确认 keypad 小写输入够用（含 OpenRouter Key `sk-or-v1-...`）。
- [ ] **记录移植结论**：哪些可原样移植进 allinone、哪些需调整（UI 布局、超时、NVS 命名）。

## 阶段 1：实现 `examples/allinone`（设计评审通过后）

- [ ] 按 `docs/allinone-design.md` §7 实现（菜单 + GPS/MP3/词典/键盘/WiFi/AI 屏）。
- [ ] 移植阶段 0 验证过的 WiFi 配置 + AI 对话/配置屏。

## 阶段 2：编译与真机验证

- [ ] `pio run -e allinone --jobs 8` 编译通过、无未定义引用。
- [ ] 烧录真机：菜单切换、GPS/MP3/词典/键盘各功能、WiFi/AI 可用。
