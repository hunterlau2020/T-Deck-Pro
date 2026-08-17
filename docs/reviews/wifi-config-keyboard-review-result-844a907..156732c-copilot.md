# 第六轮整改与 Usage 交互复审结果（Copilot）

- **评审日期**：2026-08-17
- **评审申请书**：[wifi-config-keyboard-review-request-844a907..156732c.md](wifi-config-keyboard-review-request-844a907..156732c.md)
- **关联代码范围**：`844a907^..156732c`
- **本次重点范围**：`3cdff38..156732c`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 AI Config 的 Test 和 Usage 仍没有键盘入口

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_cfg.cpp:323-329,395-405,491-524`
- **证据**：
  - Test、Usage 只注册了 `LV_EVENT_CLICKED` 回调。
  - 键盘路径的 Enter 只在三个输入字段间前进，最后直接执行 Save。
  - `\t`（Alt+Enter）和 `\v` 被直接忽略，也没有按钮焦点或快捷键状态。
- **影响**：
  - 在当前以实体键盘为主要导航方式的产品路径中，用户无法发起 Test，因而 `s_ai_test_passed` 永远为 false，Save 也无法完成。
  - 新增 Usage 统计只能依赖触摸；触摸失效或不易命中时没有替代路径。
- **建议修复**：把底部 Save/Test/Usage 纳入统一键盘焦点状态；至少提供明确快捷键，例如 Alt+Enter 循环底部操作、Enter 执行，Back 返回字段。

### 1.2 AI Chat 离页会静默丢弃在途请求及已经清空的输入

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:628-640,749-769,1106-1131`
- **触发场景**：Send 后等待服务器期间通过触摸 Back、其他页面切换或生命周期销毁离开 AI Text。
- **证据**：
  - Send 后输入框立即清空，同时清除 draft；pending 气泡明确不写入 `chat.log`。
  - destroy 递增 page generation，迟到结果被无条件丢弃，并直接清除 busy。
  - exit 看到空输入框后再次清除 draft。
  - 注释称 pending “is persisted”，但 `chat_log_save()` 实际跳过 `chat_pending_idx`。
- **影响**：用户已经提交的问题及可能成功返回的回复都会静默消失；重进或重启均无法恢复，也没有“请求已取消”的反馈。
- **建议修复**：离页时将原 prompt 保存为 retry draft 并移除/标记 pending，或者禁止离页并明确提示；若允许后台完成，则必须保留 generation 所属会话并在重进时接收结果。

### 1.3 CHL1 恢复日志声称已转存 CHL2，但没有实际写回

- **严重性**：Low
- **位置**：`examples/pda2/ui_ai_chat.cpp:338-350`
- **证据**：成功解析 CHL1 后仅输出 `resaved as CHL2` 并返回，没有调用 `chat_log_save()`。
- **影响**：只读恢复后每次启动仍重复走歧义格式探测；诊断日志与真实持久化状态不一致。
- **建议修复**：成功恢复后立即安全写回 CHL2，或把日志改为 `will be saved as CHL2 on next change`。

### 1.4 申请书验证状态存在互相矛盾的旧记录

- **严重性**：Low
- **位置**：申请书 §3、§4、§5
- **证据**：
  - §2.18 已记录第二轮“历史恢复 + 多轮上下文接续”通过。
  - §3 和 §5 仍写第一轮恢复失败、修复后待复测。
  - §4 后半部分又保留未勾选的 usage、Sleep、轮次配对条目，与前面的通过记录部分重复。
- **影响**：无法从申请书单一判断当前门禁状态，后续评审容易重复追踪已经完成的项目。
- **建议修复**：以第二轮结果统一 §3-§5；保留失败历史，但明确标为“第一轮失败、第二轮通过”。

## 2. 上一轮 Findings 复核

- **通过**：任务创建前复制 `ctx_msgs/trimmed`，handoff 后不再访问 `rq`，跨核 use-after-free 已修复。
- **通过**：CHL1 带 count/不带 count 两种已分别探测，上一部署版本日志可恢复。
- **通过**：official 解析失败会尝试 bak；tmp 在 rename 前显式 flush。
- **通过**：`openai_stats_poll()` 已挂入主 loop，dirty stats 即使没有后续响应也会在 60 秒窗口到期后提交。
- **通过**：AI Config destroy 增加 stats checkpoint；V1 迁移会标记 dirty，并在 poll/lifecycle checkpoint 写入 V2。
- **通过**：Usage 读取与累计共用静态 mutex，显示 chat/test 两组数据，不引入新的无锁读取。
- **通过**：菜单第一屏顺序与申请一致；等待层按秒更新，不进行高频 EPD 动画。

## 3. 验证说明

- `python scripts\test_nvs_atomic_save.py`：11 项 PASS。
- `git diff --check 3cdff38..156732c`：通过。
- 当前环境没有 `pio`，无法独立复现申请中的固件编译。
- 申请登记的 Sleep、历史恢复、多轮上下文、Test 取消、New 确认、发送等待层和 WiFi 离页回归采用用户真机记录。
- 真实 API Key 继续按项目开发期例外处理；结果文件未复制 Key 正文。

## 4. 审批意见

- [ ] A. 全量接受
- [x] B. 退回修订
- [ ] C. 部分接受

上一轮 High 阻断项已经关闭，但 Test/Usage 键盘不可达会破坏 AI Config 的完整实体键流程；AI Text 离页还会静默丢失已经提交的消息。修复这两个交互生命周期问题并整理验证状态后再关闭本轮。
