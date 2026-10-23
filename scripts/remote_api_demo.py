"""远程 API 演示 (2026-08-20 API 访问; 2026-09-18 (九十八) 重构: 按通道/功能分方法)。

只用一个 `X-API-Key` 请求头走完整闭环; 方法即分类 —— 硬件开发按通道取用,
`--channel penpal|learn|all` 可单跑一条通道 (通道 = API Key scope):

┌─ penpal 通道 (scope: penpal) ── run_penpal_channel ────────────────┐
│ ⓪ 我是谁        demo_profile                GET  /users/me/profile │
│ ①② 笔友与主题   demo_penpals_and_topics     GET  /pen-pals         │
│                                               GET  /topics/suggestions │
│ ③④ 写信+读线程  demo_send_and_read_thread   POST /emails (Idempotency-Key │
│     (含同键重放)                              + 同 key 重试重放)    │
│                                               GET  /emails?pen_pal_id&thread_root_id │
│ ⑤⑥ 纠错与润色   demo_letter_feedback        POST /emails/{id}/correction|polish │
│ ⑦⑧ 回信与 Tips  demo_reply_and_tips         POST /emails (锚定线程) │
│                                               POST /emails/{id}/tips │
│ ⑨ 同主题新线程  demo_thread_forks           POST /emails (裸标题) + mailbox 行 │
│ ⑩⑪ 信箱+残留    demo_mailbox_and_residual   GET  /emails/mailbox    │
│                                               GET  /emails?thread_root_id (无 pen_pal_id) │
└─────────────────────────────────────────────────────────────────────┘
┌─ learn 通道 (scope: learn) ──── run_learn_channel ──────────────────┐
│ ⑫ 词库浏览      demo_vocab_bank             GET  /words (cefr/q/theme 过滤) │
│     (九十八)                                  GET  /words/{id} 单词条     │
│                                               GET  /words/{id}/detail    │
│                                               GET  /words/{id}/mindmap   │
│ ⑫b 考试选卡     demo_exam_words              GET  /words/exams 考试名枚举 │
│     (九十九)                                  GET  /words?exam= 按考试名列词 │
│ ⑬ 抽认卡        demo_flashcards             GET  /reviews/card      │
│                                               POST /reviews/grade (幂等) │
│ ⑭ 错题本        demo_error_book             GET  /errors + /errors/stats │
│                                               POST /errors/{id}/drill (幂等) │
│ ⑮ 定级测·整卷   demo_level_test_paper       POST attempts → POST submit (权威)│
│ ⑯ 定级测·自适应 demo_level_test_adaptive    POST attempts → POST answers (自适应)│
│     (ESP32 参考)                                                        │
│ ⑰ 定级测·历史   demo_level_test_history     GET  /users/me/level-test/history │
│ ⑱ 入学状态      demo_onboarding_state       GET  /onboarding/state  │
│ ⑲ 计划总览      demo_plan_overview          GET  /plan + /plan/next │
│ ⑳ 目标切换预览  demo_plan_switch_preview    GET  /plan/switch-preview │
│     (含免修开关)                              + skip_below_level 对比   │
│ ⑳b 双向预览     demo_plan_preview_confirm   POST /plan/preview       │
│     (三百零七 UX-PREVIEW-1)                   顺推/倒推 + 健康度诊断    │
│ ⑳c 确认生成     demo_plan_preview_confirm   POST /plan/confirm       │
│     (含同键重放)                              (Idempotency-Key + token)│
│ ㉑ 纯复习课      demo_review_lesson          POST /reviews/lesson/start → finish │
│ ㉒ 主线课闭环    demo_mainline_lesson        POST /lessons/{id}/start → submit → finish │
└─────────────────────────────────────────────────────────────────────┘

线程模型 (2026-08-21 线程按首信分组):
- 每个线程由**首信**标识,锚点是 `EmailOut.thread_root_id` (首信即自身);
- 精确取线程: `GET /emails?pen_pal_id&thread_root_id`;
- `subject` 参数是**兼容通道** —— 跨线程合并同首信主题返回 (旧客户端);
- `Re:` 回信未带锚点时自动接同对方同主题的最近线程;**裸标题一律新线程**。

残留线程读取 (R9, 2026-08-22):
- `GET /emails` 的 `pen_pal_id` **可选** —— 缺省时须给 `thread_root_id`,
  按**参与者授权**读取 (线程首信收发双方须包含当前用户);
- 解绑笔友后信箱的残留行 (`pen_pal_id=null`) 用此通道"显示 + 只读"
  (客户端 HOME null 行);他人线程 403;响应 `pen_pal_id` 为 null。

幂等重试 (2026-08-21 (十八),ESP32 客户端语义):
- 创建型 POST 可带 `Idempotency-Key` 头 (客户端生成,惯例 32 位 hex):
  发信后结果未确认 (用户关掉等待框/超时/断连) 时**复用同一把 key 重试**,
  服务端按 (用户, key) 重放已存信件,不产生重复信;
- 重放响应 200 + `Idempotent-Replayed: true`,正文返回同一封信 (id 不变);
- 收到确认成功后 key 作废;草稿内容变化要换新 key;
- 状态推进接口 (/reviews/grade, /errors/{id}/drill) 走四元捕获重放,
  断网重试同键不重复推进 SM-2/重拼状态;
- v7 学习主线新增: /onboarding/daily-quota 与 /plan/regenerate|switch
  **必带**幂等键 (claim 先于业务 4xx, 同键重试重放首响);
- 不带该头的请求行为与旧版完全一致;
- Phase 2 计划确认 (UX-PREVIEW-1): POST /plan/confirm **必带** Idempotency-Key
  (缺失 400; 已完成同键重放直通首响, 绝不校验 Token/漂移 —— 与发信幂等同构)。

v7 学习主线 (2026-09-21 批 3 + Web 闭环; 2026-10-11 (二百七十) 主线课硬件全面开放):
- `GET /onboarding/state` · `GET /plan` · `GET /plan/next` ·
  `GET /plan/switch-preview` —— learn 双通道只读;
- `POST /reviews/lesson/start` 纯复习课 (不受帽/不占课序/不耗配额;
  毕业项抽查 ≤1 题混入热身最前; 响应带 included_count/due_total 双数字);
- `POST /lessons/{id}/start` 主线课 (learn 双通道开放, Idempotency-Key 幂等重放);
- **API key 的 submit/finish 放行主线课与纯复习会话**; 孤儿遗留会话 403 隔离;
- 跳过通道: submit `{"skipped": true}` (属主裁定: 跳过计 0 分 + 入错题本重开周期,
  SRS 调度零触碰),
  响应回带 skipped 标记; finish 响应带 xp_awarded/correct_items/
  total_items/skipped_items 真实数字 (跳过计 0 分, XP = 课分)。

用法:
    cd backend
    python scripts/remote_api_demo.py --key <你的10位APIKey>
    python scripts/remote_api_demo.py --key xxx --base http://127.0.0.1:8000
    python scripts/remote_api_demo.py --key xxx --channel learn     # 只跑学习通道 (词库/抽认卡/错题/定级测/学习主线)
    python scripts/remote_api_demo.py --key xxx --channel penpal    # 只跑笔友通道
    # 反向代理 (HTTPS) 环境 (DEPLOY_APACHE.md):
    python scripts/remote_api_demo.py --key xxx --base https://your-domain.example.com
    python scripts/remote_api_demo.py --key xxx --base https://your-domain.example.com         --gate-pin <闸门口令>          # §4b.7 静态闸门启用时必带
    python scripts/remote_api_demo.py --key xxx --base https://192.168.x.x         --insecure                     # 自签证书本地测试 (公网勿用)

Key 在网页端 个人资料页 → 🔑 API Key 创建 (仅显示一次);默认 scope 含
penpal,learn (单通道 key 只能跑对应 --channel)。
"""
from __future__ import annotations

