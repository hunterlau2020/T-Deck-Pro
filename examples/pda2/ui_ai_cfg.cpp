/**
 * @file      ui_ai_cfg.cpp
 * @brief     AI endpoint / model / API key config screen (OpenRouter etc.).
 *            One input box per field (base URL is multi-line), Save/Test
 *            buttons at the bottom.
 *
 * Review findings incorporated:
 *   - Test targets the DRAFT config: the models endpoint is derived from
 *     the user's base URL (…/chat/completions -> …/models?limit=2) with the
 *     draft key, and shows data[0].id (finding 1.6)
 *   - Save validates all fields and requires a successful Test since the
 *     last edit (finding 1.7); NVS write failure never replaces old config
 *   - the 10s msgbox countdown IS the request deadline (HTTP timeout 10s);
 *     Close = Cancel: bumps the request generation so a late result is
 *     dropped (finding 1.8)
 *   - async results travel over a FreeRTOS queue as heap-allocated structs
 *     carrying the request generation; the UI owns the busy state
 *     (findings 1.4/1.5)
 *
 * Keypad map:
 *   \n : commit the active field -> next field; on the last field -> save
 *   \b : delete char; empty -> previous field; on first -> exit
 *   '\t' (Alt+Enter) / '\v' (volume): ignored
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "ui_scr_mrg.h"
#include "http_utils.h"
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define AI_CFG_FIELD_NUM 3
#define AI_TEST_TIMEOUT_MS 10000      /* == the msgbox countdown: the deadline is real */

static lv_obj_t *ai_base_lab = NULL;
static lv_obj_t *ai_base_ta = NULL;      /* multi-line: long URLs stay editable */
static lv_obj_t *ai_model_lab = NULL;
static lv_obj_t *ai_model_ta = NULL;
static lv_obj_t *ai_key_lab = NULL;
static lv_obj_t *ai_key_ta = NULL;
static lv_obj_t *ai_status_lab = NULL;
static bool ai_cfg_kbd_active = false;
static int  ai_cfg_field = 0;            /* 0=base 1=model 2=key */
static char ai_base[160] = {0};
static char ai_model[80] = {0};
static char ai_key[80] = {0};

typedef struct {
    uint32_t req_gen;                   /* invalidated by Close / a new Test */
    http_response_t resp;
} ai_test_result_t;

static QueueHandle_t s_ai_test_q = NULL;
static volatile uint32_t s_ai_cfg_page_gen = 0;    /* bumped on every page entry */
static volatile uint32_t s_ai_test_req_gen = 0;    /* bumped to cancel a pending test */
static volatile bool s_ai_test_busy = false;       /* UI-owned */
static bool s_ai_test_passed = false;              /* required by Save; cleared on edit */

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

static void ai_msgbox_show(const char *text)
{
    ai_msgbox_close_cb(NULL);
    ai_msgbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ai_msgbox, 220, 160);
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
    if (strlen(ai_key) < 16) {
        ai_msgbox_show("Key too short");
        return;
    }
    if (!s_ai_test_passed) {
        ai_msgbox_show("Run Test first");
        return;
    }

    if (openai_save_config(ai_base, ai_model, ai_key)) {
        lv_label_set_text(ai_status_lab, "Saved");
        Serial.println("[AICfg] saved");
    } else {
        lv_label_set_text(ai_status_lab, "Save failed");
        Serial.println("[AICfg] save failed (NVS)");
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

/* Derive the models endpoint from the DRAFT base URL:
 * https://host/api/v1/chat/completions -> https://host/api/v1/models?limit=2 */
static void ai_build_models_url(char *out, int outlen)
{
    strncpy(out, ai_base, outlen - 1);
    out[outlen - 1] = '\0';
    char *p = strstr(out, "chat/completions");
    if (p) {
        strcpy(p, "models");
    } else {
        int len = strlen(out);
        while (len > 0 && out[len - 1] == '/') out[--len] = '\0';
        strncat(out, "/models", outlen - strlen(out) - 1);
    }
    strncat(out, "?limit=2", outlen - strlen(out) - 1);
}

static void ai_test_task_func(void *param)
{
    uint32_t req_gen = (uint32_t)(uintptr_t)param;
    ai_test_result_t *res = new ai_test_result_t;
    res->req_gen = req_gen;
    char url[192];
    ai_build_models_url(url, sizeof(url));
    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", ai_key);
    /* HTTP timeout == the 10s msgbox countdown: the deadline is real */
    res->resp = http_get_auth(url, auth, AI_TEST_TIMEOUT_MS);
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
    if (!s_ai_test_q) s_ai_test_q = xQueueCreate(4, sizeof(void *));

    s_ai_test_req_gen++;
    s_ai_test_busy = true;
    ai_msgbox_show("Testing... 10s");
    ai_msgbox_countdown_active = true;
    ai_msgbox_t0 = millis();
    ai_msgbox_last_secs = 99;
    TaskHandle_t h = NULL;
    if (xTaskCreate(ai_test_task_func, "ai_test", 1024 * 8,
                    (void *)(uintptr_t)s_ai_test_req_gen, 1, &h) != pdPASS) {
        s_ai_test_busy = false;
        ai_msgbox_countdown_active = false;
        ai_msgbox_set_text("Cannot start task");
    }
}

static void ai_save_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Save button clicked");
    ai_cfg_save();
}

