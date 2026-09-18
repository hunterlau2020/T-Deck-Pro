/**
 * @file      ui_leveltest.cpp
 * @brief     Level Test app - "⓯ 定级测 (整卷 staircase)" on the device
 *            (user request 2026-09-18).
 *
 * Endpoints (penpal_api.h, contract from /openapi.json 2026-09-18):
 *   GET  /api/v1/users/me/level-test/questions  - whole paper (~10 q,
 *        Pre-A1..B2+, grammar|vocab), answer_index SHIPS with each question:
 *        grading is client-side and self-reported (server does not re-check).
 *   POST /api/v1/users/me/level-test            - {answers:[{level,correct}],
 *        mode:"staircase"} -> {score, resulting_level, level_detail[]}.
 *   GET  /api/v1/users/me/level-test/history    - previous results (HOME).
 *
 * Async model (whoami wa_* pattern, async_ipc_contract §1): one worker task
 * at a time, result struct's first field is the screen generation; results
 * from a stale generation are dropped and freed by the consume path.
 *
 * UI states (one screen, LV_OBJ_FLAG_HIDDEN page switch - penpal pattern):
 *   HOME   last result + "Enter: start"          -> fetch questions
 *   QUIZ   one question per render: progress row, stem, up to 4 option rows;
 *          keys 1-4 or a tap answers -> next question; the last answer
 *          launches the submit task (one EPD render per question)
 *   RESULT score + resulting level + per-level detail + "Enter: retest"
 *
 * Keyboard: Enter start/retest, 1-4 answer, Backspace exit.
 */
#include "Arduino.h"
#include <WiFi.h>
#include "ui_leveltest.h"
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
#define LT_FONT &Font_Mono_Bold_15

/* Vocab options are the word's Chinese meaning (server DB), and grammar
 * stems/options stay English: switch the label font per content - the mono
 * asset font has no CJK glyphs (penpal msgbox pattern, user report
 * 2026-09-18). Font_Hanzi_16 = ASCII + the full vocab hanzi set. */
static bool lt_has_cjk(const char *txt)
{
    for (const unsigned char *p = (const unsigned char *)txt; p && *p; p++)
        if (*p & 0x80) return true;
    return false;
}

static void lt_set_text(lv_obj_t *lab, const char *txt)
{
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_font(lab,
        lt_has_cjk(txt) ? &Font_Hanzi_16 : LT_FONT, LV_PART_MAIN);
}

/* ---- screen state (UI thread owned) ---------------------------------------- */

enum { LT_PAGE_HOME = 0, LT_PAGE_QUIZ, LT_PAGE_RESULT };

static int s_lt_page = LT_PAGE_HOME;
static int s_quiz_count = 0;
static pp_lt_question_t s_quiz[PP_LT_Q_MAX];
static bool s_correct[PP_LT_Q_MAX];
static int s_quiz_pos = 0;
static pp_lt_result_t s_result_cache;
static pp_lt_hist_t s_last_hist[PP_LT_HIST_MAX];
static int s_hist_count = 0;
static char s_status[96] = "Enter: start the test";

/* ---- LVGL objects (created once per create; nulled on destroy) ------------- */

static lv_obj_t *s_pg_home, *s_pg_quiz, *s_pg_result;
static lv_obj_t *s_home_hist_lab;
static lv_obj_t *s_progress_lab, *s_stem_lab, *s_opt[PP_LT_OPT_MAX];
static lv_obj_t *s_score_lab, *s_detail_lab;
static lv_obj_t *s_status_lab;

static void lt_answer(int opt_index);    /* below */

static void lt_status(const char *txt)
{
    snprintf(s_status, sizeof(s_status), "%s", txt);
    if (s_status_lab) lt_set_text(s_status_lab, s_status);
}

/* ---- async glue (wa_* pattern) ---------------------------------------------- */

enum { LT_REQ_QUESTIONS = 0, LT_REQ_SUBMIT, LT_REQ_HISTORY };

struct lt_msg_t {
    uint32_t gen;                        /* screen generation (contract rule 2) */
    int kind;
    bool ok;
    char err[96];
    int q_count;                         /* QUESTIONS */
    pp_lt_question_t qs[PP_LT_Q_MAX];
    pp_lt_result_t result;               /* SUBMIT */
    int hist_count;                      /* HISTORY */
    pp_lt_hist_t hist[PP_LT_HIST_MAX];
};