import argparse
import json
import secrets
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import httpx  # noqa: E402


def _ensure_encodable_stdout() -> None:
    """(评审 claude P3-K, 同 (二十三)② start.py 修复) GBK 控制台无法编码
    emoji 时把 stdout 重包一层 errors='replace' —— emoji 降级为 '?' 而非
    UnicodeEncodeError 崩溃;UTF-8 环境 (含测试 capsys) 完全不变。"""
    import io

    enc = getattr(sys.stdout, "encoding", None) or "utf-8"
    try:
        "✅💡✦".encode(enc)
    except (LookupError, UnicodeEncodeError):
        sys.stdout = io.TextIOWrapper(
            sys.stdout.buffer, encoding=enc, errors="replace", line_buffering=True
        )


_ensure_encodable_stdout()


def new_idem_key() -> str:
    """32 位 hex 幂等键 —— 固件侧等价: ESP32 每次投递前生成并保存,
    结果未确认期间的重试复用同一把,确认成功后丢弃。"""
    return secrets.token_hex(16)


def step(n: int | str, title: str) -> None:
    print(f"\n{'=' * 62}\n步骤 {n}: {title}\n{'=' * 62}")


def channel_banner(name: str, scope: str) -> None:
    print(f"\n{'#' * 62}\n# 通道 {name} (API Key scope: {scope})\n{'#' * 62}")


# ══════════════════════════════════════════════════════════════════════
# penpal 通道 (scope: penpal) — run_penpal_channel
# ══════════════════════════════════════════════════════════════════════


def run_penpal_channel(client: "httpx.Client", wait_reply: int) -> None:
    """笔友通道全流程: 资料 → 笔友/主题 → 写信(幂等) → 纠错润色 →
    回信/Tips → 同主题新线程 → 信箱/残留线程。"""
    demo_profile(client)
    pal, subject, topic_id = demo_penpals_and_topics(client)
    email, root_id = demo_send_and_read_thread(client, pal, subject, topic_id)
    demo_letter_feedback(client, email)
    demo_reply_and_tips(client, pal, subject, root_id, wait_reply)
    demo_thread_forks(client, pal, subject, root_id)
    demo_mailbox_and_residual(client, root_id, email)


def demo_profile(client: "httpx.Client") -> dict:
    """⓪ 我是谁: GET /users/me/profile (仅属主自己的资料,只读)。"""
    step(0, "GET /api/v1/users/me/profile — 我是谁")
    r = client.get("/api/v1/users/me/profile")
    r.raise_for_status()
    me = r.json()
    print(
        f"  {me['name']} · {me['age_band']} · {me['level']}"
        f" · {me['city'] or '城市未填'} · 兴趣: {me['interests'] or '未填'}"
    )
    return me


def demo_penpals_and_topics(client: "httpx.Client") -> tuple[dict, str, int | None]:
    """①② 枚举笔友 + 按年龄段题库拉可选主题 (写信的两个前置)。"""
    step(1, "GET /api/v1/pen-pals — 列出笔友")
    r = client.get("/api/v1/pen-pals")
    r.raise_for_status()
    pals = r.json()
    for p in pals:
        print(f"  pen_pal_id={p['id']:<3} {'NPC' if p['is_npc'] else '用户'} {p['name']}")
    pal = next((p for p in pals if p["is_npc"]), pals[0] if pals else None)
    if pal is None:
        sys.exit("没有笔友 —— 请先在网页端添加一位 NPC 笔友再运行演示")
    print(f"  → 选定笔友: {pal['name']} (pen_pal_id={pal['id']})")

    step(2, "GET /api/v1/topics/suggestions — 可选写作主题 (按年龄段题库)")
    r = client.get("/api/v1/topics/suggestions")
    r.raise_for_status()
    topics = r.json()
    for t in topics[:5]:
        print(f"  topic_id={t['id']:<3} {t['title']} ({t.get('exam_tag') or '-'})")
    topic = topics[0] if topics else None
    if topic:
        print(f"  → 选定主题: {topic['title']} (topic_id={topic['id']})")
        print(f"    引导问题: {topic['guiding_questions']}")
    subject = topic["title"] if topic else "A letter from the remote API demo"
    return pal, subject, topic["id"] if topic else None


def demo_send_and_read_thread(
    client: "httpx.Client", pal: dict, subject: str, topic_id: int | None
) -> tuple[dict, int]:
    """③④ 写信 (关联主题 + Idempotency-Key, 含同键重放演示) + 按线程锚点读信。"""
    step(3, "POST /api/v1/emails — 写信并关联主题 (带 Idempotency-Key)")
    content = (
        "Dear friend, hello from the remote API demo! I want to tell you about "
        "my favourite animal. I like cats very much because they are quiet and "
        "soft. My cat Momo sleeps on my desk every day. What animal do you like?"
    )
    send_body = {
        "pen_pal_id": pal["id"],
        "subject": subject,
        "topic_id": topic_id,
        "content": content,
    }
    idem_key = new_idem_key()  # 一把 key 绑定一次投递
    r = client.post("/api/v1/emails", json=send_body,
                    headers={"Idempotency-Key": idem_key})
    r.raise_for_status()
    email = r.json()["email"]
    root_id = email["thread_root_id"]  # 新线程首信 = 本信 (2026-08-21)
    print(f"  已寄出 email_id={email['id']} topic_id={email['topic_id']} subject={subject!r}")
    print(f"  thread_root_id={root_id} (新线程首信;后续看信/回信都用这个锚点)")
    print(f"  reply_pending={r.json()['reply_pending']} (NPC 将在 ~60s 后回信)")
    print(f"  Idempotency-Key={idem_key}"
          f" → Idempotent-Replayed={r.headers.get('Idempotent-Replayed')} (首次,201 新建)")

    # ③b 模拟"结果未确认"的重试: 同一把 key 原样重发 → 服务端重放 -----------
    print("\n  --- 模拟 ESP32 结果未确认的重试 (同一把 key 原样重发) ---")
    r2 = client.post("/api/v1/emails", json=send_body,
                     headers={"Idempotency-Key": idem_key})
    r2.raise_for_status()
    replayed = r2.json()["email"]
    print(f"  HTTP {r2.status_code} Idempotent-Replayed={r2.headers.get('Idempotent-Replayed')}")
    print(f"  重放同一封信 email_id={replayed['id']} (= 首次的 {email['id']}) → 线程不会多出第二封")
    assert r2.headers.get("Idempotent-Replayed") == "true"
    assert replayed["id"] == email["id"]
    print("  (换新 key 或不带 key 的同内容请求才会新建一封信 —— 见步骤⑨)")

    step(4, "GET /api/v1/emails?pen_pal_id&thread_root_id — 读线程 (按首信锚点)")
    r = client.get("/api/v1/emails", params={
        "pen_pal_id": pal["id"], "thread_root_id": root_id,
    })
    r.raise_for_status()
    for e in r.json()["emails"]:
        who = "我" if e["sender_user_id"] else f"NPC {e['sender_name']}"
        print(f"  [{e['id']}] {who}: {e['content'][:60].replace(chr(10), ' ')}...")
    print("  (兼容通道: GET ?subject=<标题> 跨线程合并返回,旧客户端仍可用)")
    return email, root_id


