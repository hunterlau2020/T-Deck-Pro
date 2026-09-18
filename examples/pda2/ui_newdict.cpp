/**
 * @file      ui_newdict.cpp
 * @brief     Word Bank app "new_dict" - "⑫ 词库浏览" on the device
 *            (user request 2026-09-18).
 *
 * Endpoints (penpal_api.h, contract from remote_api_demo.py demo_vocab_bank
 * + openapi 2026-09-18; read-only, learn scope):
 *   GET /api/v1/words?skip&limit&q   one page of the bank; q matches the
 *        word OR meaning_zh (server LIKE) - prefix typing works.
 *   GET /api/v1/words/{id}/detail    the detail card: pos-grouped senses,
 *        examples, chunks, assoc groups (flattened in penpal_api for EPD).
 *
 * Async model (whoami wa_* pattern, async_ipc_contract §1): one worker task
 * at a time, result struct's first field is the screen generation; results
 * from a stale generation are dropped and freed by the consume path.
 *
 * UI states (one screen, LV_OBJ_FLAG_HIDDEN page switch):
 *   LIST    header (total / page range / query), 8 word rows, hint line;
 *           typing builds the query (echoed), Enter = fetch page 1 (or open
 *           the focused row when the query is unchanged - see nd_key),
 *           +/- move focus, past the last row turns the page, touch opens.
 *   DETAIL  head (word / phonetic / CEFR / meaning), senses, examples,
 *           chunks, assoc; Enter or Backspace returns to LIST.
 *
 * Chinese (meaning_zh, example zh, chunk zh) switches the label to
 * Font_Hanzi_16 per content (LevelTest v1.16 pattern) - the mono asset
 * font has no CJK glyphs.
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
#define ND_FONT &Font_Mono_Bold_15

/* ---- screen state (UI thread owned) ---------------------------------------- */

enum { ND_PAGE_LIST = 0, ND_PAGE_DETAIL };

static int s_nd_page = ND_PAGE_LIST;
static pp_wb_page_t s_list;              /* last fetched page */
static int s_focus = 0;                  /* focused row in s_list.items */
static int s_skip = 0;                   /* next list request offset */
static char s_query[26];                 /* typed search text ("" = browse) */
static bool s_query_dirty = false;       /* typed since the last fetch */
static pp_wb_detail_t s_detail;
static char s_status[96] = "Enter: browse";
static bool s_enter_pending = false;     /* Enter buffered across an
                                          * in-flight request (LevelTest
                                          * v1.18 lesson: never eat keys) */

/* ---- LVGL objects (created once per create; nulled on destroy) ------------- */

static lv_obj_t *s_pg_list, *s_pg_detail;
static lv_obj_t *s_head_lab, *s_query_lab;
static lv_obj_t *s_row[PP_WB_ROWS];
static lv_obj_t *s_d_head_lab, *s_d_senses_lab, *s_d_ex_lab, *s_d_tail_lab;
static lv_obj_t *s_status_lab;

static void nd_open_detail(int row);     /* below */

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

enum { ND_REQ_LIST = 0, ND_REQ_DETAIL };

struct nd_msg_t {
    uint32_t gen;                        /* screen generation (contract rule 2) */
    int kind;
    bool ok;
    char err[96];
    pp_wb_page_t page;                   /* LIST */
    pp_wb_detail_t detail;               /* DETAIL */
};

struct nd_req_t {
    uint32_t gen;
    int kind;
    string base;
    string key;
    int skip;                            /* LIST */
    string q;                            /* LIST */
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
    string err;
    const char *base = rq->base.c_str();
    const char *key = rq->key.c_str();

    if (rq->kind == ND_REQ_LIST) {
        m->ok = penpal_wb_list(base, key, rq->skip, rq->q.c_str(), &m->page,
                               &err);
    } else {
        m->ok = penpal_wb_detail(base, key, rq->id, &m->detail, &err);
    }
    snprintf(m->err, sizeof(m->err), "%s", err.c_str());

