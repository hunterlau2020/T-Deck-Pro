#include <TinyGPS++.h>
#include <driver/gpio.h>
#include <ctype.h>
#include <string.h>

#define BOARD_GPS_EN  39
#define BOARD_GPS_RXD 44
#define BOARD_GPS_TXD 43
#define BOARD_GPS_PPS 1

#define BOARD_LORA_EN 46
#define BOARD_6609_EN 41
#define BOARD_1V8_EN  38

#define SerialMon Serial
#define SerialGPS Serial2

struct TrafficSnapshot {
    uint32_t bytes = 0;
    uint32_t printable = 0;
    uint32_t nmeaStarts = 0;
    bool sawDollar = false;
    bool sawUbxSync = false;
    char sample[96] = {0};
    size_t sampleLen = 0;
};

TinyGPSPlus gps;
uint8_t ubxBuffer[256];

static bool gps_online = false;
static bool gps_ubx_verified = false;
static bool gps_raw_dump_enabled = false;
static bool gps_warned_no_traffic = false;
static uint32_t gps_active_baud = 0;
static uint32_t gps_last_init_ms = 0;
static uint32_t gps_last_byte_ms = 0;
static uint32_t gps_last_nmea_ms = 0;
static uint32_t gps_last_fix_ms = 0;
static uint32_t gps_last_status_ms = 0;
static uint32_t gps_last_fix_log_ms = 0;
static uint32_t gps_init_attempts = 0;
static uint32_t gps_total_bytes = 0;
static uint32_t gps_total_printable = 0;
static uint32_t gps_total_nmea_lines = 0;
static uint32_t gps_total_fix_logs = 0;
static char gps_last_nmea_line[128] = {0};
static size_t gps_line_len = 0;

static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint32_t GPS_SCAN_WINDOW_MS = 350;
static constexpr uint32_t GPS_STATUS_PERIOD_MS = 5000;
static constexpr uint32_t GPS_NO_TRAFFIC_WARN_MS = 10000;
static constexpr uint32_t GPS_FIX_LOG_MIN_INTERVAL_MS = 2000;
static constexpr uint32_t GPS_BAUD_CANDIDATES[] = {
    38400,
    9600,
    115200,
    57600,
    19200,
    4800,
};

static void printHelp();
static void printDivider(const char *title);
static void clearPinHolds();
static void isolateGpsForTest();
static void gpsPower(bool on);
static void gpsPowerCycle();
static void gpsFlushInput();
static void resetRuntimeStats();
static void appendTrafficSample(TrafficSnapshot &snapshot, uint8_t value);
static TrafficSnapshot captureTraffic(uint32_t windowMs);
static void printTrafficSnapshot(uint32_t baud, const TrafficSnapshot &snapshot);
static int readUbxPayload(uint8_t expectedClass, uint8_t expectedId, uint8_t *buffer, uint16_t capacity, uint32_t timeoutMs);
static bool pollMonVer(bool verbose);
static bool pollCfgRate(bool verbose);
static bool runRecoverySequence(bool verbose);
static bool gpsTryBaud(uint32_t baud);
static bool gpsInit();
static void processGpsByte(uint8_t value);
static void handleConsole();
static void printStatus(const char *reason);
static void printFix();
static void printStartupBanner();

static void printDivider(const char *title)
{
    SerialMon.println();
    SerialMon.print(F("========== "));
    SerialMon.print(title);
    SerialMon.println(F(" =========="));
}

static void printHelp()
{
    printDivider("GPS Test Commands");
    SerialMon.println(F("h / ? : show this help"));
    SerialMon.println(F("s     : print current GPS status"));
    SerialMon.println(F("r     : power-cycle GPS and rescan baud rates"));
    SerialMon.println(F("p     : toggle raw GPS UART dump"));
    SerialMon.println(F("v     : poll UBX MON-VER"));
    SerialMon.println(F("u     : poll UBX CFG-RATE"));
    SerialMon.println(F("f     : run UBX recovery / clear sequence"));
}

static void clearPinHolds()
{
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_dis((gpio_num_t)BOARD_6609_EN);
    gpio_hold_dis((gpio_num_t)BOARD_1V8_EN);
}

static void isolateGpsForTest()
{
    pinMode(BOARD_LORA_EN, OUTPUT);
    pinMode(BOARD_6609_EN, OUTPUT);
    pinMode(BOARD_1V8_EN, OUTPUT);
    pinMode(BOARD_GPS_EN, OUTPUT);
    pinMode(BOARD_GPS_PPS, INPUT);

    digitalWrite(BOARD_LORA_EN, LOW);
    digitalWrite(BOARD_6609_EN, LOW);
    digitalWrite(BOARD_1V8_EN, LOW);
    digitalWrite(BOARD_GPS_EN, HIGH);
}

