#!/usr/bin/env python3
"""OTA manifest signer / verifier (docs/ota-update-design.md §2.1).

Canonical signed byte string = the six explicit fields, each value followed
by one '\\n', UTF-8, no BOM, no extra whitespace (v6 §2.1 pinned encoding):
    version \\n url \\n size \\n sha256 \\n seq \\n notes \\n
  - size / seq : decimal ASCII, unsigned, no leading zeros (str(int))
  - sha256     : 64 LOWERCASE hex chars
  - notes containing '\\n' or '\\r' are rejected at signing time

Signature: ECDSA P-256 over SHA-256, serialized IEEE P1363 r||s (64 bytes),
base64 in the manifest's "sig" field. The device rebuilds the same byte
string and verifies with mbedtls_ecdsa_verify (r/s via mbedtls_mpi).

Usage:
  python scripts/ota_sign.py firmware.bin --url https://host/fw.bin \
      --version v2.6-260915 --seq 7 --notes "PenPal TTS; voice fixes" \
      [--key ota_signing_key.pem] [--out manifest.json]
  python scripts/ota_sign.py --verify manifest.json   # self-check only
"""
import argparse
import base64
import hashlib
import json
import math
import pathlib
import sys

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import (
    decode_dss_signature)

ROOT = pathlib.Path(__file__).resolve().parents[1]


def canonical_bytes(m: dict) -> bytes:
    """Rebuild the signed byte string from manifest fields - the SAME code
    path signs and verifies, so any encoding drift fails loudly here."""
    b = f"{m['version']}\n{m['url']}\n{m['size']}\n{m['sha256']}\n{m['seq']}\n{m['notes']}\n"
    return b.encode("utf-8")


def check_encoding(m: dict) -> None:
    assert str(m["size"]) == m["size"], "size must stay a decimal string"
    assert str(m["seq"]) == m["seq"], "seq must stay a decimal string"
    assert m["size"].isdigit() and not m["size"].startswith("0") or m["size"] == "0"
    assert m["seq"].isdigit() and not m["seq"].startswith("0")
    assert len(m["sha256"]) == 64 and m["sha256"] == m["sha256"].lower()
    assert all(c in "0123456789abcdef" for c in m["sha256"])
    assert "\n" not in m["notes"] and "\r" not in m["notes"], \
        "notes with newline are rejected (§2.1)"


def sign(path_bin, url, version, seq, notes, key_path) -> dict:
    blob = path_bin.read_bytes()
    m = {
        "version": version,
        "url": url,
        "size": str(len(blob)),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "seq": str(seq),
        "notes": notes,
    }
    check_encoding(m)
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
    key = load_pem_private_key(key_path.read_bytes(), password=None)
    assert isinstance(key, ec.EllipticCurvePrivateKey), "P-256 key required"
    der = key.sign(canonical_bytes(m), ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der)
    sig = r.to_bytes(32, "big") + s.to_bytes(32, "big")   # IEEE P1363 r||s
    assert len(sig) == 64
    m["sig"] = base64.b64encode(sig).decode("ascii")
    return m


def verify(m: dict, key_path) -> bool:
    """Publish-side self check (mirrors the device's mbedtls path)."""
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives.serialization import (
        load_pem_private_key)
    from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    check_encoding(m)
    sig = base64.b64decode(m["sig"])
    assert len(sig) == 64, "sig must be 64 bytes r||s"
    pub = load_pem_private_key(key_path.read_bytes(),
                               password=None).public_key()
    r = int.from_bytes(sig[:32], "big")
    s = int.from_bytes(sig[32:], "big")
    der = encode_dss_signature(r, s)
    try:
        pub.verify(der, canonical_bytes(m), ec.ECDSA(hashes.SHA256()))
        return True
    except InvalidSignature:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware", nargs="?", type=pathlib.Path,
                    help=".pio/build/pda2/firmware.bin")
    ap.add_argument("--url", help="public URL the device will download from")
    ap.add_argument("--version", help='e.g. "v2.6-260915"')
    ap.add_argument("--seq", type=int, help="monotonic manifest sequence")
    ap.add_argument("--notes", default="", help="one line, no newlines")
    ap.add_argument("--key", type=pathlib.Path,
                    default=ROOT / "ota_signing_key.pem")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "manifest.json")
    ap.add_argument("--verify", action="store_true",
                    help="verify an existing manifest instead of signing")
    args = ap.parse_args()

    if args.verify:
        m = json.loads(args.out.read_text(encoding="utf-8"))
        ok = verify(m, args.key)
        print("verify:", "OK" if ok else "FAILED")
        sys.exit(0 if ok else 1)

    assert args.firmware and args.firmware.exists(), "firmware.bin missing"
    assert args.url and args.url.startswith("https://") or \
        (args.url and args.url.startswith("http://")), "url required"
    assert args.version and args.seq is not None
    m = sign(args.firmware, args.url, args.version, args.seq, args.notes,
             args.key)
    assert verify(m, args.key), "self-verify failed"
    args.out.write_text(json.dumps(m, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print(f"wrote {args.out} ({m['size']} B, seq {m['seq']}, {m['version']})")
    print(f"  sha256 {m['sha256']}")
    print("publish firmware.bin + manifest.json at the URL's host")


if __name__ == "__main__":
    main()