struct lt_req_t {
    uint32_t gen;
    int kind;
    string base;
    string key;
    int q_count;
    pp_lt_question_t qs[PP_LT_Q_MAX];
    bool correct[PP_LT_Q_MAX];
};

static QueueHandle_t s_lt_q = NULL;
static volatile TaskHandle_t s_lt_task = NULL;
static uint32_t s_lt_gen = 0;            /* bumped on entry/exit: stale drops */

static void lt_task_func(void *param)
{
    lt_req_t *rq = (lt_req_t *)param;    /* task-owned snapshot */
    lt_msg_t *m = new lt_msg_t;
    m->gen = rq->gen;
    m->kind = rq->kind;
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    if (rq->kind == LT_REQ_QUESTIONS) {
        m->q_count = 0;
        m->ok = penpal_lt_get_questions(base, key, m->qs, PP_LT_Q_MAX,
                                        &m->q_count, &err);
    } else if (rq->kind == LT_REQ_SUBMIT) {
        m->ok = penpal_lt_submit(base, key, rq->qs, rq->correct, rq->q_count,
                                 &m->result, &err);
    } else {
        m->hist_count = 0;
        m->ok = penpal_lt_get_history(base, key, m->hist, PP_LT_HIST_MAX,
                                      &m->hist_count, &err);
    }
    snprintf(m->err, sizeof(m->err), "%s", err.c_str());

    if (s_lt_q) {
        xQueueSend(s_lt_q, &m, pdMS_TO_TICKS(2000));
    } else {
        delete m;
    }
    delete rq;
    s_lt_task = NULL;
    vTaskDelete(NULL);
}

/* Single flight + preflight (whoami wa_start pattern); the status line says
 * why a refusal happened. The queue is created once and never deleted. */
static bool lt_start(int kind)
{
    if (s_lt_task) {
        lt_status("busy - wait for current request");
        return false;
    }
    char base[PP_BASE_MAX], key[PP_KEY_MAX];
    penpal_load_config(base, sizeof(base), key, sizeof(key));
    if (base[0] == '\0' || key[0] == '\0') {
        lt_status("server/key not set (Whoami Cfg)");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        lt_status("WiFi not connected");
        return false;
    }
    if (!s_lt_q) s_lt_q = xQueueCreate(4, sizeof(void *));
    if (!s_lt_q) {
        lt_status("queue create failed");
        return false;
    }

    lt_req_t *rq = new lt_req_t;
    rq->gen = s_lt_gen;
    rq->kind = kind;
    rq->base = base;
    rq->key = key;
    if (kind == LT_REQ_SUBMIT) {
        rq->q_count = s_quiz_count;
        memcpy(rq->qs, s_quiz, sizeof(s_quiz));
        memcpy(rq->correct, s_correct, sizeof(s_correct));
    }

    if (xTaskCreate(lt_task_func, "lvltest", 1024 * 8, rq, 1,
                    (TaskHandle_t *)&s_lt_task) != pdPASS) {
        s_lt_task = NULL;
        delete rq;
        lt_status("task create failed");
        return false;
    }
    return true;
}

/* ---- UI builders -------------------------------------------------------------- */

static void lt_scr_pop_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        scr_mgr_pop(false);
    }
}

