# 评审结果：v1.22–v1.23 批次——评审收口 + Voice AI 稳定性/滚动 + PenPal 修复链（4d27849..bf0aa72，claude）

- **评审日期**：2026-09-27
- **申请文件**：[session-batch-review-request-4d27849..bf0aa72.md](session-batch-review-request-4d27849..bf0aa72.md)
- **评审提交**：`4d27849..bf0aa72`（首含尾含，9 个受评 commit）
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）+ `CLAUDE.md` 第 2
  条秘密链硬规则。
- **评审结论**：**C 部分接受，且有 1 项 P0 必须当批处理**——本轮新增的三个排查脚本
  （`inspect_mailbox.py`/`whoami_check.py`/`mb_check.py`）把一个**真实的、指向线上
  生产域名的 API key + Gate-Pin 明文**提交进了 tracked 源码，且**已推送到
  `origin/HD-V2-250915`**（本机 `git status`/`git log origin/...` 核对，工作区干净、
  分支与远端一致）。这正是 `CLAUDE.md` 第 2 条"无真实 key 入 tracked 源码"要防的
  那类事故，量级不小于 2026-08-21 那次触发 `git filter-repo` 的 OpenRouter key
  事件。**这条必须当批处理，不建议留到下一轮**。
- 除这条 P0 外，本批的核心工作——PenPal 来信方向判定修复、缓存 v2/v3、uid 持久化、
  Voice AI 崩溃修复——**逻辑本身经独立复核站得住**，未发现新的功能性缺陷。

---

## P0：真实 API key + Gate-Pin 明文入库，且已推送到远端

- **位置**：
  - `scripts/inspect_mailbox.py:12-13`：
    ```python
    KEY = "89rg35eua2"           # hunter test key (device env.cfg PENPAL_KEY)
    GATE = "49ef146ed5"          # X-Gate-Pin (penpal_api.cpp:168)
    ```
  - `scripts/whoami_check.py:3-4`、`scripts/mb_check.py:3-6`：同一对常量原样重复。
  - 三个脚本的 `BASE` 均硬编码 `https://www.studyreview.net`——**不是**局域网/本地
    开发服务器地址（对照 `CLAUDE.md` 历史记录，PenPal 此前的测试环境是
    `http://192.168.3.186:8000` 这类 PC 局域网 IP；这次是真实公网生产域名）。
- **证据**：
  1. `git log --oneline -1 origin/HD-V2-250915` = `113fe0c`，与本地 `HEAD` 一致，
     `git status` 显示工作区干净——**这三个文件已经在远端**，不是待评审的本地暂存
     内容。
  2. `git grep` 核对：`89rg35eua2` 这个 key 值**在本批之前**已经出现在
     `docs/penpal-design.md`（明文写在设计文档里,历史遗留问题,不在本轮引入范围）；
     但 `49ef146ed5`（Gate-Pin）**是本批新出现**的——`git grep 49ef146ed5` 命中的
     三个文件全部是本批新增（`67acddf`/`4ca5874`）。**key 单独存在时已经是一个
     历史遗留问题，但这次把 key 和 Gate-Pin 配对、并指向线上生产域名写进三份可
     直接运行的脚本，是本批新增且更严重的一步**——之前只是"文档里写了一个字符串"，
     现在是"复制粘贴就能对生产账号发起真实请求的可执行代码，认证要素齐全"。
  3. 申请文件自己在归属表里承认了这一点（"含 key/gate-pin 常量"），但没有提出
     处置方案，相当于把决策权交给评审——按 §0 原则 1"证据先行"和 §5.4"新发现真实
     key 入 tracked = P0 阻断"，这不是一个可以"接受现状"的选项。
- **影响**：`A/1/有声 → P0`（A 类"泄密不可逆"；可达性 1——任何拿到这份仓库
  历史的人，不需要额外条件，直接运行脚本即可以 hunter 账号身份读取生产服务器上
  真实用户的 mailbox/pen-pals/thread 数据）。按 §0 原则 9"硬件语境加权"的姊妹原则
  （秘密链条款）：**泄密不可逆**，权重高于普通功能缺陷。
- **两轴**：`VERIFIED`（CODE：三处硬编码值逐字核对一致；`git log`/`git status`
  确认已推送）｜`VIOLATES`（`CLAUDE.md` 第 2 条"无真实 key 入 tracked 源码"；
  `docs/review_guide.md` §5.4"新发现真实 key 入 tracked = P0 阻断 + 走
  `SECURITY.md` 轮换/清洗"）。
