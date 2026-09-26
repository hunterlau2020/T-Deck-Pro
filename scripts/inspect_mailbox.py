"""Dump raw PenPal server JSON for mailbox + threads (device-truth check).

Context (user report 2026-09-26): hello thread (2 letters, state pend)
shows 'hunter' in mailbox but 'To: hunter' in detail - is the server
data or the client state machine wrong?
"""
import json
import ssl
import urllib.request

BASE = "https://www.studyreview.net"
KEY = "89rg35eua2"           # hunter test key (device env.cfg PENPAL_KEY)
GATE = "49ef146ed5"          # X-Gate-Pin (penpal_api.cpp:168)

ctx = ssl.create_default_context()


def get(path):
    req = urllib.request.Request(BASE + path)
    req.add_header("X-API-Key", KEY)
    req.add_header("X-Gate-Pin", GATE)
    with urllib.request.urlopen(req, timeout=20, context=ctx) as r:
        return json.loads(r.read().decode())


mb = get("/api/v1/emails/mailbox")
print("== MAILBOX ==")
for row in (mb if isinstance(mb, list) else mb.get("rows", mb.get("emails", []))):
    print(json.dumps(row, ensure_ascii=False))

pals = get("/api/v1/pen-pals")
print("\n== PEN-PALS ==")
for p in pals:
    print(json.dumps(p, ensure_ascii=False))

# dump every thread referenced by the mailbox (raw letter fields)
roots = []
for row in (mb if isinstance(mb, list) else mb.get("rows", mb.get("emails", []))):
    rid = row.get("thread_root_id")
    if rid and rid not in roots:
        roots.append(rid)
for rid in roots[:6]:
    print(f"\n== THREAD root={rid} ==")
    data = get(f"/api/v1/emails?thread_root_id={rid}")
    for e in data.get("emails", []):
        keep = {k: e.get(k) for k in
                ("id", "subject", "sender_user_id", "sender_name",
                 "thread_root_id", "pen_pal_id", "created_at", "topic_id")}
        print(json.dumps(keep, ensure_ascii=False))
