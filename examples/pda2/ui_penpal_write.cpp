/**
 * @file      ui_penpal_write.cpp
 * @brief     PenPal screen, writing side - COMPOSE + TOPICS pages and the
 *            send pipeline (design: docs/penpal-design.md v3.2 §4.2/§4.3).
 *
 *            COMPOSE textareas are created ONCE at screen create: the draft
 *            survives every internal page roundtrip (TOPICS Pick, PROFILE
 *            View) and screen re-entry - it is RAM-only and intentionally
 *            NOT persisted (§5 R7). Reply entry overwrites the Title with
 *            "Re: <subject>" and records the thread anchor; the body stays
 *            as-is (thin-client choice, §4.2).
 *
 *            Send pipeline (§3.2 v3.1/v3.2):
 *              - gates: non-empty title, body >= 50 CHARS (pp_utf8_count)
 *              - payload built as canonical pp_send_req_t (std::string)
 *              - idempotency key: reused when the payload equals the stored
 *                snapshot (retry-safe replay), regenerated on ANY change;
 *                voided on confirmed success; RAM-only
 *              - SEND in flight: Title/Body/Pick/Tips/Send disabled (edit
 *                lock); the waitbox Close = background continue does NOT
 *                unlock; the result consumption compares the payload
 *                snapshot before clearing COMPOSE (double insurance)
 *              - Title limit: 56 BYTES at a UTF-8 boundary so
 *                "Re: " + 56 <= 60 <= subject[64] (§4.2)
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "ui_penpal.h"
#include "ui_scr_mrg.h"
#include "src/assets.h"
#include <esp_random.h>

/* ui_deckpro.cpp keeps this macro private - mirror it (same assets font) */
#define FONT_BOLD_MONO_SIZE_15 &Font_Mono_Bold_15

/* ---- COMPOSE widgets (created once; the draft lives in them) ------------- */
static lv_obj_t *s_comp_page = NULL;
static lv_obj_t *s_comp_title_lab = NULL;    /* back-bar title label */
static lv_obj_t *s_to_lab = NULL;
static lv_obj_t *s_topic_lab = NULL;         /* "Topic: ..." (new mode) */
static lv_obj_t *s_title_ta = NULL;
static lv_obj_t *s_body_ta = NULL;
static lv_obj_t *s_send_btn = NULL;
static lv_obj_t *s_tips_btn = NULL;
static lv_obj_t *s_pick_btn = NULL;
static lv_obj_t *s_count_lab = NULL;
static bool s_focus_title = false;           /* false = Body (default) */

/* ---- TOPICS widgets ------------------------------------------------------ */
static lv_obj_t *s_top_page = NULL;
static lv_obj_t *s_top_btn[6];
static lv_obj_t *s_top_lab[6];
static lv_obj_t *s_top_nav = NULL;
static lv_obj_t *s_top_prev = NULL;
static lv_obj_t *s_top_next = NULL;
/* suggestion overlay (own buttons - not the simple notice msgbox) */
static lv_obj_t *s_top_box = NULL;
static int s_top_box_idx = -1;

#define PPW_TITLE_BYTES 56                    /* "Re: "+56 = 60 <= 64 (§4.2) */

/* ---- helpers -------------------------------------------------------------- */

/* Title byte clamp at a UTF-8 code-point boundary (ui_ai_chat.cpp pattern). */
static void ppw_title_clamp(void)
{
    const char *t = lv_textarea_get_text(s_title_ta);
    if (!t) return;
    int len = (int)strlen(t);
    if (len <= PPW_TITLE_BYTES) return;
    int cut = PPW_TITLE_BYTES;
    while (cut > 0 && ((uint8_t)t[cut] & 0xC0) == 0x80) cut--;
    char buf[64];
    memcpy(buf, t, cut);
    buf[cut] = 0;
    lv_textarea_set_text(s_title_ta, buf);
}

/* "<title bytes>/56 bytes | <body chars> chars" - both budgets are visible
 * at once, no focus dependency (§4.2 label, chars per kimi §1.11.3) */