- **最小修复（不可选，须当批执行）**：
  1. **立即在服务端轮换/吊销**这个 API key（`89rg35eua2`）与 Gate-Pin
     （`49ef146ed5`）——按已推送到远端的事实，视同已泄露，不因为"接下来要不要
     清理历史"而推迟轮换这一步。
  2. 按 `SECURITY.md` 既有流程对这三个脚本文件（及历史上已经写了 key 的
     `docs/penpal-design.md`，一并核对是否也要处理）跑一次 `git filter-repo`
     级别的清洗，或至少确认该仓库的可见范围（是否已公开/是否有第三方已拉取）
     再决定清洗的紧迫度。
  3. 三个脚本改为从 gitignored 的 `config_keys.h`/`env.cfg`/环境变量读取 key 和
     Gate-Pin（比照项目已有的秘密链查找序：NVS → SPIFFS `/env.cfg` → gitignored
     `config_keys.h` → 空默认），不要在 tracked `.py` 里出现任何真实凭据字面量，
     哪怕注释里写"test key"——测试账号的凭据同样是需要保护的密钥,`CLAUDE.md`
     的规则没有"测试账号例外"。
  4. `docs/penpal-design.md` 里已经存在的 `89rg35eua2` 明文（历史遗留，非本批
     引入）建议一并登记进 `issue_list.md`，交由属主决定是否需要连带清洗——本条
     不算本批新增的缺陷，但既然本批因为同一个 key 触发了 P0,顺手把这条并案
     处理更省事。

---

## 其余变更复核（PenPal/Voice AI/OTA 核心逻辑）

### PenPal 来信方向判定 + 缓存 v2/v3 + uid 持久化（2760655 → 77e0634 → 4ca5874）—— 独立复核，逻辑成立

- **方向判定**（`penpal_api.cpp` `j_mine()`）：从"`sender_user_id` 非零即我方"改为
  "等于本人 id"，本人 id 来自 `penpal_get_profile()` 解析的 `id` 字段。独立核对
  修复前后两版代码、`penpal_get_profile()` 的 JSON 解析路径，逻辑改动正确地对应
  了申请描述的根因（NPC 时代规则不适用真实笔友）。
- **cache v3 头**（`<fetched> <base> <key>`）：`pp_cache_read()` 用 `strtok_r`
  重新解析三段头,base/key 任一不符即整份缓存作废并删除文件——独立核对
  `pp_cache_write()`/`pp_cache_read()` 两侧的头格式完全对应，v1/v2 旧头（缺
  key 段）会落进"legacy header - invalidated"分支被清除，不会被误读成 v3 格式
  （`stored_key` 解析不到时判定失败，不会把 base 误当 key）。
- **uid 持久化**（`penpal_my_uid_load/store`，NVS `penpal`/`my_uid`）：
  `pp_entry()` 在任何缓存解析之前调用 `penpal_my_uid_load()`（关闭了"先进
  PenPal 后进 Whoami 导致 uid 仍为 0"的原始时序洞）；`wa_cfg_save_cb` 在检测到
  key 变化时调 `penpal_my_uid_store(0)`——独立核对了这处**比较顺序**：
  `key_changed` 的比较发生在 `s_cfg_key_real` 被新值覆盖**之前**，不存在
  "新旧值已经相同、误判未换号"的问题。
- **跨线程写**（Nit 级，未列入正式发现）：`penpal_get_profile()` 在 worker 线程
  里直接 `s_my_user_id = jid->valueint;`（`penpal_api.cpp`），是对一个非
  `volatile`/非原子的全局 `int` 的跨线程写，读者（`j_mine()`）可能在另一个
  worker 线程（如 PenPal 自己的线程解析任务）并发执行时读到该值。ESP32 上
  字对齐的 `int` 读写在指令级是原子的（不会读到"撕裂"的中间值），且触发窗口
  需要 Whoami 的 profile-fetch worker 和 PenPal 的 thread-fetch worker**恰好
  同时在飞**（离开 Whoami 且请求未消费、同时进 PenPal 触发新请求）——多前提
  折算可达性档 3，后果止于"某一条信件的方向判定用了上一刻而非这一刻的 uid"，
  不产生崩溃或数据损坏，下一次重新解析即自愈。**不升级为正式发现**，但建议
  以后有空档时把这类"worker 直接改 UI/其它线程拥有的裸变量"统一走队列或至少
  `volatile`/原子化，和这个项目自己在别处（PenPal busy handle、OTA inflight）
  已经确立的纪律保持一致。

### 残留的 stale_clock 不一致（Nit，建议顺手补）

- **位置**：`ui_penpal.cpp:884`（`pp_home_row_cb`，从邮箱行直接打开一个线程）
  调用 `penpal_cache_load_thread(pp.thr_root, pp.letters, PP_THREAD_MAX,
  &pp.letters_cnt, &pp.thr_dropped)`——**没有传 `stale_clock` 参数**（该形参
  在 `penpal_api.h` 里是带默认值 `nullptr` 的可选参数）。对照 `pp_entry()`
  （`:1209-1227`）读取 mailbox/pals 缓存时**显式**传了 `&stale` 并在
  `stale==true` 时追加一次后台网络刷新（"cached (clock unsynced) -
  refreshing..."）。
