/**
 * @file      ui_leveltest.cpp
 * @brief     Level Test app - "⓰ 定级测 (单题自适应循环)" on the device
 *            (user request 2026-09-18; whole-paper flow reworked v1.17
 *            after a device 400 "session incomplete" report).
 *
 * Contract (remote_api_demo.py step 16, the ESP32 reference flow):
 *   GET  /users/me/level-test/questions/next?level=&exclude=  one question
 *        per call (answer_index ships with it; grading is client-side and
 *        self-reported, 契约 §5). exclude = "|||"-joined already-asked stems.
 *   POST /users/me/level-test  {answers:[{level,correct}..],mode:"staircase"}
 *        THE SUBMIT CONDITION IS A TERMINATION EVENT, not a fixed count:
 *        B2+ 2-correct top / Pre-A1 2-wrong floor / 2nd demotion / 16-answer
 *        cap; 2..16 answers. The client mirrors the server staircase
 *        (leveling.py grade_staircase: 2 up / 2 down / max 2 demotions)
 *        locally and submits exactly the consumed prefix - force-submitting
 *        after 10 questions gets 400 (v1.16 device report).
 *
 * Async model (whoami wa_* pattern, async_ipc_contract §1): one worker task
 * at a time, result struct's first field is the screen generation; results
 * from a stale generation are dropped and freed by the consume path.
 *
 * UI states (one screen, LV_OBJ_FLAG_HIDDEN page switch - penpal pattern):
 *   HOME   last result + "Enter: start"          -> fetch first question
 *   QUIZ   one question per render: progress row, stem, up to 4 option rows;
 *          keys 1-4 or a tap answers -> local grade + staircase step ->
 *          next fetch, or submit on termination (one EPD render/question)
 *   RESULT score + resulting level + per-level detail + "Enter: retest"
 *
 * Keyboard: Enter start/retest, 1-4 answer, Backspace exit (abandons the
 * session; the protocol is stateless, nothing to clean up server-side).
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

/* Server CEFR_ORDER mirror (leveling.py): probe ladder, floor Pre-A1. */
static const char *const LT_ORDER[] = {"Pre-A1", "A1", "A2", "B1", "B2+"};
enum { LT_LEVEL_TOP = 4 };

/* Staircase state - MUST stay a faithful mirror of grade_staircase:
 * 2 consecutive correct -> level up (terminate at B2+ top);
 * 2 consecutive wrong   -> level down (terminate at Pre-A1 floor, or on
 * the 2nd demotion); PP_LT_A_MAX answers without an event -> cap. */
static int s_st_idx, s_st_sc, s_st_sw, s_st_downs;
static pp_lt_answer_t s_ans[PP_LT_A_MAX];
static int s_ans_n = 0;
static pp_lt_question_t s_cur;           /* question currently on screen */
static bool s_next_retry = false;        /* one silent retry for a NEXT fetch */
static bool s_enter_pending = false;     /* Enter seen during an in-flight
                                          * request - fires when the task
                                          * goes idle (see keyboard poll) */

/* Asked-stem list for the exclude param ("|||"-joined, RAW/decoded bytes -
 * the server validates the decoded value at 2000; stop appending when the
 * cap looms: repeats are legal, only less nice). */
#define LT_EXCL_MAX 1990
static char s_exclude[LT_EXCL_MAX];
static int s_excl_len = 0;

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

/* ---- staircase mirror + session --------------------------------------------- */

static void lt_session_reset(void)
{
    s_st_idx = s_st_sc = s_st_sw = s_st_downs = 0;
    s_ans_n = 0;
    s_excl_len = 0;
    s_exclude[0] = '\0';
    s_next_retry = false;
    s_enter_pending = false;
}

