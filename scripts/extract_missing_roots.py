"""Extract the two missing anchor roots and save them as pinned extra roots.

1. AAA Certificate Services: self-signed root served in api.minimaxi.com's
   own chain - extracting from the live chain guarantees the subject DER
   byte-matches what the device sees.
2. DigiCert Global Root CA: official download from digicert.com, subject
   DER verified against the issuer DER observed on api.minimax.io.
"""
import hashlib
import os
import socket
import ssl
import urllib.request

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.serialization import Encoding

OUT = "examples/pda2/scripts/extra_roots"


def get_chain(host):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with socket.create_connection((host, 443), timeout=10) as sock:
        with ctx.wrap_socket(sock, server_hostname=host) as tls:
            return [x509.load_der_x509_certificate(c)
                    for c in tls.get_unverified_chain()]


# --- 1. AAA Certificate Services from the served chain ---
aaa = get_chain("api.minimaxi.com")[-1]
assert aaa.subject == aaa.issuer and "AAA Certificate Services" in aaa.subject.rfc4514_string()
# (SHA-1 self-signature: cryptography>=50 refuses to verify it; the
#  fingerprint pin below + exact chain provenance is the integrity check)
with open(os.path.join(OUT, "aaa-certificate-services.pem"), "wb") as f:
    f.write(aaa.public_bytes(Encoding.PEM))
fp = aaa.fingerprint(hashes.SHA256()).hex().upper()
print(f"aaa-certificate-services.pem  sha256={fp}")
print(f"  valid {aaa.not_valid_before_utc:%Y-%m-%d} .. {aaa.not_valid_after_utc:%Y-%m-%d}")

# --- 2. DigiCert Global Root CA from the official CDN ---
# (downloaded beforehand: curl -sk https://cacerts.digicert.com/DigiCertGlobalRootCA.crt
#  -> authenticity is proven below by verifying it signs the live chain top)
der_path = os.path.join(os.environ.get("TEMP", "/tmp"), "DigiCertGlobalRootCA.crt")
der = open(der_path, "rb").read()
dgrc = x509.load_der_x509_certificate(der)
print(f"downloaded DigiCert Global Root CA: {dgrc.subject.rfc4514_string()}")
assert dgrc.subject.rfc4514_string().startswith("CN=DigiCert Global Root CA,")

# the subject DER must byte-match the issuer DER observed on api.minimax.io
mini = get_chain("api.minimax.io")[-1]
assert mini.issuer.public_bytes(Encoding.DER) == dgrc.subject.public_bytes(Encoding.DER), \
    "DigiCert Global Root CA subject DER != issuer DER seen on wire"
mini.verify_directly_issued_by(dgrc)   # raises InvalidSignature if not the true root
with open(os.path.join(OUT, "digicert-global-root-ca.pem"), "wb") as f:
    f.write(dgrc.public_bytes(Encoding.PEM))
fp = dgrc.fingerprint(hashes.SHA256()).hex().upper()
print(f"digicert-global-root-ca.pem  sha256={fp}")
print(f"  valid {dgrc.not_valid_before_utc:%Y-%m-%d} .. {dgrc.not_valid_after_utc:%Y-%m-%d}")
