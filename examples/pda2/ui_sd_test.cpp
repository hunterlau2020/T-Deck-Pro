#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "src/assets.h"

static lv_obj_t *sd_test_label = NULL;
static lv_obj_t *sd_test_run_btn = NULL;
static ui_sd_test_result_t sd_test_result = {0};

static const char *sd_test_status(bool ok)
{
    return ok ? "PASS" : "FAIL";
}

static void sd_test_format_size(char *buf, size_t len, bool available, uint64_t value_mb)
{
    if (available) {
        snprintf(buf, len, "%lluMB", value_mb);
    } else {
        snprintf(buf, len, "N/A");
    }
}

static void sd_test_render_result(const ui_sd_test_result_t *result)
{
    if ((result == NULL) || (sd_test_label == NULL)) {
        return;
    }

    char card_size[24];
    char total_size[24];
    char used_size[24];

    sd_test_format_size(card_size, sizeof(card_size), result->mounted, result->card_size_mb);
    sd_test_format_size(total_size, sizeof(total_size), result->mounted, result->total_mb);
    sd_test_format_size(used_size, sizeof(used_size), result->mounted, result->used_mb);

    String text;
    text.reserve(320);
    text += "Overall: ";
    text += sd_test_status(result->pass);
    text += "\nMount: ";
    text += sd_test_status(result->mounted);
    text += "\nCard Type: ";
    text += result->card_type[0] ? result->card_type : "N/A";
    text += "\nCard Size: ";
    text += card_size;
    text += "\nTotal / Used: ";
    text += total_size;
    text += " / ";
    text += used_size;
    text += "\nWrite: ";
    text += sd_test_status(result->write_ok);
    text += "\nReadback: ";
    text += sd_test_status(result->read_ok && result->verify_ok);
    text += "\nCleanup: ";
    text += sd_test_status(result->cleanup_ok);
    text += "\nError: ";
    text += result->error[0] ? result->error : "-";

    lv_label_set_text(sd_test_label, text.c_str());
}

static void sd_test_run_and_render(void)
{
    ui_sd_test_run(&sd_test_result);
    sd_test_render_result(&sd_test_result);
}

static void sd_test_back_cb(lv_event_t *e)
{
    if (e->code == LV_EVENT_CLICKED) {
        scr_mgr_pop(false);
    }
}

static void sd_test_run_btn_cb(lv_event_t *e)
{
    if (e->code == LV_EVENT_CLICKED) {
        sd_test_run_and_render();
    }
}

static void sd_test_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "SD Test", sd_test_back_cb);

    sd_test_label = lv_label_create(parent);
    lv_obj_set_width(sd_test_label, LV_HOR_RES - 18);
    lv_obj_align(sd_test_label, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_text_font(sd_test_label, &Font_Mono_Bold_14, LV_PART_MAIN);
    lv_label_set_long_mode(sd_test_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sd_test_label, "Running...");

    sd_test_run_btn = lv_btn_create(parent);
    lv_obj_set_size(sd_test_run_btn, 124, 40);
    lv_obj_align(sd_test_run_btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_clear_flag(sd_test_run_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(sd_test_run_btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sd_test_run_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sd_test_run_btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sd_test_run_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(sd_test_run_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sd_test_run_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sd_test_run_btn, sd_test_run_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *run_label = lv_label_create(sd_test_run_btn);
    lv_obj_center(run_label);
    lv_obj_set_style_text_font(run_label, &Font_Mono_Bold_14, LV_PART_MAIN);
    lv_label_set_text(run_label, "Run Again");

    sd_test_run_and_render();
}

static void sd_test_entry(void)
{
    ui_disp_full_refr();
}

static void sd_test_exit(void)
{
    ui_disp_full_refr();
}

static void sd_test_destroy(void)
{
    memset(&sd_test_result, 0, sizeof(sd_test_result));
    sd_test_label = NULL;
    sd_test_run_btn = NULL;
}

scr_lifecycle_t screen_sd_test = {
    .create = sd_test_create,
    .entry = sd_test_entry,
    .exit = sd_test_exit,
    .destroy = sd_test_destroy,
};