/* One step per recorded answer; returns the termination reason or NULL. */
static const char *lt_stair_step(bool correct)
{
    if (correct) {
        s_st_sc++;
        s_st_sw = 0;
        if (s_st_sc >= 2) {
            if (s_st_idx >= LT_LEVEL_TOP) return "B2+ top";
            s_st_idx++;
            s_st_sc = s_st_sw = 0;
        }
    } else {
        s_st_sw++;
        s_st_sc = 0;
        if (s_st_sw >= 2) {
            if (s_st_idx <= 0) return "Pre-A1 floor";
            s_st_downs++;
            if (s_st_downs >= 2) return "2nd demotion";
            s_st_idx--;
            s_st_sc = s_st_sw = 0;
        }
    }
    if (s_ans_n >= PP_LT_A_MAX) return "16-answer cap";
    return NULL;
}

static void lt_exclude_add(const char *stem)
{
    int len = (int)strlen(stem);
    int need = len + (s_excl_len ? 3 : 0) + 1;
    if (s_excl_len + need > LT_EXCL_MAX) return;
    if (s_excl_len) {
        memcpy(s_exclude + s_excl_len, "|||", 3);
        s_excl_len += 3;
    }
    memcpy(s_exclude + s_excl_len, stem, (size_t)len + 1);
    s_excl_len += len;
}

/* ---- async glue (wa_* pattern) ---------------------------------------------- */

enum { LT_REQ_NEXT = 0, LT_REQ_SUBMIT, LT_REQ_HISTORY };

struct lt_msg_t {
    uint32_t gen;                        /* screen generation (contract rule 2) */
    int kind;
    bool ok;
    char err[96];
    pp_lt_question_t question;           /* NEXT */
    pp_lt_result_t result;               /* SUBMIT */
    int hist_count;                      /* HISTORY */
    pp_lt_hist_t hist[PP_LT_HIST_MAX];
};

struct lt_req_t {
    uint32_t gen;
    int kind;
    string base;
    string key;
    string level;                        /* NEXT: probed CEFR level */
    string exclude;                      /* NEXT: asked stems snapshot */
    int n;                               /* SUBMIT */
    pp_lt_answer_t ans[PP_LT_A_MAX];
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

