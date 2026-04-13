/**
 * @file      hal_interface.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * HAL implementation for T-Deck Pro v1.1.
 * Ported from LilyGoLib/examples/pda/hal_interface.cpp, replacing all
 * instance.* (LilyGoLib) calls with direct peripheral access.
 */
#include "hal_interface.h"
#include <math.h>
#include <lvgl.h>

#ifdef ARDUINO
#include "driver/gpio.h"
#endif

#define NVS_NAME    "pager"
static user_setting_params_t user_setting;

typedef struct _device_const_var {
    uint16_t max_brightness;
    uint16_t min_brightness;
    uint16_t max_charge_current;
    uint16_t min_charge_current;
    uint8_t  charge_level_nums;
    uint8_t  charge_steps;
} device_const_var_t;

#ifdef ARDUINO

#include "Esp.h"

#define  CONFIG_BLE_KEYBOARD
#include <esp_mac.h>
#include <WiFi.h>
#include <SD.h>
#include <Preferences.h>
#include "driver/rtc_io.h"
#include "utilities.h"
#include "factory.h"
#include "peripheral.h"

static Preferences           prefs;
static bool                  pps_trigger = false;

#if defined(USING_BLE_KEYBOARD)
#include <BleKeyboard.h>
BleKeyboard bleKeyboard;
#endif

#endif

/* SD card on T-Deck Pro uses shared SPI bus */
#if defined(ARDUINO)
static bool sd_mounted = false;

bool hw_sd_begin()
{
    if (sd_mounted) return true;

    shared_spi_lock();
    shared_spi_prepare_device(BOARD_SD_CS);

    if (!SD.begin(BOARD_SD_CS, SPI)) {
        shared_spi_unlock();
        Serial.println("[SD] SD.begin() FAILED");
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        shared_spi_unlock();
        Serial.println("[SD] cardType is CARD_NONE");
        return false;
    }
    Serial.printf("[SD] OK, cardType=%d, size=%llu MB\n", SD.cardType(), SD.cardSize() / (1024 * 1024));
    sd_mounted = true;
    shared_spi_unlock();
    return true;
}
#else
bool hw_sd_begin() { return false; }
#endif

static device_const_var_t dev_conts_var = {
    .max_brightness = DEVICE_MAX_BRIGHTNESS_LEVEL,
    .min_brightness = DEVICE_MIN_BRIGHTNESS_LEVEL,
    .max_charge_current = DEVICE_MAX_CHARGE_CURRENT,
    .min_charge_current = DEVICE_MIN_CHARGE_CURRENT,
    .charge_level_nums = DEVICE_CHARGE_LEVEL_NUMS,
    .charge_steps = DEVICE_CHARGE_STEPS,
};

static const char *hw_devices[] = {
    USING_RADIO_NAME,
#ifdef USING_TOUCHPAD
    "Touch Panel",
#else
    "",
#endif
    "Haptic Drive",
    "Power management",
    "",  // No RTC on T-Deck Pro
    "PSRAM",
    "GPS",
    "SD card",
    "",  // No NFC
#ifdef USING_BHI260_SENSOR
    "BHI260AP 6-Axis Sensor",
#else
    "",
#endif
    "Keyboard",
    "Gauge",
    "",  // No XL9555 expander
    "",  // No audio codec
    "",  // No NRF24
    "",  // No SI4735
    "",  // No BME280
    "",  // No QMC5883
    "",  // No BMA423
    "",  // No QMI8658
};

static bool sync_date_time = false;

extern void hw_radio_begin();

#ifndef ARDUINO
int random(int min, int max)
{
    if (min > max) {
        int temp = min;
        min = max;
        max = temp;
    }
    int range = max - min + 1;
    return rand() % range + min;
}
#endif


#ifdef ARDUINO

size_t getArduinoLoopTaskStackSize(void)
{
    return 30 * 1024;
}

/* Audio playback stubs — T-Deck Pro has no ES8311 audio codec.
 * PCM5102A DAC conflicts with modem pins. */

#endif


