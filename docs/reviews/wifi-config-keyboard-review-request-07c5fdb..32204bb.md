# 评审申请书：TLS 信任切换为全量 Mozilla 根证书包

- **申请人**：Claude（用户决策驱动）
- **申请日期**：2026-08-28
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 2 个）：
  - `07c5fdb` — `scripts: full Mozilla CA-bundle generator + generated header`
  - `32204bb` — `http: switch TLS trust to the full Mozilla root bundle`
- **背景**：`78239c5` 手工补了 deepseek/minimax 两个根之后，用户问"证书过期/
  缺失能否硬件自处理，是否必须人工加头文件"。评估结论（已向用户报告）：
  过期校验由 mbedTLS 自动做；信任锚必须随固件更新（PKI 设计使然），但可一次
  换成全量 Mozilla 根包免去逐家手工维护。资源实测：flash +58KB（31.4%→32.0%）、
  heap 反而更省。用户拍板切换。
- **硬件**：T-Deck-Pro HD-V2（COM5，`32204bb` 已分块烧录 9/9，开机冒烟通过）

---

## 1. 变更明细

### 1.1 `07c5fdb` — 生成器 + 生成头

- `scripts/gen_ca_bundle.py`：curl.se Mozilla 信任库（121 根）→ esp_crt_bundle
  二进制格式（2B 数量 + 每根 [2B 名长][2B 钥长][主体 DER][SPKI DER]，按主体
  严格排序供 core 二分查找），产出 `ca_bundle_full.h`（55,587 B const 数组，
  带来源/日期/SHA-256 头注释）。
- 生成期自检：严格排序（二分查找前提）；五家 provider 锚点必须在源内
  （Amazon Root CA 1 / USERTrust RSA / GTS R4 / GlobalSign R3(OU) /
  DigiCert G2）；每把 SPKI 独立可解析（即 core 握手时做的事）。
- 回读校验（生成头反解）：格式游走无残尾、排序严格、SPKI 全部可解析、
  主体集合 == 源集合（121/121）。
- 刷新流程：下载新 cacert.pem → 重跑脚本 → 头文件与说明一起提交。

### 1.2 `32204bb` — http_apply_tls 切换

- `setCACert(8根PEM)` → `setCACertBundle(CA_BUNDLE_MOZILLA)`；删除原 PEM
  串（-277 行）与 `ca_bundle_check.py`（其检查对象消失，自检移交生成器）。
- 资源语义（读 core 源码确认 + 实测）：
  - flash：bundle 为 const 驻 flash，app 31.4%→32.0%；
  - heap：core 仅 `calloc(121×4B)` 指针索引；握手时主体名二分（~7 步
    memcmp）+ 仅解析命中根的公钥（~1KB 瞬时）。对比旧 `setCACert`：每次
    请求把全部 8 张完整证书解析进 heap 链——**切换后 RAM 净减**（实测
    50.1%，与切换前 50.0% 持平，因为索引按需分配）。
- Trust 开关（`setInsecure`）语义不变，仍为设备级逃生通道。

## 2. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 生成器自检 | ✅ | 121 根、排序严格、锚点齐、SPKI 可解析 |
| 生成头回读 | ✅ | 反解格式游走无残尾；主体集合 == 源（121/121） |
| 信任集链验证（PC） | ✅ | openssl verify 对 openrouter/deepseek/minimax/qwen/tencent 五链全 OK |
| 编译 | ✅ | `pio run -e pda2` SUCCESS（flash 32.0%，RAM 50.1%） |
| 烧录 | ✅ | 分块 9/9（全部一次成功），hard_reset 后 slot 0 自动重连、EPD 刷新、无 panic |
| 真机 AI Test | ⏸ 待用户实测 | deepseek / minimax（Trust OFF）各跑一次；openrouter 回归一次 |

## 3. 遗留项

- Mozilla 信任库随时间增删根（每年数个）；刷新按 1.1 流程随固件升级执行，
  无需逐 provider 操作。根过期由 mbedTLS 链校验自然拒绝（安全方向失败）。
- core 3.x 若未来升级，可换内置 `setUseCACertBundle()` 并删自管 bundle——
  届时另开一轮。

## 4. 回滚方案

- `git revert 32204bb 07c5fdb`（有序）即回到 8 根 PEM；或仅 revert `32204bb`
  保留生成器与头文件备用。设备侧无任何持久状态变化（bundle 只读）。

## 5. 审批事项

- A 全量接受 / B 退回修订 / C 部分接受。
