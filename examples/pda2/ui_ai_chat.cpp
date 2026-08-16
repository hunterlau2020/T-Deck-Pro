/**
 * @file      ui_ai_chat.cpp
 * @brief     AI Text chat screen, WeChat/WhatsApp-style layout:
 *              - top 2/3: read-only, scrollable conversation history
 *                (AI replies left-aligned, user messages right-aligned)
 *              - bottom 1/3: multi-line input box for the current draft
 *              - small Send / Clear / New buttons on the side of the input
 *                (New = start a fresh conversation, clears history + log)
 *
 * Sending is asynchronous (FreeRTOS task + result queue with page
 * generation). The draft stays in the input box until a reply arrives;
 * on failure it is kept for direct retry.
 *
 * History storage (review round 20 findings 1.2/1.5/1.9, copilot 1.6-1.9):
 *   - message bodies are std::string on the heap (no fixed 256 B cut);
 *     a total byte budget (CHAT_HIST_BUDGET) evicts the oldest messages,
 *     a per-message cap (CHAT_MSG_MAX) is the ONLY truncation mechanism
 *     and appends "(truncated)" at a UTF-8 code point boundary
 *   - the ring is persisted to SPIFFS /chat.log (binary records, rewritten
 *     on every change, 16 KB budget); restored on screen create. If SPIFFS
 *     is unavailable the chat degrades to RAM-only (logged once)
 *   - re-entering the screen re-renders the restored history (copilot 1.6)
 *   - the user bubble is added only AFTER the task really started, and a
 *     retry REUSES the pending bubble (drop-last + re-add), marking it
 *     "(failed)" while it awaits a retry (copilot 1.7)
 *   - the send task works on its own snapshot (finding 1.6); the prompt is
 *     copied as a full std::string, never cut at 255 bytes (copilot 1.8)
 *   - only a result matching the CURRENT page generation may clear busy
 *   - multi-turn context (round 21): the API request's messages array
 *     carries system + the most recent turns up to CHAT_CTX_BUDGET bytes
 *     (whole turns, oldest cut first, pending bubble excluded) + the
 *     current prompt, so the assistant actually remembers the session
 *
 * Layout pixel budget (240x320 EPD, 14 px font, review finding 1.6):
 *   back bar y=0..32 | container y=32..306 (232x274):
 *     history 160 + gap 4 + status 16 + gap 4 + input row 86 = 270 <= 274
 *   input row: textarea 176x86 | button column 44x86 (3 x 26 + 2 x 4 gaps)
 *   textarea max length: 200 chars (<= 800 UTF-8 bytes, heap-allocated)
 *
 * Keypad map:
 *   \n : send                     \b : delete; empty -> back to menu
 *   + / - (Sym/Alt layer): scroll the history
 *   '\t' (Alt+Enter): New chat confirm
 *   '\v' (volume): ignored
 *   confirmation open: \n = OK, any other key = Cancel
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "openai_api.h"
#include "ui_scr_mrg.h"
#include <SPIFFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <vector>

#define CHAT_HIST_MAX    40
#define CHAT_MSG_MAX     4096    /* per-message cap; the ONLY truncation point */
#define CHAT_HIST_BUDGET 16384   /* total bytes across all messages */
#define CHAT_CTX_BUDGET  8192    /* history bytes sent as API context per request */
#define CHAT_BUBBLE_W    178     /* bubble width incl. border; label 170 inside */
#define CHAT_TRUNC_MARK  "(truncated)"
#define CHAT_LOG_PATH    "/chat.log"
#define CHAT_LOG_TMP     "/chat.log.tmp"
#define CHAT_DRAFT_PATH  "/chat.draft"
#define CHAT_LOG_MAGIC   0x314C4843u    /* "CHL1" little-endian */

typedef struct {
    bool from_user;
    string text;                    /* dynamic: no fixed 256 B cut */
} chat_msg_t;