def demo_letter_feedback(client: "httpx.Client", email: dict) -> None:
    """⑤⑥ AI 反馈: 纠错 (我的信) + 润色 (我的信)。"""
    step(5, f"POST /api/v1/emails/{email['id']}/correction — 纠错 (我写的信)")
    r = client.post(f"/api/v1/emails/{email['id']}/correction")
    r.raise_for_status()
    fb = r.json()
    print(f"  degraded={fb['degraded']}")
    for c in fb["corrections"]:
        print(f"  [{c['type']}] {c['from']!r} → {c['to']!r}")
        print(f"         {c['explanation']}")

    step(6, f"POST /api/v1/emails/{email['id']}/polish — 润色 (我写的信)")
    r = client.post(f"/api/v1/emails/{email['id']}/polish")
    r.raise_for_status()
    fb = r.json()
    print(f"  degraded={fb['degraded']}")
    print("  --- 润色稿 ---")
    for line in fb["improved_email"].splitlines():
        print(f"  {line}")
    for imp in fb.get("improvements", []):
        print(f"  ✦ {imp}")
    for cov in fb.get("topic_coverage", []):
        if cov["status"] != "not_applicable":
            mark = "✅" if cov["status"] == "answered_from_source" else "📝"
            print(f"  {mark} {cov['question']}")


def demo_reply_and_tips(
    client: "httpx.Client", pal: dict, subject: str,
    root_id: int, wait_reply: int,
) -> None:
    """⑦⑧ 回信 (锚定线程 + 新幂等键) + 可选: 等 NPC 回信后演示回信 Tips。"""
    step(7, "POST /api/v1/emails — 回信 (body 带 thread_root_id 锚点 + 幂等键)")
    reply_subject = f"Re: {subject}"
    r = client.post("/api/v1/emails", json={
        "pen_pal_id": pal["id"],
        "subject": reply_subject,
        "thread_root_id": root_id,  # 精确锚定;不带时 Re: 前缀也接最近同主题线程
        "content": (
            "Dear friend, thank you for reading my letter. I will write more "
            "about my weekend next time. Have a nice day!"
        ),
    }, headers={"Idempotency-Key": new_idem_key()})  # 每次投递一把新 key
    r.raise_for_status()
    reply = r.json()["email"]
    print(f"  已寄出回信 email_id={reply['id']} subject={reply_subject!r}")
    print(f"  Idempotent-Replayed={r.headers.get('Idempotent-Replayed')} (首次,201 新建)")
    assert reply["thread_root_id"] == root_id  # 回信仍在本线程

    step(8, "POST /api/v1/emails/{id}/tips — 回信 Tips (基于收到的信)")
    if wait_reply > 0:
        print(f"  等待 {wait_reply}s 让 NPC 回信 (延迟 ~60s)...")
        time.sleep(wait_reply)
        r = client.get("/api/v1/emails", params={
            "pen_pal_id": pal["id"], "thread_root_id": root_id,
        })
        incoming = [e for e in r.json()["emails"] if not e["sender_user_id"]]
        if not incoming:
            print("  (该线程还没有收到回信 —— Tips 需要基于收到的信,跳过)")
        else:
            inc = incoming[-1]
            print(f"  收到 NPC 回信 email_id={inc['id']},请求 Tips...")
            r = client.post(f"/api/v1/emails/{inc['id']}/tips")
            r.raise_for_status()
            tips = r.json()
            print(f"  degraded={tips['degraded']}")
            for t in tips["tips"]:
                print(f"  💡 {t}")
    else:
        print("  跳过 (加 --wait-reply 75 可等待 NPC 回信后演示;示例:")
        print('  curl -X POST -H "X-API-Key: <KEY>" http://127.0.0.1:8000/api/v1/emails/<来信ID>/tips)')


def demo_thread_forks(
    client: "httpx.Client", pal: dict, subject: str, root_id: int
) -> dict:
    """⑨ 同主题二次撰写 = 新线程 (线程按首信分组);信箱同主题各自成行。"""
    step(9, "同主题再写一封 → 新线程;信箱同主题各自成行")
    r = client.post("/api/v1/emails", json={
        "pen_pal_id": pal["id"],
        "subject": subject,  # 裸标题 (无 Re: 前缀,无锚点) → 开启新线程
        "content": (
            "Dear friend, hello again! This is a NEW conversation with the same "
            "title. The mailbox will show it as a separate thread."
        ),
    }, headers={"Idempotency-Key": new_idem_key()})  # 新投递 = 新 key → 正常新建
    r.raise_for_status()
    fresh = r.json()["email"]
    print(f"  POST /emails → email_id={fresh['id']} subject={subject!r}")
    print(f"  thread_root_id={fresh['thread_root_id']} (≠ 步骤③的 {root_id} → 独立线程)")
    print(f"  Idempotent-Replayed={r.headers.get('Idempotent-Replayed')} (新 key,201 新建)")

    r = client.get("/api/v1/emails/mailbox")
    r.raise_for_status()
    print("  GET /emails/mailbox — 同主题的线程行:")
    for row in r.json():
        if row["subject"] == subject:
            print(
                f"    thread_root_id={row['thread_root_id']:<4} {row['counterpart']}"
                f" · {row['count']} 封 · {row['state']}"
                + ("  ← 本轮新线程" if row["thread_root_id"] == fresh["thread_root_id"] else "")
            )
    return fresh


