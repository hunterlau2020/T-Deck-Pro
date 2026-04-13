/**
 * @file      pda.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * PDA application for T-Deck Pro v1.1
 * Ported from LilyGoLib/examples/pda/ to T-Deck Pro hardware.
 * Combines factory init sequence with PDA app flow.
 */

#include <Arduino.h>
#include "utilities.h"
#include <GxEPD2_BW.h>
#include <TouchDrvCSTXXX.hpp>
#include <TinyGPS++.h>
#include "lvgl.h"
#include <Fonts/FreeMonoBold9pt7b.h>
#include "factory.h"
#include "peripheral.h"
#include <Preferences.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include "hal_interface.h"
#include "event_define.h"
#include "ui_define.h"

extern void setupGui();

/*********************************************************************************
 *                              GLOBALS
 * *******************************************************************************/

Adafruit_DRV2605 drv;
Preferences preferences;

TinyGsm modem(SerialAT);
TaskHandle_t a7682_handle;

XPowersPPM PPM;
BQ27220 bq27220;
Audio audio;

static constexpr uint16_t FACTORY_BATTERY_DESIGN_CAPACITY_MAH = 1400;
static constexpr uint16_t FACTORY_BQ25896_CHARGE_TARGET_MV = 4208;
static constexpr uint16_t FACTORY_BQ25896_FAST_CHARGE_MA = 512;
static constexpr uint16_t FACTORY_BQ25896_PRECHARGE_MA = 128;
static constexpr uint16_t FACTORY_BQ25896_TERMINATION_MA = 128;
static constexpr uint16_t FACTORY_BQ25896_INPUT_LIMIT_MA = 1000;
static constexpr uint16_t FACTORY_BQ25896_SYS_POWER_DOWN_MV = 3300;
static constexpr uint32_t FACTORY_BQ25896_RUNTIME_CHECK_MS = 5000;
static constexpr uint32_t FACTORY_BQ25896_RECOVERY_COOLDOWN_MS = 30000;
static constexpr uint32_t FACTORY_EPD_SPI_HZ = 2000000;

using InkPanel = GxEPD2_310_GDEQ031T10;
using InkDisplay = GxEPD2_BW<InkPanel, InkPanel::HEIGHT>;
static constexpr int16_t BOARD_EPD_RST_UNUSED = -1;

InkDisplay display_v1_1(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST, BOARD_EPD_BUSY));
InkDisplay display_v1_0(InkPanel(BOARD_EPD_CS, BOARD_EPD_DC, BOARD_EPD_RST_UNUSED, BOARD_EPD_BUSY));
InkDisplay *display = &display_v1_1;

uint8_t *decodebuffer = NULL;
lv_timer_t *flush_timer = NULL;
int disp_refr_mode = DISP_REFR_MODE_PART;

uint8_t isT_Deck_Pro_v1_1 = 0;
const char Version_str1[] = "T-Deck-Pro V1.0";
const char Version_str2[] = "T-Deck-Pro V1.1";

bool peri_init_st[E_PERI_NUM_MAX] = {0};
static SemaphoreHandle_t shared_spi_mutex = nullptr;

/*********************************************************************************
 *                              NTP
 * *******************************************************************************/

static const char *ntpServer1 = "pool.ntp.org";
static const char *ntpServer2 = "time.nist.gov";
static const uint64_t gmtOffset_sec = GMT_OFFSET_SECOND;
static const int daylightOffset_sec = 0;

