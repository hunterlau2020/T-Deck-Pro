# 评审结果：PenPal 点击死机根治——LVGL 池扩容 48K→64K（Codex）

- **评审日期**：2026-09-13
- **申请文件**：[penpal-pool-freeze-review-request-721e04a.md](penpal-pool-freeze-review-request-721e04a.md)
- **评审提交**：`721e04a`
- **评审结论**：**A 全量接受**。

## 结论与依据

- `721e04a` 将 `LV_MEM_SIZE` 从 48K 扩至 64K；申请中的三段真机水位
  （2172B → 540B → 524B）、`pp_waitbox_show()` 的崩溃栈，以及仅创建空
  label 仍能复现的对照实验，共同充分支持“LVGL 内部池耗尽”这一根因。
- 64K 比实测临界状态多出约 16K 余量，而板级 RAM 预算为 327680B；申请
  记录的 RAM 占用从 50.1% 到 55.1%，与新增 16384B 一致。当前阶段不应把
  LVGL 对象分配器迁入 PSRAM：它是频繁、小块的 UI 分配路径，内部 SRAM 的
  延迟和确定性更合适。
- `pp_dbg_pool()` 调用的 `lv_mem_monitor()` 在本仓库 LVGL 8.3 实现中可用，
  三个采样点的频率低，可保留为现场观测。后续正式发布可再改为编译期开关，
  以避免长期串口噪声，但这不阻塞本修复。
- `config/lv_conf.h` 由 `-include` 强制引入而未进入 SCons 依赖图；修改后
  clean build 并用 RAM 涨幅核验的规则准确且必要。
- 诊断批次没有混入本次提交；“worker 写 SPIFFS 转 UI 线程”与内存池根因
  独立，作为单独变更再评审是正确边界。

## 验证

- 已静态核对完整 `721e04a` diff、LVGL 内存监视实现、板级 RAM 预算和相关
  构建文档。
- `git diff --check 721e04a^ 721e04a` 通过。
- 当前环境未安装 PlatformIO，未独立重新编译；申请中已有真机完整回归记录。

## 审批意见

- [x] A. **全量接受**
- [ ] B. 退回修订
- [ ] C. 部分接受
