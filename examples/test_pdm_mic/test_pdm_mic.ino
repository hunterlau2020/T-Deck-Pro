/**
 * @file      test_pdm_mic.ino
 * @brief     PDM microphone probe with earphone playback (T-Deck-Pro).
 *
 * FINAL VERDICT (2026-09-10, v1..v11): the audio-variant board's PDM mic
 * is NOT usable - across ~200 6-second windows, GPIO41 high/low/floating,
 * both PDM half-bit streams and supervised close-talk, speech never rose
 * above the ambient electrical floor (per-second VU), while that floor
 * stayed constant (-1494 pedestal). Mic unpopulated or acoustically
 * ported nowhere; not firmware-fixable. Details: issue_list §16.1.
 *
 * Kept for the four audio rules it established (all needed by the future
 * letter-TTS feature):
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
/* BOARD_6609_EN (GPIO41, "enable 7682 module" on the 4G variant): on the
 * audio variant the same pad powers the audio section - the factory
 * pcm5102a_init() drives it HIGH before any sound comes out. Without it
 * the probe played "successfully" into a dead rail (serial evidence
 * 2026-09-10: full decode pipeline, zero sound). */
#define AUDIO_PWR_EN 41
#define SAMPLE_RATE  16000
#define REC_SECONDS  6

#define REC_BYTES    (SAMPLE_RATE * REC_SECONDS * 2)   /* 16-bit mono */
#define WAV_SIZE     (44 + REC_BYTES)
/* playback copy: 44.1 kHz stereo - the format domain of everything that
 * has ever been audible on this port (stock MP3, cues); the 16k-mono WAV
 * path was the one item nobody ever clearly heard */
#define OUT_RATE     44100
#define OUT_SIZE     (44 + OUT_RATE * REC_SECONDS * 4)

static Audio audio;
static uint8_t *wav_buf = NULL;      /* PSRAM: header + samples */
static uint8_t *out_buf = NULL;      /* PSRAM: 44.1k stereo playback copy */

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
/* The Audio lib installs its I2S_NUM_0 TX driver ONCE in its constructor
 * and never reinstalls it. PDM RX needs the same port, so recording kills
 * the lib's driver; this replica (field-for-field copy of the lib ctor
 * config - 16000 Hz, tx_desc_auto_clear=true, APLL off) rebuilds it.
 * v1/v2 of this probe used a hand-rolled config (44100, no auto-clear) and
 * produced total silence even though the decode pipeline ran - serial
 * evidence 2026-09-10: boot-time stock MP3 through the untouched ctor
 * driver was audible, the juggling after it was not. */
static bool port_alive = true;      /* ctor driver intact until first record */

static void i2s_tx_install(void)
{
    i2s_driver_uninstall(I2S_NUM_0);

    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = 16000;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 8;
    cfg.dma_buf_len = 1024;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    cfg.fixed_mclk = I2S_PIN_NO_CHANGE;
    esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
    Serial.printf("[probe] tx reinstall: %s\n", esp_err_to_name(err));

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
    /* v10: ONLY_LEFT decoded a clean-but-nearly-deaf stream (ambient fine,
     * speech ~2x ambient at best over 120 windows). PDM half-bit streams
     * come in L/R pairs - if the mic drives the other half, ONLY_LEFT
     * yields exactly this "quiet floor, no voice" signature. */
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
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
    port_alive = false;                         /* RX driver killed the TX one */

    /* flush the DMA pipeline of stale samples */
    uint8_t flush[2048];
    size_t n = 0;
    i2s_read(I2S_NUM_0, flush, sizeof(flush), &n, portMAX_DELAY);

    Serial.printf("[probe] REC %ds - SPEAK NOW (live levels below)\n",
                  REC_SECONDS);
    size_t offset = 44, remaining = REC_BYTES;
    /* live VU: per-second AC peak, printed as we go - if the caller is
     * speaking, the second they speak must jump way above the ambient
     * ~1000 counts, regardless of window timing */
    size_t sec_bytes = 0;
    int32_t sec_peak = 0;
    int sec_idx = 0;
    while (remaining > 0) {
        size_t to_read = remaining > 4096 ? 4096 : remaining;
        size_t got = 0;
        if (i2s_read(I2S_NUM_0, wav_buf + offset, to_read, &got,
                     pdMS_TO_TICKS(2000)) != ESP_OK || got == 0) break;
        for (size_t i = 0; i < got; i += 2) {
            int32_t v = (int32_t)(int16_t)(wav_buf[offset + i] |
                                           (wav_buf[offset + i + 1] << 8));
            v -= -1491;                        /* strip the known pedestal */
            if (v < 0) v = -v;
            if (v > sec_peak) sec_peak = v;
        }
        sec_bytes += got;
        if (sec_bytes >= SAMPLE_RATE * 2) {
            Serial.printf("[probe]   sec %d: peak=%d%s\n", sec_idx, sec_peak,
                          sec_peak > 3000 ? "  <== SIGNAL!" : "");
            sec_idx++;
            sec_bytes = 0;
            sec_peak = 0;
        }
        offset += got;
        remaining -= got;
    }
    if (sec_bytes) Serial.printf("[probe]   sec %d: peak=%d%s\n", sec_idx,
                                 sec_peak, sec_peak > 3000 ? "  <== SIGNAL!" : "");
    wav_header(wav_buf, offset - 44);
    Serial.printf("[probe] REC done: %u/%u bytes\n",
                  (unsigned)(offset - 44), (unsigned)REC_BYTES);
    return true;
}

