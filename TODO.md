# TODO

> 当前基线：`examples/pda2` 整合固件（原 allinone 目标已由 pda2 覆盖，
> 2026-08-19 用户决策）。设计评审：`docs/reviews/`；评审方法论：
> `docs/review_guide.md` v1.3（设计稿收口机制）。
> 本文件只保**未完成**事项；已完成工作看 `CHANGELOG.md`（按日期）与
> `docs/issue_list.md`（修复台账）。
> **刷机纪律（2026-09-16 起）**：固件写入一律
> `python scripts/flash_verified.py <COMx> .pio/build/pda2/firmware.bin`
> （issue_list §22：手写分块刷写漏一块 = bootloader 哈希拒绝假"变砖"）；
> **串行单机刷写**——一次只刷一台，严禁两台并行（坏镜像不应有同时砖掉
> 全部设备的机会）。

## 待办（2026-09-16 盘点，2026-09-20 更新，按优先级）

- [x] **v1.21 真机验收**（2026-09-19/20 完成，issue_list §33/§34）：
      new_dict 全功能走查 ✅（§33，其间修复布局溢出/页码/整页翻等
      A-1~A-6）、L1 双 Enter ✅（B1，§34 闭环）、LevelTest 回归组 ✅
      （v1.16/17/18 各项）。**剩余**：C3 慢扫描 F1′ 反例（需 >6s 慢扫
      描环境）；C2/C4 串口判据待正式记录；W-2 `scan failed(-2)` 首启
      快速重试（P3 待办）。字库缺字逃生通道：题库更新后重跑生成管线
      （DB 字符集提取 → lv_font_conv → 回读验证，见 CHANGELOG v1.16；
      IPA 字符 ː 与 PUA 私有区两个字形有意不含）。**词库/题库新增内容
      若超出 3955 字全集会出缺字，需重新生成 Font_Hanzi_16**。
- [ ] **OTA 真机验证矩阵**（实现已落地 `71c09e7`，设计 §8 剩余项）。
      **④ 局域网端到端已通过**（2026-09-20，issue_list §35：Check→
      Install→重启→自证→already up to date 全链路；台架环境
      `pda2_ota_bench` + `ota_test/` 服务已沉淀；顺带修复台架宏名 P2
      并完成信任锚换根 `325BDA1A`）。剩余：①校准打点（两机 setup 各段
      耗时 + `_busy_timeout` 实测）→ 复核 WDT 窗口 T=60s 假设、填
      `docs/ota-baseline.md`；②回滚矩阵（自毁固件，PENDING_VERIFY →
      WDT 复位 → 自动回滚，用 `pda2_ota_bench` 台架机做）；③覆盖层
      吸收 / 断网 45s 重试 / 低电安全阀 / 签名否定用例（大写 hex、
      前导零、错锚验签）。生产 https OTA 另需：生产站证书续期 +
      `/firmware/` 端点部署（设备侧已就绪）。真机已过：自证窗口
      `mark valid ok` 一次（机 #3）+ ④ 全链路（机 #1，2026-09-20）。
- [ ] **session batch 修复复审**（`095e41a` 修复轮：P1 rollback + 五项
      P2）：按评审指南 §2.5 修复轮安排 delta 复审（核销 71c09e7 四方
      评审发现的修复证据 + 反例击穿）。
- [ ] **v1.24-v1.28 评审申请**：NewDict 修复轮（§32/§33）+ Gate-pin
      （v1.26）+ OTA 台架与宏名修复（§35），范围按 `first^..last` 含两端
      记法、逐文件归属表（修正 §32 Nit-4 的申请记法错误）。
- [ ] **mainline（学习主线）设计评审 + 实现**：设计稿
      `docs/mainline-design.md` v1（`4b16209`）已提交 L1 设计评审
      （`docs/reviews/mainline-design-review-request-4b16209.md`，G-开工
      门禁）。通过后编码 v1.30：今日推荐/开课/逐题作答/结课闭环，
      口语题 v1 skipped（Round 2 接 ASR）；评审焦点与自评见申请文件。
