# 评审结果：v1.22–v1.23 批次 4d27849..bf0aa72（qwen）

- **评审日期**：2026-09-27
- **申请文件**：`docs/reviews/session-batch-review-request-4d27849..bf0aa72.md`
  （取代 `…4d27849..d68289e.md`，取代关系已在旧文件标注，符合"结果永存/申请可合并"规则）
- **评审提交**（9 个受评）：`4d27849`、`0fd2043`、`a37face`、`d68289e`、
  `67acddf`、`2760655`、`77e0634`、`4ca5874`、`bf0aa72`；
  另 `8d82043`（docs/版本）、`01ba2e0`（platformio.ini 注释乱码清理，
  逐行核对确认为纯注释）不评。
- **评审范围**：`c531d66..bf0aa72` 全区间 19 文件（归属表逐行核对 = 19 ✓；
  `git rev-list --count` = 11 ✓，头部自校复算通过）。
- **前提重核（§0 原则 5）**：评审时 HEAD = `113fe0c`，已越过申请基线
  `bf0aa72`；`git diff bf0aa72 HEAD --stat` = 4 文件（CHANGELOG v1.23、
  本申请文件、旧申请取代注记、fw_version 1.23），纯文档/版本，无代码面变化。
  代码评审对象仍锚定 `bf0aa72`。
- **评审结论**：**A 全量接受**（附 1×P2 应修 + 2×P3 + 3×Nit，全部登记
  `issue_list.md` §32 / 疑问清单，无阻断项；P2 绑 G-发布前清零）。

## 逐 commit 结论（§7.3）

| commit | 结论 | 摘要 |
|---|---|---|
| `4d27849` | clean | OTA 清单 4KB 预检正确（chunked/无长度/超限均在分配前拒绝）；Whoami 忙句柄改 UI-owned 成立（见"已通过项"#2） |
| `0fd2043` | issue | 崩溃修复/筛选逻辑本身 clean；P3-1（12K 栈无水印）+ Nit-1（BOM/乱码回归）|
| `a37face` | clean | 滚动容器 + redraw-on-release + UTF-8 尾切均安全 |
| `d68289e` | clean | 草稿确认框；SEND 飞行直退语义保留（`pp.send_lock` 守卫）|
| `67acddf` | issue | P2-1（inspect_mailbox.py 明文 key+gate-pin 入 tracked）|
| `2760655` | clean | 缓存 v2 双侧时钟洞封堵正确；Nit-2（stale 标志覆盖）|
| `77e0634` | issue | 方向判定修复正确；P2-1（whoami_check.py 明文 key）+ P3-2（回退窗口，4ca5874 已收窄）|
| `4ca5874` | issue | uid NVS 生命周期正确、`*out = pp_profile_t{}` 值初始化 ✓；P2-1（mb_check.py 明文 key）|
| `bf0aa72` | clean | 自动刷新完整复用 busy/gen/inflight-cap/stale-drop 框架；Nit-3（waitbox 与"后台"措辞）|
| `8d82043` / `01ba2e0` | docs-only | — |

## Findings

### P2-1（应修，根因既有——本批扩散）：3 个新增 tracked 脚本硬编码生产站 PenPal API key + gate-pin

- **位置**：`scripts/inspect_mailbox.py:12-13`（67acddf）、
  `scripts/whoami_check.py:3-4`（77e0634）、`scripts/mb_check.py:3,6`（4ca5874）
- **证据**：`git grep 89rg35eua2` 命中上述 3 文件（另命中既有的
  `docs/penpal-design.md:19,435`、`TODO.md:76`）；`git branch -r --contains bf0aa72`
  = `origin/HD-V2-250915`——**本批已推送远端**。key 为生产站
  `www.studyreview.net` 的 hunter 账号凭据；gate-pin `49ef146ed5` 与固件
  `penpal_api.cpp:210` 同值。
- **影响**：泄密类（A），但**根因既有**：key 早于本区间已入 tracked
  （penpal-design.md），gate-pin 于 `39c7472`（2026-09-14，区间外）入库，
  且 `TODO.md:63`（gate-pin 去 hardcode）、`TODO.md:76`（轮换 key）已登记在案。
  本批的增量是把待轮换清理面从 2 处扩大到 5+ 处。坐标：**A/1/根因既有
  → P2 应修**（非本批新 P0——秘密的 git 暴露状态未因本批改变）。
  **回升条件**：仓库转公开、或 hunter 账号在生产站承载真实数据 → 升阻断
  并走 `SECURITY.md` 4 步。