def demo_mailbox_and_residual(
    client: "httpx.Client", root_id: int, email: dict
) -> None:
    """⑩⑪ 信箱线程概览 + R9 残留线程读取 (仅凭 thread_root_id,无 pen_pal_id)。

    解绑笔友 (网页端) 后信箱残留行 pen_pal_id=null,客户端用此通道"显示 +
    只读";授权只看线程首信参与者 (与笔友是否解绑无关),故对任何自己的
    线程都可直接演示。他人线程 → 403。
    """
    step(10, "GET /api/v1/emails/mailbox — 信箱线程概览")
    r = client.get("/api/v1/emails/mailbox")
    r.raise_for_status()
    for row in r.json():
        print(f"  pen_pal={row['pen_pal_id']} {row['subject']!r} {row['state']} 未读{row['unread']}")

    step(11, "R9 — GET /api/v1/emails?thread_root_id (无 pen_pal_id)")
    r = client.get("/api/v1/emails", params={"thread_root_id": root_id})
    r.raise_for_status()
    residual = r.json()
    ids = [e["id"] for e in residual["emails"]]
    assert residual["pen_pal_id"] is None
    assert email["id"] in ids  # 与步骤④同一线程 (③ 的首信在内)
    print(f"  HTTP {r.status_code}  pen_pal_id={residual['pen_pal_id']}  emails={ids}")
    print("  (残留线程读取通道: 响应 pen_pal_id 为 null,客户端据此渲染只读形态;")
    print("   两参皆缺 → 400;他人线程 → 403)")


# ══════════════════════════════════════════════════════════════════════
# learn 通道 (scope: learn) — run_learn_channel
# ══════════════════════════════════════════════════════════════════════


def run_learn_channel(client: "httpx.Client") -> None:
    """学习通道全流程: 词库 → 考试选卡 → 抽认卡 → 错题本 → 定级测 (整卷/自适应/历史)
    → 入学状态 → 计划总览 → 目标切换预览 (含免修开关) → 双向预览与确认生成
    → 纯复习课闭环 → 主线课闭环 (v7 学习主线)。"""
    demo_vocab_bank(client)
    demo_exam_words(client)
    demo_flashcards(client)
    demo_error_book(client)
    demo_level_test_paper(client)
    demo_level_test_adaptive(client)
    demo_level_test_history(client)
    demo_onboarding_state(client)
    demo_plan_overview(client)
    demo_plan_switch_preview(client)
    demo_plan_preview_confirm(client)
    demo_review_lesson(client)
    demo_mainline_lesson(client)



def demo_vocab_bank(client: "httpx.Client") -> None:
    """⑫ 词库浏览 (九十八): 列表 (分页/cefr/q/theme 过滤) + 单词条 + 详情卡 +
    思维导图 —— 词库只读四端点全实调。

    只读端点, 2026-09-18 起接受 learn scope —— 硬件可离线包之外在线查词。
    """
    step(12, "GET /api/v1/words — 词库浏览 (列表 / 词条 / 详情卡 / 思维导图)")
    r = client.get("/api/v1/words", params={"limit": 5})
    r.raise_for_status()
    page = r.json()
    print(f"  词库共 {page['total']} 词 (分页 skip={page['skip']} limit={page['limit']}):")
    for w in page["items"]:
        print(f"    [{w['id']:>5}] {w['word']:<16} {w['cefr_level']:<6} {(w.get('meaning_zh') or '')[:24]}")
    if page["total"] == 0:
        print("  ⚠ 词库为空 —— 部署缺 seed_data/dicts/vocabd.business.db (启动日志'词库体检'有告警)")
        return

    # 搜索 + 级别过滤 (同端点参数化; q=/theme= 同理)
    first = page["items"][0]
    r = client.get("/api/v1/words", params={"cefr": first["cefr_level"], "limit": 3})
    r.raise_for_status()
    same_level = r.json()
    print(f"  过滤 cefr={first['cefr_level']} → {same_level['total']} 词"
          f" (如 {', '.join(w['word'] for w in same_level['items'][:3])})")

    # 单词条: GET /words/{id} (轻量字段集, 含 stage/exam/theme/scenario 标签)
    r = client.get(f"/api/v1/words/{first['id']}")
    r.raise_for_status()
    w = r.json()
    tag_bits = [f"考 {','.join(w['exam_tags'])}" if w.get("exam_tags") else "",
                f"场景 {','.join(w['scenario_tags'])}" if w.get("scenario_tags") else ""]
    tag_bits = [t for t in tag_bits if t]
    print(f"  GET /words/{first['id']} — {w['word']} {w.get('phonetic') or ''} "
          f"({w['cefr_level']} · {w.get('pos') or '-'})"
          + (f"  [{'; '.join(tag_bits)}]" if tag_bits else ""))

    # 详情卡: 词性分组释义 + 例句 + 词块 + 关联词
    r = client.get(f"/api/v1/words/{first['id']}/detail")
    r.raise_for_status()
    d = r.json()
    print(f"  详情卡 /words/{first['id']}/detail — {d['word']} {d.get('phonetic') or ''}")
    for g in d["senses"][:2]:
        s0 = g["senses"][0]
        print(f"    {g['pos']}: {s0.get('zh') or s0.get('en') or ''}")
    for e in d["examples"][:2]:
        print(f"    例: {e['en']} {e.get('zh') or ''}")
    for c in d["chunks"][:2]:
        print(f"    词块: {c['text']} ({c.get('zh') or ''})")
    for ag in d["assoc_groups"][:3]:
        print(f"    关联[{ag['type']}]: {', '.join(ag['words'][:4])}")

    # 思维导图: 中心词 + 按类型分组的关联分支 (单层 ≤10 节点)
    r = client.get(f"/api/v1/words/{first['id']}/mindmap")
    r.raise_for_status()
    mm = r.json()
    if mm["branches"]:
        parts = [f"{b['type']}: {', '.join(n['word'] for n in b['nodes'][:4])}"
                 for b in mm["branches"][:3]]
        print(f"  GET /words/{first['id']}/mindmap — {mm['center']['word']} → " + " | ".join(parts))
    else:
        print(f"  GET /words/{first['id']}/mindmap — {mm['center']['word']} (无关联分支)")


def demo_exam_words(client: "httpx.Client") -> None:
    """⑫b 考试选卡 (九十九): 枚举词库考试名 + 按考试名列词。

    硬件"考试词书"两步走: GET /words/exams 拿候选项渲染选卡 →
    GET /words?exam= 拉词 (skip/limit 翻页, 可叠 cefr=);
    中文考试名 (高考/中考...) 请求时按 URL 编码传递。
    """
    step("12b", "GET /api/v1/words/exams + ?exam= — 考试选卡 (枚举考试名 / 按考试名列词)")
    r = client.get("/api/v1/words/exams")
    r.raise_for_status()
    exams = r.json()["items"]
    print(f"  考试名 {len(exams)} 个 (词库共 {r.json()['total_words']} 词, 按词量降序):")
    for e in exams[:8]:
        print(f"    {e['exam']:<8} {e['count']:>5} 词")
    if len(exams) > 8:
        print(f"    ... 共 {len(exams)} 个")
    if not exams:
        print("  (词库无考试标签 —— 检查 seed 导入)")
        return
    pick = next((e for e in exams if e["exam"] == "KET"), exams[-1])
    r = client.get("/api/v1/words", params={"exam": pick["exam"], "limit": 5})
    r.raise_for_status()
    picked = r.json()
    print(f"  过滤 exam={pick['exam']} → {picked['total']} 词 (列表项带 exam_tags 回显):")
    for w in picked["items"]:
        print(f"    [{w['id']:>5}] {w['word']:<16} {w['cefr_level']:<6} 📝 {'/'.join(w.get('exam_tags') or [])}")


