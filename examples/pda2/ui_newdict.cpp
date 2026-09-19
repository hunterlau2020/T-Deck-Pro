/**
 * @file      ui_newdict.cpp
 * @brief     Word Bank app "new_dict" - "⑫ 词库浏览 + ⑫b 考试选卡" on the
 *            device (user request 2026-09-18; search-first + tabs v1.20).
 *
 * Endpoints (penpal_api.h; read-only, learn scope):
 *   GET /api/v1/words?skip&limit&q&exam  one page; q fuzzy-matches word OR
 *        meaning_zh, exam is an exact exam_tags match (Home searches, List
 *        filters); both percent-encoded in penpal_api.
 *   GET /api/v1/words/exams              exam cards (exam + count, desc).
 *   GET /api/v1/words/{id}/detail        detail card, flattened for EPD.
 *
 * Async model (whoami wa_* pattern, async_ipc_contract §1): one worker task
 * at a time, result struct's first field is the screen generation; results
 * from a stale generation are dropped and freed by the consume path.
 *
 * UI (one screen, LV_OBJ_FLAG_HIDDEN page switch): two TABS on top -
 *   HOME  search-first (local Dict heritage): a one-line input box + result
 *         rows; typing edits the query, Enter searches (or opens the focused
 *         row when the query is unchanged), +/- focus/pages rows, tap opens.
 *   LIST  exam cards ("⑯ exam-book"): GET /words/exams rendered as rows
 *         ("IELTS  5700"); opening one switches to that exam's word list
 *         (server-paginated with +/-); Backspace climbs back words->exams.
 *   DETAIL (shared, either tab) word/phonetic/CEFR/meaning + senses +
 *         examples + chunks + assoc; the body is flattened and locally
 *         paged with +/- (EPD: one re-render per turn, never scrolled);
 *         Enter/Backspace returns to the caller.
 *
 * Keyboard: letters/digits edit the query (from EITHER tab - typing on List
 * jumps to Home), Enter search/open, +/- turn whole pages (touch picks
 * rows; DETAIL: +/- pages the card), Backspace delete-query-
 * char / climb / exit. Touch: tabs, rows. Chinese switches labels to
 * Font_Hanzi_16 per content (LevelTest v1.16 pattern).
 */
#include "Arduino.h"
#include <WiFi.h>
#include "ui_newdict.h"
#include "ui_deckpro_port.h"
#include "penpal_api.h"
#include "peripheral.h"
#include "src/assets.h"
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

using namespace std;

/* ui_deckpro.cpp keeps this macro private - mirror it (same assets font) */
#define ND_FONT (&Font_Mono_Bold_15)

/* ---- screen state (UI thread owned) ---------------------------------------- */

enum { ND_PAGE_TABS = 0, ND_PAGE_DETAIL };   /* top-level pages */

enum { ND_TAB_HOME = 0, ND_TAB_LIST };
enum { ND_LS_EXAMS = 0, ND_LS_WORDS };       /* List tab sub-states */

static int s_nd_page = ND_PAGE_TABS;
static int s_tab = ND_TAB_HOME;

/* HOME: search */
static char s_query[26];                 /* typed search text ("" = none yet) */
static bool s_query_dirty = false;       /* typed since the last search */
static pp_wb_page_t s_list;              /* last fetched word page (HOME) */
static int s_focus = 0;                  /* focused row in the visible list */
static int s_skip = 0;                   /* next word-page request offset */

/* LIST: exam cards + chosen exam's words */
static pp_wb_exam_t s_exams[PP_WB_EXAM_MAX];
static int s_exam_count = 0;
static int s_exam_total_words = 0;
static int s_exam_focus = 0;             /* focused exam card (local paging) */
static int s_exam_top = 0;               /* first visible exam card index */
static int s_list_sub = ND_LS_EXAMS;
static char s_cur_exam[20];              /* opened exam ("", exams view) */
static pp_wb_page_t s_exam_list;         /* words of s_cur_exam */
static int s_exam_list_focus = 0;

static pp_wb_detail_t s_detail;
static char s_status[96] = "type a word, Enter: search";
static bool s_enter_pending = false;     /* Enter buffered across an
                                          * in-flight request (LevelTest
                                          * v1.18 lesson: never eat keys) */
static bool s_exams_deferred = false;    /* List tab opened while the worker
                                          * was busy (P2-1): fire the lazy
                                          * exams fetch once idle again */

/* DETAIL local pagination (EPD discipline: slice + re-render, no scroll).
 * Page geometry is MEASURED, not estimated: glyph advances come from the
 * LVGL font, page capacity from the live container size - only one safety
 * row is subtracted, so a page fills the area down to the hint label. */
#define ND_D_LINES_MAX  12               /* senses 6 + ex 2 + chunks 2 + assoc 1 */
#define ND_D_PAGES_MAX  8
#define ND_D_FALLBACK_W 200              /* before the first layout pass */
#define ND_D_FALLBACK_H 180              /* ditto: err toward shorter pages */

static char s_d_body[ND_D_LINES_MAX][164];   /* flattened logical lines */
static int  s_d_body_h[ND_D_LINES_MAX];      /* measured px height per line */
static int  s_d_body_n = 0;
static int  s_d_page_top[ND_D_PAGES_MAX];    /* first body line of each page */
static int  s_d_pages = 1, s_d_page = 0;

