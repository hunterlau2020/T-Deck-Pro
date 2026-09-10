/**
 * @file      test_pdm_mic.ino
 * @brief     PDM microphone probe with earphone playback (T-Deck-Pro).
 *
 * Answers: "is the PDM mic (DATA 17 / CLOCK 18) actually populated on THIS
 * board?" - the option matrix never states which configuration carries the
 * mic (issue_list §16), and the PCM5102A is output-only.
 *
 * Headless loop (no display/touch needed), ~12 s cycle:
 *   1. beep    - 440 Hz cue WAV through PCM5102A; the output path is already
 *                device-proven, so the beep doubles as the "speak NOW" cue
 *   2. record  - 5 s from the PDM mic (I2S_NUM_0 RX, 16 kHz mono -> PSRAM)
 *   3. stats   - peak / RMS / DC / zero-crossings / flatline counters
 *   4. save    - /probe_mic.wav to SPIFFS (drops stock demo MP3s on ENOSPC)
 *   5. play    - the recording back through the 3.5mm jack
 *
 * Verdict: hearing your own voice = mic present and working. The serial
 * stats are the objective version - a missing mic leaves the data line
 * floating, which shows up as all-zero / DC-saturated samples with
 * near-zero zero-crossings.
 *
 * I2S_NUM_0 is used in both directions, replicating the battle-tested
 * ui_voice_ai.cpp sequence (commits 90b4545..3ee077a): uninstall before
 * reinstalling the port the other way, and IDF-install the TX driver
 * before Audio.setPinout().
 *
 * Usage: plug earphones, flash, watch serial; speak during the 5 s after
 * each beep, then listen to the playback.
 */

#include <Arduino.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "Audio.h"
#include "FS.h"
#include "SPIFFS.h"

#define MIC_DATA     17
#define MIC_CLOCK    18
#define I2S_BCLK     7
#define I2S_DOUT     8
#define I2S_LRC      9
#define SAMPLE_RATE  16000
#define REC_SECONDS  5

#define REC_BYTES    (SAMPLE_RATE * REC_SECONDS * 2)   /* 16-bit mono */
#define WAV_SIZE     (44 + REC_BYTES)

static Audio audio;
static uint8_t *wav_buf = NULL;      /* PSRAM: header + samples */

void audio_info(const char *info)
{
    Serial.print("audio_info: ");
    Serial.println(info);
}

/* ---- WAV header ------------------------------------------------------- */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}
static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = v; p[1] = v >> 8;
}
static void wav_header(uint8_t *b, uint32_t data_bytes)
{
    memcpy(b, "RIFF", 4);           put_u32(b + 4, 36 + data_bytes);
    memcpy(b + 8, "WAVE", 4);
    memcpy(b + 12, "fmt ", 4);      put_u32(b + 16, 16);
    put_u16(b + 20, 1);             put_u16(b + 22, 1);
    put_u32(b + 24, SAMPLE_RATE);   put_u32(b + 28, SAMPLE_RATE * 2);
    put_u16(b + 32, 2);             put_u16(b + 34, 16);
    memcpy(b + 36, "data", 4);      put_u32(b + 40, data_bytes);
}

/* ---- shared I2S port dance (one port, two directions) ------------------ */
static void i2s_tx_install(void)
{
    i2s_driver_uninstall(I2S_NUM_0);            /* stale RX/Audio handle */

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 44100;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    Serial.printf("[probe] tx i2s_driver_install: %s\n", esp_err_to_name(err));

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_BCLK;
    pins.ws_io_num = I2S_LRC;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    i2s_set_pin(I2S_NUM_0, &pins);

    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);
}

static bool pdm_record(void)
{
    i2s_driver_uninstall(I2S_NUM_0);            /* stale TX handle */

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[probe] rx i2s_driver_install failed: %s\n",
                      esp_err_to_name(err));
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.bck_io_num = I2S_PIN_NO_CHANGE;
    pins.ws_io_num = MIC_CLOCK;                 /* PDM clock out */
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = MIC_DATA;                /* PDM data in */
    i2s_set_pin(I2S_NUM_0, &pins);

    /* flush the DMA pipeline of stale samples */
    uint8_t flush[2048];
    size_t n = 0;
    i2s_read(I2S_NUM_0, flush, sizeof(flush), &n, portMAX_DELAY);

    Serial.printf("[probe] REC %ds - SPEAK NOW\n", REC_SECONDS);
    size_t offset = 44, remaining = REC_BYTES;
    while (remaining > 0) {
        size_t to_read = remaining > 4096 ? 4096 : remaining;
        size_t got = 0;
        if (i2s_read(I2S_NUM_0, wav_buf + offset, to_read, &got,
                     pdMS_TO_TICKS(2000)) != ESP_OK || got == 0) break;
        offset += got;
        remaining -= got;
    }
    wav_header(wav_buf, offset - 44);
    Serial.printf("[probe] REC done: %u/%u bytes\n",
                  (unsigned)(offset - 44), (unsigned)REC_BYTES);
    return true;
}

