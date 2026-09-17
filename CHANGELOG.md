# Changelog

本文件记录 pda2 预研（T-Deck-Pro HD-V2，分支 `HD-V2-250915`）的主要工作。
评审细节见 `docs/reviews/`（每轮 = 申请 + 双评审结果，按 commit 范围命名）。

## 2026-09-17（v1.10：4_1 双光标齐闪 + 空退格静默跳字段吃 SSID）

- **症状**（真机报告，scan-pick 跳转后）：SSID/密码两个输入框都有
  光标跳动；删密码字母时 SSID 同步掉字母。
- **根因 1（blink 泄漏）**：项目 LVGL 的 textarea 无 DEFOCUSED 光标
  处理，`LV_EVENT_FOCUSED` 一发 blink 即启动且**永不停止**——
  `wifi_cfg_set_field` 只发 FOCUSED 不收尾，每次切字段泄漏一个闪烁；
  pick 路径 `wifi_cfg_set_slot`（尾部 set_field(0)）+ 随后 set_field(1)
  连发两次 → 两框齐闪。
- **根因 2（静默跳字段）**：密码框删空后再按退格会 `set_field(0)`
  静默跳回 SSID——EPD 慢刷新 + 双光标下用户无感知，继续按退格
  开始删 SSID（感知为"同步删"）。
- **修复**：光标可见性改由 CURSOR part 的 `bg_opa` 驱动（出框
  TRANSP / 入框 COVER），`anim_time=0` 静态常显（EPD 本就不该闪，
  每 530ms 的局部刷新风暴一并消除）；create 时 pass 框初始隐藏、
  SSID 框点亮；空退格跳回 SSID 时 banner 明示 "Back at SSID field"。
- 版本 v1.9 → **v1.10**。教训：**用一个 lv_event_send 驱动 UI 状态
  前先核对该事件在所用 LVGL 构建里是否真的双向**（此处 FOCUSED
  有效、DEFOCUSED 压根没有分支）。

## 2026-09-17（v1.9：已连接态异步扫描被驱动中止——统一空闲态扫描）

- **定案证据**（串口打点，用户已连接态复现）：`4_2 collect failed
  r=-2 mode=0x1 status=3`——扫描**启动成功但中途被驱动中止**
  （mode=STA、status=WL_CONNECTED 均正常）。ESP32 已连接 STA 的
  异步全信道扫描与连接维护冲突（离开本信道遍历），驱动会掐掉
  扫描 → `scanComplete()` 返回 FAILED → UI "Scan failed (-2)"。
  空闲态扫描（用户 Disc 后实测）100% 可靠。
- **修复**：`ui_wifi_scan_prepare()` 撤销"已连接原地扫"特赦——
  已连接也统一**断开 → 空闲态扫描**。恢复语义：
  - 4_1 单次扫描结束立即重连（`s_scan_dropped_link` 标志 +
    `wifi_autoconn_retry_now()`——否则要等管理器守卫窗口最多
    65s，不可接受）；
  - 4_2 停留期间保持断网（选网场景合理），`exit4_2` 恢复并立即
    重连；Disc 过（管理器停）则保持断开不自动重连。
- 附：抓取工具 `E:/tmp/capture.py` 补断线重连 + 追加写（深睡断
  USB 后旧句柄失效会静默丢数据，之前两轮窗口因此没抓到操作）。
- 版本 v1.8 → **v1.9**。教训：**"文档说允许"≠"实测可靠"**——
  ESP-IDF 允许连接态扫描，但异步模式下会被中止；行为要以真机
  打点为准。

## 2026-09-17（v1.8：Disc 断开按钮 + 扫描失败可见性 + kick 防御）

- **用户诉求**：① wifi 没有 disconnect 功能；② 4_2 仍报
  "Scan failed" 且无详细错误信息。
- **Disc 按钮**（4_1 按钮行第 4 个）：`wifi_autoconn_stop()`（新增
  管理器接口）+ `disconnect`（保 NVS）——STA 置 idle（扫描最友
  好态），直到下次显式连接/重启；状态行 "Disconnected" + banner。
- **失败可见性**：4_2 标题行带错误码 "Scan failed (-2) - retry 10s"；
  kick/collect 失败串口打点带 `mode/status` 诊断字段——可区分
  "启动被拒"（kick，connecting 残留）vs "扫描中途失败"（collect，
  如迟到 SCAN_DONE/中止），v1.7 前两者在 UI 上不可分。
- **kick 防御**：4_2 tick 启动前检查 `ui_wifi_scan_release_clear()`
  （4_1 abort 协议的 pending 导出）——上一轮中止的迟到 SCAN_DONE
  未落地时跳过本轮（下秒再试），消除反复进出屏后 kick 与迟到
  回调的竞争（4_2 此前无此防御，是"仍失败"的候选根因之一）。
- 版本 v1.7 → **v1.8**。

## 2026-09-17（v1.7：4_2 扫描循环全程挂起自动重连——kick 撞 begin 竞争）

- **症状**（真机复测 v1.6，未连接场景）：进入 WIFI Scan 列表屏很快
  出现 "Scan failed - retry every 10s"。
- **根因**（v1.5 引入）：4_2 每轮扫描结束 collect 后即恢复管理器，
  管理器发现退避/守卫时间已过**立刻 begin**保存槽位——STA 回到
  connecting；10 秒后下一轮 kick 的 `disconnect+delay(100)` 排不净
  connecting 残留，`scanNetworks` 被拒（-2）→ 每轮失败。首进屏时
  管理器若在重试窗口，第一轮 kick 同样撞车。
- **修复**：① `entry4_2` 进屏即 `wifi_autoconn_hold(true)`，
  `exit4_2` 才恢复——4_2 停留期间管理器全程挂起（扫描间隙 STA 保持
  idle，对扫描最友好）；collect/async_start 不再内部重连。② kick
  被拒时**重试一次**（再 disconnect + 200ms 落定），4_1 手动触发
  同样加固。③ 4_2 kick 失败补串口打点（`4_2 scan kick failed`）。
- 版本 v1.6 → **v1.7**。教训：**挂起/恢复要覆盖"消费方活跃的整个
  周期"，不是每笔操作各自成对**——循环型消费方在轮次间恢复共享
  资源，等于给下一轮埋竞争。

## 2026-09-17（v1.6：充电中自动深睡延至 10 分钟）

- **用户诉求**：硬件能否识别充电状态？能则把自动睡眠延后到 10 分钟。
- **现状确认**：BQ25896 已有 VBUS 检测（`ui_battery_25896_is_vbus_in()`
  → `PPM.isVbusIn()`，电池屏同款轮询）；自动深睡原为 **5 分钟**
  （`UI_IDLE_SLEEP_MS`，每 5s 检查）。
- **修复**：VBUS 接入（充电/USB 插着，如串口抓取、刷机会话）时窗口
  取 `UI_IDLE_SLEEP_CHARGING_MS = 10 分钟`，否则维持 5 分钟；I2C 读
  失败自然退化为 5 分钟（安全侧）。附带收益：开发/抓串口不再被
  5 分钟深睡打断 USB CDC。
- 版本 v1.5 → **v1.6**。