/* ---- LVGL objects (created once per create; nulled on destroy) ------------- */

static lv_obj_t *s_pg_tabs, *s_pg_detail;
static lv_obj_t *s_tab_btn[2];
static lv_obj_t *s_search_ta;
static lv_obj_t *s_home_head_lab;
static lv_obj_t *s_row[PP_WB_ROWS];      /* shared by HOME results / exams /
                                          * exam words - one row pool */
static lv_obj_t *s_d_head_lab, *s_d_body_lab, *s_d_hint_lab;
static lv_obj_t *s_status_lab;

static void nd_open_detail(int row);     /* below */
static void nd_open_exam(int row);       /* below */
static void nd_tab_set(int tab);         /* below */

static void nd_status(const char *txt)
{
    snprintf(s_status, sizeof(s_status), "%s", txt);
    if (s_status_lab) lv_label_set_text(s_status_lab, s_status);
}

/* CJK-aware label text (LevelTest v1.16 pattern) */
static bool nd_has_cjk(const char *txt)
{
    for (const unsigned char *p = (const unsigned char *)txt; p && *p; p++)
        if (*p & 0x80) return true;
    return false;
}

static void nd_set_text(lv_obj_t *lab, const char *txt)
{
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab,
        nd_has_cjk(txt) ? &Font_Hanzi_16 : ND_FONT, LV_PART_MAIN);
}

/* ---- async glue (wa_* pattern) ---------------------------------------------- */

enum { ND_REQ_LIST = 0, ND_REQ_EXAMS, ND_REQ_DETAIL };

struct nd_msg_t {
    uint32_t gen;                        /* screen generation (contract rule 2) */
    int kind;
    bool ok;
    bool exam_words;                     /* LIST destination snapshot (P2-1):
                                          * land the result by where it was
                                          * asked, not by the tab shown on
                                          * arrival */
    char q[26];                          /* LIST home: query snapshot - text
                                          * typed during the flight must not
                                          * be "confirmed" by this result */
    char err[96];
    pp_wb_page_t page;                   /* LIST */
    int exam_count;                      /* EXAMS */
    int exam_total_words;
    pp_wb_exam_t exams[PP_WB_EXAM_MAX];
    pp_wb_detail_t detail;               /* DETAIL */
};

struct nd_req_t {
    uint32_t gen;
    int kind;
    bool exam_words;                     /* LIST: destination snapshot */
    string base;
    string key;
    int skip;                            /* LIST */
    string q;                            /* LIST (home search) */
    string exam;                         /* LIST (exam words) */
    int id;                              /* DETAIL */
};

static QueueHandle_t s_nd_q = NULL;
static volatile TaskHandle_t s_nd_task = NULL;
static uint32_t s_nd_gen = 0;            /* bumped on entry/exit: stale drops */

static void nd_task_func(void *param)
{
    nd_req_t *rq = (nd_req_t *)param;    /* task-owned snapshot */
    nd_msg_t *m = new nd_msg_t;
    m->gen = rq->gen;
    m->kind = rq->kind;
    m->exam_words = rq->exam_words;      /* P2-1: carry the destination */
    snprintf(m->q, sizeof(m->q), "%s", rq->q.c_str());
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    if (rq->kind == ND_REQ_LIST) {
        m->ok = penpal_wb_list(base, key, rq->skip, rq->q.c_str(),
                               rq->exam.c_str(), &m->page, &err);
    } else if (rq->kind == ND_REQ_EXAMS) {
        m->exam_count = 0;
        m->ok = penpal_wb_exams(base, key, m->exams, PP_WB_EXAM_MAX,
                                &m->exam_count, &m->exam_total_words, &err);
    } else {
        m->ok = penpal_wb_detail(base, key, rq->id, &m->detail, &err);
    }
    snprintf(m->err, sizeof(m->err), "%s", err.c_str());

    if (s_nd_q) {
        if (xQueueSend(s_nd_q, &m, pdMS_TO_TICKS(2000)) != pdTRUE)
            delete m;                   /* Nit-1: implement the documented
                                          * bounded-send leak guard */
    } else {
        delete m;
    }
    delete rq;
    s_nd_task = NULL;
    vTaskDelete(NULL);
}

/* Single flight + preflight (whoami wa_start pattern). The queue is created
 * once and never deleted. */
