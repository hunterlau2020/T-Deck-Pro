/**
 * @file      ui_whoami.cpp
 * @brief     Whoami app: "Me" (GET /users/me/profile via the API key) +
 *            "Cfg" (the PenPal server config, MOVED here from PenPal's
 *            internal Cfg page - user request 2026-09-14).
 *
 * Layout: two tabs in the top row next to the back button (AI Text chat
 * pattern): [Me] shows the profile fields, [Cfg] holds the server URL /
 * API key / AI provider + Save/Test. PenPal's home Cfg button now pushes
 * this screen with the Cfg tab preselected (whoami_open_cfg()).
 *
 * Async: own small worker task + queue + lv_timer drain (voice-AI pattern)
 * - PenPal's pp_start machinery is generation-gated to the PenPal screen,
 * so Test/profile run through wa_task_func instead. NVS layout unchanged:
 * the same "penpal" namespace keys, so PenPal keeps working with whatever
 * is saved here (and vice versa).
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "ui_penpal.h"          /* pp_cfg_load / pp_cfg_from_nvs */
#include "penpal_api.h"
#include "openai_api.h"         /* ai_provider_* */
#include "secret_mask.h"
#include "fw_version.h"

#include <WiFi.h>
#include <freertos/queue.h>
#include <Preferences.h>
#include <time.h>

/* ---- tabs -------------------------------------------------------------- */
static lv_obj_t *s_me_tab_btn = NULL;
static lv_obj_t *s_cfg_tab_btn = NULL;
static lv_obj_t *s_me_page = NULL;
static lv_obj_t *s_cfg_page = NULL;
static bool s_wa_tab_cfg = false;          /* false = Me, true = Cfg */
static bool s_wa_kbd_active = false;

/* Me tab */
static lv_obj_t *s_me_label = NULL;
static lv_obj_t *s_me_status = NULL;
static pp_profile_t s_profile;
static bool s_profile_valid = false;
static bool s_profile_fetched = false;     /* auto-fetch once per power cycle */

/* ---- Me tab profile cache (user request 2026-09-16) -----------------------
 * The profile is stable data - persist it in NVS ("whoami" namespace) and
 * stop pulling it from the server on every entry/boot. Network is hit only
 * on explicit Refresh (or the very first run with no cache). The cache is
 * cleared when Cfg is saved: the key may point at another user then. */
static bool s_cache_read = false;          /* NVS consulted once per boot */
static uint32_t s_cache_ts = 0;            /* fetch time, 0 = unknown */

static bool wa_cache_load(void)
{
    Preferences nvs;
    if (!nvs.begin("whoami", true)) return false;      /* read-only open */
    pp_profile_t p;
    bool ok = nvs.getBytesLength("me_prof") == sizeof(p)
              && nvs.getBytes("me_prof", &p, sizeof(p)) == sizeof(p);
    uint32_t ts = ok ? nvs.getULong("me_ts", 0) : 0;
    nvs.end();
    if (!ok) return false;
    s_profile = p;
    s_profile_valid = true;
    s_cache_ts = ts;
    return true;
}

static void wa_cache_save(void)
{
    Preferences nvs;
    if (!nvs.begin("whoami", false)) return;
    nvs.putBytes("me_prof", &s_profile, sizeof(s_profile));
    time_t now = time(NULL);
    nvs.putULong("me_ts", now >= 1600000000 ? (uint32_t)now : 0);
    nvs.end();
    s_cache_ts = now >= 1600000000 ? (uint32_t)now : 0;
}

static void wa_cache_clear(void)
{
    Preferences nvs;
    if (nvs.begin("whoami", false)) {
        nvs.clear();
        nvs.end();
    }
    s_cache_ts = 0;
}

/* status line for a cache-served profile */
static void wa_me_status(const char *txt);   /* defined below */
static void wa_me_status_cached(void)
{
    struct tm tmv;
    time_t ts = (time_t)s_cache_ts;
    if (s_cache_ts > 0 && localtime_r(&ts, &tmv)) {
        char buf[48];
        strftime(buf, sizeof(buf), "cached %Y-%m-%d (Refresh)", &tmv);
        wa_me_status(buf);
    } else {
        wa_me_status("cached (press Refresh)");
    }
}

/* Cfg tab (moved from ui_penpal.cpp) */
enum {
    WA_CFG_FOCUS_BASE = 0,
    WA_CFG_FOCUS_KEY,
    WA_CFG_FOCUS_PROVIDER,
    WA_CFG_FOCUS_NUM
};
static int s_cfg_focus = WA_CFG_FOCUS_BASE;
static lv_obj_t *s_cfg_base_ta = NULL;
static lv_obj_t *s_cfg_key_ta = NULL;
static lv_obj_t *s_cfg_provider_dd = NULL;
static char s_cfg_provider_options[256] = "";
static int s_cfg_provider_idx = 0;
static char s_cfg_key_real[PP_KEY_MAX] = {0};
static bool s_cfg_key_masked = false;
static lv_obj_t *s_cfg_status = NULL;

