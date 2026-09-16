# 评审结果：会话批次 71c09e7（CA根/语音上下文/自动休眠/PenPal修复/Whoami/TTS开关/OTA实现）（qwen）

- **评审日期**：2026-09-15
- **申请文件**：[session-batch-review-request-71c09e7.md](session-batch-review-request-71c09e7.md)
- **评审提交**：`71c09e7`（单 commit，base `6eb8245`，27 文件 +4149/−1580，8 组不相关变更）
- **评审类型**：**代码**批次（review_guide v1.3 §1.1"新屏/新异步任务"+"跨模块大批次"；**L2/L3 track**，非设计稿 L1——申请头部"L1 DOC-ALIGNED"系沿用设计稿模板笔误，本批是实现代码，按 §2.3 代码 A/B/C）
- **评审依据**：`docs/review_guide.md` **v1.3**（§4.2 定级、§5 承重不变量、§6 反例库、§2.3 代码 A/B/C「A 不得挂必修项=阻断 P0/P1；P2 应修走台账」、§7.2 对称三格、§9【C】/【ALL】自审）
- **评审范围与覆盖（§3.5 参照系诚实声明）**：按申请 §0 分组逐组评。**深核**（读源码逐行）= 组 8 OTA（`ota_update.cpp` 617 行全读 + `factory.ino` WDT/自证/poll diff + `ui_deckpro.cpp` OTA 屏生命周期）、组 5 Whoami（`ui_whoami.cpp` 任务/队列/销毁/消费路径全核）；**中核**（靶向 grep + 关键行）= 组 1 CA、组 2 语音上下文；**轻核**（申请描述 + 模式级 + §5 不变量扫）= 组 3 自动休眠、组 4 PenPal 修复、组 6 菜单、组 7 TTS 开关（多为设备报告驱动的 UI 修复，倚重作者设备闭环 + G-真机）。全批做了 §5.2/§5.4 不变量扫（memset/肥聚合/秘密）。
- **评审结论**：**A 全量接受**（无 P0/P1 阻断项）。附 **2×P2 应修**（登 `issue_list.md` 台账 + 绑门禁，非本批阻断）+ 2×P3 + 3×Nit + 1 流程观察。按 v1.3 §0 原则 11 / §2.3，无 P0/P1 不得给 C；P2 应修走台账绑门禁。

---

## 一、§5 承重不变量全批扫描（结论：无破坏）

| 不变量（§5） | 核验 | 结果 |
|---|---|---|
| §5.2 含 `std::string` 结构禁 memset | `rg memset` 于 ota_update/ui_whoami/ui_voice_ai → 0 命中；`ota_manifest_t`/`wa_*_t`/`vai_ctx` 均默认构造或 `new T()` 值初始化 | ✅ 无违例 |
| §5.2 UI 线程禁 `= T()` 肥聚合 | OTA `snap_t` 走 `new`（堆，`ota_start_async:567`）；Whoami `wa_msg_t*`/`wa_req_t*` 走 `new`；voice `vai_ctx` 是 `vector<pair<string,string>>`（堆） | ✅ 无 8KB 栈压爆风险 |
| §5.1 异步契约：worker `new`→队列→UI `delete` 恰一次 | OTA：`new ota_result_t`→`xQueueOverwrite(&ptr)`→`ota_result_poll`→consumer `delete r` 恰一次（`ui_deckpro.cpp:1260`）；manifest 所有权链 re-check 先删旧（`:1210`）、handoff 先置 NULL（`:1183`）、cancel/exit/destroy 均删（`:1295/1361/1399`）。Whoami：`new wa_msg_t`→`xQueueSend`→`wa_consume` `delete m`（`:331`），失败分支 `delete m`（`:224`） | ✅ IC-1 落实，无双重释放/泄漏（详见组 8/5） |
| §5.1 队列先建后置 busy/inflight；create 失败不递增 | OTA：`s_ota_q` 先建检返回值（`ota_update.cpp:542/562`）；`s_ota_inflight` 在 `xTaskCreate` **成功后**才置 1（`:551/579`），失败 `delete` 快照不递增（`:576`）。Whoami：`s_wa_q` 先建（`:266`），`xTaskCreate` 失败 `delete rq`（`:282`） | ✅ |
| §5.4 秘密链：无真实 key 入 tracked | `git show 71c09e7 \| rg` sk-api/sk-or-v1/硬编码 key → **0 命中**；CA 私钥/OTA 私钥均 gitignored（`.gitignore +3`） | ✅ |
| §5.1 非 UI 线程禁调 LVGL | OTA/Whoami worker 仅做网络+`new` 结果，UI 更新全在 `*_keyboard_poll`（loop/UI 线程）；worker 不碰 LVGL | ✅ |

