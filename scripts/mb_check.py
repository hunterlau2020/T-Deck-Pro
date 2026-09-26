import json, ssl, urllib.request

def get(path, key="89rg35eua2"):
    req = urllib.request.Request("https://www.studyreview.net" + path)
    req.add_header("X-API-Key", key)
    req.add_header("X-Gate-Pin", "49ef146ed5")
    with urllib.request.urlopen(req, timeout=20,
                                context=ssl.create_default_context()) as r:
        return json.loads(r.read())

print("== pals under hunter key ==")
for p in get("/api/v1/pen-pals"):
    print(json.dumps(p, ensure_ascii=False))

for root in (2, 7):
    print(f"== thread {root} letters ==")
    for e in get(f"/api/v1/emails?thread_root_id={root}").get("emails", []):
        print("letter", e["id"], "sender_user_id=", e.get("sender_user_id"),
              "sender=", e.get("sender_name"))
