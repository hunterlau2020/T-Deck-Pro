# Changelog

本文件记录 pda2 预研（T-Deck-Pro HD-V2，分支 `HD-V2-250915`）的主要工作。
评审细节见 `docs/reviews/`（每轮 = 申请 + 双评审结果，按 commit 范围命名）。

## 2026-08-17

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
