/**
 * @file      ui_penpal.h
 * @brief     PenPal app shared declarations (design: docs/penpal-design.md v3.1).
 *
 * One screen (registered by the menu commit), 7 internal pages switched
 * with LV_OBJ_FLAG_HIDDEN (§3.1) - COMPOSE's textareas are created ONCE so
 * the draft survives the COMPOSE<->TOPICS roundtrip. Async model (§3.2):
 * a single queue + busy flag + busy-generation + page-generation, results
 * carry (gen, type) as their first two fields.
 *
 * Split for reviewability (§6):
 *   ui_penpal.cpp        - async framework, waitbox (Close split by type),
 *                          HOME, CFG, screen lifecycle, keyboard poll
 *   ui_penpal_write.cpp  - COMPOSE, TOPICS, send/idempotency/edit-lock
 *   ui_penpal_read.cpp   - THREAD, FB (fix/polish), PROFILE
 */
#ifndef __UI_PENPAL_H__
#define __UI_PENPAL_H__

#include "Arduino.h"
#include "lvgl.h"
#include "penpal_api.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/* ---- internal pages (§3.1) ----------------------------------------------- */
typedef enum {
    PP_PAGE_HOME = 0,
    PP_PAGE_COMPOSE,
    PP_PAGE_TOPICS,
    PP_PAGE_THREAD,
    PP_PAGE_FB,
    PP_PAGE_PROFILE,
    PP_PAGE_CFG,
} pp_page_t;

#define PP_PAGE_CNT 7           /* keep in sync with pp_page_t */

/* ---- async result types (§3.2; single queue, type-dispatched) ------------- */
typedef enum {
    PP_RES_PALS = 0,
    PP_RES_MAILBOX,
    PP_RES_THREAD,
    PP_RES_SEND,
    PP_RES_TOPICS,
    PP_RES_FIX,
    PP_RES_POLISH,
    PP_RES_TIPS,
} pp_res_type_t;

/* Result: gen FIRST, type SECOND (§3.2). New'ed by the task, ownership moves
 * to the UI over s_pp_q, deleted after consume. Only the fields belonging to
 * `type` are filled. FB/TIPS text is FORMATTED IN THE TASK THREAD (§4.5). */
typedef struct {
    uint32_t gen;
    int type;               /* pp_res_type_t */
    bool ok;
    string err;
    /* SEND */
    bool replayed;          /* Idempotent-Replayed: true (§2.2) */
    int email_id;
    int root_id;
    bool reply_pending;
    /* payload arrays + counters */
    int count;
    bool truncated;         /* mailbox overflow */
    int dropped;            /* thread oldest-dropped (§5) */
    pp_pal_t pals[PP_PAL_MAX];
    pp_topic_t topics[PP_TOPIC_MAX];
    pp_thread_row_t rows[PP_MAILBOX_MAX];
    pp_letter_t letters[PP_THREAD_MAX];
    pp_fix_t fix;
    pp_polish_t polish;
    pp_tips_t tips;
    string text;            /* FIX/POLISH display text / TIPS msgbox text */
} pp_result_t;

/* Task-owned request snapshot (§3.2: base/key/params are full copies; the
 * task never reads UI-owned state). */
typedef struct {
    uint32_t gen;
    int type;               /* pp_res_type_t */
    string base;
    string key;
    string ai_provider;     /* AI Config provider id for LLM endpoints */
    string ai_model;        /* AI Config model id for LLM endpoints */
    int pen_pal_id;         /* THREAD */
    int thread_root_id;     /* THREAD / SEND(reply anchor) */
    int email_id;           /* FIX / POLISH / TIPS */
    char idem_key[33];      /* SEND; "" = no Idempotency-Key header */
    pp_send_req_t send;     /* SEND canonical payload (§5) */
} pp_task_req_t;

/* Screen-level shared state. Survives internal page switches; reset on
 * destroy. The COMPOSE draft lives in the (never destroyed) textareas plus
 * these fields - RAM only, NOT persisted (design R7). */
typedef struct {
    /* HOME */
    pp_pal_t pals[PP_PAL_MAX];
    int pals_cnt;
    pp_thread_row_t rows[PP_MAILBOX_MAX];    /* null-pal rows filtered out */
    int rows_cnt;
    bool mailbox_truncated;
    int home_page;
    /* COMPOSE (new/reply) */
    bool reply_mode;
    int comp_pal_id;
    char comp_pal_name[24];
    bool comp_has_topic;
    int comp_topic_id;
    char comp_topic_title[64];
    int comp_root_id;                          /* reply thread anchor */
    /* idempotency key lifecycle (§3.2): RAM, payload-bound */
    bool idem_valid;
    char idem_key[33];
    pp_send_req_t idem_snap;                   /* payload snapshot */
    bool send_lock;                            /* edit lock while SEND in flight */
    /* TOPICS */
    pp_topic_t topics[PP_TOPIC_MAX];
    int topics_cnt;
    int topics_page;
    int topics_sel;                            /* highlighted row, -1 = none */
    /* THREAD: letters stored NEWEST-FIRST (index 0 = newest, §4.4) */
    int thr_root;
    int thr_pal;                              /* 0 = residual read-only (R9) */
    char thr_subject[64];
    pp_letter_t letters[PP_THREAD_MAX];
    int letters_cnt;
    int thr_idx;
    int thr_dropped;                          /* oldest evicted (16KB, §5) */
    /* FB */
    bool fb_degraded;
    /* small helpers */
    char fmt[160];                             /* shared snprintf scratch */
} pp_state_t;

