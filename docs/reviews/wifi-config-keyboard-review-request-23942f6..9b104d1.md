# 评审申请书：第 8-16 轮合并申请（CA/校时/WiFi Test/AI 配置与对话屏）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16（合并申请；原第 8-16 轮申请文件已并入本文并删除）
- **关联分支**：`HD-V2-250915`
- **关联 commit**（按提交顺序，文件名取范围首尾 id）：
  `23942f6`、`d8f0ab7`、`8001ff1`、`64ebcb7`、`048ea73`、`9551bd7`、`6be70eb`、`e5b109d`、`d4ccf28`、`9b104d1`
- **历史文档**（保留不覆盖）：第 1-7 轮申请与全部结果文档见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 申请事由

第 8-16 轮改动围绕三条主线：**HTTPS 可用性**（CA bundle / NTP / 时区）、**WiFi 屏可用性**（扫描中止竞态、FIFO 边界、WiFi Test 异步化、状态栏时钟、Time Sync 按钮）、**AI 两屏重构**（配置屏三输入框 + Test msgbox、对话屏多行输入 + Send/Clear + 异步发送）。原 9 份申请文档按用户要求合并为本文。

## 2. 变更总览（按 commit）

| Commit | 标题 | 变更要点 | 关联评审 Findings / 用户需求 |
|---|---|---|---|
| `23942f6` | fix WiFi Test TLS failure - complete CA bundle + NTP time check | CA bundle 由 1 根扩为 4 根（ISRG X1 / **ISRG Root YR** / DigiCert G2 / GlobalSign R3）；HTTPS 前时间校验 + NTP 重试（cn.pool 优先）；`http_response_t.error` 透出 mbedtls 原因 | 用户：WiFi Test "HTTP -1" |
| `d8f0ab7` | add GTS Root R4, NTP sync after connect, CST-8 timezone | bundle 补 **GTS Root R4**（openrouter.ai 当前链 WE1←R4）；连接成功后自动 NTP 校时（≤8s）；三处 `configTzTime` 从 PST8PDT 改 **CST-8** | 用户：openrouter 根证书、连网自动校时、时钟错误 |
| `8001ff1` | send curl User-Agent to ifconfig.me | `http_get_ua()` 新函数；WiFi Test 带 `curl/8.5.0` UA 取纯文本 IP + 尾部空白裁剪 | 用户：避免 HTML 输出 |
| `64ebcb7` | flush hardware key FIFO on screen transitions | `keypad_clear_chars()` 同时 `keypad.flush()` 排空**硬件 FIFO** + 重置瞬时修饰键；Sym 锁跨页保留（产品语义文档化） | 第4轮 1.1、第5轮 1.1 |
| `048ea73` | SCAN_DONE event sync, async WiFi Test, Time Sync button | 扫描中止改 **SCAN_DONE 事件同步**（onEvent 晚于框架 `_scanDone`，超时推迟释放）；WiFi Test **异步任务** + `/ip` 端点 + `inet_pton` IP 校验 + 断网弹窗；新增 **Time Sync 列表项**（弹窗显示同步前后时间）；`http_require_wifi` 契约修正 | 第4轮 1.2（High）、第6/7轮 1.1/1.2/1.3、用户 Time Sync 需求 |
| `9551bd7` | show real local time in the menu status bar | 状态栏时间从硬编码 "10:19" 改为实时本地时间（`--:--` 至 NTP 同步）；后并入电量 10s 刷新周期 | 用户：状态栏时间写死 |
| `6be70eb` | rework AI config screen - per-field boxes, Save/Test buttons | Base **多行**/Model/Key **各自独立输入框**（label 不再重复值）；草稿保留 + 触摸焦点同步；**Save/Test 按钮**；`http_get_auth()`；Test 异步请求 `/models?limit=2` 显示 `data[0].id`；状态栏时间并入 10s 周期 | 用户 AI 配置屏 4 项反馈 |
| `e5b109d` | fix AI config Test button, add validation hints and model default | Save/Test 按钮行 **FLOATING 钉底**（位置确定、命中区加大）；Test 逐字段校验提示；`AI_MODEL_DEFAULT = deepseek/deepseek-v4-flash-0731` | 用户：Test 无反应 + 缺省 model |
| `d4ccf28` | AI Test feedback as msgbox with countdown and Close | Test 反馈改 **msgbox**："Testing... Ns" 倒计时（仅秒变时刷文本）→ 结果替换内容；校验失败也在 msgbox 显示；Close 按钮；弹窗期间吞键盘；退出销毁 | 用户：msgbox 需求 |
| `9b104d1` | rework AI Text - multi-line input, Send/Clear buttons, async send | 输入框**多行**（64px/200 字）；**Send/Clear 按钮**（FLOATING 钉底）；发送**异步任务**（不冻结 UI，发送中吞键）；请求体按 OpenRouter curl 范例：system 提示（KET English examiner）+ `temperature 0.7` + `reasoning.exclude` + user 内容（cJSON 自动 JSON 转义）；**`AI_KEY_DEFAULT` 内置默认 Key**（NVS 优先，⚠️ 已入仓库需保密/轮换） | 用户 AI Text 4 项需求 + Key 默认值 |

## 3. 关键设计决策

1. **信任库策略**：根证书一次性补全（4+1 根），覆盖 ifconfig.me（LE YR 层级）、openrouter.ai（GTS R4）、Cloudflare（DigiCert G2）等主流链；任何 CA 新签发的站自动验证，无需按站下载
2. **时间链路**：开机 configTzTime（CST-8，cn.pool 优先）→ 连接成功自动校时 → HTTPS 前兜底校验/重试 → Time Sync 手动按钮；状态栏实时时间（未同步显示 `--:--`）
3. **异步范式统一**：WiFi Test / AI Test / AI 发送均 FreeRTOS 任务 + 轮询收取 + 页面活动检查 + 发送中吞键——UI 永不因网络请求冻结
4. **弹窗范式统一**：msgbox（正文 + Close）/ 横幅（3s 自动消失）/ 覆盖层（倒计时 + 屏蔽输入）按场景分级使用；top layer 对象在 destroy 时清理
5. **键盘交付模型**：软件 FIFO + 页面边界双清（软队列 + 硬件 FIFO）+ 修饰键独立状态 + 溢出 W1C 恢复；Sym 锁跨页保留
6. **配置持久化**：连接/测试成功语义优先（Connect 成功才存 WiFi；AI 配置 Save 显式提交），失败不覆盖旧值

## 4. 验证状态

| 项目 | 状态 | 说明 |
|---|---|---|
| 编译 / 烧录 | ✅ | `pio run -e pda2` SUCCESS；COM5 Hash verified |
| WiFi Test / Time Sync / 状态栏时间 | ✅ | 用户真机确认（公网 IP、同步前后时间、实时时钟） |
| AI 配置 Test msgbox / 默认 model、key | ⏸ | 待用户真机复测（上轮 Key empty 已由默认 Key 修复） |
| AI 对话发送（新请求体 + 异步） | ⏸ | 待用户真机发问验证 |
| 第 4-7 轮修复项回归 | ⏸ | 页面切换残留、扫描中退出、双 Shift 交叠等 |

## 5. 回滚方案

```bash
git revert 9b104d1 d4ccf28 e5b109d 6be70eb 9551bd7 048ea73 64ebcb7 8001ff1 d8f0ab7 23942f6
```

## 6. 申请审批事项

- [ ] **A. 全量接受** — 十个 commit 保留，关闭本次评审循环
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
