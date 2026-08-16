/**
 * @file      ui_ai_chat.cpp
 * @brief     AI Text chat screen, WeChat/WhatsApp-style layout:
 *              - top 2/3: read-only, scrollable conversation history
 *                (AI replies left-aligned, user messages right-aligned)
 *              - bottom 1/3: multi-line input box for the current draft
 *              - small Send / Clear buttons on the side of the input box
 *
 * Sending is asynchronous (FreeRTOS task + result queue with page
 * generation). The draft stays in the input box until a reply arrives;
 * on failure it is kept for direct retry. History is in-RAM only.
 *
 * Review findings incorporated:
 *   - the task works on its OWN snapshot of the prompt + AI config
 *     (finding 1.6): a re-entry while a send is in flight can no longer
 *     corrupt the in-flight request, and only a result matching the
 *     CURRENT page generation may clear the busy flag
 *   - chat_history_add backs off to a UTF-8 code point boundary before
 *     truncating and marks the cut explicitly (finding 1.10)
 *   - the result queue is created and CHECKED before busy is set
 *     (finding 1.5)
 *   - contract: docs/async_ipc_contract.md
 *
 * Keypad map:
 *   \n : send                     \b : delete; empty -> back to menu
 *   + / - (Sym/Alt layer): scroll the history
 *   '\t' (Alt+Enter) / '\v' (volume): ignored
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "ui_scr_mrg.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#define CHAT_HIST_MAX    40
#define CHAT_MSG_MAX     256
#define CHAT_BUBBLE_W    178     /* bubble width incl. border; label 170 inside */
#define CHAT_TRUNC_MARK  "(truncated)"

typedef struct {
    bool from_user;
    char text[CHAT_MSG_MAX];
} chat_msg_t;

/* Per-task request snapshot (finding 1.6): the task never reads UI-owned
 * state, so leaving/re-entering the screen mid-flight is harmless. */
typedef struct {
    uint32_t gen;                   /* page generation captured at send time */
    char prompt[CHAT_MSG_MAX];
    char base[160];
    char model[80];
    char key[80];
} chat_send_req_t;

typedef struct {
    uint32_t gen;
    bool ok;
    string reply;
} chat_reply_t;

static lv_obj_t *chat_hist_cont = NULL;     /* scrollable read-only history */
static lv_obj_t *chat_input_ta = NULL;      /* current draft */
static lv_obj_t *chat_status_lab = NULL;
static bool chat_kbd_active = false;

static chat_msg_t chat_history[CHAT_HIST_MAX];
static int chat_hist_cnt = 0;

static QueueHandle_t s_chat_q = NULL;
static volatile uint32_t s_chat_page_gen = 0;   /* bumped on entry/destroy */
static volatile bool s_chat_send_busy = false;  /* UI-owned */

/* Append a message (rolls: oldest entry is dropped when full). Overlong
 * text is truncated at a UTF-8 code point boundary - the cut never lands
 * inside a multi-byte sequence - and marked with CHAT_TRUNC_MARK
 * (finding 1.10). */
static void chat_history_add(bool from_user, const char *text)
{
    if (chat_hist_cnt >= CHAT_HIST_MAX) {
        memmove(&chat_history[0], &chat_history[1],
                sizeof(chat_msg_t) * (CHAT_HIST_MAX - 1));
        chat_hist_cnt = CHAT_HIST_MAX - 1;
    }
    chat_msg_t *m = &chat_history[chat_hist_cnt++];
    m->from_user = from_user;

    size_t n = strlen(text);
    size_t limit = CHAT_MSG_MAX - 1 - sizeof(CHAT_TRUNC_MARK);
    if (n > limit) {
        n = limit;
        /* back off over trailing continuation bytes (10xxxxxx) so the cut
         * lands on a lead byte; a lead byte never starts with 10 */
        while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80) n--;
        memcpy(m->text, text, n);
        strcpy(m->text + n, CHAT_TRUNC_MARK);
    } else {
        memcpy(m->text, text, n + 1);
    }
}

/* Rebuild the bubbles: AI left-aligned, user right-aligned; jump to the
 * newest message. UTF-8 wrapping is handled by LV_LABEL_LONG_WRAP. */
