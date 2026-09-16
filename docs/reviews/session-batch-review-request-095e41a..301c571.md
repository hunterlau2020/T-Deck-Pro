# 评审申请：修复轮核销 + v1.0 版本号/缓存批次（095e41a..301c571）

- **申请人**：Claude（ZCode 代理）；**申请日期**：2026-09-16
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **关联 commit**（4 个，首末含两端）：
  - `095e41a` — session batch 修复轮：P1 `verifyRollbackLater()` override + P2-1/2/3/4 四项代码修复，P2-5 核实既有实现
  - `22e3b87` — `scripts/flash_verified.py`（强制逐块哈希校验刷机）+ issue_list §22 + CHANGELOG（当日两台机"变砖"假案定案）
  - `110802a` — TODO 刷新 + OTA 设计稿 §5.1 机制事实/§8 刷写纪律补注
  - `301c571` — **v1.0**：固件版本号体系（`fw_version.h` x.y.build）+ Whoami Me 页 profile NVS 缓存 + Cfg 页 FW label
- **背景（上轮结论 + 处置）**：`71c09e7` 会话批次四方评审 = Claude C / GPT C / Grok C / Qwen A
  （`docs/reviews/session-batch-review-result-71c09e7-{claude,gpt,grok,qwen}.md`，
  1×P1 + 6×P2）。`095e41a` 为修复轮；本轮申请同时覆盖修复核销与 v1.0 新批次。
  71c09e7 评审产物本身（`b037fac`..`2c86eca` 共 6 个 docs/scripts 提交）是上轮纸面
  记录，不属本轮评审范围。
- **硬件**：机 `28:37:2f:91:2c:20`（V1.0/4G，COM5）**已烧录 v1.0**（flash_verified.py
  5/5 块校验 + 整段回读 MD5 一致）；机 `10:20:ba:34:18:5c` 按用户指令留 `71c09e7`；
  机 `10:20:ba:34:19:ec` 未连接。

## 头部自校

- `git rev-list --count 2c86eca..301c571` = **4**（095e41a, 22e3b87, 110802a, 301c571）
- `git diff --name-only 2c86eca..301c571 | wc -l` = **11** = 归属表行数

### 区间文件归属表（11 文件）

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/factory.ino` | **评** | 095e41a P1 override；301c571 splash 版本行 |
| `examples/pda2/ota_update.cpp` | **评** | 095e41a P2-2/3/4 |
| `examples/pda2/ui_voice_ai.cpp` | **评** | 095e41a P2-1 |
| `examples/pda2/ui_deckpro_port.cpp` | **评** | 301c571 `fw_version_string()` + SF Version |
| `examples/pda2/ui_whoami.cpp` | **评** | 301c571 NVS 缓存 + FW label + 布局 |
| `examples/pda2/fw_version.h` | **评** | 301c571 新文件（版本宏 + 策略注释） |
| `scripts/flash_verified.py` | 不评（构建脚本档 §1.1） | 已实战验证（本轮烧录即用它）；BUILD-VERIFIED |
| `CHANGELOG.md` / `TODO.md` | 不评（纯文档） | 时点记录 |
| `docs/issue_list.md` | 不评（纯文档） | §22 台账 |
| `docs/ota-update-design.md` | 不评（纯文档） | §5.1/§8 补注，事实来自本轮真机核实 |

## §1 变更明细

### §1.1 修复轮核销表（71c09e7 四方发现 → 095e41a 处置）

| 发现（四方） | 处置 | 证据 |
|---|---|---|
| **P1** `verifyRollbackLater()` 缺失，initArduino 在 setup 前自动标 valid，层 2 回滚不存在（四家一致） | ✅ 已修：`extern "C" bool verifyRollbackLater() { return true; }`（factory.ino:651-659） | 编译✓；真机核实（2026-09-16）：核心 sdkconfig `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 属实、override 在版固件完整刷入后 30s 零复位正常启动。**注意**：当前 otadata=VALID 态，override 为空操作；PENDING_VERIFY 真实回滚路径仍待 §3 遗留的回滚矩阵真测 |
| **P2-1** 语音 ctx 把 user_text 当 role，第二轮起必错（Claude/Grok/GPT） | ✅ 已修：每轮发 2 条（user+assistant），`n+2>max_msgs` 守卫 | `ui_voice_ai.cpp:352-370`；真机多轮对话回归 ⏸（§3） |
| **P2-2** `s_ota_inflight` 创建后置位竞态（三家） | ✅ 已修：两个 launch 函数均在 `xTaskCreate` **前** claim，失败回滚归零 | `ota_update.cpp:559-608` |
| **P2-3** `xQueueOverwrite` 丢旧指针泄漏（Claude/Grok/GPT） | ✅ 已修：覆盖前 Peek+Receive 旧指针并 delete | `ota_update.cpp:274-286` |
| **P2-4** OTA TLS 继承 AI "Trust self-signed"（三家） | ✅ 已修：两处下载点改 `setCACertBundle(CA_BUNDLE_MOZILLA)`；明文仅编译期 `OTA_ALLOW_PLAINTEXT`（默认 0） | `ota_update.cpp:322-337, 433-441` |
| **P2-5** 自动休眠期间 OTA 覆盖层被打断（Claude/GPT） | 核实既有实现已防：`ota_busy()` 时自动休眠拒绝 push Sleep；低电另有 10 分钟安全阀（无代码改动） | 095e41a 提交说明引用既有实现；反例推演 ⏸ 交回滚矩阵一并真测 |
| **P2-6** OTA/Whoami 异步任务未登记 `async_ipc_contract.md`（GPT/Grok） | ❌ **未闭合**：提交说明称"queued with docs batch"，但本轮末该文档仍无 OTA/Whoami 行——登记为遗留（§3），下批补 | `grep -n "ota\|whoami" docs/async_ipc_contract.md` = 0 hits |

