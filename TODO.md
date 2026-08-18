# TODO

> 总目标：实现 `examples/allinone` 整合固件（GPS + MP3 + 键盘 + 词典 + WiFi 配置 + AI 对话）。
> 设计评审：`docs/allinone-design.md`（2026-08-17 第三轮修订，与 pda2 预研最终实现对齐）。

## 阶段 0（当前）：pda2 预研

> 状态（2026-08-19）：WiFi 配置屏、AI 对话/配置屏已在 pda2 跑通并经 **31 轮评审**迭代
> （`docs/reviews/`，最新申请 `wifi-config-keyboard-review-request-a924c4e.md`）。
> 2026-08-19 用户决策：不新开 allinone，pda2 即最终整合固件（设计稿归档为参考）。

### 阻塞项（合并前置条件）

- [x] **P0 Sleep 三项真机回归**（2026-08-17 用户实测 ✅：倒计时 2→1→深睡、Back 取消、
      BOOT 唤醒）——合并门禁满足。
- [x] **重启恢复复测**（2026-08-17 第二轮 ✅：历史恢复 + 多轮接续；`867435e` bak 三步换入生效）。
- [ ] **P1/P2 真机回归**（申请清单剩余项）：多轮记忆 ✅、重启恢复 ✅、Test 文案/Close ✅、
      New 确认 ✅、发送交互 ✅、WiFi Test 离页重进 ✅、Weather 三页/`r` 刷新 ✅、
      provider 下拉 ✅ 已过；剩余 3 项：Save 后 key 恢复（#6）、失败重试路径（关热点，14）、
      长回答 `(truncated)`（15）。

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
- [x] **test_keypad 镜像注释**（`980b6df`）：示例加换算提示 + README §2 镜像说明（issue_list 1.4 闭合）。
- [ ] **开机 NTP 等待**：setup() 末尾轮询时间同步（设计稿建议 30s，未实施，issue_list 2.2）。

### 安全（推公网 / 发布前，`SECURITY.md` 4 步）

- [x] 删除 `AI_KEY_DEFAULT` 真实 Key 字符串（`0e78025`：默认改 `""`）。
- [x] 移除 `[env:pda2]` 的 `-DAI_KEY_DEFAULT_COMPILED`（`0e78025`）。
- [ ] OpenRouter 后台轮换 Key。
- [ ] `git filter-repo` 清理历史 + 仓库通告。

## 阶段 1：~~实现 `examples/allinone`~~（2026-08-19 用户决策：不再新开 allinone）

> pda2 即最终整合固件（菜单两页 18 入口已覆盖 GPS/词典/WiFi/AI/天气/睡眠等）。
> 原 allinone 项处置：
> - [x] **MP3 屏取消**——4G 版无 PCM5102A DAC（`issue_list` §3.3 探针实测无声）
> - [ ] **Keys 键盘演示屏**：可做进 pda2（`ui_keypad.cpp` 设计待定，用户未拍板）
> - [x] WiFi 状态/配置、AI 对话/配置、词典、GPS——pda2 已实现并评审（原"新写/
>       移植"项全部由现有屏覆盖）
> - [x] `[env:allinone]` 不追加
> - `docs/allinone-design.md` 归档为 pda2 演进参考

## 阶段 2：~~allinone 编译与真机验证~~（随阶段 1 取消）
