# 评审申请书：第 8-16 轮双评审整改（第二次合并申请）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-16
- **关联分支**：`HD-V2-250915`
- **关联 commit**（模块拆分，文件名取范围首尾 id）：
  - `01f8eac` — 替换损坏的 ISRG Root X1 + `ca_bundle_check.sh`
  - `cb8201f` — 扫描代次绑定释放 + WiFi 页队列 IPC + 页面代次
  - `57356fa` — AI Config：Test 验草稿端点 / Save 门槛 / 真实 deadline / 队列 IPC
  - `8b96656` — AI Chat：失败保留草稿 / UTF-8 断行 / 队列 IPC
- **评审依据**：[主评审结果](wifi-config-keyboard-review-result-23942f6..9b104d1.md)（10 Findings）+ [Copilot 复审](wifi-config-keyboard-review-result-23942f6..9b104d1-copilot.md)（10 Findings）
- **历史文档**（保留不覆盖）：前十七轮申请与结果见 `docs/reviews/`
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. Findings 整改对照

| Finding | 状态 | 整改 |
|---|---|---|
| 主 1.1 / Cop 1.1 **Critical 真实 Key 入库** | **⏸ 用户决策延后** | 用户将去 OpenRouter 后台 revoke；代码暂不删除 `AI_KEY_DEFAULT`（用户"暂时不修复"）；后续方案已定：NVS-only + 缺 Key 提示。**本申请不关闭此项** |
| Cop 1.2 **ISRG Root X1 损坏** | ✅ `01f8eac` | 官方完整 PEM（Mozilla bundle）替换；openssl 逐张验证 5 根全部可解析；新增 `scripts/ca_bundle_check.sh`（主 1.5） |
| Cop 1.3 **扫描代次释放竞态** | ✅ `cb8201f` | SCAN_DONE 事件**计数**替代布尔；中止超时置 `s_scan_release_pending`，下次扫描**先等事件再 `scanDelete()`**，等不到则拒绝启动（"Scan busy - retry"） |
| 主 1.4 / Cop 1.4/1.5 **异步任务生命周期与并发** | ✅ `cb8201f`/`57356fa`/`8b96656` | 统一范式（见 §2）；页面/请求代次校验，旧结果丢弃 |
| Cop 1.6 **AI Test 写死 OpenRouter** | ✅ `57356fa` | Test 端点从**草稿 Base** 推导（`chat/completions → models?limit=2`），用草稿 Key 请求 |
| Cop 1.7 **Save 无校验无门槛** | ✅ `57356fa` | Save 校验（https:// 前缀/非空/Key≥16）+ **Test 通过且无后续编辑**才允许保存；`openai_save_config` 返回 bool，NVS 写失败不替换旧值 |
| Cop 1.8 **倒计时非 deadline / Close 不取消** | ✅ `57356fa` | HTTP timeout = 10s = msgbox 倒计时；Close = Cancel（请求代次++，迟到结果丢弃） |
| Cop 1.9 **发送失败丢草稿** | ✅ `8b96656` | 发送**成功后才清空输入框**；失败（网络/认证/超时/任务错误）草稿留在框内可直接重试 |
| Cop 1.10 **按字节切行破坏 UTF-8** | ✅ `8b96656` | 断行点回退越过 continuation byte（0x80-0xBF），多字节序列不被截断 |
| 主 1.2 拆 commit | ✅ | 本批 4 个 commit 按模块拆分（证书 / WiFi 页 / AI Config / AI Chat） |
| 主 1.3/1.10 真机验证不足 | 部分 | 见 §4 回归清单（AI 两屏待用户配合实测） |
| 主 1.6 状态栏刷新策略 | 文档化 | 状态栏时间在电量 10s 周期内检查、仅分钟变化时更新（无额外计时器，最坏滞后 10s）；策略记录在 issue_list §5.1 |
| 主 1.7 NVS 迁移 | 文档化 | `ai` namespace 键名（base/model/key）不变、语义兼容；无 schema 版本——将来加字段时引入 `cfg_version` |
| 主 1.8 UA 泛化 | 文档化 | UA 策略：`http_get_ua` 仅用于明确按 UA 区分响应的端点（ifconfig.me）；其余端点保持默认 UA，不做全局参数化 |

