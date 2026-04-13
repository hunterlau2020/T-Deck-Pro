/**
 * @file      hw_sx1262.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * LoRa SX1262 radio driver for PDA on T-Deck Pro.
 * Ported from LilyGoLib/examples/pda/hw_sx1262.cpp.
 * Uses RadioLib directly with shared SPI mutex.
 */

#include "hal_interface.h"

#if defined(ARDUINO_T_DECK_PRO) || defined(ARDUINO)

#ifdef ARDUINO
#include <RadioLib.h>
#include "utilities.h"
#include "factory.h"
#include "peripheral.h"

static SX1262 radio = new Module(BOARD_LORA_CS, BOARD_LORA_INT, BOARD_LORA_RST, BOARD_LORA_BUSY);

static EventGroupHandle_t radioEvent = NULL;
static uint32_t last_send_millis = 0;

#define LORA_ISR_FLAG                  _BV(0)

static void hw_radio_isr()
{
    BaseType_t xHigherPriorityTaskWoken, xResult;
    xHigherPriorityTaskWoken = pdFALSE;
    xResult = xEventGroupSetBitsFromISR(
                  radioEvent,
                  LORA_ISR_FLAG,
                  &xHigherPriorityTaskWoken);
    if (xResult == pdPASS) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void hw_radio_begin()
{
    radioEvent = xEventGroupCreate();

    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);

    int state = radio.begin(868.0, 125.0, 10, 6, 0xAB, 22, 15, 0, false);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[PDA Radio] SX1262 init failed: %d\n", state);
        shared_spi_unlock();
        return;
    }

    radio.setTCXO(2.4);
    radio.setDio2AsRfSwitch(true);
    radio.setPacketSentAction(hw_radio_isr);

    shared_spi_unlock();
    Serial.println("[PDA Radio] SX1262 init OK");
}
#endif

int16_t hw_set_radio_params(radio_params_t &params)
{
#ifdef ARDUINO
    int16_t state = 0;
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);

    state = radio.setFrequency(params.freq);
    if (state == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println(F("Selected frequency is invalid for this module!"));
    }
    state = radio.setBandwidth(params.bandwidth);
    if (state == RADIOLIB_ERR_INVALID_BANDWIDTH) {
        Serial.println(F("Selected bandwidth is invalid for this module!"));
    }
    state = radio.setSpreadingFactor(params.sf);
    if (state == RADIOLIB_ERR_INVALID_SPREADING_FACTOR) {
        Serial.println(F("Selected spreading factor is invalid for this module!"));
    }
    state = radio.setCodingRate(params.cr);
    if (state == RADIOLIB_ERR_INVALID_CODING_RATE) {
        Serial.println(F("Selected coding rate is invalid for this module!"));
    }
    state = radio.setSyncWord(params.syncWord);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.println(F("Unable to set sync word!"));
    }
    state = radio.setOutputPower(params.power);
    if (state == RADIOLIB_ERR_INVALID_OUTPUT_POWER) {
        Serial.println(F("Selected output power is invalid for this module!"));
    }
    state = radio.setCurrentLimit(140);
    if (state == RADIOLIB_ERR_INVALID_CURRENT_LIMIT) {
        Serial.println(F("Selected current limit is invalid for this module!"));
    }

    switch (params.mode) {
    case RADIO_DISABLE:
        state = radio.standby();
        break;
    case RADIO_TX:
        state = radio.startTransmit("");
        break;
    case RADIO_RX:
        state = radio.startReceive();
        break;
    case RADIO_CW:
        break;
    default:
        break;
    }
    shared_spi_unlock();
    return state;
#else
    return 0;
#endif
}

void hw_get_radio_params(radio_params_t &params)
{
    params.bandwidth = 125.0;
    params.freq = RADIO_DEFAULT_FREQUENCY;
    params.cr = 5;
    params.isRunning = false;
    params.mode = RADIO_DISABLE;
    params.sf = 12;
    params.power = 22;
    params.interval = 3000;
    params.syncWord = 0xCD;
}

void hw_set_radio_default()
{
    radio_params_t params;
    hw_get_radio_params(params);
    hw_set_radio_params(params);
}

