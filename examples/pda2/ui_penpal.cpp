/**
 * @file      ui_penpal.cpp
 * @brief     PenPal screen - async framework, waitbox, HOME, CFG, lifecycle,
 *            keyboard poll (design: docs/penpal-design.md v3.2 §3.1/§3.2).
 *
 *            Screen split for reviewability (§6):
 *              ui_penpal.cpp        - this file (framework + HOME + CFG)
 *              ui_penpal_write.cpp  - COMPOSE, TOPICS, send/idempotency
 *              ui_penpal_read.cpp   - THREAD, FB (fix/polish), PROFILE
 *
 *            Async model (§3.2, async_ipc_contract): one result queue
 *            s_pp_q (registered deviation, results carry type as their
 *            second field), one busy flag + busy-generation, one page
 *            generation. The worker task owns a full request snapshot and
 *            parses JSON on its own thread; results are heap structs whose
 *            ownership moves to the UI over the queue.
 *
 *            Waitbox Close semantics (v3 §3.2, first-of-kind in this repo):
 *              read/compute types -> CANCEL  (gen++, busy=false, late
 *                                            results dropped by gen)
 *              SEND               -> background continue (box hides, gen
 *                                            and busy kept - the POST may
 *                                            already be out; cancelling the
 *                                            wait would risk a double send)
 *
 *            HOME sync is a serial two-leg chain PALS -> MAILBOX with busy
 *            held across both legs (§3.2). R9 (2026-08-22): null-pal
 *            residual rows are SHOWN and open read-only - no filtering.
 */
#include "Arduino.h"
#include "ui_deckpro.h"
#include "ui_deckpro_port.h"
#include "ui_penpal.h"
#include "ui_scr_mrg.h"
#include "http_utils.h"
#include "src/assets.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstdarg>

/* ui_deckpro.cpp keeps this macro private - mirror it (same assets font) */
#define FONT_BOLD_MONO_SIZE_15 &Font_Mono_Bold_15

/* ---- shared state (definitions; declared in ui_penpal.h) ---------------- */
pp_state_t pp;
QueueHandle_t s_pp_q = NULL;
volatile uint32_t s_pp_gen = 0;
volatile bool s_pp_busy = false;
uint32_t s_pp_busy_gen = 0;
bool s_pp_active = false;

/* P2 (Codex): tasks that already left pp_start are NOT abortable - a READ
 * Close only stops the UI from waiting. Cap the number of live workers
 * (one cancelled zombie + one new request) so repeated Close/retry cannot
 * pile up 8 KiB-stack tasks; the counter is RMW'd from both cores. */
#define PP_TASK_MAX_CONCURRENCY 2
static volatile int s_pp_inflight = 0;

/* P1 (Codex): screens are created at scr_mgr REGISTER time (boot), long
 * before entry - the entry auto-sync must not re-run on every visit. */
static bool s_pp_autosynced = false;

/* ---- file-internal forward decls (define-order dependencies) ------------ */
static void pp_home_pal_cb(lv_event_t *e);
static void pp_home_cfg_back_cb(lv_event_t *e);
void pp_home_render_pals(void);
void pp_home_render_rows(void);
void pp_cfg_prefill(void);

/* ---- page registry ------------------------------------------------------ */
static lv_obj_t *s_pages[PP_PAGE_CNT] = { 0 };
static lv_obj_t *s_status_lab[PP_PAGE_CNT] = { 0 };   /* pages w/ a status line */
static pp_page_t s_cur_page = PP_PAGE_HOME;

void pp_page_register(pp_page_t page, lv_obj_t *cont)
{
    if (page >= 0 && page < PP_PAGE_CNT) s_pages[page] = cont;
}

void pp_status_register(pp_page_t page, lv_obj_t *lab)
{
    if (page >= 0 && page < PP_PAGE_CNT) s_status_lab[page] = lab;
}

pp_page_t pp_get_page(void) { return s_cur_page; }

void pp_set_page(pp_page_t page)
{
    if (page < 0 || page >= PP_PAGE_CNT || !s_pages[page]) return;
    for (int i = 0; i < PP_PAGE_CNT; i++) {
        if (s_pages[i]) lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_pages[page], LV_OBJ_FLAG_HIDDEN);
    s_cur_page = page;
    switch (page) {
    case PP_PAGE_COMPOSE: ppw_show_compose(); break;
    case PP_PAGE_TOPICS:  ppw_render_topics(); break;
    case PP_PAGE_THREAD:  ppr_show_thread(); break;
    case PP_PAGE_PROFILE: ppr_show_profile(); break;
    case PP_PAGE_CFG:     pp_cfg_prefill(); break;
    default: break;
    }
    ui_disp_full_refr();
}

void pp_status_set(const char *fmt, ...)
{
    lv_obj_t *lab = s_status_lab[s_cur_page];
    if (!lab) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pp.fmt, sizeof(pp.fmt), fmt, ap);
    va_end(ap);
    lv_label_set_text(lab, pp.fmt);
}

/* ---- HOME status + one-sync-cycle note (§4.2 sent-ok visibility) -------- */
static char s_home_note[48] = "";

void pp_home_note(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_home_note, sizeof(s_home_note), fmt, ap);
    va_end(ap);
    if (s_status_lab[PP_PAGE_HOME]) {
        lv_label_set_text(s_status_lab[PP_PAGE_HOME], s_home_note);
    }
}

/* ---- notice msgbox (any key closes, +/- scrolls the body) --------------- */
static lv_obj_t *s_msgbox = NULL;
static lv_obj_t *s_msgbox_body = NULL;