static void chat_history_render(void)
{
    lv_obj_clean(chat_hist_cont);
    lv_coord_t w = lv_obj_get_content_width(chat_hist_cont);
    int y = 2;
    for (int i = 0; i < chat_hist_cnt; i++) {
        chat_msg_t *m = &chat_history[i];
        lv_obj_t *box = lv_obj_create(chat_hist_cont);
        lv_obj_set_size(box, CHAT_BUBBLE_W, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(box, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_black(), 0);
        lv_obj_set_style_radius(box, 6, 0);
        lv_obj_set_style_pad_all(box, 4, 0);
        lv_obj_set_pos(box, m->from_user ? (w - CHAT_BUBBLE_W - 4) : 4, y);

        lv_obj_t *lab = lv_label_create(box);
        lv_obj_set_width(lab, CHAT_BUBBLE_W - 8);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
        lv_label_set_text(lab, m->text);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);

        lv_obj_update_layout(chat_hist_cont);
        y += lv_obj_get_height(box) + 4;
    }
    lv_obj_update_layout(chat_hist_cont);
    lv_obj_scroll_to_y(chat_hist_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* Async send (review: sync HTTP would freeze the UI for up to 30s).
 * Results travel over a FreeRTOS queue as heap-allocated structs
 * carrying the page generation - no volatile-flag-guarded std::string
 * cross-core access, stale replies of a previous visit are dropped. */
static void chat_send_task_func(void *param)
{
    chat_send_req_t *rq = (chat_send_req_t *)param; /* task-owned snapshot */
    chat_reply_t *res = new chat_reply_t;
    res->gen = rq->gen;
    res->ok = openai_chat(rq->prompt, rq->base, rq->model, rq->key,
                          res->reply, 30000);
    delete rq;
    if (s_chat_q) {
        xQueueSend(s_chat_q, &res, portMAX_DELAY);
    } else {
        delete res;
    }
    vTaskDelete(NULL);
}

static void chat_send(void)
{
    if (s_chat_send_busy) return;               /* already sending */

    const char *prompt = lv_textarea_get_text(chat_input_ta);
    if (!prompt || prompt[0] == '\0') return;

    /* finding 1.5: create and CHECK the queue before going busy */
    if (!s_chat_q) s_chat_q = xQueueCreate(4, sizeof(void *));
    if (!s_chat_q) {
        lv_label_set_text(chat_status_lab, "Out of memory");
        return;
    }

    /* snapshot prompt + AI config into a task-owned request (finding 1.6):
     * the task never reads UI-owned state again */
    chat_send_req_t *rq = new chat_send_req_t;
    rq->gen = s_chat_page_gen;
    strncpy(rq->prompt, prompt, sizeof(rq->prompt) - 1);
    rq->prompt[sizeof(rq->prompt) - 1] = '\0';
    openai_load_config(rq->base, sizeof(rq->base),
                       rq->model, sizeof(rq->model),
                       rq->key, sizeof(rq->key));
    if (rq->key[0] == '\0') {
        delete rq;
        lv_label_set_text(chat_status_lab, "No API key - set it in AI Config");
        return;
    }

    /* the message goes into the history immediately (chat semantics); the
     * draft STAYS in the input box until success, so failures can retry */
    chat_history_add(true, prompt);
    chat_history_render();
    lv_label_set_text(chat_status_lab, "Thinking...");

    s_chat_send_busy = true;
    TaskHandle_t h = NULL;
    if (xTaskCreate(chat_send_task_func, "chat_send", 1024 * 8,
                    rq, 1, &h) != pdPASS) {
        delete rq;
        s_chat_send_busy = false;
        lv_label_set_text(chat_status_lab, "Cannot start task");
    }
}

void ai_chat_keyboard_poll(void)
{
    /* Drain async replies (queue, ownership transfers to the UI).
     * Only a result matching the CURRENT page generation may clear busy
     * (finding 1.6): a stale reply of an earlier visit must not release
     * the busy flag of the request currently in flight. */
    chat_reply_t *cr = NULL;
    while (s_chat_q && xQueueReceive(s_chat_q, &cr, 0) == pdTRUE) {
        if (!cr) continue;
        if (cr->gen == s_chat_page_gen && chat_kbd_active) {
            s_chat_send_busy = false;
            if (cr->ok) {
                Serial.printf("[AIChat] reply len=%u\n", (unsigned)cr->reply.length());
                lv_textarea_set_text(chat_input_ta, "");    /* success: draft consumed */
                chat_history_add(false, cr->reply.c_str());
                chat_history_render();
                lv_label_set_text(chat_status_lab, "");
            } else {
                /* failure: the draft stays in the box for retry */
                lv_label_set_text(chat_status_lab, "AI error - check cfg / WiFi");
            }
        } else {
            Serial.println("[AIChat] stale reply dropped");
        }
        delete cr;
    }

    if (!chat_kbd_active) return;

    char c;
    if (!keypad_get_val(&c)) return;
    keypad_set_flag();

    if (c == '\t' || c == '\v') return;         /* Alt+Enter scan combo / volume key */

    if (s_chat_send_busy) return;               /* sending: swallow input */

    if (c == '\n') {
        chat_send();
    } else if (c == '+' || c == '-') {
        /* scroll the history (Sym/Alt layer) */
        lv_obj_scroll_by(chat_hist_cont, 0, c == '+' ? -120 : 120, LV_ANIM_OFF);
    } else if (c == '\b') {
        const char *txt = lv_textarea_get_text(chat_input_ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(chat_input_ta);
        } else {
            chat_kbd_active = false;
            scr_mgr_pop(false);
        }
    } else {
        lv_textarea_add_char(chat_input_ta, c);
    }
}

static void chat_send_btn_cb(lv_event_t *e)
{
    Serial.println("[AIChat] Send button clicked");
    chat_send();
}

static void chat_clear_btn_cb(lv_event_t *e)
{
    Serial.println("[AIChat] Clear button clicked");
    lv_textarea_set_text(chat_input_ta, "");
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
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(cont, 4, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* --- history: read-only, scrollable, top 2/3 --- */
    chat_hist_cont = lv_obj_create(cont);
    lv_obj_set_width(chat_hist_cont, lv_pct(100));
    lv_obj_set_height(chat_hist_cont, 164);
    lv_obj_set_style_bg_color(chat_hist_cont, lv_color_white(), 0);
    lv_obj_set_style_border_width(chat_hist_cont, 1, 0);
    lv_obj_set_style_border_color(chat_hist_cont, lv_color_black(), 0);
    lv_obj_set_style_radius(chat_hist_cont, 4, 0);
    lv_obj_set_style_pad_all(chat_hist_cont, 2, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(chat_hist_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(chat_hist_cont, LV_DIR_VER);
    lv_obj_clear_flag(chat_hist_cont, LV_OBJ_FLAG_SCROLL_CHAIN);

    /* --- status line --- */
    chat_status_lab = lv_label_create(cont);
    lv_obj_set_style_text_font(chat_status_lab, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(chat_status_lab, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_label_set_text(chat_status_lab, "");

    /* --- input row: multi-line box (1/3) + small side buttons --- */
    lv_obj_t *input_row = lv_obj_create(cont);
    lv_obj_set_width(input_row, lv_pct(100));
    lv_obj_set_height(input_row, 82);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(input_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(input_row, 4, LV_PART_MAIN);

    chat_input_ta = lv_textarea_create(input_row);
    lv_obj_set_width(chat_input_ta, 176);
    lv_obj_set_height(chat_input_ta, 82);
    lv_textarea_set_max_length(chat_input_ta, 200);
    lv_textarea_set_placeholder_text(chat_input_ta, "Type here...");
    lv_obj_set_style_text_font(chat_input_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* small Send / Clear buttons stacked on the side */
    lv_obj_t *btn_col = lv_obj_create(input_row);
    lv_obj_set_width(btn_col, 44);
    lv_obj_set_height(btn_col, 82);
    lv_obj_set_style_bg_opa(btn_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_col, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(btn_col, 4, LV_PART_MAIN);

    lv_obj_t *send_btn = lv_btn_create(btn_col);
    lv_obj_set_width(send_btn, 44);
    lv_obj_set_height(send_btn, 38);
    lv_obj_t *send_lab = lv_label_create(send_btn);
    lv_label_set_text(send_lab, "Send");
    lv_obj_center(send_lab);
    lv_obj_add_event_cb(send_btn, chat_send_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_btn = lv_btn_create(btn_col);
    lv_obj_set_width(clear_btn, 44);
    lv_obj_set_height(clear_btn, 38);
    lv_obj_t *clear_lab = lv_label_create(clear_btn);
    lv_label_set_text(clear_lab, "Clear");
    lv_obj_center(clear_lab);
    lv_obj_add_event_cb(clear_btn, chat_clear_btn_cb, LV_EVENT_CLICKED, NULL);

    chat_kbd_active = true;
}

static void chat_entry(void)
{
    ui_disp_full_refr();
    s_chat_page_gen++;                          /* invalidate replies of a previous visit */
}
static void chat_exit(void)  { ui_disp_full_refr(); }
static void chat_destroy(void)
{
    chat_kbd_active = false;
    s_chat_page_gen++;                          /* late replies of this visit are dropped */
    /* safe to clear busy here: the in-flight task owns its own request
     * snapshot (finding 1.6), and its reply will be dropped by gen */
    s_chat_send_busy = false;
}

scr_lifecycle_t screen_ai_chat = {
    .create = chat_create,
    .entry = chat_entry,
    .exit  = chat_exit,
    .destroy = chat_destroy,
};
