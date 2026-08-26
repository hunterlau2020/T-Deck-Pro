/**
 * @file      ui_ai_cfg.cpp
 * @brief     AI endpoint / model / API key config screen (OpenRouter etc.).
 *            One input box per field (base URL is multi-line), Save/Test
 *            buttons at the bottom.
 *
 * Review findings incorporated:
 *   - Test sends a minimal chat-completion against the DRAFT base/model/key
 *     (finding 1.7): the draft model is actually exercised, not just listed
 *     by /models; a reply proves the endpoint+key+model triplet works
 *   - Save validates all fields (2026-08-25 user decision: Save and Test
 *     are DECOUPLED - a passing Test is no longer required to Save; do not
 *     reinstate that gate from older design notes)
 *   - the msgbox countdown is the ABSOLUTE deadline: HTTP timeout 45s +
 *     5s worst-case NTP = 50s; on deadline the request generation is
 *     bumped so a late result is dropped (finding 1.9)
 *   - Close = Cancel: bumps the request generation (finding 1.8)
 *   - async results travel over a FreeRTOS queue as heap-allocated structs
 *     carrying the request generation; the task works on its own snapshot
 *     of the draft config (findings 1.4/1.5, contract: async_ipc_contract.md)
 *   - Status hint (finding 1.4, decoupled since 2026-08-25): reflects the
 *     Test state ("Testing..."/"Test OK"/"Save / Test") - it is a hint,
 *     not a Save gate
 *
 * Keypad map:
 *   \n : commit the active field -> next field; on the last field -> save
 *   \b : delete char; empty -> previous field; on first -> exit
 *   '\t' (Alt+Enter): cycle the provider dropdown
 *   '\v' (volume): toggle the "Trust" self-signed TLS switch
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "env_secrets.h"
#include "config_keys.h"
#include "ui_scr_mrg.h"
#include "http_utils.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define AI_CFG_FIELD_NUM 3
#define AI_TEST_HTTP_MS 45000     /* connect + response read */
#define AI_TEST_NTP_MS  5000      /* worst-case NTP wait inside http_* */
#define AI_TEST_DEADLINE_MS (AI_TEST_HTTP_MS + AI_TEST_NTP_MS)

static lv_obj_t *ai_base_lab = NULL;
static lv_obj_t *ai_base_ta = NULL;      /* multi-line: long URLs stay editable */
static lv_obj_t *ai_model_lab = NULL;
static lv_obj_t *ai_model_ta = NULL;
static lv_obj_t *ai_key_lab = NULL;
static lv_obj_t *ai_key_ta = NULL;
static lv_obj_t *ai_status_lab = NULL;
static lv_obj_t *ai_provider_dd = NULL;
static char ai_provider_options[256] = "";
static lv_obj_t *ai_tls_sw = NULL;         /* "Trust" self-signed TLS switch */
static bool ai_cfg_kbd_active = false;
static int  ai_cfg_field = 0;            /* 0=base 1=model 2=key */
static char ai_base[160] = {0};
static char ai_model[80] = {0};
static char ai_key[80] = {0};


/* The task works on its OWN snapshot of the draft (finding 1.6 pattern):
 * UI edits while the request is in flight can never corrupt it. */
typedef struct {
    uint32_t req_gen;                   /* invalidated by Close / a new Test */
    char base[160];
    char model[80];
    char key[80];
} ai_test_req_t;

typedef struct {
    uint32_t req_gen;
    bool ok;                            /* chat-completion succeeded */
    string reply;                       /* assistant reply (test proof) */
} ai_test_result_t;

static QueueHandle_t s_ai_test_q = NULL;
static volatile uint32_t s_ai_test_req_gen = 0;    /* bumped to cancel a pending test */
static volatile bool s_ai_test_busy = false;       /* UI-owned */
static bool s_ai_test_passed = false;              /* required by Save; cleared on edit */

