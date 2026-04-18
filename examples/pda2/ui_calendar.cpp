/**
 * @file      ui_calendar.cpp
 * @brief     Calendar app with US Federal & Chinese Traditional holidays.
 *            Adapted for T-Deck Pro factory framework.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "lunar_calendar.h"
#include <time.h>

static lv_obj_t *calendar_obj = NULL;
static lv_obj_t *holiday_label = NULL;
static lv_calendar_date_t highlighted_days_buf[31];
static int highlighted_count = 0;

static int current_year = 2025;
static int current_month = 1;
static bool cal_kbd_active = false;

static int cached_year = 0;
static int cached_month = 0;
static int cached_count = 0;
static int cached_days[31];
static const char *cached_names[31];

static void cache_holidays(int year, int month)
{
    if (year == cached_year && month == cached_month) return;
    cached_year = year;
    cached_month = month;
    cached_count = get_month_holidays(year, month, cached_days, cached_names);
}

static const char *cached_holiday_name(int year, int month, int day)
{
    if (year != cached_year || month != cached_month) cache_holidays(year, month);
    for (int i = 0; i < cached_count; i++) {
        if (cached_days[i] == day) return cached_names[i];
    }
    return NULL;
}

static void update_holidays(int year, int month)
{
    cache_holidays(year, month);
    highlighted_count = cached_count;

    for (int i = 0; i < highlighted_count && i < 31; i++) {
        highlighted_days_buf[i].year = year;
        highlighted_days_buf[i].month = month;
        highlighted_days_buf[i].day = cached_days[i];
    }

    if (calendar_obj) {
        if (highlighted_count > 0) {
            lv_calendar_set_highlighted_dates(calendar_obj, highlighted_days_buf, highlighted_count);
        } else {
            lv_calendar_set_highlighted_dates(calendar_obj, NULL, 0);
        }
    }

    if (holiday_label) {
        if (highlighted_count > 0) {
            char buf[256];
            int pos = snprintf(buf, sizeof(buf), "%d/%d ", year, month);
            for (int i = 0; i < highlighted_count && pos < (int)sizeof(buf) - 1; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, " | ");
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%d:%s",
                                cached_days[i], cached_names[i]);
            }
            lv_label_set_text(holiday_label, buf);
        } else {
            lv_label_set_text_fmt(holiday_label, "%d/%d - No holidays", year, month);
        }
    }
}

static void navigate_month(int delta)
{
    current_month += delta;
    if (current_month > 12) { current_month = 1; current_year++; }
    if (current_month < 1)  { current_month = 12; current_year--; }
    lv_calendar_set_showed_date(calendar_obj, current_year, current_month);
    update_holidays(current_year, current_month);
}

static void calendar_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_calendar_date_t date;
        if (lv_calendar_get_pressed_date(obj, &date) == LV_RES_OK) {
            if (date.year != current_year || date.month != current_month) {
                current_year = date.year;
                current_month = date.month;
                update_holidays(current_year, current_month);
            }
            const char *name = cached_holiday_name(date.year, date.month, date.day);
            if (name) {
                lv_label_set_text_fmt(holiday_label, "%d/%d/%d: %s",
                                      date.year, date.month, date.day, name);
            } else {
                lv_label_set_text_fmt(holiday_label, "%d/%d/%d",
                                      date.year, date.month, date.day);
            }
        }
    }
}

// --- Keyboard ---

void calendar_keyboard_poll()
{
    if (!cal_kbd_active) return;
    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\b') {
        cal_kbd_active = false;
        scr_mgr_pop(false);
    } else if (c == 'a' || c == 'z') {
        navigate_month(-1);
    } else if (c == 'd' || c == 'x') {
        navigate_month(1);
    }
}

// --- Screen lifecycle ---

static void cal_back_cb(lv_event_t *e)
{
    cal_kbd_active = false;
    scr_mgr_pop(false);
}

static void cal_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Calendar", cal_back_cb);

    /* Get current date */
    struct tm timeinfo;
    time_t now = time(NULL);
    localtime_r(&now, &timeinfo);
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    if (year < 2020 || year > 2099) { year = 2025; month = 1; day = 1; }

    current_year = year;
    current_month = month;

    /* Calendar widget */
    calendar_obj = lv_calendar_create(parent);
    lv_obj_set_size(calendar_obj, 230, 220);
    lv_obj_align(calendar_obj, LV_ALIGN_TOP_MID, 0, 28);

    lv_calendar_set_today_date(calendar_obj, year, month, day);
    lv_calendar_set_showed_date(calendar_obj, year, month);
    lv_obj_add_event_cb(calendar_obj, calendar_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    lv_calendar_header_arrow_create(calendar_obj);

    /* Holiday label at bottom */
    holiday_label = lv_label_create(parent);
    lv_obj_set_width(holiday_label, 230);
    lv_obj_align(holiday_label, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_text_font(holiday_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(holiday_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(holiday_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(holiday_label, "A/Z:prev  D/X:next  Tap:info");

    update_holidays(year, month);
    cal_kbd_active = true;
}

static void cal_entry(void) { ui_disp_full_refr(); }
static void cal_exit(void) { ui_disp_full_refr(); }
static void cal_destroy(void)
{
    cal_kbd_active = false;
    calendar_obj = NULL;
    holiday_label = NULL;
    cached_year = 0;
    cached_month = 0;
}

scr_lifecycle_t screen_calendar = {
    .create = cal_create,
    .entry = cal_entry,
    .exit = cal_exit,
    .destroy = cal_destroy,
};
