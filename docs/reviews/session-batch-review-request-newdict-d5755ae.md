# 评审申请：WordBank "new_dict" 词库 APP（v1.19，单提交 d5755ae）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-09-18
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）
- **关联 commit**（1 个）：
  - `d5755ae` — **v1.19**：new_dict APP 全栈（`ui_newdict.cpp/.h` + `penpal_api`
    词库封装 + 菜单第 2 屏入口 + factory 轮询）
- **关联文档提交**：`355e3dc` 为其补登了 `docs/async_ipc_contract.md` 的 WordBank 行
  （含 PenPal/LevelTest 补登，纯文档，不属本轮评审代码面）。
- **背景**：用户指令"按新范例 `demo_vocab_bank`（步骤 ⑫）写新字典 app"。端点为只读
  learn-scope：`GET /words?skip&limit&q`（q 同时模糊匹配 word 与 meaning_zh）+
  `GET /words/{id}/detail`（词性分组释义/例句/词块/关联词，解析层拍平为 EPD 行）。
- **硬件**：机 `28:37:2f:91:2c:20`（COM5）已烧 v1.19；**真机功能验收被环境阻塞**——
  设备 NVS 指向的生产服务器 `/words` 尚未部署 key 通道（HTTP 401，诊断见
  CHANGELOG v1.19）；本地开发服务器（当前代码）API 冒烟全通。

## 头部自校

- `git show --stat d5755ae` = **10 文件，835 insertions / 2 deletions**（与归属表一致）
- `git diff --check d5755ae^ d5755ae`：通过。

### 文件归属表（10 文件）

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/ui_newdict.cpp` / `.h` | **评（重点）** | LIST/DETAIL 两页状态机、键盘/触摸交互、wa_* 异步胶水 |
| `examples/pda2/penpal_api.h` / `.cpp` | **评（重点）** | `penpal_wb_list`（q percent-encode）/ `penpal_wb_detail`（拍平解析）；`s_urlenc` 复用自 v1.17 |
| `examples/pda2/ui_deckpro.h` / `.cpp` | 评 | `SCREEN_NEWDICT_ID`、菜单第 2 屏 "NewDict" 条目（22 入口）、注册 |
| `examples/pda2/factory.ino` | 评 | loop 挂 `newdict_keyboard_poll()` |
| `examples/pda2/fw_version.h` | 评（低风险） | v1.19 版本链 |
| `CHANGELOG.md` / `TODO.md` | 不评（纯文档） | 台账 |

## 自评最没把握的点（请重点击穿）

1. **异步契约遵循**：`s_nd_q` 一次创建永不删除、`s_nd_task` 任务句柄即 busy（任务自
   清，wa 模式——契约 §1 表已登记该变体）、`s_nd_gen` entry/exit 各 +1。请对照
   `docs/async_ipc_contract.md` §2 十一条硬性规则逐条过。
2. **Enter 语义重载**：LIST 页 Enter = "查询脏或列表空 → 搜索；否则 → 打开焦点行"。
   用户敲完字没按 Enter 直接按 +/- 翻页时，`s_query_dirty` 仍真、翻页请求却用**新**
   `s_query` + 旧 skip 发出——结果集语义是否会让用户困惑？（我认为可接受但没把握。）
3. **`nd_render_detail` 缓冲**：senses 6×84 / examples 2×164 / tail 2×84+100 静态栈缓冲
   + `snprintf` 链式截断；`off < sizeof - 行宽` 的边界是否有差一。
4. **assoc 拍平的 `started` 标志**：首词前缀 ": "、后续 ", "——初版用 `off > 2` 判首词
   是错的（已修），请复核现实现。
5. **数据形状 vs 设备缓冲**：实库 word 最长 16B（buf 28）、`meaning_zh` 最长 92B
   （buf 56，`s_copy_disp` 边界截断 + "..."）；词表行 `%-12s %-5s` 拼装 96B buf。
   服务端字段 nullable 的空值路径（`s_lt_str` 返回 NULL → `s_copy` 空串）。
6. **entry 自动拉取**：首次进屏自动 `nd_fetch_list(0)`；WiFi 未连时 `nd_start` 拒绝并
   打点，列表空 + 状态行提示。再进入时若上次请求仍在飞（`s_nd_task` 非空）→ 跳过
   拉取、渲染旧列表，在飞结果因 gen 不符被丢弃后**无自动重拉**——已知边界，请评估
   是否需要补。
7. **退格语义**：DETAIL 页退格 = 返回列表（不退出）；LIST 页查询空时退格 = 退出 APP。
   与本地 Dict APP 旧习惯（输入框退空才退）一致性。

## 验证边界（诚实声明）

- 编译 SUCCESS；COM5 刷写 6/6 VERIFIED + `[FW] v1.19` + `mark valid ok`。
- API 冒烟（开发机直连本地服务器）：`/words` 分页（total=6448）、`q=app`→41 词、
  `q=苹果`→apple（中文子串命中）、`/words/1/detail` 字段形状——全 200。
- 真机交互流程（搜索/翻页/详情/触摸）：**未验收**（生产服务器 401 阻塞 + 待用户切
  base 到本地开发服务器后复测）。
- 生产服务器 401 归因（其 `/words` 先于 key 通道改动部署）为环境诊断，非代码缺陷；
  诊断链在 CHANGELOG v1.19。