/*********************************************************************************
 *                              FFT / Microphone stubs
 * *******************************************************************************/
/* T-Deck Pro has a PDM mic on GPIO 17/18 but no codec for playback.
 * FFT/mic functions are stubbed for now. */

void hw_audio_get_fft_data(FFTData *fft_data)
{
    /* Stub — no codec/mic integration yet */
}

bool hw_set_mic_start()
{
    /* Stub — PDM mic init could be added here */
    return false;
}

void hw_set_mic_stop()
{
    /* Stub */
}


/*********************************************************************************
 *                              Recorder stubs
 * *******************************************************************************/
bool hw_recorder_start(const char *filepath)
{
    return false;
}

void hw_recorder_stop()
{
}

bool hw_recorder_is_recording()
{
    return false;
}

uint32_t hw_recorder_get_duration_ms()
{
    return 0;
}


/*********************************************************************************
 *                              NFC stubs
 * *******************************************************************************/
bool hw_start_nfc_discovery()
{
    return false;
}

void hw_stop_nfc_discovery()
{
}


/*********************************************************************************
 *                              hw_init
 * *******************************************************************************/
/* Back button (GPIO 0) support */
static void(*back_button_cb)() = NULL;
static void(*app_keyboard_cb)(int state, char &c) = NULL;

