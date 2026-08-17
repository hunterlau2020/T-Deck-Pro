# 评审申请书：第 29 轮 Info 小项闭合（key.custom 死存储 + options/s_providers 同步）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-18
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个，未评审）：
  - `a2ecd7b` — `pda2: AI Config - skip per-provider key save for custom + sync note`
- **命名说明**：文件名 = 本轮实际覆盖 commit（含两端，非 git 区间记法）；
  评审范围以正文列表为准。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 Save 不再写 `key.custom`（第 29 轮 §1.1 Info ①）

- 原守卫 `p->name[0] != '\0'` 对 custom 也为真（name="custom"），Save 会写入
  `key.custom`，但 custom 的 key 永不被读回（apply 的 custom 分支直接清空三框，
  不读 NVS）——死存储。
- 改为 `p->base[0] != '\0'`：与 `ai_provider_apply` 的 custom 判别一致，custom
  下 Save 不再落 per-provider key。行为影响：custom 时改 key 并 Save，切走再切
  回 custom 仍是空框（与"custom = 从头开始"语义一致，无回归）。

### 1.2 下拉 options 与 s_providers 同步标注（第 29 轮 §1.1 Info ②）

- 两处加 `KEEP IN SYNC` 注释（数组定义处 + `lv_dropdown_set_options` 处）：
  名称与顺序必须一致，dd_cb 按 selected 索引直接查 s_providers。纯注释，无
  行为变化。

## 2. 闭环登记（第 29 轮跟踪项，无新代码）

| 项 | 处理 |
|---|---|
| §1.4 Low：申请书历史引用未前移 + 编号重复 + §1 倒序 | `9cb6d25` 已修正（fd7be74 出列、历史范围补第 28 轮、编号 1-15 顺排、§1 重排） |
| §1.2 现场操作：设备 `/env.cfg` 条目名改 `OPENROUTER_KEY` | 设备 SPIFFS 上**无** `/env.cfg`（从未上传，仅开发目录备份），无需操作；备份已改名 ✅ |

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified |
| 真机回归 | ⏸ | 继承第 29 轮 15 项待用户；本轮新增 1 项代码级（custom Save 不写 key.custom，无可见 UI 变化） |

## 4. 遗留项（继承，简要）

- M1：3 份评审结果文档明文旧 Key 掩码（推公网前）
- 挂起：wifi_scan_overlay exit4_1 hide；exit9/entry9 对称性；双 Enter 静默窗
- 阶段 1：SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏

## 5. 回滚方案

```bash
git revert a2ecd7b
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