### §1.2 v1.0 新功能变更明细（301c571）

1. **版本号体系**：`fw_version.h` 新文件定义 `FW_VERSION_MAJOR=1 / MINOR=0`，build
   由 `__DATE__` 编译期生成（`ui_deckpro_port.cpp:175-194` 重排为
   `v<x>.<y> build <yyyy-mm-dd>`）。三处显示：splash（factory.ino:203，替换
   LilyGO 上游 `v2.4-260320`）、系统信息 SF Version（ui_deckpro_port.cpp:195-197）、
   Whoami Cfg 页 AI Provider 下拉下方灰字 label（ui_whoami.cpp:585-595）。
   升降级策略（大改动 x / 小改动 y / build 自动）写入头文件注释，CHANGELOG 登记。
2. **Whoami Me 页 NVS 缓存**（用户需求 2026-09-16："不要每次拉取远程"）：
   - `whoami` NVS 命名空间存 `pp_profile_t` blob（228B）+ `me_ts` 抓取时刻
     （ui_whoami.cpp:44-91）
   - 进屏/开机：每 boot 首次读 NVS 一次，命中即渲染 + 状态行 `cached <日期>
     (Refresh)`，**不发网络请求**（ui_whoami.cpp:838-848）
   - 自动拉取仅剩一种情况：无任何可用 profile（首次使用）；手动 Refresh 照旧走
     网络并回写缓存（ui_whoami.cpp:368-371）
   - Cfg 保存（key 可能换用户）：清 NVS 缓存 + RAM 副本置无效 + 重渲染占位文案
     （ui_whoami.cpp:494-503）
3. **Cfg 页布局**：FW label 落 y=212，Save/Test 216→232、status 254→268/高 60→50
   （ui_whoami.cpp:597-620）。

### §1.3 靶向清单（行级）

| 文件:行区间 | 函数 | 行为变化 | 波及面 |
|---|---|---|---|
| factory.ino:13,203 | (splash) | splash 版本行改打 `fw_version_string()` | 仅显示 |
| ui_deckpro_port.cpp:6,175-197 | `fw_version_string` / `ui_setting_get_sf_ver` | 新增版本串函数；SF Version 改返回项目版本 | 系统信息页显示 |
| fw_version.h:1-19 | （宏） | 新增版本宏 + 策略 | 无运行时行为 |
| ui_whoami.cpp:27,31-32 | (includes) | +Preferences/time.h | — |
| ui_whoami.cpp:44-115 | `wa_cache_load/save/clear` + `wa_me_status_cached` | 新增 NVS 缓存三函数 + 状态行 | Me 页数据来源 |
| ui_whoami.cpp:368-371 | `wa_consume` | 成功抓取后回写缓存 | +1 次 NVS 写/成功抓取 |
| ui_whoami.cpp:494-503 | `wa_cfg_save_cb` | 保存成功后清缓存 + RAM 置无效 + 重渲染 | Me 页下次进屏重新拉取 |
| ui_whoami.cpp:585-620 | `wa_cfg_build` | +FW label；Save/Test/status 下移 | Cfg 页布局 |
| ui_whoami.cpp:838-855 | `wa_entry` | 首次进屏先 NVS 后渲染；仅无缓存才自动拉 | Me 页网络行为（核心变更） |

**相邻但未改的高危邻居**：`wa_me_render`（未改，被缓存路径复用——渲染逻辑与数据
来源解耦）；`wa_start`（未改，仍读 Cfg 文本域/NVS——缓存命中时不再被自动调用，
仅 Refresh/Enter 触发）；`whoami_keyboard_poll`（未改，队列排水与缓存无关）。

