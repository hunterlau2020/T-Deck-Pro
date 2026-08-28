# 评审申请书：CA bundle 补根（deepseek/minimax X509 校验失败修复）

- **申请人**：Claude（用户真机反馈驱动）
- **申请日期**：2026-08-28
- **关联分支**：`HD-V2-250915`
- **关联 commit**（本轮 1 个）：
  - `78239c5` — `http: add Amazon Root CA 1 + USERTrust RSA to the CA bundle`
- **背景**（真机现象）：遮蔽轮烧录后用户在 AI Config 执行 Test 报
  `X509 - Certificate verification failed`（Trust 开关 OFF / CA 校验模式）。
  NTP 无问题——时间不同步会走 `http_ensure_time` 的独立错误文案
  （"Time not synced - retry after NTP"），该错误来自
  `WiFiClientSecure::lastError`，即证书链校验失败。
- **硬件**：T-Deck-Pro HD-V2（COM5，`78239c5` 已分块烧录，开机冒烟通过）

---

## 1. 根因（PC 侧 openssl s_client 实测各 provider 链）

固件 `CA_BUNDLE` 原有 6 根：ISRG X1/YR、DigiCert G2、GlobalSign R3、
GTS R4、Sectigo R46。实测（与设备同一网络）：

| 端点 | 服务端链 | 原 bundle 可验？ |
|---|---|---|
| openrouter.ai | WE1 → GTS R4（含自签根） | ✅ |
| api.deepseek.com | Amazon RSA 2048 M01 → Amazon Root CA 1（Starfield 交叉签） | ❌ 缺锚点 |
| api.minimaxi.com | WoTrus DV → USERTrust RSA（Comodo 交叉签） | ❌ 缺锚点 |
| dashscope.aliyuncs.com | GlobalSign GCC R46 → Root R46（R3 交叉签） | ✅（经 R3） |
| tokenhub.tencentmaas.com | DigiCert Secure Site OV G2 → DigiCert G2 | ✅ |

即：deepseek / minimax 两个 provider 在 CA 校验模式下必然 X509 失败。

## 2. 修复

- 追加两个**自签根**：Amazon Root CA 1（官方
  amazontrust.com/repository/AmazonRootCA1.pem）+ USERTrust RSA
  Certification Authority（curl.se Mozilla bundle 提取）；均有效至 2038。
- mbedTLS 信任锚按 subject+公钥匹配：服务端发送的交叉签中间证书可由
  bundle 内同 subject 同 key 的自签根锚定（与既有 GlobalSign R46→R3、
  GTS R4 跨签先例同机制）。

## 3. 验证状态

| 项目 | 状态 | 证据 |
|---|---|---|
| 证书解析 | ✅ | `scripts/ca_bundle_check.py` PASS（8/8 根解析通过） |
| 链验证（PC 模拟设备行为） | ✅ | 用**固件 bundle 原文**提取的 8 根做 `openssl verify -untrusted <服务端链>`：deepseek / minimax / openrouter 三链全 OK |
| 编译 | ✅ | `pio run -e pda2` SUCCESS |
| 烧录 | ✅ | 分块烧录 8/8（本次全部一次成功），hard_reset 后开机冒烟：slot 0 自动重连、EPD 刷新、无 panic |
| 真机 Test | ⏸ 待用户实测 | deepseek / minimax 各跑一次 Test（Trust OFF）应通过 |

## 4. 回滚方案

- `git revert 78239c5`（纯增量两段 PEM；无逻辑改动）。
- 临时规避（不改代码）：AI Config 右上 Trust 开关切 ON（设备级
  `setInsecure`，§7.4 已注明影响全部 HTTPS 消费者——不建议长期使用）。

## 5. 审批事项

- A 全量接受 / B 退回修订 / C 部分接受。
