/**
 * @file      test_bluetooth.ino
 * @brief     T-Deck-Pro BLE connection test with on-screen result.
 *
 * ESP32-S3 only supports BLE, so this example exposes a BLE GATT server.
 * Use a phone app such as nRF Connect / LightBlue to:
 * 1. Search and connect to "T-Deck-Pro-BLE"
 * 2. Write any text to the characteristic
 * 3. Check the EPD screen for CONNECT / DATA PASS
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <string>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <GxEPD2_BW.h>
#include "utilities.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error BLE is not enabled in this build.
#endif

using InkPanel = GxEPD2_310_GDEQ031T10;
using InkDisplay = GxEPD2_BW<InkPanel, InkPanel::HEIGHT>;

static constexpr int16_t BOARD_EPD_RST_UNUSED = -1;
static constexpr uint32_t EPD_SPI_HZ = 2000000;
static constexpr char BLE_DEVICE_NAME[] = "T-Deck-Pro-BLE";
static constexpr char BLE_SERVICE_UUID[] = "12345678-1234-5678-1234-56789abcdef0";
static constexpr char BLE_CHAR_UUID[] = "12345678-1234-5678-1234-56789abcdef1";

InkDisplay display_v1_1(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY));
InkDisplay display_v1_0(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST_UNUSED, BOARD_EPD_BUSY));
InkDisplay *display = &display_v1_1;

BLEServer *bleServer = nullptr;
BLECharacteristic *bleCharacteristic = nullptr;

struct BleUiState {
    bool board_v1_1 = false;
    bool ble_ready = false;
    bool connected = false;
    bool advertising = false;
    bool had_connection = false;
    bool had_write = false;
    bool pending_restart_adv = false;
    bool screen_dirty = true;
    uint32_t connection_count = 0;
    uint32_t write_count = 0;
    char last_rx[64] = "<none>";
    char last_event[48] = "Boot";
};

static portMUX_TYPE ble_state_mux = portMUX_INITIALIZER_UNLOCKED;
static BleUiState ble_state;

static void mark_screen_dirty()
{
    portENTER_CRITICAL(&ble_state_mux);
    ble_state.screen_dirty = true;
    portEXIT_CRITICAL(&ble_state_mux);
}

static void sanitize_text(const std::string &input, char *output, size_t output_size)
{
    if (output_size == 0) {
        return;
    }

    size_t j = 0;
    for (size_t i = 0; i < input.size() && j < output_size - 1; ++i) {
        const uint8_t c = static_cast<uint8_t>(input[i]);
        if (c == '\r' || c == '\n' || c == '\t') {
            if (j > 0 && output[j - 1] != ' ') {
                output[j++] = ' ';
            }
            continue;
        }
        output[j++] = (c >= 32 && c <= 126) ? static_cast<char>(c) : '.';
    }

    output[j] = '\0';
    if (j == 0) {
        strlcpy(output, "<empty>", output_size);
    }
}

static bool take_ui_snapshot(BleUiState &snapshot)
{
    bool dirty = false;

    portENTER_CRITICAL(&ble_state_mux);
    dirty = ble_state.screen_dirty;
    if (dirty) {
        snapshot = ble_state;
        ble_state.screen_dirty = false;
    }
    portEXIT_CRITICAL(&ble_state_mux);

    return dirty;
}

static void select_ink_display(bool board_v1_1)
{
    display = board_v1_1 ? &display_v1_1 : &display_v1_0;
}

static bool probe_i2c_address(uint8_t address)
{
    for (int i = 0; i < 3; ++i) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            return true;
        }
        delay(10);
    }
    return false;
}

static void init_board_pins()
{
    gpio_hold_dis((gpio_num_t)BOARD_6609_EN);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_dis((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_dis((gpio_num_t)BOARD_A7682E_PWRKEY);
    gpio_deep_sleep_hold_dis();

    pinMode(BOARD_KEYBOARD_LED, OUTPUT);
    digitalWrite(BOARD_KEYBOARD_LED, LOW);

    pinMode(BOARD_EPD_BL, OUTPUT);
    digitalWrite(BOARD_EPD_BL, HIGH);

    pinMode(BOARD_LORA_CS, OUTPUT);
    digitalWrite(BOARD_LORA_CS, HIGH);
    pinMode(BOARD_LORA_RST, OUTPUT);
    digitalWrite(BOARD_LORA_RST, HIGH);
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);
    pinMode(BOARD_EPD_CS, OUTPUT);
    digitalWrite(BOARD_EPD_CS, HIGH);
}

static void init_display()
{
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    display->epd2.selectSPI(SPI, SPISettings(EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    display->init(115200, true, 2, false);
    display->setRotation(0);
    display->setTextColor(GxEPD_BLACK);
    display->setTextWrap(false);
}

static void draw_centered_text_in_box(int16_t x, int16_t y, int16_t w, int16_t h, const char *text, uint8_t text_size, uint16_t text_color)
{
    int16_t tbx, tby;
    uint16_t tbw, tbh;

    display->setTextSize(text_size);
    display->setTextColor(text_color);
    display->getTextBounds(text, 0, 0, &tbx, &tby, &tbw, &tbh);

    const int16_t text_x = x + ((w - static_cast<int16_t>(tbw)) / 2) - tbx;
    const int16_t text_y = y + ((h - static_cast<int16_t>(tbh)) / 2) - tby;
    display->setCursor(text_x, text_y);
    display->print(text);
}

static void draw_status_banner(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, const char *value, bool active)
{
    static constexpr int16_t header_h = 16;

    display->fillRect(x, y, w, header_h, GxEPD_BLACK);
    if (active) {
        display->fillRect(x, y + header_h, w, h - header_h, GxEPD_BLACK);
    } else {
        display->fillRect(x, y + header_h, w, h - header_h, GxEPD_WHITE);
    }
    display->drawRect(x, y, w, h, GxEPD_BLACK);

    display->setTextSize(1);
    display->setTextColor(GxEPD_WHITE, GxEPD_BLACK);
    display->setCursor(x + 6, y + 4);
    display->print(label);

    draw_centered_text_in_box(x + 2, y + header_h + 2, w - 4, h - header_h - 4, value, 2, active ? GxEPD_WHITE : GxEPD_BLACK);
    display->setTextColor(GxEPD_BLACK);
    display->setTextSize(1);
}

static void draw_wrapped_rx_text(const char *text, int16_t x, int16_t y)
{
    static constexpr size_t MAX_LINES = 3;
    static constexpr size_t CHARS_PER_LINE = 16;

    char lines[MAX_LINES][CHARS_PER_LINE + 1] = {};
    const size_t len = strlen(text);
    size_t cursor = 0;

    for (size_t line = 0; line < MAX_LINES && cursor < len; ++line) {
        while (cursor < len && text[cursor] == ' ') {
            cursor++;
        }
        if (cursor >= len) {
            break;
        }

        size_t end = cursor + CHARS_PER_LINE;
        if (end > len) {
            end = len;
        } else {
            size_t backtrack = end;
            while (backtrack > cursor && text[backtrack - 1] != ' ') {
                backtrack--;
            }
            if (backtrack > cursor) {
                end = backtrack;
            }
        }

        size_t segment_len = end - cursor;
        if (segment_len > CHARS_PER_LINE) {
            segment_len = CHARS_PER_LINE;
        }

        memcpy(lines[line], text + cursor, segment_len);
        lines[line][segment_len] = '\0';

        while (segment_len > 0 && lines[line][segment_len - 1] == ' ') {
            lines[line][--segment_len] = '\0';
        }

        cursor = end;
    }

    if (cursor < len) {
        size_t last_line_len = strlen(lines[MAX_LINES - 1]);
        if (last_line_len > CHARS_PER_LINE - 3) {
            last_line_len = CHARS_PER_LINE - 3;
            lines[MAX_LINES - 1][last_line_len] = '\0';
        }
        strlcat(lines[MAX_LINES - 1], "...", sizeof(lines[MAX_LINES - 1]));
    }

    display->setTextSize(2);
    display->setTextColor(GxEPD_BLACK);
    for (size_t line = 0; line < MAX_LINES; ++line) {
        if (lines[line][0] == '\0') {
            continue;
        }
        display->setCursor(x, y + static_cast<int16_t>(line) * 20);
        display->print(lines[line]);
    }
    display->setTextSize(1);
}

static void draw_screen(const BleUiState &state)
{
    const char *conn_text = state.connected ? "CONNECTED" : (state.had_connection ? "DISCONNECTED" : "WAITING");
    const bool conn_active = state.connected;
    const char *data_text = state.had_write ? "DATA PASS" : "DATA WAIT";

    display->setFullWindow();
    display->firstPage();
    do {
        display->fillScreen(GxEPD_WHITE);
        display->fillRect(0, 0, LCD_HOR_SIZE, 28, GxEPD_BLACK);
        display->setTextSize(2);
        display->setTextColor(GxEPD_WHITE, GxEPD_BLACK);
        display->setCursor(10, 6);
        display->print("BLE TEST");

        display->setTextSize(1);
        display->setTextColor(GxEPD_BLACK);
        display->setCursor(8, 36);
        display->print("Board: ");
        display->println(state.board_v1_1 ? "T-Deck-Pro V1.1" : "T-Deck-Pro V1.0");
        display->setCursor(8, 48);
        display->print("BLE  : ");
        display->println(state.ble_ready ? "READY" : "INIT FAIL");
        display->setCursor(132, 48);
        display->print("Adv: ");
        display->println(state.advertising ? "ON" : "OFF");
        display->setCursor(8, 60);
        display->print("Name : ");
        display->println(BLE_DEVICE_NAME);

        draw_status_banner(8, 72, 224, 42, "CONNECTION", conn_text, conn_active);
        draw_status_banner(8, 122, 224, 42, "RECEIVE RESULT", data_text, state.had_write);

        display->drawRect(8, 176, 224, 88, GxEPD_BLACK);
        display->fillRect(8, 176, 224, 18, GxEPD_BLACK);
        display->setTextColor(GxEPD_WHITE, GxEPD_BLACK);
        display->setTextSize(1);
        display->setCursor(14, 181);
        display->print("LAST RECEIVED TEXT");

        display->setTextColor(GxEPD_BLACK);
        draw_wrapped_rx_text(state.last_rx, 16, 204);

        display->setCursor(8, 274);
        display->setTextSize(1);
        display->print("Conn=");
        display->print(state.connection_count);
        display->print("  Write=");
        display->print(state.write_count);
        display->setCursor(8, 288);
        display->print("Evt: ");
        display->println(state.last_event);

        display->setCursor(8, 302);
        display->print("Phone: connect, then write text.");
    } while (display->nextPage());

    display->powerOff();
}

static void restart_advertising_if_needed()
{
    bool should_restart = false;

    portENTER_CRITICAL(&ble_state_mux);
    if (ble_state.pending_restart_adv) {
        ble_state.pending_restart_adv = false;
        should_restart = true;
    }
    portEXIT_CRITICAL(&ble_state_mux);

    if (!should_restart) {
        return;
    }

    delay(200);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising restarted");

    portENTER_CRITICAL(&ble_state_mux);
    ble_state.advertising = true;
    strlcpy(ble_state.last_event, "Advertising restarted", sizeof(ble_state.last_event));
    ble_state.screen_dirty = true;
    portEXIT_CRITICAL(&ble_state_mux);
}

class TestServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *server) override
    {
        (void)server;
        Serial.println("[BLE] Client connected");

        portENTER_CRITICAL(&ble_state_mux);
        ble_state.connected = true;
        ble_state.advertising = false;
        ble_state.had_connection = true;
        ble_state.connection_count++;
        strlcpy(ble_state.last_event, "Client connected", sizeof(ble_state.last_event));
        ble_state.screen_dirty = true;
        portEXIT_CRITICAL(&ble_state_mux);
    }

    void onDisconnect(BLEServer *server) override
    {
        (void)server;
        Serial.println("[BLE] Client disconnected");

        portENTER_CRITICAL(&ble_state_mux);
        ble_state.connected = false;
        ble_state.advertising = false;
        ble_state.pending_restart_adv = true;
        strlcpy(ble_state.last_event, "Client disconnected", sizeof(ble_state.last_event));
        ble_state.screen_dirty = true;
        portEXIT_CRITICAL(&ble_state_mux);
    }
};

class TestCharacteristicCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override
    {
        const std::string value = characteristic->getValue();
        char sanitized[sizeof(ble_state.last_rx)];
        sanitize_text(value, sanitized, sizeof(sanitized));

        Serial.printf("[BLE] RX: %s\n", sanitized);

        const std::string echo = std::string("Echo: ") + sanitized;
        characteristic->setValue(echo);
        characteristic->notify();

        portENTER_CRITICAL(&ble_state_mux);
        ble_state.had_write = true;
        ble_state.write_count++;
        strlcpy(ble_state.last_rx, sanitized, sizeof(ble_state.last_rx));
        strlcpy(ble_state.last_event, "Data received", sizeof(ble_state.last_event));
        ble_state.screen_dirty = true;
        portEXIT_CRITICAL(&ble_state_mux);
    }
};

static void init_ble_server()
{
    BLEDevice::init(BLE_DEVICE_NAME);

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new TestServerCallbacks());

    BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
    bleCharacteristic = service->createCharacteristic(
        BLE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    bleCharacteristic->addDescriptor(new BLE2902());
    bleCharacteristic->setCallbacks(new TestCharacteristicCallbacks());
    bleCharacteristic->setValue("Write text here");

    service->start();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);

    BLEDevice::startAdvertising();
    Serial.printf("[BLE] Advertising started: %s\n", BLE_DEVICE_NAME);

    portENTER_CRITICAL(&ble_state_mux);
    ble_state.ble_ready = true;
    ble_state.advertising = true;
    strlcpy(ble_state.last_event, "Advertising started", sizeof(ble_state.last_event));
    ble_state.screen_dirty = true;
    portEXIT_CRITICAL(&ble_state_mux);
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[BOOT] BLE test example");

    init_board_pins();

    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    ble_state.board_v1_1 = probe_i2c_address(BOARD_I2C_ADDR_DRV2605);
    select_ink_display(ble_state.board_v1_1);
    init_display();

    Serial.printf("[BOARD] Detected %s\n", ble_state.board_v1_1 ? "T-Deck-Pro V1.1" : "T-Deck-Pro V1.0");

    init_ble_server();
    mark_screen_dirty();
}

void loop()
{
    restart_advertising_if_needed();

    BleUiState snapshot;
    if (take_ui_snapshot(snapshot)) {
        draw_screen(snapshot);
    }

    delay(20);
}