static void back_btn_timer_cb(lv_timer_t *t)
{
    static bool last_state = true;
    static uint32_t press_start = 0;
    static bool long_press_fired = false;

    bool current = digitalRead(BOARD_BOOT_PIN);
    if (last_state && !current) {
        // Press detected
        press_start = millis();
        long_press_fired = false;
    } else if (!last_state && !current) {
        // Still held
        if (!long_press_fired && (millis() - press_start) > 1000) {
            long_press_fired = true;
            // Long press — could launch power app
        }
    } else if (!last_state && current) {
        // Released
        if (!long_press_fired) {
            if (back_button_cb) {
                back_button_cb();
            } else {
                // Try to find menu back button in LVGL
                extern lv_obj_t *main_screen;
                if (main_screen) {
                    lv_obj_t *app_tile = lv_obj_get_child(main_screen, 1);
                    if (app_tile) {
                        uint32_t cnt = lv_obj_get_child_cnt(app_tile);
                        for (uint32_t i = 0; i < cnt; i++) {
                            lv_obj_t *child = lv_obj_get_child(app_tile, i);
                            if (lv_obj_check_type(child, &lv_menu_class)) {
                                lv_obj_t *back_btn = lv_menu_get_main_header_back_btn(child);
                                if (back_btn) {
                                    lv_event_send(back_btn, LV_EVENT_CLICKED, NULL);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    last_state = current;
}

/* Keyboard callback wrapper for PDA apps */
static void keypad_event_wrapper(int state, char val)
{
    if (app_keyboard_cb) {
        app_keyboard_cb(state, val);
    }
}

void hw_init()
{
#ifdef ARDUINO
    hw_radio_begin();

    // Register keyboard callback from factory keypad driver
    keypad_regetser_cb(keypad_event_wrapper);

    // Setup GPIO 0 (BOOT button) as physical back button
    pinMode(BOARD_BOOT_PIN, INPUT_PULLUP);
    lv_timer_create(back_btn_timer_cb, 50, NULL);

    prefs.begin(NVS_NAME);
    if (prefs.getBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t)) != sizeof(user_setting_params_t)) {
        Serial.println("NVS: set default settings");
        user_setting.brightness_level = 50;
        user_setting.keyboard_bl_level = 80;
        user_setting.disp_timeout_second = 30;
        user_setting.charger_current = DEVICE_CHARGE_CURRENT_RECOMMEND;
        user_setting.charger_enable = true;
        prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
    }

    user_setting.charger_current = hw_get_charger_current();
    hw_set_disp_backlight(user_setting.brightness_level);
    hw_set_kb_backlight(user_setting.keyboard_bl_level);

#else
    user_setting.brightness_level = 10;
    user_setting.keyboard_bl_level = 255;
    user_setting.disp_timeout_second = 30;
    user_setting.charger_current = 1000;
    user_setting.charger_enable = true;
#endif
}


/*********************************************************************************
 *                              Settings
 * *******************************************************************************/

void hw_get_user_setting(user_setting_params_t &param)
{
    param = user_setting;
}

void hw_set_user_setting(user_setting_params_t &param)
{
    user_setting = param;
#ifdef ARDUINO
    prefs.putBytes(NVS_NAME, &user_setting, sizeof(user_setting_params_t));
#endif
}

const uint32_t hw_get_disp_timeout_ms()
{
    return user_setting.disp_timeout_second * 1000;
}


/*********************************************************************************
 *                              Device info
 * *******************************************************************************/

uint16_t hw_get_devices_nums()
{
    return sizeof(hw_devices) / sizeof(hw_devices[0]);
}

const char *hw_get_devices_name(int index)
{
    if (index > hw_get_devices_nums()) {
        return "NULL";
    }
    return hw_devices[index];
}

const char *hw_get_variant_name()
{
    return "T-Deck Pro v1.1";
}

bool hw_get_mac(uint8_t *mac)
{
#ifdef ARDUINO
    esp_efuse_mac_get_default(mac);
    return true;
#else
    return false;
#endif
}


/*********************************************************************************
 *                              WiFi
 * *******************************************************************************/

void hw_get_wifi_ssid(string &param)
{
#ifdef ARDUINO
    param = WiFi.isConnected() ? WiFi.SSID().c_str() : "N.A";
#else
    param = "NO CONFIG";
#endif
}

wl_status_t hw_get_wifi_status()
{
#ifdef ARDUINO
    return WiFi.status();
#else
    return WL_NO_SSID_AVAIL;
#endif
}

void hw_get_ip_address(string &param)
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        param = WiFi.localIP().toString().c_str();
        return;
    }
#endif
    param = "N.A";
}

int16_t hw_get_wifi_rssi()
{
#ifdef ARDUINO
    if (WiFi.isConnected()) {
        return WiFi.RSSI();
    }
#endif
    return -99;
}

int16_t hw_set_wifi_scan()
{
#ifdef ARDUINO
    return WiFi.scanNetworks(true);
#endif
    return 0;
}

bool hw_get_wifi_scanning()
{
#ifdef ARDUINO
    return WiFi.getStatusBits() & WIFI_SCANNING_BIT;
#endif
    return false;
}

void hw_get_wifi_scan_result(vector<wifi_scan_params_t> &list)
{
    list.clear();
#ifdef ARDUINO
    int16_t nums = WiFi.scanComplete();
    if (nums < 0) {
        return;
    }
    wifi_scan_params_t param;
    for (int i = 0; i < nums; ++i) {
        String ssid;
        uint8_t encryptionType;
        int32_t rssi;
        uint8_t *BSSID;
        int32_t channel;
        WiFi.getNetworkInfo(i, ssid, encryptionType, rssi, BSSID, channel);
        param.authmode = encryptionType;
        param.ssid = ssid.c_str();
        param.rssi = rssi;
        param.channel = channel;
        memcpy(param.bssid, BSSID, 6);
        list.push_back(param);
    }
#endif
}

void hw_set_wifi_connect(wifi_conn_params_t &params)
{
#ifdef ARDUINO
    WiFi.begin(params.ssid.c_str(), params.password.c_str());
    hw_wifi_save_credentials(params.ssid.c_str(), params.password.c_str());
#endif
}

void hw_wifi_save_credentials(const char *ssid, const char *password)
{
#ifdef ARDUINO
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", password);
    Serial.printf("WiFi credentials saved for: %s\n", ssid);
#endif
}

bool hw_wifi_auto_connect()
{
#ifdef ARDUINO
    String ssid = prefs.getString("wifi_ssid", "");
    String password = prefs.getString("wifi_pass", "");
    if (ssid.length() > 0 && password.length() > 0) {
        Serial.printf("Auto-connecting to saved WiFi: %s\n", ssid.c_str());
        WiFi.begin(ssid, password);
        return true;
    }
    Serial.println("No saved WiFi credentials found");
#endif
    return false;
}

bool hw_get_wifi_connected()
{
#ifdef ARDUINO
    return WiFi.isConnected();
#endif
    return false;
}


/*********************************************************************************
 *                              Date/Time
 * *******************************************************************************/

void hw_get_date_time(string &param)
{
#ifdef ARDUINO
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        param = buf;
    } else {
        param = "N.A";
    }
#else
    param = "2025-01-01 00:00:00";
#endif
}

void hw_get_date_time(struct tm &timeinfo)
{
#ifdef ARDUINO
    if (!getLocalTime(&timeinfo)) {
        memset(&timeinfo, 0, sizeof(struct tm));
    }
#else
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    timeinfo = *t;
#endif
}


/*********************************************************************************
 *                              Battery / Power
 * *******************************************************************************/

int16_t hw_get_battery_voltage()
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ27220]) {

        return bq27220.getVoltage();
    } else if (peri_init_st[E_PERI_BQ25896]) {
        return PPM.getBattVoltage();
    }
#endif
    return 0;
}