- **说明为什么不算新发现的正式条目**：本批引入的 cache v3 已经把"跨账号/跨域名"
  这个原始严重问题（"To: hunter"误判）用 base+key 绑定**无条件**堵死了，不依赖
  `stale_clock`；`stale_clock` 机制解决的是更窄的一层——"同一账号、同一服务端，
  NTP 未同步导致无法判断 TTL 是否过期"。这层残留只影响"设备刚开机、NTP 还没
  同步、用户从邮箱直接点进某个 `unread==0` 的线程"这个更窄的窗口,且 bf0aa72
  刚加的"unread>0 自动后台刷新"已经覆盖了最常见的"有新信"场景。
- **建议**：`pp_home_row_cb` 也传 `&stale`,复用 `pp_entry()` 里"渲染后若
  stale 则追加一次后台刷新"的同一套处理,消除这处不对称,不必现在阻断本批。

### Voice AI 崩溃修复（0fd2043）—— 逻辑站得住，附带发现一处编码污染

- `start_voice_record()`/`start_tts()` 均在触发会**重新安装 I2S0 驱动**的路径
  （PDM 录音 / `ensure_audio_init()`）之前，先 `if (tts_playing) {
  audio.stopSong(); tts_playing=false; ui_disp_suppress_flush(false); }`——
  与设备日志里 addr2line 定位的崩溃点（TTS 播放期间 `pdm_init()` 卸载 I2S0 后
  `audio.loop()` 写向已卸载驱动导致 `LoadProhibited`）对应，顺序正确。
- 录音时长筛选 `dur_ms = (wav_len - 44) / 32`：`wav_len` 若小于 44 会在
  无符号运算下下溢，但该分支的前置条件已由录音器自身的 ~700ms 早停门保证
  `wav_len` 远大于 44 字节（700ms × 32B/ms ≈ 22400B）——**在当前录音器实现下
  不可达**,不构造为缺陷（按 §2.5.1 的"反假想级联"精神,不因假设录音器未来
  被换成另一实现而升级）。
- **顺带发现（Nit，不阻断，建议登记）**：本次对 `ui_voice_ai.cpp` 的编辑引入了
  编码污染——文件开头被插入了一个 UTF-8 BOM（diff 首行 `-/**` → `+\xEF\xBB\xBF/**`），
  且两处已有注释的特殊字符被破坏：`§16.1` 被写成了`搂16.1`（`ensure_audio_init`
  函数头附近），`—`（破折号）被写成了 `鈥?`（"Now Audio.setPinout is safe"那行）。
  这与本批次早些的 `01ba2e0`（专门清理 `platformio.ini` 的"writer-env
  mojibake"）是**同一类问题**，但这次发生在 `ui_voice_ai.cpp` 且**没有被后续
  commit 清理**。GCC/xtensa 工具链通常会容忍文件开头的 BOM（不会导致编译失败），
  所以不算功能缺陷，但注释内容已经变得不知所云，建议比照 `01ba2e0` 的做法
  单独开一个"mojibake cleanup"commit 处理。

### OTA 清单封顶 + Whoami busy 句柄归属（4d27849）—— 成立

- `ota_check_task()` 在 `http.getString()`（会按 `Content-Length` 分配内存）之前
  新增 `mlen = http.getSize(); if (mlen<=0 || mlen>4096) { 拒绝 }`——独立核对
  这个检查确实挡在分配之前,不存在"先分配后校验"的时序问题。
- `s_wa_task` 的置空点从 worker 线程（任务退出前）搬到 UI 线程（`wa_consume()`
  消费到结果时）——本轮独立验证了这个改动**不会导致死锁或无限阻塞**：
  `xQueueSend` 从"限时 2000ms 失败即弃"改成了"失败则 `vTaskDelay(10ms)` 重试"
  的无界循环,乍看有卡死风险,但 `whoami_keyboard_poll()`（`:749-753`）在
  **任何**分支之前无条件调用 `wa_consume()`,而后者本身在 `factory.ino:937-938`
  被 `loop()` 无条件每 tick 调用——只要 `loop()` 还在跑（哪怕被 EPD/GPS 等
  长阻塞操作拖慢几秒）,队列迟早会被排空,不存在真正的死锁,只是最坏情况下
  worker 退出会延迟到下一次 `loop()` 恢复运行。**结论：设计上是安全的重试
  循环,不是缺陷**。

### 滚动会话区（a37face）/ 草稿放弃确认（d68289e）—— 均已核对，未发现问题

