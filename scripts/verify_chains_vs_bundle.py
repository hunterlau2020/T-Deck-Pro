#!/usr/bin/env python3
"""Simulate the device's esp_crt_bundle verification on the PC.

For each provider endpoint:
  1. fetch the live served chain (TLS, no verify - we only want the certs)
  2. find which bundle root the chain anchors to
     (esp_crt_bundle matches the TOP served cert's ISSUER against the
     bundle's subject entries, then compares the public key)
  3. fully verify the chain (signatures + validity) against that root,
     reconstructed from cacert.pem / the pinned extra root
"""
import datetime
import re
import socket
import ssl
import sys

from cryptography import x509
from cryptography.hazmat.primitives.asymmetric import rsa, ec, padding
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from cryptography.x509.oid import NameOID

BUNDLE = "examples/pda2/ca_bundle_full.h"
CACERT = "examples/pda2/scripts/../..//cacert_download.pem"  # filled below
EXTRA_ROOTS = ["examples/pda2/scripts/extra_roots/globalsign-root-r1.pem",
               "examples/pda2/scripts/extra_roots/digicert-global-root-ca.pem",
               "examples/pda2/scripts/extra_roots/aaa-certificate-services.pem"]
HOSTS = [
    "api.deepseek.com",
    "api.minimax.io",
    "api.minimaxi.com",
    "openrouter.ai",
    "dashscope.aliyuncs.com",
    "tokenhub.tencentmaas.com",
    "generativelanguage.googleapis.com",
]

# ---- load bundle (esp_crt_bundle binary format) ----
src = open(BUNDLE, encoding="utf-8").read()
blob = bytes.fromhex("".join(re.findall(r"0x([0-9a-fA-F]{2})", src)))
count = int.from_bytes(blob[0:2], "big")
off = 2
entries = {}                       # subject DER -> SPKI DER
for _ in range(count):
    nlen = int.from_bytes(blob[off:off + 2], "big")
    klen = int.from_bytes(blob[off + 2:off + 4], "big")
    off += 4
    subj = blob[off:off + nlen]
    off += nlen
    spki = blob[off:off + klen]
    off += klen
    entries[subj] = spki
print(f"bundle loaded: {len(entries)} roots")


def pem_files(path):
    txt = open(path, encoding="utf-8").read()
    pems, cur = [], []
    for line in txt.splitlines():
        cur.append(line)
        if "-----END CERTIFICATE-----" in line:
            pems.append("\n".join(cur))
            cur = []
    return pems


# full certs for the roots we may anchor to (subject DER -> cert)
root_certs = {}
import os
cacert = os.path.join(os.environ.get("TEMP", "/tmp"), "cacert.pem")
if os.path.exists(cacert):
    for pem in pem_files(cacert):
        c = x509.load_pem_x509_certificate(pem.encode())
        root_certs[c.subject.public_bytes(Encoding.DER)] = c
for path in EXTRA_ROOTS:
    c = x509.load_pem_x509_certificate(open(path, "rb").read())
    root_certs[c.subject.public_bytes(Encoding.DER)] = c
print(f"root certs available for full verification: {len(root_certs)}")


def get_chain(host):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with socket.create_connection((host, 443), timeout=10) as sock:
        with ctx.wrap_socket(sock, server_hostname=host) as tls:
            raw = tls.get_unverified_chain()   # py3.13: list[bytes DER]
            return [ssl.DER_cert_to_PEM_cert(c) for c in raw]


def verify_sig(child, issuer):
    pub = issuer.public_key()
    if isinstance(pub, rsa.RSAPublicKey):
        pub.verify(child.signature, child.tbs_certificate_bytes,
                   padding.PKCS1v15(), child.signature_hash_algorithm)
    elif isinstance(pub, ec.EllipticCurvePublicKey):
        pub.verify(child.signature, child.tbs_certificate_bytes,
                   ec.ECDSA(child.signature_hash_algorithm))
    else:
        raise RuntimeError("unsupported key type")


now = datetime.datetime.now(datetime.timezone.utc)
for host in HOSTS:
    try:
        chain = [x509.load_pem_x509_certificate(p.encode()) for p in get_chain(host)]
    except Exception as e:
        print(f"\n{host}: FETCH FAILED - {e}")
        continue
    names = " -> ".join(c.subject.rfc4514_string()[:40] for c in chain)
    print(f"\n{host}: chain ({len(chain)} certs)")
    print(f"  {names}")
    top = chain[-1]
    issuer_der = top.issuer.public_bytes(Encoding.DER)
    hit = entries.get(issuer_der)
    if hit is None:
        print("  *** TOP CERT ISSUER NOT IN BUNDLE -> device would fail X509 ***")
        continue
    anchor = root_certs.get(issuer_der)
    if anchor is None:
        print("  issuer found in bundle but full root cert not on disk for deep verify")
        continue
    if anchor.public_key().public_bytes(Encoding.DER, PublicFormat.SubjectPublicKeyInfo) != hit:
        print("  *** BUNDLE SPKI MISMATCH for anchor ***")
        continue
    ok = True
    try:
        for c, p in zip(chain, chain[1:] + [anchor]):
            verify_sig(c, p)
        verify_sig(top, anchor)
        for c in chain:
            if not (c.not_valid_before_utc <= now <= c.not_valid_after_utc):
                print(f"  *** out of validity: {c.subject.rfc4514_string()}")
                ok = False
    except Exception as e:
        print(f"  *** SIGNATURE VERIFY FAILED: {e}")
        ok = False
    if ok:
        print(f"  OK - anchors to: {anchor.subject.rfc4514_string()}")