/* async plumbing */
enum { WA_REQ_PROFILE = 0, WA_REQ_TEST };
struct wa_msg_t { int kind; bool ok; char text[160]; pp_profile_t prof; };
struct wa_req_t { int kind; string base; string key; };
/* Queue is created ONCE and NEVER deleted (penpal s_pp_q pattern): the
 * worker task may outlive the screen (user leaves mid-request), and a
 * queue delete between the task's NULL-check and its xQueueSend asserted
 * xQueueGenericSend and rebooted the device (serial evidence 2026-09-14).
 * Late results are consumed by whoami_keyboard_poll, which runs in loop()
 * unconditionally; widget updates are NULL-guarded after destroy. */
static QueueHandle_t s_wa_q = NULL;
static TaskHandle_t s_wa_task = NULL;
static volatile uint32_t s_wa_gen = 0;

/* ---- status helpers ----------------------------------------------------- */
static void wa_me_status(const char *txt)
{
    if (s_me_status) lv_label_set_text(s_me_status, txt);
}

static void wa_cfg_status(const char *txt)
{
    if (s_cfg_status) lv_label_set_text(s_cfg_status, txt);
}

/* montserrat has no CJK glyphs - server profile fields are often Chinese;
 * switch the whole label to the simsun built-in when non-ASCII is present
 * (same rule as pp_msgbox_show, user report 2026-09-14). */
static void wa_label_font_cjk(lv_obj_t *lab, const char *text)
{
    bool cjk = false;
    for (const char *p = text; p && *p; p++) {
        if ((uint8_t)*p & 0x80) { cjk = true; break; }
    }
    lv_obj_set_style_text_font(lab,
        cjk ? &lv_font_simsun_16_cjk : &lv_font_montserrat_14, 0);
}

/* ---- waitbox (user request 2026-09-15: clicking Test/Refresh must show a
 * countdown box BEFORE the network runs - the silent background connect
 * left the user without any feedback until the result msgbox) ----------
 * PenPal pp_waitbox pattern: 10 s countdown, then "still waiting...";
 * hidden by wa_consume when the result lands, and by exit/destroy. */
static lv_obj_t *s_wa_waitbox = NULL;
static lv_obj_t *s_wa_wait_body = NULL;
static uint32_t s_wa_wait_t0 = 0;
static int s_wa_wait_last = -1;
static char s_wa_wait_title[48] = "";

static void wa_waitbox_hide(void)
{
    if (s_wa_waitbox) {
        lv_obj_del(s_wa_waitbox);
        s_wa_waitbox = NULL;
        s_wa_wait_body = NULL;
    }
}

static void wa_waitbox_show(const char *title)
{
    wa_waitbox_hide();
    snprintf(s_wa_wait_title, sizeof(s_wa_wait_title), "%s", title);
    s_wa_waitbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_wa_waitbox, 220, 110);
    lv_obj_align(s_wa_waitbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_wa_waitbox, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_wa_waitbox, 1, 0);
    lv_obj_set_style_border_color(s_wa_waitbox, lv_color_black(), 0);
    lv_obj_set_style_radius(s_wa_waitbox, 6, 0);
    lv_obj_set_style_pad_all(s_wa_waitbox, 8, 0);
    lv_obj_clear_flag(s_wa_waitbox, LV_OBJ_FLAG_SCROLLABLE);

    s_wa_wait_body = lv_label_create(s_wa_waitbox);
    lv_obj_set_width(s_wa_wait_body, lv_pct(100));
    lv_label_set_long_mode(s_wa_wait_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(s_wa_wait_body, "%s\n10s", s_wa_wait_title);
    lv_obj_set_style_text_font(s_wa_wait_body, &lv_font_montserrat_14, 0);
    lv_obj_center(s_wa_wait_body);

    s_wa_wait_t0 = millis();
    s_wa_wait_last = -1;
}

static void wa_waitbox_tick(void)
{
    if (!s_wa_waitbox || !s_wa_wait_body) return;
    int32_t remain = 10000 - (int32_t)(millis() - s_wa_wait_t0);
    if (remain <= 0) {
        if (s_wa_wait_last != 0) {
            s_wa_wait_last = 0;
            lv_label_set_text_fmt(s_wa_wait_body,
                                  "%s\nstill waiting...", s_wa_wait_title);
        }
        return;
    }
    uint32_t secs = ((uint32_t)remain + 999) / 1000;
    if ((int)secs != s_wa_wait_last) {
        s_wa_wait_last = (int)secs;
        lv_label_set_text_fmt(s_wa_wait_body, "%s\n%lus",
                              s_wa_wait_title, (unsigned long)secs);
    }
}

