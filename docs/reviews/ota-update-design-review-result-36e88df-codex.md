# 设计评审结果：OTA 固件远程升级（Codex）

- **评审日期**：2026-09-13
- **评审文件**：[ota-update-design.md](../ota-update-design.md)
- **评审提交**：`36e88df`
- **评审结论**：**C 退回修订**。以下 P1 全部闭合前不得将方案用于生产设备。

## P1：OTA 不能复用可被 AI 配置关闭验证的 TLS 策略

设计要求 OTA 下载调用 `http_apply_tls(client)`，但该函数会读取设备级
`tls_insecure` 状态；AI Config 的“Trust self-signed”一旦启用，HTTPS OTA
就会执行 `client.setInsecure()`。攻击者只需处于网络路径上，即可替换清单
或固件并取得任意代码执行。

**要求**：OTA 另设只允许 CA 校验的 TLS 配置，绝不可继承
`HTTP_TLS_INSECURE`；生产构建必须拒绝 manifest 与 bin URL 的 `http://`。
若确有实验室明文需求，使用默认关闭、不可由 `/env.cfg` 打开的编译期开关，
并在 UI 中醒目标识。

## P1：未认证的清单不能作为远程刷写授权

设计将 SHA-256 视为可选，并把 Ed25519 签名推迟到二期。这意味着 OTA 服务
器、CDN 或其发布凭据被攻陷时，攻击者可以同步篡改清单、URL、固件与哈希；
HTTPS 只保护传输路径，不能认证已被控制的发布端。

**要求**：v1 必须把固化在设备内的 Ed25519 公钥用于验签，签名覆盖
`version`、`url`、`size` 和 `sha256`；这四个字段均应为必填。下载时同时
强制长度上限、分区容量检查与 SHA-256 匹配。单独添加、但未签名的哈希仅能
发现偶发损坏，不能解决供应链攻击。

## P1：A/B 与回滚前提尚不可复现或验收

仓库的 board 定义只引用框架包中的 `default_16MB.csv`；仓库未版本化实际的
分区表、bootloader 或启用 rollback 的 sdkconfig。因而设计中“预编译
bootloader 已启用 rollback”的关键前提既不能从当前源码复建，也不能保证
USB 初始烧录与实际量产机一致。现有 `factory.ino` 也还没有自证调用。

**要求**：将所用分区表和 bootloader 产物纳入受控发布流程，记录其版本与
SHA-256；在两台实际设备读回并核验分区和 bootloader。实现
`esp_ota_mark_app_valid_cancel_rollback()` 后，先刷入一个故意不自证的测试
固件，真实验证 pending → rollback，才可发布首个 OTA。

## 可接受的设计方向

- Settings 手动触发、两次确认、低电量拒绝，以及成功后由用户确认重启，均
  合理。
- 自证不依赖 WiFi 成功是正确选择；否则网络故障会让健康固件错误回滚。
- Arduino 侧采用 `HTTPUpdate` 可行，但不替代独立的发布认证、分区验证和
  回滚真机测试。

## 验证说明

- 已核对 OTA 设计、`http_apply_tls()` 的全局不安全分支、AI 配置对该分支的
  持久化设置、当前 board 定义和 `factory.ino` 初始化路径。
- 当前环境未安装 PlatformIO，无法直接读取框架包的 `default_16MB.csv` 或
  预编译 bootloader；这正是上述可复现性 P1 的证据之一。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 部分接受
- [x] C. **退回修订**