---

## 二、逐组 findings

### 组 8：OTA 实现（深核；design v6 的 L2 落地）

**正确落地项（核验通过，对应 design v6 IC-1..4）**：指针结果通道（`xQueueCreate(1,sizeof(ota_result_t*))` + `xQueueOverwrite` 永不阻塞 + `ota_result_poll` 每拍排空，`ota_update.cpp:42/272/583`）；单飞 `s_ota_inflight` create 成功后递增、唯一出口 `ota_task_exit` 递减并同点释 `s_ota_hw_lock`（`:262-268/551`）；签名 `mbedtls_ecdsa_verify`+MPI r/s、65B SEC1 trust anchor、禁 `read_signature`（`:103-141`）；六字段规范化字节串 + 编码钉死校验（十进制无前导零 `:180-193`、小写 hex `:201-208`、notes 禁换行 `:174`）；`last_seq` 仅 `Update.end(true)` 后写（`:509-515`，我 v4 P2-1 闭合）；Content-Length 强制（`:436-441`）、size>槽拒（`:406`）、失败 `Update.abort()` 不 `end()`（`:527`）、读空闲 45s+绝对 10min（`:464/471`）；自证窗口 `s_boot_wdt_subscribed` 守卫 + `add` 仅 `ESP_OK` 置位（`factory.ino` WDT 块）+ 五喂狗点 + 首帧 `done_seq` 门控 + 自证语义注释**正确引用我 v5 P2 修正**（"proves sequence completion, NOT pixels on glass; broken panel still attests"）；`ota_result_poll()` 在 loop **先于** keyboard poll（drain-first，关闭作者最没把握②的覆盖泄漏窗口）。

#### P2-1（应修，绑 G-发布）：OTA HTTPS 走 `http_apply_tls()`，继承全局 `tls_insecure`——违反 design v6 §3.1/§3.4「专用 setCACertBundle、不经 http_apply_tls、不继承 tls_insecure」

- **位置**：`ota_update.cpp:315`（Check）、`:423`（Update）`http_apply_tls(secure)`；`http_utils.cpp:36-43`（`if (s_tls_mode==HTTP_TLS_INSECURE) client.setInsecure();`）；design v6 §3.1/§3.4。
- **证据/反例**：design v2 起明确要求 OTA 用**专用** `setCACertBundle(CA_BUNDLE_MOZILLA)`、**不经** `http_apply_tls`，正是为切断 v1 轮 qwen P1（OTA 继承 AI-Config 的 Trust 自签开关）。实现却调 `http_apply_tls(secure)` → 当用户在 AI Config 打开"Trust self-signed"（`s_tls_mode==INSECURE`）时，OTA 的清单与固件下载 `setInsecure()`、**不校验证书**。
- **影响**：后果 **C/B**（安全控制被弱化 + 实现与设计相悖）；可达性 **2**（需 tls_insecure ON + 在途攻击者）。**关键缓解**：固件经 ECDSA P-256 签名、对**flash 内不可变 trust anchor** 验签（`:121/347`）+ 下载后 sha256 比对签名值（`:505`）+ seq 单调（`:354`）——MITM **无法**伪造合法签名固件，故**不构成 v1 轮那个 P1**（彼时无签名）。坐标 **C/2 → P2**（签名把它从 P1 降到 P2）。
- **两轴**：`VERIFIED`（CODE：`http_apply_tls` 读全局 mode + 两处调用）/ `VIOLATES`（design v6 §3.1/§3.4；review_guide §11"不得留设计说 A、代码做 B"）。
- **最小修复（应修，绑 G-发布——OTA 实际推送前必须闭合）**：把 `:315/:423` 改为 OTA 专用 `secure.setCACertBundle(CA_BUNDLE_MOZILLA)`（不调 `http_apply_tls`），使 OTA **恒 CA 校验、永不继承 tls_insecure**，与 design §3.1 一致。**或**若属主裁定"签名已足够、TLS 模式可继承"，则**改 design §3.1 + 在 §11 登记该决策**（二选一，不得留设计-代码相悖）。

