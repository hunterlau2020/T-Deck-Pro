/**
 * @file      ui_main.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-23
 *
 * Launcher UI for PDA on T-Deck Pro.
 * Redesigned for 240×320 portrait E-Paper display:
 *   - 3-column grid layout with wrap
 *   - Monochrome-friendly (black/white)
 *   - All animations disabled for E-Paper
 *   - Clock uses built-in montserrat fonts
 */
#include "ui_define.h"

LV_IMG_DECLARE(img_wifi);
LV_IMG_DECLARE(img_configuration);
LV_IMG_DECLARE(img_radio);
LV_IMG_DECLARE(img_gps);
LV_IMG_DECLARE(img_power);
LV_IMG_DECLARE(img_monitoring);
LV_IMG_DECLARE(img_calendar);
LV_IMG_DECLARE(img_keyboard);
LV_IMG_DECLARE(img_gyroscope);
LV_IMG_DECLARE(img_msgchat);
LV_IMG_DECLARE(img_bluetooth);
LV_IMG_DECLARE(img_calculator);
LV_IMG_DECLARE(img_dictionary);
LV_IMG_DECLARE(img_weather);

#define DEVICE_CAN_SLEEP                (LV_OBJ_FLAG_USER_1)
#define SCREEN_TIMEOUT 10000

lv_obj_t *main_screen;
lv_obj_t *menu_panel;
lv_group_t *menu_g, *app_g;
static lv_timer_t *clock_timer;
static lv_obj_t *clock_page;
static lv_timer_t *disp_timer = NULL;
static lv_timer_t *dev_timer = NULL;
static uint32_t disp_time_ms = 0;

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *date;
    lv_obj_t *seg;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_label;
} clock_label_t;

static clock_label_t clock_label;

static struct {
    lv_obj_t *cont;
    lv_obj_t *time_label;
    lv_obj_t *wifi_icon;
    lv_obj_t *bt_icon;
    lv_obj_t *battery_label;
    lv_obj_t *battery_bar;
} status_bar_ui;

static lv_obj_t *desc_label;
static void status_bar_update();
static RTC_DATA_ATTR uint8_t brightness_level = 0;
static RTC_DATA_ATTR uint8_t keyboard_level = 0;

void set_low_power_mode_flag(bool enable)
{
    if (enable) {
        lv_obj_add_flag(main_screen, DEVICE_CAN_SLEEP);
    } else {
        lv_obj_clear_flag(main_screen, DEVICE_CAN_SLEEP);
    }
}

bool get_enter_low_power_flag()
{
    return lv_obj_has_flag(main_screen, DEVICE_CAN_SLEEP);
}

void menu_show()
{
    set_default_group(menu_g);
    lv_obj_set_tile_id(main_screen, 0, 0, LV_ANIM_OFF);
    lv_timer_resume(disp_timer);
    lv_disp_trig_activity(NULL);
    hw_feedback();
}

void menu_hidden()
{
    lv_obj_set_tile_id(main_screen, 0, 1, LV_ANIM_OFF);
    lv_timer_pause(disp_timer);
}

bool isinMenu()
{
    return !lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
}

void set_default_group(lv_group_t *group)
{
    lv_indev_t *cur_drv = NULL;
    for (;;) {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv) {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER) {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}


static void btn_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    char *text = (char *)lv_event_get_user_data(e);
    if (c == LV_EVENT_FOCUSED) {
        lv_msg_send(MSG_MENU_NAME_CHANGED, text);
    }
}

