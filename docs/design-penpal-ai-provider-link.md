# 设计方案：PenPal AI Provider 与 AI Config 关联

## 1. 背景与目标

当前 `AI Config` 屏已经管理了一套 OpenAI 兼容的 provider 配置（base URL / model / API key），并通过 Test/Save 机制保证可用性。`PenPal` 的 `Cfg` 页目前只配置 PenPal 服务端的地址和 key，没有独立的“AI API”配置。

如果未来 PenPal 需要客户端指定 AI provider（例如让服务端按指定模型做 Fix/Polish/Tips，或在 PenPal 内直接调用 LLM），应避免再建一套并行的 AI endpoint 配置，而是**直接从 AI Config 中复用已保存的 provider**。

### 目标
- 取消 PenPal 中独立的 AI endpoint / key 输入。
- 在 PenPal Cfg 中以下拉形式列出 AI Config 里已配置/保存过的 provider。
- PenPal 仅保存“我使用哪个 provider”这一引用（provider name），不重复保存 base/model/key。
- 保持 PenPal 服务端 base/key 配置不变（PenPal 服务本身仍需认证）。

## 2. 数据模型

### 2.1 AI Config 侧（复用现有）

`ui_ai_cfg.cpp` 已经维护：

- `s_providers[]`：provider 名称、默认 base、默认 model、env.cfg key 名。
- NVS namespace `"ai"`：
  - 双槽保存当前激活的 base/model/key（`base.0/1`、`model.0/1`、`key.0/1`、`active`）。
  - 每个 provider 的独立 key：`key.<name>`（例如 `key.openrouter`、`key.deepseek`）。

需要在 `openai_api.h/.cpp` 暴露一组只读查询接口，让 PenPal 能安全读取（不破坏双槽/测试门控逻辑）。

### 2.2 PenPal 侧新增

NVS namespace `"penpal"` 新增：

| Key | 类型 | 含义 |
|---|---|---|
| `ai_provider` | String | 选中的 provider 名称，如 `"openrouter"`、`"deepseek"` |
| `ai_model`（可选） | String | 如果用户要覆盖默认 model，可保存 |

保留原有的 `base`、`key`（PenPal 服务端地址与认证 key）。

## 3. 推荐架构

```textnPenPal Cfg 页
    │  下拉选择 AI Provider
    │  （只读显示 base/model）
    ▼
openai_api: ai_provider_enum() / ai_provider_get()
    │  读取 AI Config 的 s_providers + key.<name> + active slot
    ▼
PenPal 保存 ai_provider 名到 NVS "penpal:ai_provider"
    │
    ▼  调用 Fix/Polish/Tips 时
penpal_api: 在请求中附带 model / provider 信息
    │
    ▼  PenPal Server 按指定 provider/model 调用 LLM
```

### 3.1 新增/修改的 API

在 `openai_api.h` 中声明：

```cpp
/* 返回 AI Config 中可供选择的 provider 数量（含 custom） */
int ai_provider_count(void);

/* 枚举第 idx 个 provider 的显示名、base、model（key 不出库） */
bool ai_provider_enum(int idx, const char **name,
                      const char **base, const char **model);

/* 按名称读取完整 provider 配置（含 key）。若该 provider 在 AI Config 中
 * 未保存过 key，返回 false，调用方应提示“先在 AI Config 中 Test & Save”。 */
bool ai_provider_get(const char *name,
                     char *base, int base_len,
                     char *model, int model_len,
                     char *key, int key_len);
```

实现放在 `openai_api.cpp`，只读访问 Preferences，不修改 `"ai"` namespace。

### 3.2 PenPal Cfg 页改造

当前 `PP_PAGE_CFG`（`ui_penpal.cpp` 约 929–1016 行）有两个输入框：Base URL、API key。

改造后保留：

- **Server URL** + **Server API key**：PenPal 服务仍需要。
- **AI Provider 下拉**：从 `ai_provider_enum()` 构建选项。
- 一个只读状态行，显示当前选中 provider 的 `base/model` 与 key 是否存在（例如 `"openrouter (key ok)"`）。

键盘映射：

