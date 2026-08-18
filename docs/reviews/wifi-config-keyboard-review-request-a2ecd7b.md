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
| 真机回归 | 部分 ✅ | 下方清单：2026-08-18 用户实测 11 项通过，6/14/15 待测 |
| 本轮新增 1 项 | ✅（代码级） | custom Save 不写 `key.custom`，无可见 UI 变化 |

### 真机回归清单（继承第 29 轮 15 项，2026-08-18 回填）

**d22007d..4c3c9b1（第 29 轮）新增**
1. ✅ Provider 下拉显示与切换（点击 / Alt+Enter 循环 6 项）；选 deepseek → base/model 自动填
2. ✅ 选 openrouter → Key 自动填入（串口 `[AICfg] key for openrouter loaded`）
3. ✅ 选 deepseek 后 Test → 通过（base 自动补 `/chat/completions`，无 404）
4. ✅ custom 选中 → base/model/key 三框清空
5. ✅ Usage 弹窗数据正确（跨月清零逻辑代码级，等 9 月自动验证）
6. ⏸ Save 后切走再切回同一 provider → key 从 NVS `key.<provider>` 恢复

**上轮 b9b1ed4（第 27 轮）**
7. ✅ Weather `r` 键 → `Fetching...` → 数据更新
8. ✅ Weather 城市显示深圳（不再是 San Carlos）
9. ✅ Shutdown 四路径：Enter=关机；任意键=返回菜单；Cancel 按钮=返回；返回键=返回
10. ✅ USB 插入时进 Shutdown → 只显示提示无关机

**Weather/Secrets 继承**
11. ✅ Weather 三页内容（Current/Hourly/5-Day）+ `+`/`-` 翻页 + `UV:--`
12. ✅ 无 GPS 时深圳回退（串口 `Using config: lat=22.5431`）
13. ✅（代码级） 空 NVS 时 AI Key 走 config_keys.h——第 29 轮 §1.3 四级链核验通过；
    实测需擦除 NVS（连带丢失 WiFi 配置），不做破坏性验证
14. ⏸ 失败重试路径：关热点发送 → 等待层消失 + 文本回填 + 气泡 `(failed)` → 重开热点重发成功
15. ⏸ 长回答 >4KB → `(truncated)` 无乱码

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
