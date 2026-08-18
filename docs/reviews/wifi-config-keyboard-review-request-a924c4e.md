# 评审申请书：Setting 屏 SD 挂载失败原因提示（用户需求）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-19
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个，未评审）：
  - `a924c4e` — `pda2: Setting screen explains SD mount failures`
- **命名说明**：文件名 = 本轮实际覆盖 commit（含两端，非 git 区间记法）；
  评审范围以正文列表为准。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，已烧录）

---

## 1. 变更明细

### 1.1 需求背景（用户反馈）

- 用户插入 120GB exFAT 卡，About System 屏 "SD total: 0MB" 无任何解释；
  串口报 `f_mount failed: (13) There is no valid FAT volume`。ESP32 SD 库
  （FATFS）仅支持 FAT16/FAT32，不支持 exFAT/NTFS。

### 1.2 实现（`a924c4e`）

- `ui_setting_get_sd_capacity(total, used)` 增加第三出参 `state`：
  - `0` 挂载成功；`1` 未检测到卡；`2` 有卡但文件系统非 FAT16/FAT32
- 失败区分依据：`SD.cardType()` 在 f_mount 失败后仍保留检测到的卡类型
  （卡已应答初始化命令），与空槽（CARD_NONE）区分；失败原因 + cardType
  同步打印串口 `[SD] capacity query failed: ...`
- About System 屏：挂载失败时在 SD used 行下追加提示行
  `SD hint: need FAT32` / `SD hint: no card`（屏幕现有文案为英文，保持一致）
- 调用点仅 1 处（`ui_deckpro.cpp` create2_1），签名变更已同步头文件

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified |
| 真机回归 | ⏸ | 见下方清单（本轮 3 项） |

### 真机回归清单

1. ⏸ 插入 exFAT 卡 → Setting/About System → SD total/used 均 0MB，下方显示
     `SD hint: need FAT32`；串口 `[SD] capacity query failed ... cardType=SDHC`
2. ⏸ 拔出卡 → 重进屏 → 显示 `SD hint: no card`
3. ⏸ 卡格式化 FAT32 后插回 + 重启 → 显示真实容量（~114GB），无 hint 行

## 3. 遗留项（继承，简要）

- M1：3 份评审结果文档明文旧 Key 掩码（推公网前）
- 挂起：exit9/entry9 对称性；双 Enter 静默窗
- 收尾：SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏
- 真机回归继承项：Save 后 key 恢复、失败重试路径、长回答 `(truncated)`

## 4. 回滚方案

```bash
git revert a924c4e
```

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
