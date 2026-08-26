# 设计方案：PenPal AI Provider 与 AI Config 关联

## 1. 背景与目标

`AI Config` 屏已经管理了一套 OpenAI 兼容的 provider 配置（base URL / model / API key），并通过 Test/Save 机制保证可用性。`PenPal` 的 `Cfg` 页原本只配置 PenPal 服务端的地址和 key，没有独立的“AI API”配置。

为了避免再建一套并行的 AI endpoint 配置，PenPal 直接复用 AI Config 中已保存的 provider：

- 取消 PenPal 中独立的 AI endpoint / key 输入。
- 在 PenPal Cfg 中以下拉形式列出 AI Config 里已配置好的 provider。
- PenPal 仅保存“我使用哪个 provider”这一引用（provider name），不重复保存 base/model/key。
- 保持 PenPal 服务端 base/key 配置不变（PenPal 服务本身仍需认证）。
- Fix / Polish / Tips 调用时把选中的 provider/model 附加到请求，让服务端可以按指定模型调用 LLM。

## 2. 数据模型

### 2.1 AI Config 侧（复用现有）

Provider 注册表现在集中在 `openai_api.cpp`：

| name | label | base_url | model |
|---|---|---|---|
| `openrouter` | OpenRouter | `https://openrouter.ai/api/v1` | `deepseek/deepseek-v4-flash-0731` |
| `deepseek` | DeepSeek | `https://api.deepseek.com/v1` | `deepseek-v4-flash` |
| `minimax` | MiniMax | `https://api.minimaxi.com/v1` | `MiniMax-M3` |
| `qwen` | Qwen | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `qwen3.7-plus` |
| `tencent` | Tencent | `https://tokenhub.tencentmaas.com/v1` | `hy3` |

NVS namespace `"ai"`：

- 双槽保存当前激活的 base/model/key（`base.0/1`、`model.0/1`、`key.0/1`、`active`）。
- 每个 provider 的独立 key：`key.<name>`（例如 `key.openrouter`、`key.deepseek`）。
- env.cfg / `config_keys.h` 兜底链保持不变。

### 2.2 PenPal 侧新增

NVS namespace `"penpal"` 新增：

| Key | 类型 | 含义 |
|---|---|---|
| `ai_provider` | String | 选中的 provider 名称，如 `"openrouter"`；空字符串表示 `custom` |

保留原有的 `base`、`key`（PenPal 服务端地址与认证 key）。PenPal 不再单独保存 `ai_model`；model 由 AI Config 解析得到。

## 3. 架构

```text
PenPal Cfg 页
    │  下拉选择 AI Provider
    │  （只读显示 label / model / key 是否存在）
    ▼
openai_api: ai_provider_enum() / ai_provider_get()
    │  读取 AI Config 的 provider 注册表 + active slot + key.<name> + env.cfg
    ▼
PenPal 保存 ai_provider 名到 NVS "penpal:ai_provider"
    │
    ▼  调用 Fix/Polish/Tips 时（UI 线程解析）
pp_task_req_t 携带 ai_provider + ai_model
    │
    ▼  worker 线程请求 PenPal Server
penpal_api: 在 URL 中附带 ?provider=...&model=...
    │
    ▼  PenPal Server 按指定 provider/model 调用 LLM
```

### 3.1 新增/修改的 API

`openai_api.h` 中声明：

```cpp
typedef struct {
    const char *name;     /* 内部 id，如 "openrouter" */
    const char *label;    /* 显示标签，如 "OpenRouter" */
    const char *base_url; /* endpoint base */
    const char *model;    /* 默认 model id */
} ai_provider_info_t;

int  ai_provider_count(void);
bool ai_provider_enum(int idx, ai_provider_info_t *out);
int  ai_provider_find(const char *name);
bool ai_provider_get(const char *name,
                     char *base, int base_len,
                     char *model, int model_len,
                     char *key, int key_len);
```

`ai_provider_get()` 的 key 解析顺序：

1. 如果 AI Config 的 active slot 的 `base` 与注册表中的 `base_url` 匹配，说明用户已用 AI Config 配置并保存过该 provider，直接使用 active slot 的 `base/model/key`（允许用户自定义 model）。
2. 否则读 NVS `"ai"` 中的 `key.<name>`。
3. 否则读 `/env.cfg` 的 `<NAME>_KEY`。
4. 否则 `openrouter` 可回退到 `AI_KEY_DEFAULT_DEV`。
5. 否则 key 为空。

实现放在 `openai_api.cpp`，只读访问 Preferences，不修改 `"ai"` namespace。

`penpal_api.h` 中新增：

```cpp
void penpal_load_ai_provider(char *name, int name_len);
bool penpal_save_ai_provider(const char *name);
```

并把 `penpal_correction / penpal_polish / penpal_tips` 的签名扩展为：

```cpp
bool penpal_correction(const char *base, const char *key, int email_id,
                       const char *ai_provider, const char *ai_model,
                       pp_fix_t *out, string *err);
```

### 3.2 PenPal Cfg 页改造

`PP_PAGE_CFG`（`ui_penpal.cpp`）保留：