static void ppw_count_update(void)
{
    const char *t = lv_textarea_get_text(s_title_ta);
    const char *b = lv_textarea_get_text(s_body_ta);
    lv_label_set_text_fmt(s_count_lab, "%d/%d bytes | %d chars",
                          t ? (int)strlen(t) : 0, PPW_TITLE_BYTES,
                          b ? pp_utf8_count(b) : 0);
}

/* payload equality - the idempotency snapshot compare (§3.2) */
static bool ppw_payload_eq(const pp_send_req_t *a, const pp_send_req_t *b)
{
    return a->pen_pal_id == b->pen_pal_id &&
           a->has_topic == b->has_topic &&
           (!a->has_topic || a->topic_id == b->topic_id) &&
           a->has_thread_root == b->has_thread_root &&
           (!a->has_thread_root || a->thread_root_id == b->thread_root_id) &&
           a->subject == b->subject &&
           a->content == b->content;
}

/* Build the canonical payload from the CURRENT UI + state. */
static void ppw_payload_build(pp_send_req_t *out)
{
    memset(out, 0, sizeof(*out));
    const char *t = lv_textarea_get_text(s_title_ta);
    const char *b = lv_textarea_get_text(s_body_ta);
    out->pen_pal_id = pp.comp_pal_id;
    out->subject = t ? t : "";
    out->has_topic = pp.comp_has_topic;
    out->topic_id = pp.comp_topic_id;
    out->has_thread_root = pp.reply_mode && pp.comp_root_id > 0;
    out->thread_root_id = pp.comp_root_id;
    out->content = b ? b : "";
}

