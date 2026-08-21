# 评审申请书：PenPal 首轮真机回归修复（崩溃 / 菜单翻页 / WiFi Test）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个，按模块拆分）：
  - `423b312` — `penpal: fix Back-press stack overflow (pp_state_t temp on stack)`
  - `e70b591` — `wifi: test result shows the device LAN IP next to the public IP`
  - `bfa7a16` — `menu: one page per swipe (gesture poll edge-detect)`
- **背景**：acc3893 烧录后首轮真机回归（issue_list §12）：①PenPal 返回
  按钮**每次点击必崩**（重启）；②主菜单滑动一次从第 1 页跳到第 3 页；
  ③用户要求 WiFi Test 显示设备网卡 IP。三处同日修复，用户真机复测通过。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，COM5，
  已连接、已烧录复测）

---

## 1. 变更明细

### 1.1 `423b312` penpal — 返回即重启（P0，每次必现）

- **证据链**：串口抓到 `Debug exception reason: Stack canary watchpoint
  triggered (loopTask)`；崩溃点在 `scr_mgr_pop` 链内（其开头的
  `[KBD] char fifo cleared` 已打印）。第一轮修复尝试（把 pop 用
  `lv_async_call` 延迟出点击事件栈）后仍然崩溃 → 定位到爆栈与调用深度
  无关，而是 `pp_destroy` 的 `pp = pp_state_t()` 本身
- **根因**：`pp_state_t` ≈ **~15KB**（mailbox 24 行×160B + topics
  16×368B + letters 64×~68B + idem_snap/fmt 等）；`x = T()` 会在栈上
  先物化整个临时对象再拷贝赋值——loopTask 栈共 8KB，任何调用深度必炸。
  该写法随 PenPal 实现引入（b231dd3），Codex 实现评审（§10）未覆盖
- **修复**：
  - `pp_state_reset()` 逐字段原地复位；数组**逐元素值初始化**（单元素
    临时最大 ~370B：`pp_topic_t`），字符串走元素析构，不 memset（§10
    规则延续）
  - 保留 `lv_async_call` 延迟 pop（第一轮尝试的遗留，独立有益：点击
    事件栈先回退再跑拆链；双击重入由 `scr_mgr_pop` 的 top==root
    拒绝兜底）
  - `pp_destroy` 末尾加 `[PenPal] destroy done` 串口标记（回归观察点）
- **规则沉淀**（issue_list §12）：UI 线程禁止对大聚合做 `= T()` 整体
  赋值；全库扫描 `= *_t{}` 模式，其余均为 ≤400B 小结构（工作线程/浅
  调用），安全

### 1.2 `e70b591` wifi — Test 结果增显示 LAN IP（用户需求）

- 结果框 `Public IP:\n<ip>` → `Public IP:\n<ip>\nLAN IP:\n<lan>`；
  串口 `[WiFiTest] public ip: ... lan ip: ...`；section 头部补
  `#include <WiFi.h>`（该段位于文件中部 1815 行既有 include 之前）
- 动机：排查 PenPal 测试服务器可达性时需要直接看到设备所在网段

### 1.3 `bfa7a16` menu — 滑动一次跳两页（1→3 跳过 2）

- **根因**：`indev_get_gesture_dir` 每 30ms 轮询
  `lv_indev_get_gesture_dir`，而 LVGL 的 `gesture_dir` 从手势检测起
  **持续保持到下一次按下**才复位（`lv_indev.c` 按下起点 reset +
  `gesture_sent` 每次按压单发）→ 一次滑动期间回调连发 N 次。两页时代
  边界钳位（0↔1）掩盖了连发；三页即暴露
- **修复**：边沿触发——仅 `NONE → LEFT/RIGHT` 跃迁时回调一次（每手势
  恰好一帧）；顺带给 `ui_get_gesture_dir` 加 NULL 防护（菜单 exit 后
  该指针置 NULL，非菜单页发生手势本会空调用函数指针）

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` SUCCESS（RAM ~49.9%，无新警告） |
| 烧录 | ✅ | COM5，每轮修复后均重烧 |
| 开机冒烟 | ✅ | 多轮 25-45s 串口：无 panic/无 `rst:0xc` |
| ① 返回崩溃 | ✅ 用户实测 | 串口 `[PenPal] destroy done` 打出、无重启；点击与键盘 `\b` 两路径均过 |
| ② 菜单翻页 | ✅ 用户实测 | 1→2→3 逐页、双向反向滑均正常 |
| ③ WiFi Test | ✅ 用户实测 | 结果框显示 Public + LAN 两段 |
| PenPal sync 回归 | ⏸ | 被服务器侧阻塞：uvicorn 需 `--host 0.0.0.0` 重启（现为 127.0.0.1）+ 防火墙 8000 入站；设备端 env.cfg 已就绪（`PENPAL_BASE=http://192.168.3.186:8000`） |

## 3. 遗留项（简要）

- 其他屏的**点击**返回与 PenPal 共用 `scr_mgr_pop` 同步拆链模式
  （树更小未触发；PenPal 已加 async 延迟）。如评审认为应统一，可后续
  把 `scr_back_btn_create` 的 pop 类回调统一走 async——本轮未动，避免
  一次性改所有屏
- sync 及 acc3893 的 4 项修复路径回归合并执行（待服务器侧就绪）
- 待结果申请清点：`c8f62f3`、`141942d`、`acc3893` + 本份

## 4. 回滚方案

```bash
git revert bfa7a16 e70b591 423b312
```

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