/* Provider presets (user request): choosing one fills the base/model
 * boxes (still editable); "custom" leaves the boxes alone. The stored
 * base never carries /chat/completions - openai_chat appends it. */
static void ai_cfg_sync_draft(void);
static void ai_cfg_status_hint(void);

static int ai_provider_idx = ai_provider_count();   /* custom until matched */

/* Apply the current provider: base/model boxes + THAT provider's resolved
 * key.  Resolution is centralised in openai_api.cpp.  "custom" CLEARS all
 * three boxes (user request). */
static void ai_provider_apply(void)
{
    const int builtin = ai_provider_count();
    if (ai_provider_idx >= builtin) {
        /* custom: start from scratch */
        lv_textarea_set_text(ai_base_ta, "");
        lv_textarea_set_text(ai_model_ta, "");
        lv_textarea_set_text(ai_key_ta, "");
        ai_base[0] = '\0';
        ai_model[0] = '\0';
        ai_key[0] = '\0';
    } else {
        ai_provider_info_t p;
        ai_provider_enum(ai_provider_idx, &p);
        char k[96] = "";
        ai_provider_get(p.name, ai_base, sizeof(ai_base),
                        ai_model, sizeof(ai_model), k, sizeof(k));
        lv_textarea_set_text(ai_base_ta, ai_base);
        lv_textarea_set_text(ai_model_ta, ai_model);
        lv_textarea_set_text(ai_key_ta, k);
        strncpy(ai_key, k, sizeof(ai_key) - 1);
        ai_key[sizeof(ai_key) - 1] = '\0';
        if (k[0] != '\0') Serial.printf("[AICfg] key for %s loaded\n", p.name);
    }
    s_ai_test_passed = false;               /* base/model changed: Test is stale */
    ai_cfg_status_hint();
}

static void ai_provider_select(int idx)
{
    ai_cfg_sync_draft();                    /* keep the outgoing field's edits */
    ai_provider_idx = idx;
    ai_provider_apply();
    ai_provider_info_t p;
    if (ai_provider_enum(ai_provider_idx, &p)) {
        Serial.printf("[AICfg] provider: %s\n", p.name);
    } else {
        Serial.println("[AICfg] provider: custom");
    }
}

/* Native lv_dropdown (touch expands the list) + Alt+Enter cycles. */
static void ai_provider_dd_cb(lv_event_t *e)
{
    ai_provider_select((int)lv_dropdown_get_selected(lv_event_get_target(e)));
}

static void ai_provider_next(void)
{
    const int total = ai_provider_count() + 1;   /* + custom */
    int sel = (ai_provider_idx + 1) % total;
    lv_dropdown_set_selected(ai_provider_dd, sel);  /* fires dd_cb -> select */
}

/* Test feedback msgbox: "Testing... Ns" countdown, replaced by the result,
 * always with a Close button. Close = Cancel while a test is pending. */
static lv_obj_t *ai_msgbox = NULL;
static lv_obj_t *ai_msgbox_body = NULL;
static bool ai_msgbox_countdown_active = false;
static uint32_t ai_msgbox_t0 = 0;
static uint32_t ai_msgbox_last_secs = 99;

static void ai_msgbox_close_cb(lv_event_t *e)
{
    if (ai_msgbox) {
        lv_obj_del(ai_msgbox);
        ai_msgbox = NULL;
        ai_msgbox_body = NULL;
    }
    if (s_ai_test_busy && ai_msgbox_countdown_active) {
        s_ai_test_req_gen++;            /* Close = Cancel: late result is dropped */
        s_ai_test_busy = false;
        Serial.println("[AICfg] test cancelled by Close");
    }
    ai_msgbox_countdown_active = false;
}

static void ai_msgbox_set_text(const char *text)
{
    if (ai_msgbox_body) {
        lv_label_set_text(ai_msgbox_body, text);
    }
}

