/**
 * @file      ui_calendar.cpp
 * @brief     Calendar app with US Federal & Chinese Traditional holidays.
 *            Two pages: calendar view + holiday list.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "lunar_calendar.h"
#include <time.h>

static lv_obj_t *calendar_obj = NULL;
static lv_obj_t *holiday_label = NULL;
static lv_obj_t *page_cal = NULL;
static lv_obj_t *page_holidays = NULL;
static lv_obj_t *page_indicator = NULL;
static lv_calendar_date_t highlighted_days_buf[31];
static int highlighted_count = 0;

static int current_year = 2026;
static int current_month = 4;
static int cal_page = 0;
static bool cal_kbd_active = false;

static int cached_year = 0;
static int cached_month = 0;
static int cached_count = 0;
static int cached_days[31];
static const char *cached_names[31];
static TaskHandle_t fetch_task_handle = NULL;
static volatile bool fetch_pending = false;
static lv_timer_t *fetch_check_timer = NULL;

static void cache_holidays(int year, int month)
{
    if (year == cached_year && month == cached_month) return;
    cached_year = year;
    cached_month = month;
    cached_count = get_month_holidays(year, month, cached_days, cached_names);
}

static void show_cal_page(int pg)
{
    cal_page = pg;
    if (pg == 0) {
        if (page_cal) lv_obj_clear_flag(page_cal, LV_OBJ_FLAG_HIDDEN);
        if (page_holidays) lv_obj_add_flag(page_holidays, LV_OBJ_FLAG_HIDDEN);
        if (page_indicator) lv_label_set_text(page_indicator, "Calendar [1/2]  Enter:holidays");
    } else {
        if (page_cal) lv_obj_add_flag(page_cal, LV_OBJ_FLAG_HIDDEN);
        if (page_holidays) lv_obj_clear_flag(page_holidays, LV_OBJ_FLAG_HIDDEN);
        if (page_indicator) lv_label_set_text(page_indicator, "Holidays [2/2]  Bksp:calendar");
    }
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
        if (highlighted_count > 0)
            lv_calendar_set_highlighted_dates(calendar_obj, highlighted_days_buf, highlighted_count);
        else
            lv_calendar_set_highlighted_dates(calendar_obj, NULL, 0);
    }

    if (holiday_label) {
        if (highlighted_count > 0) {
            char buf[1024];
            int pos = 0;
            for (int i = 0; i < highlighted_count && pos < (int)sizeof(buf) - 1; i++) {
                pos += snprintf(buf + pos, sizeof(buf) - pos, " %2d: %s\n",
                                cached_days[i], cached_names[i]);
            }
            lv_label_set_text(holiday_label, buf);
        } else {
            lv_label_set_text_fmt(holiday_label, "No holidays in %d/%d", year, month);
        }
    }
}

static void start_async_fetch(int year, int month);

static void navigate_month(int delta)
{
    current_month += delta;
    if (current_month > 12) { current_month = 1; current_year++; }
    if (current_month < 1)  { current_month = 12; current_year--; }
    lv_calendar_set_showed_date(calendar_obj, current_year, current_month);
    /* Show computed holidays immediately, then fetch API in background */
    update_holidays(current_year, current_month);
    start_async_fetch(current_year, current_month);
}

static int fetch_year_req = 0;
static int fetch_month_req = 0;

static void fetch_task_func(void *param)
{
    holidays_fetch_api(fetch_year_req, fetch_month_req);
    fetch_pending = true;
    fetch_task_handle = NULL;
    vTaskDelete(NULL);
}

static void start_async_fetch(int year, int month)
{
    if (fetch_task_handle) { vTaskDelete(fetch_task_handle); fetch_task_handle = NULL; }
    fetch_year_req = year;
    fetch_month_req = month;
    xTaskCreatePinnedToCore(fetch_task_func, "hol_fetch", 8192, NULL, 5, &fetch_task_handle, 0);
}

static volatile bool month_changed_pending = false;

