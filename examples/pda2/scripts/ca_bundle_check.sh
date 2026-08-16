#!/bin/sh
# CA bundle sanity check (review finding 1.5 / allinone-design.md §2.3).
#
# Extracts every PEM embedded in http_utils.cpp's CA_BUNDLE and parses it
# with openssl. ANY certificate that fails to parse aborts the build check:
# a corrupted root silently breaks TLS verification at runtime.
#
# Usage: sh ca_bundle_check.sh
#   Last verified: 2026-08-16 (ISRG X1/YR, DigiCert G2, GlobalSign R3, GTS R4)

set -e

SRC="$(dirname "$0")/../http_utils.cpp"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Extract the C string concatenation and unescape the literal \n.
awk '/static const char \*CA_BUNDLE =/,/;/{print}' "$SRC" \
    | grep -o '"[^"]*"' | tr -d '"' \
    | awk '{gsub(/\\n/, "\n"); print}' > "$TMP/bundle.pem"

grep -q "BEGIN CERTIFICATE" "$TMP/bundle.pem" || {
    echo "FAIL: no certificates extracted from $SRC"
    exit 1
}

count=0
awk 'BEGIN{n=0} /-----BEGIN CERTIFICATE-----/{n++; fn=sprintf("%s/cert-%02d.pem", dir, n)}
     {print > fn}' dir="$TMP" "$TMP/bundle.pem"

for f in "$TMP"/cert-*.pem; do
    [ -e "$f" ] || continue
    subject=$(openssl x509 -in "$f" -noout -subject 2>/dev/null) || {
        echo "FAIL: cannot parse $(basename "$f")"
        exit 1
    }
    echo "OK   $subject"
    count=$((count + 1))
done

[ "$count" -ge 1 ] || { echo "FAIL: no certificates parsed"; exit 1; }
echo "PASS: $count root certificate(s) parse correctly"