## 2026-09-17（v1.5：有界 WiFi 自动重连——5 次 + 指数退避，放弃后空闲）

- **用户诉求**：连接失败会无限重试，未连接状态下 wifi app 明显慢；
  应设重试上限。
- **根因**：开机 `setAutoReconnect(true)` 在目标 AP 不在场时每 2.4s
  无限重连（§24 的 -2 源头）——v1.3/v1.4 让扫描能"破局"，但每轮
  重连的内部信道扫描仍持续与 UI 扫描竞争驱动，且 v1.4 扫描结束的
  reconnect 还会**重启**重连循环，未连接状态永远"忙"。
- **修复：自动连接管理器**（`wifi_autoconn_*`，ui_deckpro.cpp，
  factory loop 每 tick poll）：
  - 最多 **5 次**尝试，指数退避 **2.5s→5s→10s→20s→40s**，放弃后
    串口打点、STA 保持 idle（对扫描最友好），直到下次显式连接
    （Save/Test 成功 `wifi_autoconn_restart()`）或重启；
  - 事件驱动（GOT_IP 重置计数 / DISCONNECTED 计数退避），65s 无
    事件 guard 自愈；已连接状态下 guard 到期只重臂不重连；
  - UI 扫描周期 `wifi_autoconn_hold(true/false)` 挂起/恢复管理器
    （恢复时丢弃自身 prepare 引起的 disconnect 事件，不误计失败）；
    port 层 reconnect 不再 begin——v1.4 在这里重启了重连循环；
  - 手动连接（`wifi_cfg_connect`）也关 autoReconnect、成功后交
    管理器接管，意外断开同样有界重试。
- 版本 v1.4 → **v1.5**。TODO"WiFi 开机重连退避/上限"核销。

## 2026-09-17（v1.4：v1.3 复测暴露两项——4_2 同步扫描阻塞 + scan-pick 冲槽）

- **症状**（真机复测 v1.3）：① Wifi app 内点击 scan/config、scan 里点
  SSID、backspace 退出都明显变慢；② 从 Scan 点 SSID 跳 Config 永远落
  slot 1，冲掉已存配置（不点 Save 也会丢）。
- **①根因 = v1.3 副作用**：4_2 列表屏 10s lv_timer 里是**同步**
  `WiFi.scanNetworks()`，主循环阻塞 2-3s；v1.3 前它被 -2 秒拒（瞬间
  返回）所以"流畅"——修好 -2 反而把潜伏的阻塞暴露了。
  **修复**：4_2 全面异步化——port 层 `ui_wifi_scan_async_start()/
  ui_wifi_scan_collect()` 替换同步 getter；timer 周期 10s→1s 只做
  非阻塞轮询，每 10s 节奏 kick 新扫描；退出屏时复用 4_1 abort 的
  SCAN_DONE 释放协议（提取 `wifi_scan_stop_and_release()` 共享）。
  空列表文案三态：Scan failed / No APs found / Scanning。
- **②根因**：`entry4_1` 的 scan-pick 无条件落 slot 0 并清密码，叠加
  切槽即"masked-aware outgoing save"——不点 Save 切走也会把清空后的
  密码写进 NVS，已存配置被毁。**修复（三态）**：pick 的 SSID 已在某
  槽 → 跳到该槽（密码保留，banner "existing slot"）；否则 → 第一个
  **空槽**（新网络重输密码）；全满 → banner "Slots full - clear one
  first" 且不动任何槽。
- 版本 v1.3 → **v1.4**。教训：**"修好一个被掩盖的性能地雷"时，把
  它遮挡的下游行为一起过一遍**（-2 秒拒曾伪装成"快"）。

## 2026-09-17（v1.3：WiFi 扫描 -2 定案——重连循环与扫描互斥）

- **症状**（真机报告，v1.0 起两轮复现）：WIFI Scan 列表屏"停滞无反应"；
  WIFI Config 屏清空 SSID 框回车立刻报 `scan start failed(-2)`。
- **根因**（串口抓取定案，`E:/tmp/wifi_capture.log`）：开机自动连
  slot 0（HUAWEIP50）+ `setAutoReconnect(true)`，目标 AP 不在场 → 每
  2.4s 一轮 `NO_AP_FOUND` 重连，**STA 永远处于 connecting 态**；
  ESP-IDF 规定该状态下 `esp_wifi_scan_start()` 拒绝
  （`ESP_ERR_WIFI_STATE`），Arduino 包装为 `WIFI_SCAN_FAILED(-2)`：
  - 4_1 异步路径有打点 → 用户看到 "scan start failed(-2)"；
  - 4_2 同步路径 `n=-2` 被当 "0 个结果" 渲染空表、无提示 → "停滞"。
  两轮 CJK/渲染修复未中根因：这是**环境触发的既有缺陷**（保存的 AP
  不在场才复现；之前 WiFi Test 能显示 LAN IP 时 AP 在场、已连接，
  扫描合法），不是 v1.0 引入的回归。
- **修复**：新增 `ui_wifi_scan_prepare()/ui_wifi_scan_reconnect()`
  （port 层导出）：扫描前把**未连接**的 STA 停成 idle（关自动重连 +
  `disconnect(false,false)` 保 NVS）；已连接 STA 原地扫（合法）。
  扫描终态（完成/失败/丢弃/中止/启动失败）恢复保存槽位重连。
  4_1 三处接入（start/poll/abort），4_2 同步扫描前后成对调用，
  空列表 + 扫描失败时标题行显示 "Scan failed - retry every 10s"。
- 版本 v1.2 → **v1.3**。教训：**"无反应"型症状先查驱动层状态机，
  渲染修复在驱动拒绝面前是空转。**

## 2026-09-17（v1.2：评审修复轮——095e41a..301c571 三方结果处置）

- **评审结论**：Claude C / GPT C / Grok A（`session-batch-review-result-
  095e41a..301c571-{claude,gpt,grok}.md`）。修复轮处置：
- **P1（GPT/Grok）Whoami 旧账号结果回写**：请求/结果携带**配置代次**
  `s_wa_cfg_gen`（Cfg 保存成功时 ++，契约规则 2 首字段 gen）；迟到
  profile 代次不匹配即丢弃，不再渲染/写 NVS——修复"Refresh A →
  Save B → A 响应到达 → 重启后 B 配置显示 A 资料"反例。
