"""Byte-level audit of ca_bundle_full.h against a live served chain.

1) parse the bundle (esp_crt_bundle format: u16be count, then per cert
   [u16be name_len][u16be key_len][subject DER][SPKI DER])
2) for each cert in the served chain, look up the bundle entry by subject
   DER (binary-search semantics) and compare the SPKI bytes
3) verify the whole bundle is strictly sorted by subject DER (the binary
   search precondition)
"""
import re
import sys

from cryptography import x509
from cryptography.hazmat.primitives.serialization import (
    Encoding, PublicFormat)

BUNDLE = "examples/pda2/ca_bundle_full.h"
CHAIN = sys.argv[1:]          # PEM files, leaf first

src = open(BUNDLE).read()
hexstr = "".join(re.findall(r"0x([0-9a-fA-F]{2})", src))
blob = bytes.fromhex(hexstr)

count = int.from_bytes(blob[0:2], "big")
off = 2
entries = []
for _ in range(count):
    nlen = int.from_bytes(blob[off:off + 2], "big")
    klen = int.from_bytes(blob[off + 2:off + 4], "big")
    off += 4
    subj = blob[off:off + nlen]
    off += nlen
    spki = blob[off:off + klen]
    off += klen
    entries.append((subj, spki))
print("bundle: %d entries, %d bytes parsed, leftover=%d"
      % (count, len(blob), len(blob) - off))

subjects = [e[0] for e in entries]
sorted_ok = all(subjects[i] < subjects[i + 1] for i in range(len(subjects) - 1))
print("strictly sorted by subject DER:", sorted_ok)

for path in CHAIN:
    cert = x509.load_pem_x509_certificate(open(path, "rb").read())
    subj = cert.subject.public_bytes(Encoding.DER)
    spki = cert.public_key().public_bytes(
        Encoding.DER, PublicFormat.SubjectPublicKeyInfo)
    name = cert.subject.rfc4514_string()
    hit = [e for e in entries if e[0] == subj]
    if not hit:
        print("%-40s subject NOT in bundle" % name)
        continue
    match = "SPKI MATCH" if hit[0][1] == spki else "*** SPKI MISMATCH ***"
    dup = " (dup subject x%d!)" % len(hit) if len(hit) > 1 else ""
    print("%-40s subject in bundle, %s%s" % (name, match, dup))