#### P2-2（应修，绑 G-真机）：boot WDT `T=60s` 未校准（design §5.3/§7.3 的 IC-3 未完成）

- **位置**：`factory.ino` `#define OTA_BOOT_WDT_T_S 60`（注释自承"TODO(§7.3): calibrate on both machines"）；design v6 §5.3。
- **证据**：T=60s 声称 = 2× GxEPD2 `_busy_timeout`（"constructor-bound ~30s"），但 §5.3 要求的**两机校准打点未做**、`docs/ota-baseline.md` 无数据。最长不可喂狗间隔 = 单页 `_waitWhileBusy`（库内不可插喂狗，喂狗点在页间）；其真实值未实测。作者最没把握①亦自承"V1.0 单页 busy ~10s…若某未识别库内等待 >60s 会误触发回滚"。
- **影响**：后果 **B**（T 偏小则健康镜像被误回滚——我 v3 P1-1 同类）；但 60s 对已知间隔（A7682E testAT 1s/轮已喂、GPS getAck 已喂、EPD 单页 ~10–30s）有 2–6× 余量，**当前证据下不构成 P1**。坐标 **B/2 → P2**（值未证，方法对、余量看似足）。
- **两轴**：`PARTIALLY_VERIFIED`（方法核验：re-init/add(NULL)/守卫 API 经框架头确证；**值未实测**）/ `CONFORMS`（design §5.3 方法）。
- **最小修复（应修，绑 G-真机/§8.1-8.2）**：按 §9 步骤 0 在两机打点实测最长喂狗间隔（含 GxEPD2 `_busy_timeout` 真值 + V1.0 兼容路径单页 busy），核 `T ≥ 2× 实测`，数据入 `docs/ota-baseline.md`；§8.2"健康固件正向"必须确认两机自证成功、无 TWDT 误复位。**未校准前不得依赖 layer-2 回滚做现场 OTA**。

#### P3-1：`xQueueOverwrite` 覆盖未消费旧指针的"泄漏窗口一个结果结构"（作者最没把握②）——结构上近不可达，建议防御

- **位置**：`ota_update.cpp:272 ota_send_result` `xQueueOverwrite`；design v6 §3.1 自承该窗口。
- **分析**：深度 1 + overwrite，若旧结果未消费即被新结果覆盖 → 旧指针泄漏。但 ① `s_ota_inflight` cap 1（同时仅一个 worker → 一个在飞结果）；② `ota_result_poll()` 在 loop **先于** keyboard poll（drain-first，新 op 只能在旧结果消费后发起）；③ 新 worker 网络耗时秒级，旧结果早被排空。三者叠加使泄漏**结构上近不可达**。坐标 **E/3 → P3**。
- **最小修复（可选）**：`ota_send_result` 内 overwrite 前先 `xQueuePeek`+`delete` 旧指针（防御性），或注释钉死"inflight cap1 + drain-first 保证窗口不可达"。

#### P3-2：清单 `http.getString()` 无响应体大小上限

