
#include "ui_deckpro.h"
#include "src/assets.h"
#include "stdio.h"
#include "ui_deckpro_port.h"
#include "Arduino.h"
#include "openai_api.h"     /* openai_stats_flush() at the deep-sleep checkpoint */

#define SETTING_PAGE_MAX_ITEM 7
#define GET_BUFF_LEN(a) sizeof(a)/sizeof(a[0])

#define FONT_BOLD_SIZE_14 &Font_Mono_Bold_14
#define FONT_BOLD_SIZE_15 &Font_Mono_Bold_15
#define FONT_BOLD_SIZE_16 &Font_Mono_Bold_16
#define FONT_BOLD_SIZE_17 &Font_Mono_Bold_17
#define FONT_BOLD_SIZE_18 &Font_Mono_Bold_18
#define FONT_BOLD_SIZE_19 &Font_Mono_Bold_19

#define FONT_BOLD_MONO_SIZE_14 &Font_Mono_Bold_14
#define FONT_BOLD_MONO_SIZE_15 &Font_Mono_Bold_15
#define FONT_BOLD_MONO_SIZE_16 &Font_Mono_Bold_16
#define FONT_BOLD_MONO_SIZE_17 &Font_Mono_Bold_17
#define FONT_BOLD_MONO_SIZE_18 &Font_Mono_Bold_18
#define FONT_BOLD_MONO_SIZE_19 &Font_Mono_Bold_19

#define GLOBAL_BUF_LEN 30
#define LOW_VOLTAGE_THRESHOLD_MV 3300
#define LOW_VOLTAGE_SOC_THRESHOLD 5
#define LOW_VOLTAGE_SHUTDOWN_DELAY_MS 20000
#define LOW_VOLTAGE_POLL_MS 250
static char global_buf[GLOBAL_BUF_LEN];

static lv_timer_t *touch_chk_timer = NULL;
static lv_timer_t *taskbar_update_timer = NULL;
static lv_timer_t *low_voltage_timer = NULL;
static lv_obj_t *label_list[10] = {0};
uint16_t taskbar_statue[TASKBAR_ID_MAX] = {0};
static lv_obj_t *low_voltage_popup = NULL;
static lv_obj_t *low_voltage_countdown_label = NULL;
static bool low_voltage_latched = false;
static bool low_voltage_shutdown_requested = false;
static uint32_t low_voltage_shutdown_deadline_ms = 0;
static int low_voltage_last_countdown_sec = -1;

