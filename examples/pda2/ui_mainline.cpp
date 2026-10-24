/**
 * @file      ui_mainline.cpp
 * @brief     Learning-spine app - see ui_mainline.h and
 *            docs/mainline-design.md v3.1 (L1-passed, four A verdicts).
 *
 * Async: wa_* single-flight pattern (async_ipc_contract §1/§2) - request
 * snapshot by value, result first field gen, queue created once, 2s
 * bounded send with self-delete, stack 1024*8 prio 1.
 *
 * State machine: HOME(fetch overview -> 4-state card) -> SESSION(items
 * linear, submit per item; 409 decision table per design §3.3/IC-2) ->
 * RESULT(finish stats) -> HOME. All answer-bearing prompt fields are
 * dropped in penpal_api (HW-MAINLINE-1); this UI renders only the
 * per-kind render-set it receives.
 */
#include "Arduino.h"
#include <WiFi.h>
#include "ui_mainline.h"
#include "ui_deckpro_port.h"
#include "penpal_api.h"
#include "ui_penpal.h"        /* pp_msgbox (submit-result popup) */
#include "peripheral.h"
#include "src/assets.h"
#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

using namespace std;

#define ML_FONT (&Font_Mono_Bold_15)      /* deckpro private-macro mirror */

/* ---- UI-thread state ------------------------------------------------------ */

enum { ML_PAGE_HOME = 0, ML_PAGE_SESSION, ML_PAGE_RESULT };

static int s_ml_page = ML_PAGE_HOME;
static char s_status[96] = "Enter: today's lesson";

static bool ml_has_cjk(const char *txt);
static void ml_status_set(const char *txt);
static void ml_render_item(void);

static pp_ml_overview_t s_ov;                 /* HOME payload */
static pp_ml_session_t *s_sess = NULL;  /* heap; ownership moved from the
                                          * worker msg (v1.30.1: the by-value
                                          * copy ate 32KB static RAM and
                                          * starved TLS allocations) */
static int s_cur = 0;                         /* current item index */
static bool s_finishing = false;
static bool s_finish_sent = false;
static pp_ml_finish_t s_fin;

/* recall/cloze/output typed answer */
static char s_input[128];
static int  s_input_len = 0;
/* recognition picked option (1-based, 0 = none) */
static int s_pick = 0;

static bool s_enter_pending = false;          /* v1.18: never eat keys */
static bool s_ov_cached = false;            /* HOME rendered from
                                              * NVS cache (v1.30.5) */
static uint32_t s_popup_ms = 0;               /* submit-result popup open
                                              * time; poll closes it after
                                              * 1.8s and advances */
static char s_popup_feedback[64];             /* feedback shown in popup */
static int  s_popup_next = -1;                /* item to goto on close, or
                                              * -1 = finish */

/* ---- async glue (wa_* pattern) -------------------------------------------- */

enum { ML_REQ_OVERVIEW = 0, ML_REQ_START, ML_REQ_SUBMIT, ML_REQ_FINISH };

struct ml_msg_t {
    uint32_t gen;
    int kind;
    bool ok;
    int  item_idx = -1;                       /* SUBMIT: which item this was */
    char err[160];
    pp_ml_overview_t ov;                      /* OVERVIEW */
    pp_ml_session_t *sess = NULL;             /* START: heap; ownership
                                               * moves to the consumer
                                               * (contract rule 1) */
    pp_ml_submit_t sub;                       /* SUBMIT */
    pp_ml_finish_t fin;                       /* FINISH */
    pp_ml_finish409_t miss;                   /* FINISH 409 detail */
};

struct ml_req_t {
    uint32_t gen;
    int kind;
    string base;
    string key;
    int lesson_id;                            /* START */
    int item_id;                              /* SUBMIT */
    int item_idx;                             /* SUBMIT */
    char user_input[160];                     /* SUBMIT ("" = skipped) */
    bool skipped;
    int session_id;                           /* FINISH */
};

static QueueHandle_t s_ml_q = NULL;
static volatile TaskHandle_t s_ml_task = NULL;
static uint32_t s_ml_gen = 0;