    if (s_nd_q) {
        xQueueSend(s_nd_q, &m, pdMS_TO_TICKS(2000));
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
        rq->skip = s_skip;
        rq->q = s_query;
    } else {
        rq->id = s_list.items[detail_row].id;
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

/* Row tap = open that word's detail (4_2 link-row pattern). */
static void nd_row_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    nd_open_detail((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_t *nd_page_create(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(86));
    lv_obj_align(page, LV_ALIGN_BOTTOM_MID, 0, -22);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 4, LV_PART_MAIN);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
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

static void create_nd(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Word Bank", nd_scr_pop_cb);

    /* LIST */
    s_pg_list = nd_page_create(parent);
    s_head_lab = nd_label(s_pg_list, "");
    s_query_lab = nd_label(s_pg_list, "q: _");
    for (int i = 0; i < PP_WB_ROWS; i++) {
        lv_obj_t *row = lv_btn_create(s_pg_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 26);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, nd_row_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *l = lv_label_create(row);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(l, ND_FONT, LV_PART_MAIN);
        lv_obj_set_style_text_color(l, lv_color_black(), LV_PART_MAIN);
        s_row[i] = l;
    }
    nd_label(s_pg_list, "+/-: focus/page  Enter: open");

    /* DETAIL */
    s_pg_detail = nd_page_create(parent);
    s_d_head_lab = nd_label(s_pg_detail, "");
    s_d_senses_lab = nd_label(s_pg_detail, "");
    s_d_ex_lab = nd_label(s_pg_detail, "");
    s_d_tail_lab = nd_label(s_pg_detail, "");
    nd_label(s_pg_detail, "Enter/Back: list");

    /* shared status line (always black - EPD lesson, issue_list §23) */
    s_status_lab = lv_label_create(parent);
    lv_obj_set_width(s_status_lab, lv_pct(100));
    lv_label_set_long_mode(s_status_lab, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_status_lab, ND_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_lab, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(s_status_lab, LV_ALIGN_BOTTOM_LEFT, 10, -4);
    lv_label_set_text(s_status_lab, s_status);
}

/* ---- render (one EPD flush per state change) ---------------------------------- */

static void nd_page_show(int page)
{
    s_nd_page = page;
    lv_obj_add_flag(s_pg_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pg_detail, LV_OBJ_FLAG_HIDDEN);
    if (page == ND_PAGE_LIST) lv_obj_clear_flag(s_pg_list, LV_OBJ_FLAG_HIDDEN);
    if (page == ND_PAGE_DETAIL)
        lv_obj_clear_flag(s_pg_detail, LV_OBJ_FLAG_HIDDEN);
}

static void nd_render_list(void)
{
    char head[64];
    if (s_list.count > 0)
        snprintf(head, sizeof(head), "Words %d-%d / %d", s_list.skip + 1,
                 s_list.skip + s_list.count, s_list.total);
    else
        snprintf(head, sizeof(head), "Words 0 / %d", s_list.total);
    lv_label_set_text(s_head_lab, head);

    char qbuf[40];
    snprintf(qbuf, sizeof(qbuf), "q: %s%s", s_query,
             s_query_dirty ? "_" : "");
    lv_label_set_text(s_query_lab, qbuf);

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
    nd_page_show(ND_PAGE_LIST);
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

    char sen[PP_WB_SENSE_MAX * 84 + 1];
    int off = 0;
    for (int i = 0; i < s_detail.sense_count &&
                    off < (int)sizeof(sen) - 84; i++)
        off += snprintf(sen + off, sizeof(sen) - off, "%s\n",
                        s_detail.senses[i]);
    if (off == 0) snprintf(sen, sizeof(sen), "(no senses)");
    nd_set_text(s_d_senses_lab, sen);

    char ex[PP_WB_EX_MAX * 164 + 1];
    off = 0;
    for (int i = 0; i < s_detail.ex_count && off < (int)sizeof(ex) - 164; i++)
        off += snprintf(ex + off, sizeof(ex) - off, "- %s\n",
                        s_detail.examples[i]);
    lv_label_set_text(s_d_ex_lab, ex);

    char tail[PP_WB_CHUNK_MAX * 84 + 100];
    off = 0;
    for (int i = 0; i < s_detail.chunk_count &&
                    off < (int)sizeof(tail) - 84; i++)
        off += snprintf(tail + off, sizeof(tail) - off, "# %s\n",
                        s_detail.chunks[i]);
    if (s_detail.assoc[0])
        off += snprintf(tail + off, sizeof(tail) - off, "%s\n",
                        s_detail.assoc);
    lv_label_set_text(s_d_tail_lab, tail);
    nd_page_show(ND_PAGE_DETAIL);
}

static void nd_render_empty_list(void)
{
    s_list = pp_wb_page_t{};
    s_focus = 0;
    nd_render_list();
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

    if (m->kind == ND_REQ_LIST) {
        if (!m->ok) {
            char buf[128];
            snprintf(buf, sizeof(buf), "list failed: %s", m->err);
            nd_status(buf);
            if (s_list.count == 0) nd_render_empty_list();
        } else {
            s_list = m->page;
            s_focus = 0;
            s_query_dirty = false;
            if (s_list.count == 0)
                nd_status(s_query[0] ? "no match - keep typing" : "bank empty");
            else
                nd_status("");
            nd_render_list();
        }
    } else {                             /* DETAIL */
        if (!m->ok) {
            char buf[128];
            snprintf(buf, sizeof(buf), "detail failed: %s", m->err);
            nd_status(buf);
            nd_render_list();
        } else {
            s_detail = m->detail;
            nd_status("");
            nd_render_detail();
        }
    }
    delete m;
}

/* ---- actions -------------------------------------------------------------------- */

static void nd_open_detail(int row)
{
    if (s_nd_page != ND_PAGE_LIST || s_nd_task) return;
    if (row < 0 || row >= s_list.count) return;
    s_focus = row;
    nd_status("Fetching detail...");
    nd_start(ND_REQ_DETAIL, row);
}

static void nd_fetch_list(int skip)
{
    s_skip = skip;
    nd_status(skip ? "Fetching page..." : "Fetching...");
    nd_start(ND_REQ_LIST, 0);
}

/* ---- keyboard (factory loop) ------------------------------------------------------ */

void newdict_keyboard_poll(void)
{
    if (!s_status_lab) return;           /* screen never created */
    nd_consume();

    /* buffered Enter fires as soon as the task goes idle (LevelTest v1.18:
     * keys are never eaten silently during an in-flight request) */
    if (s_enter_pending && !s_nd_task && s_nd_page == ND_PAGE_LIST) {
        s_enter_pending = false;
        if (s_query_dirty || s_list.count == 0)
            nd_fetch_list(0);
        else
            nd_open_detail(s_focus);
    }

    char c;
    int guard = 8;
    while (guard-- > 0 && keypad_get_val(&c)) {
        keypad_set_flag();
        if (c == '\b') {
            if (s_nd_page == ND_PAGE_DETAIL) {
                nd_render_list();        /* detail -> list */
                continue;
            }
            size_t len = strlen(s_query);
            if (len > 0) {
                /* cut one UTF-8 char (queries are typed ASCII, but be safe) */
                do { len--; } while (len > 0 && (s_query[len] & 0xC0) == 0x80);
                s_query[len] = '\0';
                s_query_dirty = true;
                nd_render_list();        /* echo the shorter query */
            } else {
                scr_mgr_pop(false);
                keypad_clear_chars();
                return;
            }
            continue;
        }
        if (c == '\t' || c == '\v') continue;  /* combo/volume: ignore */

        if (s_nd_page == ND_PAGE_DETAIL) {
            if (c == '\n') nd_render_list();
            continue;
        }

        /* LIST page */
        if (c == '\n') {
            if (s_nd_task) {             /* in flight: buffer, don't eat */
                s_enter_pending = true;
                continue;
            }
            if (s_query_dirty || s_list.count == 0)
                nd_fetch_list(0);        /* search with the typed query */
            else
                nd_open_detail(s_focus);
            continue;
        }
        if ((c == '+' || c == '-') && !s_nd_task) {
            if (s_list.count == 0) continue;
            if (c == '+') {
                if (s_focus + 1 < s_list.count) {
                    s_focus++;
                } else if (s_list.skip + s_list.count < s_list.total) {
                    nd_fetch_list(s_list.skip + s_list.count);
                }
            } else {
                if (s_focus > 0) {
                    s_focus--;
                } else if (s_list.skip > 0) {
                    nd_fetch_list(s_list.skip - PP_WB_ROWS);
                }
            }
            if (!s_nd_task) nd_render_list();
            continue;
        }
        if (!s_nd_task && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == ' ' || c == '\'')) {
            size_t len = strlen(s_query);
            if (len + 1 < sizeof(s_query)) {
                s_query[len] = (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
                s_query[len + 1] = '\0';
                s_query_dirty = true;
                nd_render_list();        /* echo the typed query */
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
    if (s_list.count == 0 && !s_nd_task) {
        nd_status("Fetching...");
        nd_fetch_list(0);                /* first page on first entry */
    } else {
        nd_render_list();
    }
}

static void exit_nd(void)
{
    ui_disp_full_refr();
    s_nd_gen++;                          /* late results will be dropped */
}

static void destroy_nd(void)
{
    s_pg_list = s_pg_detail = NULL;
    s_head_lab = s_query_lab = NULL;
    s_d_head_lab = s_d_senses_lab = NULL;
    s_d_ex_lab = s_d_tail_lab = NULL;
    s_status_lab = NULL;
    for (int i = 0; i < PP_WB_ROWS; i++) s_row[i] = NULL;
}

scr_lifecycle_t screen_newdict = {
    .create = create_nd,
    .entry = entry_nd,
    .exit = exit_nd,
    .destroy = destroy_nd,
};
