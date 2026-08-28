# 评审申请书：PenPal 点击死机根治——LVGL 池扩容 48K→64K

- **申请人**：Claude（pda2 现场调试，配合用户真机 bisect 三轮）
- **申请日期**：2026-08-29
- **关联分支**：`HD-V2-250915`
- **关联 commit**：
  - `721e04a` — `lvgl: enlarge LV_MEM pool 48K -> 64K - PenPal click-freeze root cause`（`config/lv_conf.h` 池扩容 + `ui_penpal.cpp` `pp_dbg_pool()` 水位观测点）
- **关联改动**：1 个 commit，2 个文件（+23 / −1 行）
- **关联文档**：[`docs/issue_list.md`](../issue_list.md) §15（完整定位链与证据）、
  [`docs/build-and-code-structure.md`](../build-and-code-structure.md) §5 坑 7、
  [`CHANGELOG.md`](../../CHANGELOG.md) 2026-08-29 条目
- **硬件**：T-Deck-Pro HD-V2（COM5，已烧录 `721e04a` 等效固件）

---

## 1. 申请事由

进入 PenPal 后点击任何链接（Sync / 邮件行 / topic pick）死机或重启，
2026-08-28 起多轮真机上报，无法正常使用。经三轮设备 bisect 定位为
**LVGL 48K 内存池耗尽**，`f8d73f6`（邮件行表头 +10 个 label）只是压垮
骆驼的最后一根稻草——1269bf7 时代池余量已只剩 ~1K（偶发 topic 死机同源）。

## 2. 定位证据链（详见 issue_list §15）

1. `1269bf7` 整刷实测稳定；`f8d73f6` 一点即崩，panic 回溯：
   `pp_waitbox_show → lv_btn_create → lv_obj_mark_layout_as_dirty`
   （LoadProhibited 0x22，ELF 哈希校验后 addr2line 解码）。
2. Step B 探针（1269bf7 + 10 个只创建不写文本的空 label，渲染逻辑不动）
   同样崩 → 排除渲染逻辑。
3. 探针 `pp_dbg_pool()` 水位：`create` 剩 2172B → `cached` 剩 540B →
   点击 `wbshow` 剩 **524B**——等待框（box+2 label+btn+文本）需要数百
   字节，分配失败。诊断构建中崩溃点漂移（`lv_mem_buf_get` 挂死 /
   layout-dirty panic）均为池耗尽的不同表现，非独立 bug。

## 3. 修复内容

- `LV_MEM_SIZE` `(48U * 1024U)` → `(64U * 1024U)`，附根因注释；
  app RAM 50.1% → 55.1%（327KB RAM 预算内），flash 不变。
- `pp_dbg_pool()` 观测点进主线（create / cached / wbshow 三处，
  `[PD]` 串口行），余量 ~17K，防再次逼近临界而无感。
- 真机复测：邮件列表 + Sync + 开信 + topic pick 全稳定（用户确认）。

## 4. 请评审重点

1. **池尺寸选择**：64K 是否合理？有无更优做法（如 LV_MEM_CUSTOM 接
   PSRAM/heap_caps）需要在此硬件约束下权衡的？
2. **观测点噪声**：`pp_dbg_pool()` 三处打印的频率（每次进 penpal ~2 行 +
   每次网络动作 1 行）是否可接受，还是应降级为条件触发？
3. **教训条目**：`config/lv_conf.h` 的 `-include` 无依赖跟踪坑（build 文档
   坑 7）表述是否准确、规则是否足够？
4. 诊断批次（心跳任务/栈扫描/面包屑，含 SPIFFS-to-UI 线程缓存写入改动）
   随根因落定**整体弃置**未合入——是否认可？（其中"worker 写 SPIFFS 移到
   UI 线程"的改动如评审认为仍有价值，可另行走正式评审。）
