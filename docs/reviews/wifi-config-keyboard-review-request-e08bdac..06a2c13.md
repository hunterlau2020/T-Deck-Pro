# 评审申请书：用户反馈修复（第八轮）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-17
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 4 个，全部未评审）：
  - `e08bdac` — `pda2: AI Config - Usage breakdown includes cached_tokens details`
  - `8770a41` — `pda2: wifi scan - hold the overlay for a minimum visible time`
  - `f4449c3` — `pda2: AI Chat - two-tab layout: full-screen Chat + big Input`
  - `0b43685` — `pda2: AI Chat - hide the waitbox on screen exit too`
  - `cc94452` — `pda2: AI Chat - tabs move into the top row next to back`
  - `06a2c13` — `pda2: AI Chat - ignore the volume key on the Chat tab`（Codex §1.3 修复）
- **历史范围**：`844a907..156732c`（31→28 个 commit）已由 Codex **全量接受**（见
  [评审结果](wifi-config-keyboard-review-result-codex-844a907..156732c.md)），本申请
  **不再重复携带**已通过的内容——评审只需审上述 4 个新 commit。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 Usage 弹窗显示完整明细（`e08bdac`，用户反馈）

AI Config 的 Usage 按钮弹窗从两行改为完整 breakdown：

```
Chat: 1234 tok
  cached 100, write 0
  audio 0, rsn 195
  cost 0.000234 USD
Test: 56 tok, 0.000100 USD
```

用户点名要求展示 `usage.prompt_tokens_details.cached_tokens`；实现为 `openai_stats_text()`
（mutex 保护读 RAM 结构），msgbox 高度参数化（Usage 用 205px 高变体）。

### 1.2 WiFi 扫描中间提示最短显示（`8770a41`，用户反馈）

用户实测看不到 "Scanning..." 覆盖层。确认：覆盖层在 `scanNetworks` 返回 RUNNING 后**立即**
弹出（非扫描完成后），但当前网络下扫描 1-2s 即完成，覆盖层未走完 EPD 局刷就被隐藏。
修复：`WIFI_SCAN_OVL_MIN_MS=800`——扫描结束（或失败）后覆盖层继续停留至满 800ms 再隐藏，
中间状态保证可见。

### 1.3 AI Text 双 Tab 布局（`f4449c3`，用户反馈）

原上下分屏（历史 160px + 输入 86px）太挤，改为两页 Tab：

- 顶部 **Chat / Input** tab 条（30px），Alt+Enter 切换，触摸可点
- **Chat tab**：历史记录**占满全屏**（flex_grow）
- **Input tab**：大输入框（176×~220，上限 200 字符）+ 大按钮（48×~74 的 Send/Clear/New）
- **点 Send 自动跳回 Chat tab**（等待层与回复流程不变）
- Chat tab 下按任意可见字符自动跳 Input tab 并追加；Enter 跳 Input；空框 Backspace 回 Chat
- 重试草稿恢复时默认打开 Input tab
- New 键盘路径改到音量键 `'\v'`（Input tab 下；Alt+Enter 让位给 tab 切换）

### 1.6 音量键 Chat tab 忽略（`06a2c13`，Codex 1.3 修复）

Chat tab 下 `` 原本落入"跳转+输入字符"兜底——切 tab 且把控制字节写入输入框；
现为 no-op（New 确认仍仅 Input tab）。

### 1.5 Tab 并入顶栏（`cc94452`，用户反馈）

Chat/Input tab 按钮移到返回按钮同一行（顶栏右端），下方整块区域归页面使用——
历史区比上一版再高出原 tab 条高度（约 30px）。

### 1.4 等待层离屏清理（`0b43685`，Codex §1.11 顺手修复）

`scr_mgr_push` 只触发 exit 不触发 destroy，等待层会留在 top layer——`chat_exit` 现在也
hide waitbox（与 destroy 一致）。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified |
| NVS 算法测试 | ✅ | `scripts/test_nvs_atomic_save.py` 11/11 PASS（沿用） |
| CA bundle | ✅ | 5 根证书 PASS（沿用） |
| 真机回归（§3） | ⏸ 待用户 | 上机体验项见下 |

## 3. 真机回归清单（新增待测项）

1. ⏸ **Usage 弹窗**：AI Config → Usage → 6 行 breakdown 完整显示（含 cached 行）
2. ⏸ **扫描提示**：WiFi Cfg 触发扫描 → 可见 "Scanning..." ≥0.8s 再出结果
3. ⏸ **双 Tab**：Chat/Input 切换（Alt+Enter + 触摸）；历史占满全屏；Input 大按钮；
   Send 后自动跳回 Chat tab；Chat 下按字符自动跳 Input 并输入
4. ⏸ **等待层离屏**：发送中按 Back 离开 → 其他屏无残留等待层
5. ⏸ 顺带项：长回答 >4KB `(truncated)` 无乱码；关 WiFi 发送失败 → 文本回填可重试

## 4. 遗留项（继承，简要）

- Key 按 `api-key-dev-exception` 决策延后（C1/C2 已落地；推公网前 SECURITY.md 4 步）
- SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏 → TODO.md 阶段 1

## 5. 回滚方案

```bash
git revert 06a2c13 cc94452 0b43685 f4449c3 8770a41 e08bdac
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
