# Changelog

本文件记录 pda2 预研（T-Deck-Pro HD-V2，分支 `HD-V2-250915`）的主要工作。
评审细节见 `docs/reviews/`（每轮 = 申请 + 双评审结果，按 commit 范围命名）。

## 2026-08-22

- **第四批评审修复**（GPT 跟进评审 3 项 P2 全部关闭，Codex 结果 **A**）：
  `c27cb39` Weather 部分刷新不再缓存为成功（current/forecast 分别跟踪，
  仅双 ok 推进时间戳/落盘，部分刷新失效可重试 + 状态行提示）；
  `153eef7` CI paths 补 `script/**`；`3475c9b` factory.ino TLS extern 改 `void`
  （issue_list §9）；申请 `c27cb39..3475c9b`
- **四份 Codex 结果齐至**：第三批 `6d26699..1473ef9` **A**（结果文件以清洗前
  旧哈希 `3f654a5..4c3a331` 命名——该批申请文件当时漏写，`eefb2fd` 补齐并
  配对闭合）；`de78338` 菜单幽灵页 **A**；`c27cb39..3475c9b` **A**；
  `a924c4e` SD 提示 **C 部分接受**（1 项 P2）
- **SD 提示 P2 修复**（`c8f62f3`，issue_list §3.4 跟进）："有卡但挂载失败"
  不能断言为 FAT32 格式问题——改两行提示 `SD hint: mount failed` +
  `try FAT16/FAT32?`（事实 + 建议），两处过度断言注释同步改准确；
  申请 `c8f62f3`

## 2026-08-21

- **第三批评评修复**（评审 `pda2-review-result-2026-08-07-20.md` 遗留 3 P2，全部关闭）：
  `6d26699` CI 尊重外部 `PLATFORMIO_SRC_DIR`（set_srcdir.py 不再覆盖矩阵选择）；
  `52f709e` GPS 写侧临界区（displayInfo 一次锁发布 11 字段 + getter 补锁）；
  `950fcfe` AI Config 补 Trust 自签 TLS 开关（NVS `ai`/`tls_insecure` 单键 +
  `\v` 键切换 + 开机 `openai_tls_apply()`）；台账 issue_list §7。申请
  `6d26699..1473ef9` 已递交
- **安全收尾（SECURITY.md checklist 全勾）**：`git filter-repo` 历史清洗
  （3 条替换规则，全对象库 blob 扫描 0 命中）→ `HD-V2-250915` force-push 且
  远程 blob 验证干净；OpenRouter key 作废旧 key、新 key 只存设备 `/env.cfg` +
  `data/env.cfg`（均 gitignored）；`config_keys.h` 清空为模板（OWM key/深圳坐标
  同日迁入 env.cfg）。旧→新 commit 映射：`.git/filter-repo/commit-map`
- **菜单幽灵页修复**（`de78338`，issue_list §8.1）：`page_num` 由页数改最大下标
  语义 `(MENU_BTN_NUM-1)/9`——18 项时第 2 页再左滑不再进入不存在的页 2
  （k3 设计评审 §1.1 顺带揭出的存量 bug）；申请已递交
- **笔友 App 设计两轮评审 → v2**：Codex 首轮（C 部分接受，已归档）+ Kimi k3
  二轮独立复核（C 部分接受，`8019da8` 归档，含对首轮 3 处失实引用的勘误）；
  v2 修订 `97e5d2f` 落实 3 条前置（菜单页数公式最大下标语义 / LLM 超时 180s /
  `s_pp_busy_gen`）+ 同批 8 项（结果 type 字段、env.cfg 容量、数据模型三处等）。
  **待复审**，通过后按 §9 预案实现
- **GPT 跟进评审到达**（`b4082ff`，`pda2-review-result-2026-08-07-21-gpt.md`）：
  3 项 P2 **待修（2026-08-22 批次）**——weather 部分刷新误存成功（issue_list §9.1）、
  CI paths 漏 `script/**`（§9.2）、factory.ino TLS extern 声明不一致（§9.3）；
  CA bundle 6 根 + NVS 双槽 11/11 复核通过

