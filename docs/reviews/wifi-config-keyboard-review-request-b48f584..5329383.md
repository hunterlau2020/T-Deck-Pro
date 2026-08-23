# 评审申请书：笔友（PenPal）App 实现全量（4 个模块 commit）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 4 个，按 §9 拆分预案；文件名 = 首末含两端）：
  - `b48f584` — `penpal: API client for the pen-pal service`
  - `16c13e3` — `penpal: R9 residual-thread query (pen_pal_id optional) + design v3.2`
  - `b231dd3` — `penpal: screen UI - mailbox/compose/thread pages`
  - `5329383` — `penpal: menu icon + third menu page`
- **背景**：设计文档 `docs/penpal-design.md` 经 v1→v3.2 五轮双评审收敛
  （v3.1 Codex **A 全量接受**为实现基线；R9 服务端需求 2026-08-22 上线后
  追加 v3.2 恢复"显示 + 只读"）。本批为 §9 预案的 1-3 步实现 + 本 docs
  commit（第 4 步）。**实现细节以设计文档为准**，本申请只列要点、已登记
  偏差与实现裁量。
- **出列说明**（本批 push 共 6 个 commit）：`7ee0807`（v3.1 基线登记，
  纯 docs，Codex 结果归档）与本 docs commit 不在评审范围；被评审代码 =
  上列 4 个 commit（与文件名一致）。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E 变体，COM5，
  **已连接、已烧录**）；测试服务器 `http://<PC局域网IP>:8000`（用户侧维护）。

---

## 1. 变更明细

### 1.1 `b48f584` — API client（`penpal_api.h/.cpp` 新增，~700 行）

- 自有传输层：`http://` 前缀 → 明文 `WiFiClient`，`https://` →
  `WiFiClientSecure`（前缀强制，不符即拒）；https 路径复用
  `http_get_tls_mode()` 策略 + NTP 时间门
- 8 个端点封装 + 防御式 cJSON 解析（字段缺失 → 默认值 + 串口日志，
  不崩溃——设计 R1）
- 幂等（§2.2）：`penpal_new_idem_key`（`esp_fill_random` → 32 hex）出参
  `Idempotency-Key`；响应头 `Idempotent-Replayed` 收集 → `replayed` 标志；
  201 首发 / 200 重放均为成功
- 线程取数按 `(pen_pal_id, thread_root_id)` 双参锚点；单信 4KB UTF-8
  边界截断 + 16KB 总预算、最旧丢弃计数
- 配置链：NVS `"penpal"`（单槽 base/key）→ SPIFFS `/env.cfg`
  `PENPAL_BASE/PENPAL_KEY` → `config_keys.h` → 空
- 超时：CRUD/发信 20s，LLM（correction/polish/tips）180s
- **http_utils 增量导出**：`apply_tls`/`http_ensure_time` 由 static 改
  `http_apply_tls`/`http_ensure_time` 导出——penpal https 路径与既有
  `http_*` 共享**同一份 CA bundle + TLS 策略**，避免漂移副本；既有调用
  方行为逐字节不变（3 处调用点仅改函数名）
- `env.cfg.example` / `config_keys.h.example` 补模板行

### 1.2 `16c13e3` — R9 残留线程查询（服务端跟进，插在 1/2 之间）

- 服务端 R9 上线（`GET /emails` 的 `pen_pal_id` 可选，demo 脚本步骤⑪）
  → **GET-only 复测**：`?thread_root_id=50` 单参 200 + 响应
  `pen_pal_id=null` + emails `[50,56]`；双参皆缺 400；他人线程 403
- `penpal_get_thread` 在 `pen_pal_id<=0`（null 哨兵）时**省略该参数**
  （单参残留通道）；头注释同步 R9 契约
- 设计文档 v3.2：v3.1 过渡方案"HOME 过滤 null 行"退役，恢复
  "HOME 显示 + THREAD 只读"（§4.1/§4.4/§5/§7/§8 R9 关闭）
- `scripts/remote_api_demo.py` 步骤⑪（用户手改，随本 commit 入库）

### 1.3 `b231dd3` — 屏幕 UI（`ui_penpal.h` + 3 个 .cpp，~2000 行）

