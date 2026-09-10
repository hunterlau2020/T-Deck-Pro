"""Chunked full-flash backup for the flaky-CDC COM port.

Reads the whole 16MB flash in 512KB chunks with per-chunk retries (the
USB-CDC drop documented in docs/build-and-code-structure.md pitfall 6 hits
long transfers; per-chunk retries are idempotent because each chunk is
re-read from the same address and written at its file offset).

Usage: python script/flash_backup.py <PORT> <outdir>
Creates <outdir>/full_16m.bin (16MB) and prints its MD5.
"""
import hashlib
import os
import subprocess
import sys

ESPTOOL = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")

FLASH_SIZE = 0x1000000          # 16MB
CHUNK = 0x80000                 # 512KB
PORT = sys.argv[1]
OUTDIR = sys.argv[2]

os.makedirs(OUTDIR, exist_ok=True)
out_path = os.path.join(OUTDIR, "full_16m.bin")

# Preallocate so failed runs leave a full-size file with zero holes.
with open(out_path, "wb") as f:
    f.write(b"\xff" * FLASH_SIZE)

total = FLASH_SIZE // CHUNK
for idx in range(total):
    addr = idx * CHUNK
    tmp = os.path.join(OUTDIR, "chunk.tmp")
    if os.path.exists(tmp):
        os.remove(tmp)
    ok = False
    for attempt in range(1, 6):
        cmd = [
            sys.executable, ESPTOOL,
            "--chip", "esp32s3", "--port", PORT, "--baud", "921600",
            "--before", "default_reset", "--after", "no_reset",
            "read_flash", str(addr), str(CHUNK), tmp,
        ]
        r = subprocess.run(cmd, capture_output=True, text=True)
        got = os.path.getsize(tmp) if os.path.exists(tmp) else 0
        print("chunk %2d/%d @ 0x%06X try %d rc=%d got=%d" %
              (idx + 1, total, addr, attempt, r.returncode, got), flush=True)
        if r.returncode == 0 and got == CHUNK:
            ok = True
            break
    if not ok:
        print("FAILED at chunk %d (0x%06X)" % (idx, addr))
        sys.exit(1)
    with open(out_path, "r+b") as f:
        f.seek(addr)
        f.write(open(tmp, "rb").read())
    os.remove(tmp)

md5 = hashlib.md5(open(out_path, "rb").read()).hexdigest()
size = os.path.getsize(out_path)
print("DONE %s size=%d md5=%s" % (out_path, size, md5))
