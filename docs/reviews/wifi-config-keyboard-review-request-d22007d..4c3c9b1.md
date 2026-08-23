# 评审申请书：AI Config provider 原生下拉重构 + OPENROUTER_KEY 改名 + custom 清空

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-18
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个）：
  - `d22007d` — `pda2: AI Config - native dropdown provider selector + per-provider keys`（用户反馈）
  - `da0217f` — `pda2: secrets - rename env default key AI_KEY -> OPENROUTER_KEY`（用户反馈）
  - `4c3c9b1` — `pda2: AI Config - custom clears fields + key chain config_keys fallback`（用户反馈）
- **评审状态**：第 29 轮 Codex **A 全量接受**，本申请闭环（见
  [评审结果](wifi-config-keyboard-review-result-codex-d22007d..4c3c9b1.md)）。
- **已出列范围**（均 Codex 全量接受）：
  - `e08bdac..b9b1ed4`（第 27 轮）— [结果](wifi-config-keyboard-review-result-codex-e08bdac..b9b1ed4.md)
  - `b9b1ed4..fd7be74`（第 28 轮，含 `fd7be74` provider 预设 + usage 月度清零）—
    [结果](wifi-config-keyboard-review-result-codex-b9b1ed4..fd7be74.md)
- **命名说明**：文件名 = 正文"关联 commit"的首末 id（**含两端**，非 git 区间记法）；
  评审范围以正文列表为准。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

> `fd7be74`（provider 预设 + usage V3 月度清零）已随第 28 轮接受出列，明细见
> [第 28 轮结果](wifi-config-keyboard-review-result-codex-b9b1ed4..fd7be74.md)。

### 1.1 Provider 原生下拉 + 每 provider 独立 key（`d22007d`）

- 行内循环按钮改为**原生 lv_dropdown**（触摸展开列表），Alt+Enter 键盘循环保留
- **每 provider 独立 key**：切换时按 NVS `key.<provider>` → /env.cfg `<NAME>_KEY`
  （OPENROUTER/DEEPSEEK/MINIMAX/QWEN/TENCENT_KEY）→ 空框；Save 把 key 存入当前
  provider 名下，切走再切回自动恢复
- 下拉在进屏时先按已存 base 设置选中**再**挂 change 回调——初始化不会覆盖已存值

### 1.2 用户反馈修正（`da0217f` + `4c3c9b1`）

- /env.cfg 默认 key 改名 `AI_KEY` → `OPENROUTER_KEY`（`openai_load_config` 与
  provider 链统一读 `OPENROUTER_KEY`，示例/注释同步）——设备无 /env.cfg 时
  切换 openrouter 不再显示空 Key
- openrouter 的 key 链补最后一级：gitignored `config_keys.h` 的
  `AI_KEY_DEFAULT_DEV`（此前链到 env 即止）
- `custom` 选中时**清空** base/model/key 三框及草稿缓冲（此前保持不动，用户要求
  "custom = 从头开始"）

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified |
| NVS 算法测试 | ✅ | `scripts/test_nvs_atomic_save.py` 11/11 PASS（沿用） |
| CA bundle | ✅ | 6 根证书 PASS（沿用） |
| 真机回归（§3） | ⏸ | 完整清单如下，逐轮回填 |

## 3. 真机回归清单（P1 整改：完整待测项，含前两轮继承）

**d22007d..4c3c9b1（本轮）**
1. ⏸ Provider 行显示与切换（点击 / Alt+Enter 循环 6 项）；选中 deepseek → base/model 自动填
2. ⏸ 选 openrouter → Key 自动填入（NVS key.openrouter → env → config_keys 兜底，
     串口 `[AICfg] key for openrouter loaded`）
3. ⏸ 选 deepseek 后 Test → 正常通过（base 自动补 `/chat/completions`，串口无 404）
4. ⏸ custom 选中 → base/model/key 三框清空（`4c3c9b1` 起）
5. ⏸ usage 月度清零：Usage 弹窗数据正确；跨月清零逻辑（代码级，等 9 月自动验证）
6. ⏸ Save 后切走再切回同一 provider → key 从 NVS `key.<provider>` 恢复

**上轮 b9b1ed4**
7. ⏸ Weather `r` 键 → `Fetching...` → 数据更新
8. ⏸ Weather 城市显示深圳（不再是 San Carlos）
9. ⏸ Shutdown 四路径：Enter=关机；任意键=返回菜单；Cancel 按钮=返回；返回键=返回
10. ⏸ USB 插入时进 Shutdown → 只显示提示无关机

**Weather/Secrets 继承**
11. ⏸ Weather 三页内容（Current/Hourly/5-Day）+ `+`/`-` 翻页 + `UV:--`
12. ⏸ 无 GPS 时深圳回退（串口 `Using config: lat=22.5431`）
13. ⏸ 空 NVS 时 AI Key 走 config_keys.h（AI Config Key 框有值）

**AI 历史回归（继承）**
14. ⏸ 失败重试路径：关热点发送 → 等待层消失 + 文本回填 + 气泡 `(failed)` → 重开热点重发成功
15. ⏸ 长回答 >4KB → `(truncated)` 无乱码

## 4. 遗留项（简要）

- Key：HEAD 无真实 Key（配置链 NVS→env.cfg→config_keys.h）；git 历史残留 → 推公网前
  filter-repo + OpenRouter 轮换（SECURITY.md）
- SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 → TODO 阶段 1

## 5. 回滚方案

```bash
git revert 4c3c9b1 da0217f d22007d
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