float hw_get_sd_size()
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_SD]) {
        return SD.cardSize() / 1024.0 / 1024.0 / 1024.0;
    }
#endif
    return 0.0;
}

void hw_get_arduino_version(string &param)
{
#ifdef ARDUINO
    param = String(ESP_ARDUINO_VERSION_MAJOR).c_str();
    param += ".";
    param += String(ESP_ARDUINO_VERSION_MINOR).c_str();
    param += ".";
    param += String(ESP_ARDUINO_VERSION_PATCH).c_str();
#else
    param = "N.A";
#endif
}


/*********************************************************************************
 *                              GPS
 * *******************************************************************************/

void hw_gps_attach_pps()
{
#ifdef ARDUINO
    pinMode(BOARD_GPS_PPS, INPUT);
    attachInterrupt(BOARD_GPS_PPS, []() {
        pps_trigger ^= 1;
    }, CHANGE);
#endif
}

void hw_gps_detach_pps()
{
#ifdef ARDUINO
    detachInterrupt(BOARD_GPS_PPS);
    pinMode(BOARD_GPS_PPS, OPEN_DRAIN);
#endif
}

bool hw_get_gps_info(gps_params_t &param)
{
#ifdef ARDUINO
    static uint32_t interval = 0;
    param.pps = pps_trigger;
    bool debug = param.enable_debug;
    if (millis() < interval && debug == false) {
        return false;
    }
    interval = millis() + 1000;
    memset(&param, 0, sizeof(gps_params_t));

    param.model = "u-blox MIA-M10Q";

    double lat = 0, lng = 0;
    gps_get_coord(&lat, &lng);

    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0;
    gps_get_data(&year, &month, &day);
    gps_get_time(&hour, &minute, &second);

    uint32_t vsat = 0;
    gps_get_satellites(&vsat);

    double speed = 0;
    gps_get_speed(&speed);

    bool location = (lat != 0.0 || lng != 0.0);
    bool datetime = (year > 2000);

    if (location) {
        param.lat = lat;
        param.lng = lng;
        param.speed = speed;
    }

    if (datetime) {
        param.datetime.tm_year = year - 1900;
        param.datetime.tm_mon = month - 1;
        param.datetime.tm_mday = day;
        param.datetime.tm_hour = hour;
        param.datetime.tm_min = minute;
        param.datetime.tm_sec = second;

        if (!sync_date_time) {
            sync_date_time = true;
            struct tm utc_tm = {0};
            utc_tm.tm_year = year - 1900;
            utc_tm.tm_mon = month - 1;
            utc_tm.tm_mday = day;
            utc_tm.tm_hour = hour;
            utc_tm.tm_min = minute;
            utc_tm.tm_sec = second;
            time_t utc = mktime(&utc_tm);
            utc += GMT_OFFSET_SECOND;
            struct timeval tv = { .tv_sec = utc, .tv_usec = 0 };
            settimeofday(&tv, NULL);
        }
    }

    param.satellite = vsat;
    return location && datetime;
#else
    param.model = "Dummy";
    param.lat = 0.0;
    param.lng = 0.0;
    param.satellite = rand() % 30;
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    param.datetime = *timeinfo;
    return true;
#endif
}


