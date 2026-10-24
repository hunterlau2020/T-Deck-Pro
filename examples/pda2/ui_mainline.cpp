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
static pp_ml_session_t s_sess;                /* SESSION items (UI owned) */
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

/* ---- async glue (wa_* pattern) -------------------------------------------- */

enum { ML_REQ_OVERVIEW = 0, ML_REQ_START, ML_REQ_SUBMIT, ML_REQ_FINISH };

struct ml_msg_t {
    uint32_t gen;
    int kind;
    bool ok;
    int  item_idx;                            /* SUBMIT: which item this was */
    char err[160];
    pp_ml_overview_t ov;                      /* OVERVIEW */
    pp_ml_session_t sess;                     /* START */
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
    ml_msg_t *m = new ml_msg_t;
    m->gen = rq->gen;
    m->kind = rq->kind;
    m->item_idx = rq->item_idx;
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    if (rq->kind == ML_REQ_OVERVIEW) {
        m->ok = penpal_ml_overview(base, key, &m->ov, &err);
    } else if (rq->kind == ML_REQ_START) {
        m->ok = penpal_ml_lesson_start(base, key, rq->lesson_id,
                                       &m->sess, &err);
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
                     const char *user_input, bool skipped)
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

    ml_req_t *rq = new ml_req_t;
    rq->gen = s_ml_gen;
    rq->kind = kind;
    rq->base = base;
    rq->key = key;
    rq->lesson_id = lesson_id;
    rq->item_id = item_id;
    rq->skipped = skipped;
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
static lv_obj_t *s_q_input_lab;               /* typed answer echo */
static lv_obj_t *s_r_lab, *s_r_lab2;
static lv_obj_t *s_status_lab;

static void ml_scr_pop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) scr_mgr_pop(false);
}