/* ---- edit lock (§3.2 v3.1 P1) -------------------------------------------- */
void ppw_lock_edit(bool lock)
{
    pp.send_lock = lock;
    lv_obj_t *objs[] = { s_title_ta, s_body_ta, s_pick_btn, s_tips_btn,
                         s_send_btn };
    for (unsigned i = 0; i < sizeof(objs) / sizeof(objs[0]); i++) {
        if (!objs[i]) continue;
        if (lock) lv_obj_add_state(objs[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(objs[i], LV_STATE_DISABLED);
    }
}

/* ---- COMPOSE apply-state (every entry / page return) --------------------- */
void ppw_show_compose(void)
{
    if (!s_comp_page) return;
    lv_label_set_text(s_comp_title_lab, pp.reply_mode ? "Reply" : "Write");
    snprintf(pp.fmt, sizeof(pp.fmt), "To: %s",
             pp.comp_pal_name[0] ? pp.comp_pal_name : "-");
    lv_label_set_text(s_to_lab, pp.fmt);

    /* Topic row: new mode only; Tips: reply mode only (§4.2) */
    if (pp.reply_mode) {
        lv_obj_add_flag(s_topic_lab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pick_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_tips_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_topic_lab, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pick_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_tips_btn, LV_OBJ_FLAG_HIDDEN);
        snprintf(pp.fmt, sizeof(pp.fmt), "Topic: %s",
                 pp.comp_has_topic ? pp.comp_topic_title : "(none)");
        lv_label_set_text(s_topic_lab, pp.fmt);
    }
    /* nothing in flight (anymore) -> make sure editing is possible */
    if (!s_pp_busy || !pp.send_lock) {
        if (pp.send_lock) ppw_lock_edit(false);
    }
    ppw_count_update();
}

/* THREAD "Reply" -> COMPOSE(reply): anchor + "Re: " title (§2 ⑦ double
 * anchor with the body's thread_root_id). The body is deliberately kept. */
void ppw_open_reply(void)
{
    pp.reply_mode = true;
    pp.comp_pal_id = pp.thr_pal;
    pp.comp_pal_name[0] = 0;
    for (int i = 0; i < pp.pals_cnt; i++) {
        if (pp.pals[i].id == pp.thr_pal) {
            strlcpy(pp.comp_pal_name, pp.pals[i].name,
                    sizeof(pp.comp_pal_name));
            break;
        }
    }
    if (!pp.comp_pal_name[0]) {
        snprintf(pp.comp_pal_name, sizeof(pp.comp_pal_name), "pal #%d",
                 pp.thr_pal);
    }
    pp.comp_root_id = pp.thr_root;
    snprintf(pp.fmt, sizeof(pp.fmt), "Re: %s", pp.thr_subject);
    lv_textarea_set_text(s_title_ta, pp.fmt);
    ppw_title_clamp();
    pp_set_page(PP_PAGE_COMPOSE);
}

/* ---- send pipeline -------------------------------------------------------- */

void ppw_on_send_result(const pp_result_t *res)
{
    if (!res->ok) {
        /* failure/timeout: draft kept + unlocked + key kept - a retry of the
         * unchanged payload replays the same key (§3.2) */
        if (pp.send_lock) ppw_lock_edit(false);
        pp_status_set("send failed: %s", res->err.c_str());
        return;
    }
    /* confirmed success: void the key and clear COMPOSE only when the UI
     * still holds THIS payload (lock makes equality guaranteed; compare is
     * the double insurance against future bypass edits, §3.2) */
    pp_send_req_t cur;
    ppw_payload_build(&cur);
    pp.idem_valid = false;
    if (ppw_payload_eq(&cur, &pp.idem_snap)) {
        lv_textarea_set_text(s_title_ta, "");
        lv_textarea_set_text(s_body_ta, "");
        pp.comp_has_topic = false;
        pp.comp_topic_id = 0;
        pp.comp_topic_title[0] = 0;
        if (pp.send_lock) ppw_lock_edit(false);
        ppw_count_update();
        pp_home_note("sent ok%s", res->replayed ? " (replayed)" : "");
        pp_set_page(PP_PAGE_HOME);
        pp_home_sync(false);              /* auto sync keeps the note */
    } else {
        /* a DIFFERENT draft is on screen - keep it, just report */
        if (pp.send_lock) ppw_lock_edit(false);
        pp_home_note("previous send ok%s", res->replayed ? " (replayed)" : "");
        pp_status_set("previous send ok - draft kept");
    }
}

bool ppw_send_in_background_hint(void)
{
    pp_status_set("sending in background...");
    return true;
}

static void ppw_send_click(void)
{
    if (s_pp_busy) {
        pp_status_set("busy - wait for current request");
        return;
    }
    const char *title = lv_textarea_get_text(s_title_ta);
    const char *body = lv_textarea_get_text(s_body_ta);
    if (!title || !title[0]) {
        pp_status_set("title is empty");
        return;
    }
    int chars = pp_utf8_count(body ? body : "");
    if (chars < 50) {
        pp_status_set("Need 50+ chars (now %d)", chars);
        return;
    }

    pp_send_req_t payload;
    ppw_payload_build(&payload);

    /* idempotency key lifecycle: unchanged retry reuses the key (replay
     * safe), any change regenerates it (§2.2/§3.2) */
    if (!pp.idem_valid || !ppw_payload_eq(&payload, &pp.idem_snap)) {
        penpal_new_idem_key(pp.idem_key);
        pp.idem_snap = payload;
        pp.idem_valid = true;
    }

    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_SEND;
    strlcpy(rq.idem_key, pp.idem_key, sizeof(rq.idem_key));
    rq.send = payload;
    if (!pp_start(&rq, false)) return;    /* status set by pp_start; key kept */

    ppw_lock_edit(true);                  /* edit lock for the whole flight */
    ppw_count_update();
    pp_status_set("sending...");
}

static void ppw_send_cb(lv_event_t *e) { ppw_send_click(); }

/* Tips (reply only): latest incoming letter, POST /emails/{id}/tips */
static void ppw_tips_click(void)
{
    if (s_pp_busy) {
        pp_status_set("busy - wait for current request");
        return;
    }
    int email_id = 0;
    for (int i = 0; i < pp.letters_cnt; i++) {
        if (!pp.letters[i].mine) {        /* newest-first: [0] = newest */
            email_id = pp.letters[i].id;
            break;
        }
    }
    if (!email_id) {
        pp_msgbox_show("Tips", "No incoming letter yet");
        return;
    }
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_TIPS;
    rq.email_id = email_id;
    pp_start(&rq, false);
}

static void ppw_tips_cb(lv_event_t *e) { ppw_tips_click(); }

/* TOPICS fetch on every Pick (fresh suggestions, §4.3) */
static void ppw_pick_click(void)
{
    if (s_pp_busy) {
        pp_status_set("busy - wait for current request");
        return;
    }
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_TOPICS;
    pp_start(&rq, false);
}

static void ppw_pick_cb(lv_event_t *e) { ppw_pick_click(); }

static void ppw_view_cb(lv_event_t *e) { pp_set_page(PP_PAGE_PROFILE); }

static void ppw_back_cb(lv_event_t *e)
{
    /* touch back always returns; the draft lives on in the textareas */
    pp_set_page(PP_PAGE_HOME);
}

/* ---- TOPICS page ----------------------------------------------------------- */

void ppw_render_topics(void)
{
    if (!s_top_page) return;
    int pages = (pp.topics_cnt + 5) / 6;
    if (pp.topics_page >= pages && pages > 0) pp.topics_page = pages - 1;
    for (int i = 0; i < 6; i++) {
        int idx = pp.topics_page * 6 + i;
        if (idx >= pp.topics_cnt) {
            lv_obj_add_flag(s_top_btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_top_btn[i], LV_OBJ_FLAG_HIDDEN);
        snprintf(pp.fmt, sizeof(pp.fmt), "%s  [%s]", pp.topics[idx].title,
                 pp.topics[idx].tag);
        lv_label_set_text(s_top_lab[i], pp.fmt);
        lv_obj_set_user_data(s_top_btn[i], (void *)(intptr_t)idx);
        /* selected-row highlight (position-stable border, §4.3) */
        if (idx == pp.topics_sel) {
            lv_obj_set_style_border_width(s_top_btn[i], 2, 0);
        } else {
            lv_obj_set_style_border_width(s_top_btn[i], 1, 0);
        }
    }
    if (pages > 1) {
        lv_obj_clear_flag(s_top_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_top_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_top_next, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_top_nav, "page %d/%d", pp.topics_page + 1,
                              pages);
    } else {
        lv_obj_add_flag(s_top_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_top_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_top_next, LV_OBJ_FLAG_HIDDEN);
    }
}

/* suggestion overlay: title + background + guiding questions + Use/Cancel.
 * Keyboard: Enter = Use, any other key = Cancel (chat confirm semantics). */
static void ppw_top_use(void);
static void ppw_top_cancel(void);

static void ppw_top_use_cb(lv_event_t *e) { ppw_top_use(); }
static void ppw_top_cancel_cb(lv_event_t *e) { ppw_top_cancel(); }

static void ppw_top_box_show(int idx)
{
    if (idx < 0 || idx >= pp.topics_cnt) return;
    ppw_overlays_close();
    s_top_box_idx = idx;
    const pp_topic_t *t = &pp.topics[idx];

    s_top_box = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_top_box, 224, 220);
    lv_obj_align(s_top_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_top_box, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_top_box, 1, 0);
    lv_obj_set_style_border_color(s_top_box, lv_color_black(), 0);
    lv_obj_set_style_radius(s_top_box, 6, 0);
    lv_obj_set_style_pad_all(s_top_box, 8, 0);
    lv_obj_set_flex_flow(s_top_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_top_box, 4, 0);

    lv_obj_t *title = lv_label_create(s_top_box);
    lv_obj_set_width(title, lv_pct(100));
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title, t->title);
    lv_obj_set_style_text_font(title, FONT_BOLD_MONO_SIZE_15, 0);

    lv_obj_t *body = lv_label_create(s_top_box);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_height(body, 118);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    snprintf(pp.fmt, sizeof(pp.fmt), "%s\n\nGuiding questions:\n%s",
             t->background, t->guiding);
    lv_label_set_text(body, pp.fmt);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(body, 1);

    lv_obj_t *row = lv_obj_create(s_top_box);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t *cancel = lv_btn_create(row);
    lv_obj_set_flex_grow(cancel, 1);
    lv_obj_set_height(cancel, 32);
    lv_obj_t *clab = lv_label_create(cancel);
    lv_label_set_text(clab, "Cancel");
    lv_obj_center(clab);
    lv_obj_add_event_cb(cancel, ppw_top_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *use = lv_btn_create(row);
    lv_obj_set_flex_grow(use, 1);
    lv_obj_set_height(use, 32);
    lv_obj_t *ulab = lv_label_create(use);
    lv_label_set_text(ulab, "Use");
    lv_obj_center(ulab);
    lv_obj_add_event_cb(use, ppw_top_use_cb, LV_EVENT_CLICKED, NULL);
}

static void ppw_top_use(void)
{
    if (s_top_box && s_top_box_idx >= 0 && s_top_box_idx < pp.topics_cnt) {
        const pp_topic_t *t = &pp.topics[s_top_box_idx];
        pp.comp_has_topic = true;
        pp.comp_topic_id = t->id;
        strlcpy(pp.comp_topic_title, t->title, sizeof(pp.comp_topic_title));
        pp.topics_sel = s_top_box_idx;
        /* Title auto-fill (editable afterwards, §4.3) */
        lv_textarea_set_text(s_title_ta, t->title);
        ppw_title_clamp();
        ppw_count_update();
    }
    ppw_overlays_close();
    pp_set_page(PP_PAGE_COMPOSE);
}

static void ppw_top_cancel(void)
{
    pp.topics_sel = -1;
    ppw_overlays_close();
    ppw_render_topics();
    ui_disp_full_refr();
}

void ppw_overlays_close(void)
{
    if (s_top_box) {
        lv_obj_del(s_top_box);
        s_top_box = NULL;
        s_top_box_idx = -1;
    }
}

static void ppw_top_row_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    ppw_top_box_show(idx);
}

static void ppw_top_prev_cb(lv_event_t *e)
{
    if (pp.topics_page > 0) {
        pp.topics_page--;
        ppw_render_topics();
        ui_disp_full_refr();
    }
}

static void ppw_top_next_cb(lv_event_t *e)
{
    int pages = (pp.topics_cnt + 5) / 6;
    if (pp.topics_page + 1 < pages) {
        pp.topics_page++;
        ppw_render_topics();
        ui_disp_full_refr();
    }
}

static void ppw_top_back_cb(lv_event_t *e)
{
    /* not selecting a topic is allowed - the draft is untouched */
    pp_set_page(PP_PAGE_COMPOSE);
}

/* ---- keyboard (COMPOSE + TOPICS) ------------------------------------------ */
static void ppw_compose_key(char c)
{
    lv_obj_t *ta = s_focus_title ? s_title_ta : s_body_ta;
    if (pp.send_lock) {
        /* locked while SEND is in flight: only the empty-back escape and
         * the focus toggle stay alive (typing is refused by the disabled
         * textareas anyway - this is the keypad-side mirror) */
        if (c == '\b') {
            const char *t = lv_textarea_get_text(s_title_ta);
            const char *b = lv_textarea_get_text(s_body_ta);
            if ((!t || !t[0]) && (!b || !b[0])) pp_set_page(PP_PAGE_HOME);
        }
        return;
    }
    if (c == '\t') {
        s_focus_title = !s_focus_title;
        return;
    }
    if (c == '\n') {
        if (!s_focus_title) lv_textarea_add_char(s_body_ta, '\n');
        return;                            /* one-line Title ignores Enter */
    }
    if (c == '\b') {
        const char *txt = lv_textarea_get_text(ta);
        if (txt && txt[0] != '\0') {
            lv_textarea_del_char(ta);
        } else {
            const char *t = lv_textarea_get_text(s_title_ta);
            const char *b = lv_textarea_get_text(s_body_ta);
            if ((!t || !t[0]) && (!b || !b[0])) pp_set_page(PP_PAGE_HOME);
        }
        ppw_count_update();
        return;
    }
    if (c >= 0x20) {
        lv_textarea_add_char(ta, c);
        if (s_focus_title) ppw_title_clamp();
        ppw_count_update();
    }
}

static void ppw_topics_key(char c)
{
    if (s_top_box) {
        /* suggestion overlay: Enter = Use, any other key = Cancel (§4.3) */
        if (c == '\n') ppw_top_use();
        else ppw_top_cancel();
        return;
    }
    if (c == '+' || c == '-') {
        int pages = (pp.topics_cnt + 5) / 6;
        if (c == '+' && pp.topics_page + 1 < pages) pp.topics_page++;
        else if (c == '-' && pp.topics_page > 0) pp.topics_page--;
        else return;
        ppw_render_topics();
        ui_disp_full_refr();
    } else if (c == '\b') {
        pp_set_page(PP_PAGE_COMPOSE);
    }
}

void ppw_key(char c)
{
    if (pp_get_page() == PP_PAGE_COMPOSE) ppw_compose_key(c);
    else ppw_topics_key(c);
}

/* ---- builders -------------------------------------------------------------- */
static void ppw_compose_build(lv_obj_t *parent)
{
    s_comp_page = lv_obj_create(parent);
    lv_obj_set_size(s_comp_page, 240, 320);
    lv_obj_set_pos(s_comp_page, 0, 0);
    lv_obj_set_style_bg_opa(s_comp_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_comp_page, 0, 0);
    lv_obj_set_style_pad_all(s_comp_page, 0, 0);
    lv_obj_clear_flag(s_comp_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_comp_page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_COMPOSE, s_comp_page);

    s_comp_title_lab = scr_back_btn_create(s_comp_page, "Write", ppw_back_cb);

    s_to_lab = lv_label_create(s_comp_page);
    lv_obj_align(s_to_lab, LV_ALIGN_TOP_LEFT, 6, 42);
    lv_label_set_text(s_to_lab, "To: -");
    lv_obj_set_style_text_font(s_to_lab, &lv_font_montserrat_14, 0);

    lv_obj_t *view_btn = lv_btn_create(s_comp_page);
    lv_obj_set_size(view_btn, 44, 24);
    lv_obj_align(view_btn, LV_ALIGN_TOP_RIGHT, -4, 40);
    lv_obj_t *vl = lv_label_create(view_btn);
    lv_label_set_text(vl, "View");
    lv_obj_center(vl);
    lv_obj_add_event_cb(view_btn, ppw_view_cb, LV_EVENT_CLICKED, NULL);

    s_topic_lab = lv_label_create(s_comp_page);
    lv_obj_align(s_topic_lab, LV_ALIGN_TOP_LEFT, 6, 62);
    lv_obj_set_width(s_topic_lab, 176);
    lv_label_set_long_mode(s_topic_lab, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_topic_lab, "Topic: (none)");
    lv_obj_set_style_text_font(s_topic_lab, &lv_font_montserrat_14, 0);

    s_pick_btn = lv_btn_create(s_comp_page);
    lv_obj_set_size(s_pick_btn, 44, 24);
    lv_obj_align(s_pick_btn, LV_ALIGN_TOP_RIGHT, -4, 60);
    lv_obj_t *pl = lv_label_create(s_pick_btn);
    lv_label_set_text(pl, "Pick");
    lv_obj_center(pl);
    lv_obj_add_event_cb(s_pick_btn, ppw_pick_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tl = lv_label_create(s_comp_page);
    lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 6, 88);
    lv_label_set_text(tl, "Title:");
    lv_obj_set_style_text_font(tl, &lv_font_montserrat_14, 0);

    s_title_ta = lv_textarea_create(s_comp_page);
    lv_obj_set_size(s_title_ta, 228, 32);
    lv_obj_align(s_title_ta, LV_ALIGN_TOP_MID, 0, 104);
    lv_textarea_set_max_length(s_title_ta, 63);
    lv_textarea_set_one_line(s_title_ta, true);
    lv_obj_set_style_text_font(s_title_ta, &lv_font_montserrat_14, 0);

    s_body_ta = lv_textarea_create(s_comp_page);
    lv_obj_set_size(s_body_ta, 180, 156);
    lv_obj_align(s_body_ta, LV_ALIGN_TOP_LEFT, 6, 142);
    lv_textarea_set_max_length(s_body_ta, 1000);
    lv_textarea_set_placeholder_text(s_body_ta, "Dear ...");
    lv_obj_set_style_text_font(s_body_ta, &lv_font_montserrat_14, 0);

    s_send_btn = lv_btn_create(s_comp_page);
    lv_obj_set_size(s_send_btn, 50, 76);
    lv_obj_align(s_send_btn, LV_ALIGN_TOP_RIGHT, -4, 142);
    lv_obj_t *sl = lv_label_create(s_send_btn);
    lv_label_set_text(sl, "Send");
    lv_obj_center(sl);
    lv_obj_add_event_cb(s_send_btn, ppw_send_cb, LV_EVENT_CLICKED, NULL);

    s_tips_btn = lv_btn_create(s_comp_page);
    lv_obj_set_size(s_tips_btn, 50, 76);
    lv_obj_align(s_tips_btn, LV_ALIGN_TOP_RIGHT, -4, 222);
    lv_obj_t *tgl = lv_label_create(s_tips_btn);
    lv_label_set_text(tgl, "Tips");
    lv_obj_center(tgl);
    lv_obj_add_event_cb(s_tips_btn, ppw_tips_cb, LV_EVENT_CLICKED, NULL);

    s_count_lab = lv_label_create(s_comp_page);
    lv_obj_align(s_count_lab, LV_ALIGN_TOP_LEFT, 6, 302);
    lv_label_set_text(s_count_lab, "0/56 bytes | 0 chars");
    lv_obj_set_style_text_font(s_count_lab, &lv_font_montserrat_14, 0);

    lv_obj_t *status = lv_label_create(s_comp_page);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 130, 302);
    lv_obj_set_width(status, 106);
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_palette_main(LV_PALETTE_GREY), 0);
    pp_status_register(PP_PAGE_COMPOSE, status);
}

