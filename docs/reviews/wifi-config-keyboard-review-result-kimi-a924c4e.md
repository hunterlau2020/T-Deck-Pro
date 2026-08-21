# 评审结果：Setting 屏 SD 挂载失败原因提示（a924c4e）

- **评审日期**：2026-08-22
- **申请书**：[wifi-config-keyboard-review-request-a924c4e.md](wifi-config-keyboard-review-request-a924c4e.md)
- **hash 映射**：申请文件名 `a924c4e` 为 filter-repo 历史重写前 id，当前 HEAD 对应
  commit 为 **`23030c9`**（"pda2: Setting screen explains SD mount failures"），
  本结果以当前 hash 复核。
- **评审人**：Kimi
- **评审结论**：**A. 全量接受**

---

## 1. Findings

### 1.1 失败区分机制成立，实现与声明一致

- **严重性**：✅ 通过
- **位置**：`23030c9` → `examples/pda2/ui_deckpro_port.cpp:336-380`、`ui_deckpro.cpp:973-994`
- **验证**：
  - `ui_setting_get_sd_capacity(total, used, state)` 三出参，空指针防御齐全
    （total/used/state 各自判空）；
  - `SD.cardType()` 在 `f_mount` 失败后保留卡类型的机制与申请书描述一致，代码内
    注释记录了该依赖（"the card answered the init commands"）；
  - 调用点确仅 1 处（`create2_1`），头文件签名已同步（`ui_deckpro_port.h:81`）；
  - 串口日志 `[SD] capacity query failed: ... (cardType=...)` 含原因 + 卡类型，
    与申请书 §2 回归清单的期望输出吻合；
  - hint 文案英文，与 About System 屏现有文案一致。

### 1.2（观察，Low）state=2 实际覆盖所有非 FAT 挂载失败

- **位置**：`ui_deckpro_port.cpp:366-369`
- **说明**：`cardType != CARD_NONE` 即判 state=2，除 exFAT/NTFS 外也包含"FAT32 卡但
  分区表损坏"等情况，提示文案 `need FAT32` 是指导性建议而非精确诊断。对用户而言
  该建议在所有这些场景下都是正确动作，不构成误导，登记备查即可。

## 2. 验证说明

- 本评审为静态代码复核（diff 级），未独立编译/烧录；编译与烧录状态采信申请书
  §2（pio SUCCESS + COM5 Hash verified）。
- 真机回归 3 项（exFAT 提示 / 拔卡提示 / FAT32 恢复容量）仍为 ⏸，按申请书
  清单逐轮回填，不阻塞本结论。

## 3. 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

代码正确、范围克制（3 文件 +27/-3）、错误路径与日志完备。SD 提示屏可并入后续
真机回归批次统一验证。
