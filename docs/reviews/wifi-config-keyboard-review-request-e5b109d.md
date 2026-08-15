# 评审申请书：AI 配置 Test 按钮可达性与字段校验（第十四次申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮整改，与文件名对应）：
  - `e5b109d` — `pda2: fix AI config Test button, add validation hints and model default`
- **历史文档**（保留不覆盖）：前十三轮申请与结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

用户反馈：① 点击 Test 无反应，怀疑字段未填但**无提示**；② Model 输入框需要一个缺省值。

## 2. 变更明细（`examples/pda2/`，commit `e5b109d`）

### 2.1 Test 按钮点击无反应

- **排查**：串口日志显示用户多次点按（触摸坐标落在按钮区域附近）但回调未触发（无任何 `[AICfg]` 输出）——按钮行位置依赖 flex 内容高度推算，实际命中区域与视觉位置可能偏移
- **修复**：
  - Save/Test 按钮行改为 `LV_OBJ_FLAG_FLOATING` + `lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0)`——**固定钉在容器底部**，位置不再随上方内容高度浮动；`lv_obj_move_foreground(btn_row)` 保证在最前
  - 按钮高度 30 → 34（命中区域加大）
  - 两个按钮回调入口加串口日志（`[AICfg] Test/Save button clicked`），点击是否触发可即时诊断

### 2.2 字段校验提示

`ai_test_btn_cb` 按序校验并显示明确提示：`WiFi not connected` / `Base empty - fill it first` / `Model empty - fill it first` / `Key empty - fill it first`；全部通过才启动测试任务。

### 2.3 Model 缺省值

`openai_api.h` 新增 `AI_MODEL_DEFAULT "deepseek/deepseek-v4-flash-0731"`；`openai_load_config` 的 NVS 读取默认值从 `""` 改为 `AI_MODEL_DEFAULT`——NVS 无存值时 Model 输入框预填该值（有存值仍以存值为准，AI 对话屏共用同一加载函数，行为一致）。

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ 通过 | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ 完成 | COM5，Hash verified |
| Test 按钮点击 | ⏸ 待测 | 点击应见串口 `[AICfg] Test button clicked` 与状态行反馈 |
| 缺字段提示 | ⏸ 待测 | 逐个清空 WiFi/Base/Model/Key 各验证提示文案 |
| Model 缺省值 | ⏸ 待测 | 清除 NVS（或首次配置）时 Model 框应预填 `deepseek/deepseek-v4-flash-0731` |

## 4. 回滚方案

```bash
git revert e5b109d
```

## 5. 申请审批事项

- [ ] **A. 全量接受** — 保留 commit，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
