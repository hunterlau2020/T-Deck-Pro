/**
 * @file      minimax_audio.h
 * @brief     MiniMax speech APIs for the voice AI flow (China-reachable,
 *            replaces the Google/Gemini path, user request 2026-09-11):
 *            ASR  POST /v1/speech_to_text (multipart, model asr-1.0)
 *            TTS  POST /v1/t2a_v2 (JSON, model speech-2.6-turbo, hex mp3)
 *            Docs: platform.minimax.io, api domain api.minimax.io.
 *
 * Key resolution (user rule 2026-09-11): the AI Config minimax provider
 * key wins when configured; otherwise /env.cfg MINIMAX_AUDIO_KEY.
 */
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Resolve the MiniMax API key (AI Config first, env.cfg fallback).
 * @return true and key filled; false when no key is configured anywhere.
 */
bool minimax_audio_key(char *key, int key_len);

/**
 * @brief Speech-to-text: upload a WAV (16 kHz mono container required -
 *        raw PCM is rejected) and get the transcript.
 * @param text  Output transcript (truncated to text_len-1).
 * @param err   Human-readable error on failure.
 */
bool minimax_asr(const uint8_t *wav, size_t wav_len, const char *key,
                 char *text, int text_len, char *err, int err_len);

/**
 * @brief Text-to-speech: synthesize text and write the mp3 to a SPIFFS
 *        path ready for audio.connecttoFS().
 * @param spath SPIFFS destination (e.g. "/tts.mp3"), overwritten.
 */
bool minimax_tts(const char *text, const char *key, const char *spath,
                 char *err, int err_len);

#ifdef __cplusplus
}
#endif