void pp_msgbox_show(const char *title, const char *text)
{
    pp_msgbox_close();
    s_msgbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_msgbox, 220, 180);
    lv_obj_align(s_msgbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_msgbox, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_msgbox, 1, 0);
    lv_obj_set_style_border_color(s_msgbox, lv_color_black(), 0);
    lv_obj_set_style_radius(s_msgbox, 6, 0);
    lv_obj_set_style_pad_all(s_msgbox, 8, 0);
    lv_obj_set_flex_flow(s_msgbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_msgbox, 4, 0);

    lv_obj_t *t = lv_label_create(s_msgbox);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, FONT_BOLD_MONO_SIZE_15, 0);

    s_msgbox_body = lv_label_create(s_msgbox);
    lv_obj_set_width(s_msgbox_body, lv_pct(100));
    lv_obj_set_height(s_msgbox_body, 116);
    lv_label_set_long_mode(s_msgbox_body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_msgbox_body, text);
    lv_obj_set_style_text_font(s_msgbox_body, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(s_msgbox_body, 1);
}

void pp_msgbox_close(void)
{
    if (s_msgbox) {
        lv_obj_del(s_msgbox);
        s_msgbox = NULL;
        s_msgbox_body = NULL;
    }
}

bool pp_msgbox_open(void) { return s_msgbox != NULL; }

/* ---- waitbox (§3.2; Close semantics split by request kind) -------------- */
typedef enum { PP_WAIT_READ = 0, PP_WAIT_SEND } pp_wait_kind_t;

static lv_obj_t *s_waitbox = NULL;
static lv_obj_t *s_wait_body = NULL;
static pp_wait_kind_t s_wait_kind = PP_WAIT_READ;
static uint32_t s_wait_t0 = 0;
static uint32_t s_wait_last = 99;

bool pp_waitbox_visible(void) { return s_waitbox != NULL; }

static void pp_waitbox_hide(void)
{
    if (s_waitbox) {
        lv_obj_del(s_waitbox);
        s_waitbox = NULL;
        s_wait_body = NULL;
    }
}

/* Close button (touch-only; the keyboard is swallowed while the box is up):
 * READ -> cancel (gen++ kills the late result, busy cleared here - the
 *                task's own result can no longer release it, §3.2)
 * SEND -> background continue (only the box hides; gen/busy kept so the
 *                result is still consumed and no second send can start) */
static void pp_wait_close_cb(lv_event_t *e)
{
    if (!s_waitbox) return;
    if (s_wait_kind == PP_WAIT_SEND) {
        pp_waitbox_hide();
        ppw_send_in_background_hint();
    } else {
        pp_waitbox_hide();
        s_pp_gen++;
        s_pp_busy = false;
        pp_status_set("cancelled");
        Serial.println("[PenPal] wait cancelled (read-type request)");
    }
}

static void pp_waitbox_show(pp_wait_kind_t kind, const char *title)
{
    pp_waitbox_hide();
    s_waitbox = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_waitbox, 220, 130);
    lv_obj_align(s_waitbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_waitbox, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_waitbox, 1, 0);
    lv_obj_set_style_border_color(s_waitbox, lv_color_black(), 0);
    lv_obj_set_style_radius(s_waitbox, 6, 0);
    lv_obj_set_style_pad_all(s_waitbox, 8, 0);
    lv_obj_set_flex_flow(s_waitbox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_waitbox, 4, 0);

    lv_obj_t *t = lv_label_create(s_waitbox);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, FONT_BOLD_MONO_SIZE_15, 0);

    s_wait_body = lv_label_create(s_waitbox);
    lv_obj_set_width(s_wait_body, lv_pct(100));
    lv_label_set_text(s_wait_body, "Waiting... 10s");
    lv_obj_set_style_text_font(s_wait_body, &lv_font_montserrat_14, 0);
    lv_obj_set_flex_grow(s_wait_body, 1);

    lv_obj_t *close_btn = lv_btn_create(s_waitbox);
    lv_obj_set_size(close_btn, 64, 26);
    lv_obj_t *close_lab = lv_label_create(close_btn);
    lv_label_set_text(close_lab, "Close");
    lv_obj_center(close_lab);
    lv_obj_add_event_cb(close_btn, pp_wait_close_cb, LV_EVENT_CLICKED, NULL);

    s_wait_kind = kind;
    s_wait_t0 = millis();
    s_wait_last = 99;
}

static void pp_waitbox_tick(void)
{
    if (!s_waitbox || !s_wait_body) return;
    int32_t remain = 10000 - (int32_t)(millis() - s_wait_t0);
    if (remain <= 0) {
        if (s_wait_last != 0) {
            s_wait_last = 0;
            lv_label_set_text(s_wait_body,
                              s_wait_kind == PP_WAIT_SEND
                                  ? "still sending...\nClose = background"
                                  : "still waiting...\nClose = cancel");
        }
        return;
    }
    uint32_t secs = ((uint32_t)remain + 999) / 1000;
    if (secs != s_wait_last) {
        s_wait_last = secs;
        snprintf(pp.fmt, sizeof(pp.fmt), "Waiting... %lus", (unsigned long)secs);
        lv_label_set_text(s_wait_body, pp.fmt);
    }
}

/* ---- config helpers (UI thread only - Preferences not re-entrant) ------- */
void pp_cfg_load(char *base, int base_len, char *key, int key_len)
{
    penpal_load_config(base, base_len, key, key_len);
}

bool pp_cfg_from_nvs(void)
{
    Preferences pr;
    if (!pr.begin("penpal", true)) return false;
    bool has = pr.isKey("base") || pr.isKey("key");
    pr.end();
    return has;
}

/* ---- worker task -------------------------------------------------------- */
/* FIX/POLISH/TIPS display text is FORMATTED HERE, on the task thread (§4.5),
 * so the UI consume path only copies into labels. */
static void pp_fix_text(string &out, const pp_fix_t *f)
{
    out.clear();
    for (int i = 0; i < f->count; i++) {
        char head[24];
        snprintf(head, sizeof(head), "%d. ", i + 1);
        out += head;
        out += "[";
        out += f->items[i].type;
        out += "]\n";
        out += f->items[i].from;
        out += "\n-> ";
        out += f->items[i].to;
        out += "\n";
        out += f->items[i].explanation;
        out += "\n\n";
    }
    if (f->truncated) out += "(more fixes not shown)\n";
    if (f->count == 0) out = "No corrections found.\n";
}

