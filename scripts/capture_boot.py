"""Reset the board over the auto-reset circuit (DTR/RTS pulse, esptool
style) and capture N seconds of boot output to spot where the EPD busy
timeouts start relative to the OTA WDT window messages."""
import sys
import time

import serial  # pyserial (platformio bundles it)

port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 35

# classic esptool reset: DTR=False RTS=True (EN low), then swap
s = serial.Serial(port, 115200, timeout=0.2)
s.setDTR(False)
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
time.sleep(0.05)
s.setDTR(False)

t0 = time.time()
buf = b""
while time.time() - t0 < secs:
    buf += s.read(4096)
s.close()
sys.stdout.buffer.write(buf)
