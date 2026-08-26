# 评审申请书：PenPal 响应缓存 + 界面打磨 + weather 契约化 + 诊断日志

- **申请人**：opencode（Claude 系现场代理）
- **申请日期**：2026-08-26
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 9 个，文件名首末含两端；`10b8733` 菜单对调与
  `19b7b66`/`3a1758d` 评审归档/注释订正为用户直指令或纯 docs，一并列入
  备查，重点评审对象为其余 7 个）：
  - `10b8733` — `menu: rotate Sleep/Shutdown/PenPal across pages`（用户直指令，纯数据表对调）
  - `db2c3e5` — `weather: join the async IPC contract (gen gating, no vTaskDelete)`
  - `93162fb` — `wifi: log heap snapshots around the WiFi Test request`
  - `18ce65c` — `penpal: response cache layer (SPIFFS, 2-day TTL, parse-only split)`
  - `1eb66ad` — `penpal: cache-first HOME/THREAD, in-page thread Sync, msgbox Close`
  - `d859c12` — `docs: penpal response-cache design update`
  - `f3d36ab` — `penpal: move the THREAD Sync button into the title row`（用户直指令）
  - `ce47f38` — `penpal: hyperlink-style HOME list - no boxes`（用户直指令）
  - `3a1758d` — `ai: align comments with the decoupled Save/Test decision (N1/N2)`（纯注释）
- **上游关联**：Claude 二轮独立评审 `wifi-config-keyboard-review-result-c1c6a14..ff6d906-claude.md`
  的 N1/N2 落点 = `3a1758d`；其"仓库卫生 1"（菜单对调待走申请）= 本申请 `10b8733`。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，**COM3**——端口自 COM5 漂移）

---

## 1. 变更明细

### 1.1 PenPal 响应缓存（`18ce65c` + `1eb66ad`，产品需求）

需求原文：① 不点 Sync 时 HOME 从缓存读 mail list，点 Sync 删缓存重拉；
② 点行开会话先读缓存，会话界面加刷新入口；③ 缓存有效期 2 天；
④ TIPs 错误弹窗无关闭按钮。

- **缓存层**（`penpal_api.cpp/.h`）：SPIFFS `/penpal/pals.json` /
  `mailbox.json` / `th_<root_id>.json`；文件 = `<fetched_at>\n<原始响应体>`。
  getter 网络成功后写（worker 线程，`FILE_WRITE` 整写）；读侧
  `penpal_cache_load_*` 复用从 getter 拆出的 **parse-only** 解析
  （pals/mailbox/thread 三段，网络与缓存路径语义严格一致：0 哨兵 pal 行、
  4KB 单信截断、16KB 线程逐出）。TTL = `PP_CACHE_TTL_S`（2×86400）；
  **时钟未同步期写入（`fetched_at=0`）视为有效**——显示旧数据优于空白，
  手动 Sync 总可强刷；过期即删文件按 miss 处理。缓存文件非关键数据，
  任何读/解析失败一律当 miss（原始语义：薄客户端以服务端为准）。
- **HOME**（`pp_entry`）：自动同步前先试缓存，**pals+mailbox 全命中**才免
  网络；直接解析进全局 `pp` 状态——**无栈中转**（24 行 mailbox ≈3.8KB，
  issue_list §12 "UI 线程禁止大聚合压栈"规则；任一 miss 时另一侧半更新
  状态被随后的网络 sync 全量覆盖，无害）。命中后状态行
  `cached - press Sync to refresh`。
  `pp_home_sync(manual=true)`（键盘 `\n` 与触摸 Sync 同路）先
  `penpal_cache_drop_home()` 再强制 PALS→MAILBOX；发信后 auto-sync
  （`manual=false`，ui_penpal_write.cpp:213）不 drop、走网络并覆盖缓存。
- **THREAD**（`pp_home_row_cb`）：先试 `th_<root_id>` 缓存；命中后
  **升序数组原地反转**为 newest-first（与 PP_RES_THREAD 消费端逐元素
  拷贝反转的最终语义一致，`std::move` 交换避免拷贝）。页内
  **Sync 按钮**（44×26 文本按钮，初版在 nav 行、`f3d36ab` 应用户要求挪
  标题行右端 `< Thread` 同行）强制重拉；PP_RES_THREAD 消费端按
  `s_cur_page` 分支：HOME→切页、**THREAD（刷新场景）→`ppr_show_thread()`
  重渲染**。dropped 提示从计数标签挪进信头（长文本会压到按钮）。
- **msgbox Close**（`pp_msgbox_show`）：补 Close 按钮（waitbox 同款
  64×26）——TIPs 失败弹窗此前仅键盘任意键可关，触摸用户无关闭路径。
- **UI 去框**（`ce47f38`，用户直指令）：笔友 66×52 边框按钮 → 无边框
  点击区 + 名字下 44×1 下划线；邮件行 232×34 边框按钮 → 无边框点击区 +
  每行 232×1 分隔横线（空行隐藏）。`lv_obj_remove_style_all` 只去样式
  不去事件（`scr_back_btn_create` 同款先例），点击热区/回调不变。

### 1.2 weather 纳入异步契约（`db2c3e5`，关闭 issue_list §11 契约层 Medium）

- `weather_fetch_task` 携带 launch 时 `weather_page_gen` 快照；
  `weather_entry`/`weather_cleanup` gen++，迟到结果在任务尾部判定 stale
  丢弃——不推进 `last_fetch_time`、不 `save_cache()`。
- **删除 `vTaskDelete` 强杀**（原 `weather_cleanup` 直接杀在飞任务，
  栈上 HTTPClient/Preferences 自动对象被连带释放）；任务自然跑完自我
  失效。`fetch_task` 句柄保留（refresh_cb 的 `!fetch_task` 门控 + 防重入）。