- **ui_penpal.cpp**（异步框架 + HOME/CFG + 生命周期）：单队列 `s_pp_q`
  深度 4（§3.2 已登记偏差，结果头两字段 `gen`/`type`）；`s_pp_busy` +
  `s_pp_busy_gen`（仅同代次可清 busy）+ 页代次 `s_pp_gen`（进/退/取消 ++
  内部换页不 ++）；任务 `xTaskCreate` 1024*8 prio 1 独占堆快照
  （`new/delete`）；**waitbox Close 按类型拆分**——READ=取消
  （gen++、busy=false、迟到结果丢弃），SEND=收起后台继续（busy/代次
  保持、结果照常消费）——**本仓首个可取消 waitbox**（§3.2 首例标注）；
  HOME（笔友行 + 5 行/页线程列表 + 串行 PALS→MAILBOX 同步、busy 跨腿
  保持）；CFG（base/key 保存 NVS，`/env.cfg` 现值提示）；poll 挂接
  `factory.ino` loop()
- **ui_penpal_write.cpp**（COMPOSE/TOPICS + 发送管线）：Title 56 **字节**
  UTF-8 边界 clamp（`Re: `+56=60≤64）；正文 ≥50 字符门槛（`pp_utf8_count`
  按字符）；幂等键 RAM 生命周期——payload 与快照相等则复用、任何编辑
  重新生成、确认成功作废、失败保留；SEND 在飞**编辑锁**（v3.1 P1：
  Title/Body/Pick/Tips `LV_STATE_DISABLED`，Close 不解锁）；结果消费时
  payload 快照比对才清空 COMPOSE（不等 → `previous send ok - draft kept`）；
  TOPICS 6 行/页 + suggestion 覆盖层（Use 自动填 Title）
- **ui_penpal_read.cpp**（THREAD/FB/PROFILE）：THREAD 新信在前（下标 0）、
  Start/Prev/Next 三键 + `%d/%d · %d old dropped` 计数；非我信
  Fix/Polish DISABLED（kimi v2）；Reply 仅第 1 页且绑定线程；
  **R9 只读形态**：`thr_pal==0` 信头加 `pal removed - read only`、
  Reply 隐藏（我方信件 Fix/Polish 仍可用）；FB 页显示任务侧预格式化
  文本 + degraded 后缀；触摸滚动抑制/释放全刷（chat 模式）
- COMPOSE 文本框**创建一次不销毁**，草稿跨页/跨 TOPICS 往返存活
  （RAM only，R7 取舍）

### 1.4 `5329383` — 菜单第 3 页 + 图标（9/9/1，PenPal 独占第 3 页）

- `ui_deckpro.h`：`SCREEN_PENPAL_ID` 追加枚举尾
- `ui_deckpro.cpp`：`menu_screen3` + **共享 `menu_page_create()`**（原
  screen1/2 两段复制粘贴样式块 → 单一 helper）；按钮分派三路
  `i<9/<18/else`；手势 cb 泛化为 **`menu_page_apply()`**（切页 + 页点
  单点复绘，任意页数成立；`page_num` 公式沿用 `de78338` 最大下标语义，
  19 项 → 下标 0..2）；页点按 `page_num` 循环创建（原 2 个硬编码）+
  create0 末尾 apply 一次——**顺带修存量瑕疵**：初始两页点全黑
  （首次手势前 active 点未画）；`scr_mgr_register(SCREEN_PENPAL_ID,
  &screen_penpal)`（前一 commit 的屏幕自此可达）
- `scripts/gen_img_penpal.py` **新增**（无 gen_img_* 先例，设计 §1.11.2
  已注明）：纯 Python 光栅化信封图标 → `src/img_penpal.c`，50×50
  `LV_IMG_CF_TRUE_COLOR_ALPHA`，**2 字节/像素 (color, alpha)**（
  `LV_COLOR_DEPTH=1` 所致），ink=`0x00,0xff`——格式经 `--inspect`
  渲染既有 `img_dictionary.c` 验证一致；`src/assets.h` 补 declare

## 2. 已登记偏差 / 先例（设计评审已批准，此处复核）

