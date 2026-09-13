# 设计复审结果：OTA 固件远程升级 V2（Codex）

- **评审日期**：2026-09-13
- **评审文件**：[ota-update-design.md](../ota-update-design.md)
- **评审提交**：`be6a52c`
- **评审结论**：**C 退回修订**。V1 的 TLS 隔离与签名清单 P1 已闭合；以下
  3 项 P1 关闭前不得实施或发布 OTA。

## 已接受的 V2 修订

- OTA 直接配置 Mozilla CA bundle、不继承 AI Config 的 `tls_insecure`，且
  生产版拒绝 `http://`，关闭了 V1 的传输认证缺口。
- 清单强制签名、`size` 与 SHA-256，且签名覆盖 version/url/size/sha256；
  使用预编译 mbedTLS 支持的 ECDSA P-256 可以接受，无需为 Ed25519 引入
  第三方密码库。
- 弃用 `HTTPUpdate`、使用受应用控制的流式写入和哈希校验，是正确方向。

## P1：页面代次取消不能解除 OTA 写 flash 的互斥

§3.2 规定取消为 `gen+1 + busy=false`，同时规定任务不能读 UI 状态、下载和
写槽不可中止。这样旧任务仍可能占用全局 `Update`/flash，而页面退出或重新
进入后 busy 已清，可启动第二个任务；两个任务会并发写 flash，或者旧任务在
UI 已显示取消后仍完成 `Update.end()` 并切换 boot partition。`gen` 只能防
止迟到结果覆盖 UI，不能作为破坏性写入的互斥机制。

**要求**：建立跨页面、跨代次的全局 OTA 写入锁，只有任务实际执行
`Update.abort()` 或 `Update.end()` 并退出后才释放；离页不得启动第二个 OTA
任务。取消令牌须在写入前、写入循环及最终 commit 前检查；若取消，abort 且
不切换 boot partition。UI 代次仅用于展示结果，不能释放硬件操作锁。

## P1：无复位保证时，死循环不会“自动回滚”

V2 的验证用例要求一个“无 WDT 挂载前”的 `setup()` 死循环自动回滚。实际
OTA 状态机是在**下一次重启**时才把未确认的 `PENDING_VERIFY` 标为
`ABORTED` 并回滚；若该死循环未触发 reset，设备只会一直冻结。因而“setup
无 while(1)”不足以构成恢复保障，§8 的测试也无法按文字所述观察自动回滚。

**要求**：为自证窗口指定并真机验证必定导致复位的 watchdog/健康超时，且在
自证前启用；或者把该场景明确为需要用户人工复位。验证须分别覆盖 setup 与
loop 死锁，并记录 reset 来源和 pending → aborted → previous-valid 的状态。

## P1：OTA 信任根不能置于 gitignored 的 secrets 文件

§2.1 把设备侧 ECDSA 公钥放入 gitignored `config_keys.h`。公钥没有保密需求，
却是决定哪些远程固件可执行的信任根；让它随每台构建机的本地文件变化，会使
发布不可复现，也无法审核固件实际固化了哪个信任根。

**要求**：将公钥、key id 和编码格式放入受版本控制的
`ota_trust_anchor.h`（或等价文件），发布脚本校验其指纹。只有签名私钥应留在
gitignored 的本地存储。

## P2：建议补足协议和发布基线

- 将展示给用户的 `notes` 纳入签名对象；否则攻击者可篡改升级说明来诱导用户
  接受一个虽签名正确但不符合说明的固件。
- 增加签名的单调 release counter，并默认拒绝回放/降级；特殊开发降级应走
  显式、受限流程，而不是仅凭“版本字符串不同”。
- 将分区表和 bootloader 的版本、SHA-256 基线写进受版本控制的清单。整片
  `backups/` 可以继续忽略，但不能是唯一的兼容性依据。

## 验证说明

- 已复核 V2 对 V1 的 TLS、签名和回滚修订，以及现有全局 TLS 状态、board
  分区引用和异步 IPC 契约。
- ESP-IDF 的官方 OTA 状态机明确：未确认镜像在**下一次 boot**才会由
  `PENDING_VERIFY` 转为 aborted 并回滚；此结论用于本次死循环 P1。

## 审批意见

- [ ] A. 全量接受
- [ ] B. 部分接受
- [x] C. **退回修订**
