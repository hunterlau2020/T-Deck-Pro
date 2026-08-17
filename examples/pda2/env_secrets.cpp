/**
 * @file      env_secrets.cpp
 * @brief     Parser for the device-side secrets file SPIFFS /env.cfg.
 *
 * Format (see examples/pda2/env.cfg.example):
 *   # comment
 *   OPENROUTER_KEY=sk-or-v1-...
 *   OWM_KEY=08db...
 *   WEATHER_COORDS=lat=22.54&lon=114.06
 *
 * The file is read lazily (first env_get call) and cached. Secrets stay
 * on the device: NVS and /env.cfg hold them, the gitignored
 * config_keys.h carries compile-time dev values, and TRACKED source
 * contains no real keys.
 */
#include "Arduino.h"
#include "env_secrets.h"
#include <SPIFFS.h>

#define ENV_PATH   "/env.cfg"
#define ENV_MAX_ENTRIES 8

typedef struct {
    char key[24];
    char val[96];
} env_entry_t;

static env_entry_t s_env[ENV_MAX_ENTRIES];
static int s_env_cnt = 0;
static bool s_env_loaded = false;

static void env_load(void)
{
    if (s_env_loaded) return;
    s_env_loaded = true;
    s_env_cnt = 0;

    if (!SPIFFS.begin(false)) {
        Serial.println("[env] SPIFFS unavailable - no /env.cfg");
        return;
    }
    if (!SPIFFS.exists(ENV_PATH)) {
        Serial.println("[env] /env.cfg absent - compile-time defaults apply");
        return;
    }
    File f = SPIFFS.open(ENV_PATH, FILE_READ);
    if (!f) return;
    while (f.available() && s_env_cnt < ENV_MAX_ENTRIES) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String k = line.substring(0, eq);
        String v = line.substring(eq + 1);
        k.trim();
        v.trim();
        if (k.length() == 0 || v.length() == 0) continue;
        env_entry_t *e = &s_env[s_env_cnt++];
        strncpy(e->key, k.c_str(), sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        strncpy(e->val, v.c_str(), sizeof(e->val) - 1);
        e->val[sizeof(e->val) - 1] = '\0';
        Serial.printf("[env] %s loaded\n", e->key);
    }
    f.close();
}

bool env_get(const char *key, char *out, int outlen)
{
    if (!key || !out || outlen <= 0) return false;
    env_load();
    for (int i = 0; i < s_env_cnt; i++) {
        if (strcmp(s_env[i].key, key) == 0) {
            strncpy(out, s_env[i].val, outlen - 1);
            out[outlen - 1] = '\0';
            return true;
        }
    }
    return false;
}
