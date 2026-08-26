/**
 * @file      ui_penpal_read.cpp
 * @brief     PenPal screen, reading side - THREAD, FB (fix/polish result)
 *            and PROFILE pages (design: docs/penpal-design.md v3.2
 *            §4.4/§4.5/§4.6).
 *
 *            THREAD shows one letter per page, NEWEST first (index 0).
 *            Fix/Polish are position-stable and DISABLED on letters that
 *            are not mine (kimi v2 §3.1); Reply exists only on page 0 of a
 *            BOUND thread. R9 (2026-08-22): residual threads of a deleted
 *            pen-pal (pal_id == 0 sentinel) open read-only - the header
 *            carries the "pal removed - read only" hint and Reply stays
 *            hidden; Fix/Polish on my own letters remain available (the
 *            server corrects by email id).
 *
 *            FB renders text that the worker task pre-formatted (§4.5);
 *            the degraded flag suffixes the title. Both THREAD body and FB
 *            use the chat-style scroll: touch scrolling suppresses EPD
 *            writes, one full refresh on release; +/- scrolls by keyboard.
 *
 *            PROFILE is synthesized client-side (R2): pal record from
 *            pen-pals + thread count / unread sum from the mailbox rows.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "ui_penpal.h"
#include "ui_scr_mrg.h"
#include "src/assets.h"

/* ui_deckpro.cpp keeps this macro private - mirror it (same assets font) */
#define FONT_BOLD_MONO_SIZE_15 &Font_Mono_Bold_15

/* ---- THREAD widgets ------------------------------------------------------ */
static lv_obj_t *s_thr_page = NULL;
static lv_obj_t *s_thr_start = NULL;
static lv_obj_t *s_thr_prev = NULL;
static lv_obj_t *s_thr_next = NULL;
static lv_obj_t *s_thr_count = NULL;      /* "2/5" (+ dropped note) */
static lv_obj_t *s_thr_head = NULL;       /* From:/To: + time (+ R9 hint) */
static lv_obj_t *s_thr_body = NULL;       /* scroll container */
static lv_obj_t *s_thr_body_lab = NULL;
static lv_obj_t *s_thr_fix = NULL;
static lv_obj_t *s_thr_polish = NULL;
static lv_obj_t *s_thr_reply = NULL;

/* ---- FB widgets ----------------------------------------------------------- */
static lv_obj_t *s_fb_page = NULL;
static lv_obj_t *s_fb_title = NULL;
static lv_obj_t *s_fb_body = NULL;
static lv_obj_t *s_fb_lab = NULL;

/* ---- PROFILE widgets ------------------------------------------------------- */
static lv_obj_t *s_pro_page = NULL;
static lv_obj_t *s_pro_lab = NULL;

/* ---- scroll suppress/flush (chat pattern) ---------------------------------- */
static void ppr_scroll_begin_cb(lv_event_t *e)
{
    if (lv_event_get_indev(e) != NULL) {
        ui_disp_suppress_flush(true);
    }
}

static void ppr_scroll_end_cb(lv_event_t *e)
{
    if (lv_event_get_indev(e) != NULL) {
        ui_disp_suppress_flush(false);
        ui_disp_full_refr();
    }
}

/* ---- THREAD ---------------------------------------------------------------- */

/* created_at "2026-08-20T14:11:11" -> "08-20 14:11" (server time = local) */
static void ppr_when(const char *iso, char *out, int out_len)
{
    out[0] = 0;
    if (strlen(iso) >= 16) {
        memcpy(out, iso + 5, 11);
        out[11] = 0;
    }
}

