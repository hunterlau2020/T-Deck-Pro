# 评审申请书：双评审 Low 项收尾批次（weather partial 上屏 / Trust 影响面文案）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 2 个，文件名 = 首末含两端）：
  - `71fa528` — `weather: accept forecast-only success as data_valid`
  - `a58a73c` — `ai-cfg: spell out TLS toggle scope in status line`
- **背景**：两份 Kimi 双评审结果（`…-kimi-c27cb39..3475c9b.md` / `…-kimi-3f654a5..4c3a331.md`，
  均 **A 全量接受**）各登记 1 项 Low"下次顺手做"（issue_list §9.4 / §7.4）；
  服务端幂等键排期期间集中清掉。同批文档收尾（issue_list §4.1 复核闭合、
  penpal-design.md 铺入 Kimi v2 四项 Low）为 docs commit，不属代码评审范围。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**已连接、已烧录**）

---

## 1. 变更明细

### 1.1 `71fa528` — weather：仅 forecast 成功也置 data_valid

- **问题**（Kimi c27cb39 §1.1 Low，issue_list §9.4）：`data_valid` 只在
  `parse_current_weather` 置位；冷启动无缓存时若 current 失败、forecast
  成功，`refresh_cb` 不进 `update_ui()`，`Partial data` 提示也不显示
  （提示路径同样被 `data_valid` 门控）——屏幕空白且无声，forecast 已解析
  却不上屏。
- **修复**：`parse_forecast` 尾部（cJSON_Delete 前）置
  `data_valid = true`，附注释说明理由。不改任何调用方/门控逻辑：
  partial 场景的语义是"有解析出的数据就值得上屏 + 给 partial 提示"。
- **不改的部分**：时间戳推进逻辑不变（仅双 ok 才推
  `last_fetch_time`——§9.1 修复的口径），partial 下次进屏仍会重试；
  缓存路径不变。

### 1.2 `a58a73c` — ai-cfg：Trust 开关状态行拼出作用域

- **问题**（Kimi 3f654a5 §1.3 Low，issue_list §7.4）：Trust 开关作用于
  全设备所有 http_utils 消费者（天气、词典、WiFi Test…），ON 即全设备
  HTTPS 放弃 CA 校验；影响面大于 AI Config 单屏直觉，而旧文案仅
  `TLS: trust self-signed` / `TLS: CA verify`。
- **修复**（纯文案）：状态行改 `TLS: ALL HTTPS trust self-signed` /
  `TLS: ALL HTTPS CA verify`（16/21 字符，状态行宽度内）；串口日志改
  `[AICfg] tls insecure=%d (applies to ALL HTTPS)`。开关语义、NVS 键、
  `openai_tls_set()` 调用链全部不动。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS（19.4s，无新警告） |
| 烧录 | ✅ | COM5，SUCCESS |
| 开机冒烟 | ✅ | 串口 45s：EPD/触摸/键盘初始化正常，无 panic |
| forecast-only 路径 | ⏸ | 需 current 端点故障注入（改 URL），留真机回归 §3-1 |
| Trust 文案 | ⏸ | 静态推算宽度；真机视觉确认见 §3-2 |

## 3. 真机回归清单

**本轮新增**
1. ⏸ Weather：（可选）current URL 改错触发 partial——forecast 应上屏 +
     `Partial data` 提示出现（修复前为空白无提示）；恢复 URL 后完整刷新正常
2. ⏸ AI Config Trust 开关切换：状态行显示 `TLS: ALL HTTPS …` 新文案，
     排版不溢出；触摸 / `\v` 键 / 重启保持（既有回归项，文案换了字）

**继承（未回归项顺延，与 c8f62f3 申请 §3 一致）**
3. ⏸ SD exFAT 两行提示排版 / 拔卡 no card
4. ⏸ GPS 屏读数刷新 + altitude；菜单第 2 页左滑；#6/#14/#15

## 4. 遗留项（简要）

- `docs/reviews/` 待结果申请：本份 + `c8f62f3`（Codex C 的 P2 修复）。
- 笔友 App：Codex v2 复审 C（P1 幂等键 / P2 subject 缓冲）——幂等键方案
  已提交服务端，v3 修订待服务端定稿后动笔；Kimi v2 四项 Low 已预铺入
  `docs/penpal-design.md`（同批 docs commit）。
- CI 矩阵日志抽查（set_srcdir 修复验证）仍待做（本机无 gh CLI）。

## 5. 回滚方案

```bash
git revert a58a73c 71fa528
```

## 6. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