static void ml_opt_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_ml_page != ML_PAGE_SESSION || s_ml_task) return;
    const pp_ml_item_t *it = &s_sess.items[s_cur];
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
    lv_obj_set_style_pad_row(s_pg_sess, 4, LV_PART_MAIN);
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
    s_q_input_lab = ml_label(s_pg_sess);

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
    const pp_ml_item_t *it = &s_sess.items[s_cur];
    char buf[192];

    snprintf(buf, sizeof(buf), "item %d/%d  %s", s_cur + 1,
             s_sess.item_count, it->submitted ? "(done)" : "");
    ml_set_text(s_q_kind_lab, buf);

    s_pick = (it->kind == PP_ML_ITEM_RECOGNITION && !it->submitted) ? s_pick
                                                                    : 0;
    switch (it->kind) {
    case PP_ML_ITEM_RECOGNITION: {
        snprintf(buf, sizeof(buf), "%s  %s %s", it->word, it->phonetic,
                 it->pos);
        ml_set_text(s_q_stem_lab, buf);
        for (int i = 0; i < PP_ML_OPT_MAX; i++) {
            if (i < it->option_count) {
                snprintf(buf, sizeof(buf), "%d %s %s", i + 1,
                         (i + 1 == s_pick) ? ">" : " ", it->options[i]);
                ml_set_text(s_q_opts[i], buf);
                lv_obj_clear_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        ml_set_text(s_q_input_lab, "");
        break;
    }
    case PP_ML_ITEM_RECALL:
        snprintf(buf, sizeof(buf), "Spell it: %s  %s", it->meaning_zh,
                 it->pos);
        ml_set_text(s_q_stem_lab, buf);
        if (it->meaning_en[0]) {
            ml_set_text(s_q_input_lab, it->meaning_en);
        }
        goto text_opts_hidden;
    case PP_ML_ITEM_CLOZE:
        ml_set_text(s_q_stem_lab, it->sentence_en);
        if (it->sentence_zh[0]) {
            snprintf(buf, sizeof(buf), "%s", it->sentence_zh);
            ml_set_text(s_q_input_lab, buf);
        }
        goto text_opts_hidden;
    case PP_ML_ITEM_OUTPUT: {
        int off = snprintf(buf, sizeof(buf), "%s",
                           it->topic_title[0] ? it->topic_title
                                              : "Write with scene words");
        ml_set_text(s_q_stem_lab, buf);
        off = 0;
        for (int i = 0; i < it->scene_word_count && off < (int)sizeof(buf) - 34;
             i++)
            off += snprintf(buf + off, sizeof(buf) - off, "%s ",
                            it->scene_words[i]);
        ml_set_text(s_q_input_lab, buf);
        goto text_opts_hidden;
    }
    default:
        ml_set_text(s_q_stem_lab, "Unsupported item type");
        ml_set_text(s_q_input_lab, "Enter: submit as skipped");
        goto text_opts_hidden;
    }
text_opts_hidden:
    for (int i = 0; i < PP_ML_OPT_MAX; i++)
        lv_obj_add_flag(s_q_opts[i], LV_OBJ_FLAG_HIDDEN);
    ml_page_show(ML_PAGE_SESSION);
}

/* typed-echo line for text answers: shown while typing */
static void ml_render_input_echo(void)
{
    const pp_ml_item_t *it = &s_sess.items[s_cur];
    if (it->kind == PP_ML_ITEM_RECOGNITION || it->kind == PP_ML_ITEM_OUTPUT)
        return;                               /* recognition shows options;
                                                 * output keeps scene words */
    char buf[160];
    snprintf(buf, sizeof(buf), "> %s", s_input);
    ml_set_text(s_q_input_lab, buf);
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
    for (int i = 0; i < s_sess.item_count; i++)
        if (!s_sess.items[i].submitted) return false;
    return true;
}

static void ml_goto_item(int idx)
{
    if (idx < 0 || idx >= s_sess.item_count) return;
    s_cur = idx;
    s_input_len = 0;
    s_input[0] = '\0';
    s_pick = 0;
    ml_render_item();
    char st[48];
    snprintf(st, sizeof(st), "item %d/%d - Enter: submit",
             s_cur + 1, s_sess.item_count);
    ml_status_set(st);
}

static void ml_try_finish(void)
{
    if (!ml_all_submitted()) {
        /* skip already-done prefix forward */
        for (int i = 0; i < s_sess.item_count; i++) {
            if (!s_sess.items[i].submitted) {
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
    pp_ml_item_t *it = &s_sess.items[s_cur];
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
    ml_start(ML_REQ_SUBMIT, 0, it->id, s_input, skip);
}

static void ml_consume(void)
{
    if (!s_ml_q) return;
    void *p = NULL;
    ml_msg_t *m = NULL;
    while (xQueueReceive(s_ml_q, &p, 0) == pdTRUE) {
        m = (ml_msg_t *)p;
        if (m->gen == s_ml_gen) break;
        delete m;
        m = NULL;
    }
    if (!m) return;

    if (m->kind == ML_REQ_OVERVIEW) {
        if (m->ok) {
            s_ov = m->ov;
            ml_render_home();
            ml_page_show(ML_PAGE_HOME);
            ml_status_set(s_ov.has_plan ? "" :
                          "no plan - create on web");
        } else {
            ml_status_set(m->err);
        }
    } else if (m->kind == ML_REQ_START) {
        if (m->ok) {
            s_sess = m->sess;
            s_finishing = false;
            s_finish_sent = false;
            int first = 0;
            for (int i = 0; i < s_sess.item_count; i++)
                if (!s_sess.items[i].submitted) {
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
            if (m->item_idx >= 0 && m->item_idx < s_sess.item_count)
                s_sess.items[m->item_idx].submitted = true;
            char st[64];
            if (m->sub.passed_present)
                snprintf(st, sizeof(st), "%s  q=%d%s",
                         m->sub.passed ? "PASS" : "retry",
                         m->sub.quality,
                         m->sub.in_errorbook ? " +book" : "");
            else
                snprintf(st, sizeof(st), "score %d",
                         m->sub.score_write >= 0 ? m->sub.score_write
                                                 : m->sub.combined);
            ml_status_set(st);
            /* advance to the next undone item, or finish */
            int nxt = -1;
            for (int i = 0; i < s_sess.item_count; i++)
                if (!s_sess.items[i].submitted) {
                    nxt = i;
                    break;
                }
            if (nxt >= 0) ml_goto_item(nxt);
            else ml_try_finish();
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
                for (int i = 0; i < s_sess.item_count; i++)
                    if (s_sess.items[i].is_output &&
                        !s_sess.items[i].submitted)
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
                for (int i = 0; i < s_sess.item_count; i++)
                    if (s_sess.items[i].id == m->miss.missing_first) {
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
        const pp_ml_item_t *it = &s_sess.items[s_cur];
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
            if (c >= '1' && c <= '4' && (c - '0') <= it->option_count) {
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
    s_ml_page = ML_PAGE_HOME;
    memset(&s_ov, 0, sizeof(s_ov));
    s_ov.recommend = -1;
    ml_render_home();
    ml_page_show(ML_PAGE_HOME);
    ml_status_set("Loading...");
    ml_start(ML_REQ_OVERVIEW, 0, 0, NULL, false);
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
    s_q_input_lab = NULL;
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