/*********************************************************************************
 *                              Device online status
 * *******************************************************************************/

uint32_t hw_get_device_online()
{
#ifdef ARDUINO
    uint32_t online = 0;
    if (peri_init_st[E_PERI_TOUCH])    online |= HW_TOUCH_ONLINE;
    if (peri_init_st[E_PERI_KYEPAD])   online |= HW_KEYBOARD_ONLINE;
    if (peri_init_st[E_PERI_BQ25896])  online |= HW_PMU_ONLINE;
    if (peri_init_st[E_PERI_BQ27220])  online |= HW_GAUGE_ONLINE;
    if (peri_init_st[E_PERI_GPS])      online |= HW_GPS_ONLINE;
    if (peri_init_st[E_PERI_LORA])     online |= HW_RADIO_ONLINE;
    if (peri_init_st[E_PERI_BHI260AP]) online |= HW_BHI260AP_ONLINE;
    if (peri_init_st[E_PERI_SD])       online |= HW_SD_ONLINE;
    // DRV2605 is always present on v1.1
    online |= HW_DRV_ONLINE;
    return online;
#else
    return HW_TOUCH_ONLINE | HW_DRV_ONLINE | HW_PMU_ONLINE | HW_KEYBOARD_ONLINE;
#endif
}


/*********************************************************************************
 *                              Display backlight
 * *******************************************************************************/
/* E-Paper backlight is binary on/off via GPIO 45 */

static uint8_t epd_bl_level = 255;

void hw_set_disp_backlight(uint8_t level)
{
#ifdef ARDUINO
    epd_bl_level = level;
    digitalWrite(BOARD_EPD_BL, level > 0 ? HIGH : LOW);
#endif
}

uint8_t hw_get_disp_backlight()
{
    return epd_bl_level;
}

bool hw_get_disp_is_on()
{
    return epd_bl_level > 0;
}

void hw_inc_brightness(uint8_t level)
{
#ifdef ARDUINO
    hw_set_disp_backlight(255);
#endif
}

void hw_dec_brightness(uint8_t level)
{
#ifdef ARDUINO
    hw_set_disp_backlight(0);
#endif
}


/*********************************************************************************
 *                              Keyboard backlight
 * *******************************************************************************/

static uint8_t kb_bl_level = 0;

void hw_set_kb_backlight(uint8_t level)
{
#ifdef ARDUINO
    kb_bl_level = level;
    // TCA8418 keyboard LED is a simple GPIO
    digitalWrite(BOARD_KEYBOARD_LED, level > 0 ? HIGH : LOW);
#endif
}

void hw_set_led_backlight(uint8_t level)
{
    hw_set_kb_backlight(level);
}

uint8_t hw_get_kb_backlight()
{
    return kb_bl_level;
}


/*********************************************************************************
 *                              Haptic feedback (DRV2605)
 * *******************************************************************************/

void hw_feedback()
{
#ifdef ARDUINO
    extern Adafruit_DRV2605 drv;
    extern uint8_t isT_Deck_Pro_v1_1;
    if (isT_Deck_Pro_v1_1) {
        drv.setWaveform(0, 1);  // Effect 1 — strong click
        drv.setWaveform(1, 0);  // End
        drv.go();
    }
#endif
}


/*********************************************************************************
 *                              Charge management (BQ25896 PPM)
 * *******************************************************************************/

bool hw_get_charge_enable()
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        return PPM.isEnableCharge();
    }
#endif
    return false;
}

void hw_set_charger(bool enable)
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        if (enable) {
            PPM.enableCharge();
        } else {
            PPM.disableCharge();
        }
    }
#endif
}

uint16_t hw_get_charger_current()
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        return PPM.getChargerConstantCurr();
    }
#endif
    return 0;
}

void hw_set_charger_current(uint16_t milliampere)
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        PPM.setChargerConstantCurr(milliampere);
    }
