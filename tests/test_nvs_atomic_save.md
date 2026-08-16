# AI 配置原子保存 —— 伪测试 / 失败注入用例

> 目的（评审要求：`wifi-config-keyboard-review-result-eecebda..ceade9c.md` 主 §1.9）：
> 用可读的伪测试描述 `openai_save_config` 双槽原子写在各失败点的预期行为，
> 作为真机 / 单测（unity）实现的规格。目标固件接口：`examples/pda2/openai_api.cpp`。

## 被测函数

```c
bool openai_save_config(const char *base, const char *model, const char *key,
                        const char **err);
```

## 状态机

- NVS namespace `ai`；字段 `base.<slot>` `model.<slot>` `key.<slot>`（slot ∈ {0,1}）；
  单一 `active` 键（UChar）指向活动槽。
- 保存 = 暂存到非活动槽（3 次 putString）→ 读回校验 → 1 次 putUChar("active") 提交。

## 用例

| # | 场景（注入） | 预期行为 | UI 表现 |
|---|---|---|---|
| 1 | 正常保存 | 全部写入非活动槽、校验通过、active 翻转 | `Saved` |
| 2 | 首次保存（无旧配置） | active 键缺失时按 slot 0 活动处理，因此首次保存写 **slot 1**，active 翻转为 1 | `Saved`；重启后 load 读 slot 1 |
| 3 | NVS 满：第 1 次 putString 返回 0 | 中止；非活动槽无半成品；active 不变；err="NVS write failed" | msgbox `Save failed:\nNVS write failed`；旧配置完好 |
| 4 | NVS 满：第 3 次 putString 返回 0 | 同上（槽内可能残留部分键，但 active 未翻转，load 不可见） | 同上 |
| 5 | 读回校验不一致（写入被截断） | 中止；err="NVS write failed" | 同上 |
| 6 | putUChar("active") 返回 0（提交失败） | 旧槽仍为 active；err="NVS commit failed"；新槽数据残留但不可见 | `Save failed:\nNVS commit failed`；旧配置可用 |
| 7 | 提交后立即掉电 | 重启后 active 要么指向旧槽（未提交完成）要么新槽（已提交）——**任何时刻都无混合配置** | 两端点配置二者其一 |
| 8 | 连续保存 3 次 | active 在 0/1 间翻转；每次 load 得最近一次完整配置 | — |
| 9 | 旧固件平键迁移 | 槽均未初始化（slot 0/1 无任何键）且旧平键存在时：load 读平键；第一次 save 写非活动槽 + active 翻转，此后不再走平键路径 | — |
| 10 | 保存空 Base | 允许：槽**只要任一键存在即算已初始化**（isKey 判断），load 原样读回空 Base，**不**回退默认值；仅当槽完全未初始化时才回退平键/默认值 | `Saved`；重进 AI Config 显示空 Base |

## 注入方式建议

- ESP32 无 NVS 注入接口；建议在 PC 侧用 `Preferences` 的 mock（ArduinoFake 或手写
  固定容量的 KV 表，容量可调）跑用例 3-6；用例 7 用断电开关实测。
- 通过标准：全部用例通过；任何失败点下 `openai_load_config` 返回的
  三元组都等于**某一次成功保存**的完整三元组。
