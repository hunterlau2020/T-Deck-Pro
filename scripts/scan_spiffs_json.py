"""Scan a raw SPIFFS partition image for PenPal cache files and print the
JSON payloads (SPIFFS stores content uncompressed; file data pages appear
verbatim). Cache format: first line = fetched_at epoch, rest = raw body.
"""
import json
import re
import sys

img = open(sys.argv[1] if len(sys.argv) > 1 else "spiffs_device.bin", "rb").read()
print("image", len(img), "bytes")

# find the cache files by their signature strings
for marker in (b"/penpal/pals.json", b"/penpal/mailbox.json", b"/penpal/th_"):
    hits = [m.start() for m in re.finditer(re.escape(marker), img)]
    print(f"\n== {marker.decode()} : {len(hits)} name-page hit(s) ==")

# JSON payloads: scan for arrays/objects containing the tell-tale keys
seen = []
for m in re.finditer(rb'\[\s*\{\s*"(id|pen_pal_id)"', img):
    start = m.start()
    # take up to 4 KB and trim to the matching closing bracket
    chunk = img[start:start + 4096]
    depth = 0
    end = None
    in_str = False
    esc = False
    for i, b in enumerate(chunk):
        c = chr(b)
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c in "[{":
            depth += 1
        elif c in "]}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end:
        blob = chunk[:end]
        try:
            data = json.loads(blob)
        except Exception:
            continue
        if data in seen:
            continue
        seen.append(data)
        print(f"\n-- JSON @ {start:#x} --")
        print(json.dumps(data, ensure_ascii=False, indent=1)[:2400])
