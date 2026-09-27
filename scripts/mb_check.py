"""Mailbox + thread-direction quick check (no hardcoded credentials).

--key/--gate or PENPAL_KEY/GATE_PIN env; --base or PENPAL_BASE
(default http://127.0.0.1:8000). See inspect_mailbox.py header.
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
ap.add_argument("--insecure", action="store_true")
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
        return json.loads(r.read())


print("== pals ==")
for p in get("/api/v1/pen-pals"):
    print(json.dumps(p, ensure_ascii=False))

for row in get("/api/v1/emails/mailbox"):
    print(row["pen_pal_id"], row["thread_root_id"], row["counterpart"],
          row["state"], row["last_sender"], "count=", row["count"],
          "unread=", row["unread"])

for row in get("/api/v1/emails/mailbox"):
    root = row["thread_root_id"]
    print(f"== thread {root} letters ==")
    for e in get(f"/api/v1/emails?thread_root_id={root}").get("emails", []):
        print("letter", e["id"], "sender_user_id=", e.get("sender_user_id"),
              "sender=", e.get("sender_name"))