static void ai_msgbox_show(const char *text, int height = 160)
{
    ai_msgbox_close_cb(NULL);
    ai_msgbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ai_msgbox, 220, height);
    lv_obj_align(ai_msgbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(ai_msgbox, lv_color_white(), 0);
    lv_obj_set_style_border_width(ai_msgbox, 1, 0);
    lv_obj_set_style_border_color(ai_msgbox, lv_color_black(), 0);
    lv_obj_set_style_radius(ai_msgbox, 6, 0);
    lv_obj_set_style_pad_all(ai_msgbox, 8, 0);
    lv_obj_set_flex_flow(ai_msgbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ai_msgbox, 6, 0);
    lv_obj_clear_flag(ai_msgbox, LV_OBJ_FLAG_SCROLLABLE);

    ai_msgbox_body = lv_label_create(ai_msgbox);
    lv_obj_set_width(ai_msgbox_body, lv_pct(100));
    lv_label_set_long_mode(ai_msgbox_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ai_msgbox_body, text);
    lv_obj_set_style_text_font(ai_msgbox_body, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(ai_msgbox_body, 1);

    lv_obj_t *close_btn = lv_btn_create(ai_msgbox);
    lv_obj_set_width(close_btn, lv_pct(100));
    lv_obj_set_height(close_btn, 32);
    lv_obj_t *close_lab = lv_label_create(close_btn);
    lv_label_set_text(close_lab, "Close");
    lv_obj_center(close_lab);
    lv_obj_add_event_cb(close_btn, ai_msgbox_close_cb, LV_EVENT_CLICKED, NULL);
}

static lv_obj_t *ai_cfg_field_ta(int f)
{
    return (f == 0) ? ai_base_ta : (f == 1) ? ai_model_ta : ai_key_ta;
}

/* Sync the ACTIVE field's textarea into its buffer (draft model, mirrors
 * the WiFi config screen: field switches never rewrite other boxes). */
static void ai_cfg_sync_draft(void)
{
    lv_obj_t *ta = ai_cfg_field_ta(ai_cfg_field);
    char *buf = (ai_cfg_field == 0) ? ai_base :
                (ai_cfg_field == 1) ? ai_model : ai_key;
    int cap = (ai_cfg_field == 0) ? (int)sizeof(ai_base) :
              (ai_cfg_field == 1) ? (int)sizeof(ai_model) : (int)sizeof(ai_key);
    strncpy(buf, lv_textarea_get_text(ta), cap - 1);
    buf[cap - 1] = '\0';
}

/* Field markers only - the textareas hold live drafts. */
static void ai_cfg_refresh_labels(void)
{
    lv_label_set_text(ai_base_lab,  ai_cfg_field == 0 ? "Base >"  : "Base");
    lv_label_set_text(ai_model_lab, ai_cfg_field == 1 ? "Model >" : "Model");
    lv_label_set_text(ai_key_lab,   ai_cfg_field == 2 ? "Key >"   : "Key");
}

static void ai_cfg_set_field(int f)
{
    if (f != ai_cfg_field) {
        ai_cfg_sync_draft();                    /* keep the outgoing field's edits */
    }
    ai_cfg_field = f;
    lv_event_send(ai_cfg_field_ta(f), LV_EVENT_FOCUSED, NULL);
    ai_cfg_refresh_labels();
}

/* Save requires valid fields AND a successful Test since the last edit
 * (review finding 1.7): a bad draft must never replace a working config. */
static void ai_cfg_save(void)
{
    ai_cfg_sync_draft();                        /* active field; others synced on leave */

    if (ai_base[0] == '\0') {
        ai_msgbox_show("Base empty - fill it first");
        return;
    }
    if (strncmp(ai_base, "https://", 8) != 0) {
        ai_msgbox_show("Base must start with https://");
        return;
    }
    if (ai_model[0] == '\0') {
        ai_msgbox_show("Model empty - fill it first");
        return;
    }
    if (strlen(ai_key) < 15) {
        ai_msgbox_show("Key too short");
        return;
    }

    const char *err = NULL;
    if (openai_save_config(ai_base, ai_model, ai_key, &err)) {
        /* remember the key under THIS provider too, so switching away
         * and back restores it (per-provider keys). Skip custom (idx past
         * built-ins) - key.custom would be dead storage, never read back. */
        ai_provider_info_t p;
        if (ai_provider_enum(ai_provider_idx, &p)) {
            char nkey[32];
            snprintf(nkey, sizeof(nkey), "key.%s", p.name);
            Preferences pr;
            pr.begin("ai", false);
            pr.putString(nkey, ai_key);
            pr.end();
        }
        lv_label_set_text(ai_status_lab, "Saved");
        Serial.println("[AICfg] saved");
    } else {
        /* state the reason (main finding 1.9): dual-slot save fails either
         * while staging the new slot or at the active-slot commit */
        char buf[96];
        snprintf(buf, sizeof(buf), "Save failed:\n%s", err ? err : "NVS error");
        ai_msgbox_show(buf);
        lv_label_set_text(ai_status_lab, "Save failed");
        Serial.printf("[AICfg] save failed: %s\n", err ? err : "NVS error");
    }
}

/* "Trust self-signed" switch (review 2026-08-07-20, P2 TLS bypass control):
 * persists + applies the device-level TLS mode used by EVERY http_utils
 * request, not just AI ones. Fired by touch AND by the volume key (which
 * flips LV_STATE_CHECKED then sends VALUE_CHANGED manually - programmatic
 * state changes alone don't raise the event). */
static void ai_tls_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (openai_tls_set(on)) {
        /* the status line spells out the SCOPE: this is a device-level
         * transport setting applied to every http_utils consumer
         * (weather, dict, WiFi Test...), not just AI endpoints
         * (review kimi third-batch Low, issue_list 7.4) */
        lv_label_set_text(ai_status_lab,
                          on ? "TLS: ALL HTTPS trust self-signed"
                             : "TLS: ALL HTTPS CA verify");
        Serial.printf("[AICfg] tls insecure=%d (applies to ALL HTTPS)\n", on ? 1 : 0);
    } else {
        /* NVS failure: revert the control to the persisted state */
        if (openai_tls_insecure()) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        ai_msgbox_show("Save failed:\nNVS error");
        Serial.println("[AICfg] tls save failed");
    }
}