/* Per-task request snapshot (finding 1.6): the task never reads UI-owned
 * state, so leaving/re-entering the screen mid-flight is harmless.
 * history: (role, content) pairs in chronological order - OWN copies, so
 * the task never dereferences UI-owned strings. */
typedef struct {
    uint32_t gen;                   /* page generation captured at send time */
    string prompt;                  /* full copy - never cut mid UTF-8 (copilot 1.8) */
    vector<pair<string, string>> history;   /* multi-turn context (round 21) */
    string base;
    string model;
    string key;
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
static int chat_hist_bytes = 0;             /* sum of body lengths */
static int chat_pending_idx = -1;           /* bubble awaiting a reply; retried in place */
static bool chat_spiffs_ok = false;         /* SPIFFS mounted; log enabled */

static QueueHandle_t s_chat_q = NULL;
static volatile uint32_t s_chat_page_gen = 0;   /* bumped on entry/destroy */
static volatile bool s_chat_send_busy = false;  /* UI-owned */

/* ---- history storage -------------------------------------------------- */

/* Drop the LAST entry (the pending bubble is always last while it exists). */
static void chat_history_drop_last(void)
{
    if (chat_hist_cnt <= 0) return;
    chat_hist_cnt--;
    chat_hist_bytes -= chat_history[chat_hist_cnt].text.length();
    chat_history[chat_hist_cnt].text.clear();
}

/* The single truncation mechanism (main finding 1.5): cut at a UTF-8 code
 * point boundary - the cut never lands inside a multi-byte sequence - and
 * mark it explicitly. Returns the (possibly truncated) body. */
static string chat_text_trunc(const char *text)
{
    string body(text);
    size_t limit = CHAT_MSG_MAX - strlen(CHAT_TRUNC_MARK);
    if (body.length() <= limit) return body;
    body.resize(limit);
    while (!body.empty() && ((uint8_t)body.back() & 0xC0) == 0x80) {
        body.pop_back();            /* back off over trailing continuation bytes */
    }
    body += CHAT_TRUNC_MARK;
    return body;
}

/* Append a message: count limit first, then byte-budget eviction from the
 * FRONT (the newest message always survives). */
static void chat_history_add(bool from_user, const char *text)
{
    if (chat_hist_cnt >= CHAT_HIST_MAX) {
        /* drop oldest with proper string assignment (memmove would be
         * invalid for std::string members) */
        chat_hist_bytes -= chat_history[0].text.length();
        for (int i = 0; i < chat_hist_cnt - 1; i++) {
            chat_history[i] = chat_history[i + 1];
        }
        chat_hist_cnt--;
    }
    string body = chat_text_trunc(text);
    chat_history[chat_hist_cnt].from_user = from_user;
    chat_history[chat_hist_cnt].text = body;
    chat_hist_cnt++;
    chat_hist_bytes += body.length();
    while (chat_hist_cnt > 1 && chat_hist_bytes > CHAT_HIST_BUDGET) {
        chat_hist_bytes -= chat_history[0].text.length();
        for (int i = 0; i < chat_hist_cnt - 1; i++) {
            chat_history[i] = chat_history[i + 1];
        }
        chat_hist_cnt--;
    }
}

/* Persist the ring to /chat.log via an atomic replace (copilot finding
 * 1.4): write everything to a TEMP file checking every write, append a
 * byte-sum checksum, then rename over the official path - the visible
 * log is always either the complete previous version or the complete new
 * one, never a torn half. The pending (unconfirmed) bubble is NOT
 * persisted (copilot finding 1.5): it would be replayed as a real turn
 * after a reboot. */
static void chat_log_save(void)
{
    if (!chat_spiffs_ok) return;
    File f = SPIFFS.open(CHAT_LOG_TMP, FILE_WRITE);
    if (!f) {
        Serial.println("[AIChat] log save: tmp open failed");
        return;
    }
    uint32_t magic = CHAT_LOG_MAGIC;
    uint32_t sum = 0;
    int n = 0;
    bool ok = (f.write((const uint8_t *)&magic, 4) == 4);
    for (int i = 0; i < chat_hist_cnt && ok; i++) {
        if (i == chat_pending_idx) continue;    /* unconfirmed: skip */
        uint8_t fl = chat_history[i].from_user ? 1 : 0;
        uint16_t len = (uint16_t)chat_history[i].text.length();
        const uint8_t *data = (const uint8_t *)chat_history[i].text.c_str();
        ok = (f.write(&fl, 1) == 1) &&
             (f.write((uint8_t *)&len, 2) == 2) &&
             (f.write(data, len) == len);
        if (!ok) break;
        sum += fl;
        sum += len;
        for (uint16_t k = 0; k < len; k++) sum += data[k];
        n++;
    }
    ok = ok && (f.write((uint8_t *)&sum, 4) == 4);
    f.close();
    if (!ok || !SPIFFS.rename(CHAT_LOG_TMP, CHAT_LOG_PATH)) {
        SPIFFS.remove(CHAT_LOG_TMP);
        Serial.println("[AIChat] log save failed - previous log kept");
        return;
    }
    Serial.printf("[AIChat] log saved: %d msgs\n", n);
}

/* Restore the ring from /chat.log on screen create. A log is accepted
 * ONLY when magic and checksum both match - a torn write from a power
 * loss is discarded whole, never partially replayed (copilot 1.4). */
static void chat_log_load(void)
{
    chat_spiffs_ok = SPIFFS.begin(false);   /* NEVER auto-format on mount trouble (copilot 1.4) */
    if (!chat_spiffs_ok) {
        Serial.println("[AIChat] SPIFFS unavailable - history RAM-only");
        return;
    }
    if (!SPIFFS.exists(CHAT_LOG_PATH)) return;
    File f = SPIFFS.open(CHAT_LOG_PATH, FILE_READ);
    if (!f) return;

    uint32_t magic = 0;
    if (f.available() < 4 || f.read((uint8_t *)&magic, 4) != 4 || magic != CHAT_LOG_MAGIC) {
        f.close();
        Serial.println("[AIChat] log corrupt: bad magic - ignored");
        return;
    }
    char buf[CHAT_MSG_MAX + 1];
    uint32_t sum = 0;
    int loaded = 0;
    while (f.available() >= 3 && chat_hist_cnt < CHAT_HIST_MAX) {
        uint8_t fl = 0;
        uint16_t len = 0;
        f.read(&fl, 1);
        f.read((uint8_t *)&len, 2);
        if (len == 0 || len > CHAT_MSG_MAX || f.available() < len) {
            break;                  /* corrupt tail */
        }
        f.read((uint8_t *)buf, len);
        buf[len] = '\0';
        chat_history_add(fl == 1, buf);
        sum += fl;
        sum += len;
        for (uint16_t k = 0; k < len; k++) sum += (uint8_t)buf[k];
        loaded++;
    }
    uint32_t stored = 0;
    if (f.available() < 4 || f.read((uint8_t *)&stored, 4) != 4 || stored != sum) {
        f.close();
        for (int i = 0; i < chat_hist_cnt; i++) chat_history[i].text.clear();
        chat_hist_cnt = 0;
        chat_hist_bytes = 0;
        Serial.println("[AIChat] log corrupt: checksum mismatch - ignored");
        return;
    }
    f.close();
    Serial.printf("[AIChat] history restored: %d msgs, %d bytes\n",
                  loaded, chat_hist_bytes);
}

/* ---- retry draft persistence (copilot finding 1.5) ---------------------
 * The input draft survives a reboot so a failed message can be retried
 * after power loss; it is cleared on success, on New, and never sent as
 * part of the persisted log. */
static void chat_draft_save(const char *text)
{
    if (!chat_spiffs_ok) return;
    File f = SPIFFS.open(CHAT_DRAFT_PATH, FILE_WRITE);
    if (!f) return;
    f.print(text);
    f.close();
}

static void chat_draft_clear(void)
{
    if (!chat_spiffs_ok) return;
    SPIFFS.remove(CHAT_DRAFT_PATH);
}

static String chat_draft_load(void)
{
    if (!chat_spiffs_ok || !SPIFFS.exists(CHAT_DRAFT_PATH)) return String("");
    File f = SPIFFS.open(CHAT_DRAFT_PATH, FILE_READ);
    if (!f) return String("");
    String s = f.readString();
    f.close();
    return s;
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
        lv_label_set_text(lab, m->text.c_str());
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);

        lv_obj_update_layout(chat_hist_cont);
        y += lv_obj_get_height(box) + 4;
    }
    lv_obj_update_layout(chat_hist_cont);
    lv_obj_scroll_to_y(chat_hist_cont, LV_COORD_MAX, LV_ANIM_OFF);
}