static void gpsPower(bool on)
{
    digitalWrite(BOARD_GPS_EN, on ? HIGH : LOW);
    SerialMon.printf("[GPS][POWER] GPS_EN -> %s\n", on ? "HIGH" : "LOW");
}

static void gpsPowerCycle()
{
    printDivider("GPS Power Cycle");
    SerialGPS.end();
    gpsPower(false);
    delay(300);
    gpsPower(true);
    delay(600);
}

static void gpsFlushInput()
{
    while (SerialGPS.available()) {
        SerialGPS.read();
    }
}

static void resetRuntimeStats()
{
    gps_online = false;
    gps_ubx_verified = false;
    gps_warned_no_traffic = false;
    gps_active_baud = 0;
    gps_last_init_ms = millis();
    gps_last_byte_ms = 0;
    gps_last_nmea_ms = 0;
    gps_last_fix_ms = 0;
    gps_last_fix_log_ms = 0;
    gps_total_bytes = 0;
    gps_total_printable = 0;
    gps_total_nmea_lines = 0;
    gps_total_fix_logs = 0;
    gps_line_len = 0;
    gps_last_nmea_line[0] = '\0';
}

static void appendTrafficSample(TrafficSnapshot &snapshot, uint8_t value)
{
    if (snapshot.sampleLen >= (sizeof(snapshot.sample) - 1)) {
        return;
    }
    snapshot.sample[snapshot.sampleLen++] = isprint(static_cast<unsigned char>(value)) ? static_cast<char>(value) : '.';
    snapshot.sample[snapshot.sampleLen] = '\0';
}

static TrafficSnapshot captureTraffic(uint32_t windowMs)
{
    TrafficSnapshot snapshot;
    bool atLineStart = true;
    uint32_t start = millis();

    while (millis() - start < windowMs) {
        while (SerialGPS.available()) {
            uint8_t value = static_cast<uint8_t>(SerialGPS.read());
            snapshot.bytes++;

            if (isprint(static_cast<unsigned char>(value))) {
                snapshot.printable++;
            }
            if (value == '$') {
                snapshot.sawDollar = true;
                if (atLineStart) {
                    snapshot.nmeaStarts++;
                }
            }
            if (value == 0xB5) {
                snapshot.sawUbxSync = true;
            }

            appendTrafficSample(snapshot, value);
            atLineStart = (value == '\n' || value == '\r');
        }
        delay(10);
    }

    return snapshot;
}

static void printTrafficSnapshot(uint32_t baud, const TrafficSnapshot &snapshot)
{
    SerialMon.printf("[GPS][SCAN] baud=%lu bytes=%lu printable=%lu nmea=%lu ubxSync=%s sample=\"%s\"\n",
                     static_cast<unsigned long>(baud),
                     static_cast<unsigned long>(snapshot.bytes),
                     static_cast<unsigned long>(snapshot.printable),
                     static_cast<unsigned long>(snapshot.nmeaStarts),
                     snapshot.sawUbxSync ? "yes" : "no",
                     snapshot.sampleLen ? snapshot.sample : "<none>");
}

static int readUbxPayload(uint8_t expectedClass, uint8_t expectedId, uint8_t *buffer, uint16_t capacity, uint32_t timeoutMs)
{
    uint32_t start = millis();
    uint8_t state = 0;
    uint8_t messageClass = 0;
    uint8_t messageId = 0;
    uint16_t payloadLength = 0;
    uint16_t payloadIndex = 0;

    while (millis() - start < timeoutMs) {
        while (SerialGPS.available()) {
            uint8_t value = static_cast<uint8_t>(SerialGPS.read());

            switch (state) {
            case 0:
                state = (value == 0xB5) ? 1 : 0;
                break;
            case 1:
                state = (value == 0x62) ? 2 : 0;
                break;
            case 2:
                messageClass = value;
                state = 3;
                break;
            case 3:
                messageId = value;
                state = 4;
                break;
            case 4:
                payloadLength = value;
                state = 5;
                break;
            case 5:
                payloadLength |= static_cast<uint16_t>(value) << 8;
                if (payloadLength > capacity) {
                    state = 0;
                } else if (payloadLength == 0) {
                    state = 7;
                } else {
                    payloadIndex = 0;
                    state = 6;
                }
                break;
            case 6:
                buffer[payloadIndex++] = value;
                if (payloadIndex >= payloadLength) {
                    state = 7;
                }
                break;
            case 7:
                state = 8;
                break;
            case 8:
                if (messageClass == expectedClass && messageId == expectedId) {
                    return payloadLength;
                }
                state = 0;
                break;
            default:
                state = 0;
                break;
            }
        }
        delay(1);
    }

    return 0;
}