static void pp_polish_text(string &out, const pp_polish_t *p)
{
    out.clear();
    out += "Improved letter:\n";
    out += p->improved;
    out += "\n\nImprovements:\n";
    for (int i = 0; i < p->imp_count; i++) {
        out += "- ";
        out += p->improvements[i];
        out += "\n";
    }
    if (p->imp_count == 0) out += "(none)\n";
    out += "\nCoverage:\n";
    for (int i = 0; i < p->cov_count; i++) {
        out += p->coverage[i].question;
        out += ": ";
        out += p->coverage[i].status;
        out += "\n";
    }
    if (p->cov_count == 0) out += "(none)\n";
}

static void pp_tips_text(string &out, const pp_tips_t *t)
{
    out.clear();
    for (int i = 0; i < t->count; i++) {
        char head[16];
        snprintf(head, sizeof(head), "%d. ", i + 1);
        out += head;
        out += t->tips[i];
        out += "\n";
    }
    if (t->truncated) out += "(more tips not shown)\n";
    if (t->count == 0) out = "No tips for this letter.\n";
}

static void pp_task_func(void *param)
{
    pp_task_req_t *rq = (pp_task_req_t *)param;    /* task-owned snapshot */
    pp_result_t *res = new pp_result_t();
    res->gen = rq->gen;
    res->type = rq->type;
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    switch (rq->type) {
    case PP_RES_PALS:
        res->ok = penpal_get_pals(base, key, res->pals, PP_PAL_MAX,
                                  &res->count, &err);
        break;
    case PP_RES_MAILBOX:
        res->ok = penpal_get_mailbox(base, key, res->rows, PP_MAILBOX_MAX,
                                     &res->count, &res->truncated, &err);
        break;
    case PP_RES_THREAD:
        res->ok = penpal_get_thread(base, key, rq->pen_pal_id,
                                    rq->thread_root_id, res->letters,
                                    PP_THREAD_MAX, &res->count, &res->dropped,
                                    &err);
        break;
    case PP_RES_SEND:
        res->ok = penpal_send_email(base, key, &rq->send,
                                    rq->idem_key[0] ? rq->idem_key : NULL,
                                    &res->email_id, &res->root_id,
                                    &res->reply_pending, &res->replayed, &err);
        break;
    case PP_RES_TOPICS:
        res->ok = penpal_get_topics(base, key, res->topics, PP_TOPIC_MAX,
                                    &res->count, &err);
        break;
    case PP_RES_FIX:
        res->ok = penpal_correction(base, key, rq->email_id, &res->fix, &err);
        if (res->ok) pp_fix_text(res->text, &res->fix);
        break;
    case PP_RES_POLISH:
        res->ok = penpal_polish(base, key, rq->email_id, &res->polish, &err);
        if (res->ok) pp_polish_text(res->text, &res->polish);
        break;
    case PP_RES_TIPS:
        res->ok = penpal_tips(base, key, rq->email_id, &res->tips, &err);
        if (res->ok) pp_tips_text(res->text, &res->tips);
        break;
    default:
        err = "bad request type";
        res->ok = false;
        break;
    }
    if (!res->ok) res->err = err;
    delete rq;                                     /* snapshot dies here */
    if (s_pp_q) {
        xQueueSend(s_pp_q, &res, portMAX_DELAY);   /* ownership -> UI */
    } else {
        delete res;
    }
    __atomic_sub_fetch(&s_pp_inflight, 1, __ATOMIC_RELAXED);  /* P2 cap */
    vTaskDelete(NULL);
}

static void pp_wait_for_type(int type, pp_wait_kind_t *kind, const char **title)
{
    switch (type) {
    case PP_RES_PALS:    *kind = PP_WAIT_READ; *title = "Sync pen-pals...";   break;
    case PP_RES_MAILBOX: *kind = PP_WAIT_READ; *title = "Sync mailbox...";    break;
    case PP_RES_THREAD:  *kind = PP_WAIT_READ; *title = "Opening thread...";  break;
    case PP_RES_TOPICS:  *kind = PP_WAIT_READ; *title = "Loading topics...";  break;
    case PP_RES_SEND:    *kind = PP_WAIT_SEND; *title = "Sending letter...";  break;
    case PP_RES_FIX:     *kind = PP_WAIT_READ; *title = "Correcting...\n(LLM, up to 3 min)"; break;
    case PP_RES_POLISH:  *kind = PP_WAIT_READ; *title = "Polishing...\n(LLM, up to 3 min)";  break;
    case PP_RES_TIPS:    *kind = PP_WAIT_READ; *title = "Reply tips...\n(LLM, up to 3 min)"; break;
    default:             *kind = PP_WAIT_READ; *title = "Working...";         break;
    }
}

/* Busy release rule (§3.2): only the in-flight generation may clear busy. */
void pp_release_busy(uint32_t gen)
{
    if (gen == s_pp_busy_gen) {
        s_pp_busy = false;
    }
}