- [x] **三份在途评审申请待出结果**（2026-09-18 提交，`5d1dbfb`）：
      四份结果已回收并处置——Gemini A / Grok A / Claude C / GPT C，
      P2-1 等新发现全部落地（issue_list §32，v1.22）。
- [x] **设备版本统一**：COM5（`28:37:2f:91:2c:20`）已刷 **v1.28**
      （2026-09-20，串口确认）；COM7（`10:20:ba:34:18:5c`）在 **v1.27**
      （刷 v1.28 时 CDC 掉口未写入，设备完好）——**待重插 USB 补刷**；
      `10:20:ba:34:19:ec`（#3）未连接。
- [ ] **`PENPAL_BASE` 数据源 + 生产站两问题（2026-09-20 更新）**：
      **new_dict 真机验收已解阻**（搜索/详情/考试卡全通过，§33）——设备
      当前数据源可用。剩余在用户侧：①生产站 TLS 证书续期（2026-09-18
      起过期，设备 insecure 模式可过但生产 OTA 的 Mozilla-CA 严格校验
      会被拒）；②生产站部署 `/firmware/` 端点 + 上传签名 manifest/
      firmware.bin（OTA_URL 已配 `https://www.studyreview.net/firmware/
      manifest.json`，端点就绪即生产 OTA 通）。
- [ ] **读信 TTS**：PenPal THREAD 页加"朗读"——MiniMax t2a 合成（链路已验：
      `minimax_audio.cpp` 的 `minimax_tts()`）→ SPIFFS `/tts.mp3` →
      `connecttoFS` 播放（EPD 抑制 + WDT 窗口语义照搬 AI Chat 语音路径）。
      注意与 OTA 写 flash 的互斥（`ota_busy()` 已导出）。
- [x] **PenPal gate-pin 去 hardcode**（**v1.26 已落地** 2026-09-19）：
      `X-Gate-Pin` 迁入配置链 NVS → `/env.cfg GATE_PIN=` → 编译期默认
      兜底（issue_list 无独立条目，见 CHANGELOG v1.26）；Whoami Cfg 新增
      Gate PIN 输入行。与 Apache `Require expr` 侧同步轮换时注意：先更
      服务端再改设备（改错 pin 全部请求 403）。
- [x] **openrouter Test 证书修复验证**：用户真机确认通过（2026-09-19，
      AI Config → Test OK；全量包 GlobalSign R1 交叉签锚生效）。
- [ ] **OTA 信任锚管理流程**（2026-09-20 部分完成）：换根已实跑（旧钥
      丢失 → `325BDA1A`，见 §35）；新钥已备份 `backups/ota_signing_key-
      325BDA1A.pem`。**剩余**：用户侧离线副本（密码库/U 盘）；把"生成
      密钥与备份同命令序列"写进 ota_gen_key.py 或设计 §2.2。
- [ ] **检查是否需要合并上游 T-Deck-Pro 新 commits**：
      https://github.com/hunterlau2020/T-Deck-Pro/compare/HD-V2-250915...Xinyuan-LilyGO%3AT-Deck-Pro%3AHD-V2-250915
- [ ] **AI Chat / TTS 返回数据累积检查**（尤其语音；会话数据是否有上限/清理）
- [ ] **Apache Gate-Pin 双活过渡**：能否同时接受新旧两个 PIN（平滑轮换）；
      v1.26 后设备侧改 pin 已免刷机（Whoami Cfg），轮换成本大降
- [ ] **轮换 PenPal 测试 key**（`89rg35eua2`/`3s60yrgdua` 已入 git 的
      penpal-design.md）并把设计文档里的明文换成占位符。
      **scope 扩充（issue_list §32.1，2026-09-27 qwen 评审）**：v1.22–v1.23
      批次又新增 3 个含明文 key+gate-pin 的 tracked 脚本——
      `scripts/inspect_mailbox.py`、`scripts/whoami_check.py`、
      `scripts/mb_check.py`（已推 origin）；轮换时一并参数化（argv/env，
      先例 `remote_api_demo.py`）或移 gitignored
