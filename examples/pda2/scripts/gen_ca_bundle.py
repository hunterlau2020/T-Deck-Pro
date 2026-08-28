#!/usr/bin/env python3
"""Generate the full Mozilla root-CA bundle header for WiFiClientSecure.

Output format = esp_crt_bundle binary layout consumed by the Arduino core
(libraries/WiFiClientSecure/src/esp_crt_bundle.c, core 2.0.x):

    [2B cert count, big-endian]
    per cert (presorted by subject DER, memcmp order - binary search):
      [2B name_len BE][2B key_len BE][subject Name DER][SubjectPublicKeyInfo DER]

The verify callback keeps this data in FLASH (only a num_certs pointer
index lands in heap); it binary-searches the subject names and parses just
the one matching public key at handshake time.

Source: the Mozilla trust store as published by curl.se (cacert.pem,
~121 roots). Refresh procedure: download a fresh cacert.pem, re-run this
script, commit the regenerated header together with the source file.

Usage:  python gen_ca_bundle.py [path/to/cacert.pem]
        (default input: %TEMP%/cacert.pem; downloaded from curl.se if absent)
"""
import hashlib
import os
import sys
import urllib.request
from datetime import date, timezone

from cryptography import x509
from cryptography.hazmat.primitives.serialization import (
    Encoding, PublicFormat)

SRC_URL = "https://curl.se/ca/cacert.pem"
OUT_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "ca_bundle_full.h")
SYM = "CA_BUNDLE_MOZILLA"

# Anchors the pda2 providers chain to (device finding 2026-08-28): every
# pattern here must match a bundle entry's full subject DN (RFC 4514) or
# the generated header is rejected - a missing anchor would resurface as
# X509 verify failures. NOTE: the runtime match is on the FULL subject DER
# (binary search in the core), these patterns are a generation-time check.
REQUIRED_SUBJECT = [
    "CN=Amazon Root CA 1",                    # api.deepseek.com
    "CN=USERTrust RSA Certification Authority",  # api.minimaxi.com (WoTrus)
    "CN=GTS Root R4",                         # openrouter.ai
    "OU=GlobalSign Root CA - R3",             # dashscope.aliyuncs.com (via R46)
    "CN=DigiCert Global Root G2",             # tokenhub.tencentmaas.com
]


def load_pems(path):
    txt = open(path, encoding="utf-8").read()
    pems, cur = [], []
    for line in txt.splitlines():
        cur.append(line)
        if "-----END CERTIFICATE-----" in line:
            pems.append("\n".join(cur))
            cur = []
    return pems


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else None
    if not src:
        tmp = os.path.join(os.environ.get("TEMP", "/tmp"), "cacert.pem")
        if not os.path.exists(tmp):
            print(f"downloading {SRC_URL} -> {tmp}")
            urllib.request.urlretrieve(SRC_URL, tmp)
        src = tmp
    pems = load_pems(src)
    print(f"source: {src} ({len(pems)} certificates)")

    entries = []   # (subject_der, spki_der, subject_rfc4514)
    for pem in pems:
        cert = x509.load_pem_x509_certificate(pem.encode())
        subject = cert.subject.public_bytes(Encoding.DER)
        spki = cert.public_key().public_bytes(
            Encoding.DER, PublicFormat.SubjectPublicKeyInfo)
        entries.append((subject, spki, cert.subject.rfc4514_string()))

    entries.sort(key=lambda e: e[0])        # memcmp order for binary search
    for a, b in zip(entries, entries[1:]):  # binary search needs strict order
        assert a[0] < b[0], f"duplicate subject DER: {a[2]}"

    have = " || ".join(dn for _, _, dn in entries)
    missing = [p for p in REQUIRED_SUBJECT if p not in have]
    assert not missing, f"required anchors missing from source: {missing}"

    blob = bytearray(len(entries).to_bytes(2, "big"))
    for subject, spki, _ in entries:
        blob += len(subject).to_bytes(2, "big")
        blob += len(spki).to_bytes(2, "big")
        blob += subject + spki

    # header emission: 16 bytes per line keeps the file reviewable-ish
    lines = []
    for i in range(0, len(blob), 16):
        lines.append("    " + ",".join(f"0x{b:02X}" for b in blob[i:i + 16]) + ",")
    sha = hashlib.sha256(bytes(blob)).hexdigest()
    head = (
        "/**\n"
        " * GENERATED FILE - do not edit by hand.\n"
        " * Regenerate with scripts/gen_ca_bundle.py (needs: python "
        "'cryptography').\n"
        f" * Source : Mozilla trust store via {SRC_URL}\n"
        f" * Date   : {date.today().isoformat()}   Roots: {len(entries)}\n"
        f" * Size   : {len(blob)} bytes   SHA-256: {sha}\n"
        " * Layout : esp_crt_bundle binary format (2B count BE; per cert\n"
        " * 2B name_len + 2B key_len + subject DER + SubjectPublicKeyInfo\n"
        " * DER, presorted by subject for the core's binary search). The\n"
        " * array is consumed in place from flash (const) - never to RAM.\n"
        " */\n"
        f"static const uint8_t {SYM}[] = {{\n"
    )
    body = "\n".join(lines) + "\n};\n"
    with open(OUT_HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(head + body)
    print(f"wrote {OUT_HEADER}: {len(blob)} bytes, {len(entries)} roots, "
          f"sha256 {sha[:16]}...")
    print("required provider anchors: all present")


if __name__ == "__main__":
    main()