/* ---- Me tab rendering ---------------------------------------------------- */
static void wa_me_render(void)
{
    if (!s_me_label) return;
    if (!s_profile_valid) {
        lv_label_set_text(s_me_label,
                          "Profile not loaded.\n\nPress Refresh (or Enter).\n"
                          "Server + key are set in the Cfg tab.");
        wa_label_font_cjk(s_me_label, "");
        return;
    }
    char buf[320];
    snprintf(buf, sizeof(buf),
             "Name: %s\n\nAge band: %s\nLevel: %s\n\nCity: %s\n\nInterests: %s",
             s_profile.name, s_profile.age_band, s_profile.level,
             s_profile.city[0] ? s_profile.city : "-",
             s_profile.interests[0] ? s_profile.interests : "-");
    lv_label_set_text(s_me_label, buf);
    wa_label_font_cjk(s_me_label, buf);
}

/* Mask-aware read of the key box: an untouched mask means "stored key". */
static const char *wa_cfg_effective_key(char *scratch, int scratch_len)
{
    const char *key = lv_textarea_get_text(s_cfg_key_ta);
    char masked[PP_KEY_MAX];
    secret_mask_middle(s_cfg_key_real, masked, sizeof(masked), 1);
    if (s_cfg_key_masked && key && strcmp(key, masked) == 0) {
        return s_cfg_key_real;             /* untouched mask */
    }
    snprintf(scratch, scratch_len, "%s", key ? key : "");
    return scratch;
}

/* ---- async task ----------------------------------------------------------- */
static void wa_task_func(void *param)
{
    wa_req_t *rq = (wa_req_t *)param;
    wa_msg_t *m = new wa_msg_t;
    m->kind = rq->kind;
    m->ok = false;
    m->text[0] = '\0';
    Serial.printf("[Whoami] task kind=%d base=%s keylen=%d\n",
                  rq->kind, rq->base.c_str(), (int)rq->key.length());
    if (rq->kind == WA_REQ_PROFILE) {
        string err;
        m->ok = penpal_get_profile(rq->base.c_str(), rq->key.c_str(),
                                   &m->prof, &err);
        snprintf(m->text, sizeof(m->text), "%s", err.c_str());
    } else {
        string detail;
        m->ok = penpal_test_base(rq->base.c_str(), rq->key.c_str(), &detail);
        snprintf(m->text, sizeof(m->text), "%s", detail.c_str());
    }
    Serial.printf("[Whoami] task done ok=%d text=%.80s\n",
                  m->ok ? 1 : 0, m->text);
    delete rq;
    if (s_wa_q) {
        xQueueSend(s_wa_q, &m, pdMS_TO_TICKS(2000));
    } else {
        delete m;
    }
    s_wa_task = NULL;
    vTaskDelete(NULL);
}

/* Launch profile/test. Reads the textareas (Cfg tab) or NVS (Me auto-fetch). */
static void wa_start(int kind)
{
    Serial.printf("[Whoami] wa_start kind=%d task=%p\n", kind, s_wa_task);
    if (s_wa_task) {
        if (kind == WA_REQ_PROFILE) wa_me_status("busy - wait for current request");
        else wa_cfg_status("busy - wait for current request");
        return;
    }
    char base_buf[PP_BASE_MAX], key_buf[PP_KEY_MAX];
    const char *base, *key;
    if (s_cfg_base_ta) {                   /* live Cfg values */
        base = lv_textarea_get_text(s_cfg_base_ta);
        key = wa_cfg_effective_key(key_buf, sizeof(key_buf));
        snprintf(base_buf, sizeof(base_buf), "%s", base ? base : "");
        base = base_buf;
    } else {                               /* widgets not built yet */
        pp_cfg_load(base_buf, sizeof(base_buf), key_buf, sizeof(key_buf));
        base = base_buf;
        key = key_buf;
    }
    if (!base[0]) {
        if (kind == WA_REQ_PROFILE) wa_me_status("server URL not set (Cfg tab)");
        else wa_cfg_status("Test: enter server URL first");
        return;
    }
    if (!key[0]) {
        if (kind == WA_REQ_PROFILE) wa_me_status("API key not set (Cfg tab)");
        else wa_cfg_status("Test: API key not set");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        if (kind == WA_REQ_PROFILE) wa_me_status("WiFi not connected");
        else wa_cfg_status("Test: WiFi not connected");
        return;
    }
    if (!s_wa_q) s_wa_q = xQueueCreate(4, sizeof(void *));
    if (!s_wa_q) {
        if (kind == WA_REQ_PROFILE) wa_me_status("Out of memory");
        else wa_cfg_status("Out of memory");
        return;
    }
    wa_req_t *rq = new wa_req_t;
    rq->kind = kind;
    rq->base = base;
    rq->key = key;
    /* 8KB stack, same as PenPal's task: TLS handshake + HTTPClient + cJSON
     * on the task stack overflowed the initial 6KB and the task died
     * silently (device report 2026-09-14: Test stuck at "Testing...") */
    if (xTaskCreate(wa_task_func, "whoami", 1024 * 8, rq, 1,
                    &s_wa_task) != pdPASS) {
        s_wa_task = NULL;
        delete rq;
        if (kind == WA_REQ_PROFILE) wa_me_status("Cannot start task");
        else wa_cfg_status("Cannot start task");
        return;
    }
    /* feedback FIRST, network after: the countdown box is on screen before
     * the task's first byte hits the socket (user request 2026-09-15) */
    wa_waitbox_show(kind == WA_REQ_PROFILE ? "Loading profile..."
                                           : "Testing server...");
    if (kind == WA_REQ_PROFILE) wa_me_status("Loading profile...");
    else wa_cfg_status("Testing server...");
}

