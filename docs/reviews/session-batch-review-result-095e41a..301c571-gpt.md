# 评审结论：修复轮核销 + v1.0 缓存批次（095e41a..301c571）

- **评审人**：GPT（Codex）
- **评审日期**：2026-09-17
- **评审申请**：[session-batch-review-request-095e41a..301c571.md](session-batch-review-request-095e41a..301c571.md)
- **评审范围**：`2c86eca..301c571`，含首末四个提交（`095e41a`、`22e3b87`、`110802a`、`301c571`）与 11 个文件。
- **评审基线**：评审时 HEAD=`4a3e0a0`；申请末端之后仅有申请文件与刷写纪律文档变更，未将它们混入本批代码结论。
- **依据**：[review_guide.md](../review_guide.md) v1.3，代码评审 A/B/C 口径。

## 结论

**C（部分接受）；当前不通过 G-合入。**

上轮的 rollback P1 以及语音上下文、OTA 单飞、队列所有权、OTA TLS 隔离、自动休眠五项 P2，已按静态反例核销。此次新增的 Whoami NVS 缓存仍有一条静默的旧账号结果回写路径，且会持久化到重启后，属于本批必须修复的 P1。另有两项 P2 应登记台账并绑定 G-合入。

## 阻断项

### P1 — 旧账号的在飞 Profile 响应可在配置切换后重新写入缓存

- **证据**：`wa_start()` 将旧 base/key 复制到在飞请求，但 `wa_req_t` 与 `wa_msg_t` 均未携带配置代次；`s_wa_gen` 只在 entry 递增且从未参与判断。[ui_whoami.cpp](../../examples/pda2/ui_whoami.cpp) 第 126–136、294–355、358–370、835–855 行。
- **反例（SIM）**：以账号 A 点 `Refresh` → 在请求未返回时切到 Cfg，将 URL/key 改为账号 B 并 `Save` → 保存回调清除 A 的缓存和 RAM 副本 → A 的 worker 返回成功 → `wa_consume()` 无条件把 A 的 `m->prof` 写入 `s_profile` 与 NVS。重启后，B 配置仍会显示 A 的 `cached` 资料。
- **影响与定级**：跨账号资料的完整性/异步生命周期违约，需正常的两步 UI 操作；无错误提示且结果持久化。**B/2/静默 → P1**（`review_guide.md` §4.2）。`async_ipc_contract.md` §2 的代次与迟到结果规则也给出同一修复方向。
- **Required action**：为请求和结果携带独立的配置 epoch（Cfg 保存时递增），消费时仅在 epoch 匹配时渲染/写缓存；不匹配则仅释放结果。不能只用页面 entry 的代次，因为配置可在同一次访问中改变。修复后须完成“Refresh A → Save B → A 响应到达 → 重启”的真机回归。

## 应修项

### P2 — OTA 在 AI TLS 绕过开启时跳过 NTP 同步，却仍强制 CA 校验

- **证据**：两条 OTA HTTPS 路径固定 `setCACertBundle(CA_BUNDLE_MOZILLA)`，已不再调用 `http_apply_tls()`；但它们仍以全局 `http_get_tls_mode() != HTTP_TLS_INSECURE` 决定是否 `http_ensure_time()`。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 312–330、431–439 行。
- **反例（SIM）**：开启 AI 的 “Trust self-signed” → 设备冷启动且尚未同步时间 → OTA Check 跳过 NTP → 仍以 CA 校验发起 HTTPS，证书有效期校验可失败，UI 只得到 `connect failed (manifest)`。
- **影响与定级**：OTA Check 在一组可达配置下静默地失去应有前置条件，功能失败但不写 flash。**C/2/有声 → P2**。
- **Required action**：OTA 的 HTTPS 分支应始终单独执行 `http_ensure_time(5000)`，不得读取 AI 的 TLS 模式；以“Trust self-signed 已开、冷启动、未同步 NTP”的真机用例验证。

### P2 — OTA 与 Whoami 仍未登记异步 IPC 契约