static double s_last_gain = 16.0;

/* DC-removal + gain normalization so the playback is audible regardless
 * of speech level: the raw PDM stream rides on a ~-1500 pedestal which
 * both buries the voice and eats headroom. */
static void normalize_record(uint32_t data_bytes)
{
    int16_t *s = (int16_t *)(wav_buf + 44);
    int n = data_bytes / 2;
    if (n < 2) return;

    int64_t sum = 0;
    for (int i = 0; i < n; i++) sum += s[i];
    double mean = (double)sum / n;

    int32_t peak = 1;
    for (int i = 0; i < n; i++) {
        int32_t a = (int32_t)lround(s[i] - mean);
        if (a < 0) a = -a;
        if (a > peak) peak = a;
    }

    double gain = 30000.0 / peak;               /* target ~91% FS */
    if (gain > 16.0) gain = 16.0;               /* don't blow up silence */
    s_last_gain = gain;
    for (int i = 0; i < n; i++) {
        int32_t v = (int32_t)lround((s[i] - mean) * gain);
        if (v > 32000) v = 32000;
        if (v < -32000) v = -32000;
        s[i] = (int16_t)v;
    }
    Serial.printf("[probe] normalize: dc=%.1f removed, peak=%d, gain=%.1fx\n",
                  mean, peak, gain);
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
    Serial.printf("[probe] stats (post-normalize): peak=%d rms=%.1f dc=%.1f "
                  "zeros=%u%% clipped=%u%% xings/s=%u\n",
                  peak, rms, mean,
                  (unsigned)(zeros * 100 / n), (unsigned)(clipped * 100 / n),
                  (unsigned)(xings / REC_SECONDS));
    if (s_last_gain < 15.0)
        Serial.printf("[probe] VERDICT: SPEECH captured (gain %.1fx) - "
                      "confirm by listening\n", s_last_gain);
    else
        Serial.println("[probe] VERDICT: quiet window - speak right after "
                       "the long beep");
}

/* Upsample the normalized 16k mono capture to 44.1k stereo and write it -
 * see the OUT_RATE note above. */
static bool save_wav(void)
{
    uint32_t in_data;
    memcpy(&in_data, wav_buf + 40, 4);
    int in_n = in_data / 2;                       /* 16-bit samples */
    const int16_t *s = (const int16_t *)(wav_buf + 44);
    int32_t out_frames = (int32_t)((int64_t)in_n * OUT_RATE / SAMPLE_RATE);

    uint8_t h[44];
    uint32_t out_data = (uint32_t)out_frames * 4;
    memcpy(h, "RIFF", 4);           put_u32(h + 4, 36 + out_data);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);      put_u32(h + 16, 16);
    put_u16(h + 20, 1);             put_u16(h + 22, 2);        /* stereo */
    put_u32(h + 24, OUT_RATE);      put_u32(h + 28, OUT_RATE * 4);
    put_u16(h + 32, 4);             put_u16(h + 34, 16);
    memcpy(h + 36, "data", 4);      put_u32(h + 40, out_data);
    memcpy(out_buf, h, 44);

    int16_t *o = (int16_t *)(out_buf + 44);
    for (int32_t j = 0; j < out_frames; j++) {
        double t = (double)j * SAMPLE_RATE / OUT_RATE;
        int i = (int)t;
        double frac = t - i;
        int i2 = (i + 1 < in_n) ? i + 1 : i;
        int32_t v = (int32_t)lround(s[i] * (1.0 - frac) + s[i2] * frac);
        o[j * 2]     = (int16_t)v;                /* L */
        o[j * 2 + 1] = (int16_t)v;                /* R */
    }

    size_t len = 44 + out_data;
    File f = SPIFFS.open("/probe_mic.wav", FILE_WRITE);
    if (!f) goto nospc;
    f.write(out_buf, len);
    f.close();
    return true;
nospc:
    Serial.println("[probe] wav write failed - dropping demo mp3s, retry");
    const char *mp3s[] = {"/iphone_call.mp3", "/3-start.mp3", "/5-link.mp3",
                          "/4-ulink.mp3", "/2-on.mp3", "/1-off.mp3", NULL};
    for (int i = 0; mp3s[i]; i++) SPIFFS.remove(mp3s[i]);
    f = SPIFFS.open("/probe_mic.wav", FILE_WRITE);
    if (!f) return false;
    f.write(out_buf, len);
    f.close();
    return true;
}

