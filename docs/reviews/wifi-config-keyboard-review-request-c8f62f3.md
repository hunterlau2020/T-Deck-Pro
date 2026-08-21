# 评审申请书：a924c4e 评审 P2 修复（SD 提示不作诊断断言）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个）：
  - `c8f62f3` — `pda2: SD hint states the failure, offers FAT32 as advice`
- **背景**：Codex 结果 `wifi-config-keyboard-review-result-a924c4e-codex.md`
  （2026-08-21）**C 部分接受**，1 项 P2；本 commit 关闭该 P2，评审出列。
- **命名说明**：文件名 = 正文"关联 commit"（含两端，非 git 区间记法）。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 问题（评审 P2 原文要点）

`SD.begin()` 失败而 `SD.cardType() != CARD_NONE` 并不能证明卡是 exFAT/NTFS——
`cardType()` 只反映卡类型/有无应答；SPI 通信、初始化、甚至 FAT32 卡自身的挂载
错误同样进入该分支。原提示 `SD hint: need FAT32` 把这些情况统一断言为格式
问题，可能误导用户格式化一张正常的 FAT32 卡。

> **双评审分歧登记**（2026-08-22 Kimi 结果到达后补注）：Kimi
> `wifi-config-keyboard-review-result-kimi-a924c4e.md` §1.2 认为 `need FAT32`
> 是"指导性建议而非精确诊断，所有落此分支的场景下都是正确动作，不构成误导"
> ——与 Codex P2 结论相反。本修复按更严格的 Codex 口径执行（事实 + 建议两行，
> 无害于 Kimi 认可的原语义）；两说并存，供本轮评审人裁量。

### 1.2 修复（`c8f62f3`，纯文案 + 注释，无逻辑改动）

- About System 屏 state==2 提示由一行 `need FAT32` 改为两行——
  事实行 `SD hint: mount failed` + 建议行 `try FAT16/FAT32?`
  （同时满足评审给的两种措辞：中性事实 + 建议式，不再当确定诊断）；
  两行均按 `line_full_format(28, ...)` 预算（8+12、0+17 字符）。
- `ui_deckpro_port.cpp` 两处过度断言的注释改准确：
  - state 文档注释：2 = 有卡但挂载失败——**典型**是非 FAT16/32 文件系统，
    但 FAT32 卡的 SPI/初始化错误也会落此分支；原因只在串口日志，
    调用方不得把 2 当诊断；
  - `cardType()` 判定注释：它区分的是"有卡/空槽"，**不能**证明失败原因
    是文件系统类型。
- 串口日志不变（本来就打印真实 `error` + `cardType`，是事实性输出）。
- state 语义本身不变（0/1/2），调用签名不变。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS（17.7s，无新警告） |
| 烧录 | ✅ | COM5，SUCCESS |
| 开机冒烟 | ✅ | 串口 45s：EPD 刷新、触摸上报正常、无 panic |
| 提示行排版 | ⏸ | 两行均在 28 字符预算内（静态推算）；真机视觉确认见 §3-1 |

## 3. 真机回归清单

**本轮新增**
1. ⏸ 插 exFAT 卡进 Setting/About System：SD total/used 0MB，提示两行
     `SD hint: mount failed` / `try FAT16/FAT32?`，排版不折行不溢出；
     串口 `[SD] capacity query failed: ... (cardType=SDHC)`（不变）
2. ⏸ 拔卡重进屏：`SD hint: no card`（state 1 路径不受影响）

**继承（未回归项顺延）**
3. ⏸ Weather 完整刷新正常路径；（可选）forecast URL 改错的部分刷新提示
4. ⏸ AI Config Trust 开关触摸/`\v` 键/重启保持
5. ⏸ GPS 屏读数刷新 + altitude 不再恒 0
6. ⏸ 菜单第 2 页左滑不再空滑；SD FAT32 重格式化后显示容量；#6/#14/#15

## 4. 遗留项（简要）

- **四份 Codex 结果全部处理完毕**：第三批 `6d26699..1473ef9`（A）、
  `de78338`（A）、`c27cb39..3475c9b`（A）、`a924c4e`（C，P2 由本申请关闭）
  ——至此 `docs/reviews/` 无待结果申请（本份除外）。
- 第三批申请文件补写（`eefb2fd`）与其旧哈希命名结果的配对已闭合。
- 笔友 App：设计 v2（`97e5d2f`）待复审，复审通过后按 §9 预案实现。
- CI 矩阵日志抽查（验证 set_srcdir 修复真实生效）仍待做。

## 5. 回滚方案

```bash
git revert c8f62f3
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
