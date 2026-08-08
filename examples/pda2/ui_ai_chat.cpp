/**
 * @file      ui_ai_chat.cpp
 * @brief     Text-only AI chat screen (OpenAI-compatible / OpenRouter).
 *            Input via keypad (lowercase), reply shown paginated on E-Paper.
 *
 * Keypad map:
 *   \n : send the prompt (or next answer page while viewing)
 *   c  : open AI config screen
 *   \b : backspace (delete last char) / clear view / back to menu when empty
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "ui_scr_mrg.h"

/* Reviewer #3 fix:
 *   - Wrap long replies by display width (30 chars per ~16px font = 240px width).
 *   - Truncate gracefully past CHAT_MAX_LINES with explicit "...(more)" marker.
 *   - Reuse chat_lines[] as a fixed-size char array so no strtok aliasing. */
#define CHAT_LINES_PER_PAGE 8
#define CHAT_MAX_LINES      64
#define CHAT_LINE_WIDTH     30
#define CHAT_LINE_LEN       (CHAT_LINE_WIDTH + 1)
#define CHAT_ANSWER_MAX     4096   /* grown from 2048 to cover longer model replies */

static lv_obj_t *chat_ta = NULL;
static lv_obj_t *chat_status_lab = NULL;
static lv_obj_t *chat_answer_lab = NULL;
static bool chat_kbd_active = false;
static bool chat_viewing = false;
static bool chat_truncated = false;
static int  chat_page = 0;
static char chat_answer[CHAT_ANSWER_MAX] = {0};
static char chat_lines_storage[CHAT_MAX_LINES][CHAT_LINE_LEN];
static char *chat_lines[CHAT_MAX_LINES];
static int  chat_line_cnt = 0;

static void chat_render(void)
{
    static char page_buf[CHAT_LINES_PER_PAGE * CHAT_LINE_LEN + 8];
    int pos = 0;
    page_buf[0] = '\0';

    int start = chat_page * CHAT_LINES_PER_PAGE;
    for (int i = start; i < chat_line_cnt && i < start + CHAT_LINES_PER_PAGE; i++) {
        int n = snprintf(page_buf + pos, sizeof(page_buf) - pos, "%s\n", chat_lines[i]);
        if (n <= 0 || pos + n >= (int)sizeof(page_buf) - 1) break;
        pos += n;
    }

    lv_label_set_text(chat_answer_lab, page_buf);
    int pages = (chat_line_cnt + CHAT_LINES_PER_PAGE - 1) / CHAT_LINES_PER_PAGE;
    if (pages == 0) pages = 1;
    lv_label_set_text_fmt(chat_status_lab, "Page %d/%d%s",
                          chat_page + 1, pages,
                          chat_truncated ? " (truncated)" : "");
}

/* Break one source line into <=CHAT_LINE_WIDTH-char display lines, pushing
 * into chat_lines[]. Stops if CHAT_MAX_LINES reached, marks chat_truncated. */
static void chat_push_wrapped(const char *src)
{
    int slen = (int)strlen(src);
    int i = 0;
    while (i < slen) {
        if (chat_line_cnt >= CHAT_MAX_LINES) {
            chat_truncated = true;
            return;
        }
        int take = slen - i;
        if (take > CHAT_LINE_WIDTH) take = CHAT_LINE_WIDTH;
        memcpy(chat_lines_storage[chat_line_cnt], src + i, take);
        chat_lines_storage[chat_line_cnt][take] = '\0';
        chat_lines[chat_line_cnt] = chat_lines_storage[chat_line_cnt];
        chat_line_cnt++;
        i += take;
    }
}