static void copyTrimmedField(const uint8_t *src, size_t srcLen, char *dst, size_t dstLen)
{
    size_t copyLen = 0;
    while (copyLen < srcLen && copyLen < (dstLen - 1) && src[copyLen] != '\0') {
        dst[copyLen] = static_cast<char>(src[copyLen]);
        copyLen++;
    }
    dst[copyLen] = '\0';

    while (copyLen > 0 && dst[copyLen - 1] == ' ') {
        dst[--copyLen] = '\0';
    }
}

static bool pollMonVer(bool verbose)
{
    static const uint8_t pollMonVerCmd[] = {0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34};

    gpsFlushInput();
    SerialGPS.write(pollMonVerCmd, sizeof(pollMonVerCmd));

    int length = readUbxPayload(0x0A, 0x04, ubxBuffer, sizeof(ubxBuffer), 800);
    if (length < 40) {
        if (verbose) {
            SerialMon.println(F("[GPS][UBX] MON-VER timeout"));
        }
        return false;
    }

    gps_ubx_verified = true;

    if (verbose) {
        char swVersion[31];
        char hwVersion[11];
        copyTrimmedField(ubxBuffer, 30, swVersion, sizeof(swVersion));
        copyTrimmedField(ubxBuffer + 30, 10, hwVersion, sizeof(hwVersion));
        SerialMon.printf("[GPS][UBX] MON-VER sw=\"%s\" hw=\"%s\" len=%d\n", swVersion, hwVersion, length);
    }
    return true;
}

static bool pollCfgRate(bool verbose)
{
    static const uint8_t pollCfgRateCmd[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};

    gpsFlushInput();
    SerialGPS.write(pollCfgRateCmd, sizeof(pollCfgRateCmd));

    int length = readUbxPayload(0x06, 0x08, ubxBuffer, sizeof(ubxBuffer), 800);
    if (length < 6) {
        if (verbose) {
            SerialMon.println(F("[GPS][UBX] CFG-RATE timeout"));
        }
        return false;
    }

    uint16_t measRate = static_cast<uint16_t>(ubxBuffer[0]) | (static_cast<uint16_t>(ubxBuffer[1]) << 8);
    uint16_t navRate = static_cast<uint16_t>(ubxBuffer[2]) | (static_cast<uint16_t>(ubxBuffer[3]) << 8);
    uint16_t timeRef = static_cast<uint16_t>(ubxBuffer[4]) | (static_cast<uint16_t>(ubxBuffer[5]) << 8);

    if (verbose) {
        SerialMon.printf("[GPS][UBX] CFG-RATE measRate=%u navRate=%u timeRef=%u\n", measRate, navRate, timeRef);
    }
    return true;
}

static bool sendExpectAck(const uint8_t *cmd, size_t len, const char *name)
{
    gpsFlushInput();
    SerialGPS.write(cmd, len);

    int ackLength = readUbxPayload(0x05, 0x01, ubxBuffer, sizeof(ubxBuffer), 800);
    bool ok = ackLength >= 2;

    SerialMon.printf("[GPS][RECOVERY] %s -> %s\n", name, ok ? "ACK" : "timeout");
    return ok;
}

static bool runRecoverySequence(bool verbose)
{
    static const uint8_t cfgClearRam[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1C, 0xA2};
    static const uint8_t cfgClearBbr[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1B, 0xA1};
    static const uint8_t cfgClearFlash[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1D, 0xB3};

    if (verbose) {
        printDivider("GPS Recovery");
    }

    bool gotAck = false;
    gotAck |= sendExpectAck(cfgClearRam, sizeof(cfgClearRam), "clear RAM");
    gotAck |= sendExpectAck(cfgClearBbr, sizeof(cfgClearBbr), "clear BBR");
    gotAck |= sendExpectAck(cfgClearFlash, sizeof(cfgClearFlash), "clear Flash");

    bool rateOk = pollCfgRate(verbose);
    return gotAck || rateOk;
}