- **两轴**：`PARTIALLY_VERIFIED`（git grep + 远端包含关系已复现；key 的
  生产有效性未验证、也不应由评审验证）+ `VIOLATES`（CLAUDE.md 第 2 条
  secrets chain"无真实 key 入 tracked 源码"；review_guide §5.4）。
- **最小修复**：① 三个脚本改 argv/env 传参——仓内已有同构先例
  `scripts/remote_api_demo.py:78`（`args.key`），或整体移入 gitignored
  目录（一次性排查工具属性）；② `TODO.md:76` 轮换项的 scope 补上这 3 个
  脚本路径；③ 轮换本身（用户侧）维持既有 TODO 绑定。回归判据：
  `git grep 89rg35eua2 -- scripts/` = 0 hits。
- **盲点登记**：申请文件归属表虽写明脚本"含 key/gate-pin 常量"，但按
  "评（工具）"处理、未对照 §5.4 定性；作者 §2"最没把握"#2 只讨论了
  SPIFFS 缓存头明文 key，漏掉了脚本这一更大清理面。

### P3-1（应修 @G-真机）：Voice 双任务栈 16K→12K，无高水位实测（承作者自评 §2.1）

- **位置**：`examples/pda2/ui_voice_ai.cpp:539`（ai_voice）、`:671`（ai_text），0fd2043
- **证据**：diff 直接可见 16384→12288；全树无 `uxTaskGetStackHighWaterMark`
  调用点。深水路径 = `minimax_asr` + `openai_chat_multi`（TLS 握手 + HTTPClient
  + cJSON 解析 + 30s 超时重试），与本批新增的堆日志（free/largest_int/psram）
  属同一"未实测先收窄"模式。
- **影响**：若溢出 = 崩溃/堆损坏（A），但可达性依赖长回复/低内存具体时序
  （档 3），且**无溢出证据**——真机"TTS-MIC 崩溃不复现"验证的不是栈余量。
  坐标：A/3/未证实 → 按验证缺口登记，不定缺陷级。
- **两轴**：`CLAIM_ONLY`（作者真机窗口 ≠ 栈压测）+ `NO_CONTRACT`
  （async_ipc_contract 任务栈条款记录的是 PenPal 8K 先例；voice 12K 未入表）。
- **最小修复**：下次语音链路真机回归时在任务收尾打印一次
  `uxTaskGetStackHighWaterMark(NULL)`（PROBE 级，一行）；读数 ≥2K 余量则
  12K 入契约表，<2K 回退 16K。绑 G-真机。

### P3-2（决策项，承作者自评 §2.3）：uid==0 时 j_mine 回退旧规则，fresh-flash/key-change 首跑窗口方向仍错

- **位置**：`examples/pda2/penpal_api.cpp:88-92`（j_mine 回退分支）
- **证据（SIM 反例，构造成功）**：全新刷机 → Whoami Cfg 存 key
  （`wa_cfg_save_cb` 判 key_changed → `penpal_my_uid_store(0)`，uid=0）→
  **先进 PenPal**（`penpal_my_uid_load` 得 0；缓存 v3 因 key 绑定全部失效
  → 走网络）→ worker parse 线程信件 → `j_mine`：uid==0 → "非零=mine" →
  真实笔友来信全显 `To:` ——即 §1.4 原始缺陷在本窗口内原样复现。窗口直至
  首次 Whoami profile 成功（`wa_consume` → `penpal_my_uid_store`）才关闭。
  4ca5874 的 NVS 持久化已把窗口从"每次重启"收窄到"仅 fresh-flash/key-change
  首跑"，真机（carlos，uid 已持久化）验证路径不经过该窗口。
- **影响**：D/2/有声（错方向可见、可自愈）→ **P3**。
- **处置**：决策项交属主——(a) 接受窗口并在 `ui_penpal` 首跑提示"先访问
  Whoami"；或 (b) uid==0 时 THREAD 详情不显示 From:/To:（作者已提议）；
  或 (c) PenPal 首次 THREAD 请求前链式 profile 拉取。任选其一登记即可，
  不阻断。
