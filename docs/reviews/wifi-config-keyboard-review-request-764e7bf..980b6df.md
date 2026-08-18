# 评审申请书：收尾批次 1（usage 月度本地月界 + 扫描覆盖层跨屏残留 + test_keypad 镜像注释）

- **申请人**：Claude（pda2 现场调试，配合用户实测按键）
- **申请日期**：2026-08-19
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 3 个，未评审）：
  - `764e7bf` — `pda2: stats monthly reset on local time (CST-8)`
  - `6ce2b4b` — `pda2: hide wifi scan overlay/banner when the config screen is covered`
  - `980b6df` — `test_keypad: document raw-vs-driver column mirror`
- **命名说明**：文件名 = 本轮实际覆盖 commit 的首末 id（含两端，非 git 区间记法）；
  评审范围以正文列表为准。
- **已出列范围**（均 Codex 全量接受）：
  - `a2ecd7b`（第 30 轮，key.custom + 同步标注）— [结果](wifi-config-keyboard-review-result-codex-a2ecd7b.md)
  - `d22007d..4c3c9b1`（第 29 轮）、`b9b1ed4..fd7be74`（第 28 轮）、
    `e08bdac..b9b1ed4`（第 27 轮）
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，COM5，**本轮烧录待设备连接**）

---

## 1. 变更明细

### 1.1 usage 月度清零改本地月界（`764e7bf`，第 28 轮 O1）

- `factory.ino` setup() 一次设置 `TZ=CST-8` + `tzset()`（中国标准时，无夏令时）；
  Calendar / Sleep 时间戳等既有 `localtime_r()` 调用点同享该时区（此前全按 UTC）。
- `openai_api.cpp` stats 加载：`gmtime` → `localtime`，月度清零边界 = **用户本地月**
  而非 UTC 月；NTP 未同步哨兵（`time > 1700000000`）不变。
- 迁移注意：已存 blob 的 `reset_month` 是 UTC 月号，升级后首月若跨本地月会多一次
  清零——一次性、无害（统计归零重计）。

### 1.2 WiFi 扫描覆盖层跨屏残留（`6ce2b4b`，第 25 轮待办）

- 扫描覆盖层/结果 banner 建在 `lv_layer_top()`；屏幕被 push 覆盖时只跑 `.exit`
  不跑 `.destroy`，覆盖层会压在下一屏上。
- `exit4_1` 现在先 `wifi_scan_overlay_hide()` + `wifi_banner_hide()` 再全刷；
  补 `wifi_scan_overlay_hide` 前置声明。`destroy4_1` 原有清理不变（双保险）。

### 1.3 test_keypad 镜像注释（`980b6df`，issue_list 1.4）

- 示例打印的是 **raw** TCA8418 坐标；pda2 驱动列镜像（`driver_col = 9 - raw_col`），
  直接对照 keymap 会整体错位。加换算提示注释。
- `pda2/README.md` §2 键盘节同步加镜像说明（docs commit `6cee775`）。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` → SUCCESS |
| 烧录 | ✅ | COM5，Hash verified（2026-08-19） |
| 真机回归 | 部分 ✅ | 下方清单：11 项已过（2026-08-18），6/14/15 待测 + 本轮新增 16/17 |

### 真机回归清单（继承第 29/30 轮 15 项 + 本轮 3 项）

**d22007d..4c3c9b1（第 29 轮）新增**
1. ✅ Provider 下拉显示与切换（点击 / Alt+Enter 循环 6 项）；选 deepseek → base/model 自动填
2. ✅ 选 openrouter → Key 自动填入（串口 `[AICfg] key for openrouter loaded`）
3. ✅ 选 deepseek 后 Test → 通过（base 自动补 `/chat/completions`，无 404）
4. ✅ custom 选中 → base/model/key 三框清空
5. ✅ Usage 弹窗数据正确（跨月清零逻辑代码级，等 9 月自动验证）
6. ⏸ Save 后切走再切回同一 provider → key 从 NVS `key.<provider>` 恢复

**上轮 b9b1ed4（第 27 轮）**
7. ✅ Weather `r` 键 → `Fetching...` → 数据更新
8. ✅ Weather 城市显示深圳（不再是 San Carlos）
9. ✅ Shutdown 四路径：Enter=关机；任意键=返回菜单；Cancel 按钮=返回；返回键=返回
10. ✅ USB 插入时进 Shutdown → 只显示提示无关机

**Weather/Secrets 继承**
11. ✅ Weather 三页内容（Current/Hourly/5-Day）+ `+`/`-` 翻页 + `UV:--`
12. ✅ 无 GPS 时深圳回退（串口 `Using config: lat=22.5431`）
13. ✅（代码级） 空 NVS 时 AI Key 走 config_keys.h——第 29 轮 §1.3 四级链核验通过；
    实测需擦除 NVS（连带丢失 WiFi 配置），不做破坏性验证
14. ⏸ 失败重试路径：关热点发送 → 等待层消失 + 文本回填 + 气泡 `(failed)` → 重开热点重发成功
15. ⏸ 长回答 >4KB → `(truncated)` 无乱码

**本轮 764e7bf..980b6df 新增**
16. ⏸ Wifi Config 屏发起扫描（出现 Scanning 覆盖层）→ 立即 Back/切到其它屏 →
    覆盖层应消失、不残留在新屏上
17. ⏸ Calendar / Sleep 等时间显示为北京时间（此前 UTC，差 8 小时）
18. ✅（代码级） stats 月度清零改用本地月界（localmonth + TZ=CST-8；跨月行为等 9 月自动验证）

## 3. 遗留项（继承，简要）

- M1：3 份评审结果文档明文旧 Key 掩码（推公网前）
- 挂起：exit9/entry9 对称性；双 Enter 静默窗
- 阶段 1：SPIFFS append+compact、CJK 裁剪提示、system prompt NVS 化、全屏统计屏

## 4. 回滚方案

```bash
git revert 980b6df 6ce2b4b 764e7bf
```

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