bool pp_start(const pp_task_req_t *tmpl, bool chained)
{
    if (!chained && s_pp_busy) {
        pp_status_set("busy - wait for current request");
        return false;
    }
    /* config first (kimi §1.11.4): unconfigured -> guide, NO network */
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    pp_cfg_load(base, sizeof(base), key, sizeof(key));
    if (!base[0] || !key[0]) {
        pp_status_set("configure server in Cfg");
        return false;
    }
    if (!chained && !http_require_wifi("PenPal")) return false;
    if (!s_pp_q) s_pp_q = xQueueCreate(4, sizeof(void *));
    if (!s_pp_q) {
        pp_status_set("Out of memory");
        return false;
    }
    /* P2 cap (chained legs belong to the single flight that already passed) */
    if (!chained &&
        __atomic_load_n(&s_pp_inflight, __ATOMIC_RELAXED) >=
            PP_TASK_MAX_CONCURRENCY) {
        pp_status_set("wait - previous request closing");
        return false;
    }
    pp_task_req_t *rq = new pp_task_req_t(*tmpl);  /* full string copies */
    rq->base = base;
    rq->key = key;
    /* count BEFORE create: the worker may finish on the other core before
     * xTaskCreate returns here (P2) */
    __atomic_add_fetch(&s_pp_inflight, 1, __ATOMIC_RELAXED);
    TaskHandle_t h = NULL;
    if (xTaskCreate(pp_task_func, "penpal", 1024 * 8, rq, 1, &h) != pdPASS) {
        __atomic_sub_fetch(&s_pp_inflight, 1, __ATOMIC_RELAXED);
        delete rq;
        pp_status_set("Cannot start task");
        return false;
    }
    if (chained) {
        /* second leg of the HOME serial sync: busy stays held, only the
         * waitbox gets retitled (§3.2) */
        pp_wait_kind_t kind;
        const char *title;
        pp_wait_for_type(rq->type, &kind, &title);
        pp_waitbox_show(kind, title);
    } else {
        s_pp_busy = true;
        s_pp_busy_gen = rq->gen;
        pp_wait_kind_t kind;
        const char *title;
        pp_wait_for_type(rq->type, &kind, &title);
        pp_waitbox_show(kind, title);
    }
    return true;
}

/* ---- result consume (UI thread) ----------------------------------------- */
/* Auto page-switch only happens when the user is still on the page the
 * request was launched from - a result must never yank the user off a page
 * they navigated to while it was in flight (internal page switches do not
 * bump the generation; only entry/destroy/cancel do, §3.2). Data is stored
 * either way. */
static void pp_consume(pp_result_t *res)
{
    if (res->gen != s_pp_gen || !s_pp_active) {
        Serial.printf("[PenPal] stale result type=%d dropped\n", res->type);
        /* P1 (Codex): this dropped request may OWN s_pp_busy (e.g. a
         * background SEND whose result arrived after the user left the
         * screen - exit hides the box but does not clear busy). No other
         * consumer exists, so the drop must release it. */
        if (s_pp_busy && res->gen == s_pp_busy_gen) {
            s_pp_busy = false;
        }
        return;
    }
    pp_task_req_t rq = {};
    switch (res->type) {
    case PP_RES_PALS:
        if (res->ok) {
            memcpy(pp.pals, res->pals, sizeof(pp.pals));
            pp.pals_cnt = res->count;
            pp_home_render_pals();
            pp_status_set("PALS OK, syncing mailbox...");
            rq.gen = res->gen;
            rq.type = PP_RES_MAILBOX;
            if (!pp_start(&rq, true)) {
                /* chain broken: release busy here - the second leg never
                 * started, nobody else will (§3.2 kimi §3.4) */
                pp_waitbox_hide();
                pp_release_busy(res->gen);
                pp_status_set("sync stopped (task failed)");
            }
        } else {
            pp_waitbox_hide();
            pp_release_busy(res->gen);
            pp_status_set("%s", res->err.c_str());
        }
        break;

    case PP_RES_MAILBOX:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        if (res->ok) {
            memcpy(pp.rows, res->rows, sizeof(pp.rows));
            /* R9 (2026-08-22): null-pal residual rows are KEPT - HOME shows
             * them, THREAD opens them read-only via thread_root_id alone */
            pp.rows_cnt = res->count;
            pp.mailbox_truncated = res->truncated;
            pp.home_page = 0;
            pp_home_render_rows();
            if (s_home_note[0]) {
                pp_status_set("%s · Mailbox OK", s_home_note);
            } else {
                pp_status_set("Mailbox OK%s",
                              res->truncated ? " (24+ threads)" : "");
            }
        } else {
            pp_status_set("%s", res->err.c_str());
        }
        break;

    case PP_RES_THREAD:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        if (res->ok) {
            /* server order is oldest-first -> store NEWEST-first (index 0 =
             * newest, §4.4) */
            pp.letters_cnt = res->count;
            for (int i = 0; i < res->count; i++) {
                pp.letters[i] = res->letters[res->count - 1 - i];
            }
            pp.thr_idx = 0;
            pp.thr_dropped = res->dropped;
            if (s_cur_page == PP_PAGE_HOME) {
                pp_set_page(PP_PAGE_THREAD);
            }
        } else {
            pp_status_set("%s", res->err.c_str());
        }
        break;

    case PP_RES_SEND:
        /* framework part; the compose-side snapshot compare / clearing /
         * unlocking / home-note / auto-sync live in ui_penpal_write.cpp */
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        ppw_on_send_result(res);
        break;

    case PP_RES_TOPICS:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        if (res->ok) {
            memcpy(pp.topics, res->topics, sizeof(pp.topics));
            pp.topics_cnt = res->count;
            pp.topics_page = 0;
            pp.topics_sel = -1;
            if (s_cur_page == PP_PAGE_COMPOSE) {
                pp_set_page(PP_PAGE_TOPICS);
            }
        } else {
            pp_status_set("%s", res->err.c_str());
        }
        break;

    case PP_RES_FIX:
    case PP_RES_POLISH:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        if (res->ok) {
            pp.fb_degraded = (res->type == PP_RES_FIX)
                                 ? res->fix.degraded : res->polish.degraded;
            ppr_show_fb(res);
            if (s_cur_page == PP_PAGE_THREAD) {
                pp_set_page(PP_PAGE_FB);
            }
        } else {
            /* THREAD has no status line - errors surface as a notice box */
            pp_msgbox_show("Request failed", res->err.c_str());
        }
        break;

    case PP_RES_TIPS:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        if (res->ok) {
            pp_msgbox_show(res->tips.degraded ? "Tips (degraded)" : "Tips",
                           res->text.c_str());
        } else {
            pp_msgbox_show("Tips failed", res->err.c_str());
        }
        break;

    default:
        pp_waitbox_hide();
        pp_release_busy(res->gen);
        break;
    }
}