- **位置**：`ota_update.cpp:331 String body = http.getString();`。
- **分析**：清单 GET 在验签前，恶意/异常服务器可发超大 Content-Length → `getString()` 按长度分配 → OOM。OTA_URL 来自本地 env.cfg + HTTPS（secure 模式校验证书），风险低；但 P2-1 未修时（insecure）暴露面增大。坐标 **C/3 → P3**。
- **最小修复**：用 `http.getSize()` 预检上限（如 ≤4KB）或流式读封顶，再 parse。

### 组 5：Whoami App（深核；作者标记的两处已修缺陷复看——均确证修复）

**核验通过**：① **队列竞态→断言重启**（作者标记，串口实证 2026-09-14）已修——队列**一次创建永不删**（`ui_whoami.cpp:266/749 if(!s_wa_q) xQueueCreate(4,...)`，无 `vQueueDelete`），late result 由 loop 中**无条件** `whoami_keyboard_poll`→`wa_consume` 排空（`:618-622`，drain 在 `s_wa_kbd_active` 门之前）。② **6KB 栈溢出静默死**已修——`xTaskCreate(...,1024*8,...)`（`:279`，注释记设备报告）。③ **Z 序**（页面先建、顶栏按钮后建，`:673-747` 注释 + 顺序）。④ **late-result UAF 反例被证伪**（我重点构造）：`wa_destroy` 把全部 widget 静态指针置 NULL（`:787-789`），且 `wa_me_render`（`:167 if(!s_me_label)return`）/`wa_waitbox_hide`（`:112 if(s_wa_waitbox)`）/`wa_cfg_status`（`:83`）/`wa_me_status`（`:78`）**全 NULL 守卫**；worker 不杀、结果落队列、poll 排空时 widget 已 NULL → 安全丢弃（`:783-786` 注释明示"discarded beyond the NULL widget guards"）。⑤ 单飞 = `s_wa_task` 句柄门（`:234 if(s_wa_task) busy return`）。**无 P0/P1/P2**。

- **Nit-1**：`s_wa_task` 由 worker 线程在出口写 NULL（`:226`），属"任务线程写 busy 等价标志"——与 `async_ipc_contract §2.3`「busy 仅 UI 线程读写」字面有偏。**本处良性**（单飞保证同时仅一任务、句柄写读为原子指针、无 gen/陈旧误解问题），但与 OTA/PenPal 的 `inflight`+gen 模式不一致。建议：登记为可接受变体，或对齐为 UI 在 `wa_consume` 消费结果时清句柄。坐标 **E/3 → Nit**。
- **Nit-2**：`xQueueSend(s_wa_q,&m,2000ms)` 若超时返回 pdFALSE，`m` 既不入队也不 `delete`（`:221-225` 仅在 `!s_wa_q` 时删）→ 潜在泄漏。单飞+深度4+每拍排空使其不可达，但建议 `if(xQueueSend(...)!=pdTRUE) delete m;`。坐标 **E/3 → Nit**。

### 组 2：语音多轮上下文（中核）

`vai_ctx` = `vector<pair<string,string>>`（堆，无肥栈聚合）；整轮 (user,assistant) 对存储、`vai_ctx.erase(begin,begin+2)` 裁最旧轮（`:336`，turn-paired，符合 §5.1 轮次配对精神）；预算 4096B/16 msgs；**UTF-8 边界截断有据**（`:324 ((uint8_t)parts[i]->back() & 0xC0)==0x80` 检测续字节，避免截断 CJK 成豆腐块——与组 4 的 CJK 修复同源关切）；仅"确认成功"轮入史（`:384/476`）；退屏清空。**无 P0/P1/P2**。（轻核：未逐行验 `vai_ctx_build` 的预算累加边界，建议作者补一条"16 msg/4096B 边界 + CJK 不半字符"用例。）

### 组 1：CA 根修复（中核）