static void play_wait(const char *path)
{
    if (!port_alive) i2s_tx_install();   /* leave the ctor driver alone
                                          * until recording has killed it */
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

/* Two distinct cues (user-requested flow 2026-09-10): one long low tone
 * = "record NOW, speak", two short high tones = "playback NOW, listen".
 * Both 44.1 kHz stereo - the format domain of the proven stock MP3 path. */
static void write_tone(const char *path, uint32_t freq, uint32_t ms,
                       int pulses)
{
    const uint32_t sr = 44100;
    uint32_t on = sr * ms / 1000;
    uint32_t gap = sr * 60 / 1000;
    uint32_t frames = (uint32_t)pulses * on + (uint32_t)(pulses - 1) * gap;
    uint32_t data = frames * 4;                 /* 16-bit stereo */
    File f = SPIFFS.open(path, FILE_WRITE);
    if (!f) { Serial.printf("[probe] %s write failed\n", path); return; }

    uint8_t h[44];
    memcpy(h, "RIFF", 4);           put_u32(h + 4, 36 + data);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);      put_u32(h + 16, 16);
    put_u16(h + 20, 1);             put_u16(h + 22, 2);        /* stereo */
    put_u32(h + 24, sr);            put_u32(h + 28, sr * 4);
    put_u16(h + 32, 4);             put_u16(h + 34, 16);
    memcpy(h + 36, "data", 4);      put_u32(h + 40, data);
    f.write(h, 44);
    for (int p = 0; p < pulses; p++) {
        for (uint32_t i = 0; i < on; i++) {
            int16_t v = (int16_t)(25000 * sin(2 * M_PI * freq * i / sr));
            uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)v,
                            (uint8_t)(v >> 8)};
            f.write(b, 4);
        }
        if (p < pulses - 1) {
            uint8_t z[4] = {0, 0, 0, 0};
            for (uint32_t i = 0; i < gap; i++) f.write(z, 4);
        }
    }
    f.close();
}

static void make_cues(void)
{
    write_tone("/cue_rec.wav", 440, 400, 1);    /* beep: record NOW */
    /* v6's 110 ms beeps were never reported audible while the 400 ms cue
     * was - make the verdict beeps long and loud */
    write_tone("/cue_play.wav", 988, 300, 2);   /* beep-beep: quiet window */
    write_tone("/cue_hit.wav", 988, 300, 3);    /* beep-beep-beep: VOICE */
}

void setup()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n[probe] === PDM mic probe (speak after each beep) ===");

    /* pad stays Hi-Z at boot (see audio_pad_release note) */
    audio_pad_release();
    Serial.printf("[probe] audio power pad (GPIO%d) released (Hi-Z)\n",
                  AUDIO_PWR_EN);

    if (!SPIFFS.begin(true)) {
        Serial.println("[probe] SPIFFS mount FAILED");
        while (1) delay(100);
    }
    make_cues();
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(21);

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

    out_buf = (uint8_t *)heap_caps_malloc(OUT_SIZE, MALLOC_CAP_SPIRAM |
                                           MALLOC_CAP_8BIT);
    Serial.printf("[probe] out buffer: %s (%u bytes)\n",
                  out_buf ? "PSRAM" : "FAILED", (unsigned)OUT_SIZE);
    if (!out_buf) while (1) delay(100);
}

/* v9 final mic experiment: driven HIGH = deaf (v2..v7), driven LOW =
 * deaf (v8). The only state never re-tested since v1 is FLOATING - and
 * v1's "speech" bursts (peak 32768 but only ~40 zero-crossings/s) also
 * match hand-coupled noise on a floating data line, i.e. possibly no mic
 * at all. So: release the pad (Hi-Z) whenever not playing; speak-
 * continuously test decides populated-mic vs no-mic. */
static void audio_pad_release(void)
{
    pinMode(AUDIO_PWR_EN, INPUT);           /* Hi-Z, v1 state */
}

/* Playback needs the pad HIGH (output section power) - gate every play
 * with HIGH brackets, then release back to floating. */
static void play_gated(const char *path)
{
    pinMode(AUDIO_PWR_EN, OUTPUT);
    digitalWrite(AUDIO_PWR_EN, HIGH);
    play_wait(path);
    audio_pad_release();
}

void loop()
{
    Serial.println("\n[probe] --- cycle ---");
    play_gated("/cue_rec.wav");             /* long beep: REC NOW */
    delay(400);                             /* pad released already */
    if (!pdm_record()) { delay(3000); return; }
    normalize_record(REC_BYTES);
    print_stats();
    if (!save_wav()) { delay(3000); return; }
    /* audible verdict: 3 high beeps = voice in this window, then playback;
     * 2 beeps = quiet window, skip listening */
    bool speech = s_last_gain < 15.0;
    play_gated(speech ? "/cue_hit.wav" : "/cue_play.wav");
    /* v11 (user request): ALWAYS play the window back - the ear is the
     * final judge of what the mic captured, hiss or voice */
    Serial.println(speech ? "[probe] PLAYBACK - LISTEN (your voice)"
                          : "[probe] PLAYBACK - ambient (hiss = quiet "
                            "window)");
    play_gated("/probe_mic.wav");
    delay(3000);
}