static void time_available(struct timeval *t)
{
    Serial.println("Got time adjustment from NTP!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info)
{
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(IPAddress(info.got_ip.ip_info.ip.addr));
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
}

/*********************************************************************************
 *                              SHARED SPI BUS
 * *******************************************************************************/

static void shared_spi_release_all_cs()
{
    digitalWrite(BOARD_LORA_CS, HIGH);
    digitalWrite(BOARD_SD_CS, HIGH);
    digitalWrite(BOARD_EPD_CS, HIGH);
}

void shared_spi_bus_init(void)
{
    if (shared_spi_mutex == nullptr) {
        shared_spi_mutex = xSemaphoreCreateRecursiveMutex();
        if (shared_spi_mutex == nullptr) {
            Serial.println("[SPI] Failed to create shared bus mutex");
            return;
        }
    }
    shared_spi_release_all_cs();
}

void shared_spi_lock(void)
{
    if (shared_spi_mutex == nullptr) {
        shared_spi_bus_init();
    }
    if (shared_spi_mutex != nullptr) {
        xSemaphoreTakeRecursive(shared_spi_mutex, portMAX_DELAY);
    }
}

void shared_spi_unlock(void)
{
    if (shared_spi_mutex != nullptr) {
        shared_spi_release_all_cs();
        xSemaphoreGiveRecursive(shared_spi_mutex);
    }
}

void shared_spi_prepare_device(int cs_pin)
{
    shared_spi_release_all_cs();
    if (cs_pin >= 0) {
        digitalWrite(cs_pin, HIGH);
    }
}

/*********************************************************************************
 *                              E-PAPER DISPLAY
 * *******************************************************************************/

static void select_ink_display()
{
    display = isT_Deck_Pro_v1_1 ? &display_v1_1 : &display_v1_0;
    Serial.printf("[EPD] reset mode: %s (RST=%d)\n",
                  isT_Deck_Pro_v1_1 ? "hardware" : "soft-only",
                  isT_Deck_Pro_v1_1 ? BOARD_EPD_RST : BOARD_EPD_RST_UNUSED);
}

static bool ink_screen_init()
{
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_EPD_CS);

    display->epd2.selectSPI(SPI, SPISettings(FACTORY_EPD_SPI_HZ, MSBFIRST, SPI_MODE0));
    display->init(115200, true, 2, false);
    display->setRotation(0);
    display->setFont(&FreeMonoBold9pt7b);
    if (display->epd2.WIDTH < 104) display->setFont(0);
    display->setTextColor(GxEPD_BLACK);
    int16_t tbx, tby; uint16_t tbw, tbh;
    if (isT_Deck_Pro_v1_1) {
        display->getTextBounds(Version_str2, 0, 0, &tbx, &tby, &tbw, &tbh);
    } else {
        display->getTextBounds(Version_str1, 0, 0, &tbx, &tby, &tbw, &tbh);
    }
    uint16_t x = ((display->width() - tbw) / 2) - tbx;
    uint16_t y = ((display->height() - tbh) / 2) - tby;
    display->setFullWindow();
    display->firstPage();
    do {
        display->fillScreen(GxEPD_WHITE);
        display->setCursor(x, y);
        if (isT_Deck_Pro_v1_1) {
            display->print(Version_str2);
        } else {
            display->print(Version_str1);
        }
        display->setCursor(x + 20, y + 20);
        display->print(UI_T_DECK_PRO_VERSION);
    } while (display->nextPage());
    display->powerOff();
    shared_spi_unlock();
    return true;
}

static void convert_lvgl_buf_to_epd_bitmap(const lv_color_t *color_p, lv_coord_t width, lv_coord_t height)
{
    const size_t stride = EPD_BITMAP_STRIDE(width);
    const size_t bitmap_size = stride * size_t(height);

    memset(decodebuffer, 0xFF, bitmap_size);

    for (lv_coord_t y = 0; y < height; ++y) {
        size_t row_offset = size_t(y) * stride;
        for (lv_coord_t x = 0; x < width; ++x) {
            const size_t pixel_index = size_t(y) * size_t(width) + size_t(x);
            if (lv_color_brightness(color_p[pixel_index]) < 128) {
                decodebuffer[row_offset + size_t(x / 8)] &= ~(0x80 >> (x & 0x7));
            }
        }
    }
}

static void flush_epd_bitmap(const lv_area_t *area)
{
    const lv_coord_t width = lv_area_get_width(area);
    const lv_coord_t height = lv_area_get_height(area);

    if ((width <= 0) || (height <= 0)) {
        return;
    }

    shared_spi_lock();
    shared_spi_prepare_device(BOARD_EPD_CS);

    if (disp_refr_mode == DISP_REFR_MODE_PART) {
        display->setPartialWindow(area->x1, area->y1, width, height);
    } else {
        display->setFullWindow();
    }

    display->firstPage();
    do {
        if (disp_refr_mode == DISP_REFR_MODE_FULL) {
            display->fillScreen(GxEPD_WHITE);
        }
        display->drawInvertedBitmap(area->x1, area->y1, decodebuffer, width, height, GxEPD_BLACK);
    } while (display->nextPage());

    display->powerOff();
    shared_spi_unlock();
}

static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    convert_lvgl_buf_to_epd_bitmap(color_p, lv_area_get_width(area), lv_area_get_height(area));
    flush_epd_bitmap(area);

    disp_refr_mode = DISP_REFR_MODE_PART;
    lv_disp_flush_ready(disp_drv);
}

