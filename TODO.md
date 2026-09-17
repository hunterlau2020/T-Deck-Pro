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

## 待办（2026-09-16 盘点，按优先级）

- [ ] **OTA 真机验证矩阵**（实现已落地 `71c09e7`，设计 §8 剩余项）：①
      校准打点（两机 setup 各段耗时 + `_busy_timeout` 实测）→ 复核 WDT
      窗口 T=60s 假设、填 `docs/ota-baseline.md`；② 回滚矩阵（自毁固件，
      先于首次真实 OTA；PENDING_VERIFY 状态链真机未覆盖——2026-09-16
      已核实回滚机制在位、otadata 现 VALID 态，见设计 §5.1）；③
      覆盖层吸收 / 断网 45s 重试 / 低电安全阀 / 签名否定用例（大写 hex、
      前导零、错锚验签）；④ 局域网端到端（HTTP 服务 + 签一份 manifest，
      走 Check→Install→重启→自证全流程，需临时编 `OTA_ALLOW_PLAIN_HTTP`
      台架版）。真机已过：自证窗口 `mark valid ok` 一次（机 #3）。
- [ ] **session batch 修复复审**（`095e41a` 修复轮：P1 rollback + 五项
      P2）：按评审指南 §2.5 修复轮安排 delta 复审（核销 71c09e7 四方
      评审发现的修复证据 + 反例击穿）。
- [ ] **设备版本统一**：`10:20:ba:34:18:5c`（COM7）按用户指令暂留
      `71c09e7`；`28:37:2f:91:2c:20` 已在 `095e41a`（2026-09-16 验证
      完整刷写后正常）。是否统一到 `095e41a` 待用户确认（一条命令）。
      `10:20:ba:34:19:ec` 未连接，重连后核对版本。
- [ ] **`PENPAL_BASE` 数据源不一致 + 机 #3 首次配置**：两台 V1.1 的 NVS 指向
      `https://www.studyreview.net`，但 `data/env.cfg` 与机 #3（2026-09-15
      整片擦除后 SPIFFS 克隆自 V1.1，NVS 为空）均为旧局域网地址
      `192.168.3.186:8000`——重刷 env 分区前先改 `data/env.cfg`；机 #3 需在
      Whoami Cfg 保存一次 HTTPS 域名。
- [ ] **读信 TTS**：PenPal THREAD 页加"朗读"——MiniMax t2a 合成（链路已验：
      `minimax_audio.cpp` 的 `minimax_tts()`）→ SPIFFS `/tts.mp3` →
      `connecttoFS` 播放（EPD 抑制 + WDT 窗口语义照搬 AI Chat 语音路径）。
      注意与 OTA 写 flash 的互斥（`ota_busy()` 已导出）。
- [ ] **PenPal gate-pin 去 hardcode**：`X-Gate-Pin`（现 `49ef146ed5`，硬编码于
      `penpal_api.cpp`）迁入配置链——NVS → `/env.cfg`（`GATE_PIN=`）→
      现值作编译期兜底；与 Apache `Require expr` 侧同步轮换（部署文档
      DEPLOY_APACHE.md；gate-pin 是防扫描纵深层，非安全边界）。
- [ ] **openrouter Test 证书修复验证**：全量包已补 GlobalSign R1 交叉签锚
      （v6 轮 AI Config Test 的 X509 失败应已解决）——设备上点一次
      AI Config → Test 确认（顺手项）。
- [ ] **OTA 信任锚管理流程**：`ota_signing_key.pem` 仅存本机（gitignored）；
      建立离线备份位与轮换预案（换根仅 USB，设计 §2.2）。
- [ ] **检查是否需要合并上游 T-Deck-Pro 新 commits**：
      https://github.com/hunterlau2020/T-Deck-Pro/compare/HD-V2-250915...Xinyuan-LilyGO%3AT-Deck-Pro%3AHD-V2-250915
- [ ] **AI Chat / TTS 返回数据累积检查**（尤其语音；会话数据是否有上限/清理）
- [ ] **Apache Gate-Pin 双活过渡**：能否同时接受新旧两个 PIN（平滑轮换）
- [ ] **轮换 PenPal 测试 key**（`89rg35eua2`/`3s60yrgdua` 已入 git 的
      penpal-design.md）并把设计文档里的明文换成占位符
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
