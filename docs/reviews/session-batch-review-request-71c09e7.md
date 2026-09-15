# 评审申请书：会话批次（CA 根修复 + 语音上下文 + 自动休眠 + PenPal 修复 + Whoami App + TTS 开关 + OTA 实现）

- **申请人**：Claude（pda2，用户需求驱动的混合批次）
- **申请日期**：2026-09-15
- **关联分支**：`HD-V2-250915`
- **关联 commit**：`71c09e7`（单 commit 批次，基于 `6eb8245`；27 文件 +4149/-1580）
- **评审要求**：按 [`docs/review_guide.md`](../review_guide.md) 执行——L1 DOC-ALIGNED
  （§2.1）+ A/B/C；findings 按 §4.2 定级留坐标；结果写入
  `docs/reviews/session-batch-review-result-71c09e7-<reviewer>.md`（新建文件绝不覆盖）。
- **批次说明**：本批为一天内的用户需求连续落地，**混合了 8 组不相关变更**
  （见 §0）。按 review_guide 惯例应分模块 commit；因用户明确要求单 commit
  归档，请评审方按 §0 的分组逐组给 findings（组内坐标用 `文件:行`）。
- **硬件**：T-Deck-Pro HD-V2 ×3（两台 V1.1 带 DRV2605、一台 V1.0 无；
  V1.0 的 EPD 走"无硬件复位"兼容路径，本批所有真机验证在 V1.1 上完成，
  V1.0 仅启动/慢刷屏行为采集——见 §8 已知差异）。

---

## 0. 批次分组索引

| # | 组 | 主要文件 | 动因 |
|---|---|---|---|
| 1 | CA 根修复 | `ca_bundle_full.h`、`scripts/gen_ca_bundle.py`、`scripts/extra_roots/*.pem`、`scripts/verify_chains_vs_bundle.py` | 设备 X509 fatal：api.minimax.io 换链（DigiCert 交叉签名 G2）、api.minimaxi.com 自带 Comodo 老根 |
| 2 | 语音多轮上下文 | `ui_voice_ai.cpp` | 用户需求：连续语音对话无记忆 |
| 3 | 5 分钟自动休眠 | `factory.ino`、`peri_keypad.cpp`、`ui_deckpro.cpp` | 用户需求：无操作自动进同一 Sleep 屏 |
| 4 | PenPal 修复 | `ui_penpal.cpp`、`ui_penpal_write.cpp` | 三个用户报告：失败提示不可见（106px CLIP 重叠）、中文错误豆腐块、reply 改 title 被服务端拒 |
| 5 | Whoami App | `ui_whoami.cpp`（新）、`ui_penpal.*`、`ui_deckpro.*` | 用户需求：profile 展示 + Cfg 迁入 |
| 6 | 菜单重排 | `ui_deckpro.cpp` | 用户指定三个槽位对调 |
| 7 | TTS 开关 + 开关样式 | `ui_voice_ai.cpp`、`ui_ai_cfg.cpp` | 无耳机检测硬件 → 用户开关；EPD 上开关状态不可辨 |
| 8 | OTA 实现 | `ota_update.*`（新）、`ota_trust_anchor.h`（新）、`factory.ino`、`ui_deckpro.*`、`peri_gps.cpp`、`scripts/ota_sign.py`、`scripts/ota_gen_key.py` | docs/ota-update-design.md v6 落地 |

## 1. 组 1：CA 根修复（DOC 依据：设备 finding 2026-09-14，PC 链探测脚本）

- 新增两个**指纹固定**的额外根（`EXTRA_ROOT_SHA256`）：DigiCert Global
  Root CA 2006（`cacerts.digicert.com`，指纹与公开值一致，且经
  `verify_directly_issued_by` 链验证）与 AAA Certificate Services 2004
  （从 api.minimaxi.com 自带链提取，保证 subject DER 字节一致）。
- bundle 122 → 124 根（56794 B），`REQUIRED_SUBJECT` 增两行锚点断言。
- `scripts/verify_chains_vs_bundle.py`：PC 端全链验证（锚点查找 + 逐级
  签名 + 有效期），六端点全绿。
