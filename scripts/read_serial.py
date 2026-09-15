"""Plain serial read (no DTR/RTS pulsing) - capture boot output without
touching the reset lines."""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 60

# do not assert DTR/RTS: on the S3 native USB-JTAG-Serial they map to
# EN/IO0 strapping and an assert here can re-enter download mode
s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = None if False else s.dtr  # keep default state untouched
s.open()
t0 = time.time()
buf = b""
while time.time() - t0 < secs:
    buf += s.read(4096)
s.close()
sys.stdout.buffer.write(buf)
