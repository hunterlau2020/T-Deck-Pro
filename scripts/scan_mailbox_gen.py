import re

img = open("spiffs_device.bin", "rb").read()
seen = set()
out = []
for m in re.finditer(rb'"counterpart"', img):
    s = max(0, m.start() - 420)
    blob = img[s:m.start() + 260]
    key = bytes(blob[-180:])
    if key in seen:
        continue
    seen.add(key)
    txt = blob.decode("utf-8", "replace").replace("\x00", "?")
    out.append("--- @ %#x ---" % m.start())
    out.append(txt)
open("mailbox_gens.txt", "w", encoding="utf-8").write("\n".join(out))
print(len(seen), "unique generations ->", "mailbox_gens.txt")
