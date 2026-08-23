# 评审结果：菜单翻页 off-by-one（幽灵页）修复（de78338）

- **评审日期**：2026-08-22
- **申请书**：[wifi-config-keyboard-review-request-de78338.md](wifi-config-keyboard-review-request-de78338.md)
- **关联 commit**：`de78338`（当前 HEAD 有效，未经重写）
- **评审人**：Kimi（kimi）——本修复源自本评审人此前
  [penpal-design-review-result-kimi.md](penpal-design-review-result-kimi.md) §1.1
- **评审结论**：**A. 全量接受**

---

## 1. Findings

### 1.1 修复与 kimi §1.1 的建议完全一致，门控语义复核无误

- **严重性**：✅ 通过
- **位置**：`de78338` → `examples/pda2/ui_deckpro.cpp:415-417`
- **验证**：
  - 门控 `if(page_curr < page_num) page_curr++`（`ui_deckpro.cpp:287`）确将
    `page_num` 当**最大下标**消费；修复后 `(MENU_BTN_NUM - 1) / 9`：18 项 → 1，
    合法下标 0..1，幽灵页不可达——与 kimi §1.1 给出的首选修法逐字对应；
  - `MENU_BTN_NUM <= 9` 单页早退（:284）不受影响；`MENU_BTN_NUM=9` 时
    `(9-1)/9=0`，同样安全；
  - 问题机理（状态越界后右滑被"空滑"消耗一次）与代码行为吻合：越界到 2 后
    `page_curr > 0` 右滑先 2→1，期间翻页分支不命中，用户感知失灵一次——申请书
    描述准确；
  - "有意不动"部分（Setting/Test/A7682/PCM 回绕式翻页、按钮创建循环、页点）
    范围判断正确：这些页当前条目数均非 9 的整倍数，无幽灵页。
- **遗留登记（非本批范围）**：penpal 菜单第 3 页落地时仍需按 kimi §1.1 补登的两处
  改动——按钮创建循环 `i/9` 分派（`ui_deckpro.cpp:453-458`）与 `ui_Panel4`
  第 3 页点（:306-307 按 child 下标寻址）。申请书已声明并入 penpal 批次，
  跟踪至该批。

### 1.2 附带价值：修复了现存固件的潜伏 bug

- 该 off-by-one 在 18 项菜单下**当前固件即触发**（非 penpal 引入），本批先于
  penpal 实现独立修复，方向正确。

## 2. 验证说明

- 本评审为静态代码复核（diff 级），未独立编译/烧录；编译与开机冒烟采信申请书
  §2。
- 真机回归 3 项（第 2 页左滑不动 / 右滑回第 1 页 / 往返页点一致）仍为 ⏸，
  手势行为确需真机滑动验证，逐轮回填。

## 3. 审批意见

- [x] **A. 全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

单行修复，语义注释到位，kimi 评审 §1.1 的 High 前置条件中"page_num 公式"一项
对**现有固件**的部分就此关闭（penpal 设计稿 §6 的公式修订仍属设计稿 v2 复审
范围）。
