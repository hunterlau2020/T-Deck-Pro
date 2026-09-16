#!/usr/bin/env python3
"""Chunked flash with MANDATORY per-chunk hash verification.

WHY THIS EXISTS (2026-09-16 incident, two devices bricked into a silent
bootloader reset loop): the T-Deck-Pro native-USB CDC link drops the port
mid-write after a sustained transfer (~200-500 KB, reproducible), and a
naive "flash and trust esptool's tail output" retry loop can leave one
512 KB chunk unwritten. The bootloader then rejects the app image (hash
mismatch) BEFORE the app starts - symptom: rst:0x3 loop every ~0.4 s,
"entry 0x403c98d0" (that's the *bootloader* entry, not the app), zero
application console output. Recovery: reflash with every chunk actually
verified, which is what this script enforces.

Usage:
  python scripts/flash_verified.py COM5 .pio/build/pda2/firmware.bin
Options:
  --chunk-kb 512   chunk size (512 K worked on all three devices)
  --baud 460800    921600 raises the CDC-drop rate for no speed gain
  --retries 6      per-chunk attempts; a chunk that never verifies aborts
                   the whole run (partial flash is the failure mode we
                   are defending against, so fail loudly, keep going
                   never)
  --readback       after flashing, read the whole region back and hash
                   it against the file (final belt-and-braces check)
Exit code 0 only when every chunk verified (and readback matched, if
requested).
"""
import argparse
import hashlib
import os
import subprocess
import sys

ESPTOOL = os.path.join(
    os.path.expanduser("~"), ".platformio", "packages",
    "tool-esptoolpy", "esptool.py")


def run_esptool(port, baud, *args):
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3",
           "--port", port, "--baud", str(baud),
           "--before", "default_reset", "--after", "no_reset"] + list(args)
    return subprocess.run(cmd, capture_output=True, text=True).stdout


def flash_chunk(port, baud, offset, path):
    out = run_esptool(port, baud, "write_flash", str(offset), path)
    return "Hash of data verified." in out, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("image")
    ap.add_argument("--offset", type=lambda x: int(x, 0), default=0x10000)
    ap.add_argument("--chunk-kb", type=int, default=512)
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--retries", type=int, default=6)
    ap.add_argument("--readback", action="store_true")
    args = ap.parse_args()

    data = open(args.image, "rb").read()
    chunk = args.chunk_kb * 1024
    print(f"{args.image}: {len(data)} bytes, chunks of {chunk} "
          f"@ 0x{args.offset:x} -> {args.port}")

    tmpdir = os.path.join(os.environ.get("TEMP", "/tmp"), "fwchunks")
    os.makedirs(tmpdir, exist_ok=True)

    n_chunks = (len(data) + chunk - 1) // chunk
    for i in range(n_chunks):
        off = args.offset + i * chunk
        piece = data[i * chunk:(i + 1) * chunk]
        p = os.path.join(tmpdir, f"chunk_{off:08x}.bin")
        with open(p, "wb") as f:
            f.write(piece)

        for attempt in range(1, args.retries + 1):
            ok, out = flash_chunk(args.port, args.baud, off, p)
            if ok:
                print(f"chunk {i + 1}/{n_chunks} @ 0x{off:x} VERIFIED")
                break
            tail = [l for l in out.splitlines() if l.strip()][-1] if out.strip() else "(no output)"
            print(f"chunk {i + 1}/{n_chunks} @ 0x{off:x} "
                  f"attempt {attempt} failed: {tail}")
        else:
            print(f"ABORT: chunk @ 0x{off:x} never verified after "
                  f"{args.retries} attempts. The flash image is INCOMPLETE - "
                  f"do NOT boot this device expecting it to run; rerun this "
                  f"script until it exits 0.")
            return 2

    if args.readback:
        rb_path = os.path.join(tmpdir, "readback.bin")
        run_esptool(args.port, args.baud,
                    "read_flash", str(args.offset), str(len(data)), rb_path)
        rb = open(rb_path, "rb").read()
        want = hashlib.md5(data).hexdigest()
        got = hashlib.md5(rb).hexdigest()
        if want != got:
            print(f"READBACK MISMATCH want={want} got={got} - rerun")
            return 3
        print(f"readback hash OK ({want})")

    # final: one clean run with hard_reset so the app actually boots
    cmd = [sys.executable, ESPTOOL, "--chip", "esp32s3", "--port", args.port,
           "--after", "hard_reset", "chip_id"]
    subprocess.run(cmd, capture_output=True, text=True)
    print("done - device hard-reset into the new image")
    return 0


if __name__ == "__main__":
    sys.exit(main())