## §2 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | pio SUCCESS；size text=1342596 data=1051628 bss=1531202（较 095e41a +4.4KB text/+0.8KB data，缓存+版本串） |
| 烧录 | ✅ | flash_verified.py 5/5 块 "Hash of data verified" + `--readback` MD5 一致（机 `28:37:2f`，COM5） |
| 开机冒烟 | ✅ | 25s 串口 0 个 rst 标记，I2C/SPIFFS(env.cfg 完好)/SX1262/GPS/BHI260 全上线 |
| 095e41a 真机 | ✅ | 完整刷入后正常启动（§1.1 P1 行证据） |
| 静态复查 | ✅ | 本申请 §1.3 + §6 自评 |
| 真机回归（v1.0 UI/缓存） | ⏸ | ① splash 显示 `v1.0 build 2026-09-16`；② Whoami→Cfg 下拉下方 FW label；③ Me 进屏显示 `cached`且无 waitbox（不联网）；④ Refresh 手动拉取成功；⑤ Cfg 保存后 Me 重拉；⑥ 语音 AI 多轮第二轮起上下文正确（P2-1） |

## §3 遗留项

1. **P2-6 未闭合**：`async_ipc_contract.md` 补 OTA/Whoami 任务/队列/结果对象行
   （所有权、排水、销毁、页面生命周期）——下批。
2. **OTA 回滚真测矩阵**（TODO 首项）：PENDING_VERIFY 状态链 + P2-5 反例
   （下载覆盖层期间休眠定时器触发）一并真测。
3. 真机回归清单 §2 ⏸ 六项（用户实测）。
4. `ca_bundle_full.h` static 双拷贝去重（114KB flash，低成本优化）。

## §4 回滚方案

- `git revert 301c571`（v1.0 批次独立可退）；`git revert 095e41a` 需同时恢复 P1
  缺陷状态（不建议单独退）。
- 设备侧：`python scripts/flash_verified.py COM5 <上一版 bin>`；NVS `whoami`
  命名空间可留（尺寸不符自动按无缓存处理，见 §6）。
- `22e3b87`/`110802a` 为脚本/文档，无回滚需求。

## §5 申请审批事项

- [ ] **A 全量接受**（含 §3 遗留按期处置）
- [ ] **B 退回修订**（意见：）
- [ ] **C 部分接受**（保留：／回退：）
- 审批人／日期：

## §6 开发者自评

**最有把握**：缓存数据流——`wa_entry` 的 NVS-only 路径不触碰网络与异步任务，
`wa_consume` 回写只在成功分支，失败路径缓存状态不变；Cfg 保存清缓存覆盖了
"换 key = 换用户"的主要污染场景。

**最没把握（3 处，条件交点）**：
1. **NVS blob 跨版本**：`pp_profile_t` 尺寸/布局变化后，`getBytesLength != sizeof`
   按缓存未命中处理（自动重拉）——设计如此，但"旧固件写入的缓存被新固件读"这一
   跨版本场景未实测（本轮真机只有同版本写读）。
2. **首次运行的空命名空间**：`nvs.begin("whoami", true)` 只读打开不存在的命名
   空间返回 false → 走自动拉取——代码推演成立，未在 NVS 无该命名空间的干净机上
   单独验证（机 #1 是先有网络 profile 后有缓存）。
3. **Cfg 状态区高度 50px**：3 行 14px 文本（"AI: label\nmodel\nkey: set"）恰好
   贴边；若 `server from env.cfg |` 前缀拼接使首行 wrap 成 2 行则第 4 行被裁剪
   ——旧高度 60px 同样存在此边界，本轮收紧 10px 加剧了它。纯显示项。

**我知道没测到的**：lv_timer/EPD 交互下 FW label 的重绘时序（静态创建、无动态
更新，理论无风险）；Preferences 在 WiFi 任务并发时的线程安全（缓存读写全部在
loopTask 上下文，worker 任务不触碰 NVS）；`strftime` 在时间未同步（1970）时
`s_cache_ts=0` 走 "cached (press Refresh)" 分支。

---

### 附：本轮新增规则（用户裁定 2026-09-16，随本批入档）

**串行单机刷写**：任何时候只对一台设备烧录/OTA，严禁两台并行——一个坏镜像
（或刷写缺陷）不应有同时砖掉全部设备的机会。今天两台同症状"变砖"（根因虽为
刷写不完整而非固件）正是该风险的现实演示。已写入 issue_list §22 规则 4、
TODO 刷机纪律、build 文档坑 6。
