# 用户反馈修复复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-e08bdac..0b43685.md](wifi-config-keyboard-review-request-e08bdac..0b43685.md)
- **关联代码范围**：`e08bdac^..0b43685`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 等待中离页再返回会丢弃结果，并可能永久保持 busy

- **严重性**：High
- **位置**：`examples/pda2/ui_ai_chat.cpp:641-647,749-773,800,1119-1134`
- **触发场景**：
  1. Send 启动请求，request 捕获当前 `s_chat_page_gen`。
  2. 通过只触发 `exit` 的页面 push 离开；`0b43685` 隐藏 waitbox，但保留 busy 和原 request。
  3. 请求尚未返回时重新进入 AI Text；`chat_entry()` 递增 page generation。
  4. 旧 request 随后返回。
- **证据**：
  - reply 只有在 `cr->gen == s_chat_page_gen` 时才清除 `s_chat_send_busy`。
  - 重进后 generation 已变化，reply 被当作 stale 删除，busy 不会清除。
  - waitbox 已在 exit 隐藏，重进后没有任何等待反馈；键盘路径在 `s_chat_send_busy` 为 true 时直接吞键。
- **影响**：回复和已提交的问题丢失，AI Text 页面可能一直无法输入或再次发送，直到页面被真正 destroy。
- **建议修复**：明确选择一种生命周期：
  - 后台继续：push/重进不得改变该请求所属 generation，重进时恢复 waitbox，并让匹配结果清 busy；
  - 离页取消：exit 时保存 prompt 为 retry draft、使 request stale，并同步清 busy/pending；
  - 无论哪种，stale reply 分支都要按 request identity 安全释放其对应 busy，不能只删除结果。

### 1.2 扫描覆盖层的 800ms 是对象存活时间，不是面板可见时间

- **严重性**：Medium
- **位置**：`examples/pda2/ui_deckpro.cpp:2362-2425`
- **证据**：
  - `wifi_scan_ovl_t0` 在创建 LVGL 对象时记录。
  - 扫描结束后只判断 `millis() - wifi_scan_ovl_t0 >= 800` 就删除覆盖层。
  - 没有等待包含该覆盖层的 EPD flush 完成，也没有记录它首次到达面板的序号。
- **影响**：在快速扫描或刷新队列繁忙时，800ms 可能大部分消耗在首帧刷新前；覆盖层实际可见时间短于 800ms，甚至仍可能在抵达面板前被删除。申请中的“可见 ≥0.8s”没有被实现层直接保证。
- **建议修复**：沿用 Sleep 已采用的 request/done flush sequence：覆盖层对应帧完成后再起最短可见计时；或至少从首次 flush-complete 事件开始计时。

### 1.3 Chat 页的音量键会作为控制字符写入输入框

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:802-836`
- **触发场景**：当前位于 Chat tab 时按音量键 `'\v'`。
- **证据**：
  - `'\v'` 只在 Input tab 分支中映射为 New。
  - Chat tab 对除 `+/-`、Backspace、Enter 之外的所有字符执行“切到 Input 并追加”。
- **影响**：音量键会切换页签并向 textarea 写入不可见的 vertical-tab 控制字符；随后发送可能携带隐藏字符。相同实体键在两个 tab 中语义不一致。
- **建议修复**：在 tab 分支前统一处理 `'\v'`，或在 Chat tab 明确忽略；只有可打印/合法输入字符才允许触发自动切页并追加。

### 1.4 Input 页横向固定尺寸超出可用内容宽度

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:968-979,1034-1061`
- **证据**：
  - 外层宽 232px，左右 padding 各 4px，内容宽为 224px。
  - Input 页内固定 textarea 176px、列间距 4px、按钮列 48px，总计 228px。
  - Input 页禁用滚动，超出的 4px 只能被裁切或产生布局挤压。
- **影响**：右侧按钮边缘可能被裁切，触摸命中区和视觉边框与申请中的“大按钮”体验不一致。
- **建议修复**：textarea 改为 172px，或让 textarea 使用 flex-grow、按钮列固定 48px，由布局计算剩余宽度。

## 2. 已通过项

- Usage 读取继续在 stats mutex 内完成，新增 cached/write/audio/reasoning 显示没有引入无锁访问。
- Usage msgbox 高度参数化，不影响 Test/错误弹窗原有尺寸。
- 双 Tab 使用 hidden swap 而非动画/实时滚动，方向符合电子纸分页原则。
- Send 后切回 Chat、恢复 draft 时进入 Input、Alt+Enter 切换 tab 的主流程实现与申请一致。
- `chat_exit()` 现在会移除 top-layer waitbox，其他页面不再残留该对象；但其 request 生命周期仍需按 Finding 1.1 闭环。
- 本范围 `git diff --check` 通过。

## 3. 验证说明

- `python scripts\test_nvs_atomic_save.py`：11 项 PASS（回归沿用）。
- 当前环境没有 `pio`，无法独立复现申请中的固件编译。
- Usage、扫描可见时长、双 Tab、等待中离页均仍标记待真机验证。
- 真实 API Key 继续按项目开发期例外处理；结果文件未复制 Key 正文。

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

等待中离页/重进的 generation 与 busy 生命周期存在确定性阻断；修复该项后，再用目标 EPD 的 flush 完成事件验证扫描提示的真实可见时长，并完成双 Tab 键盘回归。