/* Option row tap = the matching number key (4_2 link-row pattern). */
static void lt_opt_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lt_answer((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *lt_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(86));
    lv_obj_align(page, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 6, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

static lv_obj_t *lt_label(lv_obj_t *parent, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, LT_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
    lv_label_set_text(l, txt);
    return l;
}

static void create_lt(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Level Test", lt_scr_pop_cb);

    /* HOME */
    s_pg_home = lt_page_create(parent);
    lt_label(s_pg_home, "CEFR placement test\n10 questions, ~2 min");
    s_home_hist_lab = lt_label(s_pg_home, "(no previous result)");
    lt_label(s_pg_home, "Enter: start");

    /* QUIZ */
    s_pg_quiz = lt_page_create(parent);
    s_progress_lab = lt_label(s_pg_quiz, "");
    s_stem_lab = lt_label(s_pg_quiz, "");
    for (int i = 0; i < PP_LT_OPT_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_pg_quiz);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 30);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, lt_opt_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(row);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 4, 6);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(l, LT_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
        s_opt[i] = l;
    }

    /* RESULT */
    s_pg_result = lt_page_create(parent);
    s_score_lab = lt_label(s_pg_result, "");
    s_detail_lab = lt_label(s_pg_result, "");
    lt_label(s_pg_result, "Enter: retest  Back: exit");

    /* shared status line (always black - EPD lesson, issue_list §23) */
    s_status_lab = lv_label_create(parent);
    lv_obj_set_width(s_status_lab, lv_pct(100));
    lv_label_set_long_mode(s_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status_lab, LT_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lab, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_status_lab, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_label_set_text(s_status_lab, s_status);
}

/* ---- render (one EPD flush per state change) ---------------------------------- */

static void lt_page_show(int page)
{
    s_lt_page = page;
    lv_obj_add_flag(s_pg_home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pg_quiz, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pg_result, LV_OBJ_FLAG_HIDDEN);
    if (page == LT_PAGE_HOME) lv_obj_clear_flag(s_pg_home, LV_OBJ_FLAG_HIDDEN);
    if (page == LT_PAGE_QUIZ) lv_obj_clear_flag(s_pg_quiz, LV_OBJ_FLAG_HIDDEN);
    if (page == LT_PAGE_RESULT)
        lv_obj_clear_flag(s_pg_result, LV_OBJ_FLAG_HIDDEN);
}

static void lt_render_home(void)
{
    if (s_hist_count > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Last: %s  score %d\n%.10s",
                 s_last_hist[0].resulting_level, s_last_hist[0].score,
                 s_last_hist[0].created_at);
        lt_set_text(s_home_hist_lab, buf);
    } else {
        lv_label_set_text(s_home_hist_lab, "(no previous result)");
    }
    lt_page_show(LT_PAGE_HOME);
}

