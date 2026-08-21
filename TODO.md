# TODO

> 总目标：实现 `examples/allinone` 整合固件（GPS + MP3 + 键盘 + 词典 + WiFi 配置 + AI 对话）。
> 设计评审：`docs/allinone-design.md`（2026-08-17 第三轮修订，与 pda2 预研最终实现对齐）。

## 阶段 0（当前）：pda2 预研

> 状态（2026-08-22）：WiFi 配置屏、AI 对话/配置屏已在 pda2 跑通并经 **31+ 轮评审**迭代
> （`docs/reviews/`）。2026-08-22 四份 Codex 结果齐至：第三批 `6d26699..1473ef9`（A）、
> `de78338`（A）、`c27cb39..3475c9b`（A）、`a924c4e`（C，P2 已由 `c8f62f3` 关闭）；
> 待结果申请仅剩 `c8f62f3`（第三批申请文件曾漏写，`eefb2fd` 补齐并与其旧哈希
> 命名结果配对闭合）。
> 2026-08-19 用户决策：不新开 allinone，pda2 即最终整合固件（设计稿归档为参考）。

### 阻塞项（合并前置条件）

- [x] **P0 Sleep 三项真机回归**（2026-08-17 用户实测 ✅：倒计时 2→1→深睡、Back 取消、
      BOOT 唤醒）——合并门禁满足。
- [x] **重启恢复复测**（2026-08-17 第二轮 ✅：历史恢复 + 多轮接续；`867435e` bak 三步换入生效）。
- [ ] **P1/P2 真机回归**（申请清单剩余项）：多轮记忆 ✅、重启恢复 ✅、Test 文案/Close ✅、
      New 确认 ✅、发送交互 ✅、WiFi Test 离页重进 ✅、Weather 三页/`r` 刷新 ✅、
      provider 下拉 ✅ 已过；剩余 3 项：Save 后 key 恢复（#6）、失败重试路径（关热点，14）、
      长回答 `(truncated)`（15）。

### 第五批评审（Codex a924c4e 结果 P2 —— ✅ 2026-08-22 修复，申请 `c8f62f3`）

- [x] **SD 提示不作格式诊断**（issue_list §3.4 跟进，`c8f62f3`）："有卡但挂载
      失败"改两行提示 `SD hint: mount failed` + `try FAT16/FAT32?`（事实 +
      建议），两处过度断言注释同步改准确；编译烧录冒烟已过，真机提示行排版待回归。

### 第四批评审（GPT 跟进评审，2026-08-21 到达 —— ✅ 2026-08-22 全部修复，Codex 结果 **A**）

- [x] **Weather 部分刷新误存成功**（issue_list §9.1，`c27cb39`）：current/forecast
      结果分开跟踪，仅完整刷新推进 `last_fetch_time`/落盘；部分刷新即失效可重试
      + 状态行提示。
- [x] **CI paths 补 `script/**`**（issue_list §9.2，`153eef7`）。
- [x] **factory.ino TLS extern 声明改 `void`**（issue_list §9.3，`3475c9b`）。

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
- [x] OpenRouter 后台轮换 Key（2026-08-21：旧 key 作废，新 key 只存 `/env.cfg`）。
- [x] `git filter-repo` 清理历史 + 仓库通告（2026-08-21：清洗 + force-push + 远程
      blob 验证干净，SECURITY.md 记录；`config_keys.h` 同日清空为模板）。

## 笔友（PenPal）App（进行中，2026-08-21 起）

- [x] **设计文档 v1**（`docs/penpal-design.md`，API schema 对本地测试服务器实测）。
- [x] **两轮设计评审**：Codex 首轮（C 部分接受）+ Kimi k3 二轮（C 部分接受，
      `8019da8` 归档，含首轮 3 处失实引用勘误）。
- [x] **设计 v2 修订**（`97e5d2f`）：k3 三前置（页数公式最大下标语义 / LLM 超时
      180s / `s_pp_busy_gen`）+ 同批 8 项全部落实。
- [ ] **设计 v2 复审**（Kimi/Codex 再走一轮；通过后进入实现）。
- [ ] **实现 commit 1**：`penpal: API client`（penpal_api + 配置链 + env.cfg.example）。
- [ ] **实现 commit 2**：`penpal: screen UI`（ui_penpal×3 + poll 挂接）。
- [ ] **实现 commit 3**：`penpal: menu icon + third menu page`（注意 §6 的 4 处配套，
      幽灵页存量已由 `de78338` 修复）。
- [ ] **实现 commit 4**：docs + 评审申请（含单队列偏差与 waitbox Close=取消首例标注）。
- 前置环境（用户侧）：测试服务器可达 + Windows 防火墙放行 8000 入站。

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