void ppr_show_thread(void)
{
    if (!s_thr_page || pp.letters_cnt <= 0) return;
    if (pp.thr_idx < 0) pp.thr_idx = 0;
    if (pp.thr_idx >= pp.letters_cnt) pp.thr_idx = pp.letters_cnt - 1;
    const pp_letter_t *l = &pp.letters[pp.thr_idx];

    /* header: my letters show the receiver (this THREAD's pal, not the
     * COMPOSE recipient), incoming show the sender */
    char when[12];
    ppr_when(l->time, when, sizeof(when));
    if (l->mine) {
        const char *to = "pen pal";
        for (int i = 0; i < pp.pals_cnt; i++) {
            if (pp.pals[i].id == pp.thr_pal) {
                to = pp.pals[i].name;
                break;
            }
        }
        snprintf(pp.fmt, sizeof(pp.fmt), "To: %s", to);
    } else {
        snprintf(pp.fmt, sizeof(pp.fmt), "From: %s",
                 l->sender[0] ? l->sender : "pen pal");
    }
    if (pp.thr_pal == 0) {
        /* R9 residual thread: read-only form (§4.4) */
        snprintf(pp.fmt + strlen(pp.fmt),
                 sizeof(pp.fmt) - strlen(pp.fmt),
                 "\npal removed - read only");
    }
    if (when[0]) {
        snprintf(pp.fmt + strlen(pp.fmt),
                 sizeof(pp.fmt) - strlen(pp.fmt), "   %s", when);
    }
    if (pp.thr_dropped > 0) {
        /* dropped note lives in the header, not the counter - the counter
         * stays short so it never runs into the Sync button (2026-08-26) */
        snprintf(pp.fmt + strlen(pp.fmt),
                 sizeof(pp.fmt) - strlen(pp.fmt),
                 "\n%d old dropped (size limit)", pp.thr_dropped);
    }
    lv_label_set_text(s_thr_head, pp.fmt);

    lv_label_set_text(s_thr_body_lab, l->content.c_str());
    lv_obj_scroll_to_y(s_thr_body, 0, LV_ANIM_OFF);

    /* counter (short form only; dropped note moved into the header) */
    lv_label_set_text_fmt(s_thr_count, "%d/%d", pp.thr_idx + 1, pp.letters_cnt);

    /* nav: index 0 = newest, so "older" grows the index (§4.4) */
    if (pp.thr_idx == 0) lv_obj_add_state(s_thr_start, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_thr_start, LV_STATE_DISABLED);
    if (pp.thr_idx == 0) lv_obj_add_state(s_thr_next, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_thr_next, LV_STATE_DISABLED);
    if (pp.thr_idx >= pp.letters_cnt - 1)
        lv_obj_add_state(s_thr_prev, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_thr_prev, LV_STATE_DISABLED);

    /* Fix/Polish: my letters only; position-stable disabled state */
    if (l->mine) {
        lv_obj_clear_state(s_thr_fix, LV_STATE_DISABLED);
        lv_obj_clear_state(s_thr_polish, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_thr_fix, LV_STATE_DISABLED);
        lv_obj_add_state(s_thr_polish, LV_STATE_DISABLED);
    }

    /* Reply: newest page of a BOUND thread only (R9 residual has none) */
    if (pp.thr_idx == 0 && pp.thr_pal > 0) {
        lv_obj_clear_flag(s_thr_reply, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_thr_reply, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ppr_thr_start_cb(lv_event_t *e)
{
    pp.thr_idx = 0;
    ppr_show_thread();
    ui_disp_full_refr();
}

static void ppr_thr_prev_cb(lv_event_t *e)
{
    if (pp.thr_idx < pp.letters_cnt - 1) {
        pp.thr_idx++;
        ppr_show_thread();
        ui_disp_full_refr();
    }
}

static void ppr_thr_next_cb(lv_event_t *e)
{
    if (pp.thr_idx > 0) {
        pp.thr_idx--;
        ppr_show_thread();
        ui_disp_full_refr();
    }
}

/* In-page thread refresh (product request 2026-08-26): force a network
 * re-fetch of THIS thread; the worker overwrites the thread cache and the
 * PP_RES_THREAD consumer re-renders the page (see ui_penpal.cpp). */
static void ppr_thr_sync_cb(lv_event_t *e)
{
    (void)e;
    if (s_pp_busy) return;                 /* pp_start would reject anyway */
    if (pp.thr_root <= 0) return;
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_THREAD;
    rq.pen_pal_id = pp.thr_pal;
    rq.thread_root_id = pp.thr_root;
    if (pp_start(&rq, false)) {
        pp_status_set("refreshing thread...");
    }
}

static void ppr_feedback_click(int type)
{
    if (s_pp_busy || pp.letters_cnt <= 0) return;
    const pp_letter_t *l = &pp.letters[pp.thr_idx];
    if (!l->mine) return;                 /* buttons are disabled anyway */
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = type;
    rq.email_id = l->id;
    pp_start(&rq, false);                 /* errors surface via the framework */
}

static void ppr_fix_cb(lv_event_t *e) { ppr_feedback_click(PP_RES_FIX); }

static void ppr_polish_cb(lv_event_t *e) { ppr_feedback_click(PP_RES_POLISH); }

static void ppr_reply_cb(lv_event_t *e) { ppw_open_reply(); }

static void ppr_thr_back_cb(lv_event_t *e) { pp_set_page(PP_PAGE_HOME); }

/* ---- FB ---------------------------------------------------------------------- */

void ppr_show_fb(const pp_result_t *res)
{
    if (!s_fb_page || !res) return;
    const char *kind = (res->type == PP_RES_FIX) ? "Correction" : "Polish";
    snprintf(pp.fmt, sizeof(pp.fmt), "%s%s", kind,
             pp.fb_degraded ? " (degraded)" : "");
    lv_label_set_text(s_fb_title, pp.fmt);
    lv_label_set_text(s_fb_lab, res->text.c_str());
    lv_obj_scroll_to_y(s_fb_body, 0, LV_ANIM_OFF);
}

static void ppr_fb_back_cb(lv_event_t *e) { pp_set_page(PP_PAGE_THREAD); }

/* ---- PROFILE ------------------------------------------------------------------- */

void ppr_show_profile(void)
{
    if (!s_pro_page) return;
    const pp_pal_t *p = NULL;
    for (int i = 0; i < pp.pals_cnt; i++) {
        if (pp.pals[i].id == pp.comp_pal_id) {
            p = &pp.pals[i];
            break;
        }
    }
    int threads = 0, unread = 0;
    for (int i = 0; i < pp.rows_cnt; i++) {
        if (pp.rows[i].pal_id == pp.comp_pal_id) {
            threads++;
            unread += pp.rows[i].unread;
        }
    }
    snprintf(pp.fmt, sizeof(pp.fmt),
             "%s\n\n%s\nstatus: %s\n\nthreads: %d\nunread: %d",
             p ? p->name : (pp.comp_pal_name[0] ? pp.comp_pal_name : "-"),
             p ? (p->is_npc ? "NPC pen pal" : "pen pal") : "pen pal",
             p ? p->status : "-",
             threads, unread);
    lv_label_set_text(s_pro_lab, pp.fmt);
}

static void ppr_pro_back_cb(lv_event_t *e) { pp_set_page(PP_PAGE_COMPOSE); }

/* ---- keyboard (THREAD / FB / PROFILE) ------------------------------------------ */
static void ppr_scroll_key(char c, lv_obj_t *cont)
{
    if (c == '+' || c == '-') {
        lv_obj_scroll_by(cont, 0, c == '+' ? -120 : 120, LV_ANIM_OFF);
    }
}

void ppr_key(char c)
{
    switch (pp_get_page()) {
    case PP_PAGE_THREAD:
        if (c == '\b') pp_set_page(PP_PAGE_HOME);
        else ppr_scroll_key(c, s_thr_body);
        break;
    case PP_PAGE_FB:
        if (c == '\b') pp_set_page(PP_PAGE_THREAD);
        else ppr_scroll_key(c, s_fb_body);
        break;
    case PP_PAGE_PROFILE:
        if (c == '\b') pp_set_page(PP_PAGE_COMPOSE);
        break;
    default:
        break;
    }
}

/* no persistent overlays on these pages - kept symmetric with the write
 * side so destroy stays uniform */
void ppr_overlays_close(void) { }

/* ---- builders -------------------------------------------------------------------- */
static void ppr_thread_build(lv_obj_t *parent)
{
    s_thr_page = lv_obj_create(parent);
    lv_obj_set_size(s_thr_page, 240, 320);
    lv_obj_set_pos(s_thr_page, 0, 0);
    lv_obj_set_style_bg_opa(s_thr_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_thr_page, 0, 0);
    lv_obj_set_style_pad_all(s_thr_page, 0, 0);
    lv_obj_clear_flag(s_thr_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_thr_page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_THREAD, s_thr_page);

    scr_back_btn_create(s_thr_page, "Thread", ppr_thr_back_cb);

    /* nav row: Start | < Prev | Next > + "2/5" (§4.4) */
    s_thr_start = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_start, 56, 26);
    lv_obj_align(s_thr_start, LV_ALIGN_TOP_LEFT, 4, 36);
    lv_obj_t *st = lv_label_create(s_thr_start);
    lv_label_set_text(st, "|< Start");
    lv_obj_center(st);
    lv_obj_add_event_cb(s_thr_start, ppr_thr_start_cb, LV_EVENT_CLICKED, NULL);

    s_thr_prev = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_prev, 44, 26);
    lv_obj_align(s_thr_prev, LV_ALIGN_TOP_LEFT, 64, 36);
    lv_obj_t *pv = lv_label_create(s_thr_prev);
    lv_label_set_text(pv, "< Prev");
    lv_obj_center(pv);
    lv_obj_add_event_cb(s_thr_prev, ppr_thr_prev_cb, LV_EVENT_CLICKED, NULL);

    s_thr_next = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_next, 44, 26);
    lv_obj_align(s_thr_next, LV_ALIGN_TOP_LEFT, 112, 36);
    lv_obj_t *nx = lv_label_create(s_thr_next);
    lv_label_set_text(nx, "Next >");
    lv_obj_center(nx);
    lv_obj_add_event_cb(s_thr_next, ppr_thr_next_cb, LV_EVENT_CLICKED, NULL);

    /* force-refresh this thread (product request 2026-08-26); sits in the
     * TITLE row next to "< Thread" (same corner as HOME's top-bar buttons,
     * user request same day); text button - the custom bold font has no
     * LV_SYMBOL glyph coverage */
    lv_obj_t *sync_btn = lv_btn_create(s_thr_page);
    lv_obj_set_size(sync_btn, 44, 26);
    lv_obj_align(sync_btn, LV_ALIGN_TOP_RIGHT, -6, 5);
    lv_obj_t *sy = lv_label_create(sync_btn);
    lv_label_set_text(sy, "Sync");
    lv_obj_center(sy);
    lv_obj_add_event_cb(sync_btn, ppr_thr_sync_cb, LV_EVENT_CLICKED, NULL);

    s_thr_count = lv_label_create(s_thr_page);
    lv_obj_align(s_thr_count, LV_ALIGN_TOP_RIGHT, -58, 42);
    lv_label_set_text(s_thr_count, "-/-");
    lv_obj_set_style_text_font(s_thr_count, &lv_font_montserrat_14, 0);

    s_thr_head = lv_label_create(s_thr_page);
    lv_obj_align(s_thr_head, LV_ALIGN_TOP_LEFT, 6, 66);
    lv_obj_set_width(s_thr_head, 228);
    lv_label_set_long_mode(s_thr_head, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_thr_head, "");
    lv_obj_set_style_text_font(s_thr_head, &lv_font_montserrat_14, 0);

    s_thr_body = lv_obj_create(s_thr_page);
    lv_obj_set_size(s_thr_body, 232, 168);
    lv_obj_align(s_thr_body, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_bg_color(s_thr_body, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_thr_body, 1, 0);
    lv_obj_set_style_border_color(s_thr_body, lv_color_black(), 0);
    lv_obj_set_style_radius(s_thr_body, 4, 0);
    lv_obj_set_style_pad_all(s_thr_body, 2, 0);
    lv_obj_set_scrollbar_mode(s_thr_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_thr_body, LV_DIR_VER);
    lv_obj_clear_flag(s_thr_body, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_thr_body, ppr_scroll_begin_cb,
                        LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_thr_body, ppr_scroll_end_cb,
                        LV_EVENT_SCROLL_END, NULL);

    s_thr_body_lab = lv_label_create(s_thr_body);
    lv_obj_set_width(s_thr_body_lab, 226);
    lv_label_set_long_mode(s_thr_body_lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_thr_body_lab, "");
    lv_obj_set_style_text_font(s_thr_body_lab, &lv_font_montserrat_14, 0);

    /* bottom condition row: Fix/Polish (mine only) + Reply (page 0) */
    s_thr_fix = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_fix, 44, 26);
    lv_obj_align(s_thr_fix, LV_ALIGN_BOTTOM_LEFT, 4, -6);
    lv_obj_t *fx = lv_label_create(s_thr_fix);
    lv_label_set_text(fx, "Fix");
    lv_obj_center(fx);
    lv_obj_add_event_cb(s_thr_fix, ppr_fix_cb, LV_EVENT_CLICKED, NULL);

    s_thr_polish = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_polish, 60, 26);
    lv_obj_align(s_thr_polish, LV_ALIGN_BOTTOM_LEFT, 52, -6);
    lv_obj_t *po = lv_label_create(s_thr_polish);
    lv_label_set_text(po, "Polish");
    lv_obj_center(po);
    lv_obj_add_event_cb(s_thr_polish, ppr_polish_cb, LV_EVENT_CLICKED, NULL);

    s_thr_reply = lv_btn_create(s_thr_page);
    lv_obj_set_size(s_thr_reply, 54, 26);
    lv_obj_align(s_thr_reply, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
    lv_obj_t *rp = lv_label_create(s_thr_reply);
    lv_label_set_text(rp, "Reply");
    lv_obj_center(rp);
    lv_obj_add_event_cb(s_thr_reply, ppr_reply_cb, LV_EVENT_CLICKED, NULL);
}