/* Touch focus keeps the keypad editing the box the user sees. */
static void ai_ta_focus_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    int f = (ta == ai_base_ta) ? 0 : (ta == ai_model_ta) ? 1 : 2;
    if (ai_cfg_field != f) {
        ai_cfg_set_field(f);
    }
}

/* Status hint: reflects test state now that Save no longer requires a
 * passing Test (user request). */
static void ai_cfg_status_hint(void)
{
    if (s_ai_test_busy) {
        lv_label_set_text(ai_status_lab, "Testing...");
    } else if (s_ai_test_passed) {
        lv_label_set_text(ai_status_lab, "Test OK");
    } else {
        lv_label_set_text(ai_status_lab, "Save / Test");
    }
}

static void ai_test_task_func(void *param)
{
    ai_test_req_t *rq = (ai_test_req_t *)param;     /* task-owned snapshot */
    ai_test_result_t *res = new ai_test_result_t;
    res->req_gen = rq->req_gen;
    /* Minimal chat-completion (finding 1.7): proves the draft endpoint,
     * model AND key actually work - a /models listing cannot. The HTTP
     * timeout covers connect+read; the NTP wait is inside http_* and is
     * bounded by AI_TEST_NTP_MS, so the whole task fits the UI deadline. */
    res->ok = openai_chat("ping", rq->base, rq->model, rq->key,
                          res->reply, AI_TEST_HTTP_MS);
    delete rq;
    if (s_ai_test_q) {
        xQueueSend(s_ai_test_q, &res, portMAX_DELAY);
    } else {
        delete res;
    }
    vTaskDelete(NULL);
}

