# 评审结果：PenPal 点击死机根治——LVGL 池扩容 48K→64K（Grok）

- **评审日期**：2026-09-13
- **评审人**：Grok（Cursor）
- **申请文件**：[penpal-pool-freeze-review-request-721e04a.md](penpal-pool-freeze-review-request-721e04a.md)
- **评审提交**：`721e04a` — `lvgl: enlarge LV_MEM pool 48K -> 64K - PenPal click-freeze root cause`
- **同日另一份结果**：[penpal-pool-freeze-review-result-721e04a-claude.md](penpal-pool-freeze-review-result-721e04a-claude.md)（Claude **A**）
- **评审结论**：**A 全量接受**（与 Claude 同结论；下文补 2 条独立核证 + 2 项不阻塞 Low）。

## 核对结果

- `721e04a` 仅改 `config/lv_conf.h`（`LV_MEM_SIZE` 48K→64K + 根因注释）和
  `examples/pda2/ui_penpal.cpp`（`pp_dbg_pool()` 于 `create` / `cached` /
  `wbshow` 三处）。工作树仍是该状态，commit 是当前 `HEAD` 祖先。
- 因果链与 `docs/issue_list.md` §15、CHANGELOG 2026-08-29、坑 7 一致。
  独立补证：`config/lv_conf.h` 中 `LV_USE_ASSERT_MALLOC=1` 且
  `LV_ASSERT_HANDLER` 为 `while(1);`。池耗尽时 LVGL 断言即停转，对应
  「死机无 panic」；`f8d73f6` 的 LoadProhibited `0x22` 是同一耗尽在
  TLSF 边缘的另一种表现，不是第二个 bug。
- RAM：`boards/T-Deck-Pro.json` `"maximum_ram_size": 327680`，+16384 B
  ≈ +5.0 个百分点，与申请书 50.1%→55.1% 相符；说明验收发生在
  `pio run -t clean` 之后，不是坑 7 的假扩容。
- 诊断批次未合入：工作树无心跳 / 栈扫描 / 面包屑残留。`pp_cache_write`
  仍由 worker 写 SPIFFS，与申请书「整体弃置」一致。

## 回答申请书 §4

1. **池尺寸**：64K 合理。实测缺口只有数百字节，扩 16K 后同检查点约 17K
   余量。现在不要改 `LV_MEM_CUSTOM` 接 PSRAM / `heap_caps`：LVGL 池是
   高频 alloc/free，QSPI PSRAM 未命中延迟会拖慢每次控件创建。内部 SRAM
   静态数组是当前正确取舍。控件树再涨应拆隐藏页或按页销毁，而不是先
   把池搬出 SRAM。
2. **观测点噪声**：可接受。每进 PenPal 约 2 行串口 + 每次等框 1 行，不
   碰 EPD。不建议现在改成条件触发——这正是教训②「别再靠没崩判断健康」
   的落地。
3. **坑 7**：准确。`platformio.ini` 的 `-include config/lv_conf.h` 不进
   SCons 依赖图；「改完必须 `-t clean`，用 size 报告 RAM 涨幅核验」可
   执行，不必为本 commit 改构建系统。
4. **诊断批次弃置**：认可。worker→UI 的 SPIFFS 迁移会堵住
   `lv_timer_handler` / EPD，没有独立崩溃证据就不要另开评审。

## Low（不阻塞）

1. `pp_dbg_pool()` 注释仍写「device-verified 后裁掉」，真机已于
   2026-08-29 确认。应改成常驻观测点；建议 `free < 8K` 时打 WARN，树
   再涨时有告警而不是无声逼近临界。
2. 申请文件名未用 README 的 `wifi-config-keyboard-review-request-*`
   前缀，且缺 A/B/C 审批区块。与 Claude Low-1 同项；不影响本结论。

## 验证说明

- 静态核对 `721e04a` diff、`lv_conf.h` 断言配置、`ui_penpal.cpp` 三处
  调用、issue_list §15、坑 7、板级 RAM 预算。
- 本环境无 `pio`，未复跑编译；真机复测采信申请书（用户 2026-08-29
  确认邮件列表 / Sync / 开信 / topic pick）。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受

**与 Claude 同日结果**：同为 A。差异仅在独立补证（`LV_ASSERT_HANDLER`
死循环解释无 panic）与 Low（8K WARN / 过时注释）；无结论分歧。