- **P2-5 核销失实改正（Claude/Grok 证伪）**：上轮申请称"核实既有实现
  已防"实为只核了 `sleep_do_enter()` 的深睡互斥，漏了 `idle_sleep_
  timer_cb` 的**Sleep 屏压栈**——本轮补 `ota_busy()` re-arm 真修。
  **教训：核销声明必须核代码，不是核提交说明（CLAIM_ONLY）。**
- **P2（GPT）OTA NTP 与 AI 开关解耦**：HTTPS 分支无条件
  `http_ensure_time(5000)`——原以 AI "Trust self-signed" 模式门控
  NTP，冷启动 + 开关开启时 CA 校验静默失败。
- **P2-6 闭合**：`async_ipc_contract.md` 补 Whoami/OTA 三行（含配置
  代次语义、指针通道、单飞前置位）。
- 版本 v1.1 → **v1.2**。

## 2026-09-16（v1.1：真机回归修复——EPD 灰色不可见 + WiFi 扫描空洞）

- **EPD 灰色文字系统性不可见**（issue_list §23，真机回归发现）：EPD
  二值化阈值 `<128→黑` 使 `LV_PALETTE_GREY`（158）渲染成白色——全项目
  14 处灰色状态行/边框从未显示过（含 OTA 屏与 AI Test 状态反馈）。全部
  改黑色（v1.0 报告的 whoami FW label 三处 + 11 处机械清扫）。
- **WiFi 扫描列表中文名空洞**（存量）：CJK SSID 被过滤后列表不压缩，
  渲染遇空行即断——最强 AP 为中文名时整表空白；顺带修 `name[16]` 无
  NUL 隐患与隐藏网络空 SSID。
- **`[FW] <版本串>` 开机串口打印**：任何串口抓取即可识别在跑的构建。
- **版本 v1.0 → v1.1**（小改动升 y，策略首次执行）。

## 2026-09-16（"OTA 版本致死"假案定案 + 强制校验刷机脚本）

- **固件版本号体系上线**（`fw_version.h`，x.y.build 基线 **v1.0**）：
  x=大升级批次、y=小改动（手动维护），build=编译日期（`__DATE__`
  自动生成，不手工维护）；显示于开机 splash、系统信息 "SF Version"
  （替换 LilyGO 上游的 `v2.4-260320`）与 Whoami Cfg 页 AI Provider
  下拉框下方的 `FW: vX.Y build yyyy-mm-dd` label。
- **Whoami Me 页 profile 缓存**（NVS `whoami` 命名空间）：进屏/开机
  直接渲染缓存（状态行显示 `cached <日期>`），不再每次拉远程；仅
  无缓存的首次使用或手动 Refresh 触发网络；Cfg 保存（换 key 可能换
  用户）自动清缓存。Cfg 页布局微调（Save/Test/status 下移 16px 给
  版本 label 让位）。
- **两台机"变砖"假案定案**（issue_list §22）：刷入 `095e41a` 后两台机
  零输出复位循环，根因**不是固件**而是分块刷写未逐块校验（USB CDC
  掉口后一块未写入，bootloader 哈希拒绝、应用从未启动——`entry
  0x403c98d0` 实为 bootloader 入口）。决定性实验：同一台机验证完整刷入
  `095e41a` 后 30s 零复位正常启动；map 级 diff 证明两版固件唯一实质
  差异是 +56.7KB×2 flash rodata（CA 包 static 双拷贝）。
- **`scripts/flash_verified.py` 入库**：分块刷写 + 每块强制
  "Hash of data verified." + 失败即中止 + 可选 `--readback` 整段回读
  比对；本事件用它恢复了两台机，此后固化为唯一刷机路径。
- **顺带核实**：`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 属实、两台机
  otadata state=VALID、`verifyRollbackLater()` override 真机无影响；
  OTA PENDING_VERIFY 真实回滚路径仍待真测矩阵。
- 设备状态：`28:37:2f:91:2c:20` 刷至最新 `095e41a`（实验+验证）；
  `10:20:ba:34:18:5c` 按指令留在 `71c09e7`；`10:20:ba:34:19:ec` 未连接。

## 2026-09-15（会话批次：八组功能/修复 + V1.0 面板假死事件）

- **八组变更单 commit 批次**（`71c09e7`，评审申请
  `docs/reviews/session-batch-review-request-71c09e7.md`）：① CA 根修复
  （+DigiCert Global Root CA / AAA Certificate Services 两个指纹固定额外根，
  minimax.io/.com X509 fatal 修复，122→124 根 + PC 全链验证脚本）；② 语音
  AI 多轮上下文（`openai_chat_multi`，4KB 整轮预算）；③ 5 分钟无操作自动
  深度休眠（复用 Sleep 屏）；④ PenPal 三修复（校验/发送失败 msgbox 化、
  msgbox 中文 SimSun 字体、reply title 线程锚定锁定）；⑤ Whoami App
  （`/users/me/profile` + PenPal Cfg 迁入双 tab；含 Z 序与队列生命周期两处
  崩溃/失效修复）；⑥ 菜单重排（Whoami/Wifi/Lora 三槽对调）；⑦ Voice AI
  TTS 开关（NVS 持久化）+ Trust/TTS 开关 EPD 状态配色（CHECKED 选择器）；
  ⑧ **OTA 完整实现**（设计稿 v6 落地：`ota_update` 模块指针通道/单飞/
  ECDSA P1363/流式 SHA-256/45s+10min 超时；SCREEN2_2 UI 全屏吸收覆盖层；
  启动 WDT 自证窗口 + 五喂狗点——真机自证一次通过；签名/发布脚本
  `scripts/ota_sign.py` + 密钥生成 + 信任锚；Sleep/Shutdown/低电互斥 +
  10 分钟安全阀）。
- **V1.0 面板假死事件**（issue_list §18）：机 #3（`28:37:2f`）在诊断复位
  撞上面板上电窗口后面板控制器假死，所有固件表现"慢启动"（每页 Busy
  Timeout）；**拔电池 10s 真断电修复**。出厂固件矩阵实测（§19）与 USB
  枚举问题集（§20）一并归档。
- **硬件诊断脚本入库**（`a8eb83a`）：串口抓取/复位、ESP 镜像分区表解析、
  SPIFFS dump 密钥扫描。
- **机 #3 恢复流程**（存档）：整片擦除 → 本仓固件 → V1.1 SPIFFS 分区克隆
  （env.cfg 全六键，镜像留存 `backups/`，gitignored）→ 启动零 Busy
  Timeout；注意其 NVS 已清空，PenPal base 首次取 env.cfg 的局域网地址，
  需在 Whoami Cfg 保存一次 HTTPS 域名（TODO §PENPAL_BASE 条目）。

## 2026-09-13

- **OTA 设计稿 v3 送复审**（[`docs/ota-update-design.md`](ota-update-design.md)
  `be6a52c..`）：按 v2 轮三方复审（Grok A / Codex C / Claude C）逐条处置
  5 P1 + 9 P2——全局硬件锁+取消令牌（gen 不再当写互斥）、task WDT 8s
  自证窗口（自证前任何死循环可恢复，修正 v2"冻结不回滚"漏洞）、验签公
  钥入 tracked `ota_trust_anchor.h`（公钥是信任根不是秘密）、`notes`+`seq`
  入签名（单调递增防降级）、低电关机/Sleep/按键与 OTA 全程互斥、恢复发
  布流程+USB 回退完整命令、清单 URL 配置源恢复、签名编码钉死 IEEE
  P1363。悬空引用两处修复。实现顺序改为回滚真测先行 + 分组提交。

## 2026-09-13