- 触摸滚动的 `ui_disp_suppress_flush(true/false)` 配对（滚动开始/结束）与
  `+/-` 键仅在输入框为空时才滚动（否则正常输入字符）逻辑自洽；`VAI_DISP_MAX
  4000` 的 UTF-8 边界安全截断与既有 PenPal 池教训一致；`resp_cont` 在
  `ai_destroy()` 里正确置空。
- 草稿放弃确认框正确接管键盘（`penpal_keyboard_poll()` 顶部拦截,在 notice
  box 判断之前）,且 `pp.send_lock` 为真（SEND 飞行中）时直接跳过确认、维持
  原有直接退出语义,不影响幂等比对逻辑。

---

## 验证说明

- 本环境无 `pio`、无设备：BUILD/HW 证据沿用申请方记录（编译零错误、三台真机
  烧录+哈希校验、carlos 机多线程真机核对），标 `PARTIALLY_VERIFIED`。
- P0 的"已推送到远端"这一事实性证据本轮**独立执行命令验证**（`git log
  origin/HD-V2-250915`、`git status`），不是转述申请。
- `docs/penpal-design.md` 里 `89rg35eua2` 的历史先例通过 `git grep` 独立核对，
  确认该 key 值本批之前就已入库（不算本批新增，但相关联，已在 P0 finding 里
  说明处置建议）。
- 其余源码位置：`penpal_api.cpp` 全量 diff（三个 commit 累计）、
  `ui_penpal.cpp:870-920`/`1170-1240`、`ui_whoami.cpp:289-300`/`377-385`/
  `505-520`、`ui_voice_ai.cpp` 全量 diff（`0fd2043`/`a37face`）、
  `ota_update.cpp:349-361`、`ui_penpal_write.cpp`/`ui_penpal.h`（`d68289e`）。

---

## 对称三格（§7.2）

1. **全区间**：9 个受评 commit + 2 个不评文档/构建改动全部过了一遍；额外主动
   核对了申请归属表自己标注"含 key/gate-pin 常量"的那一行——这是本轮 P0 的
   来源,申请知道有这回事但没有定性,我把它按 CLAUDE.md 硬规则定了性。
2. **独立复跑**：无 pio/无设备,SIM 级为主;P0 的"已推送"事实用 `git log`/
   `git status` 独立复跑验证,不是猜测。
3. **申请"最没把握"3 条**：① 12K 栈未打水印——沿用旧申请已知取舍,本轮未
   独立测量,不构造新反例；② 缓存头带 key 明文入 SPIFFS——申请自己已经提出
   这个疑问并给了"威胁模型内可接受"的论证,本轮认为该论证对**缓存文件**
   成立（需要物理接触分区),但与本次发现的 P0（**脚本明文硬编码 + 指向公网
   生产域名**)是完全不同量级的暴露面,不能用同一个论证去豁免 P0；③
   `j_mine` 回退分支——本轮认可其为可接受的自愈窗口,未能构造出持久性反例。

---

## §9 自审清单（节录，代码评审【C】/【ALL】强制项）

- [x] 10/11（秘密）：**本轮命中**——`git grep` 真实 key/gate-pin 不是 0 hits，
      三个新文件明文写入,判定不通过,已单列 P0。
- [x] 2 busy 释放：Whoami `s_wa_task` 归属改到 UI 线程后独立验证无死锁路径。
- [x] 6 迟到结果/所有权：`j_mine`/cache v3 的 base+key 绑定逻辑逐条核对成立。
- [x] 15 P0 与全部 Nit 均给了 `file:line` + 可复现证据（含 `git log`/`git
      grep` 命令与输出）。
- [x] 16 申请"三台真机烧录+哈希校验"按 `PARTIALLY_VERIFIED` 处理,未独立复现。
- [x] 20 未因为申请自己"已知悉但未定性"而降低秘密链这条的判定标准。

---

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [x] **C. 部分接受，含 1 项 P0 当批阻断项**

**当批必修（P0，不可延后）**：`89rg35eua2` + `49ef146ed5` 服务端立即轮换；
三个脚本改走 gitignored 配置读取；评估是否需要对这三个文件（以及
`docs/penpal-design.md` 里的历史 key）做 `SECURITY.md` 级别的历史清洗。

**应修（Nit，登记 `issue_list.md`，不阻断）**：`pp_home_row_cb` 补
`stale_clock` 参数；`ui_voice_ai.cpp` 的 BOM/mojibake 清理（比照 `01ba2e0`）；
`penpal_get_profile()` 里 `s_my_user_id` 的跨线程写有空补 `volatile`/原子化。

**保留**：PenPal 来信方向判定修复、缓存 v2/v3、uid NVS 持久化、Voice AI
崩溃修复与录音筛选、OTA 清单封顶、Whoami busy 句柄归属、滚动会话区、草稿
放弃确认——均已独立核实逻辑成立。

**回退项**：无（P0 是"轮换凭据 + 改读取方式"，不需要撤销本批任何功能性改动）。