- `start_fetch` 只在任务真正启动前清 `last_fetch_time` 并检查
  `xTaskCreatePinnedToCore` 返回值（**顺带关闭 §11 B4-L2**：WiFi 断/
  key 缺时旧序"先清后判"导致缓存永久过期、每次进屏空重试）；'r' 键
  不再预清时间戳。
- 注意：weather 仍是"结果写全局 + LVGL 定时器轮询"模式，未改为队列
  交接——契约 §2.1 的 new/queue 所有权规则不适用（无堆结果结构），
  本轮纳入的是**代次 + 不强杀 + 迟到丢弃**三项核心语义。此差异请评审
  确认是否可接受（候补：后续把 gen 判定挪进 refresh_cb 侧）。

### 1.3 WiFi Test 堆诊断（`93162fb`）

- 任务启动打印 `ESP.getFreeHeap()/getMaxAllocHeap()/getMinFreeHeap()`，
  失败时补打 free/largest + error。动机：用户报告间歇
  `Request failed SSL - Memory allocation failed`（TLS 握手需 ~40-50KB
  连续内部堆；weather 16KB 栈任务/chat/penpal worker 并发或碎片化可
  致饿死）。纯日志，无行为变更；weather 契约化（1.2）本身即消灭最大
  并发占用者的治理项。

### 1.4 菜单对调 + 注释订正（`10b8733` / `3a1758d`）

- 菜单第 1 页槽 9 Sleep→PenPal、第 2 页槽 8 Shutdown→Sleep、第 3 页
  PenPal→Shutdown（9/9/1 不变，坐标随槽位）。Claude 二轮评审"仓库卫生 1"
  的落点。
- `openai_api.h ai_provider_get` 注释改两路分支描述（原 "later wins"
  与实现不符，N1）；`ui_ai_cfg.cpp` 头注释/create 尾注释放 Test 门禁
  表述（N2，附"不得恢复门禁"防再犯说明；倒计时文案 10s→45s 同步）。

---

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | 每个代码 commit 后 `pio run -e pda2` SUCCESS（最终 Flash 31.2% / RAM 50.1% 一档） |
| 烧录 | ✅ | COM3 全部烧录验证（`f3d36ab`、`ce47f38` 均已上机） |
| 缓存路径真机回归 | ⏸ | 待验：Sync 后退出重进 HOME 免网络出列表（串口 `home served from cache`）；点行开线程免网络（`thread N served from cache`）；THREAD 标题行 Sync 强刷；2 天 TTL（可临时改 `PP_CACHE_TTL_S`） |
| msgbox Close | ⏸ | TIPs 失败弹窗触摸关闭 |
| HOME 去框渲染 | ⏸ | 笔友下划线 + 邮件行分隔线视觉效果（`ce47f38` 已烧录，用户可见） |
| weather 契约化回归 | ⏸ | 进 Weather 刷新中退出→无崩溃（原强杀路径）、重进数据正常、'r' 重试；串口观察 stale drop 日志 |
| WiFi Test 堆日志 | ⏸ | 复现 SSL alloc failed 时串口 largest 是否 <45KB |
| 菜单三页新布局 | ✅ | `10b8733` 用户已见（后续烧录均含） |

---

## 3. 遗留项与风险登记

1. **SPIFFS 容量**：缓存文件上限 = mailbox（~24×200B）+ pals（~200B）+
   线程数 ×（信件正文，单线程 ≤16KB 预算 + JSON 结构开销）。极端
   情况 24 线程全缓存 ≈ 数百 KB——SPIFFS 分区 9.9MB 充裕；但**无上限
   清理策略**（旧 `th_*.json` 随新线程累积，2 天 TTL 过期后仅在
   **被再次读取时**删除）。是否需要主动清扫（如 Sync 时清过期 th_*）
   请评审定夺。
2. **缓存与 unread 的语义**：HOME 缓存命中期间 `[new]` 标记为缓存时点
   的快照；服务端新到信件在按 Sync 前不可见。产品预期（"不点 Sync 就
   读缓存"）如此，登记备查。
3. **`fetched_at=0` 视为有效**：时钟未同步期写入的缓存无 TTL——若设备
   长期不联网校时，缓存永不过期。权衡：手动 Sync 总可强刷 + 空白屏
   体验更差。请评审确认。
4. weather 的 gen 判定在**任务尾部**（写全局前）而非 LVGL 消费侧——
   任务在 stale 判定后、`vTaskDelete(NULL)` 前不再触碰任何 UI 资源，
   但 `fetch_task = NULL` 写点在 stale 路径同样执行（新任务创建前
   `start_fetch` 有 `if (fetch_task) return` 防重入，stale 任务返回后
   句柄自清，无窗口问题）。如评审认为消费侧判定更稳妥可后续加固。
5. PenPal provider 共享 + 槽位自动保存 + 英文超时文案（上批
   `3d2a23d..9eeaa5a` 待回归项）与本批 ⏸ 项**合并一次烧录回归**。

---

## 4. 回滚方案

```bash
git revert ce47f38 f3d36ab   # penpal UI 打磨（按钮位置/去框）
git revert 3a1758d           # 注释订正（纯 docs，可独立回滚）
git revert d859c12           # 设计文档
git revert 1eb66ad           # penpal UI 缓存行为
git revert 18ce65c           # penpal 缓存层
git revert 93162fb           # wifi 堆日志
git revert db2c3e5           # weather 契约化（回到 vTaskDelete 语义）
git revert 10b8733           # 菜单对调
```

缓存文件残留：回滚 `18ce65c` 后 SPIFFS `/penpal/*.json` 无消费者，
可经 env_writer 或格式化清除；不回滚也无害（无代码路径读取）。

---

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