| # | 事项 | 出处 |
|---|---|---|
| D1 | 单队列 `s_pp_q`（结果带 `gen`+`type` 两字段分发），非每请求一队列 | 设计 §3.2（v2 前置 4 登记） |
| D2 | waitbox Close 可取消（READ 型）——**全仓首例**；SEND 型=后台继续 | 设计 §3.2 / v3 P1 |
| D3 | 配置单槽 NVS `"penpal"`（weather/provider-key 单槽先例） | 设计 §3.4 / R3 |
| D4 | COMPOSE 草稿 RAM-only 不落盘 | 设计 R7（真机试用后再议） |
| D5 | 明文 `http://` 承载 key/信件（测试服务器）；https 已备 | 设计 R8 |
| D6 | http_utils static→导出（增量、零行为变化） | §1.1 |

## 3. 实现裁量（设计未逐一钉死的小决策，供裁量）

1. **自动换页仅当用户仍在发起页**（THREAD←HOME、TOPICS←COMPOSE、
   FB←THREAD）：数据无论在哪都入库；用户已翻走则不"拽回"。
2. **HOME 提示行单同步周期合并**：发信成功后的自动同步把
   `sent ok (replayed)` 保留并追加 `· Mailbox OK`（手动 Sync 才清）
   ——否则自动同步立刻冲掉重放标志，用户看不到。
3. msgbox `+/-` 滚动的是**容器**（体 label 不可滚）；THREAD "To:" 头
   按 `thr_pal` 查笔友名（非 COMPOSE 收件人）。
4. 菜单：三处泛化（§1.4）在 19 项下行为与 18 项等价（page 0/1 往返
   不变）；`menu_screen3` 无条件创建（隐藏）——与 screen2 原做法一致。
5. 图标生成脚本含 `--inspect` 子命令（把"格式与既有图标一致"从断言
   变成可复现验证）。

## 4. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` SUCCESS（RAM 49.9% 163524/327680，flash 31.1% 2037345/6553600） |
| 烧录 | ✅ | COM5 三次（b48f584 / b231dd3 / 5329383 各自验证），hash verified |
| 开机冒烟 | ✅ | 串口 45s ×3：EPD 刷新、无 panic、无重启循环 |
| API 客户端（PC 侧） | ✅ | R9 GET-only 复测（§1.2）+ demo 脚本全步骤（幂等重放头实证，v3 记录） |
| **真机功能回归** | ⏸ | 设计 §7 清单（下方摘录），待用户实测 |

**§7 真机回归摘录（⏸ 全部待测）**：Cfg 保存/NVS 持久；HOME 自动+手动
同步；写信门槛（<50 字符拒、Title 56 字节 CJK 截断）；topic 选择 →
Use 自动填 Title；**幂等重放**（重按 Send 单信 + `sent ok (replayed)`，
编辑一字重发=新信）；**后台发送编辑锁**（Close 后四控件灰显、成功清空
解锁/失败保留解锁）；THREAD 三键导航 + Fix/Polish（我信）+ Reply；
同题双线程互不混信；NPC ~2 分钟回信 → Reply/Tips；**null 笔友残留行**
（HOME 显示、THREAD `pal removed - read only`、无 Reply）；**菜单第 3
页**（图标显示、三页往返、第 3 页继续左滑不越界）；断网各操作报错
不卡死、waitbox Close 可取消。

## 5. 遗留项（简要）

- 设计 R7（草稿落盘 `/penpal.draft`）真机试用后决定；⑩ 本人 profile
  端点仍不接入（R2）。
- （可选）Kimi 对实现再走一轮——由用户定。
- 待结果申请清点：`c8f62f3`、`71fa528..a58a73c`、`141942d`（weather
  零槽修复）+ 本份。
- **未 push**：本批 4 commit + docs commit 在本地，等用户指令。
- CI 矩阵日志抽查（set_srcdir 修复验证）仍待做。

## 6. 回滚方案

```bash
# 按逆序逐个 revert（menu → UI → R9 → API）；设计文档 revert 可选
git revert 5329383 b231dd3 16c13e3 b48f584
```

菜单 revert 后回到 2 页 18 项；penpal_api 无调用者可独立回退；
`http_utils` 导出为增量，revert b48f584 时一并还原为 static。

## 7. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