static void ppr_fb_build(lv_obj_t *parent)
{
    s_fb_page = lv_obj_create(parent);
    lv_obj_set_size(s_fb_page, 240, 320);
    lv_obj_set_pos(s_fb_page, 0, 0);
    lv_obj_set_style_bg_opa(s_fb_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_fb_page, 0, 0);
    lv_obj_set_style_pad_all(s_fb_page, 0, 0);
    lv_obj_clear_flag(s_fb_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_fb_page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_FB, s_fb_page);

    scr_back_btn_create(s_fb_page, "Back", ppr_fb_back_cb);

    s_fb_title = lv_label_create(s_fb_page);
    lv_obj_align(s_fb_title, LV_ALIGN_TOP_LEFT, 6, 38);
    lv_obj_set_width(s_fb_title, 228);
    lv_label_set_long_mode(s_fb_title, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_fb_title, "-");
    lv_obj_set_style_text_font(s_fb_title, FONT_BOLD_MONO_SIZE_15, 0);

    s_fb_body = lv_obj_create(s_fb_page);
    lv_obj_set_size(s_fb_body, 232, 246);
    lv_obj_align(s_fb_body, LV_ALIGN_TOP_MID, 0, 62);
    lv_obj_set_style_bg_color(s_fb_body, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_fb_body, 1, 0);
    lv_obj_set_style_border_color(s_fb_body, lv_color_black(), 0);
    lv_obj_set_style_radius(s_fb_body, 4, 0);
    lv_obj_set_style_pad_all(s_fb_body, 2, 0);
    lv_obj_set_scrollbar_mode(s_fb_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(s_fb_body, LV_DIR_VER);
    lv_obj_clear_flag(s_fb_body, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(s_fb_body, ppr_scroll_begin_cb,
                        LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_fb_body, ppr_scroll_end_cb,
                        LV_EVENT_SCROLL_END, NULL);

    s_fb_lab = lv_label_create(s_fb_body);
    lv_obj_set_width(s_fb_lab, 226);
    lv_label_set_long_mode(s_fb_lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_fb_lab, "");
    lv_obj_set_style_text_font(s_fb_lab, &lv_font_montserrat_14, 0);
}

static void ppr_profile_build(lv_obj_t *parent)
{
    s_pro_page = lv_obj_create(parent);
    lv_obj_set_size(s_pro_page, 240, 320);
    lv_obj_set_pos(s_pro_page, 0, 0);
    lv_obj_set_style_bg_opa(s_pro_page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pro_page, 0, 0);
    lv_obj_set_style_pad_all(s_pro_page, 0, 0);
    lv_obj_clear_flag(s_pro_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pro_page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_PROFILE, s_pro_page);

    scr_back_btn_create(s_pro_page, "Profile", ppr_pro_back_cb);

    s_pro_lab = lv_label_create(s_pro_page);
    lv_obj_align(s_pro_lab, LV_ALIGN_TOP_LEFT, 8, 42);
    lv_obj_set_width(s_pro_lab, 224);
    lv_label_set_long_mode(s_pro_lab, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_pro_lab, "-");
    lv_obj_set_style_text_font(s_pro_lab, &lv_font_montserrat_14, 0);
}

void ppr_build(lv_obj_t *parent)
{
    ppr_thread_build(parent);
    ppr_fb_build(parent);
    ppr_profile_build(parent);
}