- **OTA 设计稿 v4 送复审**：v3 四方复审（Grok C / Codex C / Qwen C /
  Claude A）三方收敛于同一 P1——8s WDT 自证窗口 < 现有 `setup()` 阻塞
  profile（`A7682E_init` 6.1s / `GPS_Recovery` 6.4s / EPD 全刷）。v4 重
  设计：先校准后定值（两机打点 → T=实测×2 下限 30s）+ `esp_task_wdt_
  init(T,panic)` 显式 re-init + 四处具名喂狗点 + 自证点改 EPD 首帧完成
  序列号（弃 `lv_async_call` 语义）；另收 seq 提交时机（end 后写）、公
  钥格式钉死 65B 未压缩 SEC1、Check 移入 worker、进度原子轮询、低电安
  全阀等 7 P2 + 5 P3/Nit。申请头按 Codex 意见补"最没把握"自评段。

## 2026-09-13

- **OTA 设计稿 v5 送复审**：v4 四方共同 P1——签名对象把 `sig` 自身编入
  （自指不可构造，实为 v4 措辞从 v3 显式六字段枚举退化）——恢复显式
  六字段枚举修复；TWDT 可实施合同（`add(NULL)` 禁写函数名、喂狗点
  `s_boot_wdt_subscribed` 守卫防自证后 ESP_ERR_NOT_FOUND 刷屏）；取号
  副作用隔离（`ui_disp_full_refr_seq()` 单次存变量）；单飞守卫
  `s_ota_inflight`（cap 1 覆盖 Check+Update，跨 gen/页面）；快照所有权
  链（结果携带 manifest→UI 持有→launch-time copy，TOCTOU 关闭）；
  `A7682E_init` 数值修正 8.4s；校准度量改最长喂狗间隔。自评③ 显式提请
  裁定"屏坏固件回滚"取舍。

## 2026-09-13

- **OTA 设计稿 v6 送复审**：v5 四方（Claude A 首个 L1 / Codex C / Grok C /
  Qwen C）残余全收——结果队列改**指针传递**（worker new / 每拍排空 /
  delete 恰一次，禁含 string 结构按值入队——Codex P1）；inflight/锁唯
  一出口递减 + `xQueueOverwrite` 永不阻塞（Codex/Grok/Qwen P2/Nit）；
  `factory.ino` loop 无条件排空（Grok P2）；TWDT add 失败守卫一致
  （Codex P3）；规范化字节串编码钉死（Grok P3-1）；EPD 库内
  `_busy_timeout` 纳入 T 下界 + SD 段补喂狗（Qwen P3-1/Grok P3-2）；
  覆盖层全屏吸收触摸、§8.3 用例重写（Grok P3-3）；按 Qwen P2 撤销
  "屏坏固件回滚"假声明、自证语义改为"软件刷新序列完成"。

## 2026-09-13（第二批：评审治理 + PenPal 焦点/网关修复 + 双机同步）

- **评审指南 v1.3 落地**（`8467051`，+ 跨项目参考说明
  `docs/review-guide-v1.3-changenote.md`）：设计稿收口机制——三类发现
  分轨（协议 P0/P1 挡 L1 / 实现合同 P2 绑 L2 / 完备性 P3）、设计稿
  L1+A 可附合同表、delta 复审、G-开工门禁、严重级阻断纪律（拒绝主审
  制/DOC 一律有声表/轮次自动 A-/通用四态，各附反例）。
- **OTA 设计六轮收敛，G-开工通过**：v5 四方 C/A → v6 修复结果通道指针
  语义、生命周期合同、自证语义声明后，复审 Grok/Claude 均 A（L1）。
  设计冻结，残留转实现合同表；实现顺序 = 校准打点 → 回滚真测 → 四组
  提交（待开工）。
- **PenPal 三处修复 + 网关**：CFG Test 按钮（`23dca83`，手动关闭弹窗
  `836acf5`）；CFG/COMPOSE 触摸焦点同步（`7ee150b`/`54cfc79`/`cfe993d`
  ——触摸哪个框，⌫/打字作用于哪个框）；全请求带 `X-Gate-Pin`
  （`39c7472`，Apache 反代门禁，纵深防御层）。
- **双机同步最新固件**（COM5/COM6，零 panic）；env.cfg 含
  `MINIMAX_AUDIO_KEY`（126B）与 PenPal 配置。用户实测：HTTPS 新域名
  Test 正常；cfg 触摸焦点 bug 修复确认。
- **文档更新**：review_guide v1.3 条文 + 裁定；OTA v5/v6 结果与 qwen
  补录（4 文件）；penpal-design CFG 节补触摸焦点/Test/门禁三条。

## 2026-09-11（第二批：AI Chat 语音全链路）

- **OTA 固件远程升级——设计稿待评审**（[`docs/ota-update-design.md`](
  ota-update-design.md)）：核实 A/B 双槽 + 预编译 bootloader 已开回滚
  开关；方案为 Settings 手动触发的 HTTPS OTA（清单 JSON + HTTPUpdate +
  电量前置 + 新固件自证），三层失败模型（无操作回退/自动回滚/USB 兜底）。
  含 6 项请评审重点（自动更新、自证点、明文限制、签名、组件选型、回滚
  实测）。**未实施**，等专家评审后开工。

- **AI Chat 语音链路重构并全链路跑通**（`c3eb661..8409527`，真机最终验
  收"所有问题解决"）：去 Google 化——ASR 走 MiniMax `speech_to_text`
  （multipart，curl 风格横线边界、不带 response_format 否则 400）、对话
  直连 MiniMax M3（`api.minimax.io/v1`，AI Config minimax 条目优先、
  env `MINIMAX_AUDIO_KEY` 兜底）、TTS 走 `t2a_v2`（hex mp3 流式落
  PSRAM→SPIFFS→`connecttoFS`）。**MIC 键=按住说话**（按下录音提示、松
  开进入 Waiting；键位 (3,6) 双机实测一致，正常层专属码 `'\f'`）。
- **途中修复的一串坑**：① 96/79 字符上限把 126 字符的 sk-api key 截成
  95/79（env 解析 val、resolve_chat_cfg 的 k[96]、AI Config 输入框，最
  后一处漏网靠三行判据日志锁定）；② provider 表 minimax 默认 base 是国
  内域 minimaxi.com，sk-api key 在那里 401——默认改 `api.minimax.io`；
  ③ minimax.io 对"无 key"与"坏 key"回同一段 1004 文案，易误判为请求格
  式错误；④ M3 回复带 `<think>` 思考块，统一剥离（否则原话被挤出屏幕、
  TTS 念标签汤）；⑤ **光标闪烁→EPD 局部刷新阻塞主循环→音频泵断粮**：
  app 内 TTS 断续噪声、退出反而清晰——关光标闪烁 + 播放期抑制 EPD 刷
  新（退出钩子兜底释放）；⑥ 发送等待弹窗（AI Text 同款）补齐，录音阶
  段不再被"Waiting server reply"误导。
- **文档/工具**：键盘双机映射复测（附表更新，`baf2e86`）；openai_chat
  打印实际端点+key 长度、env 解析打印各项长度（判据日志常驻）。

## 2026-09-11

