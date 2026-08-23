# 评审申请书：PenPal 实现评审 C 结论三项修复（2×P1 + P2）

- **申请人**：Claude（pda2 现场调试，配合用户实测）
- **申请日期**：2026-08-22
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个）：
  - `acc3893` — `penpal: close Codex C findings (memset UB, entry auto-sync, worker cap)`
- **背景**：Codex 结果
  `wifi-config-keyboard-review-result-b48f584..5329383-codex.md`（2026-08-21
  到达）**C 部分接受**，2×P1 + 1×P2。三项均先对照源码核实属实（含
  `ui_scr_mrg.c` 的 register 期 create 调用序）再修复；修复中发现并顺带
  关闭一个同族 busy 泄漏路径（见 §1.2-c）。
- **硬件**：T-Deck-Pro HD-V2（V1.1，25-09-15 批次，4G/A7682E，COM5，
  **已连接、已烧录**）

---

## 1. 变更明细

### 1.1 P1-a：`memset` 作用于含 `std::string` 的结构（UB）

- `ui_penpal_write.cpp` `ppw_payload_build()`：`memset(out,…)` →
  `*out = pp_send_req_t{};`（原样清零会破坏 subject/content 两个 string）
- `penpal_api.cpp` `penpal_polish()`：→ `*out = pp_polish_t{};`（improved）
- 同文件 `penpal_correction()`/`penpal_tips()`：本身是纯 POD（memset
  原本安全），统一改为同款值初始化——规则收敛为"**任何 `pp_*_t` 一律
  值初始化，不再 memset**"
- 全量复查：penpal 相关文件仅剩 4 处 memset——`s_pages`/`s_status_lab`
  （`lv_obj_t*` 指针数组）与 `pp = pp_state_t()` 赋值路径均安全；letters
  拷贝是逐元素赋值（string 走拷贝构造），pals/rows/topics 的 memcpy 均
  为 POD 数组

### 1.2 P1-b：create 期自动同步被 entry 的 gen++ 作废 + busy 永久泄漏

- **根因核实**：`scr_mgr_register`（`ui_scr_mrg.c:33`）在**注册时（开机）**
  即调 `create()`；`pp_create()` 里发起的自动同步携带 gen G，用户进入
  屏幕时 `pp_entry()` gen++ → G+1，结果必判 stale 丢弃，且 stale 分支
  不释放 busy
- **修复**：自动同步移入 `pp_entry()`，在 `gen++`、`s_pp_active=true`
  **之后**发起（请求携带将被消费的代次）；`s_pp_autosynced` 一次访问
  只跑一次，`pp_destroy()` 复位；**Cfg 保存成功也复位**（下次进入用新
  配置同步）。`pp_create()` 只建控件
- **同族路径顺带修复（自查新增）**：后台 SEND 期间用户退出屏幕——
  `pp_exit()` 收起 waitbox 但保留 busy，结果因 `!s_pp_active` 被 stale
  丢弃后 busy 永久卡死。现在 stale 丢弃分支在"被丢弃结果持有
  `s_pp_busy_gen`"时一并清 busy（该请求已无其他消费者）
- 修复后时序：entry(gen++) → auto-sync(gen=G+1) → 结果到达 → gen 匹配
  → 正常消费 + 释放 busy

### 1.3 P2：READ Close 后僵尸任务并发堆积（最长 180s×N）

- 采纳 Codex 给的方案 B（并发上限，不做可中止传输——HTTPClient 中止
  需改 penpal_api 传输层，收益/侵入比不划算，登记为后续可选）
- `s_pp_inflight` 原子计数（`__atomic_*`，双核 RMW；在 `xTaskCreate`
  **之前**递增，防任务先于递增结束导致负数），任务在结果入队后递减
- `pp_start()` 非链式请求在 `inflight >= 2` 时拒绝（"wait - previous
  request closing"）——语义：最多 1 个被取消僵尸 + 1 个新请求；HOME
  串行链式腿（chained）属同一单飞，跳过闸门
- 效果：连续 Close→重试最多堆积 2 个 worker（16KiB 栈），不再无界增长

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 编译 | ✅ | `pio run -e pda2` SUCCESS（RAM 49.9%，无新警告） |
| 烧录 | ✅ | COM5，hash verified |
| 开机冒烟 | ✅ | 串口 40s：无 panic/重启/backtrace；boot 期无 PenPal 活动（create 不再发起请求，符合设计） |
| P1-a 静态复查 | ✅ | penpal 文件 memset 全量清点（§1.1） |
| **修复路径真机回归** | ⏸ | ①已配置状态进入 PenPal：自动同步完成、busy 正常释放（可再 Sync）②Send→Close(后台)→退出屏→重进：不卡 busy ③LLM 请求 Close 后立刻重试×3：第 3 次见 "wait - previous request closing"，之后自行恢复 ④Cfg 保存→退出重进：自动同步用新配置 |

## 3. 遗留项（简要）

- 可中止传输（HTTP 层 cancel）登记为可选后续，当前以并发上限替代
- 原申请 `b48f584..5329383` 的 §7 真机回归清单继续顺延（待用户实测，
  与本表 §2 四项合并执行）
- 待结果申请清点：`c8f62f3`、`71fa528..a58a73c`、`141942d` + 本份
  （`b48f584..5329383` 已出结果并闭合）

## 4. 回滚方案

```bash
git revert acc3893
```

## 5. 申请审批事项

- [ ] **A. 全量接受**
- [ ] **B. 退回修订** — 具体修订意见：________________
- [ ] **C. 部分接受** — 注明保留/回退项：________________

**审批人**（手写或电子签名）：________________
**审批日期**：________________
