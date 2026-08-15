/**
 * @file      ui_ai_cfg.cpp
 * @brief     AI endpoint / model / API key config screen (OpenRouter etc.).
 *            Values stored to NVS namespace "ai" via openai_save_config().
 *
 * Keypad map (field navigation, no reserved letters):
 *   \n : confirm current field (base -> model -> key -> save & exit)
 *   \b : backspace; when field empty -> previous field; on first -> exit
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "ui_scr_mrg.h"

#define AI_CFG_FIELD_NUM 3

static lv_obj_t *ai_base_lab = NULL;
static lv_obj_t *ai_model_lab = NULL;
static lv_obj_t *ai_key_lab = NULL;
static lv_obj_t *ai_ta = NULL;
static lv_obj_t *ai_status_lab = NULL;
static bool ai_cfg_kbd_active = false;
static int  ai_cfg_field = 0;                 /* 0=base 1=model 2=key */
static char ai_base[160] = {0};
static char ai_model[80] = {0};
static char ai_key[80] = {0};

static void ai_cfg_refresh(void)
{
    const char *key_show = (ai_key[0] != '\0') ? "sk-or-v1-***" : "(empty)";
    lv_label_set_text_fmt(ai_base_lab,  "Base : %s%s",  ai_base,
                          ai_cfg_field == 0 ? " <" : "");
    lv_label_set_text_fmt(ai_model_lab, "Model: %s%s", ai_model,
                          ai_cfg_field == 1 ? " <" : "");
    lv_label_set_text_fmt(ai_key_lab,   "Key  : %s%s", key_show,
                          ai_cfg_field == 2 ? " <" : "");

    const char *cur = (ai_cfg_field == 0) ? ai_base :
                      (ai_cfg_field == 1) ? ai_model : ai_key;
    lv_textarea_set_text(ai_ta, cur);
    lv_label_set_text_fmt(ai_status_lab, "Field %d/%d - Enter:save",
                          ai_cfg_field + 1, AI_CFG_FIELD_NUM);
}

static void ai_cfg_save(void)
{
    openai_save_config(ai_base, ai_model, ai_key);
    lv_label_set_text(ai_status_lab, "Saved");
}

void ai_cfg_keyboard_poll(void)
{
    if (!ai_cfg_kbd_active || !ai_ta) return;

    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\t' || c == '\v') return;         /* Alt+Enter scan combo / volume key: not for config */

    if (c == '\n') {
        /* Commit current field, advance to next; on last field save & exit. */
        const char *txt = lv_textarea_get_text(ai_ta);
        if (ai_cfg_field == 0)      strncpy(ai_base,  txt, sizeof(ai_base)  - 1);
        else if (ai_cfg_field == 1) strncpy(ai_model, txt, sizeof(ai_model) - 1);
        else                        strncpy(ai_key,   txt, sizeof(ai_key)   - 1);
        if (ai_cfg_field < AI_CFG_FIELD_NUM - 1) {
            ai_cfg_field++;
            ai_cfg_refresh();
        } else {
            ai_cfg_save();
            ai_cfg_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else if (c == '\b') {
        const char *txt = lv_textarea_get_text(ai_ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(ai_ta);
        } else if (ai_cfg_field > 0) {
            ai_cfg_field--;
            ai_cfg_refresh();
        } else {
            ai_cfg_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else {
        lv_textarea_add_char(ai_ta, c);
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

    ai_base_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_base_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(ai_base_lab, LV_LABEL_LONG_WRAP);

    ai_model_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_model_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(ai_model_lab, LV_LABEL_LONG_WRAP);

    ai_key_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_key_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(ai_key_lab, LV_LABEL_LONG_WRAP);

    ai_ta = lv_textarea_create(cont);
    lv_obj_set_width(ai_ta, lv_pct(100));
    lv_obj_set_height(ai_ta, 30);
    lv_textarea_set_one_line(ai_ta, true);
    lv_textarea_set_max_length(ai_ta, 150);
    lv_obj_set_style_text_font(ai_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    ai_status_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(ai_status_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(ai_status_lab, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    lv_obj_t *hint = lv_label_create(cont);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(hint, "Enter:next  Backspace:del/back");

    openai_load_config(ai_base, sizeof(ai_base), ai_model, sizeof(ai_model),
                       ai_key, sizeof(ai_key));
    ai_cfg_field = 0;
    ai_cfg_refresh();
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