- `\t`（Alt+Enter）：在 Server URL / Server key / AI Provider 下拉之间循环焦点。
- 在 provider 下拉上按 `+/-` 切换选项（与 AI Config 的 provider 下拉行为一致）。
- `Enter`：最后一个字段时保存。

保存逻辑：

```cpp
static void pp_cfg_save_cb(lv_event_t *e)
{
    const char *server_base = lv_textarea_get_text(s_cfg_server_base_ta);
    const char *server_key  = lv_textarea_get_text(s_cfg_server_key_ta);
    const char *ai_name     = s_providers_in_penpal[ai_provider_idx].name;

    bool ok = penpal_save_config(server_base, server_key);
    ok &= penpal_save_ai_provider(ai_name);
    pp_status_set(ok ? "saved" : "save failed");
}
```

### 3.3 运行时调用 Fix/Polish/Tips

在 `penpal_api.cpp` 的 `penpal_correction / penpal_polish / penpal_tips` 中：

1. 读取 `penpal:ai_provider` 和 `penpal:ai_model`。
2. 调用 `ai_provider_get()` 取得 model。
3. 在 HTTP 请求中附加信息，例如：
   - Query: `?model=deepseek/deepseek-v4-flash-0731`
   - 或 Header: `X-AI-Provider: openrouter`

服务端需要相应支持；若服务端不支持，客户端可忽略该字段，保持向后兼容。

### 3.4 另一种形态：客户端直接调用 LLM

如果未来 PenPal 的 AI 功能改为客户端直接调用 OpenAI 接口（不经过 PenPal Server），则：

- `pp_task_req_t` 中增加 `ai_provider_name`。
- 新增任务类型（如 `PP_RES_AI_REPLY`），在任务线程调用 `openai_chat_multi()`。
- 复用 `ai_provider_get()` 获取 base/model/key。

本设计方案同时覆盖这两种形态，核心都是“PenPal 只保存 provider 名，配置细节由 AI Config 维护”。

## 4. 迁移与兼容性

- 未设置 `penpal:ai_provider` 的老设备：默认行为不变（服务端自行选择模型）。
- `penpal:ai_model` 为空时：使用 AI Config 中该 provider 的默认 model。
- AI Config 本身有 env.cfg / config_keys.h 兜底链；PenPal 不破坏这条链。

## 5. 改动文件清单

| 文件 | 改动 |
|---|---|
| `examples/pda2/openai_api.h` | 新增 `ai_provider_count/enum/get` 声明 |
| `examples/pda2/openai_api.cpp` | 实现只读 provider 枚举/读取 |
| `examples/pda2/ui_penpal.cpp` | CFG 页增加 provider 下拉、保存/加载逻辑 |
| `examples/pda2/ui_penpal.h` | 如需暴露状态可新增声明 |
| `examples/pda2/penpal_api.cpp` | Fix/Polish/Tips 请求附带 model/provider |
| `docs/penpal-design.md` | 更新 CFG 页说明与配置链 |

## 6. 风险与注意事项

1. **循环依赖**：`penpal_api.cpp` 目前不依赖 `openai_api.h`；引入后是单向依赖（PenPal → AI），可以接受。
2. **NVS 只读并发**：`ai_provider_get()` 与 AI Config 的 Save 都访问 `"ai"` namespace。所有调用仍在 UI 线程，符合现有 Preferences 使用约定。
3. **服务端改造**：如果希望 Fix/Polish/Tips 真正按所选模型执行，需要 PenPal Server 支持对应 query/header。
4. **EPD 刷新**：下拉展开会占用较大区域，建议在 E-Paper 上使用“单行显示 + +/- 切换”或只显示 provider 名的紧凑下拉。

## 7. 验收标准

- [ ] AI Config 中 Save 过的 provider 能在 PenPal Cfg 下拉中列出。
- [ ] 选择 provider 后，PenPal 仅保存 provider 名，不重复保存 base/model/key。
- [ ] 未在 AI Config 中保存 key 的 provider，在 PenPal 中显示为不可用或提示去 AI Config 配置。
- [ ] PenPal 原有的服务端 base/key 仍可独立编辑保存。
- [ ] Fix/Polish/Tips 调用能附带所选 model/provider 信息。