#endif
}

uint8_t hw_get_charger_current_level()
{
    return user_setting.charger_current / dev_conts_var.charge_steps;
}

uint16_t hw_set_charger_current_level(uint8_t level)
{
#ifdef ARDUINO
    uint16_t ma = level * dev_conts_var.charge_steps;
    PPM.setChargerConstantCurr(ma);
    return ma;
#else
    return level * dev_conts_var.charge_steps;
#endif
}

bool hw_get_otg_enable()
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        return PPM.isEnableOTG();
    }
#endif
    return false;
}

bool hw_set_otg(bool enable)
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BQ25896]) {
        if (enable) {
            return PPM.enableOTG();
        } else {
            PPM.disableOTG();
        }
        return true;
    }
#endif
    return false;
}


/*********************************************************************************
 *                              Power management
 * *******************************************************************************/

void hw_shutdown()
{
#ifdef ARDUINO
    hw_set_disp_backlight(0);
    ink_screen_prepare_shutdown();
    if (peri_init_st[E_PERI_BQ25896]) {
        PPM.shutdown();
    }
#endif
}

void hw_sleep()
{
#ifdef ARDUINO
    hw_set_disp_backlight(0);
    ink_screen_prepare_shutdown();
    // Enter deep sleep with BOOT button wakeup
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_BOOT_PIN, 0);
    esp_deep_sleep_start();
#endif
}


/*********************************************************************************
 *                              Monitor
 * *******************************************************************************/

void hw_get_monitor_params(monitor_params_t &params)
{
#ifdef ARDUINO
    memset(&params, 0, sizeof(monitor_params_t));
    params.type = MONITOR_PPM;

    if (peri_init_st[E_PERI_BQ25896]) {
        params.charge_state = PPM.getChargeStatusString();
        params.usb_voltage = PPM.getVbusVoltage();
        params.sys_voltage = PPM.getSystemVoltage();
        PPM.getFaultStatus();
        if (PPM.isNTCFault()) {
            params.ntc_state = PPM.getNTCStatusString();
        } else {
            params.ntc_state = "Normal";
        }
    }

    if (peri_init_st[E_PERI_BQ27220]) {

        params.battery_percent = bq27220.getStateOfCharge();
        params.battery_voltage = bq27220.getVoltage();
        params.instantaneousCurrent = bq27220.getCurrent();
        params.remainingCapacity = bq27220.getRemainingCapacity();
        params.fullChargeCapacity = bq27220.getFullChargeCapacity();
        params.standbyCurrent = 0;  // BQ27220 lib doesn't expose standbyCurrent
        params.temperature = bq27220.getTemperature();
        params.designCapacity = bq27220.getDesignCapacity();
        params.averagePower = 0;        // BQ27220 lib doesn't expose averagePower
        params.maxLoadCurrent = 0;      // BQ27220 lib doesn't expose maxLoadCurrent
    }
#else
    params.type = MONITOR_PPM;
    params.battery_percent = 30 + rand() % (100 - 30 + 1);
    params.battery_voltage = 4178;
    params.charge_state = "Fast charging";
    params.usb_voltage = 4998;
    params.ntc_state = "Normal";
#endif
}


/*********************************************************************************
 *                              IMU (BHI260AP)
 * *******************************************************************************/

static imu_params_t imu_params = {0};

void hw_get_imu_params(imu_params_t &params)
{
#ifdef ARDUINO
    if (peri_init_st[E_PERI_BHI260AP]) {
        float x, y, z;
        BHI260AP_get_val(0, &x, &y, &z);
        imu_params.roll = x;
        imu_params.pitch = y;
        imu_params.heading = z;
        params = imu_params;
    }
#else
    params = imu_params;
#endif
}

void hw_register_imu_process()
{
    // BHI260AP is handled by the factory peri_gyroscope driver
}

void hw_unregister_imu_process()
{
    // No-op
}


/*********************************************************************************
 *                              BLE (generic — stubs)
 * *******************************************************************************/

void hw_enable_ble(const char *devName)
{
}