static bool gpsTryBaud(uint32_t baud)
{
    SerialMon.printf("[GPS][SCAN] probing %lu baud\n", static_cast<unsigned long>(baud));

    SerialGPS.begin(baud, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    delay(50);

    gpsFlushInput();
    TrafficSnapshot snapshot = captureTraffic(GPS_SCAN_WINDOW_MS);
    printTrafficSnapshot(baud, snapshot);

    if (pollMonVer(true) || pollCfgRate(true)) {
        gps_active_baud = baud;
        gps_online = true;
        SerialMon.printf("[GPS][SCAN] module responded at %lu baud\n", static_cast<unsigned long>(baud));
        return true;
    }

    if (snapshot.bytes > 0 && snapshot.sawDollar) {
        gps_active_baud = baud;
        gps_online = true;
        SerialMon.printf("[GPS][SCAN] accepted %lu baud based on NMEA traffic\n", static_cast<unsigned long>(baud));
        return true;
    }

    SerialGPS.end();
    return false;
}

static bool gpsInit()
{
    resetRuntimeStats();
    gps_init_attempts++;

    printDivider("GPS Init");
    SerialMon.printf("[GPS][INIT] attempt=%lu\n", static_cast<unsigned long>(gps_init_attempts));
    gpsPowerCycle();

    for (size_t i = 0; i < (sizeof(GPS_BAUD_CANDIDATES) / sizeof(GPS_BAUD_CANDIDATES[0])); ++i) {
        if (gpsTryBaud(GPS_BAUD_CANDIDATES[i])) {
            SerialMon.printf("[GPS][INIT] success, active baud=%lu, ubx=%s\n",
                             static_cast<unsigned long>(gps_active_baud),
                             gps_ubx_verified ? "yes" : "no");
            return true;
        }
    }

    SerialMon.println(F("[GPS][INIT] failed: no UART response detected on candidate baud rates"));
    return false;
}

static void processGpsByte(uint8_t value)
{
    gps_total_bytes++;
    gps_last_byte_ms = millis();

    if (isprint(static_cast<unsigned char>(value))) {
        gps_total_printable++;
    }

    if (gps_raw_dump_enabled) {
        SerialMon.write(value);
    }

    if (value == '\r' || value == '\n') {
        if (gps_line_len > 0) {
            gps_last_nmea_line[gps_line_len] = '\0';
            if (gps_last_nmea_line[0] == '$') {
                gps_total_nmea_lines++;
                gps_last_nmea_ms = millis();

                if (gps_total_nmea_lines <= 3) {
                    SerialMon.printf("[GPS][NMEA] %s\n", gps_last_nmea_line);
                }
            }
            gps_line_len = 0;
        }
    } else if (gps_line_len < (sizeof(gps_last_nmea_line) - 1)) {
        gps_last_nmea_line[gps_line_len++] = static_cast<char>(value);
    }

    if (gps.encode(value) && millis() - gps_last_fix_log_ms >= GPS_FIX_LOG_MIN_INTERVAL_MS) {
        printFix();
    }
}

static void printFix()
{
    gps_last_fix_log_ms = millis();
    gps_total_fix_logs++;

    SerialMon.print(F("[GPS][FIX] Location="));
    if (gps.location.isValid()) {
        SerialMon.print(gps.location.lat(), 6);
        SerialMon.print(F(","));
        SerialMon.print(gps.location.lng(), 6);
        gps_last_fix_ms = millis();
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.print(F(" Date="));
    if (gps.date.isValid()) {
        SerialMon.printf("%02d/%02d/%04d", gps.date.month(), gps.date.day(), gps.date.year());
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.print(F(" Time="));
    if (gps.time.isValid()) {
        SerialMon.printf("%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.print(F(" Sats="));
    if (gps.satellites.isValid()) {
        SerialMon.print(gps.satellites.value());
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.print(F(" HDOP="));
    if (gps.hdop.isValid()) {
        SerialMon.print(gps.hdop.hdop());
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.print(F(" Speed(km/h)="));
    if (gps.speed.isValid()) {
        SerialMon.print(gps.speed.kmph(), 2);
    } else {
        SerialMon.print(F("INVALID"));
    }

    SerialMon.println();
}

static void printStatus(const char *reason)
{
    printDivider("GPS Status");
    if (reason != nullptr && reason[0] != '\0') {
        SerialMon.printf("[GPS][STAT] reason=%s\n", reason);
    }

    SerialMon.printf("[GPS][STAT] online=%s ubx=%s baud=%lu initAttempts=%lu uptime=%lu ms\n",
                     gps_online ? "yes" : "no",
                     gps_ubx_verified ? "yes" : "no",
                     static_cast<unsigned long>(gps_active_baud),
                     static_cast<unsigned long>(gps_init_attempts),
                     static_cast<unsigned long>(millis()));

    SerialMon.printf("[GPS][STAT] bytes=%lu printable=%lu nmeaLines=%lu charsProcessed=%lu\n",
                     static_cast<unsigned long>(gps_total_bytes),
                     static_cast<unsigned long>(gps_total_printable),
                     static_cast<unsigned long>(gps_total_nmea_lines),
                     static_cast<unsigned long>(gps.charsProcessed()));

    SerialMon.printf("[GPS][STAT] lastByteAgo=%lu ms lastNmeaAgo=%lu ms lastFixAgo=%lu ms GPS_EN=%d PPS=%d\n",
                     gps_last_byte_ms ? static_cast<unsigned long>(millis() - gps_last_byte_ms) : 0UL,
                     gps_last_nmea_ms ? static_cast<unsigned long>(millis() - gps_last_nmea_ms) : 0UL,
                     gps_last_fix_ms ? static_cast<unsigned long>(millis() - gps_last_fix_ms) : 0UL,
                     digitalRead(BOARD_GPS_EN),
                     digitalRead(BOARD_GPS_PPS));

    if (gps_last_nmea_line[0] != '\0') {
        SerialMon.printf("[GPS][STAT] lastNmea=\"%s\"\n", gps_last_nmea_line);
    } else {
        SerialMon.println(F("[GPS][STAT] lastNmea=<none>"));
    }
}

static void handleConsole()
{
    while (SerialMon.available()) {
        char cmd = static_cast<char>(SerialMon.read());
        if (cmd == '\r' || cmd == '\n') {
            continue;
        }

        switch (cmd) {
        case 'h':
        case '?':
            printHelp();
            break;
        case 's':
            printStatus("manual");
            break;
        case 'r':
            gpsInit();
            break;
        case 'p':
            gps_raw_dump_enabled = !gps_raw_dump_enabled;
            SerialMon.printf("[GPS][CMD] raw dump -> %s\n", gps_raw_dump_enabled ? "ON" : "OFF");
            break;
        case 'v':
            if (gps_active_baud == 0) {
                SerialMon.println(F("[GPS][CMD] no active baud, run 'r' first"));
            } else {
                pollMonVer(true);
            }
            break;
        case 'u':
            if (gps_active_baud == 0) {
                SerialMon.println(F("[GPS][CMD] no active baud, run 'r' first"));
            } else {
                pollCfgRate(true);
            }
            break;
        case 'f':
            if (gps_active_baud == 0) {
                SerialMon.println(F("[GPS][CMD] no active baud, run 'r' first"));
            } else {
                runRecoverySequence(true);
            }
            break;
        default:
            SerialMon.printf("[GPS][CMD] unknown command '%c'\n", cmd);
            printHelp();
            break;
        }
    }
}

static void printStartupBanner()
{
    printDivider("T-Deck-Pro GPS Standalone Test");
    SerialMon.println(F("[GPS][BOOT] This sketch only tests the GPS module."));
    SerialMon.println(F("[GPS][BOOT] Non-GPS peripherals are held disabled to reduce interference."));
    SerialMon.printf("[GPS][BOOT] GPS pins: EN=%d TX=%d RX=%d PPS=%d\n",
                     BOARD_GPS_EN, BOARD_GPS_TXD, BOARD_GPS_RXD, BOARD_GPS_PPS);
    printHelp();
}

void setup(void)
{
    SerialMon.begin(SERIAL_BAUD);
    delay(200);

    clearPinHolds();
    isolateGpsForTest();
    printStartupBanner();
    gpsInit();
}

void loop(void)
{
    handleConsole();

    while (SerialGPS.available()) {
        processGpsByte(static_cast<uint8_t>(SerialGPS.read()));
    }

    if (millis() - gps_last_status_ms >= GPS_STATUS_PERIOD_MS) {
        gps_last_status_ms = millis();
        printStatus("periodic");
    }

    if (!gps_warned_no_traffic && gps_last_init_ms != 0 && millis() - gps_last_init_ms >= GPS_NO_TRAFFIC_WARN_MS && gps_total_bytes == 0) {
        gps_warned_no_traffic = true;
        SerialMon.println(F("[GPS][WARN] No UART bytes received from the GPS module after init."));
        SerialMon.println(F("[GPS][WARN] This usually means GPS power, UART wiring, pin hold, or the module itself is not responding."));
    }

    delay(10);
}