/* ---- async send ------------------------------------------------------- */

/* Async send (review: sync HTTP would freeze the UI for up to 30s).
 * Results travel over a FreeRTOS queue as heap-allocated structs
 * carrying the page generation - no volatile-flag-guarded std::string
 * cross-core access, stale replies of a previous visit are dropped. */
static void chat_send_task_func(void *param)
{
    chat_send_req_t *rq = (chat_send_req_t *)param; /* task-owned snapshot */
    chat_reply_t *res = new chat_reply_t;
    res->gen = rq->gen;
    /* ai_message_t entries point INTO the snapshot-owned strings, so the
     * multi-turn array stays valid for the whole call (finding 1.6) */
    vector<ai_message_t> msgs;
    msgs.reserve(rq->history.size());
    for (size_t i = 0; i < rq->history.size(); i++) {
        ai_message_t m;
        m.role = rq->history[i].first.c_str();
        m.content = rq->history[i].second.c_str();
        msgs.push_back(m);
    }
    res->ok = openai_chat_multi(msgs.data(), (int)msgs.size(),
                                rq->prompt.c_str(), rq->base.c_str(),
                                rq->model.c_str(), rq->key.c_str(),
                                res->reply, 30000);
    delete rq;
    if (s_chat_q) {
        xQueueSend(s_chat_q, &res, portMAX_DELAY);
    } else {
        delete res;
    }
    vTaskDelete(NULL);
}

