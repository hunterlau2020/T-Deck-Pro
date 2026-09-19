# 评审申请：NewDict 验收修复轮 v1.24-v1.27 + Gate-pin v1.26 + OTA 台架/换根 v1.28（c531d66..51f76ac）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-09-20
- **关联分支**：`HD-V2-250915`
- **评审依据**：`docs/review_guide.md` v1.3，代码口径（§2.3 A/B/C）；
  含真机验收结果核对（§2.5 精神：修复/验收声明按证据核销，不采信转述）
- **关联 commit**（**2 个**，区间 `c531d66..51f76ac`，首不含记法；等价
  于含两端记法 `aad1fb3^..51f76ac`——受评提交即下表两项，无中间提交）：
  - `aad1fb3` — **v1.24-v1.27**：NewDict 真机验收修复轮（布局溢出根修、
    状态行页码指示器、`+/-` 整页翻、DETAIL 实测度量本地翻页）+ 每页
    7 行（PP_WB_ROWS 8→7）+ **Gate-pin 去 hardcode**（Whoami Cfg 可配）
  - `51f76ac` — **v1.28**：OTA 局域网台架端到端通过（矩阵④）+ 台架宏名
    漂移 P2 修复（OTA-1）+ 信任锚换根 `325BDA1A`（OTA-2）+
    `pda2_ota_bench` 台架环境 + `docs/ota-baseline.md`
- **背景**：①v1.22 批量评审（§32）的处置轮产物；②NewDict 首次真机
  验收（§33/§34）暴露 6 项，全部在本区间修复并复验；③OTA 矩阵 ④
  首次真机执行，抓住台架宏名漂移 P2（§35 OTA-1）。
- **硬件**：机 #1（COM5）= **v1.28**（USB 刷写，串口确认）；机 #2
  （COM7）= v1.27（刷 v1.28 时 CDC 掉口零写入，待重插补刷）；#3 未连。

## 头部自校

- `git rev-list --count c531d66..51f76ac` = **2**（与上方列表一致）
- `git diff --check aad1fb3^ 51f76ac`：通过
- 全区间 diff：**18 文件，+891/−196**

### 区间文件归属表（逐文件）

| 文件 | 归属 | 理由 |
|---|---|---|
| `examples/pda2/ui_newdict.cpp` / `.h` | **评（重点）** | v1.24-v1.27 全部 NewDict 改动：布局重排、分页指示器、`+/-` 整页翻、DETAIL 实测翻页、目的地快照消费、deferred exams、7 行行池 |
| `examples/pda2/penpal_api.h` | **评** | `PP_WB_ROWS` 8→7（fetch/行池/翻页/页码全链）；Gate-pin 配置链声明 |
| `examples/pda2/penpal_api.cpp` | **评** | `penpal_load/save_gate_pin`（NVS `gpin` → env `GATE_PIN` → 默认）；`X-Gate-Pin` 头改为配置链取值 |
| `examples/pda2/ui_whoami.cpp` | **评** | Cfg 页 Gate PIN 行（布局压缩）、三字段键盘路由、保存/状态分支 |
| `examples/pda2/ota_update.cpp` / `.h` | **评（重点）** | OTA-1 宏统一（:333/:445 与方案门 ：72 一致）；`OTA_ALLOW_PLAIN_HTTP` 值宏化（头文件默认 0，`#if` 门） |
| `examples/pda2/ota_trust_anchor.h` | 评（核对） | 换根后锚点 `325BDA1A`（脚本生成，勿手改） |
| `examples/pda2/fw_version.h` | 评（低风险） | v1.24→v1.28 版本链 |
| `examples/pda2/env.cfg.example` | 评（低风险） | `GATE_PIN=` / `OTA_URL=` 注记 |
| `platformio.ini` / `script/set_srcdir.py` | **评** | `pda2_ota_bench` 环境（`-DOTA_ALLOW_PLAIN_HTTP`）+ src_dir 映射 |
| `.gitignore` | 评（低风险） | `ota_test/` 排除 |
| `docs/ota-baseline.md` | 不评（新建文档） | D4 记录 + D1/D2/D3 模板 |
| `CHANGELOG.md` / `TODO.md` / `docs/issue_list.md` / `SECURITY.md` | 不评（纯文档） | §32-35 台账、v1.24-v1.28 记录、密钥章节 |

## §1 NewDict 验收修复轮（v1.24-v1.27，`ui_newdict.cpp` 为主）

真机验收（§33）暴露 6 项，处置：

| # | 现象 | 根因 | 修复 |
|---|---|---|---|
| A-1 | List 焦点"消失"、底部半行字 | **TABS 页内容栈 330px > 容器 263px**，禁滚动下底部静默裁切 | 行 23 / 搜索框 24（pad 显式 2）/ tab 26 / pad_all 2 / pad_row 3；v1.25 复验仍裁 → 余量拉到 ~18px + `create_nd` 一次性布局实测串口打印 |
| A-2 | List 残留 Home 搜索框；切换后在飞消息残留 | 搜索框未绑 tab；状态行无清场 | 搜索框仅 Home（hidden 退出 flex）；`nd_render_tabs()` 返回指示器状态，无指示器即 `nd_status("")` |
| A-3 | 列表无页码；退出详情残留详情页码 | 无指示器 | `nd_render_page_indicator()` 三视图统一（consume 重排为先渲染后消息） |
| A-9 | HOME `+/-` 被读作失灵；用户改需求 | 在飞静默丢弃；逐行移动对触摸屏无意义 | 在飞 → `busy - wait`；`+/-` 改**整页跳**（Home/词书 skip±7、考试卡窗口±7 焦点贴首行，边界显示 last/first page） |
| A-4 | 空缓冲乱码隐患 | `ex[]`/`tail[]` 未终止 | **结构性消除**：实测翻页重写后栈缓冲不复存在 |
| A-6 复发 | 第 8 行仍被裁 | v1.24 手算预算零余量 | 每页改 7 行（用户决策），裁切物理性消失 |

