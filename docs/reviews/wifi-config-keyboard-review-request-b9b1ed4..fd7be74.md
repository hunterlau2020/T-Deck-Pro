# 评审申请书：AI Config provider 下拉 + usage 月度清零（用户需求）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-18
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个，未评审）：
  - `fd7be74` — `pda2: AI Config provider presets + base suffix + monthly usage reset`
- **历史范围**：`e08bdac..b9b1ed4` 已由 Codex 全量接受（见
  [评审结果](wifi-config-keyboard-review-result-codex-e08bdac..b9b1ed4.md)），本申请
  只覆盖其后未评审的 commit。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细（`fd7be74`）

### 1.1 AI Config provider 下拉（用户需求 3/4/5）

- Base 框上方新增 **Provider 行**：`openrouter / deepseek / minimax / qwen / tencent / custom`；
  点击行或 **Alt+Enter** 循环切换
- 选中预设即自动填 base/model 输入框（用户可再改）；`custom` 不动框内容
- 存储的 base 是**供应商根地址**（如 `https://api.deepseek.com/v1`）；调用时由
  `openai_chat` 自动追加 `/chat/completions`（已含后缀的旧 NVS 值不受影响）
- 选择 **openrouter** 且 Key 框为空、NVS 无 key 时：从 `/env.cfg` 读
  `OPENROUTER_KEY`（回退 `AI_KEY`）填入 Key 输入框
- 预设表：openrouter=`deepseek/deepseek-v4-flash-0731`；deepseek=`deepseek-v4-flash`；
  minimax=`MiniMax-M3`；qwen=`qwen3.7-plus`；tencent=`hy3`
- 进屏时按已存 base 匹配预设高亮（仅显示，不覆盖已存值）

### 1.2 usage 月度清零（用户需求 6）

- 统计 blob 升 **V3**：新增 `reset_month`（YYYYMM）字段，V1/V2 blob 自动迁移不丢失
- 每次加载时比较 NTP 时间：跨月即清零（置 dirty 立即落盘）；**NTP 未同步时跳过检查**，
  冷启动不会误清零

### 1.3 附带

- 开发目录 env.cfg 备份（真实值，`.gitignore` 已加规则，永不提交）
- `config_keys.h.example` 补 `AI_KEY_DEFAULT_DEV` 占位；Shutdown 屏 "Shoutdown" 拼写修正；
  TODO 勾选已完成项

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified |
| NVS 算法测试 | ✅ | `scripts/test_nvs_atomic_save.py` 11/11 PASS（沿用） |
| CA bundle | ✅ | 6 根证书 PASS（沿用） |
| 真机回归（§3） | ⏸ | 完整清单如下，逐轮回填 |

## 3. 真机回归清单（P1 整改：完整待测项，含前两轮继承）

**本轮新增**
1. ⏸ Provider 行显示与切换（点击 / Alt+Enter 循环 6 项）；选中 deepseek → base/model 自动填
2. ⏸ 选 openrouter 且 Key 框空 → Key 自动填入（串口 `[AICfg] key filled from env.cfg`）
3. ⏸ 选 deepseek 后 Test → 正常通过（base 自动补 `/chat/completions`，串口无 404）
4. ⏸ custom 选中 → base/model 框保持不动
5. ⏸ usage 月度清零：Usage 弹窗数据正确；跨月清零逻辑（代码级，等 9 月自动验证）

**上轮 b9b1ed4**
6. ⏸ Weather `r` 键 → `Fetching...` → 数据更新
7. ⏸ Weather 城市显示深圳（不再是 San Carlos）
8. ⏸ Shutdown 四路径：Enter=关机；任意键=返回菜单；Cancel 按钮=返回；返回键=返回
9. ⏸ USB 插入时进 Shutdown → 只显示提示无关机

**Weather/Secrets 继承**
10. ⏸ Weather 三页内容（Current/Hourly/5-Day）+ `+`/`-` 翻页 + `UV:--`
11. ⏸ 无 GPS 时深圳回退（串口 `Using config: lat=22.5431`）
12. ⏸ 空 NVS 时 AI Key 走 config_keys.h（AI Config Key 框有值）

**AI 历史回归（继承）**
13. ⏸ 失败重试路径：关热点发送 → 等待层消失 + 文本回填 + 气泡 `(failed)` → 重开热点重发成功
14. ⏸ 长回答 >4KB → `(truncated)` 无乱码

## 4. 遗留项（简要）

- Key：HEAD 无真实 Key（配置链 NVS→env.cfg→config_keys.h）；git 历史残留 → 推公网前
  filter-repo + OpenRouter 轮换（SECURITY.md）
- SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 → TODO 阶段 1

## 5. 回滚方案

```bash
git revert fd7be74
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
