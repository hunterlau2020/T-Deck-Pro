# test_GPS

`test_GPS` is a standalone GPS diagnostic example for T-Deck-Pro.

This sketch only tests the GPS module and tries to reduce interference from other peripherals.
It is intended for problems such as:

- GPS does not respond after flashing firmware
- The device reports `GPS Connect failed~!`
- Other firmware reports `gps is not on`
- You need a serial log that clearly shows whether the problem is power, UART, baud rate, or satellite fix

## What This Example Does

After boot, the sketch will:

1. Release possible GPIO hold states left by previous firmware
2. Disable non-GPS peripherals that may affect diagnosis
3. Power-cycle the GPS module through `BOARD_GPS_EN`
4. Probe multiple common baud rates automatically
5. Try UBX polling commands such as `MON-VER` and `CFG-RATE`
6. Print periodic status logs
7. Parse NMEA using `TinyGPS++`

## How To Use

### PlatformIO

Edit [platformio.ini](../../platformio.ini) and switch `src_dir` to:

```ini
src_dir = examples/test_GPS
```

Then build and flash as usual.

### Serial Monitor

Open the serial monitor at:

```text
115200
```

After boot, you should see a banner like:

```text
========== T-Deck-Pro GPS Standalone Test ==========
```

## Serial Commands

You can type these commands in the serial monitor:

| Command | Description |
| --- | --- |
| `h` / `?` | Show help |
| `s` | Print current GPS status |
| `r` | Re-initialize GPS and rescan baud rates |
| `p` | Toggle raw GPS UART output |
| `v` | Poll UBX `MON-VER` |
| `u` | Poll UBX `CFG-RATE` |
| `f` | Run UBX recovery / clear sequence |

## How To Read The Log

### Case 1: GPS module responds normally

You may see:

```text
[GPS][SCAN] module responded at 38400 baud
[GPS][INIT] success, active baud=38400, ubx=yes
```

This means:

- GPS power is present
- UART communication is working
- The module is alive

If there is still no location fix after this, the problem is more likely antenna, signal environment, or GNSS configuration rather than complete hardware failure.

### Case 2: GPS sends NMEA but UBX probe does not respond

You may see:

```text
[GPS][SCAN] accepted 9600 baud based on NMEA traffic
```

This means:

- The module is sending readable GPS data
- UART is alive
- UBX probing may not be available or may be affected by current module state

### Case 3: No UART traffic at all

You may see:

```text
[GPS][INIT] failed: no UART response detected on candidate baud rates
[GPS][WARN] No UART bytes received from the GPS module after init.
```

This usually points to one of these:

- GPS power is not actually reaching the module
- The GPS enable pin state is wrong
- UART RX/TX path is not working
- GPIO hold from previous firmware was not fully cleared
- The GPS module itself is damaged or not responding

### Case 4: GPS is alive but has no fix

You may see status logs and NMEA logs, but location remains:

```text
[GPS][FIX] Location=INVALID
```

This means the module is alive, but no valid position has been acquired yet.
At that point, test outdoors with a clear sky view before concluding there is a hardware fault.

## Recommended Diagnostic Flow

1. Flash this example
2. Open serial monitor at `115200`
3. Wait for the initial baud scan result
4. If needed, run `s`
5. If needed, run `r`
6. If the module responds, leave it powered outdoors and wait for fix logs
7. If the module never produces UART bytes, save the full log and compare with hardware power / wiring state

## Notes

- This example is meant for diagnosis, not for normal product use
- If another firmware previously put GPIOs into hold or deep sleep state, a full power removal may still be helpful
- For recovery testing, disconnect USB and battery completely before retrying if behavior looks abnormal