static void touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    uint8_t touched = hyn_touch_get_point(&last_x, &last_y, 1);
    if (touched) {
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void lvgl_init(void)
{
    lv_init();

    static lv_disp_draw_buf_t draw_buf_dsc_1;
    lv_color_t *buf_1 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_color_t *buf_2 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_disp_draw_buf_init(&draw_buf_dsc_1, buf_1, buf_2, LCD_HOR_SIZE * LCD_VER_SIZE);
    decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_BITMAP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_HOR_SIZE;
    disp_drv.ver_res = LCD_VER_SIZE;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf_dsc_1;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    /* Touchpad */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);
}

void ink_screen_prepare_shutdown(void)
{
    if (!peri_init_st[E_PERI_INK_SCREEN]) {
        return;
    }
    digitalWrite(BOARD_EPD_BL, LOW);
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_EPD_CS);
    display->powerOff();
    shared_spi_unlock();
}

/*********************************************************************************
 *                              POWER MANAGEMENT
 * *******************************************************************************/

static bool bq25896_apply_factory_profile(void)
{
    PPM.resetDefault();
    PPM.disableWatchdog();
    PPM.exitHizMode();
    PPM.disableOTG();
    PPM.enableBatterPowerPath();
    PPM.setInputCurrentLimit(FACTORY_BQ25896_INPUT_LIMIT_MA);
    PPM.setSysPowerDownVoltage(FACTORY_BQ25896_SYS_POWER_DOWN_MV);
    PPM.setChargeTargetVoltage(FACTORY_BQ25896_CHARGE_TARGET_MV);
    PPM.setChargerConstantCurr(FACTORY_BQ25896_FAST_CHARGE_MA);
    PPM.setPrechargeCurr(FACTORY_BQ25896_PRECHARGE_MA);
    PPM.setTerminationCurr(FACTORY_BQ25896_TERMINATION_MA);
    PPM.enableChargingTermination();
    PPM.enableCharge();
    return PPM.enableMeasure();
}

static bool bq25896_init(void)
{
    Wire.beginTransmission(BOARD_I2C_ADDR_BQ25896);
    if (Wire.endTransmission() == 0) {
        if (!PPM.init(Wire, BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_ADDR_BQ25896)) {
            return false;
        }
        return bq25896_apply_factory_profile();
    }
    return false;
}

static bool bq27220_init(void)
{
    bq27220.setDefaultCapacity(FACTORY_BATTERY_DESIGN_CAPACITY_MAH);
    return bq27220.init();
}

static void bq25896_runtime_maintain(void)
{
    static uint32_t last_check_ms = 0;
    static uint32_t last_recovery_ms = 0;

    if (!peri_init_st[E_PERI_BQ25896]) {
        return;
    }
    if (millis() - last_check_ms < FACTORY_BQ25896_RUNTIME_CHECK_MS) {
        return;
    }
    last_check_ms = millis();

    if (!PPM.isVbusIn()) {
        return;
    }

    bool need_recover = !PPM.isCharging();
    if (!need_recover && peri_init_st[E_PERI_BQ27220]) {
        need_recover = (bq27220.getAverageCurrent() < 0);
    }
    if (!need_recover) {
        return;
    }
    if (millis() - last_recovery_ms < FACTORY_BQ25896_RECOVERY_COOLDOWN_MS) {
        return;
    }

    Serial.println("[BQ25896] Restore charge path");
    bq25896_apply_factory_profile();
    last_recovery_ms = millis();
}