static void ml_task_func(void *param)
{
    ml_req_t *rq = (ml_req_t *)param;
    ml_msg_t *m = new ml_msg_t();       /* zero-init everything */
    m->gen = rq->gen;
    m->kind = rq->kind;
    m->item_idx = rq->item_idx;
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    if (rq->kind == ML_REQ_OVERVIEW) {
        m->ok = penpal_ml_overview(base, key, &m->ov, &err);
    } else if (rq->kind == ML_REQ_START) {
        m->sess = new pp_ml_session_t;        /* consumer deletes (rule 1) */
        m->ok = penpal_ml_lesson_start(base, key, rq->lesson_id,
                                       m->sess, &err);
        if (!m->ok) {
            delete m->sess;
            m->sess = NULL;
        }
    } else if (rq->kind == ML_REQ_SUBMIT) {
        m->ok = penpal_ml_item_submit(base, key, rq->item_id,
                                      rq->skipped ? NULL
                                                  : rq->user_input,
                                      rq->skipped, &m->sub, &err);
    } else {
        m->ok = penpal_ml_session_finish(base, key, rq->session_id,
                                         &m->fin, &m->miss, &err);
    }
    snprintf(m->err, sizeof(m->err), "%s", err.c_str());

    if (s_ml_q) {
        if (xQueueSend(s_ml_q, &m, pdMS_TO_TICKS(2000)) != pdTRUE)
            delete m;                       /* bounded-send leak guard */
    } else {
        delete m;
    }
    delete rq;
    s_ml_task = NULL;
    vTaskDelete(NULL);
}

static bool ml_start(int kind, int lesson_id, int item_id,
                     const char *user_input, bool skipped,
                     int item_idx = -1)
{
    if (s_ml_task) {
        ml_status_set("busy - wait");
        return false;
    }
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    penpal_load_config(base, sizeof(base), key, sizeof(key));
    if (base[0] == '\0' || key[0] == '\0') {
        ml_status_set("server/key not set (Whoami Cfg)");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        ml_status_set("WiFi not connected");
        return false;
    }
    if (!s_ml_q) s_ml_q = xQueueCreate(4, sizeof(void *));
    if (!s_ml_q) {
        ml_status_set("queue create failed");
        return false;
    }

    ml_req_t *rq = new ml_req_t();      /* () zeroes: item_idx
                                           * MUST start at 0, not garbage */
    rq->gen = s_ml_gen;
    rq->kind = kind;
    rq->base = base;
    rq->key = key;
    rq->lesson_id = lesson_id;
    rq->item_id = item_id;
    rq->skipped = skipped;
    rq->item_idx = item_idx;
    snprintf(rq->user_input, sizeof(rq->user_input), "%s",
             user_input ? user_input : "");

    if (xTaskCreate(ml_task_func, "mainline", 1024 * 8, rq, 1,
                    (TaskHandle_t *)&s_ml_task) != pdPASS) {
        s_ml_task = NULL;
        delete rq;
        ml_status_set("task create failed");
        return false;
    }
    return true;
}

/* ---- LVGL objects --------------------------------------------------------- */

static lv_obj_t *s_pg_home, *s_pg_sess, *s_pg_res;
static lv_obj_t *s_h_line[6];                 /* HOME stacked lines */
static lv_obj_t *s_q_kind_lab, *s_q_stem_lab, *s_q_opts[PP_ML_OPT_MAX];
static lv_obj_t *s_q_clue_lab;                /* extra clue line
                                              * (meaning_en / sentence_zh) */
static lv_obj_t *s_q_echo_lab;                /* typed answer echo */
static lv_obj_t *s_r_lab, *s_r_lab2;
static lv_obj_t *s_status_lab;

static void ml_scr_pop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) scr_mgr_pop(false);
}

static void ml_refresh_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_ml_page != ML_PAGE_HOME) return;   /* refresh is a HOME action */
    if (s_ml_task) { ml_status_set("busy - wait"); return; }
    ml_status_set("Refreshing...");
    ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
}

