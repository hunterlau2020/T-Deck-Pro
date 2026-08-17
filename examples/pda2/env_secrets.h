/**
 * @file      env_secrets.h
 * @brief     Runtime secrets/config from SPIFFS /env.cfg ("KEY=VALUE" lines,
 *            '#' comments). The file lives on the DEVICE, never in git -
 *            together with the gitignored config_keys.h it replaces every
 *            hard-coded default key in tracked source.
 */
#pragma once

/**
 * @brief Look up a value from SPIFFS /env.cfg.
 *        The file is parsed lazily on first call and cached in RAM.
 * @param key  Key name (e.g. "AI_KEY", "OWM_KEY", "WEATHER_COORDS").
 * @param out  Output buffer (unchanged when the key is absent).
 * @param outlen Buffer size.
 * @return true when the key was found and copied.
 */
bool env_get(const char *key, char *out, int outlen);