- **两轴**：`VERIFIED(SIM)` + `CONFORMS`（无契约条款覆盖方向判定语义）。

### Nit-1：ui_voice_ai.cpp 重新引入 BOM + 两处注释 mojibake（0fd2043）

- **位置**：文件首 3 字节 `EF BB BF`；`ui_voice_ai.cpp:553`（`搂16.1` 应为
  `§16.1`）、`:578`（`鈥?` 应为 `—`）。
- 恰发生在 `01ba2e0`（platformio.ini mojibake 清理）之后，属写入环境churn
  复发。编译无影响（BUILD 复跑 SUCCESS）。修复 = 去 BOM + 两处字符还原。
- 同类自查：本批其余新文件（scripts/*、penpal 改动）未见新增 mojibake。

### Nit-2：pp_entry 的 stale 标志被第二次读覆盖而非 OR（2760655 引入，HEAD 仍在）

- **位置**：`examples/pda2/ui_penpal.cpp:1191-1196`——
  `load_mailbox(…,&stale) && load_pals(…,&stale)`；`pp_cache_read` 入口先
  `*stale_clock = false`，故最终 `stale` 只反映 pals 一侧。
- 仅当 NTP 恰在两次连续文件读之间完成同步时 mailbox 的 unverified 状态被吞
  （可达性≈0）。修复 = 双标志 OR。记录即可。

### Nit-3：bf0aa72 自动刷新弹出 "Opening thread..." waitbox，与提交说明"background/instantly"措辞不符

- **位置**：`examples/pda2/ui_penpal.cpp:909`（`pp_start(&rq, false)` 非
  chained → `:562-568` 置 busy + 弹 waitbox）。功能正确（可取消、busy/gen/
  stale-drop 完整、真机已按此行为通过），仅措辞/UX 与注释意图有差。
  可选：该路径改 status-only 不弹框，或修正注释为"缓存先显 + 可取消刷新框"。

## 疑问清单（不占缺陷条目）

1. `dur_ms = (wav_len - 44) / 32`（ui_voice_ai.cpp:448）：`wav_len` 为无符号，
   若录音器违约返回 ok 且 <44B 会下溢成巨大值穿过筛选门。现依赖
   `pdm_record_wav*` 的 44B 头契约成立；如想防御可加 `wav_len >= 44 + 32*VAI_REC_MIN_MS`
   下界。是否加由作者定。
2. `s_my_user_id` 为普通 `static int`，worker（penpal_get_profile:586）与
   UI（my_uid_load/store）双写、worker（j_mine）读。ESP32 对齐 int 存取
   实践原子，无实际撕裂；建议注释声明该前提（若未来改 64 位或加逻辑需升级）。
3. `penpal_my_user_id()`（penpal_api.h:216）当前**零生产调用方**（自审项 18：
   grep 全 examples/ 仅声明+定义）。作诊断导出无害，但属"已实现未接线"，
   后续若有 UI 消费方请接线，否则可删。

## 已通过项（承重不变量逐条核过）

1. **OTA 清单 4KB 封顶**（4d27849）：`getSize() <= 0 || > 4096` 在
   `getString()` 分配前拒绝（ota_update.cpp:352-362），chunked/无长度一并
   封堵；qwen P3-2 闭合。Mozilla-CA 强制校验路径未被触及 ✓。
2. **Whoami 忙句柄 UI-owned + 无限重试投递**（4d27849）：`s_wa_task` 仅在
   `wa_consume`（UI 线程）清除；worker 重试循环的承重前提 =
   `whoami_keyboard_poll` 在 `factory.ino:938` **无条件**每 tick 运行、
   `wa_consume` 在 active 检查之前 drain（ui_whoami.cpp:751-753）——已核实
   成立，队列深 4 + 单飞 ≤1 消息，循环实际不可滞留。qwen Nit-1/Nit-2 闭合 ✓。
3. **Voice TTS-MIC 崩溃修复**（0fd2043）：`start_voice_record` 先
   `stopSong()` + `tts_playing=false` + 释放 EPD 抑制，再进 pdm（I2S0 卸载）
   路径；`start_tts` 对称封堵"R 键重入时 ensure_audio_init 在活播放器下重装
   I2S0"的同源危害。根因链（设备日志 + addr2line）与修复一致 ✓（真机不复现
   声明对我为 CLAIM_ONLY）。
4. **too-short 筛选**（0fd2043）：drop 路径与既有早退路径（no-key/ASR 失败/
   录音失败）完全对称——`free(wav)` + ui_post + `ai_task=NULL` +
   `vTaskDelete(NULL)`；waitbox 由 `ui_timer_cb` 的 `ai_task==NULL` 检测统一
   收掉（ui_voice_ai.cpp:264-268），无悬挂 ✓。`ai_task` 由 worker 写属既有
   模式（本批未引入），对齐指针写实践原子——见疑问清单 2 同类。
5. **Voice 滚动**（a37face）：UTF-8 尾切 `while ((*view & 0xC0) == 0x80) view++`
   有界（≤3 字节，`'\0'` 非接续字节即停）；`lv_label_set_text` 深拷贝，无对
   `chat_history`（PSRAM realloc 对象）的别名悬挂；触摸滚动 EPD 抑制 +
   release 单次重绘（`lv_event_get_indev(e) != NULL` 守卫使程序化滚动不误抑制）
   符合 §5.3；4000 字符显示帽保 LVGL 64K 池（PenPal §15 教训）；死代码分页
   删除干净（`show_response_page` 全树 0 残留）✓。
6. **草稿放弃确认框**（d68289e）：dialog 期间键盘独占（`ppw_leave_open()` 早退
   分支在 poll 最前）；`pp.send_lock` 保留 SEND 飞行直退语义（幂等/在飞编辑锁
   不被破坏）；`ppw_overlays_close` 离屏清框 = "keep draft" 语义 ✓。
7. **缓存 v2/v3**（2760655/4ca5874）：读侧洞（pre-NTP 短路新鲜）→ serve +
   `stale_clock` 标志 → `pp_entry` 命中仍发起网络刷新；写侧洞（fetched==0
   永不过期）→ NTP 后判过期删除；v1/v2 头自动作废（`!stored_base[0] ||
   !stored_key[0]` → remove + miss）；base+key 双绑定封堵"同域换账号/服务端
   重建库"（本次事故的准确盲区）；头解析 fail-safe（截断/多空格 → mismatch
   → miss → 网络重建）；缓存文件非原子写但**可再生**（半截文件 → 头解析失败
   → miss），不触 §5.2 原子写红线 ✓。
8. **`pp_profile_t` 值初始化**（4ca5874）：`*out = pp_profile_t{}`
   （penpal_api.cpp:564），新增 `int user_id` 无未初始化读路径（server 缺 id
   → user_id=0 → store(0)=清除，安全缺省）；~232B 且在 worker 栈，不触
   "UI 线程肥聚合"红线；无 memset ✓（§5.2 全条符合）。
9. **uid NVS 生命周期**（4ca5874）：`penpal_my_uid_load` 在 `pp_entry` 的
   gen++/active=true **之后**调用（ui_penpal.cpp:1178，符合"请求/加载在 entry
   不在 create"教训）；lazy-load 单次门（`s_my_uid_loaded`）；Cfg 换 key →
   `store(0)`（mask 未动时 `save_key==s_cfg_key_real` 不误清）✓。
10. **自动刷新**（bf0aa72）：完整复用单队列框架——`rq.gen = s_pp_gen` 正确；
    非 chained → busy+busy_gen+可取消 waitbox；inflight≥2 拒绝（`:519-524`）；
    消费侧 `s_cur_page==THREAD` 原地重渲染、离页 stale-drop 释放 busy
    （`:581-590`）；cache-miss/unread==0 路径不受影响 ✓。§6 反例库适用行
    （离页旧结果/请求中退出/连续打开）逐条推演通过。
11. **堆诊断日志**（openai_api.cpp:603-612）：纯追加 Serial 输出，请求线程内、
    无副作用；`esp_heap_caps.h` 引入正确 ✓。

## 验证说明（评审方环境）

- 开发机 Windows，PlatformIO 可用，**无设备接入**——所有 HW 声明对本评审均为
  `CLAIM_ONLY`（⏸），未标 VERIFIED；未做串口/真机复现（如实标注，非"证伪"）。
- **BUILD 独立复跑**：`pio run -e pda2` @ 工作树（tracked 状态 = `113fe0c`，
  clean）→ SUCCESS 47.3s；**RAM 57.7%（188980/327680）与申请一致 ✓**；
  Flash 44.3%（2901853）vs 申请 44.2%——HEAD 越过 bf0aa72 三个文档/版本
  commit（fw_version 字符串等），0.1% 差异有归属，非异常涨幅。本批未改
  `config/lv_conf.h`，无 `-t clean` 前置要求。
- `git grep` 复现 P2-1 全部命中；`git branch -r --contains` 复现远端推送状态。
- SIM 推演：P3-2 反例、bf0aa72 生命周期、缓存 v3 头解析各分支。

## 对称三格

- **① 全区间 diff**：是。9 个受评 commit 逐个 `git show` 全读 + 区间
  `git diff --stat`（19 文件）与归属表逐行核对（含"不评"3 项：CHANGELOG、
  platformio.ini——已核为纯注释、旧申请文件）；另核了 HEAD 越过部分
  （bf0aa72..113fe0c，纯文档）。承重邻居：`factory.ino` loop 挂载点、
  `pp_start`/`pp_consume` 既有框架、`remote_api_demo.py` 传参先例均已读。
- **② 快照独立复跑**：`pio run -e pda2` SUCCESS（输出摘要见上）；git grep /
  rev-list / branch -r 均复跑。无设备 → HW/PROBE 项"无法复现"如实标注。
- **③ "最没把握"逐条构造反例**：
  §2.1（12K 栈）→ 无水印证据可核，转 P3-1 应修（PROBE 一行即可闭合）；
  §2.2（SPIFFS 缓存头明文 key）→ **同意作者裁定**：`/env.cfg` 本就同分区明文
  存同 key（CLAUDE.md secrets chain），缓存头未跨越新的暴露边界，且 PenPal
  key 为 16 字符测试 key、已广泛在库——不改；但作者漏掉了 scripts 明文 key
  这一更大清理面 → P2-1 + 盲点登记；
  §2.3（j_mine 回退）→ SIM 反例**构造成功**（fresh-flash 先进 PenPal 序列，
  见 P3-2），窗口真实存在但已被 4ca5874 收窄至首跑；转决策项交属主。

## 评审自审清单（§9【C】+【ALL】抽核记录）

1✓（voice/penpal worker 均经队列+timer，无非 UI 线程 LVGL）；2✓（stale-drop
释放 busy + gen 匹配，bf0aa72 复用同路径）；3✓（res/m 均 UI delete，rq 任务内
delete）；4✓（队列建检返回值，失败不置 busy 不启动）；5✓（rq 深拷贝快照）；
6✓；7✓（无新 memset，值初始化）；8✓（UI 线程 `pp_task_req_t rq = {}` 与既有
cache-miss 路径同款 <400B）；9✓（pp_dbg_pool 保留，lv_conf.h 未动）；10✓
（PP_KEY_MAX=17 ≥ 16 字符 PenPal key；本批未触及 sk-api 126 链）；11✗→P2-1；
12✓（voice 双机可达，无新变体假设；"三台验证"对我为 CLAIM_ONLY）；13✓
（TTS 期 EPD 抑制释放对称）；14✓（缓存可再生，fail-safe）；15✓（全部发现带
file:line+反例/证据）；16✓（HW=CLAIM_ONLY，BUILD 已独立复跑）；17✓（复跑参照系
=113fe0c 工作树已如实声明）；18→疑问清单 3（penpal_my_user_id 零调用方）；
19✓（§6 适用行推演记录于"已通过项"#10）；20✓（本轮首个结果，无冲突待核）。

## 入库登记（§7.2 入库轮义务）

本结果产生的应修/决策项已同步登记 `docs/issue_list.md` **§32**（P2-1 绑
G-发布 + TODO.md:76 scope 扩充；P3-1 绑 G-真机；P3-2 决策项待属主勾选），
Nit×3 不绑门禁。

## 审批意见

- [x] **A 全量接受**（无 P0/P1；P2-1/P3-1/P3-2 为应修/决策项，已登记 §32
      并绑门禁，符合"A 不挂阻断项"语义）
- [ ] B 退回修订
- [ ] C 部分接受

**给属主的一句话**：功能面九 commit 全部立得住（缓存 v3 三步收敛链的证据
质量尤其好——每步都有分区取证/真机反例支撑）；唯一实质动作项是把 3 个排查
脚本的明文 key 参数化并把它并入既有轮换 TODO，赶在任何公开化之前。
