import json, ssl, urllib.request
req = urllib.request.Request("https://www.studyreview.net/api/v1/users/me/profile")
req.add_header("X-API-Key", "89rg35eua2")
req.add_header("X-Gate-Pin", "49ef146ed5")
with urllib.request.urlopen(req, timeout=20, context=ssl.create_default_context()) as r:
    print(json.dumps(json.loads(r.read()), ensure_ascii=False))
