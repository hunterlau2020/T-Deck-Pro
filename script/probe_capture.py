"""Capture a base64 WAV dump from the test_pdm_mic probe over serial.

Sends 'D' to the probe, reads until [/DUMP], decodes and writes the WAV.
Usage: python script/probe_capture.py <PORT> <out.wav>
"""
import base64
import sys
import time

import serial

PORT = sys.argv[1]
OUT = sys.argv[2]

# Open WITHOUT asserting DTR/RTS: a normal open resets the device (USB-CDC)
# and the auto-take would overwrite the take we are trying to export.
ser = serial.Serial()
ser.port = PORT
ser.baudrate = 115200
ser.timeout = 5
ser.dtr = False
ser.rts = False
ser.open()
time.sleep(1.5)
ser.reset_input_buffer()

# If a boot banner shows up anyway, the take in memory has been replaced by
# a fresh auto-take - wait for it to complete before dumping.
saw_boot = False
t0 = time.time()
while time.time() - t0 < 2.5:
    raw = ser.readline()
    if raw and b"PDM mic probe" in raw:
        saw_boot = True
if saw_boot:
    print("device reset on open - waiting for the fresh take to finish")
    deadline = time.time() + 90
    while time.time() < deadline:
        raw = ser.readline()
        if raw and b"TAKE COMPLETE" in raw:
            break
def attempt_dump(ser):
    """One D round-trip; returns decoded bytes or None on corruption."""
    ser.write(b"D")
    ser.flush()
    lines = []
    size = None
    deadline = time.time() + 120
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode("ascii", "ignore").strip()
        if line.startswith("[DUMP]"):
            size = int(line.split()[1])
            lines = []
            continue
        if line.startswith("[/DUMP]"):
            break
        if size is not None and line:
            lines.append(line)
    if size is None:
        print("no dump header")
        return None
    joined = "".join(lines)
    # the probe emits unpadded base64 (no trailing '='), python needs the pad
    if len(joined) % 4:
        joined += "=" * (-len(joined) % 4)
    try:
        data = base64.b64decode(joined, validate=True)
    except Exception as e:
        print("decode failed:", e, "(%d chars)" % len(joined))
        return None
    if len(data) != size:
        print("size mismatch: %d != %d" % (len(data), size))
        return None
    return data


data = None
size_seen = None
for attempt in range(5):
    print("attempt", attempt + 1)
    data = attempt_dump(ser)
    if data:
        break
    time.sleep(1)

ser.close()
if not data:
    print("FAILED after retries")
    sys.exit(1)

print("decoded:", len(data), "bytes")
with open(OUT, "wb") as f:
    f.write(data)
print("wrote", OUT)