static void wa_consume(void)
{
    wa_msg_t *m = NULL;
    while (s_wa_q && xQueueReceive(s_wa_q, &m, 0) == pdTRUE) {
        if (m) {
            Serial.printf("[Whoami] result kind=%d ok=%d\n",
                          m->kind, m->ok ? 1 : 0);
            wa_waitbox_hide();           /* result arrived - drop the box */
            if (m->ok && m->kind == WA_REQ_PROFILE) {
                s_profile = m->prof;
                s_profile_valid = true;
                s_profile_fetched = true;
                wa_cache_save();             /* serve future boots from NVS */
                wa_me_render();
                wa_me_status("profile OK");
            } else if (m->ok) {
                wa_cfg_status(m->text[0] ? m->text : "Test OK");
                if (s_wa_kbd_active) {
                    pp_msgbox_show("Server Test", m->text[0] ? m->text
                                                             : "Test OK");
                }
            } else {
                char buf[192];
                snprintf(buf, sizeof(buf), "failed: %.150s", m->text);
                if (m->kind == WA_REQ_TEST) {
                    wa_cfg_status(buf);
                    /* the old PenPal Cfg Test surfaced its result as a
                     * notice msgbox (user request 2026-09-13) - keep that
                     * behavior in the new home (user report 2026-09-14);
                     * only while the screen is up (no popup after exit) */
                    if (s_wa_kbd_active) {
                        pp_msgbox_show("Server Test failed", buf);
                    }
                } else {
                    wa_me_status(buf);
                }
            }
            delete m;
        }
    }
}

/* ---- Cfg tab (moved from ui_penpal.cpp) ----------------------------------- */

static int wa_cfg_provider_count(void)
{
    return ai_provider_count() + 1;        /* built-ins + custom */
}

static void wa_cfg_status_text(char *buf, int buf_len)
{
    ai_provider_info_t p;
    if (ai_provider_enum(s_cfg_provider_idx, &p)) {
        char base[160] = "", model[80] = "", key[80] = "";
        ai_provider_get(p.name, base, sizeof(base),
                        model, sizeof(model), key, sizeof(key));
        snprintf(buf, buf_len, "AI: %s\n%s\nkey: %s",
                 p.label, model, key[0] ? "set" : "missing");
    } else {
        snprintf(buf, buf_len, "AI: custom\n(server default model)");
    }
}

static void wa_cfg_update_status(void)
{
    char buf[128];
    wa_cfg_status_text(buf, sizeof(buf));
    wa_cfg_status(buf);
}

static void wa_cfg_provider_dd_cb(lv_event_t *e)
{
    (void)e;
    s_cfg_provider_idx = (int)lv_dropdown_get_selected(s_cfg_provider_dd);
    wa_cfg_update_status();
}

static void wa_cfg_prefill(void)
{
    if (!s_cfg_base_ta) return;
    /* focus re-sync on every entry (device report 2026-09-13, same as the
     * former penpal cfg page) */
    s_cfg_focus = WA_CFG_FOCUS_BASE;
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    pp_cfg_load(base, sizeof(base), key, sizeof(key));
    lv_textarea_set_text(s_cfg_base_ta, base);
    strncpy(s_cfg_key_real, key, sizeof(s_cfg_key_real) - 1);
    s_cfg_key_real[sizeof(s_cfg_key_real) - 1] = '\0';
    char key_masked[PP_KEY_MAX];
    secret_mask_middle(s_cfg_key_real, key_masked, sizeof(key_masked), 1);
    lv_textarea_set_text(s_cfg_key_ta, key_masked);
    s_cfg_key_masked = s_cfg_key_real[0] != '\0';

    char provider_name[32] = "";
    penpal_load_ai_provider(provider_name, sizeof(provider_name));
    s_cfg_provider_idx = ai_provider_count();   /* custom default */
    int idx = ai_provider_find(provider_name);
    if (idx >= 0) s_cfg_provider_idx = idx;
    lv_dropdown_set_selected(s_cfg_provider_dd, s_cfg_provider_idx);

    char buf[160];
    wa_cfg_status_text(buf, sizeof(buf));
    if (base[0] && !pp_cfg_from_nvs()) {
        char combined[192];
        snprintf(combined, sizeof(combined), "server from env.cfg | %s", buf);
        wa_cfg_status(combined);
    } else {
        wa_cfg_status(buf);
    }
}