static bool nd_start(int kind, int detail_row)
{
    if (s_nd_task) {
        nd_status("busy - wait for current request");
        return false;
    }
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    penpal_load_config(base, sizeof(base), key, sizeof(key));
    if (base[0] == '\0' || key[0] == '\0') {
        nd_status("server/key not set (Whoami Cfg)");
        Serial.println("[ND] start refused: server/key not set");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        nd_status("WiFi not connected");
        Serial.println("[ND] start refused: WiFi not connected");
        return false;
    }
    if (!s_nd_q) s_nd_q = xQueueCreate(4, sizeof(void *));
    if (!s_nd_q) {
        nd_status("queue create failed");
        return false;
    }

    nd_req_t *rq = new nd_req_t;
    rq->gen = s_nd_gen;
    rq->kind = kind;
    rq->base = base;
    rq->key = key;
    if (kind == ND_REQ_LIST) {
        if (s_tab == ND_TAB_LIST) {      /* exam words (List tab) */
            rq->skip = s_skip;
            rq->exam = s_cur_exam;
            rq->exam_words = true;
        } else {                         /* home search */
            rq->skip = s_skip;
            rq->q = s_query;
            rq->exam_words = false;
        }
    } else if (kind == ND_REQ_DETAIL) {
        rq->id = (s_tab == ND_TAB_LIST && s_list_sub == ND_LS_WORDS)
                     ? s_exam_list.items[detail_row].id
                     : s_list.items[detail_row].id;
    }

    if (xTaskCreate(nd_task_func, "newdict", 1024 * 8, rq, 1,
                    (TaskHandle_t *)&s_nd_task) != pdPASS) {
        s_nd_task = NULL;
        delete rq;
        nd_status("task create failed");
        return false;
    }
    return true;
}

/* ---- UI builders -------------------------------------------------------------- */

static void nd_scr_pop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        scr_mgr_pop(false);
    }
}

static void nd_tab_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
        nd_tab_set((int)(intptr_t)lv_event_get_user_data(e));
}

/* Row tap context depends on the visible page (row pool is shared):
 * Home results / exam words open the detail card, exam cards open the book. */
static void nd_row_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int row = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_nd_page == ND_PAGE_TABS && s_tab == ND_TAB_LIST &&
        s_list_sub == ND_LS_EXAMS)
        nd_open_exam(row);
    else
        nd_open_detail(row);
}

static lv_obj_t *nd_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, ND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *nd_row_create(lv_obj_t *parent, int idx)
{
    lv_obj_t *row = lv_btn_create(parent);
    lv_obj_remove_style_all(row);
    /* 22px rows (acceptance 2026-09-19): the tabs page content box is
     * 263px and the old 8x26+hint stack overflowed it - the last two
     * rows and the hint were silently clipped (focus "vanished") */
    lv_obj_set_size(row, lv_pct(100), 23);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, nd_row_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)idx);
    lv_obj_t *l = lv_label_create(row);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l, ND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
    return l;                            /* label is the row handle */
}

