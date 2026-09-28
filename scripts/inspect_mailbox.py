"""Dump raw PenPal server JSON for mailbox + threads (device-truth check).

Credentials are NEVER hardcoded (review P0 2026-09-27: this script's
earlier revision shipped a real key + gate-pin in tracked source):
  key / gate-pin: --key/--gate or PENPAL_KEY/GATE_PIN env vars
  base:           --base or PENPAL_BASE (default http://127.0.0.1:8000)
Rotation of previously shipped values: TODO 'PenPal 测试 key 轮换'.
"""
import argparse
import json
import os
import ssl
import urllib.request

ap = argparse.ArgumentParser()
ap.add_argument("--key", default=os.environ.get("PENPAL_KEY", ""))
ap.add_argument("--gate", default=os.environ.get("GATE_PIN", ""))
ap.add_argument("--base",
                default=os.environ.get("PENPAL_BASE", "http://127.0.0.1:8000"))
ap.add_argument("--insecure", action="store_true",
                help="skip TLS verify (self-signed lab only)")
args = ap.parse_args()
assert args.key, "need --key or PENPAL_KEY"

ctx = None
if args.base.startswith("https://"):
    ctx = ssl.create_default_context()
    if args.insecure:
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE


def get(path):
    req = urllib.request.Request(args.base.rstrip("/") + path)
    req.add_header("X-API-Key", args.key)
    if args.gate:
        req.add_header("X-Gate-Pin", args.gate)
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
