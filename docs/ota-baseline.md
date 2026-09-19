# OTA Baseline（真机基线数据）

> 设计文档 `ota-update-design.md` §8 验证矩阵的实测数据落点。
> 台架环境：`pda2_ota_bench`（`-DOTA_ALLOW_PLAIN_HTTP`，仅隔离 LAN）+
> `ota_test/http_server.py`；签名：`scripts/ota_sign.py` + 私钥
> `ota_signing_key.pem`（gitignored，备份见 §锚点）。

## 状态速览

| 项 | 状态 | 数据 |
|---|---|---|
| 端到端正向（矩阵④） | ✅ 2026-09-20 机 #1（COM5） | 见 §端到端记录 |
| D1 校准打点 | ⬜ 待测 | §校准打点 |
| D2 回滚矩阵 | ⬜ 待测（台架机） | §回滚矩阵 |
| D3 异常用例组 | ⬜ 待测 | §异常用例 |
| 生产 https OTA | ⬜ 等服务端（证书续期 + `/firmware/` 端点） | — |

## 端到端记录（矩阵④，2026-09-20）

- 设备：机 #1（`28:37:2f:91:2c:20`，COM5），固件 v1.27（台架版）→ 同包
  `v1.27-ota1-http`（seq 1）。
- 链路：`http://192.168.3.186:8443`（PC，ThreadingHTTPServer）→ 设备
  （HONOR-60 热点，192.168.3.10）。OTA_URL 经 env_writer 写入 /env.cfg。
- 结果：Check 显示 `v1.27-ota1-http / 2834 KB / seq 1` → Confirm →
  下载 100% → Enter 重启 → `mark valid ok` → 再 Check =
  `already up to date (seq 1)`。
- 过程中发现并修复：台架宏名漂移 P2（`OTA_ALLOW_PLAINTEXT` vs
  `OTA_ALLOW_PLAIN_HTTP`，issue_list §35 OTA-1）；信任锚换根
  （§35 OTA-2）。

## 锚点

- 当前 key_id：**`325BDA1A`**（2026-09-20 换根；旧钥 `B09E5CE2` 丢失，
  已随 USB 重刷作废）。
- 私钥：`ota_signing_key.pem`（gitignored）+ 备份
  `backups/ota_signing_key-325BDA1A.pem`（gitignored）。
  **待办：用户侧离线副本**。
- 换根仅 USB（设计 §2.2）：gen → 锚点入库 → USB 重刷全部设备 → 旧签
  manifest 全部作废。

## D1 校准打点（待测）

目的：验证 boot WDT 窗口 T=60s 与各段耗时假设。方法：串口时间戳采集
两台机各 3 次：

- `boot → setup 入口`、`setup 各段（EPD / SPIFFS / WiFi init）`、
  `首帧 disp_flush`、`loop 首圈（= mark valid 时刻）`；
- `_busy_timeout`（ESP-IDF WDT）实测值；
- 通过标准：`mark valid` 时刻 < T/2（≥2× 裕量），否则调 T（factory.ino
  boot WDT 窗口）。

数据：⬜

## D2 回滚矩阵（待测，台架机先做）

自毁固件变体（每次一种）× 期望全部自动回滚旧版：

| # | 变体 | 期望 |
|---|---|---|
| a | setup 忙等死循环 | WDT 复位 → 回滚 |
| b | 让出/喂狗但显示栈死 | 自证窗口超时 → 回滚 |
| c | 自证点（mark valid）之前崩溃 | 复位 → 回滚 |
| d | 自证点之后崩溃 | **不回滚**（已 VALID，边界） |
| e | 下载阶段驱动失败 | abort → 原版不动 |

记录：每次的 reset 来源（rst:0x…）、otadata 状态链、回滚后版本。

## D3 异常用例（待测）

- 覆盖层吸收：下载中按 Back/任意键/触摸 → 无 UI 副作用，任务继续；
- 断网 45s：下载中拔 AP → abort + 报错 → 同 seq 重试成功；
- 低电安全阀：下载中低电锁存 → 不关机；超 10min 窗 → 强制放行；
- 签名否定：大写 hex sha256 / 前导零 seq / 错锚验签 / 篡改任一字段
  → 全部拒绝（Check 阶段报验签失败，不进入下载）；
- Content-Length 缺失 / 404 → 拒。