/*********************************************************************************
 *                              SD CARD
 * *******************************************************************************/

static bool sd_care_init(void)
{
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_SD_CS);

    if (!SD.begin(BOARD_SD_CS, SPI)) {
        shared_spi_unlock();
        Serial.println("[SD CARD] Card Mount Failed");
        return false;
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);
    shared_spi_unlock();
    return true;
}

/*********************************************************************************
 *                              A7682E MODEM
 * *******************************************************************************/

static void a7682_task(void *param)
{
    vTaskSuspend(a7682_handle);
    while (1) {
        while (SerialAT.available()) {
            SerialMon.write(SerialAT.read());
        }
        while (SerialMon.available()) {
            SerialAT.write(SerialMon.read());
        }
        delay(1);
    }
}

static bool A7682E_init(void)
{
    Serial.println("Place your board outside to catch satellite signal");
    SerialAT.begin(115200, SERIAL_8N1, BOARD_A7682E_TXD, BOARD_A7682E_RXD);
    Serial.println("Start modem...");

    digitalWrite(BOARD_A7682E_PWRKEY, LOW);
    delay(10);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);
    delay(50);
    digitalWrite(BOARD_A7682E_PWRKEY, LOW);
    delay(10);

    int retry_cnt = 5;
    int retry = 0;
    while (!modem.testAT(1000)) {
        Serial.println(".");
        if (retry++ > retry_cnt) {
            digitalWrite(BOARD_A7682E_PWRKEY, LOW);
            delay(100);
            digitalWrite(BOARD_A7682E_PWRKEY, HIGH);
            delay(1000);
            digitalWrite(BOARD_A7682E_PWRKEY, LOW);
            Serial.println("[A7682E] Init Fail");
            break;
        }
    }

    Serial.println();
    delay(200);

    xTaskCreate(a7682_task, "a7682_handle", 1024 * 3, NULL, A7682E_PRIORITY, &a7682_handle);
    return (retry < retry_cnt);
}

/*********************************************************************************
 *                              PCM5102A AUDIO DAC
 * *******************************************************************************/

static bool pcm5102a_init(void)
{
    bool ret = audio.setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT);
    if (ret == false)
        Serial.printf("[%d] Execution error\n", __LINE__);
    audio.setVolume(21);
    pinMode(BOARD_6609_EN, OUTPUT);
    digitalWrite(BOARD_6609_EN, HIGH);
    return true;
}

/*********************************************************************************
 *                              SETUP & LOOP
 * *******************************************************************************/

extern void hw_radio_begin();