static void low_voltage_popup_set_visible(bool visible)
{
    if (!low_voltage_popup) {
        return;
    }

    if (visible) {
        lv_obj_clear_flag(low_voltage_popup, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(low_voltage_popup);
    } else {
        lv_obj_add_flag(low_voltage_popup, LV_OBJ_FLAG_HIDDEN);
    }
}

static void low_voltage_popup_update(int countdown_sec)
{
    if (!low_voltage_countdown_label) {
        return;
    }

    lv_label_set_text_fmt(low_voltage_countdown_label,
                          "Battery voltage is too low.\nPlease charge now.\nAuto shutdown in %ds.",
                          countdown_sec);
}

static void low_voltage_popup_create(void)
{
    if (low_voltage_popup) {
        return;
    }

    low_voltage_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_width(low_voltage_popup, 220);
    lv_obj_set_height(low_voltage_popup, LV_SIZE_CONTENT);
    lv_obj_center(low_voltage_popup);
    lv_obj_clear_flag(low_voltage_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(low_voltage_popup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(low_voltage_popup, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(low_voltage_popup, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(low_voltage_popup, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(low_voltage_popup, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(low_voltage_popup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(low_voltage_popup, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(low_voltage_popup, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(low_voltage_popup, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(low_voltage_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(low_voltage_popup, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(low_voltage_popup);
    lv_obj_set_style_text_font(title, FONT_BOLD_SIZE_17, LV_PART_MAIN);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(title, "LOW VOLTAGE");

    low_voltage_countdown_label = lv_label_create(low_voltage_popup);
    lv_obj_set_width(low_voltage_countdown_label, lv_pct(100));
    lv_obj_set_style_text_font(low_voltage_countdown_label, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    lv_obj_set_style_text_align(low_voltage_countdown_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(low_voltage_countdown_label, LV_LABEL_LONG_WRAP);
    low_voltage_popup_update(LOW_VOLTAGE_SHUTDOWN_DELAY_MS / 1000);

    low_voltage_popup_set_visible(false);
}

static bool low_voltage_should_latch(void)
{
    bool voltage_low = false;
    bool gauge_low = false;
    bool soc_low = false;

    if (ui_test_get(E_PERI_BQ27220)) {
        uint16_t vbat_mv = (uint16_t)ui_battery_27220_get_voltage();
        voltage_low = (vbat_mv <= LOW_VOLTAGE_THRESHOLD_MV);
    }

    if (ui_battery_27220_is_vaild()) {
        gauge_low = ui_battery_27220_is_low_alarm();
        // soc_low = (ui_battery_27220_get_percent() <= LOW_VOLTAGE_SOC_THRESHOLD);
    }

    return voltage_low || gauge_low || soc_low;
}

static void low_voltage_reset_state(void)
{
    low_voltage_latched = false;
    low_voltage_shutdown_requested = false;
    low_voltage_shutdown_deadline_ms = 0;
    low_voltage_last_countdown_sec = -1;
    low_voltage_popup_set_visible(false);
}

static void low_voltage_timer_cb(lv_timer_t *t)
{
    (void)t;

    if (!low_voltage_popup) {
        low_voltage_popup_create();
    }

    if (ui_battery_is_external_power_present()) {
        low_voltage_reset_state();
        return;
    }

    // Latch until external power is inserted so the popup does not flicker near 3.3V.
    if (!low_voltage_latched && low_voltage_should_latch()) {
        low_voltage_latched = true;
        low_voltage_shutdown_requested = false;
        low_voltage_shutdown_deadline_ms = lv_tick_get() + LOW_VOLTAGE_SHUTDOWN_DELAY_MS;
        low_voltage_last_countdown_sec = -1;
    }

    if (!low_voltage_latched) {
        return;
    }

    low_voltage_popup_set_visible(true);

    int32_t remaining_ms = (int32_t)(low_voltage_shutdown_deadline_ms - lv_tick_get());
    int countdown_sec = remaining_ms > 0 ? (int)((remaining_ms + 999) / 1000) : 0;
    if (countdown_sec != low_voltage_last_countdown_sec) {
        low_voltage_popup_update(countdown_sec);
        low_voltage_last_countdown_sec = countdown_sec;
    }

    if (remaining_ms <= 0 && !low_voltage_shutdown_requested) {
        low_voltage_shutdown_requested = true;
        ui_shutdown_on();
    }
}

//************************************[ Other fun ]******************************************
#if 1
lv_obj_t *scr_back_btn_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_height(btn, 30);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 3, 3);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label2 = lv_label_create(btn);
    lv_obj_align(label2, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_text_color(label2, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_label_set_text(label2, LV_SYMBOL_LEFT);

    lv_obj_t *label = lv_label_create(parent);
    lv_obj_align_to(label, label2, LV_ALIGN_OUT_RIGHT_MID, 5, -1);
    lv_obj_set_style_text_font(label, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_label_set_text(label, text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(label, 20);

    return label;
}

static const char *line_full_format(int max_c, const char *str1, const char *str2)
{
    int len1 = 0, len2 = 0;
    int j;

    len1 = strlen(str1);

    strncpy(global_buf, str1, len1);

    len2 = strlen(str2);
    for(j = len1; j < max_c -1 - len2; j++){
        global_buf[j] = ' ';
    }
    strncpy(global_buf + j, str2, len2);
    j = j + len2;
    
    global_buf[j] = '\0'; 

    printf("[%d] buf: %s\n", __LINE__, global_buf);

    return (const char *)global_buf;
}

#endif
//************************************[ screen 0 ]****************************************** menu
#if 1
#define MENU_BTN_NUM (sizeof(menu_btn_list) / sizeof(menu_btn_list[0]))

static ui_indev_read_cb ui_get_gesture_dir = NULL;

static lv_obj_t *menu_screen1;
static lv_obj_t *menu_screen2;
static lv_obj_t *menu_screen3;
static lv_obj_t *ui_Panel4;

static lv_obj_t * menu_taskbar = NULL;
static lv_obj_t * menu_taskbar_time = NULL;
static lv_obj_t * menu_taskbar_charge = NULL;
static lv_obj_t * menu_taskbar_battery = NULL;
static lv_obj_t * menu_taskbar_battery_percent = NULL;
static lv_obj_t * menu_taskbar_wifi = NULL;

static int page_num = 0;
static int page_curr = 0;

static struct menu_btn menu_btn_list[] =
{
    /* Page one (user-requested order, 2026-08-17): AI-first layout */
    {SCREEN_AI_CFG_ID,     &img_setting,    "AI Cfg",  23,   13},
    {SCREEN_AI_CHAT_ID,    &img_voice_ai,   "AI Text", 95,   13},
    {SCREEN_VOICE_AI_ID,   &img_voice_ai,   "AI Chat", 167,  13},
    {SCREEN_DICTIONARY_ID, &img_dictionary, "Dict",    23,   101},
    {SCREEN_WEATHER_ID,    &img_weather,    "Weather", 95,   101},
    {SCREEN_CALENDAR_ID,   &img_calendar,   "Calendar",167,  101},
    {SCREEN_CALCULATOR_ID, &img_calculator, "Calc",    23,   189},
    {SCREEN4_ID,           &img_wifi,       "Wifi",    95,   189},
    {SCREEN11_ID,          &img_PCM5102,    "Sleep",   167,  189},
    /* Page two: hardware / system entries */
    {SCREEN1_ID,           &img_lora,       "Lora",    23,   13},
    {SCREEN2_ID,           &img_setting,    "Setting", 95,   13},
    {SCREEN_GPS_ENHANCED_ID,&img_GPS,       "GPS",     167,  13},
    {SCREEN5_ID,           &img_test,       "Test",    23,   101},
    {SCREEN6_ID,           &img_batt,       "Battery", 95,   101},
    {SCREEN7_ID,           &img_touch,      "Input",   167,  101},
    {SCREEN8_ID,           &img_A7682E,     "A7682E",  23,   189},
    {SCREEN9_ID,           &img_lora,       "Shutdown",95,   189},
    {SCREEN12_ID,          &img_motor,      "Motor",   167,  189},
    /* Page three: pen-pal letters (alone, 9/9/1) */
    {SCREEN_PENPAL_ID,     &img_penpal,     "PenPal",  23,   13},
};

static void menu_btn_event_cb(lv_event_t *e)
{
    struct menu_btn *tgr = (struct menu_btn *)e->user_data;
    scr_mgr_push(tgr->idx, false);
}

/* Show menu page `page_curr`, hide the others, repaint the dots. Shared by
 * the gesture handler and create0()'s initial state (3 pages since PenPal,
 * page 3 = menu_screen3). */
static void menu_page_apply(void)
{
    lv_obj_t *pages[3] = {menu_screen1, menu_screen2, menu_screen3};

    for(int i = 0; i <= page_num && i < 3; i++) {
        if(i == page_curr) {
            lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if(ui_Panel4 == NULL) return;      /* no dots when the menu is single-page */
    for(int i = 0; i <= page_num; i++) {
        lv_color_t c = (i == page_curr) ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, i), c, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void menu_get_gesture_dir(int dir)
{
    if(MENU_BTN_NUM <= 9) return;

    if(dir == LV_DIR_LEFT) {
        if(page_curr < page_num){
            page_curr++;
            // ui_disp_full_refr();
        }
        else{
            return ;
        }
    } else if(dir == LV_DIR_RIGHT) {
        if(page_curr > 0){
            page_curr--;
        }
        else{
            return ;
        }
    }

    menu_page_apply();
}

static void menu_btn_create(lv_obj_t *parent, struct menu_btn *info)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_width(btn, 50);
    lv_obj_set_height(btn, 50);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_OVERFLOW_VISIBLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 3, LV_PART_MAIN | LV_STATE_PRESSED);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, FONT_BOLD_MONO_SIZE_14, LV_PART_MAIN);
    lv_obj_set_width(label, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(label, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(label, 0);
    lv_obj_set_y(label, 20);
    lv_obj_set_align(label, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_x(btn, info->pos_x);
    lv_obj_set_y(btn, info->pos_y);

    const lv_img_dsc_t *dsc = (const lv_img_dsc_t *)info->icon;
    if (dsc->header.w <= 50) {
        lv_obj_set_style_bg_img_src(btn, info->icon, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_t *icon = lv_img_create(btn);
        lv_img_set_src(icon, info->icon);
        uint16_t zoom = (uint16_t)(256 * 40 / dsc->header.w);
        lv_img_set_zoom(icon, zoom);
        lv_obj_set_size(icon, 40, 40);
        lv_obj_align(icon, LV_ALIGN_CENTER, 0, -8);
    }

    lv_label_set_text(label, (info->name));
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn, menu_btn_event_cb, LV_EVENT_CLICKED, (void *)info);
}

/* Menu page container shared by menu_screen1/2/3 (identical styling;
 * created hidden - create0() shows page 0 via menu_page_apply()). */
static lv_obj_t *menu_page_create(lv_obj_t *parent, lv_coord_t height)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), height);
    lv_obj_set_style_bg_color(page, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(page, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(page, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_border_side(page, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_align(page, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

static void create0(lv_obj_t *parent)
{
    int status_bar_height = 25;

    menu_taskbar = lv_obj_create(parent);
    lv_obj_set_size(menu_taskbar, LV_HOR_RES, status_bar_height);
    lv_obj_set_style_pad_all(menu_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_taskbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(menu_taskbar, LV_OBJ_FLAG_SCROLLABLE);
    
    menu_taskbar_time = lv_label_create(menu_taskbar);
    lv_obj_set_style_border_width(menu_taskbar_time, 0, 0);
    lv_label_set_text(menu_taskbar_time, "--:--");  /* real time filled by the taskbar timer */
    lv_obj_set_style_text_font(menu_taskbar_time, &Font_Mono_Bold_14, LV_PART_MAIN);
    lv_obj_align(menu_taskbar_time, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *status_parent = lv_obj_create(menu_taskbar);
    lv_obj_set_size(status_parent, lv_pct(80)-2, status_bar_height-2);
    lv_obj_set_style_pad_all(status_parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_parent, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(status_parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_parent, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(status_parent, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(status_parent, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(status_parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(status_parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_parent, LV_ALIGN_RIGHT_MID, 0, 0);

    menu_taskbar_wifi = lv_label_create(status_parent);
    lv_label_set_text_fmt(menu_taskbar_wifi, "%s", LV_SYMBOL_WIFI);
    lv_obj_add_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);

    menu_taskbar_charge = lv_label_create(status_parent);
    lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_CHARGE);
    lv_obj_add_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);

    if(taskbar_statue[TASKBAR_ID_WIFI])
        lv_obj_clear_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);

    if(taskbar_statue[TASKBAR_ID_CHARGE])
        lv_obj_clear_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);

    menu_taskbar_battery = lv_label_create(status_parent);
    
    menu_taskbar_battery_percent = lv_label_create(status_parent);
    lv_obj_set_style_text_font(menu_taskbar_battery_percent, &Font_Mono_Bold_14, LV_PART_MAIN);

    // page_num is the MAX page index used by the gesture gate
    // (page_curr < page_num), not the page count: N=18 -> index 0..1.
    page_num = (MENU_BTN_NUM - 1) / 9;

    menu_screen1 = menu_page_create(parent, LV_VER_RES - status_bar_height);
    menu_screen2 = menu_page_create(parent, LV_VER_RES - status_bar_height);
    menu_screen3 = menu_page_create(parent, LV_VER_RES - status_bar_height);

    if(ui_test_a7682e() == false)
    {
        for(int i = 0; i < GET_BUFF_LEN(menu_btn_list); i++)
        {
            if(menu_btn_list[i].idx == SCREEN8_ID)
            {
                menu_btn_list[i].idx = SCREEN10_ID;
                menu_btn_list[i].name = "PCM5012";
                menu_btn_list[i].icon = &img_PCM5102;
            }
        }
    }

    for(int i = 0; i < MENU_BTN_NUM; i++) {
        if(i < 9) {
            menu_btn_create(menu_screen1, &menu_btn_list[i]);
        } else if(i < 18) {
            menu_btn_create(menu_screen2, &menu_btn_list[i]);
        } else {
            menu_btn_create(menu_screen3, &menu_btn_list[i]);
        }
    }

    if(MENU_BTN_NUM > 9) {
        ui_Panel4 = lv_obj_create(parent);
        lv_obj_set_width(ui_Panel4, 240);
        lv_obj_set_height(ui_Panel4, 25);
        lv_obj_set_align(ui_Panel4, LV_ALIGN_BOTTOM_MID);
        lv_obj_set_flex_flow(ui_Panel4, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(ui_Panel4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(ui_Panel4, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
        lv_obj_set_style_radius(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(ui_Panel4, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_spread(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_spread(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

        /* one dot per menu page (page_num is the max page index) */
        for(int i = 0; i <= page_num; i++) {
            lv_obj_t *dot = lv_btn_create(ui_Panel4);
            lv_obj_set_width(dot, 10);
            lv_obj_set_height(dot, 10);
            lv_obj_add_flag(dot, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CHECKABLE);      /// Flags
            lv_obj_set_style_radius(dot, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(dot, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(dot, DECKPRO_COLOR_FG, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* initial page state: show page 0, paint the active dot */
    menu_page_apply();
}

static void entry0(void) {
    ui_get_gesture_dir = menu_get_gesture_dir;
    lv_timer_resume(touch_chk_timer);
    lv_timer_resume(taskbar_update_timer);

    lv_label_set_text_fmt(menu_taskbar_battery, "%s", ui_battert_27220_get_percent_level());

    lv_label_set_text_fmt(menu_taskbar_battery_percent, "%d", ui_battery_27220_get_percent());
}
static void exit0(void) {
    ui_get_gesture_dir = NULL;
    lv_timer_pause(touch_chk_timer);
    lv_timer_pause(taskbar_update_timer);
}
static void destroy0(void) {
    if(menu_taskbar) {
        lv_obj_del(menu_taskbar);
        menu_taskbar = NULL;
    }
}

static scr_lifecycle_t screen0 = {
    .create = create0,
    .entry = entry0,
    .exit  = exit0,
    .destroy = destroy0,
};
#endif
//************************************[ screen 1 ]****************************************** lora
// --------------------- screen 1 --------------------- lora
#if 1
lv_obj_t * scr1_list;
static lv_obj_t *scr1_lab_buf[20];

static void scr1_list_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    for(int i = 0; i < lv_obj_get_child_cnt(obj); i++) 
    {
        lv_obj_t * child = lv_obj_get_child(obj, i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            char *str = lv_label_get_text(child);

            if(strcmp("- Auto Test", str) == 0)
            {
                scr_mgr_push(SCREEN1_1_ID, false);
            }
            if(strcmp("- Lora Setting", str) == 0)
            {
                scr_mgr_push(SCREEN1_2_ID, false);
            }
            printf("%s\n", str);
        }
    }
}

static void scr1_item_create(const char *name, lv_event_cb_t cb)
{
    lv_obj_t * obj = lv_obj_class_create_obj(&lv_list_btn_class, scr1_list);
    lv_obj_class_init_obj(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_set_height(obj, LV_VER_RES / 6);
    lv_obj_set_style_text_font(obj, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    // lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); 
}

static void scr1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create1(lv_obj_t *parent) 
{
    scr1_list = lv_list_create(parent);
    lv_obj_set_size(scr1_list, lv_pct(93), lv_pct(91));
    lv_obj_align(scr1_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_obj_set_style_bg_color(scr1_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(scr1_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr1_list, 15, LV_PART_MAIN);
    lv_obj_set_style_radius(scr1_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(scr1_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr1_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_border_color(scr1_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(scr1_list, 0, LV_PART_MAIN);

    scr1_item_create("- Auto Test", scr1_list_event);
    scr1_item_create("- Lora Setting", scr1_list_event);

    // back
    scr_back_btn_create(parent, "Lora", scr1_btn_event_cb);
}

static void entry1(void) 
{
    ui_disp_full_refr();
}
static void exit1(void) {
    ui_disp_full_refr();
}
static void destroy1(void) { }

static scr_lifecycle_t screen1 = {
    .create = create1,
    .entry = entry1,
    .exit  = exit1,
    .destroy = destroy1,
};
#endif
// --------------------- screen 1.1 --------------------- Auto Send
#if 1
static lv_obj_t *scr1_1_cont;
static lv_obj_t *lora_lab_buf[11] = {0};
static lv_obj_t *lora_sw_btn;
static lv_obj_t *lora_sw_btn_info;
static lv_timer_t *lora_RT_timer = NULL;
static lv_timer_t *lora_recv_timer = NULL;
static int lora_cnt = 0;

static void scr1_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void lora_mode_sw_event(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        if(ui_lora_get_mode() == LORA_MODE_SEND) {
            ui_lora_set_mode(LORA_MODE_RECV);
            lv_label_set_text(lora_sw_btn_info, "Recv");
            for(int i = 0; i < GET_BUFF_LEN(lora_lab_buf); i++){
                lv_label_set_text_fmt(lora_lab_buf[i], " ", i);
            }
            lora_cnt = 0;
        } else if(ui_lora_get_mode() == LORA_MODE_RECV) {
            ui_lora_set_mode(LORA_MODE_SEND);
            lv_label_set_text(lora_sw_btn_info, "Send");
            for(int i = 0; i < GET_BUFF_LEN(lora_lab_buf); i++){
                lv_label_set_text_fmt(lora_lab_buf[i], " ", i);
            }
            lora_cnt = 0;
        }
    }
}

static void lora_recv_loop_event(lv_timer_t *t)
{
    ui_lora_recv_loop();
}

static void lora_RT_timer_event(lv_timer_t *t)
{
    static int data = 0;
    char buf[32];
    const char *recv_info = NULL;
    int recv_rssi = 0;
    
    if(ui_lora_get_mode() == LORA_MODE_SEND) 

    {
        lv_snprintf(buf, 32, "DeckPro #%d", data++);
        lv_label_set_text_fmt(lora_lab_buf[lora_cnt], "send-> %s", buf);
        ui_lora_send(buf);

        lora_cnt++;
        if(lora_cnt >= GET_BUFF_LEN(lora_lab_buf)) {
            lora_cnt = 0;
        }
    }
    else if(ui_lora_get_mode() == LORA_MODE_RECV)
    {
        if(ui_lora_get_recv(&recv_info, &recv_rssi))
        {
            ui_lora_set_recv_flag();
            lv_label_set_text_fmt(lora_lab_buf[lora_cnt], "recv-> %s [%d]", recv_info, recv_rssi);

            lora_cnt++;
            if(lora_cnt >= GET_BUFF_LEN(lora_lab_buf)) {
                lora_cnt = 0;
            }
        }
    }
}

static lv_obj_t * scr2_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LV_HOR_RES - 26);
    lv_obj_set_style_text_font(label, FONT_BOLD_SIZE_15, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}
static void create1_1(lv_obj_t *parent) 
{
    scr1_1_cont = lv_obj_create(parent);
    lv_obj_set_size(scr1_1_cont, lv_pct(100), lv_pct(85));
    lv_obj_set_style_bg_color(scr1_1_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr1_1_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr1_1_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr1_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr1_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(scr1_1_cont, 13, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr1_1_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr1_1_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr1_1_cont, 5, LV_PART_MAIN);
    lv_obj_set_align(scr1_1_cont, LV_ALIGN_BOTTOM_MID);

    for(int i = 0; i < GET_BUFF_LEN(lora_lab_buf); i++){
        lora_lab_buf[i] = scr2_create_label(scr1_1_cont);
        lv_label_set_text_fmt(lora_lab_buf[i], " ", i);
    }

    lora_sw_btn = lv_btn_create(parent);
    lv_obj_set_size(lora_sw_btn, 70, 25);
    lv_obj_set_style_radius(lora_sw_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(lora_sw_btn, 2, LV_PART_MAIN);
    lora_sw_btn_info = lv_label_create(lora_sw_btn);
    lv_obj_set_style_text_font(lora_sw_btn_info, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    lv_obj_set_style_text_align(lora_sw_btn_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lora_sw_btn_info, "Send");
    lv_obj_center(lora_sw_btn_info);
    lv_obj_align(lora_sw_btn, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_add_event_cb(lora_sw_btn, lora_mode_sw_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lab = lv_label_create(parent);
    lv_obj_set_style_text_font(lab, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    lv_label_set_text_fmt(lab, "%.1fM", ui_lora_get_freq());
    lv_obj_align(lab, LV_ALIGN_TOP_RIGHT, -10, 10);

    ui_lora_set_mode(LORA_MODE_SEND);
    lora_cnt = 0;

    // back
    scr_back_btn_create(parent, ("Lora"), scr1_1_btn_event_cb);
}
static void entry1_1(void) 
{
    ui_disp_full_refr();
    lora_RT_timer = lv_timer_create(lora_RT_timer_event, 2000, NULL);
    lora_recv_timer = lv_timer_create(lora_recv_loop_event, 400, NULL);
}
static void exit1_1(void) {
    ui_disp_full_refr();
    if(lora_RT_timer) {
        lv_timer_del(lora_RT_timer);
        lora_RT_timer = NULL;
    }
    if(lora_recv_timer) {
        lv_timer_del(lora_recv_timer);
        lora_recv_timer = NULL;
    }
}
static void destroy1_1(void) { }

static scr_lifecycle_t screen1_1 = {
    .create = create1_1,
    .entry = entry1_1,
    .exit  = exit1_1,
    .destroy = destroy1_1,
};
#endif
// --------------------- screen 1.2 --------------------- Lora Setting
#if 1

#define RADIO_FREQUENCY_LIST "433MHz\n 850MHz\n 868MHz\n 915MHz\n 920MHz"
#define RADIO_BANDWIDTH "125KHz\n 250KHz\n 500KHz"
#define RADIO_TX_POWER "10dBm\n 22dBm"

static float lora_freq_list[] = {433.0, 850.0, 868.0, 915.0, 920.0};
static int lora_band_list[] = {125, 250, 500};
static int lora_power_list[] = {10, 22};

static lv_obj_t *scr1_2_cont;
static lv_obj_t *dropdown_freq;
static lv_obj_t *dropdown_band;
static lv_obj_t *dropdown_power;

static void scr1_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void lora_setting_event_handler(lv_event_t * e)
{
    char buf[32]={0};
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    const char *flag = ( const char *)lv_event_get_user_data(e);
    int select = lv_dropdown_get_selected(obj);

    lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    switch (*flag)
    {
    case 'f': 
        for(int i = 0; i < GET_BUFF_LEN(lora_freq_list); i++) {
            if(lora_freq_list[select] == lora_freq_list[i]) {
                printf("set freq %.1fMHz\n", lora_freq_list[i]);
                ui_lora_set_freq(lora_freq_list[i]);
            }
        }
        break;
    case 'b': 
        for(int i = 0; i < GET_BUFF_LEN(lora_band_list); i++) {
            if(lora_band_list[select] == lora_band_list[i]) {
                printf("set bandwidth %dKhz\n", lora_band_list[i]);
                ui_lora_set_bandwidth(lora_band_list[i]);
            }
        }
        break;
    case 'p': 
        for(int i = 0; i < GET_BUFF_LEN(lora_power_list); i++) {
            if(lora_power_list[select] == lora_power_list[i]) {
                printf("set power %ddBm\n", lora_power_list[i]);
                ui_lora_set_power(lora_power_list[i]);
            }
        }
        break;
    
    default:
        break;
    }
}

static lv_obj_t * scr1_2_lora_setting_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *ui_Container1 = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_Container1);
    lv_obj_set_height(ui_Container1, 42);
    lv_obj_set_width(ui_Container1, lv_pct(100));
    lv_obj_set_x(ui_Container1, 35);
    lv_obj_set_y(ui_Container1, -16);
    lv_obj_set_align(ui_Container1, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_Container1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Container1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_pad_row(ui_Container1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_Container1, 20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Label14 = lv_label_create(ui_Container1);
    lv_obj_set_width(ui_Label14, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label14, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Label14, -60);
    lv_obj_set_y(ui_Label14, -42);
    lv_obj_set_align(ui_Label14, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label14, text);
    lv_obj_set_style_text_font(ui_Label14, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);   

    lv_obj_t *ui_Dropdown1 = lv_dropdown_create(ui_Container1);
    lv_obj_set_width(ui_Dropdown1, lv_pct(60));
    lv_obj_set_height(ui_Dropdown1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Dropdown1, 19);
    lv_obj_set_y(ui_Dropdown1, -1);
    lv_obj_add_flag(ui_Dropdown1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags

    // lv_obj_set_style_bg_opa(ui_Dropdown1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui_Dropdown1, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    // lv_obj_set_style_shadow_width(ui_Dropdown1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);

    return ui_Dropdown1;
}

static void create1_2(lv_obj_t *parent) 
{
    scr1_2_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(scr1_2_cont);
    lv_obj_set_width(scr1_2_cont, lv_pct(100));
    lv_obj_set_height(scr1_2_cont, lv_pct(85));
    lv_obj_set_align(scr1_2_cont, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(scr1_2_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr1_2_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(scr1_2_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_pad_row(scr1_2_cont, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(scr1_2_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_width(scr1_2_cont, 3, LV_PART_MAIN);
    lv_obj_set_align(scr1_2_cont, LV_ALIGN_BOTTOM_MID);

    dropdown_freq = scr1_2_lora_setting_create(scr1_2_cont, "Freq: ");
    lv_dropdown_set_options(dropdown_freq, RADIO_FREQUENCY_LIST);
    for(int i = 0; i < GET_BUFF_LEN(lora_freq_list); i++) {
        if(ui_lora_get_freq() == lora_freq_list[i]) {
            lv_dropdown_set_selected(dropdown_freq, i);
        }
    }

    dropdown_band = scr1_2_lora_setting_create(scr1_2_cont, "Band: ");
    lv_dropdown_set_options(dropdown_band, RADIO_BANDWIDTH);
    for(int i = 0; i < GET_BUFF_LEN(lora_band_list); i++) {
        if(ui_lora_get_bandwidth() == lora_band_list[i]) {
            lv_dropdown_set_selected(dropdown_band, i);
        }
    }

    dropdown_power = scr1_2_lora_setting_create(scr1_2_cont, "Power:");
    lv_dropdown_set_options(dropdown_power, RADIO_TX_POWER);
    for(int i = 0; i < GET_BUFF_LEN(lora_power_list); i++) {
        if(ui_lora_get_power() == lora_power_list[i]) {
            lv_dropdown_set_selected(dropdown_power, i);
        }
    }
    static const char freq_flag = 'f';
    static const char band_flag = 'b';
    static const char power_flag = 'p';
    lv_obj_add_event_cb(dropdown_freq, lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&freq_flag);
    lv_obj_add_event_cb(dropdown_band, lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&band_flag);
    lv_obj_add_event_cb(dropdown_power,   lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&power_flag);
    // back
    scr_back_btn_create(parent, ("Lora Setting"), scr1_2_btn_event_cb);
}
static void entry1_2(void) 
{
    ui_disp_full_refr();
}
static void exit1_2(void) {
    ui_disp_full_refr();
    ui_lora_param_set();
}
static void destroy1_2(void) { }

static scr_lifecycle_t screen1_2 = {
    .create = create1_2,
    .entry = entry1_2,
    .exit  = exit1_2,
    .destroy = destroy1_2,
};
#endif
//************************************[ screen 2 ]****************************************** Setting
// --------------------- screen 2.1 --------------------- About System
#if 1
static lv_obj_t *scr2_1_cont;

static void scr2_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create2_1(lv_obj_t *parent) 
{
    lv_obj_t *info = lv_label_create(parent);
    lv_obj_set_width(info, LV_HOR_RES * 0.9);
    lv_obj_set_style_text_color(info, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_text_font(info, &Font_Mono_Bold_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

    String str = "";

    str += "                           \n";
    str += line_full_format(28, "SF Version:", ui_setting_get_sf_ver());
    str += "\n                           \n";

    str += line_full_format(28, "HD Version:", ui_setting_get_hd_ver());
    str += "\n                           \n";

    char buf[30];
    uint64_t total=0, used=0;
    int sd_state = 0;
    ui_setting_get_sd_capacity(&total, &used, &sd_state);
    lv_snprintf(buf, 30, "%lluMB", total);
    str += line_full_format(28, "SD total:", (const char *)buf);
    str += "\n                           \n";

    lv_snprintf(buf, 30, "%lluMB", used);
    str += line_full_format(28, "SD used:", (const char *)buf);
    str += "\n                           \n";

    /* tell the user WHY the card shows 0MB instead of leaving it cryptic.
     * State 2 only proves the card answered init commands - usually an
     * exFAT/NTFS card, but SPI/init errors on a fine FAT32 card land
     * here too, so state the failure and offer FAT32 as advice, not a
     * diagnosis (review a924c4e P2). */
    if (sd_state == 2) {
        str += line_full_format(28, "SD hint:", "mount failed");
        str += "\n                           \n";
        str += line_full_format(28, "", "try FAT16/FAT32?");
        str += "\n                           \n";
    } else if (sd_state == 1) {
        str += line_full_format(28, "SD hint:", "no card");
        str += "\n                           \n";
    }


    lv_label_set_text_fmt(info, "%s", str.c_str());
    
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 35);
    
    lv_obj_t *back2_1_label = scr_back_btn_create(parent, ("About System"), scr2_1_btn_event_cb);
}
static void entry2_1(void) 
{
    ui_disp_full_refr();
}
static void exit2_1(void) {
    ui_disp_full_refr();
}
static void destroy2_1(void) { }

static scr_lifecycle_t screen2_1 = {
    .create = create2_1,
    .entry = entry2_1,
    .exit  = exit2_1,
    .destroy = destroy2_1,
};
#endif
// --------------------- screen 2 --------------------- Setting
#if 1
static lv_obj_t *setting_list;
static lv_obj_t *setting_page;
static int setting_num = 0;
static int setting_page_num = 0;
static int setting_curr_page = 0;
static ui_setting_handle setting_handle_list[] = {
    {.name = "Keypad Backlight", .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_keypad_light, .get_cb = ui_setting_get_keypad_light},
    {.name = "Motor Status",     .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_motor_status, .get_cb = ui_setting_get_motor_status},
    {.name = "Power GPS",        .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_gps_status,   .get_cb = ui_setting_get_gps_status},
    {.name = "Power Lora",       .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_lora_status,  .get_cb = ui_setting_get_lora_status},
    {.name = "Power Gyro",       .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_gyro_status,  .get_cb = ui_setting_get_gyro_status},
    {.name = "Power A7682",      .type=UI_SETTING_TYPE_SW,  .set_cb = ui_setting_set_a7682_status, .get_cb = ui_setting_get_a7682_status},
    {.name = "- About System",   .type=UI_SETTING_TYPE_SUB, .sub_id = SCREEN2_1_ID},
};

static void setting_item_create(int curr_apge);

static void scr2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void setting_scr_event(lv_event_t *e)
{
    lv_obj_t *tgt = (lv_obj_t *)e->target;
    ui_setting_handle *h = (ui_setting_handle *)e->user_data;

    if(e->code == LV_EVENT_CLICKED) {
        switch (h->type)
        {
        case UI_SETTING_TYPE_SW:
            h->set_cb(!h->get_cb());
            lv_label_set_text_fmt(h->st, "%s", (h->get_cb() ? "ON" : "OFF"));
            break;
        case UI_SETTING_TYPE_SUB:
            scr_mgr_push(h->sub_id, false);
            break;
        default:
            break;
        }
    }
}

static void setting_page_switch_cb(lv_event_t *e)
{
    char opt = (int)e->user_data;
    
    if(setting_num < SETTING_PAGE_MAX_ITEM) return;

    int child_cnt = lv_obj_get_child_cnt(setting_list);
    
    for(int i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(setting_list, 0);
        if(child)
            lv_obj_del(child);
    }

    if(opt == 'p')
    {
        setting_curr_page = (setting_curr_page < setting_page_num) ? setting_curr_page + 1 : 0;
    }
    else if(opt == 'n')
    {
        setting_curr_page = (setting_curr_page > 0) ? setting_curr_page - 1 : setting_page_num;
    }

    setting_item_create(setting_curr_page);
    lv_label_set_text_fmt(setting_page, "%d / %d", setting_curr_page, setting_page_num);
}

static void setting_item_create(int curr_apge)
{
    printf("setting_curr_page = %d\n", setting_curr_page);
    int start = (curr_apge * SETTING_PAGE_MAX_ITEM);
    int end = start + SETTING_PAGE_MAX_ITEM;
    if(end > setting_num) end = setting_num;

    printf("start=%d, end=%d\n", start, end);

    for(int i = start; i < end; i++) {
        ui_setting_handle *h = &setting_handle_list[i];
        

        switch (h->type)
        {
        case UI_SETTING_TYPE_SW:
            h->obj = lv_list_add_btn(setting_list, NULL, h->name);
            h->st = lv_label_create(h->obj);
            lv_obj_set_style_text_font(h->st, FONT_BOLD_SIZE_15, LV_PART_MAIN);
            lv_obj_align(h->st, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_label_set_text_fmt(h->st, "%s", (h->get_cb() ? "ON" : "OFF"));
            break;
        case UI_SETTING_TYPE_SUB:
            h->obj = lv_list_add_btn(setting_list, NULL, h->name);
            break;
        default:
            break;
        }

        // style
        lv_obj_set_height(h->obj, 28);
        lv_obj_set_style_text_font(h->obj, FONT_BOLD_SIZE_14, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h->obj, DECKPRO_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_text_color(h->obj, DECKPRO_COLOR_FG, LV_PART_MAIN);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(h->obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(h->obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(h->obj, setting_scr_event, LV_EVENT_CLICKED, (void *)h);
    }
}

static void create2(lv_obj_t *parent) 
{
    setting_list = lv_list_create(parent);
    lv_obj_set_size(setting_list, LV_HOR_RES, lv_pct(88));
    lv_obj_align(setting_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(setting_list, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_top(setting_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(setting_list, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(setting_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(setting_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(setting_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(setting_list, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(setting_list, 0, LV_PART_MAIN);

    setting_num = sizeof(setting_handle_list) / sizeof(setting_handle_list[0]);
    setting_page_num = setting_num / SETTING_PAGE_MAX_ITEM;

    setting_item_create(setting_curr_page);

    lv_obj_t * ui_Button2 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button2, 71);
    lv_obj_set_height(ui_Button2, 40);
    lv_obj_set_x(ui_Button2, -70);
    lv_obj_set_y(ui_Button2, 130);
    lv_obj_set_align(ui_Button2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label1 = lv_label_create(ui_Button2);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "Back");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Button14 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button14, 71);
    lv_obj_set_height(ui_Button14, 40);
    lv_obj_set_x(ui_Button14, 70);
    lv_obj_set_y(ui_Button14, 130);
    lv_obj_set_align(ui_Button14, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button14, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button14, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button14, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button14, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label15 = lv_label_create(ui_Button14);
    lv_obj_set_width(ui_Label15, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label15, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label15, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label15, "Next");
    lv_obj_set_style_text_color(ui_Label15, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Button2, setting_page_switch_cb, LV_EVENT_CLICKED, (void*)'n');
    lv_obj_add_event_cb(ui_Button14, setting_page_switch_cb, LV_EVENT_CLICKED, (void*)'p');

    setting_page = lv_label_create(parent);
    lv_obj_set_width(setting_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(setting_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(setting_page, LV_ALIGN_BOTTOM_MID, 0, -23);
    lv_label_set_text_fmt(setting_page, "%d / %d", setting_curr_page, setting_page_num);
    lv_obj_set_style_text_color(setting_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(setting_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    scr_back_btn_create(parent, ("Setting"), scr2_btn_event_cb);
}
static void entry2(void) {
    ui_disp_full_refr();
}
static void exit2(void) {
    ui_disp_full_refr();
}
static void destroy2(void) { }

static scr_lifecycle_t screen2 = {
    .create = create2,
    .entry = entry2,
    .exit  = exit2,
    .destroy = destroy2,
};
#endif
//************************************[ screen 3 ]****************************************** GPS
#if 1
#define line_max 23
static lv_obj_t *scr3_cont;
static lv_obj_t *scr3_cnt_lab;
static lv_timer_t *GPS_loop_timer = NULL;

static void gps_set_line(lv_obj_t *label, const char *str1, const char *str2)
{
    int w2 = strlen(str2);
    int w1 = line_max - w2;
    lv_label_set_text_fmt(label, "%-*s%-*s", w1, str1, w2, str2);
}

static lv_obj_t * scr3_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(90));
    lv_obj_set_style_text_font(label, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    return label;
}

static void scr3_GPS_updata(void)
{
    double lat      = 0; // Latitude
    double lon      = 0; // Longitude
    double speed    = 0; // Speed over ground
    float alt      = 0; // Altitude
    float accuracy = 0; // Accuracy
    uint32_t   vsat     = 0; // Visible Satellites
    int   usat     = 0; // Used Satellites
    uint16_t   year     = 0; // 
    uint8_t   month    = 0; // 
    uint8_t   day      = 0; // 
    uint8_t   hour     = 0; // 
    uint8_t   min      = 0; // 
    uint8_t   sec      = 0; // 

    static int cnt = 0;

    lv_label_set_text_fmt(scr3_cnt_lab, " %05d ", ++cnt);

    ui_gps_get_coord(&lat, &lon);
    ui_gps_get_data(&year, &month, &day);
    ui_gps_get_time(&hour, &min, &sec);
    ui_gps_get_satellites(&vsat);
    ui_gps_get_speed(&speed);

    char buf[32];

    lv_snprintf(buf, 16, "%0.1f", lat);
    gps_set_line(label_list[0], "Latitude:", buf);

    lv_snprintf(buf, 16, "%0.1f", lon);
    gps_set_line(label_list[1], "Longitude:", buf);

    lv_snprintf(buf, 16, "%0.3fkmph", speed);
    gps_set_line(label_list[2], "Speed:", buf);

    lv_snprintf(buf, 16, "%d", vsat);
    gps_set_line(label_list[3], "vsat:", buf);
    
    lv_snprintf(buf, 16, "%d", year);
    gps_set_line(label_list[4], "year:", buf);

    lv_snprintf(buf, 16, "%d", month);
    gps_set_line(label_list[5], "month:", buf);

    lv_snprintf(buf, 16, "%d", day);
    gps_set_line(label_list[6], "day:", buf);

    lv_snprintf(buf, 16, "%02d:%02d:%02d", hour, min, sec);
    gps_set_line(label_list[7], "time:", buf);

    // lv_snprintf(buf, 16, "%0.1f", alt);
    // gps_set_line(label_list[3], "alt:", buf);

    // lv_snprintf(buf, 16, "%d", usat);
    // gps_set_line(label_list[5], "usat:", buf);

}

static void GPS_loop_timer_event(lv_timer_t * t)
{
    scr3_GPS_updata();
}

static void scr3_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create3(lv_obj_t *parent) 
{   
    scr3_cont = lv_obj_create(parent);
    lv_obj_set_size(scr3_cont, lv_pct(100), lv_pct(88));
    lv_obj_set_style_bg_color(scr3_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr3_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr3_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr3_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_align(scr3_cont, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_flex_flow(scr3_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr3_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    for(int i = 0; i < sizeof(label_list) / sizeof(label_list[0]); i++) {
        label_list[i] = scr3_create_label(scr3_cont);
        lv_label_set_text(label_list[i], " ");
    }

    scr3_cnt_lab = lv_label_create(parent);
    lv_obj_set_style_text_font(scr3_cnt_lab, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    lv_obj_set_style_radius(scr3_cnt_lab, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr3_cnt_lab, 2, LV_PART_MAIN);
    lv_obj_set_style_text_align(scr3_cnt_lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(scr3_cnt_lab, " %05d ", 0);
    lv_obj_center(scr3_cnt_lab);
    lv_obj_align(scr3_cnt_lab, LV_ALIGN_TOP_RIGHT, -10, 10);

    lv_obj_t *back3_label = scr_back_btn_create(parent, ("GPS"), scr3_btn_event_cb);
}
static void entry3(void) 
{
    scr3_GPS_updata();

    ui_gps_task_resume();

    GPS_loop_timer = lv_timer_create(GPS_loop_timer_event, 3000, NULL);
    ui_disp_full_refr();
}
static void exit3(void) {
    ui_gps_task_suspend();
    if(GPS_loop_timer) {
        lv_timer_del(GPS_loop_timer);
        GPS_loop_timer = NULL;
    }
    ui_disp_full_refr();
}
static void destroy3(void) { }

static scr_lifecycle_t screen3 = {
    .create = create3,
    .entry = entry3,
    .exit  = exit3,
    .destroy = destroy3,
};

#undef line_max

#endif
//************************************[ screen 4 ]****************************************** Wifi Scan
// --------------------- screen 4 --------------------- WIFI
#if 1
lv_obj_t * scr4_list;
static lv_obj_t *scr4_lab_buf[20];

// --------------------- WIFI Test (list item, no separate screen) ----------
#include "http_utils.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <lwip/sockets.h>   /* inet_pton / AF_INET for the IP format check */

static lv_obj_t *wifi_test_popup = NULL;
static bool wifi_test_active = false;           /* set while the WIFI page is on top */

static void wifi_test_close_cb(lv_event_t *e)
{
    if (wifi_test_popup) {
        lv_obj_del(wifi_test_popup);
        wifi_test_popup = NULL;
    }
}

/* Result popup (信息层): title + wrapped body + Close button. */
static void wifi_test_show_result(const char *title, const char *text)
{
    wifi_test_close_cb(NULL);
    wifi_test_popup = lv_obj_create(lv_layer_top());
    lv_obj_set_size(wifi_test_popup, 220, 250);
    lv_obj_align(wifi_test_popup, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(wifi_test_popup, lv_color_white(), 0);
    lv_obj_set_style_border_width(wifi_test_popup, 1, 0);
    lv_obj_set_style_border_color(wifi_test_popup, lv_color_black(), 0);
    lv_obj_set_style_radius(wifi_test_popup, 6, 0);
    lv_obj_set_style_pad_all(wifi_test_popup, 8, 0);
    lv_obj_set_flex_flow(wifi_test_popup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_test_popup, 6, 0);
    lv_obj_clear_flag(wifi_test_popup, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(wifi_test_popup);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);

    lv_obj_t *b = lv_label_create(wifi_test_popup);
    lv_obj_set_width(b, lv_pct(100));
    lv_label_set_long_mode(b, LV_LABEL_LONG_WRAP);
    lv_label_set_text(b, text);
    lv_obj_set_style_text_font(b, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(b, 1);

    lv_obj_t *close_btn = lv_btn_create(wifi_test_popup);
    lv_obj_set_width(close_btn, lv_pct(100));
    lv_obj_set_height(close_btn, 32);
    lv_obj_t *close_lab = lv_label_create(close_btn);
    lv_label_set_text(close_lab, "Close");
    lv_obj_center(close_lab);
    lv_obj_add_event_cb(close_btn, wifi_test_close_cb, LV_EVENT_CLICKED, NULL);
}

/* WiFi Test is fully asynchronous (review round 7 finding 1.3): the HTTP
 * request runs in a FreeRTOS task, the UI thread polls the result with an
 * LVGL timer - the UI never freezes and leaving the page works anytime.
 *
 * Review findings 1.4/1.5 fixes: results travel over a FreeRTOS queue as
 * heap-allocated structs (ownership transfers to the UI thread; no
 * volatile-flag-guarded std::string cross-core access), and every request
 * carries the page generation captured at launch - stale results from a
 * previous visit of the page are dropped. */
#define WIFI_TEST_URL "https://ifconfig.me/ip"  /* /ip returns plain text, not HTML */

typedef struct {
    uint32_t gen;               /* page generation at launch */
    http_response_t resp;
} wifi_test_result_t;

typedef struct {
    uint32_t gen;
    time_t before;
    time_t after;
    bool ok;
} time_sync_result_t;

static QueueHandle_t s_wifi_test_q = NULL;
static QueueHandle_t s_time_sync_q = NULL;
static volatile uint32_t s_wifi_page_gen = 0;   /* bumped on every page entry */
static volatile bool s_wifi_test_busy = false;  /* UI-owned busy state */
static uint32_t s_wifi_test_busy_gen = 0;       /* gen of the in-flight request */
static volatile bool s_time_sync_busy = false;
static uint32_t s_time_sync_busy_gen = 0;
static lv_timer_t *wifi_test_timer = NULL;

static bool wifi_test_ip_valid(const char *s)
{
    uint32_t a4 = 0;
    uint8_t a6[16];
    if (!s || !*s) return false;
    return inet_pton(AF_INET, s, &a4) == 1 || inet_pton(AF_INET6, s, a6) == 1;
}

static void wifi_test_task_func(void *param)
{
    uint32_t gen = (uint32_t)(uintptr_t)param;
    wifi_test_result_t *res = new wifi_test_result_t;
    res->gen = gen;
    res->resp = http_get_ua(WIFI_TEST_URL, "curl/8.5.0", 15000);
    if (s_wifi_test_q) {
        xQueueSend(s_wifi_test_q, &res, portMAX_DELAY);
    } else {
        delete res;
    }
    vTaskDelete(NULL);
}

static void wifi_page_timer_cb(lv_timer_t *t)
{
    /* Drain time sync results (queue, ownership transfers to the UI). */
    time_sync_result_t *ts = NULL;
    while (s_time_sync_q && xQueueReceive(s_time_sync_q, &ts, 0) == pdTRUE) {
        if (!ts) continue;
        /* release busy only when THIS request finished (review: a stale
         * result must not unlock a newer in-flight request) */
        if (ts->gen == s_time_sync_busy_gen) s_time_sync_busy = false;
        if (ts->gen == s_wifi_page_gen && wifi_test_active) {
            struct tm tmb, tma;
            localtime_r(&ts->before, &tmb);
            localtime_r(&ts->after, &tma);
            char buf[192];
            if (ts->ok) {
                snprintf(buf, sizeof(buf),
                         "Before: %04d-%02d-%02d %02d:%02d:%02d\n"
                         "After:  %04d-%02d-%02d %02d:%02d:%02d",
                         tmb.tm_year + 1900, tmb.tm_mon + 1, tmb.tm_mday,
                         tmb.tm_hour, tmb.tm_min, tmb.tm_sec,
                         tma.tm_year + 1900, tma.tm_mon + 1, tma.tm_mday,
                         tma.tm_hour, tma.tm_min, tma.tm_sec);
            } else {
                snprintf(buf, sizeof(buf),
                         "Sync failed\nbefore=%ld after=%ld",
                         (long)ts->before, (long)ts->after);
            }
            Serial.printf("[TimeSync] before=%ld after=%ld %s\n",
                          (long)ts->before, (long)ts->after,
                          ts->ok ? "ok" : "failed");
            wifi_test_show_result("Time Sync", buf);
        } else {
            Serial.println("[TimeSync] stale result dropped");
        }
        delete ts;
    }

    /* Drain WiFi Test results. */
    wifi_test_result_t *wr = NULL;
    while (s_wifi_test_q && xQueueReceive(s_wifi_test_q, &wr, 0) == pdTRUE) {
        if (!wr) continue;
        if (wr->gen == s_wifi_test_busy_gen) s_wifi_test_busy = false;
        if (wr->gen == s_wifi_page_gen && wifi_test_active) {
            http_response_t resp = wr->resp;
            if (resp.success && resp.status_code == 200) {
                char ip[80];
                strncpy(ip, resp.body.c_str(), sizeof(ip) - 1);
                ip[sizeof(ip) - 1] = '\0';
                char *endp = ip + strlen(ip);
                while (endp > ip && (endp[-1] == ' ' || endp[-1] == '\t' ||
                                     endp[-1] == '\r' || endp[-1] == '\n')) {
                    *--endp = '\0';
                }
                if (wifi_test_ip_valid(ip)) {
                    Serial.printf("[WiFiTest] public ip: %s\n", ip);
                    char msg[96];
                    snprintf(msg, sizeof(msg), "Public IP:\n%s", ip);
                    wifi_test_show_result("WiFi Test OK", msg);
                } else {
                    Serial.printf("[WiFiTest] unexpected response: %s\n", ip);
                    wifi_test_show_result("WiFi Test", "Unexpected response\n(not an IP address)");
                }
            } else {
                Serial.printf("[WiFiTest] request failed code=%d err=%s\n",
                              resp.status_code, resp.error.c_str());
                char buf[128];
                if (!resp.error.empty()) {
                    snprintf(buf, sizeof(buf), "Request failed\n%s", resp.error.c_str());
                } else {
                    snprintf(buf, sizeof(buf), "Request failed\nHTTP %d", resp.status_code);
                }
                wifi_test_show_result("WiFi Test", buf);
            }
        } else {
            Serial.println("[WiFiTest] stale result dropped");
        }
        delete wr;
    }
}

static void wifi_test_run(void)
{
    if (!http_require_wifi("WiFi Test")) {
        /* caller-side feedback: http_require_wifi only reports the state
         * (review round 6 finding 1.2) */
        Serial.println("[WiFiTest] WiFi not connected");
        wifi_test_show_result("WiFi Test", "WiFi not connected\nconfigure it first");
        return;
    }
    if (s_wifi_test_busy) return;               /* already running */
    if (!s_wifi_test_q) s_wifi_test_q = xQueueCreate(4, sizeof(void *));
    if (!s_wifi_test_q) {                       /* must not start without a queue */
        wifi_test_show_result("WiFi Test", "Cannot start task");
        return;
    }

    wifi_test_show_result("WiFi Test", "Testing...");
    s_wifi_test_busy = true;
    s_wifi_test_busy_gen = s_wifi_page_gen;
    TaskHandle_t h = NULL;
    if (xTaskCreate(wifi_test_task_func, "wifi_test", 1024 * 8,
                    (void *)(uintptr_t)s_wifi_page_gen, 1, &h) != pdPASS) {
        s_wifi_test_busy = false;
        wifi_test_show_result("WiFi Test", "Cannot start task");
    }
}

/* Time Sync list item: async NTP sync, popup shows before/after times. */
static void time_sync_task_func(void *param)
{
    uint32_t gen = (uint32_t)(uintptr_t)param;
    time_sync_result_t *res = new time_sync_result_t;
    res->gen = gen;
    res->before = time(nullptr);
    configTzTime("CST-8", "cn.pool.ntp.org", "pool.ntp.org", "time.nist.gov");
    uint32_t t0 = millis();
    while (time(nullptr) <= 1700000000 && millis() - t0 < 10000) {
        delay(100);
    }
    res->after = time(nullptr);
    res->ok = (res->after > 1700000000);
    if (s_time_sync_q) {
        xQueueSend(s_time_sync_q, &res, portMAX_DELAY);
    } else {
        delete res;
    }
    vTaskDelete(NULL);
}

static void time_sync_run(void)
{
    if (!http_require_wifi("Time Sync")) {
        wifi_test_show_result("Time Sync", "WiFi not connected\nconfigure it first");
        return;
    }
    if (s_time_sync_busy) return;               /* already running */
    if (!s_time_sync_q) s_time_sync_q = xQueueCreate(4, sizeof(void *));
    if (!s_time_sync_q) {
        wifi_test_show_result("Time Sync", "Cannot start task");
        return;
    }

    wifi_test_show_result("Time Sync", "Syncing...");
    s_time_sync_busy = true;
    s_time_sync_busy_gen = s_wifi_page_gen;
    TaskHandle_t h = NULL;
    if (xTaskCreate(time_sync_task_func, "time_sync", 1024 * 4,
                    (void *)(uintptr_t)s_wifi_page_gen, 1, &h) != pdPASS) {
        s_time_sync_busy = false;
        wifi_test_show_result("Time Sync", "Cannot start task");
    }
}

static void scr4_list_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    for(int i = 0; i < lv_obj_get_child_cnt(obj); i++) 
    {
        lv_obj_t * child = lv_obj_get_child(obj, i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            char *str = lv_label_get_text(child);

            if(strcmp("- WIFI Config", str) == 0)
            {
                scr_mgr_push(SCREEN4_1_ID, false);
            }
            if(strcmp("- WIFI Scan", str) == 0)
            {
                scr_mgr_push(SCREEN4_2_ID, false);
            }
            if(strcmp("- WIFI Test", str) == 0)
            {
                wifi_test_run();
            }
            if(strcmp("- Time Sync", str) == 0)
            {
                time_sync_run();
            }
            printf("%s\n", str);
        }
    }
}

static void scr4_item_create(const char *name, lv_event_cb_t cb)
{
    lv_obj_t * obj = lv_obj_class_create_obj(&lv_list_btn_class, scr4_list);
    lv_obj_class_init_obj(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_set_height(obj, LV_VER_RES / 6);
    lv_obj_set_style_text_font(obj, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    // lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); 
}

static void scr4_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create4(lv_obj_t *parent) 
{
    scr4_list = lv_list_create(parent);
    lv_obj_set_size(scr4_list, lv_pct(93), lv_pct(91));
    lv_obj_align(scr4_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_obj_set_style_bg_color(scr4_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(scr4_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr4_list, 15, LV_PART_MAIN);
    lv_obj_set_style_radius(scr4_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(scr4_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr4_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_border_color(scr4_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(scr4_list, 0, LV_PART_MAIN);

    scr4_item_create("- WIFI Config", scr4_list_event);
    scr4_item_create("- WIFI Scan", scr4_list_event);
    scr4_item_create("- WIFI Test", scr4_list_event);
    scr4_item_create("- Time Sync", scr4_list_event);

    // back
    scr_back_btn_create(parent, "WIFI", scr4_btn_event_cb);
}

static void entry4(void)
{
    ui_disp_full_refr();
    wifi_test_active = true;
    s_wifi_page_gen++;                          /* invalidate in-flight requests of a previous visit */
    if (!wifi_test_timer) {
        wifi_test_timer = lv_timer_create(wifi_page_timer_cb, 100, NULL);
    }
}
static void exit4(void) {
    ui_disp_full_refr();
    wifi_test_active = false;
    wifi_test_close_cb(NULL);                   /* no popup on other screens */
    if (wifi_test_timer) {
        lv_timer_del(wifi_test_timer);
        wifi_test_timer = NULL;
    }
}
static void destroy4(void) {
    wifi_test_active = false;
    wifi_test_close_cb(NULL);
    if (wifi_test_timer) {
        lv_timer_del(wifi_test_timer);
        wifi_test_timer = NULL;
    }
    /* async IPC contract rule 8 (copilot finding 1.5): leaving the page
     * invalidates the old generation and clears both busy flags - without
     * this, re-entering within the request window gets silently rejected.
     * The in-flight tasks keep their own snapshots; their late results are
     * dropped by the generation check, and busy_gen prevents a stale
     * result from unlocking a newer request. */
    s_wifi_page_gen++;
    s_wifi_test_busy = false;
    s_time_sync_busy = false;
}

static scr_lifecycle_t screen4 = {
    .create = create4,
    .entry = entry4,
    .exit  = exit4,
    .destroy = destroy4,
};
#endif
// --------------------- screen 4.1 --------------------- Wifi Config
#if 1
#include <WiFi.h>
#include <esp_wifi.h>       /* esp_wifi_scan_stop(): actually abort an async scan */
#include <Preferences.h>

static lv_obj_t *wifi_ssid_lab = NULL;
static lv_obj_t *wifi_ssid_ta = NULL;
static lv_obj_t *wifi_pass_lab = NULL;
static lv_obj_t *wifi_pass_ta = NULL;
static lv_obj_t *wifi_status_lab = NULL;
static bool wifi_cfg_kbd_active = false;
static int  wifi_cfg_field = 0;                 // 0 = SSID, 1 = password
static bool wifi_cfg_scan_mode = false;         // true: +/- cycle scan picks (else manual edit)
static int16_t wifi_scan_state = WIFI_SCAN_FAILED; // WIFI_SCAN_RUNNING while async scan active
static uint8_t wifi_scan_gen = 0;                 // bumped to invalidate an in-flight scan
static uint8_t wifi_scan_pending_gen = 0;         // generation of the scan in flight
static volatile uint32_t s_scan_done_cnt = 0;     // increments on every SCAN_DONE event
static bool s_scan_event_registered = false;
static bool s_scan_release_pending = false;       // aborted scan's SCAN_DONE still missing
static uint32_t s_scan_release_target = 0;        // cnt at abort; an event past it clears pending
/* The callback runs on the WiFi event task, the UI polls from the main
 * loop: cnt/pending/target are shared state and must be touched inside
 * this critical section (copilot finding 1.3: unsynchronized reads can
 * miss a publish/callback race). Serial is NOT used inside the section. */
static portMUX_TYPE s_scan_mux = portMUX_INITIALIZER_UNLOCKED;

/* All shared-state READS also go through the critical section (copilot
 * finding 1.3): the callback runs on the WiFi event task, so a plain
 * read on the UI core is a C++ data race even if it looks atomic. */
static bool scan_release_is_pending(void)
{
    bool v;
    portENTER_CRITICAL(&s_scan_mux);
    v = s_scan_release_pending;
    portEXIT_CRITICAL(&s_scan_mux);
    return v;
}

/* Framework event order (WiFiGenericClass::_eventCallback): the internal
 * WiFiScanClass::_scanDone() runs FIRST (allocates + fills the results),
 * user onEvent callbacks dispatch AFTER it. So once this fires, it is safe
 * to scanDelete() the results (review round 4 finding 1.2). The counter
 * binds waits to a specific event instead of a flag (review finding 1.3);
 * a late event arriving before the retry clears the pending state itself
 * (copilot finding 1.3: retry must not re-baseline the wait). */
static void wifi_scan_done_cb(arduino_event_id_t event, arduino_event_info_t info)
{
    bool release = false;
    portENTER_CRITICAL(&s_scan_mux);
    s_scan_done_cnt++;
    if (s_scan_release_pending && s_scan_done_cnt > s_scan_release_target) {
        s_scan_release_pending = false;
        release = true;
    }
    portEXIT_CRITICAL(&s_scan_mux);
    if (release) {
        Serial.println("[WiFi] deferred release satisfied");
    }
}
static char wifi_scan_ssids[UI_WIFI_SCAN_ITEM_MAX][33];
static int  wifi_scan_cnt = 0;                  // visible SSIDs from last scan
static int  wifi_scan_idx = 0;                  // currently shown candidate
static char wifi_ssid_pre_scan[65] = {0};       // box content before entering scan mode
static char wifi_ssid[65] = {0};
static char wifi_pass[65] = {0};
static char wifi_status[96] = {0};

static void wifi_cfg_load(void)
{
    Preferences p;
    p.begin("wifi", true);
    String s = p.getString("ssid", "");
    String q = p.getString("pass", "");
    strncpy(wifi_ssid, s.c_str(), sizeof(wifi_ssid) - 1);
    strncpy(wifi_pass, q.c_str(), sizeof(wifi_pass) - 1);
    p.end();
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(wifi_status, sizeof(wifi_status), "IP: %s", WiFi.localIP().toString().c_str());
    } else {
        snprintf(wifi_status, sizeof(wifi_status), "Not connected");
    }
}

static void wifi_cfg_save(void)
{
    Preferences p;
    p.begin("wifi", false);
    p.putString("ssid", wifi_ssid);
    p.putString("pass", wifi_pass);
    p.end();
}

/* Update field markers/status only. The textareas hold live drafts and must
 * NOT be rewritten from the buffers on field transitions (finding 2.4). */
static void wifi_cfg_refresh_labels(void)
{
    lv_label_set_text(wifi_ssid_lab, wifi_cfg_field == 0 ? "SSID >" : "SSID");
    lv_label_set_text(wifi_pass_lab, wifi_cfg_field == 1 ? "Pass >" : "Pass");
    lv_label_set_text(wifi_status_lab, wifi_status);
}

/* Sync the current field's textarea into its draft buffer, so switching
 * fields keeps uncommitted edits (finding 2.4). */
static void wifi_cfg_sync_draft(void)
{
    lv_obj_t *ta = (wifi_cfg_field == 0) ? wifi_ssid_ta : wifi_pass_ta;
    char *buf = (wifi_cfg_field == 0) ? wifi_ssid : wifi_pass;
    strncpy(buf, lv_textarea_get_text(ta), 64);
    buf[64] = '\0';
}

/* Start an asynchronous WiFi scan (Alt+Enter in the SSID field). Keeps the
 * pre-scan box content so the pick can be cancelled. Returns true if the
 * scan was actually started. */
static void wifi_cfg_scan_abort(void);
static void wifi_scan_overlay_show(void);
static void wifi_scan_overlay_update(void);
static void wifi_scan_overlay_hide(void);
static void wifi_banner_show(const char *text);
static void wifi_banner_update(void);
static void wifi_banner_hide(void);

static bool wifi_cfg_scan_start(void)
{
    if (scan_release_is_pending()) {
        /* A previous aborted scan has not delivered its SCAN_DONE yet;
         * wait (bounded) for it before touching the framework results,
         * otherwise scanDelete() races the late _scanDone() (review 1.3).
         * The event callback clears the pending state itself as soon as
         * the target count is exceeded. */
        uint32_t t0 = millis();
        while (scan_release_is_pending() && millis() - t0 < 3000) {
            delay(1);
        }
        if (scan_release_is_pending()) {
            snprintf(wifi_status, sizeof(wifi_status), "Scan busy - retry");
            lv_label_set_text(wifi_status_lab, wifi_status);
            Serial.println("[WiFi] scan start blocked: previous SCAN_DONE pending");
            return false;
        }
        WiFi.scanDelete();                      /* event arrived; safe to release now */
    }

    wifi_scan_gen++;
    wifi_scan_pending_gen = wifi_scan_gen;
    if (!wifi_cfg_scan_mode) {
        strncpy(wifi_ssid_pre_scan, lv_textarea_get_text(wifi_ssid_ta),
                sizeof(wifi_ssid_pre_scan) - 1);
        wifi_ssid_pre_scan[sizeof(wifi_ssid_pre_scan) - 1] = '\0';
    }
    WiFi.scanDelete();
    int16_t r = WiFi.scanNetworks(true);        /* async; main loop keeps draining the key FIFO */
    if (r == WIFI_SCAN_RUNNING) {
        wifi_scan_state = WIFI_SCAN_RUNNING;
        snprintf(wifi_status, sizeof(wifi_status), "Scanning...");
        lv_label_set_text(wifi_status_lab, wifi_status);
        wifi_scan_overlay_show();               /* topmost countdown, input blocked */
        Serial.println("[WiFi] scan started (async)");
        return true;
    }
    wifi_scan_state = r;
    snprintf(wifi_status, sizeof(wifi_status), "Scan start fail (%d)", r);
    lv_label_set_text(wifi_status_lab, wifi_status);
    Serial.printf("[WiFi] scan start failed r=%d\n", r);
    wifi_banner_show("Scan start failed");
    return false;
}

/* Called every loop pass while the screen is active: collect the async scan
 * result when it finishes. */
static void wifi_cfg_scan_poll(void)
{
    if (wifi_scan_state != WIFI_SCAN_RUNNING) return;
    int16_t r = WiFi.scanComplete();
    if (r == WIFI_SCAN_RUNNING) return;         /* still scanning */

    if (r < 0) {
        /* scan failure kept distinct from "no networks found" (finding 1.5) */
        wifi_scan_state = r;
        snprintf(wifi_status, sizeof(wifi_status), "Scan failed (%d)", r);
        lv_label_set_text(wifi_status_lab, wifi_status);
        Serial.printf("[WiFi] scan failed r=%d\n", r);
        wifi_banner_show("Scan failed");
        return;
    }

    wifi_scan_state = r;
    if (wifi_scan_pending_gen != wifi_scan_gen || wifi_cfg_field != 0) {
        /* superseded by a newer scan/exit, or the user left the SSID field:
         * drop the results instead of overwriting the draft (finding 2.3) */
        WiFi.scanDelete();
        wifi_scan_state = WIFI_SCAN_FAILED;
        Serial.println("[WiFi] scan results dropped (superseded)");
        return;
    }
    wifi_scan_cnt = 0;
    wifi_scan_idx = 0;
    for (int i = 0; i < r && wifi_scan_cnt < UI_WIFI_SCAN_ITEM_MAX; i++) {
        String s = WiFi.SSID(i);
        if (s.length() == 0) continue;          /* hidden network */
        strncpy(wifi_scan_ssids[wifi_scan_cnt], s.c_str(), 32);
        wifi_scan_ssids[wifi_scan_cnt][32] = '\0';
        Serial.printf("[WiFi] scan[%d] %s\n", wifi_scan_cnt, wifi_scan_ssids[wifi_scan_cnt]);
        if (strcmp(wifi_ssid, wifi_scan_ssids[wifi_scan_cnt]) == 0) {
            wifi_scan_idx = wifi_scan_cnt;      /* keep the current pick */
        }
        wifi_scan_cnt++;
    }
    WiFi.scanDelete();                          /* release the framework's result memory (2.3) */
    if (wifi_scan_cnt == 0) {
        snprintf(wifi_status, sizeof(wifi_status), "Scan: none found");
        lv_label_set_text(wifi_status_lab, wifi_status);
        Serial.println("[WiFi] scan done: no networks found");
        wifi_banner_show("Scan: none found");
        return;                                 /* stay in manual edit mode */
    }
    snprintf(wifi_status, sizeof(wifi_status), "Scan: %d found", wifi_scan_cnt);
    lv_label_set_text(wifi_status_lab, wifi_status);
    wifi_cfg_scan_mode = true;
    lv_textarea_set_text(wifi_ssid_ta, wifi_scan_ssids[wifi_scan_idx]);
    char banner_buf[48];
    snprintf(banner_buf, sizeof(banner_buf), "Scan: %d found", wifi_scan_cnt);
    wifi_banner_show(banner_buf);
}

/* Kick off (and briefly await) NTP right after a successful connect so TLS
 * cert validation has a sane clock. cn.pool.ntp.org first: pool.ntp.org is
 * often unreachable in CN networks. */
static void wifi_time_sync(void)
{
    Serial.println("[WiFi] connected - syncing NTP time");
    configTzTime("CST-8", "cn.pool.ntp.org", "pool.ntp.org", "time.nist.gov");
    uint32_t t0 = millis();
    while (time(nullptr) <= 1700000000 && millis() - t0 < 8000) {
        delay(100);
        lv_timer_handler();
    }
    Serial.printf("[WiFi] time sync %s (epoch=%ld)\n",
                  time(nullptr) > 1700000000 ? "ok" : "pending", (long)time(nullptr));
}

/* Try to connect with the current ssid/pass. Returns true on success.
 * Callers persist to NVS only when this returns true. */
static bool wifi_cfg_connect(void)
{
    if (wifi_ssid[0] == '\0') {
        snprintf(wifi_status, sizeof(wifi_status), "No SSID set");
        lv_label_set_text(wifi_status_lab, wifi_status);
        return false;
    }
    snprintf(wifi_status, sizeof(wifi_status), "Connecting...");
    lv_label_set_text(wifi_status_lab, wifi_status);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_ssid, wifi_pass);

    unsigned long t0 = millis();
    wl_status_t st;
    while ((st = WiFi.status()) != WL_CONNECTED && millis() - t0 < 15000) {
        delay(200);
        lv_timer_handler();
    }
    keypad_clear_chars();   /* drop keys pressed during the blocking connect */

    bool ok = (st == WL_CONNECTED);
    if (ok) {
        snprintf(wifi_status, sizeof(wifi_status), "OK IP: %s", WiFi.localIP().toString().c_str());
        Serial.printf("[WiFi] connected ip=%s\n", WiFi.localIP().toString().c_str());
        wifi_time_sync();   /* automatic NTP calibration after connect */
        char banner_buf[64];
        snprintf(banner_buf, sizeof(banner_buf), "Connected! IP: %s", WiFi.localIP().toString().c_str());
        wifi_banner_show(banner_buf);
    } else {
        const char *why = "Connect fail";
        switch (st) {
            case WL_NO_SSID_AVAIL:   why = "No SSID found";  break;
            case WL_CONNECT_FAILED:  why = "Connect failed"; break;
            case WL_CONNECTION_LOST: why = "Lost";           break;
            case WL_DISCONNECTED:    why = "Timeout";        break;
            default: break;
        }
        snprintf(wifi_status, sizeof(wifi_status), "%s (%d)", why, st);
        Serial.printf("[WiFi] connect failed st=%d (%s)\n", st, why);
        wifi_banner_show("Connect failed");
    }
    lv_label_set_text(wifi_status_lab, wifi_status);
    return ok;
}

/* Touch taps move LVGL focus between the two boxes independently of the
 * keypad (lv_indev.c::indev_click_focus sends LV_EVENT_FOCUSED on tap);
 * keep wifi_cfg_field in sync so keypad edits always target the box the
 * user sees the cursor in. The field guard also preserves uncommitted text:
 * tapping the already-active box must not reset the textarea. */
static void wifi_cfg_set_field(int f);

static void wifi_ssid_focus_cb(lv_event_t *e)
{
    if (wifi_cfg_field != 0) {
        wifi_cfg_set_field(0);
    }
}

static void wifi_pass_focus_cb(lv_event_t *e)
{
    if (wifi_cfg_field != 1) {
        wifi_cfg_set_field(1);
    }
}

/* Field switch (keypad or touch): sync the outgoing field's draft, move the
 * visible cursor (LV_EVENT_FOCUSED restarts the textarea cursor blink) so it
 * matches the ">" marker, and refresh labels only — the other box's content
 * is never rewritten (finding 2.4). */
static void wifi_cfg_set_field(int f)
{
    if (f != wifi_cfg_field) {
        wifi_cfg_sync_draft();
    }
    wifi_cfg_field = f;
    wifi_cfg_scan_mode = false;
    if (f != 0 && wifi_scan_state == WIFI_SCAN_RUNNING) {
        wifi_scan_gen++;        /* left the SSID field mid-scan: ignore its result (2.3) */
    }
    lv_event_send(f == 0 ? (lv_obj_t *)wifi_ssid_ta : (lv_obj_t *)wifi_pass_ta,
                  LV_EVENT_FOCUSED, NULL);
    wifi_cfg_refresh_labels();
}

void wifi_cfg_keyboard_poll()
{
    if (!wifi_cfg_kbd_active || !wifi_ssid_ta || !wifi_pass_ta) return;
    wifi_cfg_scan_poll();                       /* async scan result (runs every loop) */
    wifi_scan_overlay_update();                 /* countdown/hide of the scan overlay */
    wifi_banner_update();                       /* auto-hide of the result banner */

    /* burst processing (user feedback): drain the whole key backlog in
     * ONE poll pass so a typed run coalesces into a single EPD render
     * instead of one flush per character - the per-char dispatch below
     * is unchanged, it just runs in a loop. */
    for (int guard = 0; guard < 32; guard++) {
    char c;
    if (!keypad_get_val(&c)) break;
    keypad_set_flag();

    if (c == '\v') continue;                    /* volume key: reserved, no handler yet */

    if (wifi_cfg_field == 0) {
        if (wifi_scan_state == WIFI_SCAN_RUNNING) {
            break;                              /* scan in flight: ignore keys */
        }
        if (c == '\t') {
            /* Alt+Enter: independent scan entry, works with any box content
             * (review finding 1.2) */
            wifi_cfg_scan_start();
        } else if (wifi_cfg_scan_mode) {
            if (c == '+' || c == '-') {
                wifi_scan_idx = (wifi_scan_idx + (c == '+' ? 1 : wifi_scan_cnt - 1)) % wifi_scan_cnt;
                lv_textarea_set_text(wifi_ssid_ta, wifi_scan_ssids[wifi_scan_idx]);
            } else if (c == '\n') {
                /* pick the shown candidate (set_field syncs the draft) */
                wifi_cfg_set_field(1);
            } else if (c == '\b') {
                /* cancel the pick: restore the pre-scan box content */
                wifi_cfg_scan_mode = false;
                lv_textarea_set_text(wifi_ssid_ta, wifi_ssid_pre_scan);
            } else {
                /* start manual editing: all visible chars go to the box
                 * (review finding 1.1) */
                wifi_cfg_scan_mode = false;
                lv_textarea_add_char(wifi_ssid_ta, c);
            }
        } else {
            if (c == '\n') {
                const char *txt = lv_textarea_get_text(wifi_ssid_ta);
                if (txt && txt[0] != '\0') {
                    /* commit the box (set_field syncs the draft), goto password */
                    wifi_cfg_set_field(1);
                } else {
                    /* empty box: Enter also scans; Alt+Enter scans anytime */
                    wifi_cfg_scan_start();
                }
            } else if (c == '\b') {
                const char *txt = lv_textarea_get_text(wifi_ssid_ta);
                if (txt && txt[0] != '\0') {
                    lv_textarea_del_char(wifi_ssid_ta);
                } else {
                    wifi_cfg_kbd_active = false;
                    scr_mgr_pop(false);
                }
            } else {
                lv_textarea_add_char(wifi_ssid_ta, c);
            }
        }
    } else {
        /* password field */
        if (c == '\t') {
            return;                             /* Alt+Enter scan combo: not valid here */
        }
        if (c == '\n') {
            wifi_cfg_sync_draft();               /* pass box → wifi_pass */
            if (wifi_cfg_connect()) {
                wifi_cfg_save();                 /* persist only on success */
            }
            wifi_cfg_refresh_labels();
        } else if (c == '\b') {
            const char *txt = lv_textarea_get_text(wifi_pass_ta);
            if (txt && txt[0] != '\0') {
                lv_textarea_del_char(wifi_pass_ta);
            } else {
                wifi_cfg_set_field(0);
            }
        } else {
            lv_textarea_add_char(wifi_pass_ta, c);
        }
    }
    }                                           /* end burst loop */
}

static void scr4_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        wifi_cfg_kbd_active = false;
        scr_mgr_pop(false);
    }
}

/* Touch path for Connect: same as Enter on the password field — sync the
 * current draft, try to connect, and persist to NVS ONLY on success. */
static void wifi_connect_btn_cb(lv_event_t *e)
{
    wifi_cfg_sync_draft();
    if (wifi_cfg_connect()) {
        wifi_cfg_save();                         /* persist only on success */
    }
    wifi_cfg_refresh_labels();
}

/* Touch path for Clear: wipe both boxes, the draft buffers and the NVS
 * record (so auto-connect on boot no longer uses the old credentials). */
static void wifi_clear_btn_cb(lv_event_t *e)
{
    wifi_ssid[0] = '\0';
    wifi_pass[0] = '\0';
    lv_textarea_set_text(wifi_ssid_ta, "");
    lv_textarea_set_text(wifi_pass_ta, "");
    wifi_cfg_scan_mode = false;
    wifi_scan_gen++;                            /* invalidate any in-flight scan */
    wifi_cfg_save();
    snprintf(wifi_status, sizeof(wifi_status), "Cleared");
    wifi_cfg_set_field(0);
}

static void create4_1(lv_obj_t *parent)
{
    /* SCAN_DONE event signal for the abort path (review round 4 finding 1.2) */
    if (!s_scan_event_registered) {
        WiFi.onEvent(wifi_scan_done_cb, ARDUINO_EVENT_WIFI_SCAN_DONE);
        s_scan_event_registered = true;
    }

    scr_back_btn_create(parent, "Wifi Config", scr4_1_btn_event_cb);

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

    wifi_ssid_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(wifi_ssid_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_width(wifi_ssid_lab, lv_pct(100));

    wifi_ssid_ta = lv_textarea_create(cont);
    lv_obj_set_width(wifi_ssid_ta, lv_pct(100));
    lv_obj_set_height(wifi_ssid_ta, 34);
    lv_textarea_set_one_line(wifi_ssid_ta, true);
    lv_textarea_set_max_length(wifi_ssid_ta, 32);
    lv_textarea_set_placeholder_text(wifi_ssid_ta, "type SSID or Enter=scan");
    lv_obj_set_style_text_font(wifi_ssid_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    wifi_pass_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(wifi_pass_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_width(wifi_pass_lab, lv_pct(100));

    wifi_pass_ta = lv_textarea_create(cont);
    lv_obj_set_width(wifi_pass_ta, lv_pct(100));
    lv_obj_set_height(wifi_pass_ta, 34);
    lv_textarea_set_one_line(wifi_pass_ta, true);
    lv_textarea_set_max_length(wifi_pass_ta, 64);
    lv_textarea_set_placeholder_text(wifi_pass_ta, "password");
    lv_obj_set_style_text_font(wifi_pass_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* keep the keypad field state in sync with touch focus */
    lv_obj_add_event_cb(wifi_ssid_ta, wifi_ssid_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(wifi_pass_ta, wifi_pass_focus_cb, LV_EVENT_FOCUSED, NULL);

    wifi_status_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(wifi_status_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(wifi_status_lab, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_long_mode(wifi_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wifi_status_lab, lv_pct(100));

    lv_obj_t *hint = lv_label_create(cont);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(hint, "Enter:scan/next  +/-:pick\nAlt+Enter:scan  Backspace:del/back");

    /* Connect / Clear buttons (touch path; keyboard: Enter on pass = connect) */
    lv_obj_t *btn_row = lv_obj_create(cont);
    lv_obj_set_width(btn_row, lv_pct(100));
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 4, LV_PART_MAIN);

    lv_obj_t *connect_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(connect_btn, 1);
    lv_obj_set_height(connect_btn, 30);
    lv_obj_t *connect_lab = lv_label_create(connect_btn);
    lv_label_set_text(connect_lab, "Connect");
    lv_obj_center(connect_lab);
    lv_obj_add_event_cb(connect_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(clear_btn, 1);
    lv_obj_set_height(clear_btn, 30);
    lv_obj_t *clear_lab = lv_label_create(clear_btn);
    lv_label_set_text(clear_lab, "Clear");
    lv_obj_center(clear_lab);
    lv_obj_add_event_cb(clear_btn, wifi_clear_btn_cb, LV_EVENT_CLICKED, NULL);

    wifi_cfg_field = 0;
    wifi_cfg_scan_mode = false;
    wifi_scan_cnt = 0;
    wifi_scan_idx = 0;
    wifi_cfg_load();
    lv_textarea_set_text(wifi_ssid_ta, wifi_ssid);
    lv_textarea_set_text(wifi_pass_ta, wifi_pass);
    wifi_cfg_refresh_labels();
    wifi_cfg_kbd_active = true;
}

static void entry4_1(void) { ui_disp_full_refr(); }
static void exit4_1(void) {
    /* The scan overlay/banner live on lv_layer_top() and would outlive a
     * plain push (exit runs, destroy does not): hide them so they don't
     * sit on top of the pushed screen (review round 25 finding). */
    wifi_scan_overlay_hide();
    wifi_banner_hide();
    ui_disp_full_refr();
}
/* Abort an in-flight async scan (review round 4 finding 1.2): wait for the
 * explicit SCAN_DONE event (which guarantees the framework's _scanDone()
 * has finished allocating/filling the results) before scanDelete(). On
 * timeout the release is DEFERRED and new scans are blocked until the
 * event arrives (review finding 1.3: a follow-up scan must not run
 * scanDelete() while the late callback may still be filling results). */
static void wifi_cfg_scan_abort(void)
{
    if (wifi_scan_state != WIFI_SCAN_RUNNING) return;

    /* Copilot 1.3: publish the release target BEFORE stopping the scan,
     * then re-check the counter. An event landing between the previous
     * judgement and the publish either passes the re-check or arrives
     * after the publish and is cleared by the callback - the pending
     * state can no longer be wedged by an already-past event. */
    portENTER_CRITICAL(&s_scan_mux);
    s_scan_release_target = s_scan_done_cnt;
    s_scan_release_pending = true;
    if (s_scan_done_cnt > s_scan_release_target) {
        s_scan_release_pending = false;         /* event beat us to the publish */
    }
    portEXIT_CRITICAL(&s_scan_mux);

    esp_wifi_scan_stop();
    uint32_t t0 = millis();
    while (scan_release_is_pending() && millis() - t0 < 3000) {
        delay(1);
    }
    if (!scan_release_is_pending()) {
        WiFi.scanDelete();                      /* SCAN_DONE observed; safe to release */
    } else {
        Serial.println("[WiFi] scan abort timeout - release deferred");
    }
    wifi_scan_state = WIFI_SCAN_FAILED;
}

/* Scan progress overlay (user request): topmost message with a countdown.
 * Blocks keypad (poll guard) and touch (full-screen clickable layer) while
 * visible; hidden when the scan finishes or the countdown expires (expiry
 * also aborts a stuck scan). */
#define WIFI_SCAN_OVL_TIMEOUT_MS 10000
static lv_obj_t *wifi_scan_ovl = NULL;
static lv_obj_t *wifi_scan_ovl_lab = NULL;
static uint32_t wifi_scan_ovl_t0 = 0;
static uint32_t wifi_scan_ovl_last_secs = 0;
static uint32_t wifi_scan_ovl_flush_seq = 0;
static uint32_t wifi_scan_ovl_visible_t0 = 0;
static bool wifi_scan_ovl_frame_visible = false;

static void wifi_scan_overlay_show(void)
{
    if (wifi_scan_ovl) return;
    wifi_scan_ovl = lv_obj_create(lv_layer_top());
    lv_obj_set_size(wifi_scan_ovl, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(wifi_scan_ovl, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_scan_ovl, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wifi_scan_ovl, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wifi_scan_ovl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wifi_scan_ovl, LV_OBJ_FLAG_CLICKABLE);   /* swallow touches */

    wifi_scan_ovl_lab = lv_label_create(wifi_scan_ovl);
    lv_obj_set_style_bg_color(wifi_scan_ovl_lab, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(wifi_scan_ovl_lab, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wifi_scan_ovl_lab, 1, 0);
    lv_obj_set_style_border_color(wifi_scan_ovl_lab, lv_color_black(), 0);
    lv_obj_set_style_radius(wifi_scan_ovl_lab, 6, 0);
    lv_obj_set_style_pad_all(wifi_scan_ovl_lab, 8, 0);
    lv_obj_set_style_text_font(wifi_scan_ovl_lab, &lv_font_montserrat_14, 0);
    lv_obj_align(wifi_scan_ovl_lab, LV_ALIGN_TOP_MID, 0, 60);
    wifi_scan_ovl_t0 = millis();
    wifi_scan_ovl_last_secs = 0;
    lv_label_set_text(wifi_scan_ovl_lab, "Scanning... 10s");
    /* Bind the minimum-display timer to the frame that actually reaches
     * the EPD. Object lifetime is not panel-visible time on e-paper. */
    wifi_scan_ovl_frame_visible = false;
    wifi_scan_ovl_visible_t0 = 0;
    wifi_scan_ovl_flush_seq = ui_disp_full_refr_seq();
}

static void wifi_scan_overlay_hide(void)
{
    if (wifi_scan_ovl) {
        lv_obj_del(wifi_scan_ovl);
        wifi_scan_ovl = NULL;
        wifi_scan_ovl_lab = NULL;
    }
    wifi_scan_ovl_frame_visible = false;
    wifi_scan_ovl_visible_t0 = 0;
    wifi_scan_ovl_flush_seq = 0;
}

/* Called every loop while the screen is active. */
#define WIFI_SCAN_OVL_MIN_MS 800
static void wifi_scan_overlay_update(void)
{
    if (!wifi_scan_ovl) return;

    if (!wifi_scan_ovl_frame_visible &&
        ui_disp_flush_done_seq() >= wifi_scan_ovl_flush_seq) {
        wifi_scan_ovl_frame_visible = true;
        wifi_scan_ovl_visible_t0 = millis();
        Serial.println("[WiFi] scan overlay reached panel");
    }

    if (wifi_scan_state != WIFI_SCAN_RUNNING &&
        wifi_scan_ovl_frame_visible &&
        millis() - wifi_scan_ovl_visible_t0 >= WIFI_SCAN_OVL_MIN_MS) {
        wifi_scan_overlay_hide();
        return;
    }

    /* A result banner may be created as soon as scanComplete() returns.
     * Keep the progress layer above it until its own visible interval ends. */
    lv_obj_move_foreground(wifi_scan_ovl);

    uint32_t elapsed = millis() - wifi_scan_ovl_t0;
    if (elapsed >= WIFI_SCAN_OVL_TIMEOUT_MS) {
        wifi_cfg_scan_abort();                  /* countdown over: abort stuck scan */
        wifi_scan_overlay_hide();
        wifi_banner_show("Scan timeout");
        return;
    }
    uint32_t secs = (WIFI_SCAN_OVL_TIMEOUT_MS - elapsed + 999) / 1000;
    if (secs != wifi_scan_ovl_last_secs) {
        wifi_scan_ovl_last_secs = secs;
        lv_label_set_text_fmt(wifi_scan_ovl_lab, "Scanning... %lus", (unsigned)secs);
    }
}

/* Result banner (user request): topmost, non-blocking message that
 * auto-hides. Used to make scan/connect outcomes unmistakable. */
#define WIFI_BANNER_MS 3000
static lv_obj_t *wifi_banner_lab = NULL;
static uint32_t wifi_banner_t0 = 0;

static void wifi_banner_show(const char *text)
{
    if (!wifi_banner_lab) {
        wifi_banner_lab = lv_label_create(lv_layer_top());
        lv_obj_set_style_bg_color(wifi_banner_lab, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(wifi_banner_lab, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(wifi_banner_lab, 1, 0);
        lv_obj_set_style_border_color(wifi_banner_lab, lv_color_black(), 0);
        lv_obj_set_style_radius(wifi_banner_lab, 6, 0);
        lv_obj_set_style_pad_all(wifi_banner_lab, 8, 0);
        lv_obj_set_style_text_font(wifi_banner_lab, &lv_font_montserrat_14, 0);
        lv_obj_align(wifi_banner_lab, LV_ALIGN_TOP_MID, 0, 60);
    }
    lv_label_set_text(wifi_banner_lab, text);
    lv_obj_move_foreground(wifi_banner_lab);
    wifi_banner_t0 = millis();
}

static void wifi_banner_hide(void)
{
    if (wifi_banner_lab) {
        lv_obj_del(wifi_banner_lab);
        wifi_banner_lab = NULL;
    }
}

/* Called every loop while the screen is active. */
static void wifi_banner_update(void)
{
    if (wifi_banner_lab && millis() - wifi_banner_t0 >= WIFI_BANNER_MS) {
        wifi_banner_hide();
    }
}

static void destroy4_1(void)
{
    wifi_cfg_kbd_active = false;
    wifi_cfg_scan_mode = false;
    wifi_scan_gen++;                            /* invalidate any in-flight result application */
    wifi_scan_overlay_hide();
    wifi_banner_hide();
    wifi_cfg_scan_abort();
}

static scr_lifecycle_t screen4_1 = {
    .create = create4_1,
    .entry = entry4_1,
    .exit  = exit4_1,
    .destroy = destroy4_1,
};
#endif
// --------------------- screen 4.2 --------------------- Wifi Scan
#if 1
static lv_obj_t *scr4_2_cont;
static lv_obj_t *wifi_scan_lab;
static lv_timer_t *wifi_scan_timer = NULL;

static ui_wifi_scan_info_t wifi_info_list[UI_WIFI_SCAN_ITEM_MAX];

static void scr4_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void show_wifi_scan(void)
{
#define BUFF_LEN 400
    char buf[BUFF_LEN];
    int ret = 0, offs = 0;

    ret = lv_snprintf(buf + offs, BUFF_LEN, "       NAME      | RSSI\n");
    offs = offs + ret;
    ret = lv_snprintf(buf + offs, BUFF_LEN, "-----------------------\n");
    offs = offs + ret;

    for(int i = 0; i < UI_WIFI_SCAN_ITEM_MAX; i++) {
        if(strcmp(wifi_info_list[i].name, "") == 0 && wifi_info_list[i].rssi == 0)
        {
            break;
        }
        if(i == UI_WIFI_SCAN_ITEM_MAX - 1) {
            ret = lv_snprintf(buf + offs, BUFF_LEN, "%-16.16s | %4d", wifi_info_list[i].name, wifi_info_list[i].rssi);
            break;
        }

        ret = lv_snprintf(buf + offs, BUFF_LEN, "%-16.16s | %4d\n", wifi_info_list[i].name, wifi_info_list[i].rssi);
        offs = offs + ret;
    }
    lv_label_set_text(wifi_scan_lab, buf);
#undef BUFF_LEN
}

static void wifi_scan_timer_event(lv_timer_t *t)
{
    ui_wifi_get_scan_info(wifi_info_list, UI_WIFI_SCAN_ITEM_MAX);
    show_wifi_scan();
}

static void create4_2(lv_obj_t *parent) 
{
    scr4_2_cont = lv_obj_create(parent);
    lv_obj_set_size(scr4_2_cont, lv_pct(100), lv_pct(90));
    lv_obj_set_style_bg_color(scr4_2_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr4_2_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr4_2_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr4_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr4_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(scr4_2_cont, 13, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr4_2_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr4_2_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr4_2_cont, 5, LV_PART_MAIN);
    lv_obj_set_align(scr4_2_cont, LV_ALIGN_BOTTOM_MID);

    wifi_scan_lab = lv_label_create(scr4_2_cont);
    lv_obj_set_width(wifi_scan_lab, lv_pct(95));
    lv_obj_set_style_pad_all(wifi_scan_lab, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_scan_lab, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    lv_obj_set_style_border_width(wifi_scan_lab, 0, LV_PART_MAIN);
    lv_label_set_long_mode(wifi_scan_lab, LV_LABEL_LONG_WRAP);

    lv_obj_t *back4_label = scr_back_btn_create(parent, ("Wifi"), scr4_2_btn_event_cb);
}
static void entry4_2(void) 
{
    ui_disp_full_refr();
    wifi_scan_timer = lv_timer_create(wifi_scan_timer_event, 10000, NULL);
    lv_timer_ready(wifi_scan_timer);
}
static void exit4_2(void) {
    ui_disp_full_refr();
    if(wifi_scan_timer) {
        lv_timer_del(wifi_scan_timer);
        wifi_scan_timer = NULL;
    }
}

static void destroy4_2(void) { }

static scr_lifecycle_t screen4_2 = {
    .create = create4_2,
    .entry = entry4_2,
    .exit  = exit4_2,
    .destroy = destroy4_2,
};
#endif
//************************************[ screen 5 ]****************************************** Test
#if 1
static lv_obj_t *test_list;
static lv_obj_t *test_page;
static int test_num = 0;
static int test_page_num = 0;
static int test_curr_page = 0;

#define TEST_ITEM(_name, _peri) \
    { .name = _name, .peri_id = _peri, .sub_id = -1, .cb = ui_test_get }

#define TEST_SUB_ITEM(_name, _peri, _sub) \
    { .name = _name, .peri_id = _peri, .sub_id = _sub, .cb = ui_test_get }

static ui_test_handle test_handle_list[] = {
    TEST_ITEM("Lora", E_PERI_LORA),
    TEST_ITEM("Touch", E_PERI_TOUCH),
    TEST_ITEM("BQ25896", E_PERI_BQ25896),
    TEST_ITEM("BQ27220", E_PERI_BQ27220),
    TEST_SUB_ITEM("SD Card", E_PERI_SD, SCREEN5_1_ID),
    TEST_ITEM("A7682E", E_PERI_A7682E),
    TEST_ITEM("PCM5102A", E_PERI_PCM5102A),
    TEST_ITEM("Keypad", E_PERI_KYEPAD),
    TEST_ITEM("GPS", E_PERI_GPS),
    TEST_ITEM("BHI260AP", E_PERI_BHI260AP),
    TEST_ITEM("INK_SCREEN", E_PERI_INK_SCREEN),
};

static void test_item_create(int curr_apge);

static void test_scr_event(lv_event_t *e)
{
    ui_test_handle *h = (ui_test_handle *)e->user_data;

    if ((e->code == LV_EVENT_CLICKED) && h && (h->sub_id >= 0)) {
        scr_mgr_push(h->sub_id, false);
    }
}

static void test_refresh_page(void)
{
    if (test_list == NULL) {
        return;
    }

    while (lv_obj_get_child_cnt(test_list) > 0) {
        lv_obj_t *child = lv_obj_get_child(test_list, 0);
        if (child) {
            lv_obj_del(child);
        }
    }

    test_item_create(test_curr_page);
    if (test_page) {
        lv_label_set_text_fmt(test_page, "%d / %d", test_curr_page, test_page_num);
    }
}

static void scr5_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void test_page_switch_cb(lv_event_t *e)
{
    char opt = (int)e->user_data;
    
    if(test_num < SETTING_PAGE_MAX_ITEM) return;

    int child_cnt = lv_obj_get_child_cnt(test_list);
    
    for(int i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(test_list, 0);
        if(child)
            lv_obj_del(child);
    }

    if(opt == 'p')
    {
        test_curr_page = (test_curr_page < test_page_num) ? test_curr_page + 1 : 0;
    }
    else if(opt == 'n')
    {
        test_curr_page = (test_curr_page > 0) ? test_curr_page - 1 : test_page_num;
    }

    test_refresh_page();
}

static void test_item_create(int curr_apge)
{
    printf("test_curr_page = %d\n", test_curr_page);
    int start = (curr_apge * SETTING_PAGE_MAX_ITEM);
    int end = start + SETTING_PAGE_MAX_ITEM;
    if(end > test_num) end = test_num;

    printf("start=%d, end=%d\n", start, end);

    for(int i = start; i < end; i++) {
        ui_test_handle *h = &test_handle_list[i];
        h->obj = lv_list_add_btn(test_list, NULL, h->name);
        h->st = lv_label_create(h->obj);
        lv_obj_set_style_text_font(h->st, FONT_BOLD_SIZE_15, LV_PART_MAIN);
        lv_obj_align(h->st, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_label_set_text_fmt(h->st, "%s", (h->cb(h->peri_id) ? "PASS" : "----"));
        // style
        lv_obj_set_style_text_font(h->obj, FONT_BOLD_SIZE_15, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h->obj, DECKPRO_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_text_color(h->obj, DECKPRO_COLOR_FG, LV_PART_MAIN);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(h->obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(h->obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(h->obj, test_scr_event, LV_EVENT_CLICKED, (void *)h);
    }
}

static void create5(lv_obj_t *parent) 
{
    test_list = lv_list_create(parent);
    lv_obj_set_size(test_list, LV_HOR_RES, lv_pct(88));
    lv_obj_align(test_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(test_list, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_top(test_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(test_list, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(test_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(test_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(test_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(test_list, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(test_list, 0, LV_PART_MAIN);

    test_num = sizeof(test_handle_list) / sizeof(test_handle_list[0]);
    test_page_num = test_num / SETTING_PAGE_MAX_ITEM;
    test_refresh_page();

    lv_obj_t * ui_Button2 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button2, 71);
    lv_obj_set_height(ui_Button2, 40);
    lv_obj_set_x(ui_Button2, -70);
    lv_obj_set_y(ui_Button2, 130);
    lv_obj_set_align(ui_Button2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label1 = lv_label_create(ui_Button2);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "Back");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Button14 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button14, 71);
    lv_obj_set_height(ui_Button14, 40);
    lv_obj_set_x(ui_Button14, 70);
    lv_obj_set_y(ui_Button14, 130);
    lv_obj_set_align(ui_Button14, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button14, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button14, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button14, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button14, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label15 = lv_label_create(ui_Button14);
    lv_obj_set_width(ui_Label15, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label15, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label15, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label15, "Next");
    lv_obj_set_style_text_color(ui_Label15, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Button2, test_page_switch_cb, LV_EVENT_CLICKED, (void*)'n');
    lv_obj_add_event_cb(ui_Button14, test_page_switch_cb, LV_EVENT_CLICKED, (void*)'p');

    test_page = lv_label_create(parent);
    lv_obj_set_width(test_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(test_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(test_page, LV_ALIGN_BOTTOM_MID, 0, -23);
    lv_label_set_text_fmt(test_page, "%d / %d", test_curr_page, test_page_num);
    lv_obj_set_style_text_color(test_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(test_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back5_label = scr_back_btn_create(parent, ("Test"), scr5_btn_event_cb);
}
static void entry5(void) 
{
    test_refresh_page();
    ui_disp_full_refr();
}
static void exit5(void) {
    ui_disp_full_refr();
}
static void destroy5(void) {
    test_list = NULL;
    test_page = NULL;
}

static scr_lifecycle_t screen5 = {
    .create = create5,
    .entry = entry5,
    .exit  = exit5,
    .destroy = destroy5,
};
#endif
//************************************[ screen 6 ]****************************************** Battery
// --------------------- screen 6 --------------------- Battery
#if 1
lv_obj_t * scr6_list;
static lv_obj_t *scr6_lab_buf[20];

static void scr6_list_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    for(int i = 0; i < lv_obj_get_child_cnt(obj); i++) 
    {
        lv_obj_t * child = lv_obj_get_child(obj, i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            char *str = lv_label_get_text(child);

            if(strcmp("- BQ25896", str) == 0)
            {
                scr_mgr_push(SCREEN6_1_ID, false);
            }
            if(strcmp("- BQ27220", str) == 0)
            {
                scr_mgr_push(SCREEN6_2_ID, false);
            }
            printf("%s\n", str);
        }
    }
}

static void scr6_item_create(const char *name, lv_event_cb_t cb)
{
    lv_obj_t * obj = lv_obj_class_create_obj(&lv_list_btn_class, scr6_list);
    lv_obj_class_init_obj(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_set_height(obj, LV_VER_RES / 6);
    lv_obj_set_style_text_font(obj, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    // lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); 
}

static void scr6_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create6(lv_obj_t *parent) 
{
    scr6_list = lv_list_create(parent);
    lv_obj_set_size(scr6_list, lv_pct(93), lv_pct(91));
    lv_obj_align(scr6_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_obj_set_style_bg_color(scr6_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(scr6_list, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr6_list, 15, LV_PART_MAIN);
    lv_obj_set_style_radius(scr6_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(scr6_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr6_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_border_color(scr6_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(scr6_list, 0, LV_PART_MAIN);

    scr6_item_create("- BQ25896", scr6_list_event);
    scr6_item_create("- BQ27220", scr6_list_event);

    // back
    scr_back_btn_create(parent, "Battery", scr6_btn_event_cb);
}

static void entry6(void) 
{
    ui_disp_full_refr();
}
static void exit6(void) {
    ui_disp_full_refr();
}
static void destroy6(void) { }

static scr_lifecycle_t screen6 = {
    .create = create6,
    .entry = entry6,
    .exit  = exit6,
    .destroy = destroy6,
};
#endif
// --------------------- screen 6.1 --------------------- BQ25896
#if 1
#define line_max 23

static lv_timer_t *batt_6_1_timer = NULL;

static void battery_set_line(lv_obj_t *label, const char *str1, const char *str2)
{
    int w2 = strlen(str2);
    int w1 = line_max - w2;
    lv_label_set_text_fmt(label, "%-*s%-*s", w1, str1, w2, str2);
}

static lv_obj_t * scr6_1_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(90));
    lv_obj_set_style_text_font(label, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    return label;
}

static void scr6_1_battert_updata(void)
{
    char buf[line_max];

    battery_set_line(label_list[0], "Charging:", (ui_batt_25896_is_chg() == true ? "Charging" : "Not charged"));

    lv_snprintf(buf, line_max, "%.2fV", ui_batt_25896_get_vbus());
    battery_set_line(label_list[1], "VBUS:", buf);

    lv_snprintf(buf, line_max, "%.2fV", ui_batt_25896_get_vsys());
    battery_set_line(label_list[2], "VSYS:", buf);

    lv_snprintf(buf, line_max, "%.2fV", ui_batt_25896_get_vbat());
    battery_set_line(label_list[3], "VBAT:", buf);

    lv_snprintf(buf, line_max, "%.2fv", ui_batt_25896_get_volt_targ());
    battery_set_line(label_list[4], "VOLT Target:", buf);

    lv_snprintf(buf, line_max, "%.2fmA", ui_batt_25896_get_chg_curr());
    battery_set_line(label_list[5], "Charge Curr:", buf);

    lv_snprintf(buf, line_max, "%.2fmA", ui_batt_25896_get_pre_curr());
    battery_set_line(label_list[6], "Prechg Curr:", buf);

    lv_snprintf(buf, line_max, "%s", ui_batt_25896_get_chg_st());
    battery_set_line(label_list[7], "CHG ST:", buf);

    lv_snprintf(buf, line_max, "%s", ui_batt_25896_get_vbus_st());
    battery_set_line(label_list[8], "VBUS Status:", buf);

    lv_snprintf(buf, line_max, "%s", ui_batt_25896_get_ntc_st());
    battery_set_line(label_list[9], " ", buf);
}

static void batt_6_1_updata_timer_event(lv_timer_t *t) 
{
    scr6_1_battert_updata();
}

static void scr6_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create6_1(lv_obj_t *parent) 
{
    lv_obj_t *scr6_1_cont = lv_obj_create(parent);
    lv_obj_set_size(scr6_1_cont, lv_pct(100), lv_pct(88));
    lv_obj_set_style_bg_color(scr6_1_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr6_1_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr6_1_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr6_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr6_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr6_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr6_1_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr6_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_align(scr6_1_cont, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_flex_flow(scr6_1_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr6_1_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    for(int i = 0; i < sizeof(label_list) / sizeof(label_list[0]); i++) {
        label_list[i] = scr6_1_create_label(scr6_1_cont);
    }

    scr_back_btn_create(parent, ("BQ25896"), scr6_1_btn_event_cb);
}
static void entry6_1(void) 
{
    scr6_1_battert_updata();
    ui_disp_full_refr();
    batt_6_1_timer = lv_timer_create(batt_6_1_updata_timer_event, 5000, NULL);
}
static void exit6_1(void) {
    if(batt_6_1_timer) {
        lv_timer_del(batt_6_1_timer);
        batt_6_1_timer = NULL;
    }
    ui_disp_full_refr();
}
static void destroy6_1(void) { }

static scr_lifecycle_t screen6_1 = {
    .create = create6_1,
    .entry = entry6_1,
    .exit  = exit6_1,
    .destroy = destroy6_1,
};
#undef line_max

#endif
// --------------------- screen 6.2 --------------------- BQ27220
#if 1

#define line_max 23

static lv_timer_t *batt_6_2_timer = NULL;

static lv_obj_t * scr6_2_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(90));
    lv_obj_set_style_text_font(label, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    return label;
}

static void scr6_2_battert_updata(void)
{
    char buf[line_max];

    battery_set_line(label_list[0],"VBUS ST::", (ui_battery_27220_get_input() == true ? "Connected" : "Disonnected"));

    if(ui_battery_27220_get_input() == true ){
        lv_snprintf(buf, line_max, "%s", (ui_battery_27220_get_charge_finish()? "Finsish":"Charging"));
    } else {
        lv_snprintf(buf, line_max, "%s", "Discharge");
    }
    battery_set_line(label_list[1],"Charing ST:", buf);

    lv_snprintf(buf, line_max, "0x%x", ui_battery_27220_get_status());
    battery_set_line(label_list[2],"Battery ST:", buf);

    lv_snprintf(buf, line_max, "%dmV", ui_battery_27220_get_voltage());
    battery_set_line(label_list[3], "Voltage:", buf);

    lv_snprintf(buf, line_max, "%dmA", ui_battery_27220_get_current());
    battery_set_line(label_list[4], "Current:", buf);

    lv_snprintf(buf, line_max, "%.2fC", (float)(ui_battery_27220_get_temperature() / 10.0 - 273.0));
    battery_set_line(label_list[5], "Temperature:", buf);

    lv_snprintf(buf, line_max, "%dmAh", ui_battery_27220_get_remain_capacity());
    battery_set_line(label_list[6], "Cap Remain:", buf);

    lv_snprintf(buf, line_max, "%dmAh", ui_battery_27220_get_full_capacity());
    battery_set_line(label_list[7], "Cap Full:", buf);

    lv_snprintf(buf, line_max, "%d%%", ui_battery_27220_get_percent());
    battery_set_line(label_list[8], "Cap Percent:", buf);

    lv_snprintf(buf, line_max, "%d%%", ui_battery_27220_get_health());
    battery_set_line(label_list[9], "CapHealth:", buf);
}

static void batt_6_2_updata_timer_event(lv_timer_t *t) 
{
    scr6_2_battert_updata();
}

static void scr6_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create6_2(lv_obj_t *parent) 
{   
    lv_obj_t *scr6_2_cont = lv_obj_create(parent);
    lv_obj_set_size(scr6_2_cont, lv_pct(100), lv_pct(88));
    lv_obj_set_style_bg_color(scr6_2_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr6_2_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr6_2_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr6_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr6_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr6_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr6_2_cont, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr6_2_cont, 0, LV_PART_MAIN);
    lv_obj_set_align(scr6_2_cont, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_flex_flow(scr6_2_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr6_2_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    for(int i = 0; i < sizeof(label_list) / sizeof(label_list[0]); i++) {
        label_list[i] = scr6_2_create_label(scr6_2_cont);
    }
    // back
    scr_back_btn_create(parent, ("BQ27220"), scr6_btn_event_cb);
}

static void entry6_2(void) 
{
    scr6_2_battert_updata();
    ui_disp_full_refr();
    batt_6_2_timer = lv_timer_create(batt_6_2_updata_timer_event, 5000, NULL);
}
static void exit6_2(void) {
    if(batt_6_2_timer) {
        lv_timer_del(batt_6_2_timer);
        batt_6_2_timer = NULL;
    }
    ui_disp_full_refr();
}

static void destroy6_2(void) { }

static scr_lifecycle_t screen6_2 = {
    .create = create6_2,
    .entry = entry6_2,
    .exit  = exit6_2,
    .destroy = destroy6_2,
};
#undef line_max
#endif
//************************************[ screen 7 ]****************************************** Other
#if 1
static lv_obj_t *scr7_cont;
static lv_obj_t *input_touch;
static lv_obj_t *input_keypad;
static lv_obj_t *gyroscope;
static lv_timer_t *input_timer;
static String keypad_str = "keeypad:\n";

static void scr7_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void input_timer_event(lv_timer_t *t)
{
    int touch_x, touch_y;
    static int sec = 0;
    float gyro_x, gyro_y, gyro_z;
    char keypay_v;

    int ret = ui_input_get_touch_coord(&touch_x, &touch_y);

    if(ret > 0)
    {
        lv_label_set_text_fmt(input_touch,  "Touch: x: %03d | y: %03d", touch_x, touch_y);

        sec = 0;
    }

    ret = ui_input_get_keypay_val(&keypay_v);
    if(ret > 0)
    {
        ui_input_set_keypay_flag();
        keypad_str = keypad_str + String(keypay_v);
        lv_label_set_text_fmt(input_keypad, "%s", keypad_str.c_str());

        sec = 0;
    }

    sec++;
    if(sec > 60) // 2s
    {
        sec = 0;

        ui_other_get_gyro(&gyro_x, &gyro_y, &gyro_z);
        lv_label_set_text_fmt(gyroscope,    "   gyros_x: %.3f\n"
                                            "   gyros_y: %.3f\n"
                                            "   gyros_z: %.3f", gyro_x, gyro_y, gyro_z);
    }
}

static void create7(lv_obj_t *parent) 
{
    scr7_cont = lv_obj_create(parent);
    lv_obj_set_size(scr7_cont, lv_pct(100), lv_pct(88));
    lv_obj_set_style_bg_color(scr7_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr7_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr7_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr7_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr7_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(scr7_cont, 13, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr7_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr7_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr7_cont, 5, LV_PART_MAIN);
    lv_obj_set_align(scr7_cont, LV_ALIGN_BOTTOM_MID);

    input_touch = lv_label_create(scr7_cont);
    // lv_obj_set_height(input_touch, 90);
    lv_obj_set_width(input_touch, lv_pct(95));
    lv_obj_set_style_pad_all(input_touch, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(input_touch, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_border_width(input_touch, 1, LV_PART_MAIN);
    lv_label_set_long_mode(input_touch, LV_LABEL_LONG_WRAP);
    lv_label_set_text(input_touch,  "Touch: x:     | y:    ");

    input_keypad = lv_label_create(scr7_cont);
    // lv_obj_set_height(input_keypad, 100);
    lv_obj_set_width(input_keypad, lv_pct(95));
    lv_obj_set_style_pad_all(input_keypad, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(input_keypad, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_border_width(input_keypad, 1, LV_PART_MAIN);
    lv_label_set_long_mode(input_keypad, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(input_keypad, "Keypad: ");

    lv_obj_t *lab2 = lv_label_create(scr7_cont);
    lv_obj_set_style_text_font(lab2, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    lv_label_set_text(lab2, "gyroscope");

    gyroscope = lv_label_create(scr7_cont);
    // lv_obj_set_height(input_keypad, 100);
    lv_obj_set_width(gyroscope, lv_pct(95));
    lv_obj_set_style_pad_all(gyroscope, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(gyroscope, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    // lv_obj_set_style_border_width(gyroscope, 1, LV_PART_MAIN);
    lv_label_set_long_mode(gyroscope, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(gyroscope,    "   gyros_x: 000\n"
                                        "   gyros_y: 000\n"
                                        "   gyros_z: 000");

    lv_obj_t *back7_label = scr_back_btn_create(parent, ("Other"), scr7_btn_event_cb);
}
static void entry7(void) 
{
    keypad_str = "keeypad:\n";
    ui_disp_full_refr();
    input_timer = lv_timer_create(input_timer_event, 50, NULL);
}
static void exit7(void) {
    if(input_timer)
    {
        lv_timer_del(input_timer);
        input_timer = NULL;
    }
    ui_disp_full_refr();
}
static void destroy7(void) { }

static scr_lifecycle_t screen7 = {
    .create = create7,
    .entry = entry7,
    .exit  = exit7,
    .destroy = destroy7,
};
#endif
//************************************[ screen 8 ]****************************************** A7682E
// --------------------- screen 8 --------------------- A7682E
#if 1
static lv_obj_t *a7682_list;
static lv_obj_t *a7682_page;
static int a7682_num = 0;
static int a7682_page_num = 0;
static int a7682_curr_page = 0;

bool ui_a7682_call_test(const char *param)
{
    scr_mgr_push(SCREEN8_1_ID, false);
    return true;
}

bool ui_a7682_at_test(const char *param)
{
    scr_mgr_push(SCREEN8_2_ID, false);
    return true;
}

static ui_a7682_handle a7682_handle_list[] = 
{
    {"A7682 Audio", NULL, NULL, ui_a7682_at_cb},
    {"Call test", NULL, NULL, ui_a7682_call_test},
    {"AT test", NULL, NULL, ui_a7682_at_test},
};

static void a7682_item_create(int curr_apge);

static void a7682_scr_event(lv_event_t *e)
{
    lv_obj_t *tgt = (lv_obj_t *)e->target;
    ui_a7682_handle *h = (ui_a7682_handle *)e->user_data;

    if(e->code == LV_EVENT_CLICKED) {
        if(h->cb)
            h->cb(h->name);
    }
}

static void a7682_item_create(int curr_apge)
{
    printf("a7682_curr_page = %d\n", a7682_curr_page);
    int start = (curr_apge * SETTING_PAGE_MAX_ITEM);
    int end = start + SETTING_PAGE_MAX_ITEM;
    if(end > a7682_num) end = a7682_num;

    printf("start=%d, end=%d\n", start, end);

    for(int i = start; i < end; i++) {
        ui_a7682_handle *h = &a7682_handle_list[i];
        h->obj = lv_list_add_btn(a7682_list, NULL, h->name);
        lv_obj_set_height(h->obj, 28);
        // h->st = lv_label_create(h->obj);
        // lv_obj_set_style_text_font(h->st, FONT_BOLD_SIZE_15, LV_PART_MAIN);
        // lv_obj_align(h->st, LV_ALIGN_RIGHT_MID, 0, 0);
        // lv_label_set_text_fmt(h->st, "%s", (h->get_cb() ? "ON" : "OFF"));
        // style
        lv_obj_set_style_text_font(h->obj, FONT_BOLD_SIZE_14, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h->obj, DECKPRO_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_text_color(h->obj, DECKPRO_COLOR_FG, LV_PART_MAIN);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(h->obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(h->obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(h->obj, a7682_scr_event, LV_EVENT_CLICKED, (void *)h);
    }
}

static void a7682_page_switch_cb(lv_event_t *e)
{
    char opt = (int)e->user_data;
    
    if(a7682_num < SETTING_PAGE_MAX_ITEM) return;

    int child_cnt = lv_obj_get_child_cnt(a7682_list);
    
    for(int i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(a7682_list, 0);
        if(child)
            lv_obj_del(child);
    }

    if(opt == 'p')
    {
        a7682_curr_page = (a7682_curr_page < a7682_page_num) ? a7682_curr_page + 1 : 0;
    }
    else if(opt == 'n')
    {
        a7682_curr_page = (a7682_curr_page > 0) ? a7682_curr_page - 1 : a7682_page_num;
    }

    a7682_item_create(a7682_curr_page);
    lv_label_set_text_fmt(a7682_page, "%d / %d", a7682_curr_page, a7682_page_num);
}

static void scr8_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create8(lv_obj_t *parent) 
{
    a7682_list = lv_list_create(parent);
    lv_obj_set_size(a7682_list, LV_HOR_RES, lv_pct(88));
    lv_obj_align(a7682_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(a7682_list, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_top(a7682_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(a7682_list, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(a7682_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(a7682_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(a7682_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(a7682_list, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(a7682_list, 0, LV_PART_MAIN);

    a7682_num = sizeof(a7682_handle_list) / sizeof(a7682_handle_list[0]);
    a7682_page_num = a7682_num / SETTING_PAGE_MAX_ITEM;
    a7682_item_create(a7682_curr_page);

    lv_obj_t * ui_Button2 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button2, 71);
    lv_obj_set_height(ui_Button2, 40);
    lv_obj_set_x(ui_Button2, -70);
    lv_obj_set_y(ui_Button2, 130);
    lv_obj_set_align(ui_Button2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label1 = lv_label_create(ui_Button2);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "Back");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Button14 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button14, 71);
    lv_obj_set_height(ui_Button14, 40);
    lv_obj_set_x(ui_Button14, 70);
    lv_obj_set_y(ui_Button14, 130);
    lv_obj_set_align(ui_Button14, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button14, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button14, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button14, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button14, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label15 = lv_label_create(ui_Button14);
    lv_obj_set_width(ui_Label15, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label15, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label15, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label15, "Next");
    lv_obj_set_style_text_color(ui_Label15, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Button2, a7682_page_switch_cb, LV_EVENT_CLICKED, (void*)'n');
    lv_obj_add_event_cb(ui_Button14, a7682_page_switch_cb, LV_EVENT_CLICKED, (void*)'p');

    a7682_page = lv_label_create(parent);
    lv_obj_set_width(a7682_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(a7682_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(a7682_page, LV_ALIGN_BOTTOM_MID, 0, -23);
    lv_label_set_text_fmt(a7682_page, "%d / %d", a7682_curr_page, a7682_page_num);
    lv_obj_set_style_text_color(a7682_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(a7682_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back8_label = scr_back_btn_create(parent, ("A7682E"), scr8_btn_event_cb);
}
static void entry8(void) 
{
    ui_disp_full_refr();
}
static void exit8(void) {
    ui_disp_full_refr();
}
static void destroy8(void) { }

static scr_lifecycle_t screen8 = {
    .create = create8,
    .entry = entry8,
    .exit  = exit8,
    .destroy = destroy8,
};
#endif
// --------------------- screen 8.1 --------------------- Call test
#if 1
static void event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t * ta =  (lv_obj_t *)lv_event_get_user_data(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        uint32_t id = lv_btnmatrix_get_selected_btn(obj);
        const char * txt = lv_btnmatrix_get_btn_text(obj, id);
        int len = strlen(txt);
 
        if(!strcmp(txt, LV_SYMBOL_CALL)) {
            ui_a7682_call(lv_textarea_get_text(ta));
        } else if(!strcmp(txt, "Hang up"))
        {
            ui_a7682_hang_up();
        } else if(!strcmp(txt, LV_SYMBOL_BACKSPACE))
        {
            lv_textarea_del_char(ta);
        }else{
            lv_textarea_add_text(ta, txt);
        }
    }
}

static const char * btnm_map[] = {  "1", "2", "3", "\n",
                                    "4", "5", "6", "\n",
                                    "7", "8", "9", "\n",
                                    "*", "0", "#", "\n",
                                    LV_SYMBOL_CALL, "Hang up", LV_SYMBOL_BACKSPACE,""
                                 };


static void scr8_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create8_1(lv_obj_t *parent) 
{
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, lv_pct(98));
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, lv_pct(20));
    lv_obj_set_style_text_font(ta, &Font_Mono_Bold_20, LV_PART_MAIN);
    // lv_obj_add_state(ta, LV_STATE_FOCUSED); /*To be sure the cursor is visible*/
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_letter_space(ta, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ta, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btnm1 = lv_btnmatrix_create(parent);
    lv_btnmatrix_set_map(btnm1, btnm_map);
    lv_obj_set_size(btnm1, lv_pct(100)-2, lv_pct(60));
    lv_obj_set_style_border_width(btnm1, 0, 0);
    // lv_btnmatrix_set_btn_width(btnm1, 10, 2);        /*Make "Action1" twice as wide as "Action2"*/
    // lv_btnmatrix_set_btn_ctrl(btnm1, 10, LV_BTNMATRIX_CTRL_CHECKABLE);
    // lv_btnmatrix_set_btn_ctrl(btnm1, 11, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_align(btnm1, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(btnm1, event_handler, LV_EVENT_VALUE_CHANGED, ta);
    
    lv_obj_t *back8_1_label = scr_back_btn_create(parent, ("Call"), scr8_1_btn_event_cb);
}
static void entry8_1(void) 
{
    ui_a7682_loop_resume();
    ui_disp_full_refr();
}
static void exit8_1(void) {
    ui_a7682_loop_suspend();
    ui_disp_full_refr();
}
static void destroy8_1(void) { }

static scr_lifecycle_t screen8_1 = {
    .create = create8_1,
    .entry = entry8_1,
    .exit  = exit8_1,
    .destroy = destroy8_1,
};
#endif
// --------------------- screen 8.2 --------------------- AT test
#if 1
static void scr8_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create8_2(lv_obj_t *parent) 
{
    lv_obj_t *lab = lv_label_create(parent);
    lv_obj_set_width(lab, lv_pct(95));
    lv_obj_set_style_text_font(lab, FONT_BOLD_SIZE_17, LV_PART_MAIN);
    lv_label_set_text(lab, "Open the serial port, set the baud rate to 115200, "
                            "and send the AT command of A7682E to test the function.");
    lv_obj_center(lab);
    
    lv_obj_t *back8_2_label = scr_back_btn_create(parent, ("AT test"), scr8_2_btn_event_cb);
}
static void entry8_2(void) 
{
    ui_a7682_loop_resume();
    ui_disp_full_refr();
}
static void exit8_2(void) {
    ui_a7682_loop_suspend();
    ui_disp_full_refr();
}
static void destroy8_2(void) { }

static scr_lifecycle_t screen8_2 = {
    .create = create8_2,
    .entry = entry8_2,
    .exit  = exit8_2,
    .destroy = destroy8_2,
};
#endif
//************************************[ screen 9 ]****************************************** Shutdown
#if 1
static bool shutdown_kbd_active = false;
static lv_obj_t *shutdown_confirm = NULL;

static void shutdown_confirm_accept(void)
{
    if (shutdown_confirm) {
        lv_obj_del(shutdown_confirm);
        shutdown_confirm = NULL;
    }
    shutdown_kbd_active = false;
    ui_shutdown_on();
}

static void shutdown_confirm_cancel(void)
{
    if (shutdown_confirm) {
        lv_obj_del(shutdown_confirm);
        shutdown_confirm = NULL;
    }
    shutdown_kbd_active = false;
    scr_mgr_pop(false);
}

static void shutdown_confirm_accept_cb(lv_event_t *e) { shutdown_confirm_accept(); }
static void shutdown_confirm_cancel_cb(lv_event_t *e) { shutdown_confirm_cancel(); }

/* Enter = confirm shutdown, any other key = cancel back to the menu
 * (user request: accidental entry must not kill the device - the old
 * blind 2s auto-shutdown is gone). */
void shutdown_keyboard_poll(void)
{
    if (!shutdown_kbd_active) return;
    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();
    if (c == '\n') {
        shutdown_confirm_accept();
    } else {
        shutdown_confirm_cancel();
    }
}

static void scr9_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        shutdown_confirm_cancel();
    }
}

static void create9(lv_obj_t *parent)
{
    if(ui_battery_25896_is_vbus_in())
    {
        lv_obj_t * label = lv_label_create(parent);
        lv_obj_set_width(label, lv_pct(95));
        lv_obj_set_style_text_font(label, FONT_BOLD_SIZE_15, LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, "The shutdown function can only be used when the "
                            "battery is connected alone, and cannot be shut down when connected to USB.");
        lv_obj_center(label);

        // back
        scr_back_btn_create(parent, "Shutdown", scr8_btn_event_cb);
    }
    else
    {
        lv_obj_t * img = lv_img_create(parent);
        lv_img_set_src(img, &img_start);
        lv_obj_center(img);

        scr_back_btn_create(parent, "Shutdown", scr9_btn_event_cb);

        /* confirmation overlay: Enter = OK, Cancel/any key/back = cancel */
        shutdown_confirm = lv_obj_create(lv_layer_top());
        lv_obj_set_size(shutdown_confirm, 220, 130);
        lv_obj_align(shutdown_confirm, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(shutdown_confirm, lv_color_white(), 0);
        lv_obj_set_style_border_width(shutdown_confirm, 1, 0);
        lv_obj_set_style_border_color(shutdown_confirm, lv_color_black(), 0);
        lv_obj_set_style_radius(shutdown_confirm, 6, 0);
        lv_obj_set_style_pad_all(shutdown_confirm, 8, 0);
        lv_obj_set_flex_flow(shutdown_confirm, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(shutdown_confirm, 6, 0);
        lv_obj_clear_flag(shutdown_confirm, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *body = lv_label_create(shutdown_confirm);
        lv_obj_set_width(body, lv_pct(100));
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_label_set_text(body, "Shut down now?\n(Enter=OK, any key=Cancel)");
        lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
        lv_obj_set_flex_grow(body, 1);

        lv_obj_t *row = lv_obj_create(shutdown_confirm);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, 32);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);

        lv_obj_t *cancel_btn = lv_btn_create(row);
        lv_obj_set_flex_grow(cancel_btn, 1);
        lv_obj_set_height(cancel_btn, 32);
        lv_obj_t *cancel_lab = lv_label_create(cancel_btn);
        lv_label_set_text(cancel_lab, "Cancel");
        lv_obj_center(cancel_lab);
        lv_obj_add_event_cb(cancel_btn, shutdown_confirm_cancel_cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t *ok_btn = lv_btn_create(row);
        lv_obj_set_flex_grow(ok_btn, 1);
        lv_obj_set_height(ok_btn, 32);
        lv_obj_t *ok_lab = lv_label_create(ok_btn);
        lv_label_set_text(ok_lab, "OK");
        lv_obj_center(ok_lab);
        lv_obj_add_event_cb(ok_btn, shutdown_confirm_accept_cb, LV_EVENT_CLICKED, NULL);

        shutdown_kbd_active = true;
    }
}
static void entry9(void)
{
    ui_disp_full_refr();
}
static void exit9(void) {
    ui_disp_full_refr();
    shutdown_kbd_active = false;
}
static void destroy9(void)
{
    shutdown_kbd_active = false;
    if (shutdown_confirm) {
        lv_obj_del(shutdown_confirm);
        shutdown_confirm = NULL;
    }
}

static scr_lifecycle_t screen9 = {
    .create = create9,
    .entry = entry9,
    .exit  = exit9,
    .destroy = destroy9,
};
#endif
//************************************[ screen 10 ]***************************************** pcm5102
#if 1
static lv_obj_t *pcm5102_list;
static lv_obj_t *pcm5102_page;
static int pcm5102_num = 0;
static int pcm5102_page_num = 0;
static int pcm5102_curr_page = 0;

static ui_pcm5102_handle pcm5102_handle_list[] = 
{
    {"PCM5102 Audio", NULL, NULL, ui_pcm5102_cb},
};

static void pcm5102_item_create(int curr_apge);

static void pcm5102_scr_event(lv_event_t *e)
{
    lv_obj_t *tgt = (lv_obj_t *)e->target;
    ui_pcm5102_handle *h = (ui_pcm5102_handle *)e->user_data;

    if(e->code == LV_EVENT_CLICKED) {
        if(h->cb)
            h->cb(h->name);
    }
}

static void pcm5102_item_create(int curr_apge)
{
    printf("pcm5102_curr_page = %d\n", pcm5102_curr_page);
    int start = (curr_apge * SETTING_PAGE_MAX_ITEM);
    int end = start + SETTING_PAGE_MAX_ITEM;
    if(end > pcm5102_num) end = pcm5102_num;

    printf("start=%d, end=%d\n", start, end);

    for(int i = start; i < end; i++) {
        ui_pcm5102_handle *h = &pcm5102_handle_list[i];
        h->obj = lv_list_add_btn(pcm5102_list, NULL, h->name);
        lv_obj_set_height(h->obj, 28);
        // h->st = lv_label_create(h->obj);
        // lv_obj_set_style_text_font(h->st, FONT_BOLD_SIZE_15, LV_PART_MAIN);
        // lv_obj_align(h->st, LV_ALIGN_RIGHT_MID, 0, 0);
        // lv_label_set_text_fmt(h->st, "%s", (h->get_cb() ? "ON" : "OFF"));
        // style
        lv_obj_set_style_text_font(h->obj, FONT_BOLD_SIZE_14, LV_PART_MAIN);
        lv_obj_set_style_bg_color(h->obj, DECKPRO_COLOR_BG, LV_PART_MAIN);
        lv_obj_set_style_text_color(h->obj, DECKPRO_COLOR_FG, LV_PART_MAIN);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(h->obj, 1, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(h->obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_radius(h->obj, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(h->obj, pcm5102_scr_event, LV_EVENT_CLICKED, (void *)h);
    }
}

static void pcm5102_page_switch_cb(lv_event_t *e)
{
    char opt = (int)e->user_data;
    
    if(pcm5102_num < SETTING_PAGE_MAX_ITEM) return;

    int child_cnt = lv_obj_get_child_cnt(pcm5102_list);
    
    for(int i = 0; i < child_cnt; i++)
    {
        lv_obj_t *child = lv_obj_get_child(pcm5102_list, 0);
        if(child)
            lv_obj_del(child);
    }

    if(opt == 'p')
    {
        pcm5102_curr_page = (pcm5102_curr_page < pcm5102_page_num) ? pcm5102_curr_page + 1 : 0;
    }
    else if(opt == 'n')
    {
        pcm5102_curr_page = (pcm5102_curr_page > 0) ? pcm5102_curr_page - 1 : pcm5102_page_num;
    }

    pcm5102_item_create(pcm5102_curr_page);
    lv_label_set_text_fmt(pcm5102_page, "%d / %d", pcm5102_curr_page, pcm5102_page_num);
}


static void scr10_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create10(lv_obj_t *parent) 
{
    pcm5102_list = lv_list_create(parent);
    lv_obj_set_size(pcm5102_list, LV_HOR_RES, lv_pct(88));
    lv_obj_align(pcm5102_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(pcm5102_list, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_pad_top(pcm5102_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(pcm5102_list, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(pcm5102_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(pcm5102_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(pcm5102_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(pcm5102_list, DECKPRO_COLOR_FG, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(pcm5102_list, 0, LV_PART_MAIN);

    pcm5102_num = sizeof(pcm5102_handle_list) / sizeof(pcm5102_handle_list[0]);
    pcm5102_page_num = pcm5102_num / SETTING_PAGE_MAX_ITEM;
    pcm5102_item_create(pcm5102_curr_page);

    lv_obj_t * ui_Button2 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button2, 71);
    lv_obj_set_height(ui_Button2, 40);
    lv_obj_set_x(ui_Button2, -70);
    lv_obj_set_y(ui_Button2, 130);
    lv_obj_set_align(ui_Button2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button2, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button2, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label1 = lv_label_create(ui_Button2);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "Back");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Button14 = lv_btn_create(parent);
    lv_obj_set_width(ui_Button14, 71);
    lv_obj_set_height(ui_Button14, 40);
    lv_obj_set_x(ui_Button14, 70);
    lv_obj_set_y(ui_Button14, 130);
    lv_obj_set_align(ui_Button14, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button14, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button14, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_Button14, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Button14, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_spread(ui_Button14, 0, LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED);
    lv_obj_set_style_radius(ui_Button14, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label15 = lv_label_create(ui_Button14);
    lv_obj_set_width(ui_Label15, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label15, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label15, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label15, "Next");
    lv_obj_set_style_text_color(ui_Label15, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_Label15, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_Button2, pcm5102_page_switch_cb, LV_EVENT_CLICKED, (void*)'n');
    lv_obj_add_event_cb(ui_Button14, pcm5102_page_switch_cb, LV_EVENT_CLICKED, (void*)'p');

    pcm5102_page = lv_label_create(parent);
    lv_obj_set_width(pcm5102_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(pcm5102_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(pcm5102_page, LV_ALIGN_BOTTOM_MID, 0, -23);
    lv_label_set_text_fmt(pcm5102_page, "%d / %d", pcm5102_curr_page, pcm5102_page_num);
    lv_obj_set_style_text_color(pcm5102_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(pcm5102_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back10_label = scr_back_btn_create(parent, ("PCM5102"), scr10_btn_event_cb);
}
static void entry10(void) 
{
    ui_disp_full_refr();
}
static void exit10(void) 
{
    ui_pcm5102_stop();
    ui_disp_full_refr();
}
static void destroy10(void) { }

static scr_lifecycle_t screen10 = {
    .create = create10,
    .entry = entry10,
    .exit  = exit10,
    .destroy = destroy10,
};
#endif
//************************************[ screen 11 ]****************************************** Sleep
#if 1
#include <TouchDrvCSTXXX.hpp>
static lv_timer_t *sleep_timer = NULL;      /* countdown; handle saved so exit11 can cancel */
static lv_timer_t *sleep_watch_timer = NULL;/* waits for THIS frame's flush, then starts countdown */
static uint32_t sleep_wait_seq = 0;         /* flush sequence bound to this Sleep frame */
static int sleep_watch_ticks = 0;           /* 50 ms ticks; 60 = 3 s backstop */
static lv_obj_t *sleep_count_lab = NULL;
static int sleep_countdown = 0;

static void scr11_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);                     /* exit11 cancels the pending sleep */
    }
}

/* Power down peripherals and enter deep sleep.
 * Wake source: ext1 on BOARD_BOOT_PIN (BOOT button), any-low. On wake the
 * chip REBOOTS (statics re-init, keypad_init() flushes the TCA8418), so no
 * modifier state can stick; setup() releases the gpio holds. */
static void sleep_do_enter(void)
{
    // extern TouchDrvCSTXXX touch;
    // touch.sleep();

    lora_sleep();

    SerialGPS.end();

    gpio_reset_pin((gpio_num_t)BOARD_GPS_PPS);
    gpio_reset_pin((gpio_num_t)BOARD_GPS_RXD);
    gpio_reset_pin((gpio_num_t)BOARD_GPS_TXD);
    gpio_reset_pin((gpio_num_t)BOARD_LORA_RST);
    gpio_reset_pin((gpio_num_t)BOARD_TOUCH_RST);
    gpio_reset_pin((gpio_num_t)BOARD_LORA_BUSY);

    digitalWrite(BOARD_6609_EN, LOW);
    digitalWrite(BOARD_LORA_EN, LOW);
    digitalWrite(BOARD_GPS_EN, LOW);

    digitalWrite(BOARD_A7682E_PWRKEY, LOW);

    gpio_hold_en((gpio_num_t)BOARD_6609_EN);
    gpio_hold_en((gpio_num_t)BOARD_LORA_EN);
    gpio_hold_en((gpio_num_t)BOARD_GPS_EN);
    gpio_hold_en((gpio_num_t)BOARD_A7682E_PWRKEY);
    gpio_deep_sleep_hold_en();

    esp_sleep_enable_ext1_wakeup((1UL << BOARD_BOOT_PIN), ESP_EXT1_WAKEUP_ANY_LOW);
    openai_stats_flush();                       /* lifecycle checkpoint (copilot 1.1) */
    esp_deep_sleep_start();
}

/* 1s ticks: "Sleep in: 2" -> "1" -> enter sleep. The NULL-handle guard
 * makes a pending tick a no-op after exit11/destroy11 cancelled. */
static void sleep_timer_event(lv_timer_t *t)
{
    if (sleep_timer == NULL) return;            /* cancelled */
    if (sleep_countdown > 0) {
        lv_label_set_text_fmt(sleep_count_lab,
                              "Entering sleep...\n\nWake: press BOOT key.\n\nSleep in: %d",
                              sleep_countdown);
        sleep_countdown--;
    } else {
        sleep_timer = NULL;                     /* no re-entry */
        lv_timer_del(t);
        sleep_do_enter();
    }
}

static void create11(lv_obj_t *parent)
{
    sleep_count_lab = lv_label_create(parent);
    lv_obj_set_width(sleep_count_lab, lv_pct(95));
    lv_obj_set_style_text_font(sleep_count_lab, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    lv_label_set_long_mode(sleep_count_lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(sleep_count_lab, "Entering sleep...\n\nWake: press BOOT key.");
    lv_obj_center(sleep_count_lab);

    // back (cancels the pending sleep)
    scr_back_btn_create(parent, "Sleep", scr11_btn_event_cb);
}
/* 50 ms ticks from the NORMAL LVGL timer loop: wait until the Sleep
 * frame's OWN flush sequence reached the panel, then start the countdown.
 * entry() runs BEFORE lv_scr_load() in scr_mgr_push, so a synchronous
 * wait there would watch the previous screen's frame AND re-enter LVGL
 * from a lifecycle callback (copilot finding 1.2). */
static void sleep_watch_timer_event(lv_timer_t *t)
{
    if (sleep_watch_timer == NULL) return;      /* cancelled */
    if (ui_disp_flush_done_seq() >= sleep_wait_seq) {
        sleep_watch_timer = NULL;
        lv_timer_del(t);
        sleep_countdown = 2;                    /* 2 ticks -> sleep after ~3 s */
        sleep_timer = lv_timer_create(sleep_timer_event, 1000, NULL);
        if (sleep_timer) {
            lv_timer_set_repeat_count(sleep_timer, 4);  /* backstop: never fires forever */
        }
    } else if (++sleep_watch_ticks >= 60) {
        /* 3 s without the frame reaching the panel: CANCEL the sleep
         * (copilot finding 1.4) - never deep-sleep blind on an invisible
         * prompt. */
        sleep_watch_timer = NULL;
        lv_timer_del(t);
        lv_label_set_text(sleep_count_lab,
                          "Display sync failed\nsleep cancelled\n\nPress back");
        Serial.println("[Sleep] frame-wait timeout - sleep cancelled");
    }
}

static void entry11(void)
{
    /* request the full refresh and BIND the wait to this frame's sequence:
     * only the flush of the Sleep screen itself satisfies the watcher */
    sleep_wait_seq = ui_disp_full_refr_seq();
    sleep_watch_ticks = 0;
    /* defensive order (main review 1.7): del + NULL before create, so a
     * re-entry can never hold a stale handle; no nested Sleep screen is
     * possible, but the cleanup must stay double-free-safe */
    if (sleep_watch_timer) {
        lv_timer_del(sleep_watch_timer);
        sleep_watch_timer = NULL;
    }
    if (sleep_timer) {
        lv_timer_del(sleep_timer);
        sleep_timer = NULL;
    }
    sleep_watch_timer = lv_timer_create(sleep_watch_timer_event, 50, NULL);
}
static void exit11(void) {
    ui_disp_full_refr();
    if (sleep_watch_timer) {
        lv_timer_del(sleep_watch_timer);        /* back pressed: cancel sleep */
        sleep_watch_timer = NULL;
    }
    if (sleep_timer) {
        lv_timer_del(sleep_timer);
        sleep_timer = NULL;
    }
}
static void destroy11(void)
{
    if (sleep_watch_timer) {
        lv_timer_del(sleep_watch_timer);
        sleep_watch_timer = NULL;
    }
    if (sleep_timer) {
        lv_timer_del(sleep_timer);
        sleep_timer = NULL;
    }
}

static scr_lifecycle_t screen11 = {
    .create = create11,
    .entry = entry11,
    .exit  = exit11,
    .destroy = destroy11,
};
#endif
//************************************[ screen 12 ]****************************************** Motor
#if 1
static lv_obj_t * motor_label;
static lv_timer_t *motor_timer = NULL;

static void scr12_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

void motor_timer_cb(lv_timer_t *t)
{
    static int idx = 1;
    
    lv_label_set_text_fmt(motor_label, "DRV2605  Waveform Library Effects List see datasheet part 11.2 \n"
                            "Effects : %d\n", idx);
    lv_obj_center(motor_label);

    ui_motor_loop(idx++);

    if(idx > 123) {
        idx = 0;
    }
}

static void create12(lv_obj_t *parent)
{
    motor_label = lv_label_create(parent);
    lv_obj_set_width(motor_label, lv_pct(95));
    lv_obj_set_style_text_font(motor_label, FONT_BOLD_SIZE_15, LV_PART_MAIN);
    lv_label_set_long_mode(motor_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(motor_label, "DRV2605  Waveform Library Effects List see datasheet part 11.2 \n"
                            "Effects : 0");
    lv_obj_center(motor_label);

    motor_timer = lv_timer_create(motor_timer_cb, 2000, NULL);
    // back 
    scr_back_btn_create(parent, "Motor", scr8_btn_event_cb);
}
static void entry12(void) 
{
    ui_disp_full_refr();
}
static void exit12(void) {
    ui_disp_full_refr();
}
static void destroy12(void) {
    if(motor_timer) {
        lv_timer_del(motor_timer);
        motor_timer = NULL;
    }
    ui_motor_stop();
}

static scr_lifecycle_t screen12 = {
    .create = create12,
    .entry = entry12,
    .exit  = exit12,
    .destroy = destroy12,
};
#endif
//************************************[ UI ENTRY ]******************************************
static lv_obj_t *menu_keypad;
static lv_timer_t *menu_timer = NULL;

static void indev_get_gesture_dir(lv_timer_t *t)
{
    lv_indev_t * touch_indev = lv_indev_get_next(NULL);
    lv_dir_t dir = lv_indev_get_gesture_dir(touch_indev);

    if(dir == LV_DIR_RIGHT) { // right
        ui_get_gesture_dir(LV_DIR_RIGHT);
    } 
    else if(dir == LV_DIR_LEFT) { // left
        ui_get_gesture_dir(LV_DIR_LEFT);
    }
}

static void menu_keypay_get_event(lv_timer_t *t)
{
    static int sec = 0;
    static int press = false;
    char keypay_v;
    int ret = ui_input_get_keypay_val(&keypay_v);

    if(ret > 0)
    {
        sec = 0;
        press = true;
        ui_input_set_keypay_flag();
        lv_label_set_text_fmt(menu_keypad, "%c", keypay_v);
    }

    if(press){
        sec++;
        if(sec > 20) {
            sec = 0;
            press = false;
            lv_label_set_text(menu_keypad, " ");
        }
    }
}

static void menu_taskbar_update_timer_cb(lv_timer_t *t)
{
    static int sec = 0;
    static int taskbar_last_hour = -1;
    static int taskbar_last_min = -1;
    sec++;

    bool charge = 0;
    bool finish = 0;
    bool wifi = 0;
    int percent = 0;

    if(sec % 10 == 0)
    {
        /* time: refreshed together with the battery (every 10s); shows the
         * real local time, "--:--" until NTP has synced the clock */
        {
            time_t now = time(nullptr);
            int h = -1, m = -1;
            if (now > 1700000000) {
                struct tm tmv;
                localtime_r(&now, &tmv);
                h = tmv.tm_hour;
                m = tmv.tm_min;
            }
            if (h != taskbar_last_hour || m != taskbar_last_min) {
                taskbar_last_hour = h;
                taskbar_last_min = m;
                if (h >= 0) {
                    lv_label_set_text_fmt(menu_taskbar_time, "%02d:%02d", h, m);
                } else {
                    lv_label_set_text(menu_taskbar_time, "--:--");
                }
            }
        }

        finish = ui_battery_27220_get_charge_finish();
        percent = ui_battery_27220_get_percent();

        if(taskbar_statue[TASKBAR_ID_CHARGE_FINISH] != finish) 
        {
            if(finish){
                lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_OK);
            } else {
                lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_CHARGE);
            }
            taskbar_statue[TASKBAR_ID_CHARGE_FINISH] = finish;
        }

        if(taskbar_statue[TASKBAR_ID_BATTERY_PERCENT] != percent) 
        {
            lv_label_set_text_fmt(menu_taskbar_battery_percent, "%d", percent);
            lv_label_set_text_fmt(menu_taskbar_battery, "%s", ui_battert_27220_get_percent_level());
            taskbar_statue[TASKBAR_ID_BATTERY_PERCENT] = percent;
        }
    }

    charge = ui_battery_27220_get_input();
    if(taskbar_statue[TASKBAR_ID_CHARGE] != charge) 
    {
        if(charge) {
            lv_obj_clear_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
        }
        taskbar_statue[TASKBAR_ID_CHARGE] = charge;
    }

}


static int cursor = 0;
#define SCREEN_POP -1

int menu_buf[] = {
    SCREEN1_ID, SCREEN1_1_ID, SCREEN_POP, SCREEN1_2_ID, SCREEN_POP, 0,
    SCREEN2_ID, 0,
    SCREEN3_ID, 0,
    SCREEN4_ID, SCREEN4_1_ID, SCREEN_POP, SCREEN4_2_ID, SCREEN_POP, 0,
    SCREEN5_ID, 0,
    SCREEN6_ID, SCREEN6_1_ID, SCREEN_POP, SCREEN6_2_ID, SCREEN_POP, 0,
    SCREEN7_ID, 0,
    // SCREEN8_ID, SCREEN8_1_ID, SCREEN_POP, SCREEN8_2_ID, SCREEN_POP, 0,
    // SCREEN9_ID, 0,
    // SCREEN10_ID, 0,
    // SCREEN11_ID, 0,
    SCREEN12_ID, 0,
};

void ui_auto_timer_cb(lv_timer_t *t)
{
    if(menu_buf[cursor] != 0 && menu_buf[cursor] != -1) {
        scr_mgr_push(menu_buf[cursor], false);
        printf("push = %d\n", menu_buf[cursor]);
    } else if(menu_buf[cursor] == -1) {
        scr_mgr_pop(false);
        printf("pop\n");
    } else {
        scr_mgr_switch(SCREEN0_ID, false);
        printf("back\n");
    }

    cursor++;
    if(cursor > (sizeof(menu_buf)/sizeof(menu_buf[0]) - 1)) {
        cursor = 0;
    }
}

void ui_deckpro_entry(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    disp->theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);

    touch_chk_timer = lv_timer_create(indev_get_gesture_dir, LV_INDEV_DEF_READ_PERIOD, NULL);
    lv_timer_pause(touch_chk_timer);

    taskbar_update_timer = lv_timer_create(menu_taskbar_update_timer_cb, 1000, NULL);
    lv_timer_pause(taskbar_update_timer);

    low_voltage_popup_create();
    low_voltage_timer = lv_timer_create(low_voltage_timer_cb, LOW_VOLTAGE_POLL_MS, NULL);

    // auto test
    // lv_timer_create(ui_auto_timer_cb, 3000, NULL);

    scr_mgr_init();

    scr_mgr_register(SCREEN0_ID,    &screen0);      // menu
    scr_mgr_register(SCREEN1_ID,    &screen1);      // Lora
    scr_mgr_register(SCREEN1_1_ID,  &screen1_1);    // - Auto send
    scr_mgr_register(SCREEN1_2_ID,  &screen1_2);    // - Lora Setting
    scr_mgr_register(SCREEN2_ID,    &screen2);      // Setting
    scr_mgr_register(SCREEN2_1_ID,  &screen2_1);    //  - About System
    scr_mgr_register(SCREEN3_ID,    &screen3);      // 
    scr_mgr_register(SCREEN4_ID,    &screen4);      // WIFI
    scr_mgr_register(SCREEN4_1_ID,  &screen4_1);    //  - WIFI Config
    scr_mgr_register(SCREEN4_2_ID,  &screen4_2);    //  - WIFI Scan
    scr_mgr_register(SCREEN5_ID,    &screen5);      // 
    scr_mgr_register(SCREEN5_1_ID,  &screen_sd_test);
    scr_mgr_register(SCREEN6_ID,    &screen6);      // Battery
    scr_mgr_register(SCREEN6_1_ID,  &screen6_1);    //  - BQ25896
    scr_mgr_register(SCREEN6_2_ID,  &screen6_2);    //  - BQ27220
    scr_mgr_register(SCREEN7_ID,    &screen7);      // 
    scr_mgr_register(SCREEN8_ID,    &screen8);      // A7682E
    scr_mgr_register(SCREEN8_1_ID,  &screen8_1);    //  - Call test
    scr_mgr_register(SCREEN8_2_ID,  &screen8_2);    //  - AT test
    scr_mgr_register(SCREEN9_ID,    &screen9);      // Shutdown
    scr_mgr_register(SCREEN10_ID,   &screen10);     // PCM5102
    scr_mgr_register(SCREEN11_ID,   &screen11);
    scr_mgr_register(SCREEN12_ID,   &screen12);     // Motor

    extern scr_lifecycle_t screen_calculator;
    scr_mgr_register(SCREEN_CALCULATOR_ID, &screen_calculator);

    extern scr_lifecycle_t screen_weather;
    scr_mgr_register(SCREEN_WEATHER_ID, &screen_weather);

    extern scr_lifecycle_t screen_calendar;
    scr_mgr_register(SCREEN_CALENDAR_ID, &screen_calendar);

    extern scr_lifecycle_t screen_dictionary;
    scr_mgr_register(SCREEN_DICTIONARY_ID, &screen_dictionary);

    extern scr_lifecycle_t screen_gps_enhanced;
    scr_mgr_register(SCREEN_GPS_ENHANCED_ID, &screen_gps_enhanced);

    extern scr_lifecycle_t screen_voice_ai;
    scr_mgr_register(SCREEN_VOICE_AI_ID, &screen_voice_ai);

    extern scr_lifecycle_t screen_ai_chat;
    scr_mgr_register(SCREEN_AI_CHAT_ID, &screen_ai_chat);

    extern scr_lifecycle_t screen_ai_cfg;
    scr_mgr_register(SCREEN_AI_CFG_ID, &screen_ai_cfg);

    extern scr_lifecycle_t screen_penpal;
    scr_mgr_register(SCREEN_PENPAL_ID, &screen_penpal);

    scr_mgr_switch(SCREEN0_ID, false); // set root screen
    scr_mgr_set_anim(LV_SCR_LOAD_ANIM_OVER_LEFT, LV_SCR_LOAD_ANIM_OVER_LEFT, LV_SCR_LOAD_ANIM_OVER_LEFT);

    // menu_keypad = lv_label_create(lv_layer_top());
    // lv_obj_set_style_text_font(menu_keypad, FONT_BOLD_MONO_SIZE_15, LV_PART_MAIN);
    // lv_label_set_text(menu_keypad, " ");
    // lv_obj_align(menu_keypad, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

    // menu_timer = lv_timer_create(menu_keypay_get_event, 40, NULL);
}
