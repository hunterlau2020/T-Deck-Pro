# 评审申请书：配置界面秘密遮蔽显示（PenPal key / AI key / WiFi pass）

- **申请人**：Claude（用户需求驱动的安全加固轮）
- **申请日期**：2026-08-28
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 4 个，按模块拆分，基于 `2d00f3f`）：
  - `e8379b3` — `ui: shared secret-mask helper (display-only middle third)`
  - `44acbc7` — `penpal: cfg key box shows the middle third masked`
  - `fca9bc8` — `ai: cfg key box shows the middle third masked`
  - `3a92971` — `wifi: cfg pass box shows the middle third masked (>=4 stars)`
- **背景**（用户需求）：三个配置界面的输入框此前把存储的秘密**完整明文**渲染
  在 EPD 上（PenPal Cfg 的 Server Key、AI Config 的 key、Wifi Config 的
  pass），旁窥可直接读走。要求：中间 1/3 用星号替代（WiFi pass 至少 4 个
  星号），首尾保留以便辨认存储的是哪个值。
- **基线说明**：本轮曾在旧基线 `aecddc3` 上完成首版（旧 id
  `eb9d569..6f51edd`，未 push）；push 时发现远端已演进（WiFi 多槽位、
  PenPal/AI 共享 `ai_provider_*`、PenPal Cfg provider 下拉），整轮在
  `2d00f3f` 上**重新落地**，并针对新结构补齐了 WiFi 槽位切换路径。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，COM5）

---

## 1. 设计：影子缓冲 + 未改动即原值

三个界面均为"textarea 持有草稿"模型——切换字段 / Save / Connect / Test /
**WiFi 槽位切换**时会把框内文本读走。单纯改成遮蔽显示会让**星号串**被写进
NVS 槽位或发进 HTTP 请求。配套机制（三处一致）：

1. **影子缓冲**：真实值保存在 `_real` 影子缓冲（`s_cfg_key_real` /
   `s_ai_key_real` / `wifi_pass_real`），框内只显示
   `secret_mask_middle(real)`。遮蔽串与原值**等长**（星号数=遮蔽数）。
2. **未改动即原值**：每条读框路径先比较框内文本是否仍等于遮蔽串——相等
   则沿用影子里的真实值：
   - PenPal：`pp_cfg_save_cb`
   - AI：`ai_cfg_sync_draft`（Test/Save/provider 切换全部经由它）
   - WiFi：`wifi_cfg_sync_draft` **和 `wifi_cfg_set_slot` 的出向槽自动保存**
     （后者绕过 sync_draft 直接读两框，是本轮基线演进新增的路径）
3. **首次编辑清空重输**：遮蔽态下框内第一个按键（含退格）先清空再输入
   （retype 模式），杜绝"真实字符混星号"的中间态草稿。AI 侧清空同样
   触发 `s_ai_test_passed = false`（Test 过期语义一致）。
4. **成功后重新遮蔽**：PenPal server 半边保存成功、AI Save 成功、WiFi
   连接+保存成功与触摸 Save 按钮成功路径均按新存值重新遮蔽；WiFi
   **连接失败不重遮**（保留用户刚输入的明文便于改错重试）。
5. **Clear 全清**：WiFi Clear 同时清影子缓冲与遮蔽标志。
6. **槽位切换重遮蔽**：`wifi_cfg_set_slot` 载入新槽后调用
   `wifi_pass_remask()`（影子=新槽真实密码）。

## 2. 变更明细

### 2.1 `e8379b3` ui — 共享遮蔽助手 `secret_mask.h`

- `secret_mask_middle(src, dst, dst_size, min_mask)`：遮蔽数
  `m = max(len/3, min_mask)`，封顶 `len`；首尾保留（不均匀时首段短）。
  纯函数、`static inline`、无状态。
- 已知取舍：遮蔽串**与原值等长**，会暴露秘密长度（用户明确要求的展示
  格式即"中间 1/3 星号"，长度泄露属接受项——评审如认为应填充到固定
  宽度可提）。

### 2.2 `44acbc7` penpal — Cfg Server-Key 框遮蔽

- `pp_cfg_prefill()`：key 框显示遮蔽串，真实值入 `s_cfg_key_real[17]`。
- `pp_cfg_save_cb()`：未改动（框==遮蔽串）→ 存真实值；server 半边保存
  成功后**原地重遮蔽**（不 re-prefill，保留 2d00f3f 引入的分项保存
  上报 "saved / server saved; AI provider save failed / save failed"）。