def demo_flashcards(client: "httpx.Client") -> None:
    """⑬ SRS 抽认卡: 拉到期卡 → 翻卡自评 (SM-2);grade 走四元捕获重放。"""
    step(13, "GET /reviews/card + POST /reviews/grade — SRS 抽认卡")
    r = client.get("/api/v1/reviews/card", params={"limit": 3})
    r.raise_for_status()
    cards = r.json()["items"]
    if not cards:
        print("  (无到期卡 —— 新用户先在网页端练习产生复习计划,此步跳过)")
    for i, c in enumerate(cards):
        print(f"  卡{i + 1}: {c['front']}  {c['phonetic'] or ''}  = {c['back'] or '?'}")
    if cards:
        c0 = cards[0]
        grade_key = new_idem_key()  # (八十一) 四元捕获重放演示 (gpt 上上轮 required)
        r = client.post("/api/v1/reviews/grade",
                        json={"word_id": c0["word_id"], "quality": 4},
                        headers={"Idempotency-Key": grade_key})
        r.raise_for_status()
        g = r.json()
        print(f"  自评 4 (记得) → EF {g['ef']} · 间隔 {g['interval_days']} 天 · "
              f"复习次数 {g['repetitions']} · 下次到期 {g['due_at'][:10]}")
        # 断网重试语义: 同键重放
        r2g = client.post("/api/v1/reviews/grade",
                          json={"word_id": c0["word_id"], "quality": 4},
                          headers={"Idempotency-Key": grade_key})
        r2g.raise_for_status()
        replayed = r2g.headers.get("Idempotent-Replayed") == "true"
        print(f"  同 Idempotency-Key 重试 → HTTP {r2g.status_code}"
              f"{' (重放, 不重复推进 SM-2)' if replayed else ''}")


def demo_error_book(client: "httpx.Client") -> None:
    """⑭ 错题本: 概览 + 统计 + 错词重拼 (drill 四元捕获重放演示)。"""
    step(14, "GET /errors + /errors/stats — 错题本")
    r = client.get("/api/v1/errors")
    r.raise_for_status()
    errs = r.json()
    r2 = client.get("/api/v1/errors/stats")
    r2.raise_for_status()
    stats = r2.json()
    print(f"  错题 {len(errs)} 条 · 统计: {json.dumps(stats, ensure_ascii=False)[:160]}")

    print("\n  --- 错词重拼 POST /errors/{id}/drill (幂等) ---")
    if not errs:
        print("  (错题本为空 —— 先在网页端听写答错几题,此步跳过)")
        return
    e0 = errs[0]
    drill_key = new_idem_key()
    r = client.post(f"/api/v1/errors/{e0['id']}/drill",
                    json={"answer": e0["word"]},
                    headers={"Idempotency-Key": drill_key})
    r.raise_for_status()
    d = r.json()
    hint = f"({d.get('phonetic') or ''} {d.get('meaning_zh') or ''})".strip()
    print(f"  重拼 {e0['word']!r} {hint} → good_days {d.get('good_days')} · "
          f"状态 {d.get('remediation_status') or d.get('status')}")
    # 断网重试语义: 同键重放 (响应头 Idempotent-Replayed: true)
    r2 = client.post(f"/api/v1/errors/{e0['id']}/drill",
                     json={"answer": e0["word"]},
                     headers={"Idempotency-Key": drill_key})
    r2.raise_for_status()
    replayed = r2.headers.get("Idempotent-Replayed") == "true"
    print(f"  同 Idempotency-Key 重试 → HTTP {r2.status_code}"
          f"{' (重放, 不重复推进)' if replayed else ''}")


def demo_level_test_paper(client: "httpx.Client") -> None:
    """⑮ 定级测·整卷权威通道 (LEVELTEST-1: 服务端签发 Attempt + 权威判分)。"""
    step(15, "POST /users/me/level-test/attempts → POST submit — 权威定级整卷")
    # 签发权威 Attempt: 题目严格剥离 answer_index, 答案键仅存服务端
    r = client.post("/api/v1/users/me/level-test/attempts", json={"mode": "ladder", "restart": True})
    r.raise_for_status()
    attempt = r.json()
    attempt_id = attempt["attempt_id"]
    questions = attempt.get("questions", [])
    print(f"  签发 Attempt {attempt_id[:8]}… | 阶梯卷 {len(questions)} 题 (不含 answer_index): "
          + " ".join(f"{q['level']}{'V' if q['type'] == 'vocab' else 'G'}" for q in questions))
    # 模拟作答: 客户端仅提交选项下标 (0..3), 不提交 correct
    answers = [{"qid": q["qid"], "selected_index": 0} for q in questions]
    r_sub = client.post(f"/api/v1/users/me/level-test/attempts/{attempt_id}/submit",
                        json={"answers": answers})
    if r_sub.status_code == 200:
        res = r_sub.json()
        print(f"  服务端权威判分 → 级别 {res['resulting_level']} · 得分 {res['score']}%")
        for d in res.get("level_detail", []):
            print(f"    {d['level']:<7} {d['correct']}/{d['answered']} {'✅' if d['passed'] else '❌'}")
        # 幂等重放验证
        r_rep = client.post(f"/api/v1/users/me/level-test/attempts/{attempt_id}/submit",
                            json={"answers": answers})
        print(f"  重放提交 → HTTP {r_rep.status_code} · replayed={r_rep.json().get('replayed', False)}")
    else:
        print(f"  HTTP {r_sub.status_code}: {r_sub.text[:160]}")


def demo_level_test_adaptive(client: "httpx.Client") -> None:
    """⑯ 定级测·自适应阶梯权威循环 (LEVELTEST-1: ESP32 参考实现)。

    契约: 服务端闭环驱动阶梯状态机与终止事件 (登顶/地板/二次降级/16 题上限)。
    硬件无需本地镜像状态机, 仅需逐题提交选项序号 (0..3), 服务端权威裁决对错并下发下一题。
    """
    step(16, "POST /users/me/level-test/attempts/answers — 权威自适应循环 (ESP32 形态)")
    r_start = client.post("/api/v1/users/me/level-test/attempts", json={"mode": "staircase", "restart": True})
    r_start.raise_for_status()
    attempt = r_start.json()
    attempt_id = attempt["attempt_id"]
    print(f"  签发自适应 Attempt: {attempt_id[:8]}…")

    q = attempt.get("question")
    ans_count = 0
    while q and not attempt.get("completed"):
        ans_count += 1
        level = q["level"]
        # 硬件学生按键选择 (0..3)
        selected_index = 0
        r_ans = client.post(
            f"/api/v1/users/me/level-test/attempts/{attempt_id}/answers",
            json={"qid": q["qid"], "selected_index": selected_index},
        )
        if r_ans.status_code != 200:
            print(f"  作答失败: HTTP {r_ans.status_code}: {r_ans.text[:120]}")
            break
        attempt = r_ans.json()
        last_correct = attempt.get("last_correct")
        print(f"  [{ans_count:>2}] {level:<6} 选项 {selected_index} → 服务端判定: {'✓' if last_correct else '✗'}")
        if attempt.get("completed"):
            res = attempt.get("result") or {}
            print(f"  达成终止事件！评定级别 {res.get('resulting_level')} · 得分 {res.get('score')}%")
            break
        q = attempt.get("question")
    print("  注: level=B2+ 时须 URL 编码为 B2%2B")