static void create_nd(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Word Bank", nd_scr_pop_cb);

    /* TABS page: tab buttons + Home(search) / List(exams|words) below */
    s_pg_tabs = lv_obj_create(parent);
    lv_obj_set_size(s_pg_tabs, lv_pct(100), lv_pct(86));
    lv_obj_align(s_pg_tabs, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(s_pg_tabs, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pg_tabs, 0, LV_PART_MAIN);
    /* pad 2 (acceptance A-1 v1.25): the old pad 6 ate 8px the row stack
     * needed - row 8 was clipped again on device */
    lv_obj_set_style_pad_all(s_pg_tabs, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_pg_tabs, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pg_tabs, 3, LV_PART_MAIN);
    lv_obj_clear_flag(s_pg_tabs, LV_OBJ_FLAG_SCROLLABLE);

    /* tab row */
    lv_obj_t *tabrow = lv_obj_create(s_pg_tabs);
    lv_obj_set_size(tabrow, lv_pct(100), 26);
    lv_obj_set_flex_flow(tabrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_bg_opa(tabrow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabrow, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabrow, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tabrow, LV_OBJ_FLAG_SCROLLABLE);
    static const char *tab_names[2] = {"Home", "List"};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_btn_create(tabrow);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 76, 26);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, nd_tab_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_set_style_border_width(b, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(b, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, ND_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
        lv_label_set_text(l, tab_names[i]);
        lv_obj_center(l);
        s_tab_btn[i] = b;
    }

    /* Home search box (local Dict heritage; EPD discipline v1.12: cursor
     * part bg transparent + anim_time 0 - no blink, no visible caret; the
     * typed text itself is the echo). */
    s_search_ta = lv_textarea_create(s_pg_tabs);
    lv_obj_set_width(s_search_ta, lv_pct(100));
    lv_obj_set_height(s_search_ta, 24);
    lv_obj_set_style_pad_top(s_search_ta, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_search_ta, 2, LV_PART_MAIN);
    lv_textarea_set_one_line(s_search_ta, true);
    lv_textarea_set_max_length(s_search_ta, sizeof(s_query) - 1);
    lv_textarea_set_placeholder_text(s_search_ta, "type to search");
    lv_obj_set_style_text_font(s_search_ta, ND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_search_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_search_ta, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_search_ta, LV_OPA_TRANSP,
                            LV_PART_CURSOR | LV_PART_MAIN);
    lv_obj_set_style_anim_time(s_search_ta, 0, LV_PART_CURSOR);

    s_home_head_lab = nd_label(s_pg_tabs, "");

    for (int i = 0; i < PP_WB_ROWS; i++) s_row[i] = nd_row_create(s_pg_tabs, i);
    /* no hint label on the tabs page - the status line carries the page
     * indicator now (the old hint was the clipped overflow's last victim) */

    /* DETAIL page */
    s_pg_detail = lv_obj_create(parent);
    lv_obj_set_size(s_pg_detail, lv_pct(100), lv_pct(86));
    lv_obj_align(s_pg_detail, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(s_pg_detail, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pg_detail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pg_detail, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_pg_detail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pg_detail, 4, LV_PART_MAIN);
    lv_obj_clear_flag(s_pg_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pg_detail, LV_OBJ_FLAG_HIDDEN);
    s_d_head_lab = nd_label(s_pg_detail, "");
    s_d_body_lab = nd_label(s_pg_detail, "");
    s_d_hint_lab = nd_label(s_pg_detail, "Enter/Back: back");

    /* shared status line (always black - EPD lesson, issue_list §23) */
    s_status_lab = lv_label_create(parent);
    lv_obj_set_width(s_status_lab, lv_pct(100));
    lv_label_set_long_mode(s_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status_lab, ND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lab, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_status_lab, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_label_set_text(s_status_lab, s_status);

    /* one-shot layout telemetry (acceptance A-1: hand-budgeted px clipped
     * row 8 twice on device - log the real numbers for the next retune) */
    lv_obj_update_layout(s_pg_tabs);
    Serial.printf("[ND] layout: content %d, tab %d, ta %d, head %d, row %d, "
                  "rows8 %d, mono lh %d, hanzi lh %d\n",
                  (int)lv_obj_get_height(s_pg_tabs) - 4,
                  (int)lv_obj_get_height(tabrow),
                  (int)lv_obj_get_height(s_search_ta),
                  (int)lv_obj_get_height(s_home_head_lab),
                  (int)lv_obj_get_height(s_row[0]),
                  PP_WB_ROWS * 23 + 4 * 3,
                  (int)ND_FONT->line_height,
                  (int)Font_Hanzi_16.line_height);
}

/* ---- render (one EPD flush per state change) ---------------------------------- */

static void nd_page_show(int page)
{
    s_nd_page = page;
    if (page == ND_PAGE_TABS)
        lv_obj_clear_flag(s_pg_tabs, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_pg_tabs, LV_OBJ_FLAG_HIDDEN);
    if (page == ND_PAGE_DETAIL)
        lv_obj_clear_flag(s_pg_detail, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_pg_detail, LV_OBJ_FLAG_HIDDEN);
}

static void nd_tab_style(void)
{
    /* EPD-safe active cue: black bg + white text vs transparent + black
     * (issue_list §23: a gray fill like LV_OPA_20 blends to ~204 > the
     * 128 threshold and renders WHITE - invisible). */
    for (int i = 0; i < 2; i++) {
        bool act = (i == s_tab);
        lv_obj_set_style_bg_opa(s_tab_btn[i],
            act ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_tab_btn[i], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_obj_get_child(s_tab_btn[i], 0),
            act ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }
}

/* bottom page indicator for the visible list view (acceptance 2026-09-19:
 * same pattern as DETAIL). Only fires when there is content, so transient
 * caller messages and the idle hint survive an empty render. */
static bool nd_render_page_indicator(void)
{
    int page = 0, pages = 0;
    if (s_nd_page != ND_PAGE_TABS) return false;
    if (s_tab == ND_TAB_HOME) {
        if (s_list.count > 0) {
            page = s_list.skip / PP_WB_ROWS + 1;
            pages = (s_list.total + PP_WB_ROWS - 1) / PP_WB_ROWS;
        }
    } else if (s_list_sub == ND_LS_EXAMS) {
        if (s_exam_count > 0) {
            page = s_exam_top / PP_WB_ROWS + 1;
            pages = (s_exam_count + PP_WB_ROWS - 1) / PP_WB_ROWS;
        }
    } else {
        if (s_exam_list.count > 0) {
            page = s_exam_list.skip / PP_WB_ROWS + 1;
            pages = (s_exam_list.total + PP_WB_ROWS - 1) / PP_WB_ROWS;
        }
    }
    if (pages > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "page %d/%d", page, pages);
        nd_status(buf);
    }
    return pages > 0;
}

static bool nd_render_tabs(void)
{
    nd_tab_style();
    /* the search box lives on HOME only (acceptance 2026-09-19: on LIST
     * it was a dead "type to search" strip - hidden exits the flex flow) */
    if (s_tab == ND_TAB_HOME)
        lv_obj_clear_flag(s_search_ta, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(s_search_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_border_width(s_search_ta, s_tab == ND_TAB_HOME ? 2 : 0,
                                  LV_PART_MAIN);

    if (s_tab == ND_TAB_HOME) {
        char head[64];
        if (s_list.count > 0)
            snprintf(head, sizeof(head), "Found %d-%d / %d", s_list.skip + 1,
                     s_list.skip + s_list.count, s_list.total);
        else
            snprintf(head, sizeof(head), "%s", s_query[0] ? "(no result)" : "");
        lv_label_set_text(s_home_head_lab, head);
        for (int i = 0; i < PP_WB_ROWS; i++) {
            if (i < s_list.count) {
                char buf[96];
                snprintf(buf, sizeof(buf), "%s %-12s %-5s %s",
                         i == s_focus ? ">" : " ", s_list.items[i].word,
                         s_list.items[i].cefr_level, s_list.items[i].meaning_zh);
                lv_obj_clear_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
                nd_set_text(s_row[i], buf);
            } else {
                lv_obj_add_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else if (s_list_sub == ND_LS_EXAMS) {
        char head[72];
        int vis = s_exam_count - s_exam_top;
        if (vis > PP_WB_ROWS) vis = PP_WB_ROWS;
        snprintf(head, sizeof(head), "Exams %d-%d/%d, %d words",
                 s_exam_top + 1, s_exam_top + vis, s_exam_count,
                 s_exam_total_words);
        lv_label_set_text(s_home_head_lab, head);
        for (int i = 0; i < PP_WB_ROWS; i++) {
            int idx = s_exam_top + i;
            if (idx < s_exam_count) {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s %-14s %5d",
                         idx == s_exam_focus ? ">" : " ", s_exams[idx].exam,
                         s_exams[idx].count);
                lv_obj_clear_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
                nd_set_text(s_row[i], buf);
            } else {
                lv_obj_add_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {                             /* exam words */
        char head[72];
        if (s_exam_list.count > 0)
            snprintf(head, sizeof(head), "[%s] %d-%d / %d", s_cur_exam,
                     s_exam_list.skip + 1,
                     s_exam_list.skip + s_exam_list.count, s_exam_list.total);
        else
            snprintf(head, sizeof(head), "[%s] (empty)", s_cur_exam);
        nd_set_text(s_home_head_lab, head);
        for (int i = 0; i < PP_WB_ROWS; i++) {
            if (i < s_exam_list.count) {
                char buf[96];
                snprintf(buf, sizeof(buf), "%s %-12s %-5s %s",
                         i == s_exam_list_focus ? ">" : " ",
                         s_exam_list.items[i].word,
                         s_exam_list.items[i].cefr_level,
                         s_exam_list.items[i].meaning_zh);
                lv_obj_clear_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
                nd_set_text(s_row[i], buf);
            } else {
                lv_obj_add_flag(s_row[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    nd_page_show(ND_PAGE_TABS);
    return nd_render_page_indicator();
}

/* one UTF-8 codepoint; *pp advanced past its bytes */
static uint32_t nd_next_cp(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t cp;
    int n;
    if (p[0] < 0x80)      { cp = p[0];        n = 1; }
    else if (p[0] < 0xE0) { cp = p[0] & 0x1F; n = 2; }
    else if (p[0] < 0xF0) { cp = p[0] & 0x0F; n = 3; }
    else                  { cp = p[0] & 0x07; n = 4; }
    for (int i = 1; i < n && p[i]; i++) cp = (cp << 6) | (p[i] & 0x3F);
    *pp += n;
    return cp;
}

/* exact glyph advance from the font (fallback: half a row, conservative) */
static int nd_glyph_w(const lv_font_t *f, uint32_t cp)
{
    lv_font_glyph_dsc_t dsc;
    if (lv_font_get_glyph_dsc(f, &dsc, cp, 0)) return dsc.box_w;
    return f->line_height / 2;
}

/* rendered px height of one logical line in font f: char-level wrap on
 * measured advances, width kept a few px shy of the label so LVGL's
 * word-boundary wrapping can only ever err toward shorter pages */
static int nd_text_px_h(const char *s, const lv_font_t *f, int wrap_w)
{
    int row_w = 0, rows = 1;
    for (; s && *s;) {
        uint32_t cp = nd_next_cp(&s);
        int cw = nd_glyph_w(f, cp);
        if (row_w > 0 && row_w + cw > wrap_w) {
            rows++;
            row_w = 0;
        }
        row_w += cw;
    }
    return rows * f->line_height;
}

/* render the current DETAIL body page + the page-count hint in the
 * status line (one label write = one EPD refresh, never scrolled) */
static void nd_render_detail_page(void)
{
    int top = s_d_page_top[s_d_page];
    int end = (s_d_page + 1 < s_d_pages) ? s_d_page_top[s_d_page + 1]
                                         : s_d_body_n;
    static char buf[1100];               /* worst page = whole body */
    int off = 0;
    for (int i = top; i < end; i++)
        off += snprintf(buf + off, sizeof(buf) - off, "%s\n", s_d_body[i]);
    if (off == 0) buf[0] = '\0';
    lv_label_set_text(s_d_body_lab, buf);
    lv_obj_set_style_text_font(s_d_body_lab, &Font_Hanzi_16, LV_PART_MAIN);
        /* font pinned to match the measurement font (body text carries zh
         * in practice, so this is also the N1 zh-safe choice) */

    if (s_d_pages > 1) {
        char pg[32];
        snprintf(pg, sizeof(pg), "page %d/%d  (+/- turns)",
                 s_d_page + 1, s_d_pages);
        nd_status(pg);
    } else {
        nd_status("");
    }
}

static void nd_render_detail(void)
{
    char head[96];
    if (s_detail.phonetic[0])
        snprintf(head, sizeof(head), "%s  %s  %s\n%s", s_detail.word,
                 s_detail.phonetic, s_detail.cefr_level, s_detail.meaning_zh);
    else
        snprintf(head, sizeof(head), "%s  %s\n%s", s_detail.word,
                 s_detail.cefr_level, s_detail.meaning_zh);
    nd_set_text(s_d_head_lab, head);

    /* flatten the body into logical lines (N1: nd_set_text re-runs the
     * CJK font switch per rendered page, so zh lines stay visible) */
    s_d_body_n = 0;
    if (s_detail.sense_count == 0 && s_d_body_n < ND_D_LINES_MAX)
        snprintf(s_d_body[s_d_body_n++], sizeof(s_d_body[0]), "(no senses)");
    for (int i = 0; i < s_detail.sense_count && s_d_body_n < ND_D_LINES_MAX;
         i++)
        snprintf(s_d_body[s_d_body_n++], sizeof(s_d_body[0]), "%s",
                 s_detail.senses[i]);
    for (int i = 0; i < s_detail.ex_count && s_d_body_n < ND_D_LINES_MAX; i++)
        snprintf(s_d_body[s_d_body_n++], sizeof(s_d_body[0]), "- %s",
                 s_detail.examples[i]);
    for (int i = 0; i < s_detail.chunk_count && s_d_body_n < ND_D_LINES_MAX;
         i++)
        snprintf(s_d_body[s_d_body_n++], sizeof(s_d_body[0]), "# %s",
                 s_detail.chunks[i]);
    if (s_detail.assoc[0] && s_d_body_n < ND_D_LINES_MAX)
        snprintf(s_d_body[s_d_body_n++], sizeof(s_d_body[0]), "%s",
                 s_detail.assoc);

    /* measured page geometry: body font is fixed (see the page renderer),
     * the head font follows nd_set_text's CJK switch like on screen */
    const lv_font_t *body_f = &Font_Hanzi_16;
    lv_obj_update_layout(s_pg_detail);
    int wrap_w = lv_obj_get_width(s_d_body_lab) - 6;
    int area_h = lv_obj_get_height(s_pg_detail) - 4;    /* minus pad 2x2 */
    if (wrap_w < 100) wrap_w = ND_D_FALLBACK_W;
    if (area_h < 120) area_h = ND_D_FALLBACK_H;
    int head_px = nd_text_px_h(head, nd_has_cjk(head) ? body_f : ND_FONT,
                               wrap_w);
    int cap = area_h - head_px - ND_FONT->line_height - 8 /* flex row gaps */
              - body_f->line_height;              /* one safety row */
    if (cap < body_f->line_height) cap = body_f->line_height;

    /* greedy slice in px; a line taller than a page gets its own */
    s_d_pages = 1;
    s_d_page_top[0] = 0;
    int used = 0;
    for (int i = 0; i < s_d_body_n; i++) {
        s_d_body_h[i] = nd_text_px_h(s_d_body[i], body_f, wrap_w);
        if (used > 0 && used + s_d_body_h[i] > cap &&
            s_d_pages < ND_D_PAGES_MAX) {
            s_d_page_top[s_d_pages++] = i;
            used = 0;
        }
        used += s_d_body_h[i];
    }
    s_d_page = 0;
    nd_render_detail_page();
    nd_page_show(ND_PAGE_DETAIL);
}

/* ---- result consumption (UI thread, factory loop) ------------------------------ */

static void nd_consume(void)
{
    if (!s_nd_q) return;
    void *p = NULL;
    nd_msg_t *m = NULL;
    while (xQueueReceive(s_nd_q, &p, 0) == pdTRUE) {
        m = (nd_msg_t *)p;
        if (m->gen == s_nd_gen) break;   /* latest screen generation wins */
        delete m;                        /* stale: dropped and freed */
        m = NULL;
    }
    if (!m) return;

    /* order matters: render FIRST (it refreshes the page indicator), then
     * any transient status message on top - a message set before the
     * render would be wiped by the indicator */
    if (m->kind == ND_REQ_LIST) {
        if (!m->ok) {
            nd_render_tabs();
            char buf[128];
            snprintf(buf, sizeof(buf), "list failed: %s", m->err);
            nd_status(buf);
        } else if (m->exam_words) {      /* P2-1: land by the destination
                                          * the request was given, not by
                                          * the tab visible on arrival */
            s_exam_list = m->page;
            s_exam_list_focus = 0;
            nd_render_tabs();
            if (s_exam_list.count == 0)
                nd_status("exam book empty");
        } else {
            s_list = m->page;
            s_focus = 0;
            s_query_dirty = strcmp(s_query, m->q) != 0;
                /* in-flight typing/deleting is not this result's
                 * confirmation (P2-1 kin) */
            nd_render_tabs();
            if (s_list.count == 0)
                nd_status(s_query[0] ? "no match - keep typing" : "bank empty");
        }
    } else if (m->kind == ND_REQ_EXAMS) {
        if (!m->ok) {
            nd_render_tabs();
            char buf[128];
            snprintf(buf, sizeof(buf), "exams failed: %s", m->err);
            nd_status(buf);
        } else {
            memcpy(s_exams, m->exams, sizeof(s_exams));
            s_exam_count = m->exam_count;
            s_exam_total_words = m->exam_total_words;
            s_exam_focus = s_exam_top = 0;
            nd_render_tabs();
        }
    } else {                             /* DETAIL */
        s_enter_pending = false;         /* N2 (ds4) + its failure-path
                                          * residual (batch round P3): a
                                          * buffered Enter must not outlive
                                          * the request - clear for both
                                          * outcomes (success renders
                                          * DETAIL, failure renders tabs) */
        if (!m->ok) {
            nd_render_tabs();
            char buf[128];
            snprintf(buf, sizeof(buf), "detail failed: %s", m->err);
            nd_status(buf);
        } else {
            s_detail = m->detail;
            nd_render_detail();          /* sets its own page indicator */
        }
    }
    delete m;

    /* P2-1: a List tab opened while the worker was busy deferred its lazy
     * exams fetch - fire it now that a result has landed and gone idle */
    if (s_exams_deferred && !s_nd_task) {
        if (s_tab == ND_TAB_LIST && s_exam_count == 0 &&
            s_nd_page == ND_PAGE_TABS) {
            s_exams_deferred = false;
            nd_status("Fetching exam books...");
            nd_start(ND_REQ_EXAMS, 0);
        } else if (s_tab != ND_TAB_LIST) {
            s_exams_deferred = false;    /* user moved on - not wanted */
        }
    }
}

/* ---- actions -------------------------------------------------------------------- */

/* Row-pool tap/open dispatch by visible page. */
static void nd_open_detail(int row)
{
    if (s_nd_page != ND_PAGE_TABS || s_nd_task) return;
    if (s_tab == ND_TAB_HOME) {
        if (row < 0 || row >= s_list.count) return;
        s_focus = row;
    } else if (s_list_sub == ND_LS_EXAMS) {
        return;                          /* exam cards open via nd_open_exam */
    } else {
        if (row < 0 || row >= s_exam_list.count) return;
        s_exam_list_focus = row;
    }
    nd_status("Fetching detail...");
    nd_start(ND_REQ_DETAIL, row);
}

static void nd_open_exam(int row)
{
    int idx = s_exam_top + row;
    if (s_nd_task || idx < 0 || idx >= s_exam_count) return;
    s_exam_focus = idx;
    snprintf(s_cur_exam, sizeof(s_cur_exam), "%s", s_exams[idx].exam);
    s_list_sub = ND_LS_WORDS;
    s_skip = 0;
    s_exam_list = pp_wb_page_t{};
    s_exam_list_focus = 0;
    nd_render_tabs();                    /* header flips to [exam] at once */
    nd_status("Fetching exam book...");
    nd_start(ND_REQ_LIST, 0);
}

static void nd_fetch_list(int skip)
{
    s_skip = skip;
    nd_status(skip ? "Fetching page..." : "Searching...");
    nd_start(ND_REQ_LIST, 0);
}

static void nd_tab_set(int tab)
{
    if (s_nd_page != ND_PAGE_TABS) return;
    s_tab = tab;
    s_enter_pending = false;            /* N2: page context changed - a
                                          * buffered Enter no longer maps
                                          * to a valid action here */
    if (tab == ND_TAB_LIST && s_exam_count == 0) {
        if (s_nd_task) {
            s_exams_deferred = true;    /* P2-1: busy - pick the lazy fetch
                                          * up in nd_consume once idle */
        } else {
            nd_render_tabs();
            nd_status("Fetching exam books...");
            nd_start(ND_REQ_EXAMS, 0);   /* lazy: only the first List open */
            return;
        }
    }
    /* acceptance A-2: a transient fetch message from the view being left
     * (e.g. "Fetching exam book...") must not outlive the tab switch -
     * an empty target view has no page indicator to overwrite it */
    if (!nd_render_tabs())
        nd_status("");
}

/* ---- keyboard (factory loop) ------------------------------------------------------ */

void newdict_keyboard_poll(void)
{
    if (!s_status_lab) return;           /* screen never created */
    nd_consume();

    /* buffered Enter fires as soon as the task goes idle (LevelTest v1.18:
     * keys are never eaten silently during an in-flight request) */
    if (s_enter_pending && !s_nd_task && s_nd_page == ND_PAGE_TABS) {
        s_enter_pending = false;
        if (s_tab == ND_TAB_HOME) {
            if (s_query_dirty || s_list.count == 0)
                nd_fetch_list(0);
            else
                nd_open_detail(s_focus);
        } else if (s_list_sub == ND_LS_EXAMS) {
            nd_open_exam(s_exam_focus - s_exam_top);
        } else {
            nd_open_detail(s_exam_list_focus);
        }
    }

    char c;
    int guard = 8;
    while (guard-- > 0 && keypad_get_val(&c)) {
        keypad_set_flag();
        if (c == '\t' || c == '\v') continue;  /* combo/volume: ignore */

        if (s_nd_page == ND_PAGE_DETAIL) {
            if (c == '+' || c == '-') {  /* local page turn (EPD: re-render) */
                int np = s_d_page + (c == '+' ? 1 : -1);
                if (np >= 0 && np < s_d_pages) {
                    s_d_page = np;
                    nd_render_detail_page();
                }
            } else if (c == '\n' || c == '\b') {
                nd_render_tabs();
            }
            continue;
        }

        if (c == '\b') {
            if (s_tab == ND_TAB_LIST) {
                if (s_list_sub == ND_LS_WORDS) {
                    s_list_sub = ND_LS_EXAMS;   /* climb: words -> exams */
                    nd_render_tabs();
                    continue;
                }
                scr_mgr_pop(false);      /* exams view: exit app */
                keypad_clear_chars();
                return;
            }
            size_t len = strlen(s_query);
            if (len > 0) {
                do { len--; } while (len > 0 && (s_query[len] & 0xC0) == 0x80);
                s_query[len] = '\0';
                s_query_dirty = true;
                lv_textarea_set_text(s_search_ta, s_query);
                nd_render_tabs();
            } else {
                scr_mgr_pop(false);
                keypad_clear_chars();
                return;
            }
            continue;
        }

        if (c == '\n') {
            if (s_nd_task) {             /* in flight: buffer, don't eat */
                s_enter_pending = true;
                continue;
            }
            if (s_tab == ND_TAB_HOME) {
                if (s_query_dirty || s_list.count == 0)
                    nd_fetch_list(0);
                else
                    nd_open_detail(s_focus);
            } else if (s_list_sub == ND_LS_EXAMS) {
                nd_open_exam(s_exam_focus - s_exam_top);
            } else {
                nd_open_detail(s_exam_list_focus);
            }
            continue;
        }

        if (c == '+' || c == '-') {
            if (s_nd_task) {             /* acceptance 2026-09-19: the old
                                          * silent drop (only Enter is
                                          * buffered) read as "broken
                                          * +/- keys" on HOME */
                nd_status("busy - wait");
                continue;
            }
            /* page-jump semantics (acceptance A-9: touch picks rows,
             * +/- turns whole pages - per-row stepping was noise) */
            if (s_tab == ND_TAB_HOME && s_list.count > 0) {
                if (c == '+') {
                    if (s_list.skip + s_list.count < s_list.total)
                        nd_fetch_list(s_list.skip + PP_WB_ROWS);
                    else
                        nd_status("last page");
                } else {
                    if (s_list.skip > 0)
                        nd_fetch_list(s_list.skip > PP_WB_ROWS ?
                                      s_list.skip - PP_WB_ROWS : 0);
                    else
                        nd_status("first page");
                }
            } else if (s_tab == ND_TAB_LIST && s_list_sub == ND_LS_EXAMS &&
                       s_exam_count > 0) {
                if (c == '+') {
                    if (s_exam_top + PP_WB_ROWS < s_exam_count) {
                        s_exam_top += PP_WB_ROWS;
                        s_exam_focus = s_exam_top;
                        nd_render_tabs();
                    } else
                        nd_status("last page");
                } else {
                    if (s_exam_top > 0) {
                        s_exam_top = s_exam_top > PP_WB_ROWS ?
                                     s_exam_top - PP_WB_ROWS : 0;
                        s_exam_focus = s_exam_top;
                        nd_render_tabs();
                    } else
                        nd_status("first page");
                }
            } else if (s_tab == ND_TAB_LIST && s_list_sub == ND_LS_WORDS &&
                       s_exam_list.count > 0) {
                if (c == '+') {
                    if (s_exam_list.skip + s_exam_list.count <
                        s_exam_list.total)
                        nd_fetch_list(s_exam_list.skip + PP_WB_ROWS);
                    else
                        nd_status("last page");
                } else {
                    if (s_exam_list.skip > 0)
                        nd_fetch_list(s_exam_list.skip > PP_WB_ROWS ?
                                      s_exam_list.skip - PP_WB_ROWS : 0);
                    else
                        nd_status("first page");
                }
            }
            continue;
        }

        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '\'') {
            if (s_tab != ND_TAB_HOME) nd_tab_set(ND_TAB_HOME);  /* jump */
            size_t len = strlen(s_query);
            if (len + 1 < sizeof(s_query)) {
                s_query[len] = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
                s_query[len + 1] = '\0';
                s_query_dirty = true;
                lv_textarea_set_text(s_search_ta, s_query);
                nd_render_tabs();
            }
            continue;
        }
    }
}

/* ---- lifecycle -------------------------------------------------------------------- */

static void entry_nd(void)
{
    ui_disp_full_refr();
    s_nd_gen++;                          /* drop any stale in-flight result */
    s_enter_pending = false;
    s_exams_deferred = false;
    s_nd_page = ND_PAGE_TABS;
    s_tab = ND_TAB_HOME;                 /* search-first (user request) */
    s_list_sub = ND_LS_EXAMS;
    nd_render_tabs();
}

static void exit_nd(void)
{
    ui_disp_full_refr();
    s_nd_gen++;                          /* late results will be dropped */
}

static void destroy_nd(void)
{
    s_pg_tabs = s_pg_detail = NULL;
    s_tab_btn[0] = s_tab_btn[1] = NULL;
    s_search_ta = NULL;
    s_home_head_lab = NULL;
    s_d_head_lab = s_d_body_lab = s_d_hint_lab = NULL;
    s_status_lab = NULL;
    for (int i = 0; i < PP_WB_ROWS; i++) s_row[i] = NULL;
}

scr_lifecycle_t screen_newdict = {
    .create = create_nd,
    .entry = entry_nd,
    .exit = exit_nd,
    .destroy = destroy_nd,
};