static void create_app(lv_obj_t *parent, const char *name, const lv_img_dsc_t *img, app_t *app_fun)
{
    lv_obj_t *btn = lv_btn_create(parent);
    /* 3-column grid: each button ~70px wide, auto height */
    lv_obj_set_size(btn, 70, 70);
    lv_obj_set_style_bg_opa(btn, LV_OPA_0, 0);
    lv_obj_set_style_outline_color(btn, lv_color_black(), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_user_data(btn, (void *)name);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (img != NULL) {
        lv_obj_t *icon = lv_img_create(btn);
        lv_img_set_src(icon, img);
    }

    /* Small label under icon */
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 64);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    /* Focus event — update description label */
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_FOCUSED, (void *)name);

    /* Click to launch app */
    lv_obj_add_event_cb(btn, [](lv_event_t *e) {
        lv_event_code_t c = lv_event_get_code(e);
        app_t *func_cb = (app_t *)lv_event_get_user_data(e);
        lv_obj_t *parent = lv_obj_get_child(main_screen, 1);
        if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
            return;
        }
        if (c == LV_EVENT_CLICKED) {
            set_default_group(app_g);
            hw_feedback();
            if (func_cb->setup_func_cb) {
                (*func_cb->setup_func_cb)(parent);
            }
            menu_hidden();
        }
    },
    LV_EVENT_CLICKED, app_fun);
}


void menu_name_label_event_cb(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_target(e);
    lv_msg_t *m = lv_event_get_msg(e);
    const char *v = (const char *)lv_msg_get_payload(m);
    if (v) {
        lv_label_set_text(label, v);
    }
}


static void clock_update_datetime(lv_timer_t *t)
{
    lv_obj_has_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) ?
    lv_obj_clear_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN) :
    lv_obj_add_flag(clock_label.seg, LV_OBJ_FLAG_HIDDEN);

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);

    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(clock_label.hour, "%02d", timeinfo.tm_hour);
    lv_label_set_text_fmt(clock_label.minute, "%02d", timeinfo.tm_min);
    lv_label_set_text_fmt(clock_label.date, "%02d-%02d %s", timeinfo.tm_mon + 1, timeinfo.tm_mday, week[week_index]);
    monitor_params_t params;
    hw_get_monitor_params(params);
    lv_bar_set_value(clock_label.battery_bar, params.battery_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(clock_label.battery_label, "%d%%", params.battery_percent);
}

lv_obj_t *setupClock()
{
    /* Use built-in montserrat_26 for clock digits on 240×320 display */
    const lv_font_t *clock_font = &lv_font_montserrat_26;

    lv_obj_t *page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_white(), LV_PART_MAIN);

    lv_coord_t w = LV_PCT(40);
    lv_coord_t h = LV_PCT(30);
    int x_offset = 10;
    int y_offset = -40;

    lv_obj_t *hour_cont = lv_obj_create(page);
    lv_obj_set_size(hour_cont, w, h);
    lv_obj_align(hour_cont, LV_ALIGN_LEFT_MID, x_offset, y_offset);
    lv_obj_set_style_border_width(hour_cont, 1, 0);
    lv_obj_set_style_border_color(hour_cont, lv_color_black(), 0);
    lv_obj_clear_flag(hour_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *min_cont = lv_obj_create(page);
    lv_obj_set_size(min_cont, w, h);
    lv_obj_align(min_cont, LV_ALIGN_RIGHT_MID, -x_offset, y_offset);
    lv_obj_set_style_border_width(min_cont, 1, 0);
    lv_obj_set_style_border_color(min_cont, lv_color_black(), 0);
    lv_obj_clear_flag(min_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(page);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_set_style_text_font(label, clock_font, LV_PART_MAIN);
    lv_label_set_text(label, ":");
    clock_label.seg = label;

    label = lv_label_create(hour_cont);
    lv_obj_set_style_text_font(label, clock_font, LV_PART_MAIN);
    lv_label_set_text(label, "12");
    lv_obj_center(label);
    clock_label.hour = label;

    label = lv_label_create(min_cont);
    lv_obj_set_style_text_font(label, clock_font, LV_PART_MAIN);
    lv_label_set_text(label, "34");
    lv_obj_center(label);
    clock_label.minute = label;

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_label_set_text(label, "03-24 Mon");
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -60);
    clock_label.date = label;

    /* Battery bar */
    lv_obj_t *bar = lv_bar_create(page);
    lv_obj_set_size(bar, 40, 14);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, -20, -30);
    clock_label.battery_bar = bar;

    label = lv_label_create(page);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(label, "100%");
    lv_obj_align_to(label, bar, LV_ALIGN_OUT_LEFT_MID, -5, 0);
    clock_label.battery_label = label;

    clock_timer = lv_timer_create(clock_update_datetime, 1000, NULL);
    lv_timer_pause(clock_timer);

    return page;
}