static void lt_render_question(void)
{
    const pp_lt_question_t *q = &s_quiz[s_quiz_pos];
    char prog[48];
    snprintf(prog, sizeof(prog), "Q%d/%d  %s  %s", s_quiz_pos + 1,
             s_quiz_count, q->level, q->type);
    lv_label_set_text(s_progress_lab, prog);
    lt_set_text(s_stem_lab, q->stem);
    for (int i = 0; i < PP_LT_OPT_MAX; i++) {
        if (i < q->opt_count) {
            char buf[56];
            snprintf(buf, sizeof(buf), "%d. %s", i + 1, q->options[i]);
            lv_obj_clear_flag(s_opt[i], LV_OBJ_FLAG_HIDDEN);
            lt_set_text(s_opt[i], buf);
        } else {
            lv_obj_add_flag(s_opt[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lt_page_show(LT_PAGE_QUIZ);
}

static void lt_render_result(void)
{
    char head[64];
    snprintf(head, sizeof(head), "Score %d\nLevel: %s", s_result_cache.score,
             s_result_cache.resulting_level);
    lv_label_set_text(s_score_lab, head);

    char det[PP_LT_DET_MAX * 48 + 1];
    int off = 0;
    for (int i = 0; i < s_result_cache.det_count && off < (int)sizeof(det) - 48;
         i++) {
        const pp_lt_level_detail_t *d = &s_result_cache.detail[i];
        off += snprintf(det + off, sizeof(det) - off, "%s: %d/%d %s\n",
                        d->level, d->correct, d->answered,
                        d->passed ? "pass" : "stop");
    }
    if (off == 0) snprintf(det, sizeof(det), "(no detail)");
    lv_label_set_text(s_detail_lab, det);
    lt_page_show(LT_PAGE_RESULT);
}

/* ---- result consumption (UI thread, factory loop) ------------------------------ */

static void lt_consume(void)
{
    if (!s_lt_q) return;
    void *p = NULL;
    lt_msg_t *m = NULL;
    while (xQueueReceive(s_lt_q, &p, 0) == pdTRUE) {
        m = (lt_msg_t *)p;
        if (m->gen == s_lt_gen) break;   /* latest screen generation wins */
        delete m;                        /* stale: dropped and freed */
        m = NULL;
    }
    if (!m) return;

    if (m->kind == LT_REQ_QUESTIONS) {
        if (!m->ok) {
            char buf[128];
            snprintf(buf, sizeof(buf), "fetch failed: %s", m->err);
            lt_status(buf);
            lt_render_home();
        } else {
            s_quiz_count = m->q_count;
            memcpy(s_quiz, m->qs, sizeof(m->qs));
            memset(s_correct, 0, sizeof(s_correct));
            s_quiz_pos = 0;
            lt_status("1-4 answer, Back: quit");
            lt_render_question();
        }
    } else if (m->kind == LT_REQ_SUBMIT) {
        if (!m->ok) {
            char buf[128];
            snprintf(buf, sizeof(buf), "submit failed: %s", m->err);
            lt_status(buf);
            lt_render_home();
        } else {
            s_result_cache = m->result;
            lt_status("done");
            lt_render_result();
        }
    } else {                             /* HISTORY (best effort) */
        if (m->ok) {
            s_hist_count = m->hist_count;
            memcpy(s_last_hist, m->hist, sizeof(m->hist));
        }
        if (s_lt_page == LT_PAGE_HOME) lt_render_home();
    }
    delete m;
}

/* ---- answering ------------------------------------------------------------------ */

void lt_answer(int opt_index)
{
    if (s_lt_page != LT_PAGE_QUIZ || s_lt_task) return;
    const pp_lt_question_t *q = &s_quiz[s_quiz_pos];
    if (opt_index < 0 || opt_index >= q->opt_count) return;

    s_correct[s_quiz_pos] = (opt_index == q->answer_index);
    Serial.printf("[LT] q%d lvl=%s pick=%d %s\n", s_quiz_pos, q->level,
                  opt_index + 1, s_correct[s_quiz_pos] ? "ok" : "miss");

    if (s_quiz_pos + 1 < s_quiz_count) {
        s_quiz_pos++;
        lt_render_question();
    } else {
        lt_status("Submitting...");
        lt_start(LT_REQ_SUBMIT);
    }
}

/* ---- keyboard (factory loop) ------------------------------------------------------ */

void leveltest_keyboard_poll(void)
{
    if (!s_status_lab) return;           /* screen never created */
    lt_consume();

    char c;
    int guard = 8;
    while (guard-- > 0 && keypad_get_val(&c)) {
        keypad_set_flag();
        if (c == '\b') {
            scr_mgr_pop(false);
            keypad_clear_chars();
            return;
        }
        if (s_lt_task) continue;         /* request in flight */

        if (s_lt_page == LT_PAGE_HOME && c == '\n') {
            lt_status("Fetching paper...");
            lt_start(LT_REQ_QUESTIONS);
        } else if (s_lt_page == LT_PAGE_QUIZ && c >= '1' && c <= '4') {
            lt_answer(c - '1');
        } else if (s_lt_page == LT_PAGE_RESULT && c == '\n') {
            lt_status("Fetching paper...");
            lt_start(LT_REQ_QUESTIONS);  /* retest straight away */
        }
    }
}

/* ---- lifecycle -------------------------------------------------------------------- */

static void entry_lt(void)
{
    ui_disp_full_refr();
    s_lt_gen++;                          /* drop any stale in-flight result */
    lt_status("Enter: start the test");
    lt_render_home();
    lt_start(LT_REQ_HISTORY);            /* best effort: fills "Last:" */
}

static void exit_lt(void)
{
    ui_disp_full_refr();
    s_lt_gen++;                          /* late results will be dropped */
}

static void destroy_lt(void)
{
    s_pg_home = s_pg_quiz = s_pg_result = NULL;
    s_home_hist_lab = NULL;
    s_progress_lab = s_stem_lab = NULL;
    s_score_lab = s_detail_lab = NULL;
    s_status_lab = NULL;
    for (int i = 0; i < PP_LT_OPT_MAX; i++) s_opt[i] = NULL;
}

scr_lifecycle_t screen_leveltest = {
    .create = create_lt,
    .entry = entry_lt,
    .exit = exit_lt,
    .destroy = destroy_lt,
};