static void wa_cfg_save_cb(lv_event_t *e)
{
    (void)e;
    const char *base = lv_textarea_get_text(s_cfg_base_ta);
    const char *key = lv_textarea_get_text(s_cfg_key_ta);
    if (!base || !key) return;
    const char *save_key = key;
    char key_masked[PP_KEY_MAX];
    secret_mask_middle(s_cfg_key_real, key_masked, sizeof(key_masked), 1);
    if (s_cfg_key_masked && strcmp(key, key_masked) == 0) {
        save_key = s_cfg_key_real;         /* untouched mask: keep stored key */
    }
    bool server_ok = penpal_save_config(base, save_key);
    bool prov_ok = false;
    if (server_ok) {
        strncpy(s_cfg_key_real, save_key, sizeof(s_cfg_key_real) - 1);
        s_cfg_key_real[sizeof(s_cfg_key_real) - 1] = '\0';
        secret_mask_middle(s_cfg_key_real, key_masked, sizeof(key_masked), 1);
        lv_textarea_set_text(s_cfg_key_ta, key_masked);
        s_cfg_key_masked = s_cfg_key_real[0] != '\0';
        ai_provider_info_t p;
        const char *provider_name = "";
        if (ai_provider_enum(s_cfg_provider_idx, &p)) provider_name = p.name;
        prov_ok = penpal_save_ai_provider(provider_name);
    }
    if (server_ok && prov_ok) {
        wa_cfg_status("saved");
        pp_notify_cfg_changed();           /* PenPal re-syncs with new config */
        /* key may point at another user: drop the cached profile (both the
         * NVS cache and the RAM copy) so the next entry refetches */
        wa_cache_clear();
        s_profile_valid = false;
        s_profile_fetched = false;
        wa_me_render();
    } else if (server_ok) {
        wa_cfg_status("server saved; AI provider save failed");
    } else {
        wa_cfg_status("save failed (NVS)");
    }
}

static void wa_cfg_test_cb(lv_event_t *e)
{
    (void)e;
    wa_start(WA_REQ_TEST);
}

static void wa_cfg_base_focus_cb(lv_event_t *e)
{
    (void)e;
    s_cfg_focus = WA_CFG_FOCUS_BASE;
}

static void wa_cfg_key_focus_cb(lv_event_t *e)
{
    (void)e;
    s_cfg_focus = WA_CFG_FOCUS_KEY;
}