## 2026-08-19

- **收尾批次 1**（`764e7bf`/`6ce2b4b`/`980b6df`，第 31 轮 A 接受）：
  - usage 月度清零改**本地月界**：setup 设 `TZ=CST-8`，stats 用 `localtime()`
    （第 28 轮 O1 闭合）；Calendar/Sleep 时间戳同享该时区（此前全按 UTC）
  - WiFi 扫描覆盖层/结果 banner 在屏幕被 push 覆盖时隐藏（`exit4_1`，第 25 轮闭合）
  - `test_keypad` 加 raw/driver 列镜像换算注释 + README §2 说明（issue_list 1.4 闭合）
- **MP3 屏取消（硬件定论）**：4G 版板上无 PCM5102A DAC——`test_i2s_probe` 探针两轮
  实测（1s 提示音 + 60s 音频音量 0→21 渐变、`running=1`、耳机经电脑验证正常）均无声；
  I2S 引脚 7/8/9 被 A7682E（RI/ITR/RST）占用。issue_list §3.3、探针示例保留作验证记录
- **allinone 取消（用户决策）**：不新开整合固件，pda2 即最终形态；设计稿归档为参考；
  Keys 演示屏为唯一遗留候选（未拍板）
- **SD 卡格式提示**（`a924c4e`）：120GB exFAT 卡挂载失败显示 0MB 无解释 →
  About System 屏增加 `SD hint: need FAT32 / no card`（cardType 在 f_mount 失败后
  仍可区分空槽与坏格式）；串口同步打印原因。**SD 卡仅支持 FAT16/FAT32**

## 2026-08-18

- **AI Config provider 原生下拉**（`fd7be74`/`d22007d`，第 28/29 轮 A 接受）：
  lv_dropdown 6 项预设（openrouter/deepseek/minimax/qwen/tencent/custom）+ Alt+Enter
  循环；每 provider 独立 key（NVS `key.<provider>` → /env.cfg `<NAME>_KEY`）；usage
  统计升 V3（`reset_month` 月度清零，NTP 哨兵防冷启动误清）
- **用户反馈修正**（`da0217f`/`4c3c9b1`，第 29 轮 A 接受）：env 默认 key 改名
  `AI_KEY`→`OPENROUTER_KEY`；openrouter key 链补 config_keys.h 兜底；custom 选中
  清空三框；Save 不再写死存储 `key.custom`（第 30 轮，`a2ecd7b`）
- **评审流程修订**：申请文件名 = 本轮实际覆盖 commit 首末（含两端，非 git 区间记法），
  已接受 commit 彻底出列（含结果文件接受范围核对，曾漏看第 28 轮范围）
- **真机回归回填**：15 项清单 11 项 ✅（用户实测 2026-08-18；13 为代码级核验）

## 2026-08-17

- **Secrets 配置链**（`0e78025`）：真实 Key 从跟踪源码移除——NVS → SPIFFS `/env.cfg`
  → gitignored config_keys.h → 空默认；Weather key/坐标同链（默认深圳）；SECURITY.md
  重写（历史 Key 视为已泄露，推送前 filter-repo）
- **真机回归第一轮**（用户实测）：多轮记忆 ✅、P0 Sleep 三项 ✅（合并门禁满足）；
  重启恢复 ❌ → 根因 **SPIFFS rename 目标已存在时失败**（第二次保存起静默失败），
  `867435e` 改 bak 三步换入 + 诊断串口，待复测
- **Copilot 复审（844a907..8d273cd）8 项**（`3cdff38`）：usage 落盘生命周期检查点
  （深睡/离屏/New flush + 10s 失败退避）、chat.log 升 **CHL2**、ai_stats blob 升
  **V2**（旧数据迁移不丢失）、测试注入改 fail_at、上下文裁剪状态行提示