`gen_ca_bundle.py`：额外根**指纹钉死**（`EXTRA_ROOT_SHA256` + `assert want,"no pinned fingerprint"`，`:124-125`）、`REQUIRED_SUBJECT` 锚点断言（`:43/139`）、`verify_directly_issued_by` 链验证；`verify_chains_vs_bundle.py` PC 全链验证六端点绿。**无秘密泄漏**（私钥不涉及；bundle 是公钥证书）。回应申请 §1 关注点：换根流程（指纹钉死 + 仅 USB 重刷）与 `SECURITY.md` 语义一致——extra_roots 是**追加信任锚**（公钥），非秘密，指纹钉死防供应链替换。**无 P0/P1/P2**。（轻核：未独立复跑 `verify_chains_vs_bundle.py`，标 `CLAIM_ONLY`，倚重作者 PC 验证 + 真机 minimax 双端点恢复。）

### 组 3 / 4 / 6 / 7（轻核——申请描述 + 模式级 + 不变量扫；倚重设备报告闭环 + G-真机）

- **组 3 自动休眠**：`ui_activity_mark()` 在 keypad/touch 打点（`factory.ino:352`），5s `idle_sleep_timer_cb` 查 5min 超时 → `scr_mgr_push(SCREEN11_ID)`（复用手动 Sleep 屏，含倒计时/取消）；`audio.isRunning()` 期间重计时（`#include Audio.h` 注释）。定时器生命周期在改动区一致遵循"句柄保存 + `lv_timer_del` + NULL"（如 `s_ota2_2_timer:1376/1401`）。**未见 P0/P1**；轻核未逐行验 idle 计时与 Sleep 屏倒计时交互，建议 §8 真机验"5min 触发 + audio 抑制 + 取消路径"。
- **组 4 PenPal 修复**：失败提示改 `pp_msgbox_show`（原 106px CLIP 重叠不可读）、msgbox 非 ASCII → `lv_font_simsun_16_cjk`（`LV_FONT_SIMSUN_16_CJK` 已开）、reply title 三路锁定。均设备报告驱动闭环。**未逐行核**（轻核），无 §5 不变量违例迹象。
- **组 6 菜单重排** / **组 7 TTS 开关 + `LV_PART_INDICATOR|LV_STATE_CHECKED` 样式**：低风险 UI；TTS OFF 时 `start_tts()` 早退（ASR 不受影响）。**未逐行核**。

### 流程观察（非缺陷，登记）

- **G-提交 模块拆分偏离**：单 commit 混 8 组不相关变更，违反 §2.2 G-提交"按模块拆分"。申请已说明系**用户明确要求单 commit 归档**——属属主授权的例外，评审已适配为按组给 findings。登记备查，不阻断。

---

## 三、作者"最没把握"三项——构造反例（§7.2 对称三格③）

1. **① boot WDT T=60s 未校准 + V1.0 单页 busy ~10s**：构造反例"某库内不可喂狗等待 >60s → 健康镜像误回滚"。核验：喂狗点在 EPD **页间**（`factory.ino` "between pages"），故不可喂狗间隔 = **单页** `_waitWhileBusy`（≤ `_busy_timeout`），非整屏；A7682E/GPS/SD/PCM 段内均已插喂狗。已知间隔 ≤~30s，T=60s 有 2× 余量 → 反例**当前不成立**，但**值未实测** → 转 **P2-2**（绑 G-真机校准，未校准前不依赖 layer-2）。
2. **② `xQueueOverwrite` 深度 1 覆盖丢失 → delete 纪律/泄漏**：构造反例"loop 长阻塞（EPD busy×多帧）期间两结果并存 → 旧结果被覆盖泄漏"。核验：`ota_result_poll()` 在 loop **先于** `ota2_2_keyboard_poll`（drain-first）+ `s_ota_inflight` cap 1（同时仅一 worker）+ 新 worker 网络耗时秒级 → 旧结果必在新结果产生前被排空。反例**结构上不可达**（确认作者"只延迟 delete、不丢"判断正确）→ 仅余 **P3-1** 防御建议。
3. **③ Whoami/PenPal 共享 NVS 键并发写窗口**：构造反例"两屏并发写同一 NVS 键 / `pp_notify_cfg_changed` 时序错乱"。核验：scr_mgr 单活跃屏（两屏不同时活跃）；Preferences 写同步；PenPal worker 在 launch 时快照 cfg（契约 §2.5），运行中 cfg 被改不影响在飞 worker；`pp_notify_cfg_changed` 仅在 PenPal 下次进入时触发重同步。反例**不成立**（无并发写窗口）。