static void print_stats(void)
{
    const int16_t *s = (const int16_t *)(wav_buf + 44);
    int n = REC_BYTES / 2;
    int64_t sum = 0, sum_sq = 0;
    int32_t peak = 0;
    uint32_t zeros = 0, clipped = 0, xings = 0;
    for (int i = 0; i < n; i++) {
        int32_t v = s[i];
        int32_t a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        if (v == 0) zeros++;
        if (v == 32767 || v == -32768) clipped++;
        if (i && ((v < 0) != (s[i - 1] < 0))) xings++;
        sum += v;
        sum_sq += (int64_t)v * v;
    }
    double mean = (double)sum / n;
    double rms = sqrt((double)sum_sq / n);
    Serial.printf("[probe] stats: peak=%d rms=%.1f dc=%.1f zeros=%u%% "
                  "clipped=%u%% xings/s=%u\n",
                  peak, rms, mean,
                  (unsigned)(zeros * 100 / n), (unsigned)(clipped * 100 / n),
                  (unsigned)(xings / REC_SECONDS));
    if (zeros * 100 / n > 95)
        Serial.println("[probe] VERDICT: FLATLINE (all zeros) - no mic / "
                       "mic not driven");
    else if (rms < 5.0 && xings < 50)
        Serial.println("[probe] VERDICT: near-DC, no speech energy - likely "
                       "no mic");
    else
        Serial.println("[probe] VERDICT: signal present - mic looks ALIVE "
                       "(confirm by listening)");
}

static bool save_wav(void)
{
    size_t len = 44 + (wav_buf ? 0 : 0);
    /* actual length = header says data_bytes; recompute from header */
    uint32_t data_bytes;
    memcpy(&data_bytes, wav_buf + 40, 4);
    len = 44 + data_bytes;

    File f = SPIFFS.open("/probe_mic.wav", FILE_WRITE);
    if (!f) goto nospc;
    f.write(wav_buf, len);
    f.close();
    return true;
nospc:
    Serial.println("[probe] wav write failed - dropping demo mp3s, retry");
    const char *mp3s[] = {"/iphone_call.mp3", "/3-start.mp3", "/5-link.mp3",
                          "/4-ulink.mp3", "/2-on.mp3", "/1-off.mp3", NULL};
    for (int i = 0; mp3s[i]; i++) SPIFFS.remove(mp3s[i]);
    f = SPIFFS.open("/probe_mic.wav", FILE_WRITE);
    if (!f) return false;
    f.write(wav_buf, len);
    f.close();
    return true;
}

static void play_wait(const char *path)
{
    i2s_tx_install();
    bool ok = audio.connecttoFS(SPIFFS, path);
    Serial.printf("[probe] play %s: %d\n", path, ok ? 1 : 0);
    if (!ok) return;
    while (audio.isRunning()) {
        audio.loop();
        delay(1);
    }
    audio.stopSong();
    Serial.println("[probe] play done");
}

/* 440 Hz cue so the user knows when to speak (output path is proven). */
static void make_cue(void)
{
    uint32_t data = SAMPLE_RATE / 3;             /* ~0.33 s */
    File f = SPIFFS.open("/cue.wav", FILE_WRITE);
    if (!f) { Serial.println("[probe] cue write failed"); return; }
    uint8_t hdr[44];
    wav_header(hdr, data * 2);
    f.write(hdr, 44);
    for (uint32_t i = 0; i < data; i++) {
        int16_t v = (int16_t)(12000 * sin(2 * M_PI * 440 * i / SAMPLE_RATE));
        uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
        f.write(b, 2);
    }
    f.close();
}

void setup()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n[probe] === PDM mic probe (speak after each beep) ===");

    wav_buf = (uint8_t *)heap_caps_malloc(WAV_SIZE, MALLOC_CAP_SPIRAM |
                                           MALLOC_CAP_8BIT);
    if (!wav_buf) {
        wav_buf = (uint8_t *)malloc(WAV_SIZE);
        Serial.printf("[probe] wav buffer: %s (%u bytes)\n",
                      wav_buf ? "heap" : "FAILED", (unsigned)WAV_SIZE);
        if (!wav_buf) while (1) delay(100);
    } else {
        Serial.printf("[probe] wav buffer: PSRAM (%u bytes)\n",
                      (unsigned)WAV_SIZE);
    }

    if (!SPIFFS.begin(true)) {
        Serial.println("[probe] SPIFFS mount FAILED");
        while (1) delay(100);
    }
    make_cue();
}

void loop()
{
    Serial.println("\n[probe] --- cycle ---");
    play_wait("/cue.wav");
    delay(400);
    if (!pdm_record()) { delay(3000); return; }
    print_stats();
    if (!save_wav()) { delay(3000); return; }
    Serial.println("[probe] PLAYBACK - listen (your voice = mic OK)");
    play_wait("/probe_mic.wav");
    delay(3000);
}