static void ai_test_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Test button clicked");
    ai_cfg_sync_draft();

    /* explicit field validation with on-screen hints */
    if (!http_require_wifi("AI Test")) {
        ai_msgbox_show("WiFi not connected\nconfigure it first");
        return;
    }
    if (ai_base[0] == '\0') {
        ai_msgbox_show("Base empty - fill it first");
        return;
    }
    if (ai_model[0] == '\0') {
        ai_msgbox_show("Model empty - fill it first");
        return;
    }
    if (ai_key[0] == '\0') {
        ai_msgbox_show("Key empty - fill it first");
        return;
    }
    if (s_ai_test_busy) {
        ai_msgbox_show("Test already running");
        return;
    }
    /* finding 1.5: create and CHECK the queue before going busy */
    if (!s_ai_test_q) s_ai_test_q = xQueueCreate(4, sizeof(void *));
    if (!s_ai_test_q) {
        ai_msgbox_show("Out of memory - retry");
        return;
    }

    /* snapshot the draft so the task is immune to edits while in flight */
    ai_test_req_t *rq = new ai_test_req_t;
    rq->req_gen = s_ai_test_req_gen + 1;
    strncpy(rq->base,  ai_base,  sizeof(rq->base)  - 1); rq->base[sizeof(rq->base)  - 1]  = '\0';
    strncpy(rq->model, ai_model, sizeof(rq->model) - 1); rq->model[sizeof(rq->model) - 1] = '\0';
    strncpy(rq->key,   ai_key,   sizeof(rq->key)   - 1); rq->key[sizeof(rq->key)    - 1]  = '\0';

    s_ai_test_req_gen = rq->req_gen;
    s_ai_test_busy = true;
    /* billing transparency (main finding 1.3/1.4): the minimal completion
     * still consumes tokens, and it only proves network+auth */
    char init_buf[64];
    snprintf(init_buf, sizeof(init_buf),
             "Testing... %lus\ncosts ~1 token\n(network+auth only)",
             (unsigned)(AI_TEST_DEADLINE_MS / 1000));
    ai_msgbox_show(init_buf);
    ai_msgbox_countdown_active = true;
    ai_msgbox_t0 = millis();
    ai_msgbox_last_secs = 99;
    ai_cfg_status_hint();
    TaskHandle_t h = NULL;
    if (xTaskCreate(ai_test_task_func, "ai_test", 1024 * 8,
                    rq, 1, &h) != pdPASS) {
        delete rq;
        s_ai_test_busy = false;
        ai_msgbox_countdown_active = false;
        ai_msgbox_set_text("Cannot start task");
        ai_cfg_status_hint();
    }
}

static void ai_save_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Save button clicked");
    ai_cfg_save();
}

static void ai_usage_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Usage button clicked");
    char buf[192];
    openai_stats_text(buf, sizeof(buf));
    ai_msgbox_show(buf, 205);                   /* taller box for the 6-line breakdown */
}

