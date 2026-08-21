"""远程笔友 API 演示 (2026-08-20 重大功能: API 访问)。

只用一个 `X-API-Key` 请求头,走完笔友远程 API 的完整闭环:

  ⓪ 查自己的资料    GET  /api/v1/users/me/profile   (2026-08-21 新增)
  ① 列笔友          GET  /api/v1/pen-pals
  ② 列可选主题      GET  /api/v1/topics/suggestions
  ③ 写信 (关联主题) POST /api/v1/emails           (body 带 topic_id + 幂等键)
     ③b 同 key 重试  POST /api/v1/emails           (结果未确认 → 重放,不重复建信)
  ④ 看信 (按线程)   GET  /api/v1/emails?pen_pal_id&thread_root_id
  ⑤ 纠错 (我的信)   POST /api/v1/emails/{id}/correction
  ⑥ 润色 (我的信)   POST /api/v1/emails/{id}/polish
  ⑦ 回信 (锚定线程) POST /api/v1/emails           (body 带 thread_root_id + 幂等键)
  ⑧ 回信 Tips       POST /api/v1/emails/{id}/tips (基于最近一封收到的信)
  ⑨ 同主题再写      POST /api/v1/emails → 新线程  (线程按首信分组演示)
  ⑩ 信箱概览        GET  /api/v1/emails/mailbox

线程模型 (2026-08-21 线程按首信分组):
- 每个线程由**首信**标识,锚点是 `EmailOut.thread_root_id` (首信即自身);
- 精确取线程: `GET /emails?pen_pal_id&thread_root_id`;
- `subject` 参数是**兼容通道** —— 跨线程合并同首信主题返回 (旧客户端);
- `Re:` 回信未带锚点时自动接同对方同主题的最近线程;**裸标题一律新线程**。

幂等重试 (2026-08-21 (十八),ESP32 客户端语义):
- 创建型 POST 可带 `Idempotency-Key` 头 (客户端生成,惯例 32 位 hex):
  发信后结果未确认 (用户关掉等待框/超时/断连) 时**复用同一把 key 重试**,
  服务端按 (用户, key) 重放已存信件,不产生重复信;
- 重放响应 200 + `Idempotent-Replayed: true`,正文返回同一封信 (id 不变);
- 收到确认成功后 key 作废;草稿内容变化要换新 key;
- 不带该头的请求行为与旧版完全一致。

用法:
    cd backend
    python scripts/remote_api_demo.py --key <你的10位APIKey>
    python scripts/remote_api_demo.py --key xxx --base http://127.0.0.1:8000

Key 在网页端 个人资料页 → 🔑 API Key 创建 (仅显示一次)。
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


def new_idem_key() -> str:
    """32 位 hex 幂等键 —— 固件侧等价: ESP32 每次投递前生成并保存,
    结果未确认期间的重试复用同一把,确认成功后丢弃。"""
    return secrets.token_hex(16)


def step(n: int, title: str) -> None:
    print(f"\n{'=' * 62}\n步骤 {n}: {title}\n{'=' * 62}")


def main() -> None:
    ap = argparse.ArgumentParser(description="远程笔友 API 演示")
    ap.add_argument("--key", required=True, help="10 位 API Key (个人资料页创建)")
    ap.add_argument("--base", default="http://127.0.0.1:8000", help="服务地址")
    ap.add_argument("--wait-reply", type=int, default=0,
                    help="写信后等待 NPC 回信的秒数 (0=不等),等到了演示 Tips")
    args = ap.parse_args()

    client = httpx.Client(base_url=args.base, headers={"X-API-Key": args.key}, timeout=180)

    # ⓪ 我是谁: 查自己的资料 --------------------------------------------------
    step(0, "GET /api/v1/users/me/profile — 我是谁")
    r = client.get("/api/v1/users/me/profile")
    r.raise_for_status()
    me = r.json()
    print(
        f"  {me['name']} · {me['age_band']} · {me['level']}"
        f" · {me['city'] or '城市未填'} · 兴趣: {me['interests'] or '未填'}"
    )

    # ① 列笔友 --------------------------------------------------------------
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

    # ② 列可选主题 ----------------------------------------------------------
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

    # ③ 写信 (关联主题 + 客户端幂等键) ---------------------------------------
    step(3, "POST /api/v1/emails — 写信并关联主题 (带 Idempotency-Key)")
    content = (
        "Dear friend, hello from the remote API demo! I want to tell you about "
        "my favourite animal. I like cats very much because they are quiet and "
        "soft. My cat Momo sleeps on my desk every day. What animal do you like?"
    )
    send_body = {
        "pen_pal_id": pal["id"],
        "subject": subject,
        "topic_id": topic["id"] if topic else None,
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

    # ④ 看信 (按线程锚点精确读取) -------------------------------------------
    step(4, "GET /api/v1/emails?pen_pal_id&thread_root_id — 读线程 (按首信锚点)")
    r = client.get("/api/v1/emails", params={
        "pen_pal_id": pal["id"], "thread_root_id": root_id,
    })
    r.raise_for_status()
    for e in r.json()["emails"]:
        who = "我" if e["sender_user_id"] else f"NPC {e['sender_name']}"
        print(f"  [{e['id']}] {who}: {e['content'][:60].replace(chr(10), ' ')}...")
    print("  (兼容通道: GET ?subject=<标题> 跨线程合并返回,旧客户端仍可用)")

    # ⑤ 纠错 ---------------------------------------------------------------
    step(5, f"POST /api/v1/emails/{email['id']}/correction — 纠错 (我写的信)")
    r = client.post(f"/api/v1/emails/{email['id']}/correction")
    r.raise_for_status()
    fb = r.json()
    print(f"  degraded={fb['degraded']}")
    for c in fb["corrections"]:
        print(f"  [{c['type']}] {c['from']!r} → {c['to']!r}")
        print(f"         {c['explanation']}")

    # ⑥ 润色 ---------------------------------------------------------------
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

    # ⑦ 回信 (锚定线程) ----------------------------------------------------
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

    # ⑧ 回信 Tips (可选: 等 NPC 回信后演示) ----------------------------------
    step(8, "POST /api/v1/emails/{id}/tips — 回信 Tips (基于收到的信)")
    if args.wait_reply > 0:
        print(f"  等待 {args.wait_reply}s 让 NPC 回信 (延迟 ~60s)...")
        time.sleep(args.wait_reply)
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

    # ⑨ 同主题二次撰写 = 新线程 (线程按首信分组演示) -------------------------
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
    rows = r.json()
    for row in rows:
        if row["subject"] == subject:
            print(
                f"    thread_root_id={row['thread_root_id']:<4} {row['counterpart']}"
                f" · {row['count']} 封 · {row['state']}"
                + ("  ← 本轮新线程" if row["thread_root_id"] == fresh["thread_root_id"] else "")
            )

    # ⑩ 信箱概览 ------------------------------------------------------------
    step(10, "GET /api/v1/emails/mailbox — 信箱线程概览")
    for row in rows:
        print(f"  pen_pal={row['pen_pal_id']} {row['subject']!r} {row['state']} 未读{row['unread']}")

    print("\n演示完成 ✅  (远程 API 仅凭一个 X-API-Key 头完成全部笔友读写)")


if __name__ == "__main__":
    main()