**DETAIL 本地翻页**（v1.23）：正文拍平为逻辑行，按 **LVGL 实测度量**
分页（`nd_next_cp` UTF-8 解码 + `lv_font_get_glyph_dsc` 真实 advance +
`font->line_height` + 容器实时几何；安全余量一行）；body 字体固定
`Font_Hanzi_16` 与度量一致；`+/-` 重渲染（EPD 无滚动）。首版常数估算
被真机否决（每页只有一半）——已改实测。

## §2 Gate-pin（v1.26）

- 配置链：NVS `penpal.gpin` → `/env.cfg GATE_PIN=` → 编译期默认
  `PENPAL_GATE_PIN_DEFAULT`（现值不变，未配置行为与旧版一致）。
- **存空 = 回退配置链**（空 header 会 403，与 base/key 的"空保存即赢"
  **有意不同**——头文件注释已说明）。
- Whoami Cfg：Gate PIN 行 + 三字段 Tab 焦点链 + 保存分支
  （`server/provider/gpin` 三路失败分别反馈）。

## §3 OTA（v1.28，`ota_update.cpp` + 台架）

- **端到端真机通过**（§35）：Check → Update available → Confirm →
  下载 100% → 重启 → `mark valid ok` → `already up to date (seq 1)`。
- **OTA-1（P2，修复）**：`http.begin` 两处（:333 manifest / :445 下载）
  检查遗留宏 `OTA_ALLOW_PLAINTEXT`（恒 0），而方案门（:72）检查
  `OTA_ALLOW_PLAIN_HTTP` → 台架版 URL 过门但连接永不建立。修复：统一
  为 `OTA_ALLOW_PLAIN_HTTP`；头文件改**值宏**（默认 0），方案门 `#ifdef`
  改 `#if`（防"定义即放行"）。**生产 https 路径不受影响**（强 CA 校验
  分支未动）。
- **OTA-2**：信任锚换根 `325BDA1A`（旧钥丢失，零暴露——无设备装过旧签
  manifest），锚点随 USB 重刷两台；SECURITY.md 补密钥章节。
- **台架沉淀**：`pda2_ota_bench` 环境 + `set_srcdir` 映射；`ota_test/`
  服务工具（gitignored）。

## §4 自评"最没把握"清单（邀请击穿）

1. **整页翻边界算术**：Home `skip±7` 与 `s_list.skip > PP_WB_ROWS ?
   skip-7 : 0` 的下溢防护；考试卡窗口 `±7` 后 `s_exam_focus = s_exam_top`
   与缓冲 Enter 消费 `nd_open_exam(s_exam_focus - s_exam_top)` 的组合。
2. **实测翻页的 UTF-8 解码器**：`nd_next_cp` 对 4 字节序 / 截断尾字节
   的行为；`nd_glyph_w` fallback（`line_height/2`）对缺字形行高的影响；
   word-boundary 换行与逐字换行估计的偏差方向。
3. **deferred exams 竞态**：`s_exams_deferred` 在 entry 清零、tab 切走
   清零、消费末尾补发三处路径的完备性；补发被 `nd_start` 拒绝时标志
   已清（用户需重点 List 重试）——是否可接受。
4. **dirty 比较**：`strcmp(s_query, m->q)` 在"删除又重打回同一词"场景
   会把 dirty 清 0（显示列表恰好匹配该词，Enter 开焦点行）——语义
   是否可接受。
5. **Gate-pin 空值语义**：存空回退链上默认值（而非发空 header）——
   是否应允许"显式空"。
6. **`OTA_ALLOW_PLAIN_HTTP` 值宏**：`#if` 门 vs `#ifdef` 的等价性
   （生产只含默认 0 定义）；`pda2_ota_bench` 的 `-D` 与头文件默认的
   交互。
7. **7 行页码算术**：页码 = `skip/7+1`、总页 = `ceil(total/7)`——服务端
   skip 非 7 倍数时（不会发生，但请核对）页码显示。

## §5 验证表

| 项 | 状态 |
|---|---|
| BUILD `pda2` / `pda2_ota_bench` | ✅ 申请人环境实测 SUCCESS（v1.28 定版） |
| NewDict 验收（§33/§34） | ✅ 真机 COM5，逐条记录（A-1 复发→7 行根修待最终复验） |
| OTA 端到端 | ✅ 真机 COM5（§35：全链路 + seq 持久化） |
| Gate PIN 链路 | ⏳ 真机待复验（改错→403→改回） |
| C2-C4 WiFi 串口判据 | ⏳ 待测（方法已给用户） |
| HW 其余（EPD 肉眼/触摸手感） | CLAIM_ONLY → 随日常使用观察 |

## §6 遗留与回退

- **遗留**：W-2 `scan failed(-2)` 快速重试（P3）；C3 慢扫描反例；COM7
  补刷 v1.28；OTA D1/D2/D3；生产站证书续期 + 端点部署（用户侧）。
- **回退**：本区间无 NVS/服务端格式变更；`PP_WB_ROWS` 回 8 即回旧行数
  （单常量）；OTA 台架仅编译期宏，生产构建零行为差异。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 退回修订
- [ ] C. 部分接受