- 评审关注点：extra_roots 的指纹钉死与换根流程（仅 USB）是否与
  SECURITY.md 语义一致。

## 2. 组 2：语音多轮上下文

- `vai_ctx`：整轮 (user, assistant) 对存储，仅"确认成功"的轮次入史；
  `VAI_CTX_BUDGET` 4096B / `VAI_CTX_MAX_MSGS` 16，UTF-8 边界截断；
  调用改 `openai_chat_multi`。文字输入与语音共用同一上下文；退屏清空。

## 3. 组 3：自动深度睡眠

- `ui_activity_mark()`：`keypad_loop()`（按下/抬起）与 `touchpad_read()`
  （触摸）打点；5s 周期 `idle_sleep_timer_cb` 检查 5 分钟超时 →
  `scr_mgr_push(SCREEN11_ID)`（复用手动 Sleep 屏,含倒计时与取消路径）。
- `audio.isRunning()` 期间不触发（重计时）。

## 4. 组 4：PenPal 修复

- 发送失败/校验失败改 `pp_msgbox_show`（原写入与计数标签重叠的 106px
  CLIP 状态条,EPD 上不可读——设备报告 2026-09-14 两起）。
- msgbox 正文检测非 ASCII → `lv_font_simsun_16_cjk`（FastAPI 中文
  detail 豆腐块报告）。LV_FONT_SIMSUN_16_CJK 已在 lv_conf 开启。
- reply 模式 title 三路锁定（DISABLED 置灰 + 键盘焦点强制 body + 触摸
  重定向）;`ppw_lock_edit(false)` 后重套用。

## 5. 组 5：Whoami App（SCREEN_WHOAMI_ID）

- 双 tab（Me / Cfg）：`GET /users/me/profile`（`penpal_get_profile` 新增）
  + PenPal Cfg 页整体迁入（NVS 同键,`pp_notify_cfg_changed` 通知重同步）。
- **两处已修复的缺陷请复看**：① Z 序——整页容器先建、顶栏按钮后建
  （后建兄弟在上层;首版按钮被 240×320 页面盖住导致"点不到 Cfg/无法返回",
  设备报告）;② 队列生命周期——首版 destroy 删队列与在途 worker 的
  `xQueueSend` 竞态触发 `xQueueGenericSend (pxQueue)` 断言重启（串口实证
  2026-09-14）;现改 PenPal 模式：队列一次创建永不删,`whoami_keyboard_poll`
  每拍无条件排水。
- 异步任务栈 8KB（与 PenPal 一致;首版 6KB 溢出静默死——"Test 卡在
  Testing..."报告）。

## 6. 组 6：菜单重排

- Whoami → 第一页 Wifi 槽（95,189）;Wifi → 第二页 Lora 槽（23,13）;
  Lora → 第三页（95,13）。`menu_btn_list` 顺序即页面切分,20 项 → 3 页。

## 7. 组 7：TTS 开关 + 开关样式

- Voice AI 顶栏 "TTS" `lv_switch`（NVS `ai`/`tts_enabled` 默认开）;
  OFF 时 `start_tts()` 早退（ASR 不受影响——板载 PDM MIC 与耳机无关）。
- 两处开关（Trust/TTS）样式：**必须绑定 `LV_PART_INDICATOR |
  LV_STATE_CHECKED` 选择器**——默认态样式会被主题的 checked 样式覆盖
  （首版因此"改了没变化",用户报告 2026-09-15）。ON=黑轨白钮,OFF=白轨黑框。

## 8. 组 8：OTA 实现（DOC：docs/ota-update-design.md v6）

按设计 §9 顺序落地的组件与合同对齐点：

- **发布侧**：`scripts/ota_gen_key.py`（P-256 密钥对;私钥 gitignored、
  公钥 65B SEC1 进 `ota_trust_anchor.h`）;`scripts/ota_sign.py`
  （六字段规范化字节串 §2.1 钉死编码、P1363 r||s、签后自验——签名/验签
  往返已自测通过）。