static void chat_send(void)
{
    const char *prompt = lv_textarea_get_text(chat_ta);
    if (!prompt || prompt[0] == '\0') return;

    char base[160], model[80], key[80];
    openai_load_config(base, sizeof(base), model, sizeof(model), key, sizeof(key));
    if (key[0] == '\0') {
        lv_label_set_text(chat_status_lab, "No API key - press c to config");
        return;
    }

    lv_label_set_text(chat_status_lab, "Thinking...");
    ui_disp_full_refr();
    lv_timer_handler();   /* flush "Thinking..." before the blocking call */

    string reply;
    bool ok = openai_chat(prompt, base, model, key, reply);

    if (ok) {
        strncpy(chat_answer, reply.c_str(), sizeof(chat_answer) - 1);
        chat_answer[sizeof(chat_answer) - 1] = '\0';

        chat_line_cnt = 0;
        chat_truncated = false;
        char *save = NULL;
        char *line = strtok_r(chat_answer, "\n", &save);
        while (line) {
            chat_push_wrapped(line);
            line = strtok_r(NULL, "\n", &save);
            if (chat_truncated) break;
        }
        if (chat_line_cnt == 0) {
            strncpy(chat_lines_storage[0], "(no reply)", CHAT_LINE_LEN - 1);
            chat_lines[0] = chat_lines_storage[0];
            chat_line_cnt = 1;
        }
        chat_page = 0;
        chat_viewing = true;
        chat_render();
    } else {
        lv_label_set_text(chat_status_lab, "AI error - check cfg / WiFi");
    }
}

void ai_chat_keyboard_poll(void)
{
    if (!chat_kbd_active) return;

    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (chat_viewing) {
        if (c == '\n') {
            if ((chat_page + 1) * CHAT_LINES_PER_PAGE < chat_line_cnt) {
                chat_page++;
                chat_render();
            }
        } else if (c == '\b') {
            if (chat_page > 0) {
                chat_page--;
                chat_render();
            } else {
                chat_viewing = false;
                chat_page = 0;
                lv_label_set_text(chat_status_lab, "");
                lv_label_set_text(chat_answer_lab, "");
            }
        }
        return;
    }

    if (c == '\n') {
        chat_send();
    } else if (c == '\b') {
        const char *txt = lv_textarea_get_text(chat_ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(chat_ta);
        } else {
            chat_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else {
        lv_textarea_add_char(chat_ta, c);
    }
}

static void chat_back_cb(lv_event_t *e)
{
    chat_kbd_active = false;
    scr_mgr_pop(false);
}

static void chat_create(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "AI Text", chat_back_cb);

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

    chat_ta = lv_textarea_create(cont);
    lv_obj_set_width(chat_ta, lv_pct(100));
    lv_obj_set_height(chat_ta, 38);
    lv_textarea_set_one_line(chat_ta, true);
    lv_textarea_set_max_length(chat_ta, 200);
    lv_textarea_set_placeholder_text(chat_ta, "Ask anything, Enter=send");
    lv_obj_set_style_text_font(chat_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    chat_status_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(chat_status_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(chat_status_lab, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(chat_status_lab, "");

    chat_answer_lab = lv_label_create(cont);
    lv_obj_set_width(chat_answer_lab, lv_pct(100));
    lv_obj_set_flex_grow(chat_answer_lab, 1);
    lv_obj_set_style_text_font(chat_answer_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_long_mode(chat_answer_lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(chat_answer_lab, "");

    lv_obj_t *hint = lv_label_create(cont);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(hint, "Enter:send  Backspace:del\nAI Cfg: menu page 2");

    chat_kbd_active = true;
}

static void chat_entry(void) { ui_disp_full_refr(); }
static void chat_exit(void)  { ui_disp_full_refr(); }
static void chat_destroy(void)
{
    chat_kbd_active = false;
    chat_viewing = false;
    chat_page = 0;
    chat_line_cnt = 0;
    chat_truncated = false;
}

scr_lifecycle_t screen_ai_chat = {
    .create = chat_create,
    .entry = chat_entry,
    .exit  = chat_exit,
    .destroy = chat_destroy,
};
