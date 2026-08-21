# 评审结果：菜单翻页 off-by-one（幽灵页）修复

- **对应申请**：[wifi-config-keyboard-review-request-de78338.md](wifi-config-keyboard-review-request-de78338.md)
- **评审专家**：Codex
- **评审日期**：2026-08-21
- **结论**：**A 全量接受**

## 审核结果

`menu_get_gesture_dir()` 将 `page_num` 用作可达页的最大下标，而非页数。将计算改为 `(MENU_BTN_NUM - 1) / 9` 后，当前 18 个菜单项只允许页下标 `0..1`，第 2 页继续左滑不再进入不存在的页 2；右滑边界和两枚页点的既有行为保持一致。

## 验证边界

- 已核对公式与当前两页显示分支的语义一致。
- 本评审环境未安装 PlatformIO，未独立执行固件编译或触摸手势真机回归；申请书 §3 清单应继续回填。

