# 2026-08 commits 全量评审结果（Kimi）

- **评审日期**：2026-08-22 启动，2026-08-28 收尾（期间子代理额度 403 中断两轮）
- **范围**：`--since=2026-08-01` 全部 185 个 commit：89 个 docs 簿记不审，
  **96 个代码类 commit 全部审完**（90 个经 5 批次代理 + 6 个经 4 份申请评审
  单独通过：23030c9/6d26699/52f709e/950fcfe/de78338/c27cb39/153eef7/3475c9b
  中的代码项）。
- **方法**：按子系统分 5 批，逐 commit `git show` 复核 + 对照 HEAD 验证存活，
  异步改动对照 `docs/async_ipc_contract.md` 规则。全部静态评审，未独立编译/烧录。
- **总评**：**当前 HEAD 无遗留 High 缺陷**。8 月的开发呈现清晰的"问题引入→
  当轮/次轮修复"自愈模式，异步 IPC 契约是迭代踩出来的，最终态合规。
  **存活待办：6 Medium + 11 Low + 2 契约层登记**，已全部入账
  `docs/issue_list.md` §11/§11.1（含修复建议）。

## 1. 批次结论一览

| 批次 | 主题 | commit 数 | 结论 | HEAD 存活 |
|---|---|---|---|---|
| B1 | WiFi/扫描/WiFi Test/TLS/CA/NTP | 20 | 契约轨迹 f51c83e→71de56e→a0f6f01+6f2bbe7 收敛良好 | 2 Medium + 2 Low |
| B2 | AI Config/双槽 NVS/统计/secrets 链 | 24 | 中间轮 5 Medium 全闭环；HEAD 契约规则 1-10 全过、缓冲区全有界 | 1 Medium + 4 Low |
| B3 | AI Chat/任务快照/SPIFFS/多轮上下文 | 21 | 契约迭代收敛（9 项竞态全修复）；SPIFFS 崩溃窗完备 | 3 Low |
| B4 | 睡眠/键盘/天气迁移/菜单/构建 | 18+3 | 4 条引入→修复链全闭环 | 2 Low（1 项并入 B1-M1） |
| B5 | phase-0 试点（78efe2e、9bffa78） | 2 | 奠基批 6 缺陷 4 项已修 | 2 Medium + 2 Low |

各批逐 commit 明细与证据见
[2026-08-commits-review-status.md](2026-08-commits-review-status.md) §2.2-§2.6。

## 2. HEAD 存活 Medium（6 项，按建议优先级）

1. **B5-M1 菜单无键盘导航**：`9bffa78` 宣称的 '1'..'8' 映射 diff 里不存在；
   菜单纯触摸/手势（`ui_deckpro.cpp:279`）——触屏失效时所有屏不可达。
2. **B1-M2 `exit4_1` 遮挡不卸键盘/扫描**（`1ba2a4b`）：被覆盖屏偷吃按键
   （FIFO 先到先得），扫描完成还会在覆盖屏上重建 layer_top banner。
3. **B1-M1 扫描 abort pending 可卡死**（`7d5aa8d`/`55b5046`，
   `ui_deckpro.cpp:2336` 一带）：SCAN_DONE 先于 publish 计数则卡死，之后每次
   扫描 3s 阻塞 + "Scan busy"，靠 4.2 屏阻塞扫描意外解锁。
4. **B2-M1 月度用量重置只在开机首载评估**（`cb98b16`）：跨月不重启则永不清零；
   月份检查移入 `openai_stats_poll()` 即可。
5. **B5-M2 WiFi 连接+NTP 主循环阻塞 ~23s**（`ui_deckpro.cpp:2048-2077`）：
   audio/GPS/电源维护饿死——异步连接状态机。
6.（契约层）**weather fetch 任务在契约之外且被 `vTaskDelete` 强杀**
   （先于 8 月存在）：栈上 HTTPClient/Preferences 自动对象连带释放——
   入契约或显式登记例外。

## 3. HEAD 存活 Low（11 项，摘要）

- B1：`keypad_clear_chars()` 在 8s NTP 等待之前（`c416841`）；契约规则 10
  UI 绝对超时对 WiFi Test/Time Sync 未实现。
- B2：`env_secrets` 懒加载双核竞态（`21df8cd`）；切 provider 丢草稿无提示；
  旧 `AI_KEY=` env.cfg 行静默失效（`2e6ba8e`）；destroy 落盘 `portMAX_DELAY`
  抢锁卡顿（`0f73b85`）。
- B3：New 确认框 push-away 泄漏；Chat 页缺 `c >= ' '` 守卫；EPD 抑制期全刷
  被降级（自愈）。
- B4：天气 'r' 键先清时间戳（`fbfc16c`，修复前先对照 HEAD 复核——weather
  后续有 `c27cb39`/`71fa528`/`141942d` 三轮改动）。
- B5：HTTP 响应体无界入内存（`http_utils.cpp:276`）；`http_utils.h` CA 注释
  漂移。

## 4. 过程记录

- 评审期间发现另一并行会话持续推进实现与评审（PenPal 已落地并经 Codex 评审、
  issue_list §10/§12-14），本文档与 issue_list/TODO 的写入均已与其变更合并。
- 子代理额度 403 曾两轮中断（2026-08-22 日配额、2026-08-28 周配额提示），
  全部批次最终跑完，无批次漏审。
- 本评审与 Codex/GPT/Copilot 的既有评审轮次互补：它们按申请批次增量评审，
  本评审做 8 月全量回溯 + HEAD 存活核对，发现的 6 项存活 Medium 均为增量评审
  未覆盖的存量问题。
