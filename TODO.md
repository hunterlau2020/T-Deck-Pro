# TODO

> 总目标：实现 `examples/allinone` 整合固件（GPS + MP3 + 键盘 + 词典 + WiFi 配置 + AI 对话）。
> 设计评审：`docs/allinone-design.md`（2026-08-17 第三轮修订，与 pda2 预研最终实现对齐）。

## 阶段 0（当前）：pda2 预研

> 状态（2026-08-17）：WiFi 配置屏、AI 对话/配置屏已在 pda2 跑通并经 **22 轮评审**迭代
> （`docs/reviews/`，最新申请 `wifi-config-keyboard-review-request-844a907..8d273cd.md`）。
> 预研结论已回写 `docs/allinone-design.md` §4，阶段 1 照此移植。

### 阻塞项（合并前置条件）

- [x] **P0 Sleep 三项真机回归**（2026-08-17 用户实测 ✅：倒计时 2→1→深睡、Back 取消、
      BOOT 唤醒）——合并门禁满足。
- [ ] **重启恢复复测**（2026-08-17 第一轮 ❌，根因 SPIFFS rename 冲突已修 `867435e`）：
      聊 2-3 轮 → RESET → 历史恢复（串口 `chat.log present` / `history restored`）→
      追问上文 AI 能接上。
- [ ] **P1/P2 真机回归**（申请 §4 剩余项）：多轮记忆 ✅ 已过；AI Config Test 计费提示/
      取消、New 确认/重试草稿、WiFi Test 连按与离页重进、长回答 `(truncated)`。

### 预研收尾（评审跟踪项）

- [ ] **SPIFFS 写放大**：`/chat.log` 现为整文件重写（已原子安全）；改 append+compact 或
      后台保存线程（主评审 1.2 跟踪项）。
- [ ] **CJK 8KB 预算裁剪提示**：状态行/串口显示被裁掉的上下文轮数（主评审 1.3 跟踪项）。
- [ ] **AI Config 屏状态机补齐**（allinone 移植时按 `allinone-design.md` §4 实施）：
      CONFIRM_SAVE 二次确认、CONFIRM_DISCARD、Key 掩码 `****<末4位>` + Alt+R Reveal、
      最近 3 次端点/模型历史、错误分类表、Alt+0 Reset defaults、TLS 第四字段。
- [ ] **system prompt NVS 化**：`AI_SYSTEM_PROMPT` 移入 NVS `ai.system`，随 cfg_version
      迁移一起做（openai_api.h 已有 TODO）。
- [ ] **usage 统计展示屏**：读取 NVS `ai_stats` blob（chat/test 两组）做统计界面；
      如需再提供 "Reset test usage" 入口（主评审 1.4）。
- [ ] **NVS 状态机 C++ 单测化**：把 openai_api 的存取状态机提取为无 Arduino 依赖的
      单元直接编译测试（现 Python 镜像存在漂移风险，Cop 1.6）。
- [ ] **音量键 `'\v'` 处理器**：Sym 层音量键目前仅被文本输入屏忽略，无音量 UI（issue_list 1.2）。
- [ ] **麦克风键功能**：正常层麦克风键未接入录音功能（issue_list 1.3）。
- [ ] **Shutdown 观察项**（issue_list §6）：① 下次 shutdown 后插 USB 是否直接进系统
      （卡开机画面是否复现）；② 长按电源键 2-3s 能否唤醒；③ 复现卡死时抓串口日志；
      ④ 是否改为深度休眠（BOOT 键唤醒）——用户暂定"先观察再决定"。
- [ ] **test_keypad 镜像注释**：raw 坐标与驱动坐标列镜像，示例加换算提示（issue_list 1.4）。
- [ ] **开机 NTP 等待**：setup() 末尾轮询时间同步（设计稿建议 30s，未实施，issue_list 2.2）。

### 安全（推公网 / 发布前，`SECURITY.md` 4 步）

- [ ] 删除 `AI_KEY_DEFAULT` 真实 Key 字符串（改 `""` 或占位符）。
- [ ] 移除 `[env:pda2]` 的 `-DAI_KEY_DEFAULT_COMPILED`。
- [ ] OpenRouter 后台轮换 Key。
- [ ] `git filter-repo` 清理历史 + 仓库通告。

## 阶段 1：实现 `examples/allinone`（设计评审通过后）

- [ ] 按 `docs/allinone-design.md` §5/§7 复制裁剪（触摸清理清单、GPS 空句柄守卫、
      9 屏 + menu_keyboard_poll）。
- [ ] 移植阶段 0 验证过的 WiFi 配置 + AI 对话/配置屏（`openai_chat_multi` 多轮、
      双槽保存、SPIFFS 日志、usage 统计照搬）。
- [ ] 新写 `ui_mp3.cpp`、`ui_keypad.cpp`、`ui_wifi_config.cpp`、`ui_wifi_status.cpp`。
- [ ] `platformio.ini` 追加 `[env:allinone]`。

## 阶段 2：编译与真机验证

- [ ] `pio run -e allinone --jobs 8` 编译通过、无未定义引用。
- [ ] 烧录真机：菜单切换、GPS/MP3/词典/键盘各功能、WiFi/AI 可用。
