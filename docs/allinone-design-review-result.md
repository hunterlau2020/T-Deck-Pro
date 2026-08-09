# allinone 设计稿与 pda2 试点代码评审结果

- **评审日期**：2026-08-09
- **评审申请书**：[allinone-design-review-request.md](allinone-design-review-request.md)
- **设计稿**：[allinone-design.md](allinone-design.md)
- **关联 commit**：`27ad8d5`
- **评审结论**：**退回修订**

## 1. Findings

### 1.1 内置 CA 损坏且未覆盖默认服务端证书链

- **严重性**：High
- **位置**：`examples/pda2/http_utils.cpp:24-66`、`examples/pda2/http_utils.h:33-35`
- **触发场景**：默认使用 `HTTP_TLS_CA_VERIFY` 请求 OpenRouter 或 dictionaryapi.dev。
- **证据**：
  - 当前 PEM 解码后数据不完整，证书解析会失败。
  - 代码实际只包含 ISRG Root X1，并非设计稿声称的 ISRG、DigiCert、GlobalSign 三个根证书。
  - OpenRouter 当前证书链不能由现有 ISRG Root X1 单独验证。
- **影响**：默认配置下 AI 和词典 HTTPS 请求可能无法建立连接，核心联网功能不可用。
- **最小修复**：
  1. 替换为完整、可解析的 CA PEM。
  2. 补充默认端点当前所需的 GTS/GlobalSign 信任链。
  3. 增加使用内置 CA 分别连接 OpenRouter 和 dictionaryapi.dev 的握手测试。

### 1.2 删除时间同步后无法可靠执行证书验证

- **严重性**：High
- **位置**：`docs/allinone-design.md:50,128`
- **触发场景**：设备冷启动后，在默认 CA 验证模式下首次访问 HTTPS。
- **证据**：
  - 设计要求默认验证服务器证书。
  - `allinone.ino` 裁剪说明明确删除 `configTzTime()`。
  - 当前 pda2 仍在 `examples/pda2/factory.ino:713` 调用 `configTzTime()`。
- **影响**：ESP32 冷启动时系统时间无效，证书有效期检查可能失败，导致所有 HTTPS 功能不可用。
- **最小修复**：保留 SNTP，或使用 GPS 校准系统时间；首次 HTTPS 请求前确认系统时间已达到合理年份。

### 1.3 GPS 快照没有保护写端，且 UI 未使用快照接口

- **严重性**：Medium
- **位置**：`examples/pda2/peri_gps.cpp:128-218`、`examples/pda2/ui_gps_enhanced.cpp:326-333`
- **触发场景**：GPS 任务更新定位数据时，LVGL 定时器同时刷新 GPS 页面。
- **证据**：
  - `gps_get_snapshot()` 只在读取时进入临界区。
  - `displayInfo()` 在临界区外逐个写入共享字段。
  - GPS UI 仍连续调用多个旧 getter，没有调用 `ui_gps_get_snapshot()`。
- **影响**：经纬度、时间、速度和卫星数可能来自不同更新周期；64 位字段还可能发生撕裂读取。
- **最小修复**：GPS 任务先生成局部快照，再在同一临界区整体写入共享结构；GPS UI 改为一次读取完整快照。

### 1.4 周期性 EPD 全刷路径不可达

- **严重性**：Medium
- **位置**：`examples/pda2/factory.ino:220-261,318`
- **触发场景**：连续局刷达到 `FACTORY_EPD_FULL_REFRESH_INTERVAL` 设定的 60 次。
- **证据**：
  - 局刷计数逻辑只位于 `flush_timer_cb()`。
  - 创建该定时器依赖 `dips_render_start_cb()`。
  - `disp_drv.render_start_cb = dips_render_start_cb` 仍在第 318 行被注释，且没有其他调用点。
- **影响**：计数不会增长，周期全刷永远不会执行，长期运行仍会累积 EPD 鬼影。
- **最小修复**：正确注册刷新回调，或直接在实际执行的 `disp_flush()` 路径中统计局刷并触发全刷。