void ai_cfg_keyboard_poll(void)
{
    /* msgbox countdown: tick only on second changes (EPD-friendly).
     * The countdown IS the request deadline (HTTP timeout 10s). */
    if (ai_msgbox != NULL && ai_msgbox_countdown_active) {
        uint32_t elapsed = millis() - ai_msgbox_t0;
        uint32_t secs = (AI_TEST_TIMEOUT_MS - elapsed + 999) / 1000;
        if (elapsed >= AI_TEST_TIMEOUT_MS) {
            ai_msgbox_countdown_active = false;
            s_ai_test_busy = false;
            ai_msgbox_set_text("Request timeout\n(check network)");
        } else if (secs != ai_msgbox_last_secs) {
            ai_msgbox_last_secs = secs;
            char buf[48];
            snprintf(buf, sizeof(buf), "Testing... %lus", (unsigned)secs);
            ai_msgbox_set_text(buf);
        }
    }

    /* async test result: apply only when the page is active and the
     * request generation is still current (Close = cancel). */
    ai_test_result_t *tr = NULL;
    while (s_ai_test_q && xQueueReceive(s_ai_test_q, &tr, 0) == pdTRUE) {
        if (!tr) continue;
        if (tr->req_gen == s_ai_test_req_gen && ai_cfg_kbd_active) {
            ai_msgbox_countdown_active = false;
            s_ai_test_busy = false;
            http_response_t resp = tr->resp;
            if (resp.success && resp.status_code == 200) {
                cJSON *root = cJSON_Parse(resp.body.c_str());
                cJSON *data = root ? cJSON_GetObjectItem(root, "data") : NULL;
                cJSON *first = (data && cJSON_IsArray(data))
                                   ? cJSON_GetArrayItem(data, 0) : NULL;
                cJSON *id = first ? cJSON_GetObjectItem(first, "id") : NULL;
                if (id && cJSON_IsString(id) && id->valuestring) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "Test OK:\n%s", id->valuestring);
                    ai_msgbox_show(buf);        /* replace content, fresh Close */
                    lv_label_set_text_fmt(ai_status_lab, "Test OK: %s", id->valuestring);
                    s_ai_test_passed = true;
                    Serial.printf("[AICfg] test models[0].id = %s\n", id->valuestring);
                } else {
                    ai_msgbox_show("Test fail: bad JSON");
                    lv_label_set_text(ai_status_lab, "Test fail: bad JSON");
                    Serial.printf("[AICfg] test bad json: %s\n", resp.body.c_str());
                }
                if (root) cJSON_Delete(root);
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf), "Test fail: HTTP %d\n%s",
                         resp.status_code, resp.error.c_str());
                ai_msgbox_show(buf);
                lv_label_set_text_fmt(ai_status_lab, "Test fail: HTTP %d", resp.status_code);
                Serial.printf("[AICfg] test failed code=%d err=%s\n",
                              resp.status_code, resp.error.c_str());
            }
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

    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\t' || c == '\v') return;         /* Alt+Enter scan combo / volume key */

    lv_obj_t *ta = ai_cfg_field_ta(ai_cfg_field);

    if (c == '\n') {
        /* commit the active field; last field -> save */
        if (ai_cfg_field < AI_CFG_FIELD_NUM - 1) {
            ai_cfg_set_field(ai_cfg_field + 1);
        } else {
            ai_cfg_save();
        }
    } else if (c == '\b') {
        const char *txt = lv_textarea_get_text(ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(ta);
            s_ai_test_passed = false;           /* edited: Test is stale */
        } else if (ai_cfg_field > 0) {
            ai_cfg_set_field(ai_cfg_field - 1);
        } else {
            ai_cfg_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else {
        lv_textarea_add_char(ta, c);
        s_ai_test_passed = false;               /* edited: Test is stale */
    }
}

static void ai_cfg_back_cb(lv_event_t *e)
{
    ai_cfg_kbd_active = false;
    scr_mgr_pop(false);
}

static void ai_cfg_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "AI Config", ai_cfg_back_cb);

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
    ai_cfg_field = 0;
    s_ai_test_passed = false;
    ai_cfg_refresh_labels();
    ai_cfg_kbd_active = true;
}

static void ai_cfg_entry(void) { ui_disp_full_refr(); }
static void ai_cfg_exit(void)  { ui_disp_full_refr(); }
static void ai_cfg_destroy(void)
{
    ai_cfg_kbd_active = false;
    ai_msgbox_close_cb(NULL);                   /* no msgbox on other screens */
    s_ai_cfg_page_gen++;                        /* invalidate in-flight requests */
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