static void wa_cfg_build(lv_obj_t *page)
{
    lv_obj_t *bl = lv_label_create(page);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 6, 40);
    lv_label_set_text(bl, "Server URL:");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);

    s_cfg_base_ta = lv_textarea_create(page);
    lv_obj_set_size(s_cfg_base_ta, 228, 34);
    lv_obj_align(s_cfg_base_ta, LV_ALIGN_TOP_MID, 0, 58);
    lv_textarea_set_max_length(s_cfg_base_ta, 95);
    lv_textarea_set_one_line(s_cfg_base_ta, true);
    lv_obj_add_event_cb(s_cfg_base_ta, wa_cfg_base_focus_cb,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_set_style_text_font(s_cfg_base_ta, &lv_font_montserrat_14, 0);

    lv_obj_t *kl = lv_label_create(page);
    lv_obj_align(kl, LV_ALIGN_TOP_LEFT, 6, 100);
    lv_label_set_text(kl, "Server Key:");
    lv_obj_set_style_text_font(kl, &lv_font_montserrat_14, 0);

    s_cfg_key_ta = lv_textarea_create(page);
    lv_obj_set_size(s_cfg_key_ta, 228, 34);
    lv_obj_align(s_cfg_key_ta, LV_ALIGN_TOP_MID, 0, 118);
    lv_textarea_set_max_length(s_cfg_key_ta, 16);
    lv_textarea_set_one_line(s_cfg_key_ta, true);
    lv_obj_add_event_cb(s_cfg_key_ta, wa_cfg_key_focus_cb,
                        LV_EVENT_FOCUSED, NULL);
    lv_obj_set_style_text_font(s_cfg_key_ta, &lv_font_montserrat_14, 0);

    lv_obj_t *pl = lv_label_create(page);
    lv_obj_align(pl, LV_ALIGN_TOP_LEFT, 6, 160);
    lv_label_set_text(pl, "AI Provider:");
    lv_obj_set_style_text_font(pl, &lv_font_montserrat_14, 0);

    s_cfg_provider_dd = lv_dropdown_create(page);
    lv_obj_set_size(s_cfg_provider_dd, 228, 30);
    lv_obj_align(s_cfg_provider_dd, LV_ALIGN_TOP_MID, 0, 178);
    lv_obj_set_style_text_font(s_cfg_provider_dd, &lv_font_montserrat_14,
                               LV_PART_MAIN);
    s_cfg_provider_options[0] = '\0';
    for (int i = 0; i < ai_provider_count(); i++) {
        ai_provider_info_t p;
        ai_provider_enum(i, &p);
        if (i > 0) strncat(s_cfg_provider_options, "\n",
                           sizeof(s_cfg_provider_options) - strlen(s_cfg_provider_options) - 1);
        strncat(s_cfg_provider_options, p.label,
                sizeof(s_cfg_provider_options) - strlen(s_cfg_provider_options) - 1);
    }
    strncat(s_cfg_provider_options, "\ncustom",
            sizeof(s_cfg_provider_options) - strlen(s_cfg_provider_options) - 1);
    lv_dropdown_set_options(s_cfg_provider_dd, s_cfg_provider_options);
    lv_obj_add_event_cb(s_cfg_provider_dd, wa_cfg_provider_dd_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* firmware version right below the provider dropdown (user request
     * 2026-09-16) - x.y.build, see fw_version.h for the bump policy */
    lv_obj_t *fw_lab = lv_label_create(page);
    lv_obj_align(fw_lab, LV_ALIGN_TOP_LEFT, 6, 212);
    lv_label_set_text_fmt(fw_lab, "FW: %s", fw_version_string());
    lv_obj_set_style_text_font(fw_lab, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fw_lab,
                                lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *save_btn = lv_btn_create(page);
    lv_obj_set_size(save_btn, 64, 30);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, 6, 232);
    lv_obj_t *save_lab = lv_label_create(save_btn);
    lv_label_set_text(save_lab, "Save");
    lv_obj_center(save_lab);
    lv_obj_add_event_cb(save_btn, wa_cfg_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *test_btn = lv_btn_create(page);
    lv_obj_set_size(test_btn, 64, 30);
    lv_obj_align(test_btn, LV_ALIGN_TOP_LEFT, 78, 232);
    lv_obj_t *test_lab = lv_label_create(test_btn);
    lv_label_set_text(test_lab, "Test");
    lv_obj_center(test_lab);
    lv_obj_add_event_cb(test_btn, wa_cfg_test_cb, LV_EVENT_CLICKED, NULL);

    s_cfg_status = lv_label_create(page);
    lv_obj_align(s_cfg_status, LV_ALIGN_TOP_LEFT, 6, 268);
    lv_obj_set_width(s_cfg_status, 228);
    lv_obj_set_height(s_cfg_status, 50);
    lv_label_set_long_mode(s_cfg_status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_cfg_status, "");
    lv_obj_set_style_text_font(s_cfg_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_cfg_status,
                                lv_palette_main(LV_PALETTE_GREY), 0);
}

/* ---- tabs --------------------------------------------------------------- */
static void wa_set_tab(bool cfg_tab)
{
    if (!s_me_page || !s_cfg_page) return;
    s_wa_tab_cfg = cfg_tab;
    if (cfg_tab) {
        lv_obj_add_flag(s_me_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_cfg_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_cfg_tab_btn, lv_color_black(), 0);
        lv_obj_set_style_bg_color(s_me_tab_btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_cfg_tab_btn, 0),
                                    lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_me_tab_btn, 0),
                                    lv_color_black(), 0);
    } else {
        lv_obj_add_flag(s_cfg_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_me_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_me_tab_btn, lv_color_black(), 0);
        lv_obj_set_style_bg_color(s_cfg_tab_btn, lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_me_tab_btn, 0),
                                    lv_color_white(), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(s_cfg_tab_btn, 0),
                                    lv_color_black(), 0);
    }
    ui_disp_full_refr();
}

static void wa_me_tab_cb(lv_event_t *e)  { (void)e; wa_set_tab(false); }
static void wa_cfg_tab_cb(lv_event_t *e) { (void)e; wa_set_tab(true); }

static void wa_refresh_cb(lv_event_t *e)
{
    (void)e;
    wa_start(WA_REQ_PROFILE);
}