void hw_deinit_ble()
{
}

void hw_disable_ble()
{
}

size_t hw_get_ble_message(char *buffer, size_t buffer_size)
{
    return 0;
}


/*********************************************************************************
 *                              BLE Keyboard
 * *******************************************************************************/

const char *hw_get_ble_kb_name()
{
    return "Keyboard";
}

void hw_set_ble_kb_enable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    bleKeyboard.setName("Keyboard");
    bleKeyboard.begin();
#endif
}

void hw_set_ble_kb_disable()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD)
    bleKeyboard.end();
#endif
}

void hw_set_ble_kb_char(const char *c)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    if (bleKeyboard.isConnected()) {
        bleKeyboard.print(c);
    }
#endif
}

void hw_set_ble_kb_key(uint8_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    if (bleKeyboard.isConnected()) {
        bleKeyboard.press(key);
    }
#endif
}

void hw_set_ble_kb_release()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    if (bleKeyboard.isConnected()) {
        bleKeyboard.releaseAll();
    }
#endif
}

bool hw_get_ble_kb_connected()
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    return bleKeyboard.isConnected();
#endif
    return false;
}

void hw_set_ble_key(media_key_value_t key)
{
#if defined(ARDUINO) && defined(USING_BLE_KEYBOARD) && defined(CONFIG_BLE_KEYBOARD)
    if (bleKeyboard.isConnected()) {
        switch (key) {
        case MEDIA_VOLUME_UP:
            bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
            break;
        case MEDIA_VOLUME_DOWN:
            bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
            break;
        case MEDIA_PLAY_PAUSE:
            bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
            break;
        case MEDIA_NEXT:
            bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
            break;
        case MEDIA_PREVIOUS:
            bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
            break;
        default: return;
        }
    }
#endif
}


/*********************************************************************************
 *                              Keyboard
 * *******************************************************************************/

void hw_set_keyboard_read_callback(void(*read)(int state, char &c))
{
#ifdef ARDUINO
    app_keyboard_cb = read;
    keypad_regetser_cb(keypad_event_wrapper);
#endif
}

void hw_set_back_button_callback(void(*cb)())
{
#ifdef ARDUINO
    back_button_cb = cb;
#endif
}

void hw_enable_keyboard()
{
    // TCA8418 keyboard is always enabled on T-Deck Pro
}

void hw_disable_keyboard()
{
    // No-op for T-Deck Pro
}

void hw_flush_keyboard()
{
    // No-op — TCA8418 flushes via keypad_loop()
}

bool hw_has_keyboard()
{
#ifdef ARDUINO
    return peri_init_st[E_PERI_KYEPAD];
#else
    return false;
#endif
}

bool hw_has_indicator_led()
{
    return true;
}

bool hw_has_otg_function()
{
#ifdef ARDUINO
    return peri_init_st[E_PERI_BQ25896];
#else
    return false;
#endif
}


/*********************************************************************************
 *                              Audio playback (stubs — no codec)
 * *******************************************************************************/

void hw_fat_list(vector<AudioParams_t> &list, const char *dirname, uint8_t levels)
{
    /* No FFat on T-Deck Pro */
}

bool hw_sd_list(vector<AudioParams_t> &list, const char *dirname, uint8_t levels)
{
    /* Audio playback not supported */
    return false;
}

bool hw_mount_sd()
{
#ifdef ARDUINO
    return hw_sd_begin();
#else
    return false;
#endif
}

void hw_get_filesystem_music(vector<AudioParams_t> &list)
{
    /* No audio support */
}

void hw_set_sd_music_play(audio_source_type_t source_type, const char *filename)
{
    /* No audio codec */
}

void hw_set_play_stop()
{
}

void hw_set_sd_music_pause()
{
}

void hw_set_sd_music_resume()
{
}

bool hw_player_running()
{
    return false;
}

void hw_set_volume(uint8_t volume)
{
    /* No audio codec */
}

uint8_t hw_get_volume()
{
    return 0;
}


/*********************************************************************************
 *                              Input device control
 * *******************************************************************************/

