# 评审结果：PenPal 点击死机根治——LVGL 池扩容 48K→64K（Claude）

- **评审日期**：2026-09-13
- **申请文件**：[penpal-pool-freeze-review-request-721e04a.md](penpal-pool-freeze-review-request-721e04a.md)
- **评审提交**：`721e04a`
- **评审结论**：**A 全量接受**（另有 2 项不阻塞的 Low 建议，见文末）。

## 核对结果

- 复核 `721e04a` 完整 diff（`config/lv_conf.h` + `ui_penpal.cpp`），与
  `docs/issue_list.md` §15、`docs/build-and-code-structure.md` 坑 7 的叙述
  逐字对得上，三处文档（申请书/issue_list/build 文档）互相印证、没有
  漂移。
- 定位链本身站得住：Step B 探针（只建控件不写文本）复现崩溃，排除了
  "渲染逻辑"这条岔路；`pp_dbg_pool()` 三个检查点的水位数字
  （2172B → 540B → 524B）与等待框（box+2 label+btn+文本缓冲）需要的
  字节量在同一量级，"分配失败→冻结/panic"的因果链合理，也解释了为何
  不同诊断构建里崩溃点会漂移（`lv_mem_buf_get` 挂死 / layout-dirty
  panic 都是同一份池耗尽在 TLSF 分配器里的不同表现，不是独立 bug）。
- **RAM 涨幅验证**：`+16384B` 与 `64K-48K` 精确相等，且申请书记录的
  "50.1%→55.1%" 与 `boards/T-Deck-Pro.json` 里
  `"maximum_ram_size": 327680`（=320KB）换算一致（16384/327680≈5.0%）。
  这说明修复确实是在"干净重编"之后验证的，不是坑 7 描述的那种
  `-include` 假生效误判——坑 7 的教训本身被这次修复正向利用了。
- 检查当前工作树：未发现任何遗留的心跳任务/栈扫描/面包屑代码
  （`grep` 全库 `heartbeat|stack.scan|breadcrumb|uxTaskGetStackHighWaterMark`
  无命中），确认申请书 §4.4 所称"诊断批次整体弃置未合入"属实，没有
  半吊子诊断代码混进主线。
- 复核"worker 写 SPIFFS 移到 UI 线程"这条被弃置的改动：当前
  `penpal_api.cpp:pp_cache_write()` 的文档注释明确写着"Written by the
  worker task"，与申请书描述的现状（该改动未合入，仍是 worker 线程写
  SPIFFS）一致。这条改动确实独立于本次池扩容修复，若评审认为仍有
  价值应另行走评审，同意申请人的处理方式。

## 回答 §4 请评审重点

1. **池尺寸选择（64K 是否合理）**：合理。48K→64K 是刚好覆盖实测缺口
   （缺口只有几百字节）之上留出充分安全边际（~17K，从几乎为零到约
   26%的空闲率）的保守但不过度的选择；`LV_MEM_CUSTOM` 接 PSRAM/heap_caps
   目前不建议——LVGL 对象池是高频 alloc/free 的"抖动"分配器，QSPI PSRAM
   的访问延迟（缓存未命中代价）会拖慢每一次控件创建/销毁，PSRAM 更适合
   大块低频访问的数据（图片缓存、响应缓存），不适合放这种延迟敏感的
   分配器；维持内部 SRAM 静态数组 + 按需扩容是当前阶段正确的取舍。
2. **观测点噪声**：可接受。每次进 PenPal ~2 行 + 每次网络动作 1 行，
   频率低，且正是"坑 7 教训②"要求的"水位观测点进主线"的落地，不建议
   现在就降级为条件触发——等这批新屏幕（OTA/语音）验证过、确认新常态
   下有稳定余量后再考虑收敛或加编译期开关。
3. **教训条目准确性**：`build-and-code-structure.md` 坑 7 的表述准确——
   `-include config/lv_conf.h` 确实不进 SCons 依赖图，`platformio.ini`
   第 164 行的 `-include config/lv_conf.h` 印证了触发条件；"改后必须
   `-t clean`，用 size 报告的 RAM 涨幅核验"这条规则足够具体、可操作、
   可验证，不需要补充。
4. **诊断批次整体弃置**：认可。已如上核实工作树无残留；"worker 写
   SPIFFS 移到 UI 线程"如有独立价值应另立评审，同意搁置。

## Low（不阻塞，记录跟进）

1. **申请书格式与 README 约定不一致**：`docs/reviews/README.md` 规定申请
   文件名应为 `wifi-config-keyboard-review-request-<commit范围>.md`，
   且"评审纪律"要求申请必须包含 A/B/C 审批事项区块；本申请书用了
   `penpal-pool-freeze-review-request-721e04a.md` 的新前缀，且没有 §5
   审批区块。不影响本次核实结果，但建议后续申请统一前缀或在
   README 里补一条"允许按主题命名前缀，只需 request/result 成对"的
   说明，避免评审工具/脚本按老前缀做归档匹配时漏检。
2. **后续新屏幕的池水位监控**：64K 目前有约 17K 余量，但近期正在推进
   OTA（`docs/ota-update-design.md`）等会新增不少控件的功能。建议新
   屏幕落地时，在其 `create`/首次弹窗点也照 `pp_dbg_pool()` 的样子打一
   行水位日志，防止重复这次"稳定版余量已经在悬崖边却没人发现"的模式
   （另见本轮 OTA 设计评审结果中的对应建议）。

## 验证说明

- 已静态核对 `721e04a` 完整 diff、`docs/issue_list.md` §15、
  `docs/build-and-code-structure.md` 坑 7、`boards/T-Deck-Pro.json`
  的 RAM 预算数字，以及工作树内是否有诊断代码残留。
- 本环境未安装 `pio`，未独立复跑 PlatformIO 编译或复现 `[PD]` 串口水位
  （与申请书一致，用户已完成真机复测）。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受