### 1.5 菜单目标数量与屏幕 ID 数量矛盾

- **严重性**：Medium
- **位置**：`docs/allinone-design.md:72-85,131-132`
- **触发场景**：按设计实现八个数字键对应的菜单目标。
- **证据**：
  - 文档声明“菜单 + 7 个功能屏，共 8 屏”。
  - 菜单实际列出 GPS、Music、Dict、Keys、WiFi、WiFi Cfg、AI、AI Cfg 共八个目标。
  - 文档同时要求 `SCREEN_XXX_ID` 只保留八个并只注册八屏。
- **影响**：实现时必然缺少一个屏幕 ID，或者出现重复映射、某菜单项不可达。
- **最小修复**：明确 WiFi 与 WiFi Cfg 是否为两个独立屏幕；若是，总数应改为九屏并新增对应 ID，否则合并两个菜单项。

### 1.6 AI 自动换行会切断 UTF-8 字符

- **严重性**：Medium
- **位置**：`examples/pda2/ui_ai_chat.cpp:60-76`
- **触发场景**：AI 回答包含中文、emoji，或多字节字符跨过固定的 30 字节边界。
- **证据**：`strlen()`、固定 `take = 30` 和 `memcpy()` 均按字节处理，可能在 UTF-8 continuation byte 中间插入换行。
- **影响**：回答可能出现乱码、缺字或 LVGL 字符解码异常。
- **最小修复**：按 UTF-8 码点边界断行，不得从多字节字符中间切割；推荐进一步按字体像素宽度分页。

### 1.7 MP3 EOF 回调签名在设计稿中仍不一致

- **严重性**：Medium
- **位置**：`docs/allinone-design.md:91,158`
- **触发场景**：实现者依据 MP3 交互章节定义播放结束回调。
- **证据**：
  - 第 91 行仍写作无参数 `audio_eof_mp3()`。
  - 第 158 行及 `lib/ESP32-audioI2S/src/Audio.h` 要求 `void audio_eof_mp3(const char *info)`。
- **影响**：按无参数形式实现时无法覆盖库的弱回调，播放结束后自动切歌不会触发。
- **最小修复**：全文统一使用 `void audio_eof_mp3(const char *info)`。

## 2. 验收覆盖结论

| 验收项 | 结论 | 说明 |
|---|---|---|
| AI Cfg 可达 | **不通过** | 菜单目标数量与屏幕 ID 总数矛盾，无法确认所有入口均可注册和访问 |
| TLS 证书验证 | **不通过** | CA 数据损坏、信任链不匹配，且 allinone 计划删除时间同步 |
| AI 长回答分页 | **不通过** | 已增加分页和截断提示，但固定字节切割会破坏 UTF-8 |
| GPS 数据一致性 | **不通过** | 写端未同步，GPS UI 仍未使用快照接口 |
| EPD 周期全刷 | **不通过** | 计数和全刷逻辑所在路径没有注册 |
| WiFi 键盘选择 | **通过** | 当前设计使用 keypad 读取和关闭 dropdown，不依赖触摸事件 |
| MP3 EOF 回调 | **不通过** | 设计稿仍同时存在无参数和正确签名 |
| 源码引用稳定性 | **通过** | 未发现残留的具体源码行号定位方式 |
| 构建命令与容量口径 | **通过** | 命令已改为可移植形式，容量按应用分区统计 |
| allinone 完整构建 | **缺少证据** | `examples/allinone` 尚未完整落地，当前只能验证 pda2 试点代码 |
| 真机功能验证 | **缺少证据** | 评审申请书说明目标硬件未连接，无法验证 TLS、GPS、EPD 和交互行为 |

## 3. 审批意见

- [ ] A. 全量接受
- [ ] B. 仅保留设计稿
- [x] C. 退回修订
- [ ] D. 拆分提交

建议先解决两项 High 问题及 GPS、EPD 的无效整改路径，再重新申请评审。菜单屏幕数量和 MP3 回调签名可与设计稿修订一并完成。