def demo_level_test_history(client: "httpx.Client") -> None:
    """⑰ 定级历史: 历次结果 (最近优先)。"""
    step(17, "GET /users/me/level-test/history — 定级历史")
    r = client.get("/api/v1/users/me/level-test/history")
    r.raise_for_status()
    items = r.json()["items"]
    print(f"  历次 {len(items)} 条 (最近优先):")
    for it in items[:3]:
        print(f"    {it['created_at'][:16]}  {it['mode']:<9} → {it['resulting_level']}  {it['score']}%")


def demo_onboarding_state(client: "httpx.Client") -> None:
    """⑱ 三步入学状态 (v7 批 3, FSD §1.2): GET /onboarding/state (learn 只读)。"""
    step(18, "GET /onboarding/state — 入学状态 (断点恢复视图)")
    r = client.get("/api/v1/onboarding/state")
    r.raise_for_status()
    s = r.json()
    step_names = {0: "未定级", 1: "已定级", 2: "已选目标", 3: "完成"}
    print(f"  step {s['step']} ({step_names.get(s['step'], '?')}) · 级别 {s['level']} · "
          f"目标 {s.get('goal_exam') or '—'} · 默认配额 {s['quota_default']} 词/天 · "
          f"计划就绪 {s['plan_ready']}")
    print("  注: POST /onboarding/goal|daily-quota 为 Bearer-only (网页端);"
          " level-test(含 /skip) 走 learn 通道, 与既有 staircase 同校验")


def demo_plan_overview(client: "httpx.Client") -> None:
    """⑲ 序数计划总览 (v7 批 3, FSD §3.4): GET /plan + /plan/next (嵌套契约)。"""
    step(19, "GET /plan + GET /plan/next — 计划总览与今日推荐")
    r = client.get("/api/v1/plan")
    r.raise_for_status()
    plan = (r.json() or {}).get("plan")
    if not plan:
        print("  (无活跃计划 —— 三步入学第 3 步 daily-quota 后自动生成; 此步跳过)")
        return
    print(f"  目标 {plan['goal_exam']} · 课程 {plan['completed_lessons']}/{plan['total_lessons']} · "
          f"配额 {plan['quota']} 词/天 · generation {plan['generation']} · "
          f"通关 {plan.get('course_completed')}")
    nxt = client.get("/api/v1/plan/next").json()
    lesson = nxt.get("lesson")
    if lesson:
        print(f"  今日推荐: 第 {lesson['lesson_index']} 课《{lesson['title']}》 "
              f"({lesson['lesson_kind']}, 约 {lesson['est_minutes']} 分钟) · 主推 {nxt['recommend']}")
    else:
        print(f"  今日推荐: 无 (recommend={nxt.get('recommend')})")


def demo_plan_switch_preview(client: "httpx.Client") -> None:
    """⑳ 学习目标切换对比预览 (US-B4, FR-1.11): GET /plan/switch-preview
    (三百零九 方案 A: skip_below_level 免修开关参数 + skipped_lower_words 单源回显)。"""
    step(20, "GET /plan/switch-preview — 目标切换对比预览 (含免修开关对比)")
    # 一百五十八修复 (claude A4/gpt P3/grok Nit-1/qwen 应修-1): 字段名对齐
    # PlanSwitchPreviewOut;同目标 = 200 + same_goal (非 422)
    for exam in ["PET", "KET", "IELTS"]:
        r = client.get("/api/v1/plan/switch-preview", params={"target_exam": exam})
        if r.status_code == 200:
            p = r.json()
            if p.get("same_goal"):
                print(f"  [{exam}] 即当前目标 (same_goal=true),跳过对比")
                continue
            print(f"  目标切换预览 [{exam}] → 当前 {p['current_goal_exam']} vs 新目标 {p['target_goal_exam']} · "
                  f"新学词 {p['target_new_words_count']} (保留 {p['retained_words_count']}) · "
                  f"预计总课数 {p['target_total_lessons']} · 预计天数 {p['estimated_days']} 天")
            # (三百零九 方案 A) 免修开关对比: 关掉后低阶词回到排课范围
            r2 = client.get("/api/v1/plan/switch-preview",
                            params={"target_exam": exam, "skip_below_level": False})
            if r2.status_code == 200:
                p2 = r2.json()
                print(f"  免修开关对比 [{exam}]: skip_below_level=true → 免修 {p['skipped_lower_words']} 词 / "
                      f"false → 免修 {p2['skipped_lower_words']} 词 (关掉开关 = 低阶词回到排课范围)")
            break
        elif r.status_code == 422:
            print(f"  非法目标考试 {exam} (HTTP 422): {r.text[:120]}")
        else:
            print(f"  HTTP {r.status_code}: {r.text[:120]}")
            break