- [x] **`cursor.show` 直写包 helper**（评审 1561b41..b92d021 Claude Nit）：
      **v1.15 消解**——ds4 F3 证实该直写不是绘制门（start_cursor_blink
      会打回 1，真正生效的是 CURSOR part bg_opa=TRANSP），4 行直写已删，
      不再需要 helper/compat 宏。
- [x] **WiFi 开机重连退避/上限**（issue_list §24 改进项，**v1.5 已修**
      2026-09-17）：自动连接管理器——最多 5 次、指数退避 2.5s→40s、
      放弃后 STA 空闲直到显式连接/重启；UI 扫描周期挂起管理器。
      （原担心点已全部覆盖，保留观察真机长跑表现即可）

## 已知边界 / 观察（不挡使用）

- [ ] **`ca_bundle_full.h` static 双拷贝去重**（低成本优化）：`static const`
      数组在头文件里被 `http_utils.cpp` + `ota_update.cpp` 各含一份，
      2×56.7KB flash（issue_list §22 顺带发现；改 extern 声明 + 单处定义）。
- [ ] **机 #3（V1.0）睡眠观察**：面板假死（issue_list §18）判定为复位时序
      事故而非睡眠路径缺陷，自动休眠保留；观察该机后续睡眠/唤醒是否再现
      假死（再现则按 §18 教训 1 拔电池恢复，并重评 V1.0 禁用自动休眠）。
- [ ] **麦克风键**：AI Chat 已绑定（按住说话）；其余屏未接（issue_list 1.3）。
- [ ] **SPIFFS 写放大**：`/chat.log` 整文件重写（原子安全）；改 append+compact
      或后台保存线程。
- [ ] **CJK 字库**：PenPal 显示字段走服务端转义（方案 C 已定）；如需设备端
      直显中文，启用 `LV_FONT_SIMSUN_16_CJK`（已在 lv_conf.h 开）或自制字库
      （方案 A/B，未排期）。
- [ ] **Shutdown 观察项**（issue_list §6）：① shutdown 后插 USB 是否直接进
      系统；② 长按电源键 2-3s 能否唤醒；③ 是否改深度休眠（BOOT 唤醒）——
      用户暂定"先观察再决定"。
- [ ] **幽灵键事件**：机 #2 按 Q 时 TCA8418 单次报矩阵外事件（R9 C6，行号
      越界），单发未复现（issue_list 键盘实测附注）。
- [ ] **AI Config 屏状态机补齐**（allinone 移植时按 `allinone-design.md` §4）：
      CONFIRM_SAVE/CONFIRM_DISCARD、Key 掩码 `****<末4位>`、历史 3 条、
      错误分类表、TLS 第四字段。
- [ ] **system prompt NVS 化**：`AI_SYSTEM_PROMPT` 移入 NVS（openai_api.h TODO）。
- [ ] **usage 统计展示屏**：读 NVS `ai_stats` blob 做统计界面。
- [ ] **NVS 状态机 C++ 单测化**：openai_api 存取状态机提取为无 Arduino 依赖
      单元（现 Python 镜像有漂移风险）。
- [ ] **开机 NTP 等待**：setup() 末尾轮询时间同步（issue_list 2.2）。
- [ ] **音量键 `'\v'` UI**：Sym 层音量键仅文本屏忽略，无音量 UI（issue_list 1.2）。

## 历史（已完成批次的结论存档）

全部已完成事项（31+ 轮评审、五轮 OTA 设计迭代、语音全链路、双机档案等）
的逐条记录见：
- `CHANGELOG.md`（2026-08 → 2026-09-13，按日期）
- `docs/issue_list.md` §1–§16（修复台账，含证据链）
- `docs/reviews/`（评审申请/结果，commit 锚命名）