static void hw_device_poll(lv_timer_t *t)
{
    monitor_params_t params;
    hw_get_monitor_params(params);
    if (params.battery_voltage < 3300 && params.usb_voltage == 0) {
        printf("Low battery voltage: %lu mV\n", params.battery_voltage);
        lv_obj_clean(lv_scr_act());
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_radius(lv_scr_act(), 0, 0);

        lv_obj_t *label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "Battery Low!\nShutting down...");
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(label);

        lv_refr_now(NULL);
        lv_timer_handler(); delay(3000);
        hw_shutdown();
    }
}

static void ui_poll_timer_callback(lv_timer_t *t)
{
    if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
        status_bar_update();
    }

    bool timeout = lv_disp_get_inactive_time(NULL) > SCREEN_TIMEOUT;
    if (timeout) {
        if (!lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN) && get_enter_low_power_flag()) {
            lv_obj_add_flag(main_screen, LV_OBJ_FLAG_HIDDEN);

            keyboard_level = hw_get_kb_backlight();
            hw_set_kb_backlight(0);
            lv_obj_clear_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_timer_resume(clock_timer);

            hw_set_cpu_freq(80);

            if (hw_get_disp_timeout_ms() != 0) {
                disp_time_ms = lv_tick_get() + hw_get_disp_timeout_ms();
            } else {
                disp_time_ms = 0;
            }
        }
    } else {
        if (!lv_obj_has_flag(clock_page, LV_OBJ_FLAG_HIDDEN)) {
            hw_set_cpu_freq(240);
            lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
            lv_timer_pause(clock_timer);
            hw_set_kb_backlight(keyboard_level);
        }
    }

    if (lv_obj_has_flag(main_screen, LV_OBJ_FLAG_HIDDEN)) {
        bool disp_on = hw_get_disp_is_on();
        if (disp_on && disp_time_ms != 0) {
            if (lv_tick_get() > disp_time_ms) {
                brightness_level = hw_get_disp_backlight();
                hw_dec_brightness(0);

                hw_low_power_loop();

                /* Wait for button press to wake */
                pinMode(0, INPUT_PULLUP);
                while (digitalRead(0) == HIGH) {
                    delay(10);
                }

                lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_HIDDEN);
                lv_timer_pause(clock_timer);

                hw_set_cpu_freq(240);
                lv_refr_now(NULL);
                lv_disp_trig_activity(NULL);
                hw_inc_brightness(brightness_level);
                hw_set_kb_backlight(keyboard_level);
            }
        }
    }
}