def demo_plan_preview_confirm(client: "httpx.Client") -> None:
    """⑳b⑳c 计划双向预览与确认生成 (UX-PREVIEW-1, 三百零七/九/十):

    - POST /plan/preview —— 顺推 (mode=forward, 配额→天数) 与倒推
      (mode=backward, 截止日→配额) 纯内存试排; 返回健康度诊断
      (load_status recommended/challenging/overloaded) 与**签名 preview_token**;
    - POST /plan/confirm —— 必带 Idempotency-Key; Token 签名四联核验
      (goal_exam/daily_new_items/total_lessons/skip_below_level, 防跨参数篡改);
      已完成同键重放直通首响 (Idempotent-Replayed: true)。

    ⚠ 注意: confirm 将真实切换/生成学习计划 (推进 generation), 仅限测试账号。
    """
    step("20b", "POST /plan/preview — 计划双向预览 (顺推 + 倒推 + 健康度诊断)")
    # 从既有计划取目标; 无计划时用 KET 默认
    r_plan = client.get("/api/v1/plan")
    goal = ((r_plan.json() or {}).get("plan") or {}).get("goal_exam") or "KET"

    # 顺推: 已知每日配额 → 预计完成日与负载
    r = client.post("/api/v1/plan/preview", json={
        "goal_exam": goal, "mode": "forward", "daily_new_items": 6,
    })
    if r.status_code == 422:
        print(f"  顺推预览不可行 (HTTP 422): {r.text[:140]}")
        print("  (无可用词汇/未定级等环境前置缺失时 preview 明确拒绝, 不做静默降级)")
        return
    r.raise_for_status()
    fwd = r.json()
    b = fwd["breakdown"]
    print(f"  顺推 [{goal}] 每日 6 词 → 总词 {fwd['total_words_to_learn']}"
          f" (已学 {fwd['already_learned_words']}, 免修低阶 {fwd['skipped_lower_words']}) · "
          f"课时 {fwd['total_lessons']} (新学 {b['new_study_lessons']}/集训 {b['review_drill_lessons']}/"
          f"输出 {b['output_capstone_lessons']}) · {fwd['estimated_days']} 天 · "
          f"完成日 {fwd['estimated_completion_date']}")
    print(f"  负载诊断: {fwd['load_status']} ({fwd['load_advice']}) · "
          f"日均 {fwd['estimated_daily_minutes']} 分钟 / 帽 {fwd['time_cap_minutes']} 分钟"
          f" · 阶段测里程碑 {fwd['stage_test_milestones']}")
    print(f"  preview_token={fwd['preview_token'][:16]}… (HMAC 签名, 30 分钟有效)")

    # 倒推: 已知截止日 → 建议配额 (不可行时返回 feasible=false + 最早可行日)
    from datetime import date, timedelta
    deadline = (date.today() + timedelta(days=90)).isoformat()
    r = client.post("/api/v1/plan/preview", json={
        "goal_exam": goal, "mode": "backward", "target_date": deadline,
    })
    r.raise_for_status()
    bwd = r.json()
    if bwd.get("feasible", True):
        print(f"  倒推 截止 {deadline} → 建议每日 {bwd['daily_new_items']} 词 · "
              f"{bwd['total_lessons']} 课时 · {bwd['estimated_days']} 天完成")
    else:
        print(f"  倒推 截止 {deadline} → 不可行 (feasible=false): 最早可行日 "
              f"{bwd.get('earliest_feasible_date')} (最少 {bwd.get('min_feasible_days')} 天)")

    step("20c", "POST /plan/confirm — 确认生成 (Idempotency-Key + Token 四联核验)")
    idem_key = new_idem_key()  # 一把 key 绑定一次确认; 未确认期间重试复用
    r = client.post("/api/v1/plan/confirm",
                    json={
                        "goal_exam": fwd["goal_exam"],
                        "daily_new_items": fwd["daily_new_items"],
                        "preview_token": fwd["preview_token"],
                        "total_lessons": fwd["total_lessons"],       # 防跨版本漂移
                        "skip_below_level": fwd["skip_below_level"],  # 防跨参数篡改
                    },
                    headers={"Idempotency-Key": idem_key})
    if r.status_code in (400, 409):
        # Token 过期 (30 分钟)/参数不匹配 400 / 定级漂移 409 —— 演示错误语义
        print(f"  HTTP {r.status_code}: {r.text[:160]}")
        if r.status_code == 409:
            print("  (预览后定级被其他端变更 → STATE_DRIFTED, 客户端应刷新预览重来)")
        return
    r.raise_for_status()
    confirmed = r.json()
    print(f"  确认成功 → plan_id={confirmed['plan_id']} · 目标 {confirmed['goal_exam']} · "
          f"配额 {confirmed['daily_new_items']} 词/天 · {confirmed['total_lessons']} 课时 · "
          f"完成日 {confirmed['estimated_completion_date']}")
    print(f"  Idempotent-Replayed={r.headers.get('Idempotent-Replayed')} (首次)")

    # 同键重试 → 重放直通首响 (短路优先: 不校验 Token TTL/漂移)
    r2 = client.post("/api/v1/plan/confirm",
                     json={
                         "goal_exam": fwd["goal_exam"],
                         "daily_new_items": fwd["daily_new_items"],
                         "preview_token": fwd["preview_token"],
                     },
                     headers={"Idempotency-Key": idem_key})
    r2.raise_for_status()
    print(f"  同 Idempotency-Key 重试 → HTTP {r2.status_code} · "
          f"Idempotent-Replayed={r2.headers.get('Idempotent-Replayed')} · "
          f"plan_id={r2.json()['plan_id']} (= 首次的 {confirmed['plan_id']}, 不重复生成)")
    assert r2.headers.get("Idempotent-Replayed") == "true"
    assert r2.json()["plan_id"] == confirmed["plan_id"]


def demo_review_lesson(client: "httpx.Client") -> None:
    """㉑ 纯复习课闭环 (v7 批 3, FSD §4.1/§4.4): POST /reviews/lesson/start
    → submit (含 skipped 跳过通道演示) → /sessions/{id}/finish。

    纯复习课不受帽/不占课序/不耗配额;毕业项抽查 ≤1 题混入热身最前;
    **API key 的 submit/finish 放行纯复习会话与主线课** ((二百七十) 开放主线课)。
    """
    step(21, "POST /reviews/lesson/start → submit → finish — 纯复习课闭环")
    r = client.post("/api/v1/reviews/lesson/start")
    r.raise_for_status()
    body = r.json()
    items = body.get("items") or []
    sampled = body.get("graduated_sampled_word_ids") or []
    print(f"  到期 {body.get('due_total')} 词 · 本课并入 {body.get('included_count')} 题 · "
          f"毕业抽查 {len(sampled)} 题 (≥30 天未抽查最久优先, 混入热身最前)")
    if not items:
        print("  (无到期词 —— 抽认卡/练习产生复习计划后再试; finish 语义见契约 §1.3)")
        return
    for i, it in enumerate(items):
        prompt = it.get("prompt") or {}
        word = prompt.get("word") or "?"
        if i == 0 and len(items) > 1:
            # 首题演示 skipped 跳过通道 (FR-5.9: 不评分/不动调度, 仅入错题本)
            r = client.post(f"/api/v1/session-items/{it['id']}/submit",
                            json={"skipped": True})
            r.raise_for_status()
            res = r.json()
            print(f"  跳过 {word!r} → skipped={res.get('skipped')} · "
                  f"入错题本={res.get('in_errorbook')} (计 0 分, 计入跳过数)")
        else:
            r = client.post(f"/api/v1/session-items/{it['id']}/submit",
                            json={"user_input": word})
            r.raise_for_status()
    fin = client.post(f"/api/v1/sessions/{body['session_id']}/finish")
    fin.raise_for_status()
    f = fin.json()
    extra = f" · 跳过 {f.get('skipped_items')} 项 (计 0 分)" if f.get("skipped_items") else ""
    print(f"  完课 → 得分 {f.get('score')} · XP {f.get('xp_awarded')} · "
          f"答对 {f.get('correct_items')}/{f.get('total_items')} 项{extra}")
    print("  注: API key 的 submit/finish 放行纯复习会话与主线课 (v1.20.0)")