static void ppw_topics_build(lv_obj_t *parent)
{
    s_top_page = lv_obj_create(parent);
    lv_obj_set_size(s_top_page, 240, 320);
    lv_obj_set_pos(s_top_page, 0, 0);
    lv_obj_set_style_bg_opa(s_top_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_top_page, 0, 0);
    lv_obj_set_style_pad_all(s_top_page, 0, 0);
    lv_obj_clear_flag(s_top_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_top_page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_TOPICS, s_top_page);

    scr_back_btn_create(s_top_page, "Topics", ppw_top_back_cb);

    for (int i = 0; i < 6; i++) {
        s_top_btn[i] = lv_btn_create(s_top_page);
        lv_obj_set_size(s_top_btn[i], 232, 34);
        lv_obj_align(s_top_btn[i], LV_ALIGN_TOP_MID, 0, 38 + i * 36);
        lv_obj_set_style_pad_all(s_top_btn[i], 2, 0);
        s_top_lab[i] = lv_label_create(s_top_btn[i]);
        lv_obj_align(s_top_lab[i], LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_width(s_top_lab[i], 226);
        lv_label_set_long_mode(s_top_lab[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_top_lab[i], "");
        lv_obj_set_style_text_font(s_top_lab[i], &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(s_top_btn[i], ppw_top_row_cb, LV_EVENT_CLICKED,
                            NULL);
        lv_obj_add_flag(s_top_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_top_prev = lv_btn_create(s_top_page);
    lv_obj_set_size(s_top_prev, 54, 26);
    lv_obj_align(s_top_prev, LV_ALIGN_BOTTOM_LEFT, 4, -6);
    lv_obj_t *pl = lv_label_create(s_top_prev);
    lv_label_set_text(pl, "< Prev");
    lv_obj_center(pl);
    lv_obj_add_event_cb(s_top_prev, ppw_top_prev_cb, LV_EVENT_CLICKED, NULL);

    s_top_next = lv_btn_create(s_top_page);
    lv_obj_set_size(s_top_next, 54, 26);
    lv_obj_align(s_top_next, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
    lv_obj_t *nl = lv_label_create(s_top_next);
    lv_label_set_text(nl, "Next >");
    lv_obj_center(nl);
    lv_obj_add_event_cb(s_top_next, ppw_top_next_cb, LV_EVENT_CLICKED, NULL);

    s_top_nav = lv_label_create(s_top_page);
    lv_obj_align(s_top_nav, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s_top_nav, "page 1/1");
    lv_obj_set_style_text_font(s_top_nav, &lv_font_montserrat_14, 0);

    lv_obj_add_flag(s_top_nav, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_top_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_top_next, LV_OBJ_FLAG_HIDDEN);
}

void ppw_build(lv_obj_t *parent)
{
    ppw_compose_build(parent);
    ppw_topics_build(parent);
    ppw_count_update();
}