- **设备侧** `ota_update.cpp`：指针结果通道（深度 1 + `xQueueOverwrite`,
  new/delete 恰一次）;单飞 `s_ota_inflight`（xTaskCreate 成功后递增,
  worker 唯一出口递减并同点释放硬件锁）;`mbedtls_ecdsa_verify` + MPI
  r/s;双 URL scheme 检查;流式 `Update.write` + 增量 SHA-256;读空闲
  45s + 绝对 10min;Content-Length 强制;失败 `Update.abort()`;
  `last_seq` 仅 `end(true)` 后写。
- **UI** SCREEN2_2（枚举末尾追加）：Check（Cancel 覆盖层）→ 清单展示 →
  Install → **全屏吸收容器**下载覆盖层（无按钮,"DO NOT POWER OFF"）→
  Enter 重启。`factory.ino` loop 无条件 `ota_result_poll()`。
- **互斥**：Sleep 入口 busy 拒绝;Shutdown/低电量 10 分钟安全阀;
  `WiFi.setSleep` 成对;电量 <30%/仪表无效预检拒。
- **自证窗口** §5.2：`esp_task_wdt_init(60s, panic)` + add 守卫;
  五个喂狗点（A7682E testAT / GPS getAck+L76K / EPD 页间 / PCM WAV 块 /
  SD 挂载）;首帧 FULL `done_seq` 达成 → `mark_valid` + 恢复 5s 默认。
  **真机已验证**：V1.1 上 `[OTA] self-attest: mark valid ok (ESP_OK)`
  （串口实录）。
- **T 值声明**：60s = 2× GxEPD2 `_busy_timeout`（构造参数 30s）,
  §5.3 双机校准打点**未做**（TODO,数据应入 docs/ota-baseline.md）。
- **§8 验证计划完成度**：SIM/代码走查级——签名往返（PC）、编码钉死
  （脚本+设备两侧同规范）、生命周期路径推演;真机级——仅自证窗口
  通过一次;**回滚矩阵/覆盖层吸收/断网重试/低电阀均未测**（等双机
  台架,见 TODO）。

## 9. 开发者自评（review_guide §7.1）

- **最有把握**：CA 根修复（PC 全链验证绿 + 真机 minimax 双端点恢复）;
  PenPal msgbox 三处（直接设备报告闭环）;OTA 签名链路（脚本自测往返）。
- **最没把握（≤3）**：① OTA boot WDT 的 T=60s 未经校准（§5.3 数据缺）,
  且 V1.0 的 EPD 兼容路径单页 busy 等待 ~10s,五喂狗点覆盖下余量约 6 页
  ——若某未识别的库内等待 >60s 会误触发回滚;② `xQueueOverwrite` 深度 1
  的"覆盖丢失"路径的 delete 纪律依赖 `ota_result_poll` 每拍排水,若
  loop 被长阻塞（EPD busy 10s × 连续多帧）结果消费延迟但不丢
  （worker 已退,只延迟 delete）——请评审确认无泄漏路径;③ Whoami 与
  PenPal 共享 NVS 键的并发写窗口（两屏不会同时活跃,但
  `pp_notify_cfg_changed` 的时序依赖屏切换,请复核）。
- **知道没测到的**：OTA §8 全部真机项（除自证一次）;V1.0 硬件上全部
  新功能（该机 EPD 慢路径使交互验证不可行,已用 V1.1 代测）。

## 10. 硬件已知差异记录（非本批引入）

V1.0（无 DRV2605）机型：I2C 扫描无 0x1a/0x5a → 判 V1.0 → EPD 无硬件
复位兼容路径 → 每次 `ink_screen_init`/刷页 busy 超时 ~10s,启动 ~65s。
已在三种固件（本批 OTA 构建、仓库 HEAD、V1.1 预 OTA 镜像）上交叉验证
一致,确认为该硬件变体固有行为。dump 镜像存 `backups/v11_dump.bin`
（gitignored）。