def demo_mainline_lesson(client: "httpx.Client") -> None:
    """㉒ 主线课闭环 ((二百七十), v1.20.0): GET /plan/next → POST /lessons/{id}/start
    (Idempotency-Key 幂等重放) → submit (识别/选择/拼写/输出/跳过) → /sessions/{id}/finish。

    智能硬件直接上每日主线课程: 自动推进计划游标与课时完成状态，按规则发放 XP。
    ⚠ 注意: 运行此步骤将实际开课并自动答题结课，推进计划进度与 SRS 掌握度；真实儿童账号避免盲跑。
    """
    step(22, "GET /plan/next → POST /lessons/{id}/start → submit → finish — 主线课闭环")
    print("  ⚠ 提示: 本步骤演示硬件直接上主线课并结课 (会真实推进计划游标与 SRS 进度, 仅限测试账号)")
    r_next = client.get("/api/v1/plan/next")
    r_next.raise_for_status()
    next_body = r_next.json()
    lesson = next_body.get("lesson")
    if not lesson:
        print(f"  (无今日推荐主线课 —— recommend={next_body.get('recommend')}; 入学定级并生成学习计划后再试)")
        return
    lesson_id = lesson["id"]
    print(f"  今日推荐: 第 {lesson.get('lesson_index')} 课《{lesson.get('title')}》"
          f" (kind={lesson.get('lesson_kind')}, 约 {lesson.get('est_minutes')} 分钟)")

    idem_key = new_idem_key()
    r_start = client.post(
        f"/api/v1/lessons/{lesson_id}/start",
        headers={"Idempotency-Key": idem_key},
    )
    r_start.raise_for_status()
    body = r_start.json()
    session_id = body["session_id"]
    items = body.get("items") or []
    print(f"  开课成功 → session_id={session_id} · 共 {len(items)} 题")

    # 幂等重放演示 (同键重试返回同一未结课会话)
    r_replay = client.post(
        f"/api/v1/lessons/{lesson_id}/start",
        headers={"Idempotency-Key": idem_key},
    )
    r_replay.raise_for_status()
    replayed = r_replay.headers.get("Idempotent-Replayed") == "true"
    print(f"  同 Idempotency-Key 重试开课 → HTTP {r_replay.status_code}"
          f"{' (幂等重放同一会话, 不重复建课)' if replayed else ''}")

    for i, it in enumerate(items):
        prompt = it.get("prompt") or {}
        task_type = prompt.get("task_type")
        word = prompt.get("word") or it.get("target_word")
        segment = prompt.get("segment") or ""
        if task_type == "recognition":
            # 优先以 correct_option 的 1-based 序号或文本作答 (闭合 Grok Nit-1)
            correct_opt = prompt.get("correct_option") or ""
            options = prompt.get("options") or []
            if correct_opt and correct_opt in options:
                ans = str(options.index(correct_opt) + 1)
            elif correct_opt:
                ans = str(correct_opt)
            else:
                ans = "1"
        elif word:
            ans = str(word)
        elif segment == "output":
            scene_words = prompt.get("scene_words") or ["practice"]
            ans = f"We should {scene_words[0]} English every day."
        else:
            ans = ""

        if ans:
            r_sub = client.post(
                f"/api/v1/session-items/{it['id']}/submit",
                json={"user_input": ans},
            )
            r_sub.raise_for_status()
            sub_res = r_sub.json()
            is_cor = sub_res.get("passed")
            if is_cor is None:
                is_cor = sub_res.get("score_write") == 100 or sub_res.get("combined", 0) >= 80
            cor_label = "✓" if is_cor else "✗"
            print(f"  [{i + 1:>2}/{len(items)}] {segment:<8} {task_type or 'item':<11} "
                  f"作答 {ans[:16]!r} → {cor_label}")
        else:
            r_sub = client.post(
                f"/api/v1/session-items/{it['id']}/submit",
                json={"skipped": True},
            )
            r_sub.raise_for_status()
            print(f"  [{i + 1:>2}/{len(items)}] 跳过该题 (计 0 分)")

    fin = client.post(f"/api/v1/sessions/{session_id}/finish")
    fin.raise_for_status()
    f = fin.json()
    extra = f" · 跳过 {f.get('skipped_items')} 项" if f.get("skipped_items") else ""
    print(f"  结课 → 得分 {f.get('score')} · XP {f.get('xp_awarded')} · "
          f"答对 {f.get('correct_items')}/{f.get('total_items')} 项{extra}")
    print("  注: 学习主线对智能硬件全面开放 (v1.20.0), 结课自动推进计划游标与状态")



# ══════════════════════════════════════════════════════════════════════
# 入口
# ══════════════════════════════════════════════════════════════════════


def main() -> None:
    ap = argparse.ArgumentParser(
        description="远程 API 演示 (笔友 + 学习双通道, --channel 可单跑)")
    ap.add_argument("--key", required=True, help="10 位 API Key (个人资料页创建)")
    ap.add_argument("--base", default="http://127.0.0.1:8000",
                    help="服务地址 (反向代理部署时指 https://your-domain, DEPLOY_APACHE.md)")
    ap.add_argument("--channel", choices=["penpal", "learn", "all"], default="all",
                    help="演示通道: penpal=笔友信件闭环, learn=词库/抽认卡/错题/定级测/"
                         "入学状态/计划预览确认/纯复习课/主线课 (含自动完课, 仅限测试账号), "
                         "all=两者 (默认); 通道对应 API Key scope")
    ap.add_argument("--gate-pin", default=None,
                    help="Apache 静态闸门口令 (DEPLOY_APACHE.md §4b.7; 未启用闸门则不传, 行为不变)")
    ap.add_argument("--insecure", action="store_true",
                    help="跳过 TLS 证书校验 (仅自签证书本地测试用; 公网勿用)")
    ap.add_argument("--wait-reply", type=int, default=0,
                    help="写信后等待 NPC 回信的秒数 (0=不等),等到了演示 Tips (penpal 通道)")
    args = ap.parse_args()

    headers = {"X-API-Key": args.key}
    if args.gate_pin:
        # §4b.7 静态闸门: Apache 校验该头, 不带/值错 → 403 (后端不感知)
        headers["X-Gate-Pin"] = args.gate_pin
    verify = False if args.insecure else True
    if args.insecure:
        print("⚠ --insecure: 已跳过 TLS 证书校验 (仅测试用)")
    client = httpx.Client(base_url=args.base, headers=headers, timeout=180, verify=verify)

    if args.channel in ("all", "penpal"):
        channel_banner("penpal (笔友信件)", "penpal")
        run_penpal_channel(client, args.wait_reply)
    if args.channel in ("all", "learn"):
        channel_banner("learn (词库 + 学习)", "learn")
        run_learn_channel(client)

    print("\n演示完成 ✅  (远程 API 仅凭一个 X-API-Key 头;"
          " 通道=scope, 硬件按 --channel 取用对应参考实现)")


if __name__ == "__main__":
    main()