- **Server URL** + **Server Key**：PenPal 服务仍需要。
- **AI Provider 下拉**：从 `ai_provider_enum()` 构建选项，最后一项为 `custom`。
- **状态行显示当前选中 provider 的 `label/model` 与 key 是否存在**（2026-08-26
  评审订正，Codex/Claude P2：原实现从 NVS 读已保存值，Save 前状态行不跟随
  下拉选择；现按 `s_cfg_provider_idx` 即时预览，重进屏时下拉已从 NVS 同步，
  显示的自然是已保存状态）。
- **保存失败分区报告**（同轮订正）：Server 与 provider 两步 NVS 写非原子，
  部分失败时状态行明确显示 `server saved; AI provider save failed`，
  不再笼统报 `save failed`。

键盘映射：

- `\t`（Alt+Enter）：在 Server URL / Server Key / AI Provider 下拉之间循环焦点。
- 在 provider 下拉上按 `+/-` 切换选项。
- `\b`：删除当前输入框字符；为空时返回 HOME。
- 其他字符输入到 Server URL / Server Key。

保存逻辑同时写入 PenPal 服务端配置和选中的 provider：

```cpp
bool ok = penpal_save_config(base, key);
if (ok) {
    const char *provider_name = "";
    ai_provider_info_t p;
    if (ai_provider_enum(s_cfg_provider_idx, &p)) provider_name = p.name;
    ok = penpal_save_ai_provider(provider_name);
}
```

### 3.3 运行时调用 Fix/Polish/Tips

在 UI 线程的 `pp_start()` 中解析 AI provider/model，然后写进任务快照 `pp_task_req_t`：

```cpp
char provider_name[32] = "";
penpal_load_ai_provider(provider_name, sizeof(provider_name));
if (provider_name[0] &&
    ai_provider_get(provider_name, ai_base, sizeof(ai_base),
                    ai_model, sizeof(ai_model), ai_key, sizeof(ai_key))) {
    rq->ai_provider = provider_name;
    rq->ai_model    = ai_model;
}
```

worker 线程调用时把 `ai_provider` / `ai_model` 作为 query 参数附加到 URL：

```
POST /api/v1/emails/<id>/correction?provider=openrouter&model=deepseek/deepseek-v4-flash-0731
```

如果 provider 为空（custom），则不附加参数，服务端使用自己的默认模型，保持向后兼容。

## 4. 迁移与兼容性

- 未设置 `penpal:ai_provider` 的老设备：默认行为不变（服务端自行选择模型）。
- AI Config 本身有 env.cfg / `config_keys.h` 兜底链；PenPal 不破坏这条链。
- 服务端需要支持 `provider` / `model` query 参数才能真正按所选模型执行；不支持时请求仍可成功，只是忽略参数。

## 5. 改动文件清单

| 文件 | 改动 |
|---|---|
| `examples/pda2/openai_api.h` | 新增 `ai_provider_info_t` 与 `ai_provider_count/enum/find/get` 声明 |
| `examples/pda2/openai_api.cpp` | 集中式 provider 注册表与只读查询实现 |
| `examples/pda2/ui_ai_cfg.cpp` | 改用 `openai_api` 的 provider 接口，移除本地 `s_providers[]` |
| `examples/pda2/ui_penpal.cpp` | CFG 页增加 provider 下拉、保存/加载逻辑、状态显示 |
| `examples/pda2/ui_penpal.h` | `pp_task_req_t` 增加 `ai_provider` / `ai_model` |
| `examples/pda2/penpal_api.h` | 新增 `penpal_load/save_ai_provider`；扩展 LLM 接口签名 |
| `examples/pda2/penpal_api.cpp` | 实现 provider 存取；Fix/Polish/Tips URL 附带 provider/model |
| `docs/penpal-design.md` | 更新 CFG 页说明与配置链 |

## 6. 风险与注意事项

1. **循环依赖**：`penpal_api.cpp` 不直接依赖 `openai_api.h`；解析在 UI 线程完成后再传入 worker，保持两层分离。
2. **NVS 只读并发**：`ai_provider_get()` 与 AI Config 的 Save 都访问 `"ai"` namespace。所有调用仍在 UI 线程，符合现有 Preferences 使用约定。
3. **URL 编码**：目前 provider/model 直接拼接到 query 字符串；如果未来 model id 包含需要编码的字符，应补充 `url_encode`。
4. **EPD 刷新**：provider 下拉以紧凑单行显示，键盘用 `+/-` 切换，避免在电子墨水屏上展开大面积列表。

## 7. 验收标准

> 标注说明：`[x]` = 代码已实现；真机项以 TODO / 申请书 §验证状态 的 ⏸ 为准。

- [x] AI Config 中 Save 过的 provider 能在 PenPal Cfg 下拉中列出。
- [x] 选择 provider 后，PenPal 仅保存 provider 名，不重复保存 base/model/key。
- [x] PenPal 原有的服务端 base/key 仍可独立编辑保存。
- [x] Fix/Polish/Tips 调用能附带所选 provider/model 信息。
- [x] `pio run -e pda2` 编译通过。
- [ ] 真机：Cfg 下拉显示/保存后状态行/Fix/Polish/Tips 参数被服务端接收（⏸ 待烧录回归）。