static void fetch_check_cb(lv_timer_t *t)
{
    if (month_changed_pending) {
        month_changed_pending = false;
        lv_calendar_set_showed_date(calendar_obj, current_year, current_month);
        cached_year = 0; cached_month = 0;
        update_holidays(current_year, current_month);
        start_async_fetch(current_year, current_month);
    }

    if (fetch_pending) {
        fetch_pending = false;
        cached_year = 0; cached_month = 0;
        update_holidays(current_year, current_month);
    } else if (fetch_task_handle && holiday_label) {
        const char *cur_text = lv_label_get_text(holiday_label);
        if (cur_text && !strstr(cur_text, "Fetching"))
            lv_label_set_text_fmt(holiday_label, "%s\n[Fetching...]", cur_text);
    }
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
                month_changed_pending = true;
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
        if (cal_page > 0) {
            show_cal_page(cal_page - 1);
        } else {
            cal_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else if (c == '\n' || c == ' ') {
        show_cal_page(cal_page == 0 ? 1 : 0);
    } else if (c == 'a') {
        navigate_month(-1);
    } else if (c == 'd') {
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

    /* Get current date from system clock (NTP-synced) */
    struct tm timeinfo;
    time_t now = time(NULL);
    localtime_r(&now, &timeinfo);
    int year = timeinfo.tm_year + 1900;
    int month = timeinfo.tm_mon + 1;
    int day = timeinfo.tm_mday;
    if (year < 2020 || year > 2099) {
        /* NTP not synced yet — try to get from GPS */
        uint16_t gy; uint8_t gm, gd;
        ui_gps_get_data(&gy, &gm, &gd);
        if (gy >= 2020 && gy <= 2099) {
            year = gy; month = gm; day = gd;
        }
    }
    Serial.printf("[Calendar] date: %d/%d/%d\n", year, month, day);

    current_year = year;
    current_month = month;

    /* Page indicator at bottom */
    page_indicator = lv_label_create(parent);
    lv_obj_set_style_text_font(page_indicator, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(page_indicator, LV_ALIGN_BOTTOM_MID, 0, -4);

    /* === Page 0: Calendar === */
    page_cal = lv_obj_create(parent);
    lv_obj_set_size(page_cal, 236, 264);
    lv_obj_align(page_cal, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_border_width(page_cal, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_cal, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page_cal, 0, LV_PART_MAIN);
    lv_obj_clear_flag(page_cal, LV_OBJ_FLAG_SCROLLABLE);

    calendar_obj = lv_calendar_create(page_cal);
    lv_obj_set_size(calendar_obj, 234, 260);
    lv_obj_align(calendar_obj, LV_ALIGN_TOP_MID, 0, 0);

    lv_calendar_set_today_date(calendar_obj, year, month, day);
    lv_calendar_set_showed_date(calendar_obj, year, month);
    lv_obj_add_event_cb(calendar_obj, calendar_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

    lv_calendar_header_arrow_create(calendar_obj);

    /* === Page 1: Holiday list === */
    page_holidays = lv_obj_create(parent);
    lv_obj_set_size(page_holidays, 236, 264);
    lv_obj_align(page_holidays, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_border_width(page_holidays, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page_holidays, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page_holidays, 4, LV_PART_MAIN);
    lv_obj_clear_flag(page_holidays, LV_OBJ_FLAG_SCROLLABLE);

    holiday_label = lv_label_create(page_holidays);
    lv_obj_set_width(holiday_label, lv_pct(100));
    lv_obj_set_style_text_font(holiday_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(holiday_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(holiday_label, "");

    update_holidays(year, month);
    start_async_fetch(year, month);
    fetch_check_timer = lv_timer_create(fetch_check_cb, 500, NULL);
    show_cal_page(0);
    cal_kbd_active = true;
}

static void cal_entry(void) { ui_disp_full_refr(); }
static void cal_exit(void) { ui_disp_full_refr(); }
static void cal_destroy(void)
{
    cal_kbd_active = false;
    if (fetch_task_handle) { vTaskDelete(fetch_task_handle); fetch_task_handle = NULL; }
    if (fetch_check_timer) { lv_timer_del(fetch_check_timer); fetch_check_timer = NULL; }
    fetch_pending = false;
    calendar_obj = NULL;
    holiday_label = NULL;
    page_cal = page_holidays = page_indicator = NULL;
    cached_year = 0;
    cached_month = 0;
}

scr_lifecycle_t screen_calendar = {
    .create = cal_create,
    .entry = cal_entry,
    .exit = cal_exit,
    .destroy = cal_destroy,
};