/* ---- keyboard ----------------------------------------------------------- */
static void wa_cfg_key(char c)
{
    if (c == '\t') {
        s_cfg_focus = (s_cfg_focus + 1) % WA_CFG_FOCUS_NUM;
        return;
    }
    if (s_cfg_focus == WA_CFG_FOCUS_PROVIDER) {
        if (c == '+' || c == '-') {
            int total = wa_cfg_provider_count();
            int sel = (int)lv_dropdown_get_selected(s_cfg_provider_dd);
            if (c == '+') sel = (sel + 1) % total;
            else          sel = (sel + total - 1) % total;
            lv_dropdown_set_selected(s_cfg_provider_dd, sel);
            s_cfg_provider_idx = sel;
            wa_cfg_update_status();
        }
        return;
    }
    lv_obj_t *ta = (s_cfg_focus == WA_CFG_FOCUS_KEY) ? s_cfg_key_ta : s_cfg_base_ta;
    if (!ta) return;
    if (c == '\b') {
        const char *txt = lv_textarea_get_text(ta);
        if (ta == s_cfg_key_ta && s_cfg_key_masked && txt && txt[0] != '\0') {
            lv_textarea_set_text(ta, "");  /* stars shown: retype mode */
            s_cfg_key_masked = false;
        } else if (txt && txt[0] != '\0') {
            lv_textarea_del_char(ta);
        }
        return;
    }
    if (c == '\n') return;                 /* one-line fields */
    if (c >= 0x20) {
        if (ta == s_cfg_key_ta && s_cfg_key_masked) {
            lv_textarea_set_text(ta, "");  /* first edit: retype */
            s_cfg_key_masked = false;
        }
        lv_textarea_add_char(ta, c);
    }
}

void whoami_keyboard_poll(void)
{
    /* drain results FIRST, unconditionally (penpal_keyboard_poll pattern):
     * the queue outlives the screen, late results must not pile up */
    wa_consume();
    wa_waitbox_tick();                   /* countdown while a request runs */

    if (!s_wa_kbd_active) return;
    /* Test-result msgbox: any key closes (penpal_keyboard_poll pattern) */
    if (pp_msgbox_open()) {
        char c;
        if (!keypad_get_val(&c)) return;
        keypad_set_flag();
        pp_msgbox_close();
        return;
    }
    for (int guard = 0; guard < 32; guard++) {
        char c;
        if (!keypad_get_val(&c)) break;
        keypad_set_flag();
        if (c == '\b') {
            /* empty boxes (or Me tab / provider focus): leave the screen */
            if (!s_wa_tab_cfg || s_cfg_focus == WA_CFG_FOCUS_PROVIDER) {
                s_wa_kbd_active = false;
                scr_mgr_pop(false);
                return;
            }
            const char *t = lv_textarea_get_text(
                s_cfg_focus == WA_CFG_FOCUS_KEY ? s_cfg_key_ta : s_cfg_base_ta);
            if (!t || !t[0]) {
                s_wa_kbd_active = false;
                scr_mgr_pop(false);
                return;
            }
            wa_cfg_key(c);
            continue;
        }
        if (!s_wa_tab_cfg) {
            if (c == '\n') wa_start(WA_REQ_PROFILE);
            else if (c == '\t') wa_set_tab(true);
            continue;
        }
        if (c == '\t') { wa_set_tab(!s_wa_tab_cfg); continue; }
        wa_cfg_key(c);
    }
}

/* ---- screen lifecycle ---------------------------------------------------- */
static void wa_back_cb(lv_event_t *e)
{
    (void)e;
    s_wa_kbd_active = false;
    scr_mgr_pop(false);
}