void hw_disable_input_devices()
{
    /* Handled by ui_define.h disable_input_devices() */
}

void hw_enable_input_devices()
{
    /* Handled by ui_define.h enable_input_devices() */
}


/*********************************************************************************
 *                              Misc
 * *******************************************************************************/

void hw_low_power_loop()
{
#ifdef ARDUINO
    delay(100);
#endif
}

uint8_t hw_get_disp_min_brightness()
{
    return dev_conts_var.min_brightness;
}

uint16_t hw_get_disp_max_brightness()
{
    return dev_conts_var.max_brightness;
}

uint8_t hw_get_min_charge_current()
{
    return dev_conts_var.min_charge_current;
}

uint16_t hw_get_max_charge_current()
{
    return dev_conts_var.max_charge_current;
}

uint8_t hw_get_charge_level_nums()
{
    return dev_conts_var.charge_level_nums;
}

uint8_t hw_get_charge_steps()
{
    return dev_conts_var.charge_steps;
}

void hw_set_cpu_freq(uint32_t mhz)
{
#ifdef ARDUINO
    setCpuFrequencyMhz(mhz);
#endif
}

void hw_print_mem_info()
{
#ifdef ARDUINO
    Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %u\n", ESP.getFreePsram());
    Serial.printf("Total heap: %u\n", ESP.getHeapSize());
    Serial.printf("Total PSRAM: %u\n", ESP.getPsramSize());
    Serial.printf("Used heap: %u\n", ESP.getHeapSize() - ESP.getFreeHeap());
    Serial.printf("Used PSRAM: %u\n", ESP.getPsramSize() - ESP.getFreePsram());
#endif
}


/*********************************************************************************
 *                              IR Remote (stub — not present)
 * *******************************************************************************/

void hw_set_remote_code(uint32_t nec_code)
{
}

void hw_get_remote_code(uint64_t &result)
{
    result = 0;
}

void hw_ir_function_select(bool enableSend)
{
}


/*********************************************************************************
 *                              Magnetometer (stub — not present)
 * *******************************************************************************/

void hw_mag_enable(bool enable)
{
}

float hw_mag_get_polar()
{
    return 0.0f;
}


/*********************************************************************************
 *                              BME280 (stub — not present)
 * *******************************************************************************/

void hw_bme_enable(bool enable)
{
}

void hw_bme_get_data(float &temp, float &humi, float &press, float &alt)
{
    temp = 0; humi = 0; press = 0; alt = 0;
}


/*********************************************************************************
 *                              Trackball / Button callbacks (stub)
 * *******************************************************************************/

using TrackballEventCallback = void(*)(uint8_t dir);
using ButtonEventCallback = void(*)(uint8_t idx, uint8_t state);

void hw_set_trackball_callback(TrackballEventCallback callback)
{
}

void hw_set_button_callback(ButtonEventCallback callback)
{
}


/*********************************************************************************
 *                              Device info strings
 * *******************************************************************************/

const char *hw_get_device_power_tips_string()
{
    return "BQ25896 + BQ27220";
}

const char *hw_get_firmware_hash_string()
{
#ifdef ARDUINO
    static char hash[16];
    snprintf(hash, sizeof(hash), "%08X", (uint32_t)ESP.getSketchMD5().substring(0, 8).toInt());
    return hash;
#else
    return "N/A";
#endif
}

const char *hw_get_chip_id_string()
{
#ifdef ARDUINO
    static char chipId[18];
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(chipId, sizeof(chipId), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return chipId;
#else
    return "N/A";
#endif
}


/*********************************************************************************
 *                              USB RF Switch (stub — not present)
 * *******************************************************************************/

void hw_set_usb_rf_switch(bool to_usb)
{
}


/*********************************************************************************
 *                              Audio effects (stub — no codec)
 * *******************************************************************************/

void hw_set_audio_effect_3d(bool enable)
{
}

void hw_set_audio_effect_ab_class(bool enable)
{
}


/*********************************************************************************
 *                              NRF24 (not present)
 * *******************************************************************************/

bool hw_has_nrf24()
{
    return false;
}
