/**
 * @file      ui_ai_cfg.cpp
 * @brief     AI endpoint / model / API key config screen (OpenRouter etc.).
 *            One input box per field (base URL is multi-line), Save/Test
 *            buttons at the bottom. Test = GET /models with Bearer key and
 *            shows data[0].id.
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

#define AI_CFG_FIELD_NUM 3

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

static TaskHandle_t ai_test_task = NULL;
static http_response_t ai_test_result = {0, "", false, ""};
static volatile bool ai_test_result_ready = false;
static char ai_test_auth_hdr[128] = {0};

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

static void ai_cfg_save(void)
{
    ai_cfg_sync_draft();                        /* active field; others synced on leave */
    openai_save_config(ai_base, ai_model, ai_key);
    lv_label_set_text(ai_status_lab, "Saved");
    Serial.println("[AICfg] saved");
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

static void ai_test_task_func(void *param)
{
    ai_test_result = http_get_auth("https://openrouter.ai/api/v1/models?limit=2",
                                   ai_test_auth_hdr, 15000);
    ai_test_result_ready = true;
    ai_test_task = NULL;
    vTaskDelete(NULL);
}

static void ai_test_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Test button clicked");
    ai_cfg_sync_draft();

    /* explicit field validation with on-screen hints */
    if (!http_require_wifi("AI Test")) {
        lv_label_set_text(ai_status_lab, "WiFi not connected");
        return;
    }
    if (ai_base[0] == '\0') {
        lv_label_set_text(ai_status_lab, "Base empty - fill it first");
        return;
    }
    if (ai_model[0] == '\0') {
        lv_label_set_text(ai_status_lab, "Model empty - fill it first");
        return;
    }
    if (ai_key[0] == '\0') {
        lv_label_set_text(ai_status_lab, "Key empty - fill it first");
        return;
    }
    if (ai_test_task != NULL) return;           /* already running */

    snprintf(ai_test_auth_hdr, sizeof(ai_test_auth_hdr), "Bearer %s", ai_key);
    lv_label_set_text(ai_status_lab, "Testing...");
    ai_test_result_ready = false;
    if (xTaskCreate(ai_test_task_func, "ai_test", 1024 * 8, NULL, 1,
                    &ai_test_task) != pdPASS) {
        ai_test_task = NULL;
        lv_label_set_text(ai_status_lab, "Cannot start task");
    }
}

static void ai_save_btn_cb(lv_event_t *e)
{
    Serial.println("[AICfg] Save button clicked");
    ai_cfg_save();
}

void ai_cfg_keyboard_poll(void)
{
    /* async test result: apply only while the screen is active */
    if (ai_test_result_ready) {
        ai_test_result_ready = false;
        if (!ai_cfg_kbd_active) {
            Serial.println("[AICfg] test result dropped (inactive)");
            return;
        }
        http_response_t resp = ai_test_result;
        if (resp.success && resp.status_code == 200) {
            cJSON *root = cJSON_Parse(resp.body.c_str());
            cJSON *data = root ? cJSON_GetObjectItem(root, "data") : NULL;
            cJSON *first = (data && cJSON_IsArray(data))
                               ? cJSON_GetArrayItem(data, 0) : NULL;
            cJSON *id = first ? cJSON_GetObjectItem(first, "id") : NULL;
            if (id && cJSON_IsString(id) && id->valuestring) {
                lv_label_set_text_fmt(ai_status_lab, "Test OK: %s", id->valuestring);
                Serial.printf("[AICfg] test models[0].id = %s\n", id->valuestring);
            } else {
                lv_label_set_text(ai_status_lab, "Test fail: bad JSON");
                Serial.printf("[AICfg] test bad json: %s\n", resp.body.c_str());
            }
            if (root) cJSON_Delete(root);
        } else {
            lv_label_set_text_fmt(ai_status_lab, "Test fail: HTTP %d", resp.status_code);
            Serial.printf("[AICfg] test failed code=%d err=%s\n",
                          resp.status_code, resp.error.c_str());
        }
    }

    if (!ai_cfg_kbd_active) return;

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
        } else if (ai_cfg_field > 0) {
            ai_cfg_set_field(ai_cfg_field - 1);
        } else {
            ai_cfg_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else {
        lv_textarea_add_char(ta, c);
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
    lv_textarea_set_placeholder_text(ai_base_ta, "https://.../api/v1");
    lv_obj_set_style_text_font(ai_base_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* Model: label + single-line box */
    ai_model_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_model_lab, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_model_ta = lv_textarea_create(cont);
    lv_obj_set_width(ai_model_ta, lv_pct(100));
    lv_obj_set_height(ai_model_ta, 30);
    lv_textarea_set_one_line(ai_model_ta, true);
    lv_textarea_set_max_length(ai_model_ta, 79);
    lv_textarea_set_placeholder_text(ai_model_ta, "e.g. deepseek/deepseek-chat");
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
    ai_cfg_refresh_labels();
    ai_cfg_kbd_active = true;
}

static void ai_cfg_entry(void) { ui_disp_full_refr(); }
static void ai_cfg_exit(void)  { ui_disp_full_refr(); }
static void ai_cfg_destroy(void)
{
    ai_cfg_kbd_active = false;
}

scr_lifecycle_t screen_ai_cfg = {
    .create = ai_cfg_create,
    .entry = ai_cfg_entry,
    .exit  = ai_cfg_exit,
    .destroy = ai_cfg_destroy,
};
