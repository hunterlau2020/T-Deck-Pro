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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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

/* Async send (review: sync HTTP would freeze the UI for up to 30s):
 * the request runs in a FreeRTOS task, the reply is applied from the
 * keyboard poll when ready. */
static TaskHandle_t chat_send_task = NULL;
static volatile bool chat_send_result_ready = false;
static bool chat_send_ok = false;
static string chat_send_reply;
static char chat_prompt_buf[512] = {0};

static void chat_apply_reply(const string &reply)
{
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
}

static void chat_send_task_func(void *param)
{
    char base[160], model[80], key[80];
    openai_load_config(base, sizeof(base), model, sizeof(model), key, sizeof(key));
    chat_send_ok = openai_chat(chat_prompt_buf, base, model, key, chat_send_reply);
    chat_send_result_ready = true;
    chat_send_task = NULL;
    vTaskDelete(NULL);
}

static void chat_send(void)
{
    if (chat_send_task != NULL) return;         /* already sending */

    const char *prompt = lv_textarea_get_text(chat_ta);
    if (!prompt || prompt[0] == '\0') return;

    char base[160], model[80], key[80];
    openai_load_config(base, sizeof(base), model, sizeof(model), key, sizeof(key));
    if (key[0] == '\0') {
        lv_label_set_text(chat_status_lab, "No API key - set it in AI Config");
        return;
    }

    strncpy(chat_prompt_buf, prompt, sizeof(chat_prompt_buf) - 1);
    chat_prompt_buf[sizeof(chat_prompt_buf) - 1] = '\0';
    lv_textarea_set_text(chat_ta, "");          /* draft consumed */
    lv_label_set_text(chat_status_lab, "Thinking...");
    chat_send_result_ready = false;
    if (xTaskCreate(chat_send_task_func, "chat_send", 1024 * 8, NULL, 1,
                    &chat_send_task) != pdPASS) {
        chat_send_task = NULL;
        lv_label_set_text(chat_status_lab, "Cannot start task");
    }
}

void ai_chat_keyboard_poll(void)
{
    /* async reply: apply only while the screen is active */
    if (chat_send_result_ready) {
        chat_send_result_ready = false;
        if (!chat_kbd_active) {
            Serial.println("[AIChat] reply dropped (inactive)");
            return;
        }
        if (chat_send_ok) {
            Serial.printf("[AIChat] reply len=%u\n", (unsigned)chat_send_reply.length());
            chat_apply_reply(chat_send_reply);
        } else {
            lv_label_set_text(chat_status_lab, "AI error - check cfg / WiFi");
        }
    }

    if (!chat_kbd_active) return;

    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\t' || c == '\v') return;         /* Alt+Enter scan combo / volume key: not for chat */

    if (chat_send_task != NULL) return;         /* sending: swallow input */

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
        } else if (c >= ' ') {
            /* typing while viewing the answer: go back to the input box and
             * append the key instead of silently dropping it */
            chat_viewing = false;
            chat_page = 0;
            lv_label_set_text(chat_status_lab, "");
            lv_label_set_text(chat_answer_lab, "");
            lv_textarea_add_char(chat_ta, c);
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

static void chat_send_btn_cb(lv_event_t *e)
{
    Serial.println("[AIChat] Send button clicked");
    chat_send();
}

static void chat_clear_btn_cb(lv_event_t *e)
{
    Serial.println("[AIChat] Clear button clicked");
    lv_textarea_set_text(chat_ta, "");
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
    lv_obj_set_height(chat_ta, 64);            /* multi-line: room for ~150+ chars */
    lv_textarea_set_max_length(chat_ta, 200);
    lv_textarea_set_placeholder_text(chat_ta, "Ask anything...");
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

    /* Send / Clear buttons pinned to the container bottom */
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

    lv_obj_t *send_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(send_btn, 1);
    lv_obj_set_height(send_btn, 34);
    lv_obj_t *send_lab = lv_label_create(send_btn);
    lv_label_set_text(send_lab, "Send");
    lv_obj_center(send_lab);
    lv_obj_add_event_cb(send_btn, chat_send_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_btn = lv_btn_create(btn_row);
    lv_obj_set_flex_grow(clear_btn, 1);
    lv_obj_set_height(clear_btn, 34);
    lv_obj_t *clear_lab = lv_label_create(clear_btn);
    lv_label_set_text(clear_lab, "Clear");
    lv_obj_center(clear_lab);
    lv_obj_add_event_cb(clear_btn, chat_clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn_row);

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