- **修复 Copilot 复审抓出的确定性逻辑错误**（`c90307f..8d273cd`）：
  - chat.log 加载器把尾部校验和当消息头 → 合法日志必被误判损坏（加记录数头字段）
  - 多轮上下文轮次配对条件写反 → 上下文从未生效（修正 orphan 判定）
  - usage 统计 mutex 惰性创建竞态 → 改静态初始化
  - Sleep frame-wait 超时仍深睡 → 改为取消并提示
- 草稿持久化全生命周期同步（exit/Clear/成功/New）；SPIFFS 不可用时状态行明示 RAM-only
- usage 统计：chat/test 两组分离 + 持久化节流（60s/20 次最多一写）
- 可执行 NVS 双槽算法测试 `scripts/test_nvs_atomic_save.py`（11/11 PASS）
- 文档第三轮修订：allinone-design 与预研最终实现对齐（多轮、持久化、New、usage、双槽、CA 5 根）；CLAUDE.md 存储布局；issue_list §5.7；docs/reviews/README 分段评审条款
- 申请扩展为 `844a907..8d273cd`（21 commit）

## 2026-08-16

- **用户追加需求**：AI Chat 多轮上下文（整轮配对 8KB，`openai_chat_multi`）、usage
  用量统计（容错解析 8 项入 NVS）、Hist→New 改名、Key 补偿控制 C1（编译期
  `#warning`）/C2（`SECURITY.md`）
- **第 20 轮评审整改**（`844a907..538e6d0` 前半）：双槽 NVS 原子保存（暂存-校验-
  单键翻转）、扫描临界区（portMUX + 读也加锁）、Sleep 倒计时帧序号机制（watcher
  timer，修复"等待旧屏帧 + LVGL 重入"）、destroy4 清 busy、AI Chat 动态正文
  （std::string + 16KB 预算 + 单点截断）+ SPIFFS 原子日志（tmp+校验和+rename，
  不自动格式化）+ 重试复用气泡 + New 确认框 + 草稿持久化、AI Config 计费提示与
  Save 失败原因、CA 脚本 openssl 依赖检查
- **合并申请流程**：round 19/20 合并为 `eecebda..ceade9c`，`docs/reviews/README.md`
  固化合并与追溯规则
- **第 17/18 轮评审整改**（`bb1819b..e210b46`）：Sleep 屏倒计时 + timer 句柄保存、
  扫描 release-pending 目标计数、WiFi Test/Time Sync busy 代次、CA 检查脚本转
  Python（字节级提取，5 根证书 PASS）、AI Test 改最小 chat-completion（15s 绝对
  deadline + 超时递增代次）、AI Chat 每任务快照 + UTF-8 截断回退、异步 IPC 契约
  文档（`docs/async_ipc_contract.md`）
- **第 8-16 轮评审整改**（`01f8eac..8b96656` 及之前）：AI Text 聊天界面重构
  （WeChat 式气泡 + 滚动历史）、AI Config 三输入框 + Save/Test 语义、CA bundle
  修损坏的 ISRG X1 并扩至 5 根、CST-8 时区、Sleep 屏（ext1 BOOT 键唤醒）、
  WiFi 扫描生命周期（SCAN_DONE 事件计数 + 代次）、状态栏时间跟随电量刷新
- 键盘驱动（`3d98321`/`6a9ab00`/`2e559ad`/`6c51964`）：HD-V2 实测矩阵解码（无 Ctrl、
  双 Shift、Alt 临时符号层、Sym 锁定）、音量键 `'\v'`/麦克风 `'0'` Sym 层映射、
  TCA8418 `INT_STAT` W1C 溢出恢复、页面切换清按键 FIFO、触摸焦点同步

## 2026-08-15

- 搭建 pda2 编译环境（PlatformIO 6.1.19，`python -m platformio`）并首次烧录上机
- 建立评审工作流：申请带 commit id、按模块拆 commit、申请/结果归档 `docs/reviews/`
- 键盘问题定位与真机按键实测（配合用户逐键解码）
- WiFi 配置屏首版（下拉扫描 + 密码框 + NVS 存储）