---

## 四、验证说明（评审方环境）

- 开发机独立核验：`git show 71c09e7`（stat + factory.ino diff）；逐行读 `ota_update.cpp`(617)、`ui_whoami.cpp`(205-334/615-798)、`ui_deckpro.cpp` OTA 屏(1079/1170-1260/1361/1387-1401)；靶向 grep `http_utils.cpp:36-43`(http_apply_tls)、`ui_voice_ai.cpp`(vai_ctx/UTF-8)、`gen_ca_bundle.py`(指纹钉死)、`GxEPD2_EPD.h`(busy_timeout 形参)；§5 不变量全批扫（memset/肥聚合/秘密 git grep 0 命中）。
- **未独立复跑** `pio run -e pda2`（本轮未触发构建）：编译性/size 标 `UNKNOWN`，建议合入前一次 `pio run -e pda2` 冒烟。
- **无设备**：组 3/4/6/7 的 UI/交互、OTA §8 全部真机项（回滚矩阵/覆盖层吸收/断网重试/低电阀/双机）无设备复现，按 §3.4 标 `UNKNOWN`/倚重作者设备报告 + G-真机。
- 未对工作区作任何修改。

## 五、对称三格（review_guide §7.2 强制）

1. **是否核了全区间 diff（不只靶向清单）**：部分——按 §3.5 诚实声明覆盖：深核组 8/5（最高风险的新异步+flash 写代码，逐行）、中核组 1/2、轻核组 3/4/6/7（申请描述+模式级+不变量扫，未逐行）。全批 §5 不变量扫无遗漏。**未对全部 4149 行逐行**——8 组单 commit 超单次深核容量，已按风险分配深度并如实标注（建议组 3/4/6/7 以 G-真机设备回归为最终验证）。
2. **快照是否独立复跑**：部分。源码 `file:line` 独立读取核验（已记命中行）；`pio` 构建未跑（标 UNKNOWN）；真机项无设备（标 UNKNOWN）。
3. **申请"最没把握"各处是否构造反例**：是，三项逐条构造并核验（第三节）——①转 P2-2、②反例不可达(仅 P3-1)、③反例不成立。另主动构造组 5 late-result UAF 反例 → 被 NULL 守卫+destroy 置 NULL 证伪（第二节）。

## 六、§9 自审清单（代码档：【C】+【ALL】）

