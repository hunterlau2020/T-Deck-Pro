/**
 * @file      ui_dictionary.cpp
 * @brief     Dictionary app with offline (SD StarDict) and online lookup.
 *            Adapted for T-Deck Pro factory framework. Input at bottom.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "dict_lookup.h"

static lv_obj_t *search_ta = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *status_label = NULL;
static bool dict_kbd_active = false;
static int selected_dict = 0;

static void do_search()
{
    const char *word = lv_textarea_get_text(search_ta);
    if (!word || word[0] == '\0') return;

    Serial.printf("[Dict] Searching: \"%s\"\n", word);
    lv_label_set_text(status_label, "Searching...");

    dict_result_t result;
    bool found = false;

    int sd_count = dict_get_stardict_count();
    if (sd_count > 0) {
        found = dict_lookup_stardict_all(word, result);
    }

    if (!found && dict_offline_en_available()) {
        found = dict_lookup_offline_en(word, result);
    }

    if (!found) {
        const char *suggestions[MAX_SUGGESTIONS];
        int n = dict_prefix_search(word, suggestions, MAX_SUGGESTIONS, -1);
        if (n > 0) {
            static char buf[1024];
            int pos = snprintf(buf, sizeof(buf), "Did you mean:\n");
            for (int i = 0; i < n && pos < (int)sizeof(buf) - 1; i++)
                pos += snprintf(buf + pos, sizeof(buf) - pos, "  %s\n", suggestions[i]);
            lv_label_set_text(result_label, buf);
            lv_label_set_text(status_label, "");
            return;
        }
    }

    if (!found) {
        found = dict_lookup_online(word, result);
    }

    if (found && result.found) {
        static char buf[2048];
        snprintf(buf, sizeof(buf), "%s  %s\n%s",
                 result.phonetic.c_str(),
                 result.part_of_speech.c_str(),
                 result.definition.c_str());
        lv_label_set_text(result_label, buf);
        lv_label_set_text(status_label, "");
    } else {
        lv_label_set_text(result_label, "Word not found.");
        lv_label_set_text(status_label, "Try WiFi for online lookup.");
    }
}

void dict_keyboard_poll()
{
    if (!dict_kbd_active || !search_ta) return;
    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\n') {
        do_search();
    } else if (c == '\b') {
        const char *text = lv_textarea_get_text(search_ta);
        if (!text || text[0] == '\0') {
            dict_kbd_active = false;
            scr_mgr_pop(false);
        } else {
            lv_textarea_del_char(search_ta);
        }
    } else if (c >= 'a' && c <= 'z') {
        lv_textarea_add_char(search_ta, c);
    }
}

static void dict_back_cb(lv_event_t *e)
{
    dict_kbd_active = false;
    scr_mgr_pop(false);
}

static void dict_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Dictionary", dict_back_cb);

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 230, 270);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(cont, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Result area (top, takes most space) */
    result_label = lv_label_create(cont);
    lv_obj_set_width(result_label, lv_pct(100));
    lv_obj_set_flex_grow(result_label, 1);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(result_label, "");

    /* Status */
    status_label = lv_label_create(cont);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);

    int sd_count = dict_scan_stardict();
    if (sd_count > 0) {
        lv_label_set_text_fmt(status_label, "%d dict(s) on SD", sd_count);
    } else if (dict_offline_en_available()) {
        lv_label_set_text(status_label, "Offline dict on SD");
    } else {
        lv_label_set_text(status_label, "Online only (WiFi)");
    }

    /* Input at bottom */
    search_ta = lv_textarea_create(cont);
    lv_obj_set_width(search_ta, lv_pct(100));
    lv_obj_set_height(search_ta, 36);
    lv_textarea_set_placeholder_text(search_ta, "Type word, Enter to search");
    lv_textarea_set_one_line(search_ta, true);
    lv_textarea_set_max_length(search_ta, 64);
    lv_obj_set_style_text_font(search_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    dict_kbd_active = true;
}

static void dict_entry(void) { ui_disp_full_refr(); }
static void dict_exit(void) { ui_disp_full_refr(); }
static void dict_destroy(void)
{
    dict_kbd_active = false;
    search_ta = result_label = status_label = NULL;
}

scr_lifecycle_t screen_dictionary = {
    .create = dict_create,
    .entry = dict_entry,
    .exit = dict_exit,
    .destroy = dict_destroy,
};