    if (rq->kind == LT_REQ_NEXT) {
        m->ok = penpal_lt_next_question(base, key, rq->level.c_str(),
                                        rq->exclude.c_str(), &m->question,
                                        &err);
    } else if (rq->kind == LT_REQ_SUBMIT) {
        m->ok = penpal_lt_submit(base, key, rq->ans, rq->n, &m->result, &err);
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
        Serial.println("[LT] start refused: server/key not set");
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        lt_status("WiFi not connected");
        Serial.println("[LT] start refused: WiFi not connected");
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
    if (kind == LT_REQ_NEXT) {
        rq->level = LT_ORDER[s_st_idx];
        rq->exclude = s_exclude;
    } else if (kind == LT_REQ_SUBMIT) {
        rq->n = s_ans_n;
        memcpy(rq->ans, s_ans, sizeof(s_ans));
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
    lt_label(s_pg_home, "CEFR placement test\nadaptive: 2-16 questions");
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
    char prog[48];
    snprintf(prog, sizeof(prog), "Q%d  %s  %s", s_ans_n + 1, s_cur.level,
             s_cur.type);
    lv_label_set_text(s_progress_lab, prog);
    lt_set_text(s_stem_lab, s_cur.stem);
    for (int i = 0; i < PP_LT_OPT_MAX; i++) {
        if (i < s_cur.opt_count) {
            char buf[56];
            snprintf(buf, sizeof(buf), "%d. %s", i + 1, s_cur.options[i]);
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

    if (m->kind == LT_REQ_NEXT) {
        if (m->ok) {
            s_next_retry = false;
            s_cur = m->question;
            lt_status("1-4 answer, Back: quit");
            lt_render_question();
        } else if (s_ans_n == 0) {
            /* first question unreachable: nothing to answer, no session */
            char buf[128];
            snprintf(buf, sizeof(buf), "fetch failed: %s", m->err);
            lt_status(buf);
            lt_render_home();
        } else if (!s_next_retry) {
            /* mid-test hiccup: one silent retry with the same probe level */
            s_next_retry = true;
            lt_status("fetch failed - retrying...");
            lt_start(LT_REQ_NEXT);
        } else {
            /* second failure: abandon WITHOUT submitting - a real user
             * cannot answer an unfetched question, and blind self-reported
             * answers would be fiction (deviation from the demo's smoke
             * loop, which simulates a student and keeps answering). */
            char buf[128];
            snprintf(buf, sizeof(buf), "fetch failed: %s (aborted, not submitted)",
                     m->err);
            lt_status(buf);
            lt_render_home();
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
    if (opt_index < 0 || opt_index >= s_cur.opt_count) return;

    bool correct = (opt_index == s_cur.answer_index);
    if (s_ans_n < PP_LT_A_MAX) {
        snprintf(s_ans[s_ans_n].level, sizeof(s_ans[s_ans_n].level), "%s",
                 LT_ORDER[s_st_idx]);
        s_ans[s_ans_n].correct = correct;
    }
    s_ans_n++;                           /* lt_stair_step caps at PP_LT_A_MAX */
    Serial.printf("[LT] a%d lvl=%s pick=%d %s\n", s_ans_n, LT_ORDER[s_st_idx],
                  opt_index + 1, correct ? "ok" : "miss");

    lt_exclude_add(s_cur.stem);
    const char *reason = lt_stair_step(correct);
    if (reason) {
        Serial.printf("[LT] terminated: %s after %d answers\n", reason,
                      s_ans_n);
        char buf[96];
        snprintf(buf, sizeof(buf), "Submitting (%s)...", reason);
        lt_status(buf);
        lt_start(LT_REQ_SUBMIT);
    } else {
        lt_status("Fetching next...");
        lt_start(LT_REQ_NEXT);
    }
}

/* ---- keyboard (factory loop) ------------------------------------------------------ */

void leveltest_keyboard_poll(void)
{
    if (!s_status_lab) return;           /* screen never created */
    lt_consume();

    /* An Enter seen while a request was in flight fires as soon as the
     * task goes idle. First entry after boot is the deterministic trap:
     * entry_lt() launches the history fetch, the session's FIRST HTTP(S)
     * round-trip can hold s_lt_task for seconds, and the old plain
     * `continue` ate the key with zero feedback - the user had to press
     * Enter twice (device report 2026-09-18). Quiz keys 1-4 stay dropped
     * in flight ON PURPOSE: the visible question is already answered. */
    if (s_enter_pending && !s_lt_task && s_lt_page != LT_PAGE_QUIZ) {
        s_enter_pending = false;
        lt_session_reset();
        lt_status("Fetching first question...");
        lt_start(LT_REQ_NEXT);
        return;
    }

    char c;
    int guard = 8;
    while (guard-- > 0 && keypad_get_val(&c)) {
        keypad_set_flag();
        if (c == '\b') {
            scr_mgr_pop(false);
            keypad_clear_chars();
            return;
        }
        if (s_lt_task) {                 /* request in flight */
            if (c == '\n' && s_lt_page != LT_PAGE_QUIZ)
                s_enter_pending = true;  /* buffered, not eaten */
            continue;
        }

        if (s_lt_page != LT_PAGE_QUIZ && c == '\n') {
            lt_session_reset();
            lt_status("Fetching first question...");
            lt_start(LT_REQ_NEXT);
        } else if (s_lt_page == LT_PAGE_QUIZ && c >= '1' && c <= '4') {
            lt_answer(c - '1');
        }
    }
}

/* ---- lifecycle -------------------------------------------------------------------- */

static void entry_lt(void)
{
    ui_disp_full_refr();
    s_lt_gen++;                          /* drop any stale in-flight result */
    lt_session_reset();
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