## 2. 异步任务生命周期（主 1.4 文档化）

三条异步路径（WiFi Test / Time Sync / AI Test / AI 发送）统一范式：

```
UI 线程                              工作任务
───────                              ─────────
点击 → busy=true（UI 独有）
      捕获 page_gen/req_gen 经
      xTaskCreate 参数传入 ────────► new Result 结构体（含 gen）
                                    执行 HTTP（栈 8KB，优先级 1）
                                    xQueueSend(q, &ptr, portMAX_DELAY)
轮询：xQueueReceive(q,0) ◄─────────  vTaskDelete
      校验 gen == 当前代次
      → 匹配：应用结果、busy=false
      → 不匹配：丢弃（delete）
```

- **所有权**：结果结构体在任务中 new、队列交付后归 UI 线程 delete——`std::string` 等非平凡对象**不再跨核裸读**
- **busy 状态**：仅 UI 线程读写；任务句柄不再被任务回写
- **取消语义**：离页（destroy/gen++）/ 弹窗 Close（req_gen++）均通过代次失效；任务自然结束，队列结果被丢弃
- **队列深度 4**：任务侧阻塞发送，不会丢结果
- **页面代次**：entry/destroy 时 `page_gen++`，重新进入页面后旧请求结果必然被丢弃

## 3. 界面 wireframe（主 1.9，240×320 EPD，14pt 字体 ≈ 30 ASCII 列）

**AI Text 屏**：
```
┌──────────────────────────────┐ y=0
│  back "AI Text"              │
│ ┌──────────────────────────┐ │
│ │ 多行输入框 64px           │ │  约 4 行 ASCII ×30 列 ≈ 120 字符可见
│ │ （上限 200 字符，约 7 行） │ │
│ └──────────────────────────┘ │
│ 状态行（Thinking.../Page X/Y）│
│ ┌──────────────────────────┐ │
│ │ 回答区 flex_grow          │ │
│ │ （自动换行，分页 8 行/页） │ │
│ └──────────────────────────┘ │
│ ┌──────────┐ ┌──────────┐   │
│ │   Send   │ │  Clear   │   │ ← FLOATING 钉底 34px
│ └──────────┘ └──────────┘   │
└──────────────────────────────┘
```
按钮行 `FLOATING + LV_ALIGN_BOTTOM_MID`，与上方 flex 内容无重叠（内容合计 ≤ 容器 274px，实测计算留有 40px+ 余量）。

**AI Config 屏**（同构）：Base 标签+多行框(52px) / Model 标签+单行(30) / Key 标签+单行(30) / 状态行 / Save+Test 按钮行(34, FLOATING 钉底)。

## 4. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 / 烧录 | ✅ | `pio run -e pda2` SUCCESS；COM5 |
| CA bundle 5 根可解析 | ✅ | `ca_bundle_check.sh` 逻辑 + openssl 逐张验证通过 |
| 回归清单（主 1.10 展开） | ⏸ | 待用户配合：① 扫描期间按键不进下一字段 ② 连按 ⌫ 退 WiFi 屏无残留 ③ 双 Shift 交叠大写 ④ Sym 锁跨页保留 ⑤ 连接期间按键丢弃 ⑥ AI Test msgbox 倒计时→结果/Close 取消 ⑦ AI 发送失败草稿保留可重试 ⑧ 中文回答无乱码 |
| AI Test 验草稿端点 | ⏸ | 改 Base 后 Test 应对新端点发起（串口 `[AICfg] test ...` 确认 URL） |
| Save 门槛 | ⏸ | 编辑后未 Test 点 Save → "Run Test first"；Test 通过后可保存 |

## 5. 回滚方案

```bash
git revert 8b96656 57356fa cb8201f 01f8eac
```

## 6. 申请审批事项

- [ ] **A. 全量接受** — 四个 commit 保留，Key 项按用户延后处理单独跟踪
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
