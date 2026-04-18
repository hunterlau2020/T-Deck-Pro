/**
 * @file      ui_gps_enhanced.cpp
 * @brief     Enhanced GPS app with position, speed, date/time, satellite count.
 *            Two pages: position details + map-like satellite info.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"

static lv_obj_t *page1 = NULL;
static lv_obj_t *page2 = NULL;
static lv_obj_t *page_ind = NULL;
static lv_timer_t *gps_timer = NULL;
static int gps_page = 0;
static bool gps_kbd_active = false;

/* Page 1 widgets */
static lv_obj_t *lbl_lat = NULL;
static lv_obj_t *lbl_lng = NULL;
static lv_obj_t *lbl_sat = NULL;
static lv_obj_t *lbl_speed = NULL;

/* Page 2 widgets */
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_fix = NULL;
static lv_obj_t *lbl_detail = NULL;

static void show_gps_page(int pg)
{
    gps_page = pg;
    if (pg == 0) {
        if (page1) lv_obj_clear_flag(page1, LV_OBJ_FLAG_HIDDEN);
        if (page2) lv_obj_add_flag(page2, LV_OBJ_FLAG_HIDDEN);
        if (page_ind) lv_label_set_text(page_ind, "Position [1/2]");
    } else {
        if (page1) lv_obj_add_flag(page1, LV_OBJ_FLAG_HIDDEN);
        if (page2) lv_obj_clear_flag(page2, LV_OBJ_FLAG_HIDDEN);
        if (page_ind) lv_label_set_text(page_ind, "Details [2/2]");
    }
}

static void update_gps(lv_timer_t *t)
{
    double lat, lng, speed;
    uint32_t vsat;
    uint16_t year;
    uint8_t month, day, hour, minute, second;

    ui_gps_get_coord(&lat, &lng);
    ui_gps_get_satellites(&vsat);
    ui_gps_get_speed(&speed);
    ui_gps_get_data(&year, &month, &day);
    ui_gps_get_time(&hour, &minute, &second);

    bool has_fix = (vsat > 0 && (lat != 0 || lng != 0));

    /* Page 1 */
    if (lbl_lat)
        lv_label_set_text_fmt(lbl_lat, "Lat:  %.6f", lat);
    if (lbl_lng)
        lv_label_set_text_fmt(lbl_lng, "Lon:  %.6f", lng);
    if (lbl_sat)
        lv_label_set_text_fmt(lbl_sat, "Satellites: %lu", vsat);
    if (lbl_speed)
        lv_label_set_text_fmt(lbl_speed, "Speed: %.1f km/h", speed);

    /* Page 2 */
    if (lbl_date)
        lv_label_set_text_fmt(lbl_date, "Date: %04d-%02d-%02d", year, month, day);
    if (lbl_time)
        lv_label_set_text_fmt(lbl_time, "Time: %02d:%02d:%02d UTC", hour, minute, second);
    if (lbl_fix)
        lv_label_set_text(lbl_fix, has_fix ? "Fix: YES" : "Fix: NO (searching...)");
    if (lbl_detail) {
        if (has_fix) {
            lv_label_set_text_fmt(lbl_detail,
                "Position:\n"
                "  %.6f, %.6f\n\n"
                "Speed: %.1f km/h\n"
                "Satellites: %lu\n\n"
                "Place device outdoors\n"
                "for best GPS reception.",
                lat, lng, speed, vsat);
        } else {
            lv_label_set_text(lbl_detail,
                "Searching for satellites...\n\n"
                "Place device outdoors\n"
                "with clear sky view.\n\n"
                "First fix may take\n"
                "1-5 minutes.");
        }
    }
}

/* Keyboard */
void gps_keyboard_poll()
{
    if (!gps_kbd_active) return;
    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\b') {
        if (gps_page > 0) {
            show_gps_page(gps_page - 1);
        } else {
            gps_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else if (c == '\n' || c == ' ') {
        show_gps_page(gps_page == 0 ? 1 : 0);
    }
}

static void gps_back_cb(lv_event_t *e)
{
    gps_kbd_active = false;
    scr_mgr_pop(false);
}

static lv_obj_t *make_gps_page(lv_obj_t *parent)
{
    lv_obj_t *pg = lv_obj_create(parent);
    lv_obj_set_size(pg, 230, 260);
    lv_obj_align(pg, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_style_border_width(pg, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(pg, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pg, 6, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(pg, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
    return pg;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, txt);
    return l;
}

static void gps_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "GPS", gps_back_cb);

    page_ind = lv_label_create(parent);
    lv_obj_set_style_text_font(page_ind, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(page_ind, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* Page 1: Position */
    page1 = make_gps_page(parent);

    lbl_lat = make_label(page1, &lv_font_montserrat_18, "Lat:  --");
    lbl_lng = make_label(page1, &lv_font_montserrat_18, "Lon:  --");

    lv_obj_t *sep = lv_obj_create(page1);
    lv_obj_set_size(sep, lv_pct(100), 1);
    lv_obj_set_style_bg_color(sep, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(sep, 0, LV_PART_MAIN);

    lbl_sat = make_label(page1, &lv_font_montserrat_18, "Satellites: --");
    lbl_speed = make_label(page1, &lv_font_montserrat_18, "Speed: --");

    /* Page 2: Details */
    page2 = make_gps_page(parent);

    lbl_date = make_label(page2, &lv_font_montserrat_18, "Date: --");
    lbl_time = make_label(page2, &lv_font_montserrat_18, "Time: --");
    lbl_fix = make_label(page2, &lv_font_montserrat_14, "Fix: --");
    lbl_detail = make_label(page2, &lv_font_montserrat_14, "Waiting for GPS data...");

    show_gps_page(0);

    ui_gps_task_resume();
    gps_timer = lv_timer_create(update_gps, 3000, NULL);
    gps_kbd_active = true;
}

static void gps_entry(void) { ui_disp_full_refr(); }
static void gps_exit(void)
{
    ui_gps_task_suspend();
    if (gps_timer) { lv_timer_del(gps_timer); gps_timer = NULL; }
    ui_disp_full_refr();
}
static void gps_destroy(void)
{
    gps_kbd_active = false;
    if (gps_timer) { lv_timer_del(gps_timer); gps_timer = NULL; }
    lbl_lat = lbl_lng = lbl_sat = lbl_speed = NULL;
    lbl_date = lbl_time = lbl_fix = lbl_detail = NULL;
    page1 = page2 = page_ind = NULL;
}

scr_lifecycle_t screen_gps_enhanced = {
    .create = gps_create,
    .entry = gps_entry,
    .exit = gps_exit,
    .destroy = gps_destroy,
};