static void ml_opt_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_ml_page != ML_PAGE_SESSION || s_ml_task) return;
    const pp_ml_item_t *it = &s_sess->items[s_cur];
    if (it->kind != PP_ML_ITEM_RECOGNITION || it->submitted) return;
    s_pick = (int)(intptr_t)lv_event_get_user_data(e);
    ml_render_item();
}

static lv_obj_t *ml_label(lv_obj_t *parent)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, ML_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
    return l;
}

static void ml_set_text(lv_obj_t *lab, const char *txt)
{
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab,
        ml_has_cjk(txt) ? &Font_Hanzi_16 : ML_FONT, LV_PART_MAIN);
}

static bool ml_has_cjk(const char *txt)
{
    for (const unsigned char *p = (const unsigned char *)txt; p && *p; p++)
        if (*p & 0x80) return true;
    return false;
}

static void ml_status_set(const char *txt)
{
    snprintf(s_status, sizeof(s_status), "%s", txt);
    if (s_status_lab) lv_label_set_text(s_status_lab, s_status);
}

static void create_ml(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Mainline", ml_scr_pop_cb);

    lv_obj_t *rbtn = lv_btn_create(parent);
    lv_obj_set_size(rbtn, 72, 28);
    lv_obj_align(rbtn, LV_ALIGN_TOP_RIGHT, -6, 3);
    lv_obj_set_style_border_width(rbtn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(rbtn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rbtn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(rbtn, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(rbtn, ml_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(rbtn);
    lv_obj_set_style_text_font(rl, ML_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(rl, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(rl, "Refresh");
    lv_obj_center(rl);

    s_pg_home = lv_obj_create(parent);
    lv_obj_set_size(s_pg_home, lv_pct(100), lv_pct(86));
    lv_obj_align(s_pg_home, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(s_pg_home, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pg_home, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pg_home, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_pg_home, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pg_home, 6, LV_PART_MAIN);
    lv_obj_clear_flag(s_pg_home, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 6; i++) {
        s_h_line[i] = ml_label(s_pg_home);
        lv_label_set_text(s_h_line[i], "");
    }

    s_pg_sess = lv_obj_create(parent);
    lv_obj_set_size(s_pg_sess, lv_pct(100), lv_pct(86));
    lv_obj_align(s_pg_sess, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(s_pg_sess, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pg_sess, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pg_sess, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_pg_sess, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pg_sess, 2, LV_PART_MAIN);
    lv_obj_clear_flag(s_pg_sess, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pg_sess, LV_OBJ_FLAG_HIDDEN);
    s_q_kind_lab = ml_label(s_pg_sess);
    s_q_stem_lab = ml_label(s_pg_sess);
    for (int i = 0; i < PP_ML_OPT_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_pg_sess);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 24);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, ml_opt_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(i + 1));
        lv_obj_t *l = lv_label_create(row);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 2, 2);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(l, ML_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
        s_q_opts[i] = l;
    }
    s_q_clue_lab = ml_label(s_pg_sess);
    s_q_echo_lab = ml_label(s_pg_sess);

    s_pg_res = lv_obj_create(parent);
    lv_obj_set_size(s_pg_res, lv_pct(100), lv_pct(86));
    lv_obj_align(s_pg_res, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(s_pg_res, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_pg_res, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_pg_res, 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_pg_res, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pg_res, 6, LV_PART_MAIN);
    lv_obj_clear_flag(s_pg_res, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_pg_res, LV_OBJ_FLAG_HIDDEN);
    s_r_lab = ml_label(s_pg_res);
    s_r_lab2 = ml_label(s_pg_res);

    s_status_lab = lv_label_create(parent);
    lv_obj_set_width(s_status_lab, lv_pct(100));
    lv_label_set_long_mode(s_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status_lab, ML_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lab, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_status_lab, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_label_set_text(s_status_lab, s_status);
}

/* ---- rendering --------------------------------------------------------- */

static void ml_page_show(int page)
{
    s_ml_page = page;
    if (page == ML_PAGE_HOME) lv_obj_clear_flag(s_pg_home, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pg_home, LV_OBJ_FLAG_HIDDEN);
    if (page == ML_PAGE_SESSION) lv_obj_clear_flag(s_pg_sess, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pg_sess, LV_OBJ_FLAG_HIDDEN);
    if (page == ML_PAGE_RESULT) lv_obj_clear_flag(s_pg_res, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pg_res, LV_OBJ_FLAG_HIDDEN);
}

static void ml_render_home(void)
{
    int n = 0;
    char buf[128];

    if (!s_ov.has_plan || s_ov.recommend < 0) {
        snprintf(buf, sizeof(buf), "No study plan yet.");
        ml_set_text(s_h_line[n++], buf);
        ml_set_text(s_h_line[n++], "Create a plan on the web,");
        ml_set_text(s_h_line[n++], "then come back here.");
    } else {
        snprintf(buf, sizeof(buf), "Goal: %s  %d/%d lessons",
                 s_ov.goal_exam, s_ov.completed_lessons, s_ov.total_lessons);
        ml_set_text(s_h_line[n++], buf);
        if (s_ov.recommend == 0) {
            snprintf(buf, sizeof(buf), "Today L%d [%s] ~%dmin",
                     s_ov.lesson_index, s_ov.lesson_kind, s_ov.est_minutes);
            ml_set_text(s_h_line[n++], buf);
            ml_set_text(s_h_line[n++], s_ov.title);
            ml_set_text(s_h_line[n++], "Enter: start lesson");
        } else if (s_ov.recommend == 1) {
            snprintf(buf, sizeof(buf), "%d review items due.", s_ov.due_total);
            ml_set_text(s_h_line[n++], buf);
            ml_set_text(s_h_line[n++], "Use the Review app.");
        } else {
            snprintf(buf, sizeof(buf), "%s",
                     s_ov.course_completed ? "Course completed!"
                                           : "All caught up.");
            ml_set_text(s_h_line[n++], buf);
            if (s_ov.due_total > 0) {
                snprintf(buf, sizeof(buf), "%d due - Review app",
                         s_ov.due_total);
                ml_set_text(s_h_line[n++], buf);
            }
        }
    }
    while (n < 6) ml_set_text(s_h_line[n++], "");
}

static void ml_render_item(void)
{
    if (!s_sess) return;
    const pp_ml_item_t *it = &s_sess->items[s_cur];
    char buf[192];

    snprintf(buf, sizeof(buf), "item %d/%d  %s", s_cur + 1,
             s_sess->item_count, it->submitted ? "(done)" : "");
    ml_set_text(s_q_kind_lab, buf);
    ml_set_text(s_q_echo_lab, "");           /* stale echo must not leak
                                              * into the next item */

    s_pick = (it->kind == PP_ML_ITEM_RECOGNITION && !it->submitted) ? s_pick
                                                                    : 0;
    switch (it->kind) {
    case PP_ML_ITEM_RECOGNITION: {
        snprintf(buf, sizeof(buf), "%s  %s %s", it->u.rec.word,
                 it->u.rec.phonetic, it->u.rec.pos);
        ml_set_text(s_q_stem_lab, buf);
        for (int i = 0; i < PP_ML_OPT_MAX; i++) {
            if (i < it->u.rec.option_count) {
                snprintf(buf, sizeof(buf), "%d %s %s", i + 1,
                         (i + 1 == s_pick) ? ">" : " ",
                         it->u.rec.options[i]);
                ml_set_text(s_q_opts[i], buf);
                lv_obj_clear_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(s_q_opts[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            } else {
                lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
            }
        }
        ml_set_text(s_q_clue_lab, "");
        ml_set_text(s_q_echo_lab, "");
        break;
    }
    case PP_ML_ITEM_RECALL:
        /* v1.30.7: zh + en in ONE two-line label - inter-label flex gaps
         * were read as blank lines; inside one label only font line
         * spacing remains */
        if (it->u.rcl.meaning_en[0])
            snprintf(buf, sizeof(buf), "Spell it: %s\n%s. %s",
                     it->u.rcl.meaning_zh, it->u.rcl.pos,
                     it->u.rcl.meaning_en);
        else
            snprintf(buf, sizeof(buf), "Spell it: %s\n%s.",
                     it->u.rcl.meaning_zh, it->u.rcl.pos);
        ml_set_text(s_q_stem_lab, buf);
        ml_set_text(s_q_clue_lab, "");
        goto text_opts_hidden;
    case PP_ML_ITEM_CLOZE:
        if (it->u.clz.sentence_zh[0])
            snprintf(buf, sizeof(buf), "%s\n%s", it->u.clz.sentence_en,
                     it->u.clz.sentence_zh);
        else
            snprintf(buf, sizeof(buf), "%s", it->u.clz.sentence_en);
        ml_set_text(s_q_stem_lab, buf);
        ml_set_text(s_q_clue_lab, "");
        goto text_opts_hidden;
    case PP_ML_ITEM_OUTPUT: {
        snprintf(buf, sizeof(buf), "%s",
                 it->u.out.topic_title[0] ? it->u.out.topic_title
                                          : "Write with scene words");
        ml_set_text(s_q_stem_lab, buf);
        int off = 0;
        for (int i = 0;
             i < it->u.out.scene_word_count && off < (int)sizeof(buf) - 20;
             i++)
            off += snprintf(buf + off, sizeof(buf) - off, "%s ",
                            it->u.out.scene_words[i]);
        ml_set_text(s_q_clue_lab, buf);
        goto text_opts_hidden;
    }
    default:
        ml_set_text(s_q_stem_lab, "Unsupported item type");
        ml_set_text(s_q_clue_lab, "");
        ml_set_text(s_q_echo_lab, "Enter: submit as skipped");
        goto text_opts_hidden;
    }
text_opts_hidden:
    for (int i = 0; i < PP_ML_OPT_MAX; i++) {
        lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
    }
    ml_page_show(ML_PAGE_SESSION);
}

/* typed-echo line for text answers: shown while typing */
static void ml_render_input_echo(void)
{
    if (!s_sess) return;
    const pp_ml_item_t *it = &s_sess->items[s_cur];
    if (it->kind == PP_ML_ITEM_RECOGNITION || it->kind == PP_ML_ITEM_OUTPUT)
        return;                               /* recognition shows options;
                                                 * output keeps scene words */
    char buf[160];
    snprintf(buf, sizeof(buf), "> %s", s_input);
    ml_set_text(s_q_echo_lab, buf);
}

static void ml_render_result(void)
{
    char buf[192];
    int off = snprintf(buf, sizeof(buf), "Score %d", s_fin.score);
    if (s_fin.xp_awarded >= 0)
        off += snprintf(buf + off, sizeof(buf) - off, "  XP +%d",
                        s_fin.xp_awarded);
    ml_set_text(s_r_lab, buf);
    snprintf(buf, sizeof(buf), "%d/%d correct, %d skipped",
             s_fin.correct_items >= 0 ? s_fin.correct_items : 0,
             s_fin.total_items >= 0 ? s_fin.total_items : 0,
             s_fin.skipped_items >= 0 ? s_fin.skipped_items : 0);
    ml_set_text(s_r_lab2, buf);
    ml_page_show(ML_PAGE_RESULT);
}

/* ---- session flow -------------------------------------------------------- */

static bool ml_all_submitted(void)
{
    if (!s_sess) return false;
    for (int i = 0; i < s_sess->item_count; i++)
        if (!s_sess->items[i].submitted) return false;
    return true;
}

static void ml_goto_item(int idx, const char *feedback = NULL)
{
    if (!s_sess || idx < 0 || idx >= s_sess->item_count) return;
    s_cur = idx;
    s_input_len = 0;
    s_input[0] = '\0';
    s_pick = 0;
    ml_render_item();
    char st[96];
    if (feedback && feedback[0])
        snprintf(st, sizeof(st), "%s | item %d/%d", feedback,
                 s_cur + 1, s_sess->item_count);
    else
        snprintf(st, sizeof(st), "item %d/%d - Enter: submit",
                 s_cur + 1, s_sess->item_count);
    ml_status_set(st);
}

static void ml_try_finish(void)
{
    if (!ml_all_submitted()) {
        /* skip already-done prefix forward */
        for (int i = 0; i < s_sess->item_count; i++) {
            if (!s_sess->items[i].submitted) {
                ml_goto_item(i);
                ml_status_set("finish blocked: item left");
                return;
            }
        }
    }
    s_finishing = true;
    s_finish_sent = true;
    ml_status_set("Finishing...");
    ml_start(ML_REQ_FINISH, 0, 0, NULL, false);
}

static void ml_submit_current(void)
{
    pp_ml_item_t *it = &s_sess->items[s_cur];
    bool skip = (it->kind == PP_ML_ITEM_UNSUPPORTED);
    /* output items NEVER skip (IC-2); recognition needs a pick */
    if (it->kind == PP_ML_ITEM_RECOGNITION && s_pick == 0) {
        ml_status_set("pick 1-4 first");
        return;
    }
    if (it->kind == PP_ML_ITEM_RECALL || it->kind == PP_ML_ITEM_CLOZE ||
        it->kind == PP_ML_ITEM_OUTPUT) {
        if (s_input_len == 0) {
            ml_status_set("type an answer first");
            return;
        }
    }
    ml_status_set("Submitting...");
    ml_start(ML_REQ_SUBMIT, 0, it->id, s_input, skip, s_cur);
}

static void ml_consume(void)
{
    if (!s_ml_q) return;
    void *p = NULL;
    ml_msg_t *m = NULL;
    while (xQueueReceive(s_ml_q, &p, 0) == pdTRUE) {
        m = (ml_msg_t *)p;
        if (m->gen == s_ml_gen) break;
        if (m->sess) { delete m->sess; m->sess = NULL; }
        delete m;
        m = NULL;
    }
    if (!m) return;

    if (m->kind == ML_REQ_OVERVIEW) {
        if (m->ok) {
            s_ov = m->ov;
            s_ov_cached = false;
            penpal_ml_cache_save(&s_ov);
            ml_render_home();
            ml_page_show(ML_PAGE_HOME);
            ml_status_set(s_ov.has_plan ? "" :
                          "no plan - create on web");
        } else {
            ml_status_set(m->err);
        }
    } else if (m->kind == ML_REQ_START) {
        if (m->ok) {
            delete s_sess;                    /* previous session, if any */
            s_sess = m->sess;                 /* ownership moved */
            m->sess = NULL;
            s_finishing = false;
            s_finish_sent = false;
            int first = 0;
            for (int i = 0; i < s_sess->item_count; i++)
                if (!s_sess->items[i].submitted) {
                    first = i;
                    break;
                }
            ml_goto_item(first);
        } else {
            ml_status_set(m->err);
            ml_page_show(ML_PAGE_HOME);
        }
    } else if (m->kind == ML_REQ_SUBMIT) {
        if (m->ok) {
            if (m->item_idx >= 0 && m->item_idx < s_sess->item_count)
                s_sess->items[m->item_idx].submitted = true;
            char st[64];
            if (m->sub.passed_present)
                snprintf(st, sizeof(st), "%s q=%d%s",
                         m->sub.passed ? "PASS" : "retry",
                         m->sub.quality,
                         m->sub.in_errorbook ? " +book" : "");
            else
                snprintf(st, sizeof(st), "score %d",
                         m->sub.score_write >= 0 ? m->sub.score_write
                                                 : m->sub.combined);
            /* result popup (user request): show 1.8s, the poll loop
             * closes it and advances */
            int nxt = -1;
            for (int i = 0; i < s_sess->item_count; i++)
                if (!s_sess->items[i].submitted) {
                    nxt = i;
                    break;
                }
            snprintf(s_popup_feedback, sizeof(s_popup_feedback), "%s", st);
            s_popup_next = nxt;
            s_popup_ms = millis();
            char body[160];
            const char *ans = penpal_ml_reveal_answer(
                s_sess->items[m->item_idx].id);
            if (ans && ans[0])
                snprintf(body, sizeof(body), "%s\n%s\nAnswer: %s",
                         m->sub.passed_present
                             ? (m->sub.passed ? "Correct!" : "Not quite")
                             : "Recorded.",
                         st, ans);
            else
                snprintf(body, sizeof(body), "%s\n%s",
                         m->sub.passed_present
                             ? (m->sub.passed ? "Correct!" : "Not quite")
                             : "Recorded.",
                         st);
            pp_msgbox_show("Result", body);
        } else {
            if (strstr(m->err, "plan_superseded") ||
                strstr(m->err, "superseded")) {
                ml_status_set("plan switched on web - session void");
                ml_page_show(ML_PAGE_HOME);
                ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
            } else {
                ml_status_set(m->err);
            }
        }
    } else {                                  /* FINISH */
        if (m->ok) {
            s_fin = m->fin;
            ml_render_result();
            ml_status_set("Enter: back to home");
        } else {
            if (m->miss.superseded) {
                ml_status_set("plan switched on web - session void");
                ml_page_show(ML_PAGE_HOME);
                ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
            } else if (m->miss.output_rule) {
                /* capstone rule: jump to the LAST undone output item
                 * (design §3.3 / IC-2); none -> stay with the message */
                int last_out = -1;
                for (int i = 0; i < s_sess->item_count; i++)
                    if (s_sess->items[i].is_output &&
                        !s_sess->items[i].submitted)
                        last_out = i;
                if (last_out >= 0) {
                    ml_goto_item(last_out);
                    char st[160];
                    snprintf(st, sizeof(st), "%.150s", m->miss.message);
                    ml_status_set(st);
                } else {
                    ml_status_set(m->miss.message);
                }
            } else if (m->miss.missing_first > 0) {
                for (int i = 0; i < s_sess->item_count; i++)
                    if (s_sess->items[i].id == m->miss.missing_first) {
                        ml_goto_item(i);
                        break;
                    }
                ml_status_set("finish blocked: item left");
            } else {
                ml_status_set(m->err);
            }
        }
    }
    delete m;
}

/* ---- keyboard (factory loop) --------------------------------------------- */

void mainline_keyboard_poll(void)
{
    if (!s_status_lab) return;

    /* submit-result popup: auto-close after 1.8s, then advance */
    if (s_popup_ms) {
        bool timeout = millis() - s_popup_ms > 1800;
        bool key = false;
        char c;
        if (keypad_get_val(&c)) {
            keypad_set_flag();
            key = true;
        }
        if (timeout || key) {
            pp_msgbox_close();
            s_popup_ms = 0;
            if (s_popup_next >= 0)
                ml_goto_item(s_popup_next, s_popup_feedback);
            else {
                ml_status_set(s_popup_feedback);
                ml_try_finish();
            }
            return;                          /* one action per frame */
        }
        return;                              /* popup absorbs input */
    }

    ml_consume();

    if (s_enter_pending && !s_ml_task) {
        s_enter_pending = false;
        /* replay the buffered Enter by page context */
        if (s_ml_page == ML_PAGE_HOME && s_ov.recommend == 0)
            ml_start(ML_REQ_START, s_ov.lesson_id, 0, NULL, false);
        /* SESSION/RESULT Enter is handled inline below */
    }

    char c;
    int guard = 8;
    while (guard-- > 0 && keypad_get_val(&c)) {
        keypad_set_flag();

        if (s_ml_page == ML_PAGE_HOME) {
            if (c == '\n') {
                if (s_ml_task) {
                    s_enter_pending = true;
                    continue;
                }
                if (s_ov.recommend == 0) {
                    ml_status_set("Starting lesson...");
                    ml_start(ML_REQ_START, s_ov.lesson_id, 0, NULL, false);
                } else {
                    ml_status_set(s_ov.has_plan ?
                        "nothing to start today" :
                        "no plan - create on web");
                }
            } else if (c == '+') {
                if (s_ml_task) { ml_status_set("busy - wait"); continue; }
                ml_status_set("Refreshing...");
                ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
            } else if (c == '\b') {
                scr_mgr_pop(false);
                keypad_clear_chars();
                return;
            }
            continue;
        }

        if (s_ml_page == ML_PAGE_RESULT) {
            if (c == '\n' || c == '\b') {
                ml_page_show(ML_PAGE_HOME);
                ml_status_set("Refreshing...");
                ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
            }
            continue;
        }

        /* SESSION */
        if (!s_sess) { ml_status_set("no session"); continue; }
        const pp_ml_item_t *it = &s_sess->items[s_cur];
        if (c == '\n') {
            if (s_ml_task) {
                s_enter_pending = true;
                continue;
            }
            if (it->submitted) {
                /* re-submit allowed (server is_retry); re-send last answer */
                ml_submit_current();
            } else {
                ml_submit_current();
            }
            continue;
        }
        if (c == '\b') {
            if (!it->submitted &&
                (it->kind == PP_ML_ITEM_RECALL ||
                 it->kind == PP_ML_ITEM_CLOZE ||
                 it->kind == PP_ML_ITEM_OUTPUT) &&
                s_input_len > 0) {
                size_t len = strlen(s_input);
                do { len--; } while (len > 0 && (s_input[len] & 0xC0) == 0x80);
                s_input[len] = '\0';
                s_input_len = len;
                ml_render_input_echo();
            } else {
                scr_mgr_pop(false);
                keypad_clear_chars();
                return;
            }
            continue;
        }
        if (s_ml_task) {
            ml_status_set("busy - wait");
            continue;
        }
        if (it->kind == PP_ML_ITEM_RECOGNITION && !it->submitted) {
            if (c >= '1' && c <= '4' && (c - '0') <= it->u.rec.option_count) {
                s_pick = c - '0';
                ml_render_item();
            }
            continue;
        }
        if (!it->submitted &&
            ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == ' ' || c == '\'' ||
             c == '-' || c == '.' || c == ',' || c == '!')) {
            if (s_input_len + 2 < sizeof(s_input)) {
                s_input[s_input_len++] =
                    (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
                s_input[s_input_len] = '\0';
                ml_render_input_echo();
            }
            continue;
        }
    }
}

/* ---- lifecycle ------------------------------------------------------------ */

static void entry_ml(void)
{
    ui_disp_full_refr();
    s_ml_gen++;
    s_enter_pending = false;
    s_popup_ms = 0;
    s_popup_next = -1;
    if (pp_msgbox_open()) pp_msgbox_close();
    delete s_sess;                            /* previous visit's session */
    s_sess = NULL;
    s_ml_page = ML_PAGE_HOME;
    memset(&s_ov, 0, sizeof(s_ov));
    s_ov.recommend = -1;
    s_ov_cached = penpal_ml_cache_load(&s_ov);   /* instant render (v1.30.5) */
    ml_render_home();
    ml_page_show(ML_PAGE_HOME);
    if (s_ov_cached)
        ml_status_set("cached - Refresh for latest");
    else {
        ml_status_set("Loading...");
        ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);  /* first ever visit */
    }
}

static void exit_ml(void)
{
    ui_disp_full_refr();
    s_ml_gen++;
}

static void destroy_ml(void)
{
    s_pg_home = s_pg_sess = s_pg_res = NULL;
    s_q_kind_lab = s_q_stem_lab = NULL;
    s_q_clue_lab = NULL;
    s_q_echo_lab = NULL;
    s_r_lab = s_r_lab2 = NULL;
    s_status_lab = NULL;
    for (int i = 0; i < PP_ML_OPT_MAX; i++) s_q_opts[i] = NULL;
}

scr_lifecycle_t screen_mainline = {
    .create = create_ml,
    .entry = entry_ml,
    .exit = exit_ml,
    .destroy = destroy_ml,
};