void setup()
{
    gpio_hold_dis((gpio_num_t)BOARD_6609_EN);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_dis((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_dis((gpio_num_t)BOARD_A7682E_PWRKEY);
    gpio_deep_sleep_hold_dis();

    setCpuFrequencyMhz(240);
    Serial.begin(115200);

    // GPIO setup
    pinMode(BOARD_KEYBOARD_LED, OUTPUT);
    pinMode(BOARD_MOTOR_PIN, OUTPUT);
    pinMode(BOARD_6609_EN, OUTPUT);
    pinMode(BOARD_LORA_EN, OUTPUT);
    pinMode(BOARD_GPS_EN, OUTPUT);
    pinMode(BOARD_A7682E_PWRKEY, OUTPUT);
    digitalWrite(BOARD_KEYBOARD_LED, LOW);
    digitalWrite(BOARD_MOTOR_PIN, HIGH);
    digitalWrite(BOARD_6609_EN, HIGH);
    digitalWrite(BOARD_LORA_EN, HIGH);
    digitalWrite(BOARD_GPS_EN, HIGH);
    digitalWrite(BOARD_A7682E_PWRKEY, HIGH);

    // E-Paper backlight
    pinMode(BOARD_EPD_BL, OUTPUT);
    digitalWrite(BOARD_EPD_BL, HIGH);

    // SPI chip selects — all HIGH before init
    pinMode(BOARD_LORA_CS, OUTPUT);
    digitalWrite(BOARD_LORA_CS, HIGH);
    pinMode(BOARD_LORA_RST, OUTPUT);
    digitalWrite(BOARD_LORA_RST, HIGH);
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);
    pinMode(BOARD_EPD_CS, OUTPUT);
    digitalWrite(BOARD_EPD_CS, HIGH);
    shared_spi_bus_init();

    // I2C bus scan
    byte error, address;
    int nDevices = 0;
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Serial.printf(" ------------- I2C ------------- \n");
    for (address = 0x01; address < 0x7F; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            nDevices++;
            if (address == BOARD_I2C_ADDR_TOUCH) {
                Serial.printf("[0x%x] TOUCH find!\n", address);
            } else if (address == BOARD_I2C_ADDR_GYROSCOPDE) {
                Serial.printf("[0x%x] GYROSCOPDE find!\n", address);
            } else if (address == BOARD_I2C_ADDR_KEYBOARD) {
                Serial.printf("[0x%x] KEYBOARD find!\n", address);
            } else if (address == BOARD_I2C_ADDR_BQ27220) {
                Serial.printf("[0x%x] BQ27220 find!\n", address);
            } else if (address == BOARD_I2C_ADDR_BQ25896) {
                Serial.printf("[0x%x] BQ25896 find!\n", address);
            } else if (address == BOARD_I2C_ADDR_DRV2605) {
                Serial.printf("[0x%x] DRV2605 find!\n", address);
            }
        }
    }

    // Detect board version via DRV2605
    for (int i = 0; i < 3; i++) {
        Wire.beginTransmission(BOARD_I2C_ADDR_DRV2605);
        error = Wire.endTransmission();
        if (error == 0) {
            isT_Deck_Pro_v1_1 = 1;
        } else {
            isT_Deck_Pro_v1_1 = 0;
        }
    }

    select_ink_display();

    // DRV2605 haptic motor
    if (isT_Deck_Pro_v1_1) {
        Serial.println("Adafruit DRV2605 Basic test");
        if (!drv.begin()) {
            Serial.println("Could not find DRV2605");
            while (1) delay(10);
        }
        drv.selectLibrary(1);
        drv.setMode(DRV2605_MODE_INTTRIG);
    }

    // SPIFFS
    Serial.printf(" ------------- SPIFFS ------------- \n");
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed");
    }

    // NTP & WiFi events
    sntp_set_time_sync_notification_cb(time_available);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.setAutoReconnect(true);

    // SPI bus
    Serial.println(" ------------- PERI ------------- ");
    SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

    // Peripheral init
    peri_init_st[E_PERI_INK_SCREEN] = ink_screen_init();
    hw_radio_begin();
    peri_init_st[E_PERI_LORA]       = true;
    peri_init_st[E_PERI_KYEPAD]     = keypad_init(BOARD_I2C_ADDR_KEYBOARD);
    peri_init_st[E_PERI_BQ25896]    = bq25896_init();
    peri_init_st[E_PERI_BQ27220]    = bq27220_init();
    peri_init_st[E_PERI_SD]         = sd_care_init();
    peri_init_st[E_PERI_GPS]        = gps_init();
    peri_init_st[E_PERI_BHI260AP]   = BHI260AP_init();
    peri_init_st[E_PERI_A7682E]     = A7682E_init();

    // PCM5102A conflicts with modem pins — only init if modem failed
    if (peri_init_st[E_PERI_A7682E] == false) {
        peri_init_st[E_PERI_PCM5102A] = pcm5102a_init();
    }

    peri_init_st[E_PERI_TOUCH] = hyn_touch_init();

    // LVGL + E-Paper display driver
    lvgl_init();

    // PDA HAL init
    hw_init();

    // Auto-connect WiFi
    hw_wifi_auto_connect();

    // Launch PDA main UI
    setupGui();

    // Initial full refresh
    disp_full_refr();

    Serial.println("PDA start done. Running main loop.");
}

void loop()
{
    lv_task_handler();
    keypad_loop();
    bq25896_runtime_maintain();

    if (peri_init_st[E_PERI_PCM5102A] == true) {
        audio.loop();
    }

    delay(1);
}

/*********************************************************************************
 *                              GLOBAL PROTOTYPES
 * *******************************************************************************/
void disp_full_refr(void)
{
    disp_refr_mode = DISP_REFR_MODE_FULL;
}