/* ---- HOME page ----------------------------------------------------------- */
static lv_obj_t *s_pals_row = NULL;      /* icon row container */
static lv_obj_t *s_pals_more = NULL;     /* "+N" label */
static lv_obj_t *s_row_btn[5];
static lv_obj_t *s_row_lab[5];
static lv_obj_t *s_home_nav = NULL;      /* "page 1/2" */
static lv_obj_t *s_home_prev = NULL;
static lv_obj_t *s_home_next = NULL;

void pp_home_render_pals(void)
{
    if (!s_pals_row) return;
    lv_obj_clean(s_pals_row);
    int shown = pp.pals_cnt < 3 ? pp.pals_cnt : 3;
    for (int i = 0; i < shown; i++) {
        const pp_pal_t *p = &pp.pals[i];
        char initial[2] = { p->name[0] ? p->name[0] : '?', 0 };
        /* ASCII-upper the initial defensively (names are expected ASCII) */
        if (initial[0] >= 'a' && initial[0] <= 'z') {
            initial[0] = initial[0] - 'a' + 'A';
        }
        lv_obj_t *btn = lv_btn_create(s_pals_row);
        lv_obj_set_size(btn, 66, 52);
        lv_obj_t *lab = lv_label_create(btn);
        lv_label_set_text_fmt(lab, "%s\n%s", initial, p->name);
        lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
        lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(lab, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lab, LV_LABEL_LONG_CLIP);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, pp_home_pal_cb, LV_EVENT_CLICKED, NULL);
    }
    if (s_pals_more) {
        lv_label_set_text_fmt(s_pals_more,
                              pp.pals_cnt > 3 ? "+%d" : "",
                              pp.pals_cnt > 3 ? pp.pals_cnt - 3 : 0);
    }
}

/* "pending"->"pend" / "replied"->"repl"; unknown states pass through */
static void pp_state_abbr(const char *in, char *out, int out_len)
{
    if (!strcmp(in, "pending")) strlcpy(out, "pend", out_len);
    else if (!strcmp(in, "replied")) strlcpy(out, "repl", out_len);
    else strlcpy(out, in, out_len);
}

