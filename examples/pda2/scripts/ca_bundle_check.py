#!/usr/bin/env python3
"""CA bundle sanity check (review finding 1.5 / allinone-design.md §2.3).

Extracts every PEM embedded in http_utils.cpp's CA_BUNDLE and parses it
with openssl. ANY certificate that fails to parse aborts the check: a
corrupted root silently breaks TLS verification at runtime.

Usage: python ca_bundle_check.py
  Last verified: 2026-08-16 (ISRG X1/YR, DigiCert G2, GlobalSign R3, GTS R4)
"""
import os
import re
import subprocess
import sys
import tempfile

NL = chr(10)   # avoids backslash-escaping pitfalls across shells

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "http_utils.cpp")

lines = open(SRC, encoding="utf-8").read().splitlines()
start = next(i for i, l in enumerate(lines) if "CA_BUNDLE =" in l)
# statement ends on the last embedded END CERTIFICATE line
end = next(i for i in range(start, len(lines))
           if "END CERTIFICATE" in lines[i] and lines[i].rstrip().endswith('";'))

parts = []
for l in lines[start + 1:end + 1]:
    l = l.strip()
    if not l.startswith('"'):
        continue
    parts.append(l[1:-1])                       # keep the trailing literal \n
text = "".join(parts).replace(chr(92) + "n", NL)

certs = text.split("-----BEGIN CERTIFICATE-----" + NL)
certs = ["-----BEGIN CERTIFICATE-----" + NL + c for c in certs if c.strip()]

if not certs:
    print("FAIL: no certificates extracted from " + SRC)
    sys.exit(1)

count = 0
with tempfile.TemporaryDirectory() as td:
    for i, c in enumerate(certs):
        path = os.path.join(td, "cert-%02d.pem" % i)
        with open(path, "w") as f:
            f.write(c)
        r = subprocess.run(["openssl", "x509", "-in", path, "-noout", "-subject"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL: cannot parse cert %d: %s" % (i, (r.stderr or "").strip().splitlines()[0]))
            sys.exit(1)
        print("OK   " + (r.stdout or "").strip())
        count += 1

print("PASS: %d root certificate(s) parse correctly" % count)