```text
【C 异步/契约/内存/硬件】
□ 1 非 UI 线程调 LVGL？— 否，OTA/Whoami worker 仅网络+new 结果，UI 更新全在 poll(UI 线程) ✅
□ 2 busy 仅 gen 匹配释放 / stale-drop 释放？— OTA inflight 唯一出口递减；Whoami 单飞句柄 ✅（Whoami 句柄由任务写=Nit-1，良性）
□ 3 worker new 结果 UI delete 恰一次？— OTA `:1260` delete r 一次 + manifest 链全路径删；Whoami `:331`/`:224` ✅
□ 4 队列先建检返回值后置 busy/inflight？— OTA `:542/562`+`:551/579`；Whoami `:266` ✅
□ 5 任务持 UI buffer 副本（非共享）？— OTA snap launch-time deep copy(`:569`)；Whoami rq 快照 ✅
□ 7/8 memset string 结构 / UI 线程肥聚合 =T()？— 全批 0 命中 ✅
□ 9 LVGL 池/ lv_conf？— 本批未改 lv_conf 池；Whoami/OTA 屏 create 有池水位观测点(design 要求) ✅
□ 10/11 key 缓冲 / 秘密 0 hits？— git grep 0 命中；OTA 私钥/CA 私钥 gitignored ✅
□ 12 硬件变体（机#1/#2、V1.0/V1.1）？— 申请 §8/§10 记录 V1.0 EPD 慢路径差异、本批真机在 V1.1；OTA T 校准须双机(P2-2) ✅
□ 13 EPD 阻塞实时路径？— 自证门控用 EPD 首帧 done_seq；喂狗点页间(组8) ✅
□ 14 原子写链？— OTA last_seq 仅 end(true) 后写、otadata 由 Update 管；NVS Preferences 同步写 ✅
【ALL】
□ 15 每条 P0/P1 给 file:line+反例？— 本轮无 P0/P1；P2-1/P2-2 均给 file:line+证据 ✅
□ 16 作者"真机通过"声明独立核验或标 CLAIM_ONLY？— 组1 PC 链验证/组3-7 设备报告标 CLAIM_ONLY/UNKNOWN，未当 VERIFIED ✅
□ 17 参照系与结论一致？— 深核组逐行、轻核组明示模式级(§3.5)，A 结论附覆盖声明 ✅
□ 18 已实现≠已接线？— ota_result_poll/whoami_keyboard_poll 确认在 factory.ino loop 无条件调用(`:828` 区)；ota_set_consumer 在 SCREEN2_2 entry 注册(`:1387`)/exit 置 NULL(`:1400`) ✅
□ 19 不止 happy path？— §6 反例库：late-result(UAF 反例)、离页/取消、队列满、并发——逐条推演(第二/三节) ✅
□ 20 结论相反核对方事实？— 本批暂无其他模型结果；P2-1 以 design v6 §3.1 原文 + http_apply_tls 源码为据，非臆断 ✅
```

## 审批意见

- [x] **A. 全量接受**（无 P0/P1 阻断项）
- [ ] B. 退回修订
- [ ] C. 部分接受

> **应修登记（绑门禁，非本批阻断；按 v1.3 §2.3「A 不挂必修项=阻断 P0/P1」，P2 走台账）**：
> - **P2-1**（OTA `http_apply_tls` 继承 tls_insecure，违 design §3.1）→ `issue_list.md` 台账，**绑 G-发布**：OTA 现场推送前改专用 `setCACertBundle`（或改 design+§11 登记决策）。
> - **P2-2**（boot WDT T=60s 未校准）→ 台账，**绑 G-真机**：两机打点实测最长喂狗间隔 + `_busy_timeout`，核 T≥2×，入 `docs/ota-baseline.md`；§8.1-8.2 回滚矩阵 + 健康固件不误回滚。**未校准前不依赖 layer-2 做现场 OTA**。
> - **P3-1/P3-2**（overwrite 泄漏窗口防御 / 清单 getString 封顶）+ **Nit-1/2/3**（Whoami 句柄写、xQueueSend 失败删、voice 边界用例）→ 台账低优先，实现期带。
> - **G-提交 偏离**（8 组单 commit）→ 属主授权例外，登记备查。
>
> **合入前建议**：一次 `pio run -e pda2` 编译冒烟（本轮未跑，标 UNKNOWN）。
>
> **净评**：本批最高风险的两块新代码——OTA 实现（design v6 的 L2）与 Whoami 新屏——**深核后均扎实**：OTA 指针通道/单飞/所有权链/签名/seq/超时/abort/自证语义全部正确落地（我 v4/v5 轮的 P2-1/P2-2/P3-1/Nit 在代码中确证闭合），Whoami 作者标记的队列竞态重启与栈溢出已修、late-result UAF 反例被 NULL 守卫证伪。**无 P0/P1**。两条 P2 是"实现 vs 设计"的一致性(P2-1)与"安全值未校准"(P2-2)，均可台账化绑门禁、不必阻断本批合入——符合 v1.3 收口纪律（无 P0/P1 不给 C）。组 3/4/6/7 为设备报告驱动的 UI 修复，轻核未见不变量违例，最终以 G-真机回归为准。