void pp_home_render_rows(void)
{
    int pages = (pp.rows_cnt + 4) / 5;
    if (pp.home_page >= pages && pages > 0) pp.home_page = pages - 1;
    for (int i = 0; i < 5; i++) {
        int idx = pp.home_page * 5 + i;
        if (idx >= pp.rows_cnt) {
            lv_obj_add_flag(s_row_btn[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const pp_thread_row_t *r = &pp.rows[idx];
        lv_obj_clear_flag(s_row_btn[i], LV_OBJ_FLAG_HIDDEN);
        /* last_at "2026-08-20T15:10:01" -> "08-20 15:10" (server time is
         * local time, no conversion - §4.1) */
        char when[12] = "";
        if (strlen(r->last_at) >= 16) {
            memcpy(when, r->last_at + 5, 11);
            when[11] = 0;
        }
        char st[8];
        pp_state_abbr(r->state, st, sizeof(st));
        const char *who = r->last_sender[0] ? r->last_sender : r->from;
        snprintf(pp.fmt, sizeof(pp.fmt),
                 "%s  %s%s\n%s  %s",
                 who, when,
                 r->unread > 0 ? "  [new]" : "",
                 r->subject, st);
        lv_label_set_text(s_row_lab[i], pp.fmt);
        lv_obj_set_user_data(s_row_btn[i], (void *)(intptr_t)idx);
    }
    if (pages > 1) {
        lv_obj_clear_flag(s_home_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_home_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_home_next, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_home_nav, "page %d/%d", pp.home_page + 1,
                              pages);
    } else {
        lv_obj_add_flag(s_home_nav, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_home_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_home_next, LV_OBJ_FLAG_HIDDEN);
    }
}

/* HOME sync: serial PALS -> MAILBOX (§3.2). manual=true (Sync key/btn) drops
 * the one-cycle note; the post-send auto sync keeps it. */
void pp_home_sync(bool manual)
{
    if (manual) s_home_note[0] = 0;
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_PALS;
    pp_start(&rq, false);
}

static void pp_home_sync_cb(lv_event_t *e) { pp_home_sync(true); }

static void pp_home_cfg_cb(lv_event_t *e) { pp_set_page(PP_PAGE_CFG); }

static void pp_home_pal_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (i < 0 || i >= pp.pals_cnt) return;
    pp.reply_mode = false;
    pp.comp_pal_id = pp.pals[i].id;
    strlcpy(pp.comp_pal_name, pp.pals[i].name, sizeof(pp.comp_pal_name));
    pp.comp_root_id = 0;
    pp_set_page(PP_PAGE_COMPOSE);
}

static void pp_home_row_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (idx < 0 || idx >= pp.rows_cnt) return;
    if (s_pp_busy) {
        pp_status_set("busy - wait for current request");
        return;
    }
    pp.thr_root = pp.rows[idx].root_id;
    pp.thr_pal = pp.rows[idx].pal_id;      /* 0 = residual, read-only (R9) */
    strlcpy(pp.thr_subject, pp.rows[idx].subject, sizeof(pp.thr_subject));
    pp_task_req_t rq = {};
    rq.gen = s_pp_gen;
    rq.type = PP_RES_THREAD;
    rq.pen_pal_id = pp.thr_pal;
    rq.thread_root_id = pp.thr_root;
    if (!pp_start(&rq, false)) return;     /* waitbox + status handled there */
    pp_status_set("opening...");
}

static void pp_home_prev_cb(lv_event_t *e)
{
    if (pp.home_page > 0) {
        pp.home_page--;
        pp_home_render_rows();
        ui_disp_full_refr();
    }
}

static void pp_home_next_cb(lv_event_t *e)
{
    int pages = (pp.rows_cnt + 4) / 5;
    if (pp.home_page + 1 < pages) {
        pp.home_page++;
        pp_home_render_rows();
        ui_disp_full_refr();
    }
}

/* Click-back runs deep inside LVGL's event dispatch; running scr_mgr_pop
 * synchronously there (exit + destroy + menu re-entry + lv_obj_del of the
 * whole 7-page tree) tripped the loopTask stack canary - serial evidence
 * 2026-08-22: "Stack canary watchpoint triggered (loopTask)" right after
 * scr_mgr_pop's keypad_clear_chars() log line, from a title-bar click.
 * Defer the pop to the next lv_timer_handler pass so the click stack
 * unwinds first - the same context the keypad '\b' handler uses. A second
 * queued pop is harmless: scr_mgr_pop refuses when top == stack root. */
static void pp_pop_async(void *arg)
{
    (void)arg;
    scr_mgr_pop(false);
}

static void pp_back_cb(lv_event_t *e)
{
    s_pp_active = false;             /* a queued result must drop even if it
                                      * lands before the deferred pop runs */
    lv_async_call(pp_pop_async, NULL);
}

static void pp_home_build(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, 240, 320);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_HOME, page);

    scr_back_btn_create(page, "PenPal", pp_back_cb);

    lv_obj_t *cfg_btn = lv_btn_create(page);
    lv_obj_set_size(cfg_btn, 44, 30);
    lv_obj_align(cfg_btn, LV_ALIGN_TOP_RIGHT, -50, 3);
    lv_obj_t *cfg_lab = lv_label_create(cfg_btn);
    lv_label_set_text(cfg_lab, "Cfg");
    lv_obj_center(cfg_lab);
    lv_obj_add_event_cb(cfg_btn, pp_home_cfg_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sync_btn = lv_btn_create(page);
    lv_obj_set_size(sync_btn, 48, 30);
    lv_obj_align(sync_btn, LV_ALIGN_TOP_RIGHT, -2, 3);
    lv_obj_t *sync_lab = lv_label_create(sync_btn);
    lv_label_set_text(sync_lab, "Sync");
    lv_obj_center(sync_lab);
    lv_obj_add_event_cb(sync_btn, pp_home_sync_cb, LV_EVENT_CLICKED, NULL);

    /* pen-pal icon row (<=3 x 66x52) + "+N" */
    s_pals_row = lv_obj_create(page);
    lv_obj_set_size(s_pals_row, 232, 56);
    lv_obj_align(s_pals_row, LV_ALIGN_TOP_MID, 0, 36);
    lv_obj_set_style_bg_opa(s_pals_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pals_row, 0, 0);
    lv_obj_set_style_pad_all(s_pals_row, 0, 0);
    lv_obj_set_flex_flow(s_pals_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(s_pals_row, 8, 0);
    lv_obj_clear_flag(s_pals_row, LV_OBJ_FLAG_SCROLLABLE);

    s_pals_more = lv_label_create(page);
    lv_obj_align(s_pals_more, LV_ALIGN_TOP_MID, 78, 56);
    lv_obj_set_style_text_font(s_pals_more, &lv_font_montserrat_14, 0);

    lv_obj_t *status = lv_label_create(page);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 4, 96);
    lv_obj_set_width(status, 232);
    lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_palette_main(LV_PALETTE_GREY), 0);
    pp_status_register(PP_PAGE_HOME, status);

    /* 5 thread rows x 34px; row = two-line label on a button */
    for (int i = 0; i < 5; i++) {
        s_row_btn[i] = lv_btn_create(page);
        lv_obj_set_size(s_row_btn[i], 232, 34);
        lv_obj_align(s_row_btn[i], LV_ALIGN_TOP_MID, 0, 114 + i * 36);
        lv_obj_set_style_pad_all(s_row_btn[i], 2, 0);
        s_row_lab[i] = lv_label_create(s_row_btn[i]);
        lv_obj_align(s_row_lab[i], LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_set_width(s_row_lab[i], 226);
        lv_label_set_long_mode(s_row_lab[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(s_row_lab[i], "");
        lv_obj_set_style_text_font(s_row_lab[i], &lv_font_montserrat_14, 0);
        lv_obj_add_event_cb(s_row_btn[i], pp_home_row_cb, LV_EVENT_CLICKED,
                            NULL);
        lv_obj_add_flag(s_row_btn[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* bottom nav (hidden until >1 page) */
    s_home_prev = lv_btn_create(page);
    lv_obj_set_size(s_home_prev, 54, 26);
    lv_obj_align(s_home_prev, LV_ALIGN_BOTTOM_LEFT, 4, -6);
    lv_obj_t *prev_lab = lv_label_create(s_home_prev);
    lv_label_set_text(prev_lab, "< Prev");
    lv_obj_center(prev_lab);
    lv_obj_add_event_cb(s_home_prev, pp_home_prev_cb, LV_EVENT_CLICKED, NULL);

    s_home_next = lv_btn_create(page);
    lv_obj_set_size(s_home_next, 54, 26);
    lv_obj_align(s_home_next, LV_ALIGN_BOTTOM_RIGHT, -4, -6);
    lv_obj_t *next_lab = lv_label_create(s_home_next);
    lv_label_set_text(next_lab, "Next >");
    lv_obj_center(next_lab);
    lv_obj_add_event_cb(s_home_next, pp_home_next_cb, LV_EVENT_CLICKED, NULL);

    s_home_nav = lv_label_create(page);
    lv_obj_align(s_home_nav, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(s_home_nav, "page 1/1");
    lv_obj_set_style_text_font(s_home_nav, &lv_font_montserrat_14, 0);

    lv_obj_add_flag(s_home_nav, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_home_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_home_next, LV_OBJ_FLAG_HIDDEN);
}

static void pp_home_key(char c)
{
    if (c == '+' || c == '-') {
        int pages = (pp.rows_cnt + 4) / 5;
        if (c == '+' && pp.home_page + 1 < pages) pp.home_page++;
        else if (c == '-' && pp.home_page > 0) pp.home_page--;
        else return;
        pp_home_render_rows();
        ui_disp_full_refr();
    } else if (c == '\n') {
        pp_home_sync(true);
    } else if (c == '\b') {
        s_pp_active = false;
        scr_mgr_pop(false);
    }
}

/* ---- CFG page (§4.7) ----------------------------------------------------- */
static lv_obj_t *s_cfg_base_ta = NULL;
static lv_obj_t *s_cfg_key_ta = NULL;
static bool s_cfg_focus_key = false;     /* false = base, true = key */

void pp_cfg_prefill(void)
{
    if (!s_cfg_base_ta) return;
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    pp_cfg_load(base, sizeof(base), key, sizeof(key));
    lv_textarea_set_text(s_cfg_base_ta, base);
    lv_textarea_set_text(s_cfg_key_ta, key);
    if (base[0] && !pp_cfg_from_nvs()) {
        pp_status_set("current value from env.cfg - Save writes NVS");
    } else {
        pp_status_set("");
    }
}

static void pp_cfg_save_cb(lv_event_t *e)
{
    const char *base = lv_textarea_get_text(s_cfg_base_ta);
    const char *key = lv_textarea_get_text(s_cfg_key_ta);
    if (!base || !key) return;
    if (!penpal_save_config(base, key)) {
        pp_status_set("save failed (NVS)");
    } else {
        pp_status_set("saved");
        s_pp_autosynced = false;   /* next visit syncs with the new config */
    }
}

static void pp_cfg_build(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, 240, 320);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    pp_page_register(PP_PAGE_CFG, page);

    /* internal page back: HOME, not the menu */
    scr_back_btn_create(page, "Cfg", pp_home_cfg_back_cb);

    lv_obj_t *bl = lv_label_create(page);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 6, 42);
    lv_label_set_text(bl, "Base URL:");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_14, 0);

    s_cfg_base_ta = lv_textarea_create(page);
    lv_obj_set_size(s_cfg_base_ta, 228, 34);
    lv_obj_align(s_cfg_base_ta, LV_ALIGN_TOP_MID, 0, 60);
    lv_textarea_set_max_length(s_cfg_base_ta, 95);   /* env.cfg value cap §3.4 */
    lv_textarea_set_one_line(s_cfg_base_ta, true);
    lv_obj_set_style_text_font(s_cfg_base_ta, &lv_font_montserrat_14, 0);

    lv_obj_t *kl = lv_label_create(page);
    lv_obj_align(kl, LV_ALIGN_TOP_LEFT, 6, 102);
    lv_label_set_text(kl, "API key:");
    lv_obj_set_style_text_font(kl, &lv_font_montserrat_14, 0);

    s_cfg_key_ta = lv_textarea_create(page);
    lv_obj_set_size(s_cfg_key_ta, 228, 34);
    lv_obj_align(s_cfg_key_ta, LV_ALIGN_TOP_MID, 0, 120);
    lv_textarea_set_max_length(s_cfg_key_ta, 16);
    lv_textarea_set_one_line(s_cfg_key_ta, true);
    lv_obj_set_style_text_font(s_cfg_key_ta, &lv_font_montserrat_14, 0);

    lv_obj_t *save_btn = lv_btn_create(page);
    lv_obj_set_size(save_btn, 64, 30);
    lv_obj_align(save_btn, LV_ALIGN_TOP_LEFT, 6, 164);
    lv_obj_t *save_lab = lv_label_create(save_btn);
    lv_label_set_text(save_lab, "Save");
    lv_obj_center(save_lab);
    lv_obj_add_event_cb(save_btn, pp_cfg_save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *status = lv_label_create(page);
    lv_obj_align(status, LV_ALIGN_TOP_LEFT, 6, 200);
    lv_obj_set_width(status, 228);
    lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(status, "");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_palette_main(LV_PALETTE_GREY), 0);
    pp_status_register(PP_PAGE_CFG, status);
}

static void pp_home_cfg_back_cb(lv_event_t *e) { pp_set_page(PP_PAGE_HOME); }

static void pp_cfg_key(char c)
{
    lv_obj_t *ta = s_cfg_focus_key ? s_cfg_key_ta : s_cfg_base_ta;
    if (!ta) return;
    if (c == '\t') {
        s_cfg_focus_key = !s_cfg_focus_key;
        return;
    }
    if (c == '\b') {
        const char *txt = lv_textarea_get_text(ta);
        if (txt && txt[0] != '\0') lv_textarea_del_char(ta);
        else pp_set_page(PP_PAGE_HOME);
        return;
    }
    if (c == '\n') return;                /* one-line fields */
    if (c >= 0x20) {
        lv_textarea_add_char(ta, c);
    }
}

/* ---- UTF-8 helper -------------------------------------------------------- */
int pp_utf8_count(const char *s)
{
    if (!s) return 0;
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p & 0xC0) != 0x80) n++;     /* count non-continuation bytes */
    }
    return n;
}

