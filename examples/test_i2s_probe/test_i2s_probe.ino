/**
 * @file      test_i2s_probe.ino
 * @brief     Minimal audio-path probe for the T-Deck Pro (4G/A7682E variant).
 *
 * Plays /1-off.mp3 from SPIFFS through the PCM5102A I2S pins (BCLK 7,
 * LRC 9, DOUT 8) once at boot, then reports playback state on serial.
 * Purpose: answer "does THIS board actually have a DAC / wired audio
 * output on the 3.5mm jack?" - the 4G variant reuses GPIO 7/8 for the
 * modem (RI/ITR), so seller claims need a physical listen test.
 *
 * Usage: plug headphones into the 3.5mm jack, flash (with the SPIFFS
 * image uploaded first), listen for the short tone.
 */

#include <Arduino.h>
#include "Audio.h"
#include "FS.h"
#include "SPIFFS.h"

#define I2S_BCLK 7
#define I2S_DOUT 8
#define I2S_LRC 9

Audio audio;

/* ESP32-audioI2S weak callbacks: status + end-of-stream lines. */
void audio_info(const char *info)
{
    Serial.print("audio_info: ");
    Serial.println(info);
}
void audio_eof_mp3(const char *info)
{
    Serial.print("audio_eof_mp3: ");
    Serial.println(info);
}
void audio_eof_stream(const char *info)
{
    Serial.print("audio_eof_stream: ");
    Serial.println(info);
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("[probe] boot - PCM5102A audio path test");

    if (!SPIFFS.begin(true)) {
        Serial.println("[probe] SPIFFS mount FAILED");
        while (1) { delay(100); }
    }
    Serial.println("[probe] SPIFFS ok");

    bool ok = audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    Serial.printf("[probe] setPinout(BCLK=%d,LRC=%d,DOUT=%d) = %d\n",
                  I2S_BCLK, I2S_LRC, I2S_DOUT, ok ? 1 : 0);

    audio.setVolume(18); /* 0..21 */

    File f = SPIFFS.open("/1-off.mp3");
    Serial.printf("[probe] exists=%d size=%u name=\"%s\"\n",
                  SPIFFS.exists("/1-off.mp3") ? 1 : 0,
                  f ? (unsigned)f.size() : 0,
                  f ? f.name() : "(n/a)");
    if (f) f.close();

    bool r = audio.connecttoFS(SPIFFS, "/1-off.mp3");
    Serial.printf("[probe] connecttoFS returned %d, running=%d\n",
                  r ? 1 : 0, audio.isRunning() ? 1 : 0);
    Serial.flush();
}

static uint32_t last_report = 0;

void loop()
{
    audio.loop();

    if (millis() - last_report > 1000) {
        last_report = millis();
        Serial.printf("[probe] running=%d cur=%us\n",
                      audio.isRunning() ? 1 : 0,
                      (unsigned)audio.getAudioCurrentTime());
    }
}