/* Context copy of a message: UI markers never reach the API (copilot
 * finding 1.5) - the "(truncated)" suffix is stripped here; "(failed)"
 * messages are never selected at all (the pending bubble is skipped). */
static string chat_ctx_body(const chat_msg_t *m)
{
    string s = m->text;
    const size_t mark_len = strlen(CHAT_TRUNC_MARK);
    if (s.length() >= mark_len &&
        s.compare(s.length() - mark_len, mark_len, CHAT_TRUNC_MARK) == 0) {
        s.resize(s.length() - mark_len);
    }
    return s;
}

/* Multi-turn context (round 21, reworked per copilot finding 1.6):
 * messages are selected in whole TURNS - each assistant reply travels
 * with its user question - so the window never starts with an orphan
 * assistant message. A lone CONFIRMED user message at the tail (its
 * reply never arrived) is allowed. Newest-first until CHAT_CTX_BUDGET
 * bytes; stored chronologically. The pending bubble is skipped because
 * its content is exactly the prompt being sent now. */
static void chat_history_snapshot(chat_send_req_t *rq)
{
    struct turn_t { int u; int a; };
    vector<turn_t> turns;
    int budget = CHAT_CTX_BUDGET;
    int i = chat_hist_cnt - 1;
    while (i >= 0 && budget > 0) {
        if (i == chat_pending_idx) { i--; continue; }
        turn_t t = { -1, -1 };
        if (!chat_history[i].from_user) {
            /* assistant: requires its user partner right before it */
            if (i - 1 < 0 || chat_history[i - 1].from_user ||
                i - 1 == chat_pending_idx) {
                break;                  /* orphan assistant - stop the window */
            }
            t.u = i - 1;
            t.a = i;
        } else {
            t.u = i;                    /* lone confirmed user tail */
        }
        int need = (int)chat_history[t.u].text.length() +
                   (t.a >= 0 ? (int)chat_history[t.a].text.length() : 0);
        if (need > budget) break;       /* whole turns only */
        budget -= need;
        turns.push_back(t);
        i = (t.a >= 0 ? t.a : t.u) - 1;
    }
    for (size_t k = turns.size(); k > 0; k--) {
        turn_t &t = turns[k - 1];
        rq->history.push_back(make_pair(string("user"),
                                        chat_ctx_body(&chat_history[t.u])));
        if (t.a >= 0) {
            rq->history.push_back(make_pair(string("assistant"),
                                            chat_ctx_body(&chat_history[t.a])));
        }
    }
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

    /* snapshot prompt + AI config into a task-owned request (finding 1.6);
     * the prompt is copied as a full string - the textarea allows 200
     * chars (up to 800 UTF-8 bytes) and nothing may cut it mid-sequence */
    chat_send_req_t *rq = new chat_send_req_t;
    rq->gen = s_chat_page_gen;
    rq->prompt = prompt;
    char base[160], model[80], key[80];
    openai_load_config(base, sizeof(base), model, sizeof(model), key, sizeof(key));
    rq->base = base;
    rq->model = model;
    rq->key = key;
    if (rq->key.empty()) {
        delete rq;
        lv_label_set_text(chat_status_lab, "No API key - set it in AI Config");
        return;
    }
    chat_history_snapshot(rq);      /* multi-turn context, capped at 8 KB */
    Serial.printf("[AIChat] send: %u context msgs\n", (unsigned)rq->history.size());

    TaskHandle_t h = NULL;
    if (xTaskCreate(chat_send_task_func, "chat_send", 1024 * 8,
                    rq, 1, &h) != pdPASS) {
        delete rq;
        lv_label_set_text(chat_status_lab, "Cannot start task");
        return;
    }
    s_chat_send_busy = true;

    /* copilot 1.7: the bubble is added only AFTER the task really started;
     * a retry REUSES the pending bubble (drop-last + re-add) instead of
     * appending a duplicate. The draft STAYS in the input box until
     * success, so failures can retry with the same bubble. */
    if (chat_pending_idx >= 0) {
        chat_history_drop_last();
        chat_pending_idx = -1;
    }
    chat_history_add(true, prompt);
    chat_pending_idx = chat_hist_cnt - 1;
    chat_history_render();
    chat_log_save();
    lv_label_set_text(chat_status_lab, "Thinking...");
}

