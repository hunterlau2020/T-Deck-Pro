# 用户反馈修复扩展复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..f3e1698.md](wifi-config-keyboard-review-request-e08bdac..f3e1698.md)
- **关联代码范围**：`e08bdac^..f3e1698`
- **本次增量范围**：`0b43685..f3e1698`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 等待中离页再返回仍会丢弃结果并永久保持 busy

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:749-773,800,1113-1139`
- **状态**：上一轮 Copilot 1.1 未整改。
- **证据**：
  - request 在 Send 时捕获当前 `s_chat_page_gen`。
  - `chat_exit()` 只隐藏 waitbox，不取消 request、不清 busy、不保存 pending prompt。
  - 再进入 AI Text 时 `chat_entry()` 递增 generation。
  - 旧 request 返回后进入 stale 分支；该分支只删除 reply，不清除 `s_chat_send_busy`。
- **影响**：回复与已提交的问题丢失；页面重进后没有等待提示，且所有键盘输入会被 busy 分支吞掉，直到页面被真正 destroy。
- **建议修复**：明确采用“后台继续”或“离页取消”模型，并让 request identity、generation、pending、draft、waitbox 和 busy 一次性完成状态迁移；stale reply 不能遗留其对应 busy。

### 1.2 Input 页横向尺寸仍超过可用内容宽度

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:988-999,1034-1061`
- **状态**：上一轮 Copilot 1.4 未整改。
- **证据**：
  - 外层宽 232px，左右 padding 后内容宽约 224px。
  - Input 页固定 textarea 176px + 间距 4px + 按钮列 48px，共 228px。
  - 页面禁用滚动。
- **影响**：右侧按钮可能发生裁切或布局挤压；顶部 Tab 调整只增加纵向空间，没有解决横向预算。
- **建议修复**：textarea 使用 flex-grow，按钮列固定 48px；或把 textarea 固定宽度降到 172px。

### 1.3 申请书的提交数量与实际范围不一致

- **严重性**：Low
- **位置**：申请书开头和 §1
- **证据**：
  - 文档仍称“本轮 4 个”，实际列出 7 个代码 commit。
  - 历史说明仍称只需评审“上述 4 个新 commit”。
- **影响**：审批人容易误判范围，尤其 `cc94452`、`06a2c13`、`f3e1698` 恰好包含本轮复核修复。
- **建议修复**：统一为 7 个，并明确前三个增量 commit 是对上一轮结果的整改。

## 2. 已通过项

- **扫描提示帧绑定**：`f3e1698` 主动请求覆盖层全刷，以该 request/done sequence 到达面板的时刻作为 800ms 起点，并在等待期间保持覆盖层置顶，已解决“对象存在但面板未显示”的根因。
- **音量键保护**：`06a2c13` 在 Chat tab 明确忽略 `'\v'`，控制字符不再进入 textarea。
- **顶部 Tab**：`cc94452` 将 Chat/Input 放到右侧顶栏，下方页面获得额外纵向空间；与左侧返回标题没有静态坐标重叠。
- Usage breakdown 继续在 stats mutex 内读取，格式化缓冲区容量覆盖当前文本。
- Tab 切换继续使用 hidden swap，符合电子纸分页而非动画的约束。

## 3. 验证说明

- `python scripts\test_nvs_atomic_save.py`：11 项 PASS。
- 当前环境没有 `pio`，无法独立复现申请中的固件编译。
- `git diff --check 0b43685..f3e1698` 的代码变更无格式错误；范围内归档的 Codex 结果文件存在既有行尾空格。
- 扫描提示、顶部 Tab 和等待中离页仍需目标硬件回归。
- 真实 API Key 继续按项目开发期例外处理；结果文件未复制 Key 正文。

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

扫描提示和音量键问题已关闭，但等待中离页/重进仍存在确定性的 High 状态锁死。修复该生命周期问题后再关闭本轮。