- **证据**：契约适用表仍只有 WiFi Test、Time Sync、AI Test、AI Chat 四行。[async_ipc_contract.md](../async_ipc_contract.md) 第 13–18 行；本批申请也明确承认 P2-6 未闭合。
- **影响与定级**：新增 HTTP worker 的所有权、代次、排水与页面生命周期没有权威合同，已经造成上列 Whoami stale-result 路径难以被约束。**B/2/静默 → P1** 的根因已由上项阻断；本条作为文档/合同补齐任务按 **P2** 跟踪，不重复计作第二个阻断。
- **Required action**：在修复 P1 同批补列 Whoami，并补 OTA 的指针结果通道、单飞、取消、队列排水和消费者所有权；若不能同批，先在 `docs/issue_list.md` 以明确 G-合入门禁登记。

## 已核销/通过的范围

- **回滚 P1**：`verifyRollbackLater()` override 已存在，且 loop 的首帧自证点仍调用 `esp_ota_mark_app_valid_cancel_rollback()`。[factory.ino](../../examples/pda2/factory.ino) 第 653–660、881–896 行。静态路径闭合；PENDING_VERIFY→回滚仍是待真机矩阵，不能由当前 VALID 态启动记录替代。
- **语音上下文**：每个存储轮次现在输出标准 `user`/`assistant` 两条消息，并按整轮限制数量。[ui_voice_ai.cpp](../../examples/pda2/ui_voice_ai.cpp) 第 312–369 行。
- **OTA 单飞与结果所有权**：创建任务前 claim、创建失败回滚；覆盖前取回并释放旧结果指针。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 266–285、551–613 行。
- **OTA TLS 隔离**：manifest 与固件下载都固定 Mozilla CA bundle，明文仅可由默认关闭的编译期宏开启。[ota_update.cpp](../../examples/pda2/ota_update.cpp) 第 325–330、436–439 行。
- **版本号与布局**：`fw_version_string()` 的固定缓冲对目前的 `v1.0 build yyyy-mm-dd` 格式足够；三个显示调用均返回同一字符串。[fw_version.h](../../examples/pda2/fw_version.h) 第 13–19 行；[ui_deckpro_port.cpp](../../examples/pda2/ui_deckpro_port.cpp) 第 175–197 行。
- **刷写脚本**：逐块成功判据、失败中止和可选整段回读的控制流一致；`--help` 可运行。[flash_verified.py](../../scripts/flash_verified.py) 第 39–110 行。

## 区间文件归属

| 文件 | 结论 |
|---|---|
| `examples/pda2/factory.ino` | 评：rollback override 与版本显示；静态核销，回滚 HW 待测。 |
| `examples/pda2/ota_update.cpp` | 评：三项既有 P2 修复有效；发现 NTP/TLS P2。 |
| `examples/pda2/ui_voice_ai.cpp` | 评：上下文 role 修复有效。 |
| `examples/pda2/ui_whoami.cpp` | 评：发现缓存 stale-result P1。 |
| `examples/pda2/fw_version.h`、`ui_deckpro_port.cpp` | 评：版本串接口与显示一致。 |
| `scripts/flash_verified.py` | 评：静态控制流与 CLI 帮助通过；未接真实设备。 |
| `CHANGELOG.md`、`TODO.md`、`docs/issue_list.md`、`docs/ota-update-design.md` | 评：事实/门禁表述与代码交叉核对；IPC 文档缺口尚未在这些记录中闭合。 |

## 验证状态与边界

- `git diff --check 2c86eca 301c571`：**VERIFIED**（通过）。
- `python3 scripts/flash_verified.py --help`：**VERIFIED**（通过）。
- 编译、烧录、开机、OTA 写槽/PENDING_VERIFY 回滚、缓存竞态与 UI 布局：**CLAIM_ONLY / PARTIALLY_VERIFIED**。本机未发现 `pio`/`platformio` 命令，且无可操作目标设备，未独立重跑申请中的编译和真机声明。