void hw_set_radio_listening()
{
#ifdef ARDUINO
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);
    radio.startReceive();
    shared_spi_unlock();
#endif
}

void hw_set_radio_tx(radio_tx_params_t &params, bool continuous)
{
#ifdef ARDUINO
    if (continuous) {
        EventBits_t eventBits = xEventGroupWaitBits(radioEvent,
                                LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
        if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
            params.state = -1;
            return;
        }
    }

    radio.finishTransmit();

    if (!params.data) {
        Serial.println("tx data buffer is empty");
        params.state = -1;
        return;
    }

    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);
    params.state = radio.startTransmit(params.data, params.length);
    shared_spi_unlock();
#endif
}

void hw_get_radio_rx(radio_rx_params_t &params)
{
#ifdef ARDUINO
    EventBits_t eventBits = xEventGroupWaitBits(radioEvent, LORA_ISR_FLAG, pdTRUE, pdTRUE, pdTICKS_TO_MS(2));
    if ((eventBits & LORA_ISR_FLAG) != LORA_ISR_FLAG) {
        params.state = -1;
        return;
    }

    if (!params.data) {
        params.state = -1;
        return;
    }

    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);
    params.length = radio.getPacketLength();
    params.state = radio.readData(params.data, params.length);
    params.rssi = radio.getRSSI();
    params.snr = radio.getSNR();
    radio.startReceive();
    shared_spi_unlock();

    if (last_send_millis + 200 > millis()) {
        params.length = 0;
        return;
    }

    params.data[params.length] = '\0';

    if (params.state == RADIOLIB_ERR_NONE && params.length != 0) {
        Serial.println(F("[Radio] Received packet!"));
        Serial.printf("[RSSI]: %.1f dBm  [SNR]: %.1f dB\n", params.rssi, params.snr);
    }
#else
    params.length = 0;
#endif
}

bool radio_transmit(const uint8_t *data, size_t length)
{
#ifdef ARDUINO
    shared_spi_lock();
    shared_spi_prepare_device(BOARD_LORA_CS);
    int state = radio.transmit(const_cast<uint8_t*>(data), length);
    shared_spi_unlock();
    last_send_millis = millis();
    return (state == RADIOLIB_ERR_NONE);
#else
    return true;
#endif
}


static const float bandwidth_list[] = {41.7, 62.5, 125.0, 250.0, 500.0};
static const float power_level_list[] = {2, 5, 10, 12, 17, 20, 22};
static const float freq_list[] = {433.0, 470.0, 842.0, 850, 868.0, 915.0, 923.0, 945.0};

uint16_t radio_get_freq_length()
{
    return (sizeof(freq_list) / sizeof(freq_list[0]));
}

uint16_t radio_get_bandwidth_length()
{
    return (sizeof(bandwidth_list) / sizeof(bandwidth_list[0]));
}

uint16_t radio_get_tx_power_length()
{
    return (sizeof(power_level_list) / sizeof(power_level_list[0]));
}

const char *radio_get_freq_list()
{
    return "433MHz\n""470MHz\n""842MHZ\n""850MHZ\n""868MHz\n""915MHz\n""923MHz\n""945MHz";
}

float radio_get_freq_from_index(uint8_t index)
{
    if (index > radio_get_freq_length()) {
        return RADIO_DEFAULT_FREQUENCY;
    }
    return freq_list[index];
}

const char *radio_get_bandwidth_list(bool high_freq)
{
    return "41.7KHz\n""62.5KHz\n""125KHz\n""250KHz\n""500KHz";
}

float radio_get_bandwidth_from_index(uint8_t index)
{
    if (index > radio_get_bandwidth_length()) {
        return 125.0;
    }
    return bandwidth_list[index];
}

const char *radio_get_tx_power_list(bool high_freq)
{
    return "2dBm\n""5dBm\n""10dBm\n""12dBm\n""17dBm\n""20dBm\n""22dBm";
}

float radio_get_tx_power_from_index(uint8_t index)
{
    if (index > radio_get_tx_power_length()) {
        return 22;
    }
    return power_level_list[index];
}

#endif