void ai_cfg_keyboard_poll(void)
{
    /* msgbox countdown: tick only on second changes (EPD-friendly).
     * The countdown IS the absolute deadline: AI_TEST_HTTP_MS for the
     * request + AI_TEST_NTP_MS worst-case NTP wait inside http_*. */
    if (ai_msgbox != NULL && ai_msgbox_countdown_active) {
        uint32_t elapsed = millis() - ai_msgbox_t0;
        uint32_t secs = (AI_TEST_DEADLINE_MS - elapsed + 999) / 1000;
        if (elapsed >= AI_TEST_DEADLINE_MS) {
            ai_msgbox_countdown_active = false;
            s_ai_test_busy = false;
            s_ai_test_req_gen++;                /* finding 1.9: a late result is dropped */
            /* montserrat_14 has no CJK glyphs - keep UI text ASCII (the
             * earlier Chinese wording rendered as tofu blocks) */
            ai_msgbox_set_text("Request timeout\n(check network)");
            ai_cfg_status_hint();
        } else if (secs != ai_msgbox_last_secs) {
            ai_msgbox_last_secs = secs;
            char buf[48];
            snprintf(buf, sizeof(buf), "Testing... %lus\ncosts ~1 token", (unsigned)secs);
            ai_msgbox_set_text(buf);
        }
    }

    /* async test result: apply only when the page is active and the
     * request generation is still current (Close/timeout = cancel). */
    ai_test_result_t *tr = NULL;
    while (s_ai_test_q && xQueueReceive(s_ai_test_q, &tr, 0) == pdTRUE) {
        if (!tr) continue;
        if (tr->req_gen == s_ai_test_req_gen && ai_cfg_kbd_active) {
            ai_msgbox_countdown_active = false;
            s_ai_test_busy = false;
            if (tr->ok) {
                /* the draft model answered: endpoint+key+model all work */
                char buf[128];
                snprintf(buf, sizeof(buf), "Test OK:\n%.60s...\n(billed ~1 token)",
                         tr->reply.c_str());
                ai_msgbox_show(buf);            /* replace content, fresh Close */
                s_ai_test_passed = true;
                Serial.printf("[AICfg] test OK, reply len=%u\n",
                              (unsigned)tr->reply.length());
            } else {
                char fail_buf[240];   /* "Test fail:\n" + up-to-203-char reply */
                snprintf(fail_buf, sizeof(fail_buf), "Test fail:\n%s",
                         tr->reply.c_str());
                ai_msgbox_show(fail_buf, 180);
                Serial.printf("[AICfg] test failed: %s\n", tr->reply.c_str());
            }
            ai_cfg_status_hint();
        } else {
            Serial.println("[AICfg] stale test result dropped");
        }
        delete tr;
    }

    if (!ai_cfg_kbd_active) return;

    /* msgbox open: swallow keypad input until it is closed */
    if (ai_msgbox != NULL) {
        char c;
        if (keypad_get_val(&c)) keypad_set_flag();
        return;
    }

    /* burst processing (user feedback): drain the whole key backlog in
     * ONE poll pass so a typed run coalesces into a single EPD render
     * instead of one flush per character. */
    for (int guard = 0; guard < 32; guard++) {
    char c;
    if (!keypad_get_val(&c)) break;
    keypad_set_flag();

    if (c == '\t') {
        ai_provider_next();                     /* Alt+Enter: cycle the provider */
        continue;
    }
    if (c == '\v') {
        /* volume key: flip the Trust switch. State changes alone don't raise
         * VALUE_CHANGED, so send it manually - the callback persists+applies. */
        if (lv_obj_has_state(ai_tls_sw, LV_STATE_CHECKED)) {
            lv_obj_clear_state(ai_tls_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(ai_tls_sw, LV_STATE_CHECKED);
        }
        lv_event_send(ai_tls_sw, LV_EVENT_VALUE_CHANGED, NULL);
        continue;
    }

    lv_obj_t *ta = ai_cfg_field_ta(ai_cfg_field);

    if (c == '\n') {
        /* commit the active field; last field -> save */
        if (ai_cfg_field < AI_CFG_FIELD_NUM - 1) {
            ai_cfg_set_field(ai_cfg_field + 1);
        } else {
            ai_cfg_save();
            break;                              /* save may open a msgbox */
        }
    } else if (c == '\b') {
        const char *txt = lv_textarea_get_text(ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(ta);
            s_ai_test_passed = false;           /* edited: Test is stale */
            ai_cfg_status_hint();               /* finding 1.4: show the reason */
        } else if (ai_cfg_field > 0) {
            ai_cfg_set_field(ai_cfg_field - 1);
        } else {
            ai_cfg_kbd_active = false;
            scr_mgr_pop(false);
            break;
        }
    } else {
        lv_textarea_add_char(ta, c);
        s_ai_test_passed = false;               /* edited: Test is stale */
        ai_cfg_status_hint();                   /* finding 1.4: show the reason */
    }
    }                                           /* end burst loop */
}

static void ai_cfg_back_cb(lv_event_t *e)
{
    ai_cfg_kbd_active = false;
    scr_mgr_pop(false);
}

static void ai_cfg_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "AI Config", ai_cfg_back_cb);

    /* Trust self-signed switch in the title bar (top-right): toggling takes
     * effect immediately - no Save needed - because it is an independent
     * NVS key, not part of the Test-gated base/model/key draft. */
    lv_obj_t *tls_lab = lv_label_create(parent);
    lv_label_set_text(tls_lab, "Trust");
    lv_obj_set_style_text_font(tls_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(tls_lab, LV_ALIGN_TOP_RIGHT, -50, 9);
    ai_tls_sw = lv_switch_create(parent);
    lv_obj_set_size(ai_tls_sw, 44, 24);
    lv_obj_align(ai_tls_sw, LV_ALIGN_TOP_RIGHT, -2, 4);
    if (openai_tls_insecure()) {
        lv_obj_add_state(ai_tls_sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(ai_tls_sw, ai_tls_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 232, 274);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 32);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(cont, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Provider selector: native dropdown (touch expands the list);
     * Alt+Enter cycles through it from the keypad.  Options are built from
     * the central registry so labels live in one place. */
    ai_provider_dd = lv_dropdown_create(cont);
    lv_obj_set_width(ai_provider_dd, lv_pct(100));
    lv_obj_set_height(ai_provider_dd, 30);
    lv_obj_set_style_text_font(ai_provider_dd, &lv_font_montserrat_14, LV_PART_MAIN);
    ai_provider_options[0] = '\0';
    for (int i = 0; i < ai_provider_count(); i++) {
        ai_provider_info_t p;
        ai_provider_enum(i, &p);
        if (i > 0) strncat(ai_provider_options, "\n",
                           sizeof(ai_provider_options) - strlen(ai_provider_options) - 1);
        strncat(ai_provider_options, p.label,
                sizeof(ai_provider_options) - strlen(ai_provider_options) - 1);
    }
    strncat(ai_provider_options, "\ncustom",
            sizeof(ai_provider_options) - strlen(ai_provider_options) - 1);
    lv_dropdown_set_options(ai_provider_dd, ai_provider_options);

    /* Base URL: label + multi-line box (long URLs stay editable) */
    ai_base_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_base_lab, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_base_ta = lv_textarea_create(cont);
    lv_obj_set_width(ai_base_ta, lv_pct(100));
    lv_obj_set_height(ai_base_ta, 52);
    lv_textarea_set_max_length(ai_base_ta, 159);
    lv_textarea_set_placeholder_text(ai_base_ta, "https://.../api/v1/chat/completions");
    lv_obj_set_style_text_font(ai_base_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* Model: label + single-line box */
    ai_model_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_model_lab, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_model_ta = lv_textarea_create(cont);
    lv_obj_set_width(ai_model_ta, lv_pct(100));
    lv_obj_set_height(ai_model_ta, 30);
    lv_textarea_set_one_line(ai_model_ta, true);
    lv_textarea_set_max_length(ai_model_ta, 79);
    lv_textarea_set_placeholder_text(ai_model_ta, "e.g. deepseek/deepseek-v4-flash-0731");
    lv_obj_set_style_text_font(ai_model_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* Key: label + single-line box */
    ai_key_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_key_lab, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_key_ta = lv_textarea_create(cont);
    lv_obj_set_width(ai_key_ta, lv_pct(100));
    lv_obj_set_height(ai_key_ta, 30);
    lv_textarea_set_one_line(ai_key_ta, true);
    lv_textarea_set_max_length(ai_key_ta, 79);
    lv_textarea_set_placeholder_text(ai_key_ta, "sk-or-v1-...");
    lv_obj_set_style_text_font(ai_key_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_status_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_status_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ai_status_lab, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_long_mode(ai_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ai_status_lab, lv_pct(100));

    /* Save / Test buttons - pinned to the container bottom (FLOATING removes
     * it from the flex flow, so its position is deterministic regardless of
     * the content heights above) */
    lv_obj_t *btn_row = lv_obj_create(cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_height(btn_row, 34);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 4, LV_PART_MAIN);
    lv_obj_add_flag(btn_row, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *save_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(save_btn, 1);
    lv_obj_set_height(save_btn, 34);
    lv_obj_t *save_lab = lv_label_create(save_btn);
    lv_label_set_text(save_lab, "Save");
    lv_obj_center(save_lab);
    lv_obj_add_event_cb(save_btn, ai_save_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *test_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(test_btn, 1);
    lv_obj_set_height(test_btn, 34);
    lv_obj_t *test_lab = lv_label_create(test_btn);
    lv_label_set_text(test_lab, "Test");
    lv_obj_center(test_lab);
    lv_obj_add_event_cb(test_btn, ai_test_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Usage: show the accumulated chat/test statistics (user request) */
    lv_obj_t *usage_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(usage_btn, 1);
    lv_obj_set_height(usage_btn, 34);
    lv_obj_t *usage_lab = lv_label_create(usage_btn);
    lv_label_set_text(usage_lab, "Usage");
    lv_obj_center(usage_lab);
    lv_obj_add_event_cb(usage_btn, ai_usage_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn_row);

    /* touch focus keeps the keypad editing the box the user sees */
    lv_obj_add_event_cb(ai_base_ta, ai_ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ai_model_ta, ai_ta_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ai_key_ta, ai_ta_focus_cb, LV_EVENT_FOCUSED, NULL);

    openai_load_config(ai_base, sizeof(ai_base), ai_model, sizeof(ai_model),
                       ai_key, sizeof(ai_key));
    lv_textarea_set_text(ai_base_ta, ai_base);
    lv_textarea_set_text(ai_model_ta, ai_model);
    lv_textarea_set_text(ai_key_ta, ai_key);
    /* match the saved base to a provider preset (custom when unknown);
     * DISPLAY only - the saved values stay untouched. The dropdown is set
     * BEFORE its change callback is attached, so init never re-applies. */
    ai_provider_idx = ai_provider_count();   /* custom */
    for (int i = 0; i < ai_provider_count(); i++) {
        ai_provider_info_t p;
        ai_provider_enum(i, &p);
        if (strcmp(ai_base, p.base_url) == 0) {
            ai_provider_idx = i;
            break;
        }
    }
    lv_dropdown_set_selected(ai_provider_dd, ai_provider_idx);
    lv_obj_add_event_cb(ai_provider_dd, ai_provider_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ai_cfg_field = 0;
    s_ai_test_passed = false;
    ai_cfg_refresh_labels();
    ai_cfg_status_hint();                       /* "Save / Test" (decoupled) */
    ai_cfg_kbd_active = true;
}

static void ai_cfg_entry(void) { ui_disp_full_refr(); }
static void ai_cfg_exit(void)  { ui_disp_full_refr(); }
static void ai_cfg_destroy(void)
{
    ai_cfg_kbd_active = false;
    ai_msgbox_close_cb(NULL);                   /* no msgbox on other screens */
    openai_stats_flush();                       /* Test pings may have dirtied the
                                                 * stats; checkpoint here too
                                                 * (copilot finding 1.4) */
    if (s_ai_test_busy) {
        s_ai_test_req_gen++;                    /* leaving: late results are dropped */
        s_ai_test_busy = false;
    }
}

scr_lifecycle_t screen_ai_cfg = {
    .create = ai_cfg_create,
    .entry = ai_cfg_entry,
    .exit  = ai_cfg_exit,
    .destroy = ai_cfg_destroy,
};