/* ---- keyboard poll ------------------------------------------------------- */
void penpal_keyboard_poll(void)
{
    pp_waitbox_tick();

    /* drain results FIRST (chat pattern): the waitbox closes when its
     * result arrives; ownership moves to the UI here */
    pp_result_t *res = NULL;
    while (s_pp_q && xQueueReceive(s_pp_q, &res, 0) == pdTRUE) {
        if (res) {
            pp_consume(res);
            delete res;
        }
    }

    if (!s_pp_active) return;

    /* notice box: +/- scrolls the box (the body label itself does not
     * scroll), any other key closes (the topics suggestion overlay has its
     * own Use/Cancel keys inside ppw_key) */
    if (s_msgbox) {
        char c;
        if (!keypad_get_val(&c)) return;
        keypad_set_flag();
        if (c == '+' || c == '-') {
            lv_obj_scroll_by(s_msgbox, 0, c == '+' ? -60 : 60, LV_ANIM_OFF);
        } else {
            pp_msgbox_close();
        }
        return;
    }

    /* waitbox up: swallow the keyboard - Close is a touch button (chat
     * pattern; the split Close semantics are touch-side, §3.2) */
    if (s_waitbox) {
        char c;
        if (keypad_get_val(&c)) keypad_set_flag();
        return;
    }

    /* burst processing: coalesce a typed run into one EPD render */
    for (int guard = 0; guard < 32; guard++) {
        char c;
        if (!keypad_get_val(&c)) break;
        keypad_set_flag();
        switch (s_cur_page) {
        case PP_PAGE_HOME: pp_home_key(c); break;
        case PP_PAGE_COMPOSE:
        case PP_PAGE_TOPICS: ppw_key(c); break;
        case PP_PAGE_THREAD:
        case PP_PAGE_FB:
        case PP_PAGE_PROFILE: ppr_key(c); break;
        case PP_PAGE_CFG: pp_cfg_key(c); break;
        default: break;
        }
    }
}

