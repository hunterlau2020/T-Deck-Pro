# 评审申请：mainline（学习主线）设备端设计稿 v1 —— L1 设计评审（G-开工）

- **申请人**：ZCode（GLM 代理）；**申请日期**：2026-09-28
- **关联 commit**：`4b16209` — `docs: mainline design v1 - device-side learning-spine app (L1 design draft, pending review)`
- **关联文档**：[`docs/mainline-design.md`](../mainline-design.md)（v1 设计稿，评审对象全文）
- **评审依据**：`docs/review_guide.md` v1.3 **设计稿口径**——§0-§4、
  **§2.5 收口（强制）**、§6、§9【D】条目、§10；结论 = **L1 是否达标 +
  A/B/C**（设计稿语义，§2.5.2），无需 CODE/HW 证据
- **申请门禁**：**G-开工**（通过即开始 `ui_mainline` 编码，目标 v1.30）
- **服务端权威**：`E:/works/english_learning_system`（FastAPI）——
  `docs/FSD_LM_DESIGN.md` v4.2（学习主线产品语义）、
  `backend/app/api/v1/endpoints/plan.py|sessions.py|onboarding.py`、
  `backend/scripts/remote_api_demo.py`（端点契约权威）。后端 v1.20.0
  起主线课开课/作答/结课对 API-Key learn 通道开放。

---

## 1. 申请事由

设备侧新增"学习主线"应用（消费后端 StudyPlan/主线课链路：今日推荐 →
开课 → 逐题作答 → 结课得分）。本稿为**首版设计**，寻求 L1 评审以过
G-开工门禁；实现合同缺口已集中列设计稿 §10，不阻塞开工。

## 2. 设计稿要点（详见关联文档）

- **范围（v1）**：今日推荐页（`GET /plan/next` + `/plan` 进度）→
  开课（`/lessons/{id}/start`）→ 逐题作答（认读 1-4 选项 / 拼写键盘 /
  输出文本）→ 结课（`/sessions/{id}/finish`，RESULT 页）。纯复习课
  （`/reviews/lesson/start`）v1 直接支持。口语题 v1 一律 `skipped=true`
  （Round 2 接 MiniMax ASR）。
- **定位边界**：设备 = 学习执行端；计划生成/切换/配额为网页
  Bearer-only，设备只读并引导。
- **架构**：三页 HIDDEN 切换（HOME/SESSION/RESULT）+ `ml_*` 单飞
  worker 四件套（契约 §1 补行）+ `penpal_api` 六端点封装（pp_request
  learn 通道）。
- **承重不变量 HW-MAINLINE-1**：`prompt.word`/`correct_option` 为服务
  端参考答案，解析层直接丢弃、永不进设备结构体；判分完全信任服务端
  submit 响应；渲染白名单 = task_type/segment/options/scene_words。
- **明确不做（v1）**：计划管理写面、本地持久化、定级测、ASR。

## 3. 评审焦点（邀请攻击，按优先级）

1. **HW-MAINLINE-1 完备性**：是否存在任何渲染/日志/错误路径把
   `word`/`correct_option` 带上屏？白名单字段集是否充分（如
   `prompt.scene_words` 对输出题是否也算半答案）？
2. **会话状态机的 409 路径**：`plan_superseded`（网页切计划作废旧
   会话）与 `missing_item_ids`（缺项跳回）的组合下，线性推进 UI 是否
   存在卡死/重复提交洞？
3. **范围边界**：设备=消费端的定位是否漏掉了必须设备侧处理的写面
   （如定级测 learn 可调却不做，是否导致新用户在设备上死路）？
4. **纯复习课纳入 v1**：是否违反 §2.5 收口（可推迟而未推迟）？
5. **“每日”语义**：设备不本地判日、全信服务端——时钟未同步场景是否
   有残留坑？

## 4. 开发者自评（最没把握 ≤3）

1. `prompt` 各 `task_type` 的显示契约未逐型冻结（实采
   `remote_api_demo` 校准 + 未知类型降级 skipped 兜底）——兜底是否
   足够。
2. 课项超 `PP_ML_ITEMS_MAX=20` 直接拒绝开课——真实主线课的项数分布
   我未统计，若常态超限则 v1 不可用。
3. `recommend=review` 且 `due=0` 的边缘渲染（done 语义）纯纸面推演。

## 5. 审批（设计稿口径）

- [ ] **L1 DOC-ALIGNED 达标**（G-开工门槛）
- [ ] A. 接受，据此开工
- [ ] B. 退回重构（附必须重开理由）
- [ ] C. 部分接受（实现合同 P2 列清单绑 G-合入，不倒逼新设计版）