/* Mark the pending bubble failed without touching the input draft
 * (copilot 1.7: "mark failed instead of duplicating"). The draft is
 * persisted so the retry survives a reboot (copilot finding 1.5). */
static void chat_mark_pending_failed(void)
{
    if (chat_pending_idx < 0) return;
    string marked = chat_history[chat_pending_idx].text;
    marked += " (failed)";
    chat_history_drop_last();
    chat_pending_idx = -1;
    chat_history_add(true, marked.c_str());
    chat_pending_idx = chat_hist_cnt - 1;
    chat_history_render();
    chat_log_save();                            /* pending bubble is skipped inside */
    chat_draft_save(lv_textarea_get_text(chat_input_ta));
}

/* New-confirm overlay (defined below, after the poll that drives it) */
static lv_obj_t *chat_confirm = NULL;
static void chat_confirm_show(void);
static void chat_confirm_accept(void);
static void chat_confirm_cancel(void);

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
                chat_pending_idx = -1;          /* user message confirmed */
                lv_textarea_set_text(chat_input_ta, "");    /* success: draft consumed */
                chat_draft_clear();             /* retry draft no longer needed */
                chat_history_add(false, cr->reply.c_str());
                chat_history_render();
                chat_log_save();
                lv_label_set_text(chat_status_lab, "");
            } else {
                /* failure: the draft stays in the box for retry, the pending
                 * bubble is marked so the retry visibly reuses it */
                chat_mark_pending_failed();
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

    /* New-confirmation open: Enter = OK, any other key = Cancel
     * (copilot finding 1.7: the overlay must be keyboard-dismissable) */
    if (chat_confirm != NULL) {
        if (c == '\n') {
            chat_confirm_accept();
        } else {
            chat_confirm_cancel();
        }
        return;
    }

    if (c == '\v') return;                      /* volume key */

    if (s_chat_send_busy) return;               /* sending: swallow input */

    if (c == '\t') {                            /* Alt+Enter: New chat (keyboard path) */
        chat_confirm_show();
        return;
    }

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

/* ---- New-chat confirmation overlay (copilot finding 1.7) ----------------
 * New irreversibly destroys the whole history, so it must ask first. The
 * overlay is keyboard-dismissable: Enter = OK, any other key = Cancel.
 * (chat_confirm and the three prototypes live above the poll function.) */
static void chat_clear_history(void);

static void chat_confirm_close(void)
{
    if (chat_confirm) {
        lv_obj_del(chat_confirm);
        chat_confirm = NULL;
    }
}

static void chat_confirm_cancel(void)
{
    chat_confirm_close();
    lv_label_set_text(chat_status_lab, "");
}

static void chat_confirm_accept(void)
{
    chat_confirm_close();
    chat_clear_history();
}

static void chat_confirm_cancel_cb(lv_event_t *e) { chat_confirm_cancel(); }
static void chat_confirm_accept_cb(lv_event_t *e) { chat_confirm_accept(); }

static void chat_confirm_show(void)
{
    if (s_chat_send_busy) {
        lv_label_set_text(chat_status_lab, "Busy - retry after reply");
        return;
    }
    chat_confirm_close();
    chat_confirm = lv_obj_create(lv_layer_top());
    lv_obj_set_size(chat_confirm, 220, 130);
    lv_obj_align(chat_confirm, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(chat_confirm, lv_color_white(), 0);
    lv_obj_set_style_border_width(chat_confirm, 1, 0);
    lv_obj_set_style_border_color(chat_confirm, lv_color_black(), 0);
    lv_obj_set_style_radius(chat_confirm, 6, 0);
    lv_obj_set_style_pad_all(chat_confirm, 8, 0);
    lv_obj_set_flex_flow(chat_confirm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chat_confirm, 6, 0);
    lv_obj_clear_flag(chat_confirm, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = lv_label_create(chat_confirm);
    lv_obj_set_width(body, lv_pct(100));
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, "Start new chat?\nAll history cleared");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(body, 1);

    lv_obj_t *row = lv_obj_create(chat_confirm);
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
    lv_obj_add_event_cb(cancel_btn, chat_confirm_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok_btn = lv_btn_create(row);
    lv_obj_set_flex_grow(ok_btn, 1);
    lv_obj_set_height(ok_btn, 32);
    lv_obj_t *ok_lab = lv_label_create(ok_btn);
    lv_label_set_text(ok_lab, "OK");
    lv_obj_center(ok_lab);
    lv_obj_add_event_cb(ok_btn, chat_confirm_accept_cb, LV_EVENT_CLICKED, NULL);
}

/* Clear the whole history (review finding 1.2 asks for a dedicated entry). */
static void chat_clear_history(void)
{
    if (s_chat_send_busy) {
        lv_label_set_text(chat_status_lab, "Busy - retry after reply");
        return;
    }
    for (int i = 0; i < chat_hist_cnt; i++) {
        chat_history[i].text.clear();
    }
    chat_hist_cnt = 0;
    chat_hist_bytes = 0;
    chat_pending_idx = -1;
    chat_history_render();
    chat_log_save();                            /* truncates /chat.log to empty */
    chat_draft_clear();                         /* the retry draft dies with the session */
    lv_label_set_text(chat_status_lab, "History cleared");
}

static void chat_new_btn_cb(lv_event_t *e)
{
    Serial.println("[AIChat] New button clicked");
    chat_confirm_show();
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

    /* --- history: read-only, scrollable, top 2/3 (160 px of 274) --- */
    chat_hist_cont = lv_obj_create(cont);
    lv_obj_set_width(chat_hist_cont, lv_pct(100));
    lv_obj_set_height(chat_hist_cont, 160);
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
    lv_obj_set_height(input_row, 86);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(input_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input_row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(input_row, 4, LV_PART_MAIN);

    chat_input_ta = lv_textarea_create(input_row);
    lv_obj_set_width(chat_input_ta, 176);
    lv_obj_set_height(chat_input_ta, 86);
    lv_textarea_set_max_length(chat_input_ta, 200);
    lv_textarea_set_placeholder_text(chat_input_ta, "Type here...");
    lv_obj_set_style_text_font(chat_input_ta, &lv_font_montserrat_14, LV_PART_MAIN);

    /* small Send / Clear / New buttons stacked on the side (3 x 26 px) */
    lv_obj_t *btn_col = lv_obj_create(input_row);
    lv_obj_set_width(btn_col, 44);
    lv_obj_set_height(btn_col, 86);
    lv_obj_set_style_bg_opa(btn_col, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_col, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_col, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(btn_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(btn_col, 4, LV_PART_MAIN);

    lv_obj_t *send_btn = lv_btn_create(btn_col);
    lv_obj_set_width(send_btn, 44);
    lv_obj_set_height(send_btn, 26);
    lv_obj_t *send_lab = lv_label_create(send_btn);
    lv_label_set_text(send_lab, "Send");
    lv_obj_center(send_lab);
    lv_obj_add_event_cb(send_btn, chat_send_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_btn = lv_btn_create(btn_col);
    lv_obj_set_width(clear_btn, 44);
    lv_obj_set_height(clear_btn, 26);
    lv_obj_t *clear_lab = lv_label_create(clear_btn);
    lv_label_set_text(clear_lab, "Clear");
    lv_obj_center(clear_lab);
    lv_obj_add_event_cb(clear_btn, chat_clear_btn_cb, LV_EVENT_CLICKED, NULL);

    /* "New" starts a fresh conversation (user request): clears the visible
     * history AND the persisted log; usage counters in ai_stats survive.
     * The click opens a confirmation overlay (copilot finding 1.7). */
    lv_obj_t *new_btn = lv_btn_create(btn_col);
    lv_obj_set_width(new_btn, 44);
    lv_obj_set_height(new_btn, 26);
    lv_obj_t *new_lab = lv_label_create(new_btn);
    lv_label_set_text(new_lab, "New");
    lv_obj_center(new_lab);
    lv_obj_add_event_cb(new_btn, chat_new_btn_cb, LV_EVENT_CLICKED, NULL);

    chat_kbd_active = true;

    /* restore persisted history only when the RAM ring is empty (the ring
     * survives screen switches), then render it (copilot 1.6: re-entering
     * the screen must show the existing history, not a blank area).
     * The retry draft is restored into the input box as well (copilot
     * finding 1.5). */
    if (chat_hist_cnt == 0) {
        chat_log_load();
    }
    String draft = chat_draft_load();
    if (draft.length() > 0) {
        lv_textarea_set_text(chat_input_ta, draft.c_str());
        lv_label_set_text(chat_status_lab, "Retry draft restored");
    }
    chat_history_render();
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
    chat_confirm_close();                       /* no overlay on other screens */
    s_chat_page_gen++;                          /* late replies of this visit are dropped */
    /* safe to clear busy here: the in-flight task owns its own request
     * snapshot (finding 1.6), and its reply will be dropped by gen */
    s_chat_send_busy = false;
    /* the pending bubble stays in history and is persisted, so a retry
     * after re-entering the screen still reuses it */
}

scr_lifecycle_t screen_ai_chat = {
    .create = chat_create,
    .entry = chat_entry,
    .exit  = chat_exit,
    .destroy = chat_destroy,
};