/* ---- screen lifecycle ---------------------------------------------------- */
static void pp_create(lv_obj_t *parent)
{
    memset(s_pages, 0, sizeof(s_pages));
    memset(s_status_lab, 0, sizeof(s_status_lab));
    pp_home_build(parent);
    ppw_build(parent);                    /* COMPOSE + TOPICS */
    ppr_build(parent);                    /* THREAD + FB + PROFILE */
    pp_cfg_build(parent);
    pp_set_page(PP_PAGE_HOME);
    pp_home_render_pals();
    pp_home_render_rows();
    /* widgets only - scr_mgr runs create() at REGISTER time (boot), any
     * request started here would be invalidated by entry()'s gen++ before
     * its result arrives (Codex P1). Auto-sync lives in pp_entry(). */
}

static void pp_entry(void)
{
    ui_disp_full_refr();
    s_pp_gen++;                           /* invalidate prior-visit results */
    s_pp_active = true;
    /* auto-sync on the FIRST entry of a visit, after gen++ so the request
     * carries the generation that will consume it (§4.1, Codex P1) */
    if (!s_pp_autosynced) {
        s_pp_autosynced = true;
        char base[PP_BASE_MAX], key[PP_KEY_MAX];
        pp_cfg_load(base, sizeof(base), key, sizeof(key));
        if (base[0] && key[0]) {
            pp_home_sync(false);
        } else {
            pp_status_set("configure server in Cfg");
        }
    }
}

static void pp_exit(void)
{
    ui_disp_full_refr();
    ui_disp_suppress_flush(false);        /* a mid-scroll leave must not
                                           * freeze the display */
    pp_waitbox_hide();
    pp_msgbox_close();
    s_pp_active = false;
}

/* In-place reset of the screen state. pp_state_t is ~15KB (24 mailbox rows
 * + 16 topics + 64 letters + scratch) - the old "pp = pp_state_t()" built a
 * temporary of that size ON THE STACK and tripped the loopTask 8KB canary
 * on EVERY Back press (serial evidence 2026-08-22: "Stack canary watchpoint
 * triggered (loopTask)", inside scr_mgr_pop -> pp_destroy). Reset per field
 * instead: each per-element temp is <= ~370B. Strings must go through
 * per-element value-init (element dtor + empty init), never memset. */
static void pp_state_reset(void)
{
    for (int i = 0; i < PP_PAL_MAX; i++) pp.pals[i] = pp_pal_t{};
    pp.pals_cnt = 0;
    for (int i = 0; i < PP_MAILBOX_MAX; i++) pp.rows[i] = pp_thread_row_t{};
    pp.rows_cnt = 0;
    pp.mailbox_truncated = false;
    pp.home_page = 0;
    pp.reply_mode = false;
    pp.comp_pal_id = 0;
    pp.comp_pal_name[0] = 0;
    pp.comp_has_topic = false;
    pp.comp_topic_id = 0;
    pp.comp_topic_title[0] = 0;
    pp.comp_root_id = 0;
    pp.idem_valid = false;
    pp.idem_key[0] = 0;
    pp.idem_snap = pp_send_req_t{};       /* small (~100B) - stack-safe */
    pp.send_lock = false;
    for (int i = 0; i < PP_TOPIC_MAX; i++) pp.topics[i] = pp_topic_t{};
    pp.topics_cnt = 0;
    pp.topics_page = 0;
    pp.topics_sel = -1;
    for (int i = 0; i < PP_THREAD_MAX; i++) pp.letters[i] = pp_letter_t{};
    pp.letters_cnt = 0;
    pp.thr_root = 0;
    pp.thr_pal = 0;
    pp.thr_subject[0] = 0;
    pp.thr_idx = 0;
    pp.thr_dropped = 0;
    pp.fb_degraded = false;
    pp.fmt[0] = 0;
}

static void pp_destroy(void)
{
    s_pp_active = false;
    ppw_overlays_close();
    ppr_overlays_close();
    pp_msgbox_close();
    pp_waitbox_hide();
    s_pp_gen++;                           /* late results of this visit drop */
    /* safe to clear busy here: the in-flight task owns its snapshot and its
     * result is dropped by gen (chat pattern). Screen rebuild makes the
     * idempotency payload != snapshot, so the key cannot be misused. */
    s_pp_busy = false;
    pp_state_reset();                     /* strings released; state re-zeroed */
    s_home_note[0] = 0;
    s_pp_autosynced = false;              /* next visit auto-syncs again */
    memset(s_pages, 0, sizeof(s_pages));           /* lv_obj_t* array: POD */
    memset(s_status_lab, 0, sizeof(s_status_lab)); /* lv_obj_t* array: POD */
    Serial.println("[PenPal] destroy done");       /* back-press regression marker */
}

scr_lifecycle_t screen_penpal = {
    .create = pp_create,
    .entry = pp_entry,
    .exit = pp_exit,
    .destroy = pp_destroy,
};