static void wa_create(lv_obj_t *parent)
{
    /* Z-ORDER: the two full-screen tab pages are created FIRST, the back
     * button and the tab buttons AFTER - later siblings sit on top and
     * receive the taps. Created the other way round, the 240x320 page
     * containers swallowed every click on the top row (device report
     * 2026-09-14: Cfg tab unclickable, no way back). PenPal avoids this
     * by parenting its back button INSIDE each page. */

    /* Me page */
    s_me_page = lv_obj_create(parent);
    lv_obj_set_size(s_me_page, 240, 320);
    lv_obj_set_pos(s_me_page, 0, 0);
    lv_obj_set_style_bg_opa(s_me_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_me_page, 0, 0);
    lv_obj_set_style_pad_all(s_me_page, 0, 0);
    lv_obj_clear_flag(s_me_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_me_page, LV_OBJ_FLAG_HIDDEN);

    s_me_label = lv_label_create(s_me_page);
    lv_obj_align(s_me_label, LV_ALIGN_TOP_LEFT, 6, 44);
    lv_obj_set_width(s_me_label, 228);
    lv_label_set_long_mode(s_me_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_me_label, "");
    lv_obj_set_style_text_font(s_me_label, &lv_font_montserrat_14, 0);

    lv_obj_t *refresh_btn = lv_btn_create(s_me_page);
    lv_obj_set_size(refresh_btn, 76, 30);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_LEFT, 6, 278);
    lv_obj_t *rl = lv_label_create(refresh_btn);
    lv_label_set_text(rl, "Refresh");
    lv_obj_center(rl);
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(refresh_btn, wa_refresh_cb, LV_EVENT_CLICKED, NULL);

    s_me_status = lv_label_create(s_me_page);
    lv_obj_align(s_me_status, LV_ALIGN_TOP_LEFT, 90, 282);
    lv_obj_set_width(s_me_status, 144);
    lv_label_set_long_mode(s_me_status, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_me_status, "");
    lv_obj_set_style_text_font(s_me_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_me_status,
                                lv_palette_main(LV_PALETTE_GREY), 0);

    /* Cfg page */
    s_cfg_page = lv_obj_create(parent);
    lv_obj_set_size(s_cfg_page, 240, 320);
    lv_obj_set_pos(s_cfg_page, 0, 0);
    lv_obj_set_style_bg_opa(s_cfg_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_cfg_page, 0, 0);
    lv_obj_set_style_pad_all(s_cfg_page, 0, 0);
    lv_obj_clear_flag(s_cfg_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cfg_page, LV_OBJ_FLAG_HIDDEN);
    wa_cfg_build(s_cfg_page);

    /* top row ON TOP of the pages (see the z-order note above) */
    scr_back_btn_create(parent, "Whoami", wa_back_cb);

    s_me_tab_btn = lv_btn_create(parent);
    lv_obj_set_size(s_me_tab_btn, 64, 24);
    lv_obj_align(s_me_tab_btn, LV_ALIGN_TOP_LEFT, 88, 2);
    lv_obj_t *ml = lv_label_create(s_me_tab_btn);
    lv_label_set_text(ml, "Me");
    lv_obj_center(ml);
    lv_obj_set_style_text_font(ml, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_me_tab_btn, wa_me_tab_cb, LV_EVENT_CLICKED, NULL);

    s_cfg_tab_btn = lv_btn_create(parent);
    lv_obj_set_size(s_cfg_tab_btn, 64, 24);
    lv_obj_align(s_cfg_tab_btn, LV_ALIGN_TOP_LEFT, 158, 2);
    lv_obj_t *cl2 = lv_label_create(s_cfg_tab_btn);
    lv_label_set_text(cl2, "Cfg");
    lv_obj_center(cl2);
    lv_obj_set_style_text_font(cl2, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_cfg_tab_btn, wa_cfg_tab_cb, LV_EVENT_CLICKED, NULL);

    if (!s_wa_q) s_wa_q = xQueueCreate(4, sizeof(void *));

    wa_cfg_prefill();
    wa_me_render();
    wa_set_tab(false);
    s_wa_kbd_active = true;
}

static void wa_entry(void)
{
    ui_disp_full_refr();
    s_wa_gen++;
    /* NVS cache first (once per boot): render instantly, no network */
    if (!s_cache_read) {
        s_cache_read = true;
        if (wa_cache_load()) {
            wa_me_render();
            wa_me_status_cached();
        }
    }
    /* auto-fetch ONLY on a first run with no usable profile (cache empty);
     * afterwards the server is hit exclusively via Refresh (2026-09-16) */
    if (!s_profile_valid && !s_profile_fetched && !s_wa_task) {
        char base[PP_BASE_MAX], key[PP_KEY_MAX];
        pp_cfg_load(base, sizeof(base), key, sizeof(key));
        if (base[0] && key[0]) {
            s_profile_fetched = true;
            wa_start(WA_REQ_PROFILE);
        }
    }
}

static void wa_exit(void)
{
    ui_disp_full_refr();
    ui_disp_suppress_flush(false);
    wa_waitbox_hide();                   /* the box must not outlive the screen */
}

static void wa_destroy(void)
{
    s_wa_kbd_active = false;
    wa_waitbox_hide();
    /* the worker task is NOT killed and the queue NOT deleted: an in-flight
     * request finishes in the background, its result lands in the queue and
     * is consumed (and discarded beyond the NULL widget guards) by the
     * always-running whoami_keyboard_poll */
    s_me_tab_btn = s_cfg_tab_btn = s_me_page = s_cfg_page = NULL;
    s_me_label = s_me_status = NULL;
    s_cfg_base_ta = s_cfg_key_ta = s_cfg_provider_dd = s_cfg_status = NULL;
    (void)s_wa_gen;
}

scr_lifecycle_t screen_whoami = {
    .create = wa_create,
    .entry = wa_entry,
    .exit = wa_exit,
    .destroy = wa_destroy,
};