static void status_bar_update()
{
    if (!status_bar_ui.cont) return;

    const char *week[] = {"Sun", "Mon", "Tue", "Wed", "Thur", "Fri", "Sat"};
    struct tm timeinfo;
    hw_get_date_time(timeinfo);
    uint8_t week_index = timeinfo.tm_wday > 6 ? 6 : timeinfo.tm_wday;
    lv_label_set_text_fmt(status_bar_ui.time_label, "%02d:%02d %s",
                          timeinfo.tm_hour, timeinfo.tm_min, week[week_index]);

    monitor_params_t params;
    hw_get_monitor_params(params);
    lv_label_set_text_fmt(status_bar_ui.battery_label, "%d%%", params.battery_percent);
    lv_bar_set_value(status_bar_ui.battery_bar, params.battery_percent, LV_ANIM_OFF);

    if (hw_get_wifi_connected()) {
        lv_obj_clear_flag(status_bar_ui.wifi_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(status_bar_ui.wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }

    if (hw_get_ble_kb_connected()) {
        lv_obj_clear_flag(status_bar_ui.bt_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(status_bar_ui.bt_icon, LV_OBJ_FLAG_HIDDEN);
    }
}


/*********************************************************************************
 *                     Input device enable/disable
 *    (implementations in ui_tools.cpp)
 * *******************************************************************************/

lv_indev_t *lv_get_encoder_indev()
{
    lv_indev_t *indev = NULL;
    for (;;) {
        indev = lv_indev_get_next(indev);
        if (!indev) break;
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
            return indev;
        }
    }
    return NULL;
}

lv_indev_t *lv_get_keyboard_indev()
{
    lv_indev_t *indev = NULL;
    for (;;) {
        indev = lv_indev_get_next(indev);
        if (!indev) break;
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
            return indev;
        }
    }
    return NULL;
}


/*********************************************************************************
 *                              setupGui — Main launcher
 * *******************************************************************************/

void setupGui()
{
    /* White background for E-Paper */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_scr_act(), 0, 0);

    /* Brief splash */
    lv_obj_t *start_logo = lv_label_create(lv_scr_act());
    lv_label_set_text(start_logo, "T-Deck Pro");
    lv_obj_set_style_text_font(start_logo, &lv_font_montserrat_26, LV_PART_MAIN);
    lv_obj_center(start_logo);
    lv_refr_now(NULL);
    lv_timer_handler(); delay(2000);
    lv_obj_del(start_logo);

    disable_keyboard();

    const lv_font_t *main_font = MAIN_FONT;

    lv_theme_default_init(NULL, lv_color_black(), lv_palette_darken(LV_PALETTE_GREY, 3),
                          LV_THEME_DEFAULT_DARK, main_font);

    theme_init();

    /* Create groups */
    menu_g = lv_group_create();
    app_g = lv_group_create();
    set_default_group(menu_g);

    static lv_style_t style_frameless;
    lv_style_init(&style_frameless);
    lv_style_set_radius(&style_frameless, 0);
    lv_style_set_border_width(&style_frameless, 0);
    lv_style_set_bg_color(&style_frameless, lv_color_white());
    lv_style_set_shadow_width(&style_frameless, 0);

    /* Tileview for menu / app switching */
    main_screen = lv_tileview_create(lv_scr_act());
    lv_obj_align(main_screen, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_size(main_screen, LV_PCT(100), LV_PCT(100));

    menu_panel = lv_tileview_add_tile(main_screen, 0, 0, LV_DIR_HOR);
    lv_tileview_add_tile(main_screen, 0, 1, LV_DIR_HOR);

    lv_obj_set_scrollbar_mode(main_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(main_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Status bar ---- */
    lv_obj_t *status_bar = lv_obj_create(menu_panel);
    lv_obj_set_size(status_bar, LV_PCT(100), 22);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 2, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    status_bar_ui.cont = status_bar;

    /* Left: time */
    lv_obj_t *time_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_label_set_text(time_label, "00:00 Sun");
    status_bar_ui.time_label = time_label;

    /* Right side container */
    lv_obj_t *right_cont = lv_obj_create(status_bar);
    lv_obj_set_size(right_cont, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_bg_opa(right_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_cont, 0, 0);
    lv_obj_set_style_pad_all(right_cont, 0, 0);
    lv_obj_set_style_pad_column(right_cont, 4, 0);
    lv_obj_clear_flag(right_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(right_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_cont, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* WiFi icon */
    lv_obj_t *wifi_icon = lv_label_create(right_cont);
    lv_obj_set_style_text_color(wifi_icon, lv_color_white(), 0);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_add_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN);
    status_bar_ui.wifi_icon = wifi_icon;

    /* Bluetooth icon */
    lv_obj_t *bt_icon = lv_label_create(right_cont);
    lv_obj_set_style_text_color(bt_icon, lv_color_white(), 0);
    lv_label_set_text(bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_add_flag(bt_icon, LV_OBJ_FLAG_HIDDEN);
    status_bar_ui.bt_icon = bt_icon;

    /* Battery percentage */
    lv_obj_t *batt_label = lv_label_create(right_cont);
    lv_obj_set_style_text_color(batt_label, lv_color_white(), 0);
    lv_label_set_text(batt_label, "100%");
    status_bar_ui.battery_label = batt_label;

    /* Battery bar */
    lv_obj_t *batt_bar = lv_bar_create(right_cont);
    lv_obj_set_size(batt_bar, 24, 10);
    lv_bar_set_value(batt_bar, 100, LV_ANIM_OFF);
    lv_obj_set_style_radius(batt_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(batt_bar, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(batt_bar, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_style_bg_color(batt_bar, lv_color_white(), LV_PART_INDICATOR);
    status_bar_ui.battery_bar = batt_bar;

    /* ---- App grid ---- */
    lv_obj_t *panel = lv_obj_create(menu_panel);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(85));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(panel, 5, 0);
    lv_obj_set_style_pad_column(panel, 5, 0);
    lv_obj_align_to(panel, status_bar, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
    lv_obj_add_style(panel, &style_frameless, 0);

    /* Declare app entry points */
    extern app_t ui_sys_main;
    extern app_t ui_radio_main;
    extern app_t ui_wireless_main;
    extern app_t ui_gps_main;
    extern app_t ui_monitor_main;
    extern app_t ui_power_main;
    extern app_t ui_calendar_main;
    extern app_t ui_keyboard_main;
    extern app_t ui_sensor_main;
    extern app_t ui_msgchat_main;
    extern app_t ui_ble_main;
    extern app_t ui_ble_kb_main;
    extern app_t ui_calculator_main;
    extern app_t ui_weather_main;
    extern app_t ui_dictionary_main;

    /* Add applications — ordered for 3-column portrait grid */
    create_app(panel, "Setting",    &img_configuration, &ui_sys_main);
    create_app(panel, "WiFi",       &img_wifi,          &ui_wireless_main);
    create_app(panel, "LoRa",       &img_radio,         &ui_radio_main);
    create_app(panel, "Chat",       &img_msgchat,       &ui_msgchat_main);
    create_app(panel, "GPS",        &img_gps,           &ui_gps_main);
    create_app(panel, "Monitor",    &img_monitoring,     &ui_monitor_main);
    create_app(panel, "Power",      &img_power,         &ui_power_main);
    create_app(panel, "Calc",       &img_calculator,    &ui_calculator_main);
    create_app(panel, "Weather",    &img_weather,       &ui_weather_main);
    create_app(panel, "Dict",       &img_dictionary,    &ui_dictionary_main);
    create_app(panel, "Calendar",   &img_calendar,      &ui_calendar_main);
    create_app(panel, "IMU",        &img_gyroscope,     &ui_sensor_main);

#if defined(USING_BLE_KEYBOARD)
    create_app(panel, "BLE KB",     &img_bluetooth,     &ui_ble_kb_main);
    create_app(panel, "Keyboard",   &img_keyboard,      &ui_keyboard_main);
#endif

    /* Description label at bottom */
    desc_label = lv_label_create(menu_panel);
    lv_obj_set_width(desc_label, LV_PCT(100));
    lv_obj_align(desc_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(desc_label, &lv_font_montserrat_18, 0);
    lv_label_set_long_mode(desc_label, LV_LABEL_LONG_DOT);

    lv_obj_add_event_cb(desc_label, menu_name_label_event_cb, LV_EVENT_MSG_RECEIVED, NULL);
    lv_msg_subsribe_obj(MSG_MENU_NAME_CHANGED, desc_label, NULL);
    lv_event_send(lv_obj_get_child(panel, 0), LV_EVENT_FOCUSED, NULL);

    /* Clock page (hidden by default) */
    clock_page = setupClock();
    lv_obj_add_flag(clock_page, LV_OBJ_FLAG_HIDDEN);

    /* Timers */
    disp_timer = lv_timer_create(ui_poll_timer_callback, 1000, NULL);
    dev_timer = lv_timer_create(hw_device_poll, 5000, NULL);

    status_bar_update();
    set_low_power_mode_flag(true);
    lv_disp_trig_activity(NULL);
}