- **§3.3 推翻：4G 批次（机 #1）音频输出可用**：机 #1 刷入含
  `pcm5102a_init()`（GPIO41 拉高）+ 开机生成 `/pcmtone.wav` 的主固件后，
  Test→PCM5102A 与 PCM5102 app 播放**耳机实测出声**（当日 A7682E 无 SIM
  初始化失败，菜单门控显示出 PCM5102 入口）。8 月"板上无 DAC/MP3 不可
  行"实为 `test_i2s_probe` 未开音频电源轨所致。GPIO41 在两台机上均为
  音频段电源（4G 批次兼模组电源）。**产品含义：两台机都可做 TTS 朗读**。
  同日：机 #1 刷机前完成 NVS+SPIFFS 凭据分区备份（`backups/
  device1-com5-20260911/`，md5 nvs `873ae460...` / spiffs `523a2623...`）。

## 2026-09-10

- **麦克风→语音识别全链路实测通过（终判）**：跟读探针（`test_pdm_mic`
  v12/13：开机自动"英文样例→嘟→录音 6s→滴滴滴判定→回放→停"，串口
  R/D/P 遥控）+ 无损导出（`script/probe_capture.py`：DTR 关断开串口防
  复位覆盖、base64 补 padding、重试到长度精确匹配）+ **GLM-ASR-2512
  转写逐字正确**（"The quick brown fox jumps over the lazy dog."，仅尾
  部一词瑕疵）。低灵敏度麦（-20~30dB）在"对嘴大声 + 200Hz 高通 + 24×
  增益"链路下达到识别级——**语音输入可立项**（`384a7ea`/`ec97bf9`）。
  途中修复：判定阈值改为第 1~5 秒原始峰值>900（第 0 秒开机伪峰）、双重
  减均值把整段钳成 +32000 恒定的 bug。
- **测试机 #2 麦克风判定（修订终案）：在板、低灵敏度约 20~30dB**
  （issue_list §16.1，`test_pdm_mic` v1..v11）：初判"不可用"后用户在
  回放中听到房间外放音乐，追加持续元音对照——环境底噪 150~650，近距离
  大声"啊——"整窗抬至 1450~**3394**（判定链首次触发：SPEECH captured
  gain 8.8x + 三声滴 + 回放）。正常音量对话（600~1300）与底噪重叠是
  此前全轮"安静窗口"的成因。**可用性**：大声 + 对嘴 + 数字增益链可
  拾音，STT 上行质量待实测（`gemini_send_audio()` 现成）。探针固化
  4 条音频铁律（GPIO41=输出段电源、Audio 驱动重建复刻构造配置、PDM
  只能借 I2S0、回放统一 44.1k 立体声），直接服务读信 TTS。同日：整片
  16MB Flash 备份（32/32 块零重试，md5 `45cfb17d...`）+
  `script/flash_backup.py` 入库；机 #2 出厂 demo 已被探针覆盖（其余
  分区在备份内）。
- **测试机 #2 入池：V1.1 音频选配版**（issue_list §16）：新设备（COM6）
  经三步证据链判定——DRV2605 在（V1.1 批次）/ A7682E AT 无响应 + 原厂
  菜单硬件门控（无模组才显示 PCM5012 app）/ **PCM5012 app 插耳机实测
  出声**。PCM5102A 在板工作，**读信 TTS 硬件路径成立**（本机，仅耳机
  输出）；PDM 麦克风探针（`test_pdm_mic`）已刷入待说话实测。本项目固件
  移植前置：触摸需接 CST328 驱动（现树只有 hyn/CST66xx 路径）。README
  Hardware 节同步双机差异。注意：探针刷入已覆盖机 #2 出厂 demo 固件
  （其余分区完好并已整片备份，见 backups/）。

## 2026-08-29

- **PenPal 点击死机根治：LVGL 内存池 48K→64K**（真机三段 bisect 定位，
  `721e04a`）：进入 PenPal 后点任何链接死机/重启，多轮上报。设备 bisect：
  `1269bf7` 稳定 → `f8d73f6`（行表头拆分，+10 label）一点即崩（回溯
  `pp_waitbox_show → lv_btn_create`，EXCVADDR 0x22）→ 空对象探针同样崩。
  探针带 `pp_dbg_pool()` 池水位实锤：点击时 48K 池仅剩 **524B**，等待框
  分配失败；`1269bf7` 的"稳定"只是 ~1K 余量勉强够，早就在悬崖边——
  1269bf7 时代的偶发 topic 死机、诊断构建的崩溃点漂移
  （`lv_mem_buf_get` 挂死等）都是同一池耗尽的不同表现。修复后 RAM
  50.1%→55.1%，同点水位 ~17K；真机全路径复测通过（用户确认）。
  附带坑：改 `config/lv_conf.h` 不触发重编（`-include` 无依赖跟踪），必须
  `-t clean`，否则池"假扩容"（build 文档坑 7）。`pp_dbg_pool()` 留作
  观测点。诊断批次（心跳/栈扫描/面包屑，stash）随根因落定整体弃置；
  `ui_penpal_write.cpp` 的 [PPW] 面包屑同撤（topic 冻结即本条）。

## 2026-08-28

- **TLS 信任切换为全量 Mozilla 根证书包**（用户决策，`07c5fdb..32204bb` 2 个
  commit）：继 deepseek/minimax X509 修复（`78239c5` 手工补两根）后，用户
  决定不再逐家补根。新增 `scripts/gen_ca_bundle.py` 把 curl.se Mozilla 信任库
  （121 根）转为 esp_crt_bundle 二进制格式生成 `ca_bundle_full.h`（55.6KB，
  const 驻 flash）；`http_apply_tls` 改 `setCACertBundle()`。实测代价：flash
  31.4%→32.0%，heap 反而更省（旧 `setCACert` 每次请求把全部 PEM 解析成 heap
  证书链；新路径只在 heap 建 ~0.5KB 指针索引、握手时按主体名二分查找、仅解析
  命中的一把公钥）。PC 端 openssl 对五家 provider 链以同一信任集验证全 OK；
  生成头回读校验（格式游走/严格排序/SPKI 可解析/主体集合=源）通过；分块烧录
  9/9 + 开机冒烟 ✅。`ca_bundle_check.py` 被生成器自检取代并删除。真机 AI
  Test 复测 ⏸；申请 `07c5fdb..32204bb`

- **CA bundle 补根：deepseek/minimax X509 校验失败**（真机反馈，
  `78239c5`）：AI Config Test 在 CA 校验模式下对 deepseek/minimax 报
  `X509 - Certificate verification failed`——服务端链分别锚定 Amazon
  Root CA 1（Starfield 交叉签）与 USERTrust RSA（Comodo 交叉签），均不在
  原 6 根 bundle 内（PC 侧 openssl 实测五家 provider 链定位）。追加两个
  自签根（官方 amazontrust.com / Mozilla bundle，均至 2038）；PC 用固件
  bundle 原文对三链 `openssl verify` 全 OK，`ca_bundle_check.py` 8/8 解析
  通过，分块烧录后开机冒烟 ✅。真机 Test 复测 ⏸；申请 `78239c5`

- **配置界面秘密遮蔽显示**（用户安全需求，`e8379b3..3a92971` 4 个模块
  commit，基于 2d00f3f 重落地）：PenPal Cfg key / AI Config key / Wifi
  Config pass 三处输入框不再明文渲染存储的秘密——中间 1/3 用星号替代
  （WiFi pass 至少 4 星），首尾保留可辨认。配套"影子缓冲"机制：真实值驻
  `_real` 影子缓冲，所有读框路径（Save/Test/Connect/字段切换/WiFi 槽位
  切换的出向槽自动保存）发现框内仍是未改动的遮蔽串即沿用真实值，星号
  永不入 NVS/请求；遮蔽态首次按键清空重输（杜绝真字符混星号）；保存/
  连接成功后按新值重遮蔽（WiFi 连接失败不重遮，保留明文便于改错）。共
  享助手 `secret_mask.h`。首轮在旧基线 aecddc3 上实现后因远端演进
  （WiFi 槽位 / PenPal provider 共享）整轮 rebase 重落地。烧录一波三折：
  整刷在 ~21-29% 处 USB CDC 持续失联且失败重试擦残 app 分区（设备一度
  无法启动），改用**分块烧录**（8×256KB 逐块 write_flash + 幂等重试）
  恢复并完成（坑 6 沉淀至 build-and-code-structure §5）；烧录后开机
  冒烟 ✅（NVS 槽位凭据完好，slot 0 自动重连）；三屏交互真机回归 ⏸；
  申请 `e8379b3..3a92971`

## 2026-08-26

- **PenPal 响应缓存 + THREAD 强刷 + msgbox 触摸关闭**（产品需求）：
  SPIFFS `/penpal/*.json` 原始响应缓存（TTL 2 天；getter 成功写 /
  UI 侧 parse-only 复用读）；HOME 进屏缓存优先（全命中免网络，
  `cached - press Sync to refresh`）、手动 Sync drop 缓存强拉；THREAD
  开启缓存优先 + 页内 Sync 按钮强制重拉（dropped 提示挪信头防重叠）；
  `pp_msgbox_show` 补 Close 按钮（TIPs 失败弹窗触摸不可关）。缓存解析
  直接进全局 `pp` 状态（无栈中转，§12 规则）。真机回归 ⏸（issue_list §14）
- **c1c6a14..ff6d906 四方评审结果齐至 + 修复批次**（Codex **C** 2×P2 /
  Claude **C** P1+2×P2 / Gemini **A** M1-M3 / opencode **C** P2-1+4 Low）：
  采纳 Claude P1（WiFi 槽位切换静默丢草稿 → 切换即自动保存两框，设计
  §3.5 语义更新）、Codex/Claude P2×2（PenPal provider 状态行改按当前
  下拉选择即时预览 + 两步保存分区报告）、Gemini M1（enum 返回值检查，
  并入状态行重写）、opencode P2-1（中文超时文案在 montserrat_14 下为
  方块 → 改英文）+ Low×4（UTF-8 截断边界 / provider 焦点 `\b` /
  `collectHeaders` 让诊断日志生效 / 弹窗吞键后清 FIFO）。拒绝 Gemini
  M2（失实：`exit4_1` 已清理 popup）、M3（与设计 §6.3 重复登记）。
  另按 Claude acc3893 复核勘误 §10/CLAUDE.md 根因表述：`create()` 在
  每次 push 时执行（非注册期），屏幕控件树每访问重建。编译通过；真机
  回归 ⏸（issue_list §13）
- **acc3893 / 423b312..bfa7a16 三方评审结果**（各 Codex/Claude/Gemini
  三份，全部 **A 全量接受**）——无代码改动；acc3893 的 Claude Low
  （根因表述勘误）落入上述 2026-08-26 批次

## 2026-08-22

- **PenPal 首轮真机回归：1 崩溃 + 1 导航 + 1 需求全修**（`423b312` /
  `e70b591` / `bfa7a16`，issue_list §12）：返回必崩 = `pp = pp_state_t()`
  把 ~15KB 聚合临时对象压进 8KB loopTask 栈（改为 `pp_state_reset()` 逐字段
  原地复位 + 点击返回 `lv_async_call` 延迟 pop）；菜单滑动连发跳页
  （`gesture_dir` 松手前持续有效，30ms 轮询连发 N 次 → 边沿触发修复）；
  WiFi Test 结果框增显示设备 LAN IP。三处均真机复测通过；sync 回归待
  服务器改 `--host 0.0.0.0` + 防火墙放行后进行。设备端 `/env.cfg` 已注入
  PenPal 测试配置（writer 固件方案，无跟踪文件变更）。申请
  `423b312..bfa7a16`
- **PenPal 实现评审结果 + 修复**：Codex 对 `b48f584..5329383` 出
  **C 部分接受**（2×P1 + P2）——P1 memset 清零含 `std::string` 的
  payload/polish 结构（首次 Send/Polish 即 UB）；P1 create 期自动同步被
  entry gen++ 作废且 stale 分支不释放 busy（已配置设备进入即永久卡死，
  根因=scr_mgr 注册期就 create）；P2 READ Close 不中止任务、连续重试
  无界堆积。`acc3893` 全部修复：值初始化替代 memset、自动同步移 entry
  （+同族"后台 SEND 退屏 busy 泄漏"顺带关闭）、`s_pp_inflight` 原子
  计数并发上限 2。编译/烧录/冒烟通过；修复路径真机回归 ⏸；申请
  `acc3893`
- **笔友 App 实现落地**（§9 预案四个模块 commit，`b48f584..5329383`）：
  `b48f584` API client（8 端点 + 幂等键 + `thread_root_id` 锚点 + 配置链；
  http_utils 增量导出共享 TLS 策略）；`16c13e3` R9 落地后的客户端恢复
  （`pen_pal_id<=0` 省参单查 → HOME 显示 null 行 + THREAD 只读，设计 v3.2）；
  `b231dd3` 屏幕 UI（ui_penpal 三件 7 内部页、单队列异步框架、waitbox
  Close 按类型拆分、发送幂等 + 后台编辑锁）；`5329383` 菜单第 3 页
  （9/9/1，menu_page_apply 统一切页 + 循环页点 + img_penpal 生成脚本，
  顺带修存量"初始页点全黑"）。编译/烧录 COM5/开机冒烟通过；真机回归
  §7 清单 ⏸ 待用户实测；评审申请 `b48f584..5329383`
- **笔友设计 v3.1 复审 A 全量接受**（Codex `…-0ca4f4b.md`）：P1 编辑锁 /
  P2 null 行过滤 + R9 登记均确认——**v3.1 为实现基线**，R9 按服务端排期跟踪；
  实现按 §9 拆 commit 1-4 可开工
- **P2 实测定稿**（服务器恢复运行，GET-only）：`GET /emails` 的 `pen_pal_id`
  必填（缺 422）且拒 0（400），null 笔友残留行主路与 subject 回落路均不可用
  → v3.1 改为 HOME 过滤 null 行 + **R9 服务端需求**（`pen_pal_id` 改可选，
  `thread_root_id` 单查按 key 授权；上线后恢复"显示 + 只读"）
- **笔友设计 v3 复审 → v3.1**（Codex 结果 `…-8109c9e.md` **C 部分接受**，
  两项边界定稿）：P1 后台 SEND 期间误清新编辑草稿 → SEND 在飞 COMPOSE
  编辑锁（Close 不解锁）+ 消费时 payload 快照比对；P2 `pen_pal_id=null`
  残留行查询未定义 → 仅 `thread_root_id` 查询（未实测，登记 §7-2 服务端
  预验前置，拒绝回落 subject 兼容通道）。幂等键/线程锚点/标题字节预算
  整改均获通过。v3.1 待复审
- **笔友设计 v3**（服务端幂等键落地后修订，待复审）：新增 §2.2
  `Idempotency-Key` 契约（32 hex、重放 200 + `Idempotent-Replayed: true`，
  demo 实证）；§3.2 waitbox Close 按类型拆分——SEND"不再等待后台继续"
  （busy 保持），读/算型维持取消；幂等键 RAM 生命周期（快照比对复用、编辑
  换新、确认作废）；P2：Title 限长 60 字符→**56 字节**（UTF-8 边界，
  `Re: `+56=60 ≤ 显示缓冲）；跟进服务端线程模型——取线程/回信改
  **`thread_root_id` 精确锚点**（subject 兼容通道会合并同题多线程，v2 取数
  错误的正确性修复）；⑩ 本人 profile 端点已上线仍暂不接入
- **Low 收尾批次两份 Codex 结果到达**：`c8f62f3` **A 全量接受**（a924c4e
  环路闭合）；`71fa528..a58a73c` **C 部分接受**——P2：forecast JSON 仅有
  `list` 就置 `data_valid` 过宽，空列表/全过期/缺 `dt` 时零条也判有效、
  零值天气上屏 → `141942d` 收窄为 `hourly_count > 0 || daily_count > 0`
  门控（0 条返回 false → partial/失败分支，不推进时间戳不落缓存）；
  解析测试要求登记为验证缺口（无实机测试框架）。申请 `141942d`
- **双评审 Low 收尾批次**（服务端幂等键排期期间）：`71fa528` weather 仅
  forecast 成功也置 `data_valid`（冷启动 current 失败时 forecast 上屏 +
  partial 提示复通，issue_list §9.4）；`a58a73c` Trust 开关状态行拼出作用域
  `TLS: ALL HTTPS …`（§7.4）；issue_list §4.1 复核闭合（build doc §8 已于
  08-16 修好，登记滞后）；Kimi v2 四项 Low 预铺入 `penpal-design.md`（给
  v3 铺路）。申请 `71fa528..a58a73c`
- **第四批评审修复**（GPT 跟进评审 3 项 P2 全部关闭，Codex 结果 **A**）：
  `c27cb39` Weather 部分刷新不再缓存为成功（current/forecast 分别跟踪，
  仅双 ok 推进时间戳/落盘，部分刷新失效可重试 + 状态行提示）；
  `153eef7` CI paths 补 `script/**`；`3475c9b` factory.ino TLS extern 改 `void`
  （issue_list §9）；申请 `c27cb39..3475c9b`
- **四份 Codex 结果齐至**：第三批 `6d26699..1473ef9` **A**（结果文件以清洗前
  旧哈希 `3f654a5..4c3a331` 命名——该批申请文件当时漏写，`eefb2fd` 补齐并
  配对闭合）；`de78338` 菜单幽灵页 **A**；`c27cb39..3475c9b` **A**；
  `a924c4e` SD 提示 **C 部分接受**（1 项 P2）
- **SD 提示 P2 修复**（`c8f62f3`，issue_list §3.4 跟进）："有卡但挂载失败"
  不能断言为 FAT32 格式问题——改两行提示 `SD hint: mount failed` +
  `try FAT16/FAT32?`（事实 + 建议），两处过度断言注释同步改准确；
  申请 `c8f62f3`
- **Kimi 双评审四份结果齐至（全 A）**：第三批 / `a924c4e`（与 Codex P2 意见
  相反，分歧登记进 `c8f62f3` 申请；`a924c4e` 为清洗前 id，当前 HEAD 对应
  `23030c9`）/ `de78338` / `c27cb39..3475c9b`；两个 Low 登记待办
  （issue_list §7.4 Trust 开关影响面未注明、§9.4 冷启动 + 仅 forecast 成功
  无 partial 提示）；k3 设计评审结果文件改名
  `penpal-design-review-result-kimi.md`（引用同步，**设计 v2 复审仍未到达**）

## 2026-08-21

- **第三批评评修复**（评审 `pda2-review-result-2026-08-07-20.md` 遗留 3 P2，全部关闭）：
  `6d26699` CI 尊重外部 `PLATFORMIO_SRC_DIR`（set_srcdir.py 不再覆盖矩阵选择）；
  `52f709e` GPS 写侧临界区（displayInfo 一次锁发布 11 字段 + getter 补锁）；
  `950fcfe` AI Config 补 Trust 自签 TLS 开关（NVS `ai`/`tls_insecure` 单键 +
  `\v` 键切换 + 开机 `openai_tls_apply()`）；台账 issue_list §7。申请
  `6d26699..1473ef9` 已递交
- **安全收尾（SECURITY.md checklist 全勾）**：`git filter-repo` 历史清洗
  （3 条替换规则，全对象库 blob 扫描 0 命中）→ `HD-V2-250915` force-push 且
  远程 blob 验证干净；OpenRouter key 作废旧 key、新 key 只存设备 `/env.cfg` +
  `data/env.cfg`（均 gitignored）；`config_keys.h` 清空为模板（OWM key/深圳坐标
  同日迁入 env.cfg）。旧→新 commit 映射：`.git/filter-repo/commit-map`
- **菜单幽灵页修复**（`de78338`，issue_list §8.1）：`page_num` 由页数改最大下标
  语义 `(MENU_BTN_NUM-1)/9`——18 项时第 2 页再左滑不再进入不存在的页 2
  （k3 设计评审 §1.1 顺带揭出的存量 bug）；申请已递交
- **笔友 App 设计两轮评审 → v2**：Codex 首轮（C 部分接受，已归档）+ Kimi k3
  二轮独立复核（C 部分接受，`8019da8` 归档，含对首轮 3 处失实引用的勘误）；
  v2 修订 `97e5d2f` 落实 3 条前置（菜单页数公式最大下标语义 / LLM 超时 180s /
  `s_pp_busy_gen`）+ 同批 8 项（结果 type 字段、env.cfg 容量、数据模型三处等）。
  **待复审**，通过后按 §9 预案实现
- **GPT 跟进评审到达**（`b4082ff`，`pda2-review-result-2026-08-07-21-gpt.md`）：
  3 项 P2 **待修（2026-08-22 批次）**——weather 部分刷新误存成功（issue_list §9.1）、
  CI paths 漏 `script/**`（§9.2）、factory.ino TLS extern 声明不一致（§9.3）；
  CA bundle 6 根 + NVS 双槽 11/11 复核通过

## 2026-08-19

- **收尾批次 1**（`764e7bf`/`6ce2b4b`/`980b6df`，第 31 轮 A 接受）：
  - usage 月度清零改**本地月界**：setup 设 `TZ=CST-8`，stats 用 `localtime()`
    （第 28 轮 O1 闭合）；Calendar/Sleep 时间戳同享该时区（此前全按 UTC）
  - WiFi 扫描覆盖层/结果 banner 在屏幕被 push 覆盖时隐藏（`exit4_1`，第 25 轮闭合）
  - `test_keypad` 加 raw/driver 列镜像换算注释 + README §2 说明（issue_list 1.4 闭合）
- **MP3 屏取消（硬件定论）**：4G 版板上无 PCM5102A DAC——`test_i2s_probe` 探针两轮
  实测（1s 提示音 + 60s 音频音量 0→21 渐变、`running=1`、耳机经电脑验证正常）均无声；
  I2S 引脚 7/8/9 被 A7682E（RI/ITR/RST）占用。issue_list §3.3、探针示例保留作验证记录
- **allinone 取消（用户决策）**：不新开整合固件，pda2 即最终形态；设计稿归档为参考；
  Keys 演示屏为唯一遗留候选（未拍板）
- **SD 卡格式提示**（`a924c4e`）：120GB exFAT 卡挂载失败显示 0MB 无解释 →
  About System 屏增加 `SD hint: need FAT32 / no card`（cardType 在 f_mount 失败后
  仍可区分空槽与坏格式）；串口同步打印原因。**SD 卡仅支持 FAT16/FAT32**

## 2026-08-18

- **AI Config provider 原生下拉**（`fd7be74`/`d22007d`，第 28/29 轮 A 接受）：
  lv_dropdown 6 项预设（openrouter/deepseek/minimax/qwen/tencent/custom）+ Alt+Enter
  循环；每 provider 独立 key（NVS `key.<provider>` → /env.cfg `<NAME>_KEY`）；usage
  统计升 V3（`reset_month` 月度清零，NTP 哨兵防冷启动误清）
- **用户反馈修正**（`da0217f`/`4c3c9b1`，第 29 轮 A 接受）：env 默认 key 改名
  `AI_KEY`→`OPENROUTER_KEY`；openrouter key 链补 config_keys.h 兜底；custom 选中
  清空三框；Save 不再写死存储 `key.custom`（第 30 轮，`a2ecd7b`）
- **评审流程修订**：申请文件名 = 本轮实际覆盖 commit 首末（含两端，非 git 区间记法），
  已接受 commit 彻底出列（含结果文件接受范围核对，曾漏看第 28 轮范围）
- **真机回归回填**：15 项清单 11 项 ✅（用户实测 2026-08-18；13 为代码级核验）

## 2026-08-17

- **Secrets 配置链**（`0e78025`）：真实 Key 从跟踪源码移除——NVS → SPIFFS `/env.cfg`
  → gitignored config_keys.h → 空默认；Weather key/坐标同链（默认深圳）；SECURITY.md
  重写（历史 Key 视为已泄露，推送前 filter-repo）
- **真机回归第一轮**（用户实测）：多轮记忆 ✅、P0 Sleep 三项 ✅（合并门禁满足）；
  重启恢复 ❌ → 根因 **SPIFFS rename 目标已存在时失败**（第二次保存起静默失败），
  `867435e` 改 bak 三步换入 + 诊断串口，待复测
- **Copilot 复审（844a907..8d273cd）8 项**（`3cdff38`）：usage 落盘生命周期检查点
  （深睡/离屏/New flush + 10s 失败退避）、chat.log 升 **CHL2**、ai_stats blob 升
  **V2**（旧数据迁移不丢失）、测试注入改 fail_at、上下文裁剪状态行提示
- **修复 Copilot 复审抓出的确定性逻辑错误**（`c90307f..8d273cd`）：
  - chat.log 加载器把尾部校验和当消息头 → 合法日志必被误判损坏（加记录数头字段）
  - 多轮上下文轮次配对条件写反 → 上下文从未生效（修正 orphan 判定）
  - usage 统计 mutex 惰性创建竞态 → 改静态初始化
  - Sleep frame-wait 超时仍深睡 → 改为取消并提示
- 草稿持久化全生命周期同步（exit/Clear/成功/New）；SPIFFS 不可用时状态行明示 RAM-only
- usage 统计：chat/test 两组分离 + 持久化节流（60s/20 次最多一写）
- 可执行 NVS 双槽算法测试 `scripts/test_nvs_atomic_save.py`（11/11 PASS）
- 文档第三轮修订：allinone-design 与预研最终实现对齐（多轮、持久化、New、usage、双槽、CA 5 根）；CLAUDE.md 存储布局；issue_list §5.7；docs/reviews/README 分段评审条款
- 申请扩展为 `844a907..8d273cd`（21 commit）

## 2026-08-16

- **用户追加需求**：AI Chat 多轮上下文（整轮配对 8KB，`openai_chat_multi`）、usage
  用量统计（容错解析 8 项入 NVS）、Hist→New 改名、Key 补偿控制 C1（编译期
  `#warning`）/C2（`SECURITY.md`）
- **第 20 轮评审整改**（`844a907..538e6d0` 前半）：双槽 NVS 原子保存（暂存-校验-
  单键翻转）、扫描临界区（portMUX + 读也加锁）、Sleep 倒计时帧序号机制（watcher
  timer，修复"等待旧屏帧 + LVGL 重入"）、destroy4 清 busy、AI Chat 动态正文
  （std::string + 16KB 预算 + 单点截断）+ SPIFFS 原子日志（tmp+校验和+rename，
  不自动格式化）+ 重试复用气泡 + New 确认框 + 草稿持久化、AI Config 计费提示与
  Save 失败原因、CA 脚本 openssl 依赖检查
- **合并申请流程**：round 19/20 合并为 `eecebda..ceade9c`，`docs/reviews/README.md`
  固化合并与追溯规则
- **第 17/18 轮评审整改**（`bb1819b..e210b46`）：Sleep 屏倒计时 + timer 句柄保存、
  扫描 release-pending 目标计数、WiFi Test/Time Sync busy 代次、CA 检查脚本转
  Python（字节级提取，5 根证书 PASS）、AI Test 改最小 chat-completion（15s 绝对
  deadline + 超时递增代次）、AI Chat 每任务快照 + UTF-8 截断回退、异步 IPC 契约
  文档（`docs/async_ipc_contract.md`）
- **第 8-16 轮评审整改**（`01f8eac..8b96656` 及之前）：AI Text 聊天界面重构
  （WeChat 式气泡 + 滚动历史）、AI Config 三输入框 + Save/Test 语义、CA bundle
  修损坏的 ISRG X1 并扩至 5 根、CST-8 时区、Sleep 屏（ext1 BOOT 键唤醒）、
  WiFi 扫描生命周期（SCAN_DONE 事件计数 + 代次）、状态栏时间跟随电量刷新
- 键盘驱动（`3d98321`/`6a9ab00`/`2e559ad`/`6c51964`）：HD-V2 实测矩阵解码（无 Ctrl、
  双 Shift、Alt 临时符号层、Sym 锁定）、音量键 `'\v'`/麦克风 `'0'` Sym 层映射、
  TCA8418 `INT_STAT` W1C 溢出恢复、页面切换清按键 FIFO、触摸焦点同步

## 2026-08-15

- 搭建 pda2 编译环境（PlatformIO 6.1.19，`python -m platformio`）并首次烧录上机
- 建立评审工作流：申请带 commit id、按模块拆 commit、申请/结果归档 `docs/reviews/`
- 键盘问题定位与真机按键实测（配合用户逐键解码）
- WiFi 配置屏首版（下拉扫描 + 密码框 + NVS 存储）