- `pp_cfg_key()`：KEY 焦点下 `\b`/可见字符在遮蔽态先清空。BASE 与
  PROVIDER 焦点（`\t` 三态循环、`+/-` 换 provider）不受影响。

### 2.3 `fca9bc8` ai — Config key 框遮蔽

- `ai_key_show_masked()`：统一入口（create / `ai_provider_apply` 两分支 /
  Save 成功重遮蔽）；`ai_key` 草稿缓冲**始终持有真实值**（供 Test 快照
  与 Save 校验）。
- `ai_cfg_sync_draft()`：field==2 且框==遮蔽串 → 草稿保持真实值。
- 键盘 `\b`/字符分支：遮蔽态先清空并置 Test 过期；"custom" provider
  清箱路径也过 `ai_key_show_masked()`。
- 与新的共享 `ai_provider_*` API 兼容：provider key 经
  `ai_provider_get()` 载入 `ai_key` 后统一遮蔽。

### 2.4 `3a92971` wifi — Config pass 框遮蔽（≥4 星）

- `wifi_pass_remask()`：create 载入后、槽位切换后、连接/保存成功后调用。
- `wifi_cfg_sync_draft()` / `wifi_cfg_set_slot()` 出向槽保存：框==遮蔽串
  → 沿用 `wifi_pass_real`（Enter=Connect、触摸 Connect、`+/-` 换槽均
  无需重输）。
- 键盘 `\b`/字符分支：遮蔽态先清空；空框 `\b` 仍回 SSID 字段。
- `wifi_clear_btn_cb()`：追加清 `wifi_pass_real` + 标志。
- 遮蔽数 `max(len/3, 4)` 封顶 `len`：len≤4 完全遮蔽，len=5 露末 1 字符。

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 算法核对 | ✅ | 同逻辑 Python 副本对 len=2/3/4/5/6/7/8/9/16/73 逐例核对输出（16 位 key → `abcde*****456789`；8 位 pass → `ab****45`；len≤4 → 全遮蔽） |
| 编译 | ✅ | `pio run -e pda2` SUCCESS（基于 2d00f3f + 本轮 4 commit，无新警告） |
| 烧录 | ✅（恢复后完成） | 首轮整刷在 ~21-29% 处 USB CDC 持续失联（pySerial `PermissionError 13`；降速/`usb_reset`/`--no-stub` 均无效，且失败重试已把 app 分区擦掉大半→设备一度无法启动）。改用**分块烧录**恢复：firmware.bin 切 8×256KB，逐块 `write_flash` + 失败幂等重试（2 块各重试 1 次成功），写入后设备正常启动进主菜单。方案沉淀至 `docs/build-and-code-structure.md` §5 坑 6 |
| 开机冒烟 | ✅ | 烧录后串口：I2C/EPD/触摸/键盘就绪进主菜单，无 panic；`[WiFi] Connecting slot 0 to HONOR-60...` 证明新基线多槽位固件在跑且 **NVS 槽位凭据完好**（中断烧录未伤配置分区，遮蔽轮从未写入星号的旁证） |
| ① PenPal Cfg | ⏸ 待用户实测 | Server Key 框显示遮蔽串；直接 Save 后服务器仍可用；首键清空重输；provider 切换后状态行 "key: set" 不受影响 |
| ② AI Config | ⏸ 待用户实测 | key 框遮蔽；不动 key 直接 Test 通过；provider 切换重遮蔽；改 key 首键清空 |
| ③ Wifi Config | ⏸ 待用户实测 | pass 框遮蔽；不动 pass 直接 Connect 成功；`+/-` 换槽后重遮蔽且原槽密码不丢；Clear 后空框；改 pass 首键清空 |

## 4. 回滚方案

- 4 个 commit 均为 UI 展示层加法式改动（新静态量 + 条件分支），无存储
  格式/协议/任务模型变更；`git revert e8379b3..3a92971` 即整轮回滚，
  NVS/槽位数据不受影响（本轮从未写入遮蔽串，见 §1.2）。
- 单模块回滚：仅 revert 对应 `.cpp` commit 并保留 `secret_mask.h`
  （未使用的 header 无副作用）。

## 5. 审批事项

- A 全量接受 / B 退回修订 / C 部分接受。
- 请评审人特别关注：读框路径清单是否完备（三处 UI 已 grep 全部
  `*_ta` 读写点；WiFi `wifi_cfg_set_slot` 直读双框是本轮新增覆盖点）。