/* ---- framework state (ui_penpal.cpp) -------------------------------------- */
extern pp_state_t pp;
extern QueueHandle_t s_pp_q;
extern volatile uint32_t s_pp_gen;     /* page generation: entry/destroy/cancel */
extern volatile bool s_pp_busy;        /* UI-owned */
extern uint32_t s_pp_busy_gen;         /* only this gen may clear s_pp_busy */
extern bool s_pp_active;               /* keyboard poll gate */

/* Launch a request. tmpl is a STACK template - copied to a task-owned heap
 * snapshot; the caller never owns heap memory after the call. Returns false
 * (with the page status line set) when busy / WiFi down / config empty /
 * queue or task creation failed - busy is NOT set on failure.
 * chained=true: HOME serial PALS->MAILBOX second leg (§3.2) - skips the busy
 * gate and only retitles the waitbox, keeping busy held across both legs. */
bool pp_start(const pp_task_req_t *tmpl, bool chained);

/* Busy release rule (§3.2): only the in-flight generation may clear busy. */
void pp_release_busy(uint32_t gen);

/* Internal page switch (creates nothing - pages are pre-built, §3.1).
 * Runs the target page's show-hook (compose state / thread letter / profile
 * / cfg prefill) and full-refreshes the panel. */
void pp_set_page(pp_page_t page);
pp_page_t pp_get_page(void);

/* Register a pre-built page container with the switcher (builders call
 * this once at screen create; containers stay alive for the screen's
 * lifetime - the COMPOSE draft must survive page roundtrips, §3.1). */
void pp_page_register(pp_page_t page, lv_obj_t *cont);

/* Register a page's status line with pp_status_set (HOME/COMPOSE/CFG). */
void pp_status_register(pp_page_t page, lv_obj_t *lab);

/* Status line of the CURRENT page (HOME/COMPOSE/CFG have one). */
void pp_status_set(const char *fmt, ...);

/* HOME status line directly (even when another page is current) + the
 * one-sync-cycle note merge: a manual Sync clears the note, the sync
 * completion appends " · Mailbox OK" to it. Keeps "sent ok (replayed)"
 * visible across the post-send auto sync (§4.2). */
void pp_home_note(const char *fmt, ...);

/* HOME refresh: serial PALS -> MAILBOX chain (§3.2). manual=true (Sync
 * key/button) drops the one-cycle note; the post-send auto sync keeps it. */
void pp_home_sync(bool manual);

/* Simple one-button notice box (keyboard: any key closes). Overlay tracked
 * centrally so exit/destroy always cleans it. */
void pp_msgbox_show(const char *title, const char *text);
void pp_msgbox_close(void);
bool pp_msgbox_open(void);

/* Waitbox visibility for other files (e.g. COMPOSE background-send hint). */
bool pp_waitbox_visible(void);

/* Config helpers (UI thread; Preferences is not re-entrant). */
void pp_cfg_load(char *base, int base_len, char *key, int key_len);
bool pp_cfg_from_nvs(void);            /* true when NVS holds base/key */

/* UTF-8 helpers shared by write/read pages. */
int pp_utf8_count(const char *s);      /* characters, not bytes */

/* ---- per-file page builders + key handlers -------------------------------- */
/* ui_penpal_write.cpp */
void ppw_build(lv_obj_t *parent);          /* COMPOSE + TOPICS pages */
void ppw_key(char c);                      /* dispatch by current page */
void ppw_show_compose(void);               /* apply pp.* compose state to UI */
void ppw_open_reply(void);                 /* Reply btn: set state + Re: title */
void ppw_render_topics(void);
void ppw_lock_edit(bool lock);             /* SEND edit lock (v3.1 P1) */
bool ppw_send_in_background_hint(void);    /* status line after Close(SEND) */
void ppw_on_send_result(const pp_result_t *res);
void ppw_overlays_close(void);

/* ui_penpal_read.cpp */
void ppr_build(lv_obj_t *parent);          /* THREAD + FB + PROFILE pages */
void ppr_key(char c);
void ppr_show_thread(void);                /* render pp.letters[pp.thr_idx] */
void ppr_show_fb(const pp_result_t *res);  /* FIX/POLISH result -> FB page */
void ppr_show_profile(void);
void ppr_overlays_close(void);

/* Exposed for factory.ino loop() hookup (plain C++ linkage, same as the
 * other *_keyboard_poll siblings). */
void penpal_keyboard_poll(void);

#endif /* __UI_PENPAL_H__ */
