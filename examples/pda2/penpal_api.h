/**
 * @file      penpal_api.h
 * @brief     Pen-pal service API client (design: docs/penpal-design.md v3.2).
 *
 *            Own http/https transport (design §3.3): the LAN test server is
 *            plain http://, which http_utils cannot do - so this module
 *            carries its own request function (http:// -> WiFiClient,
 *            https:// -> WiFiClientSecure via http_get_tls_mode()) and
 *            reuses only http_require_wifi() policy plus the two additive
 *            http_utils exports http_apply_tls()/http_ensure_time() (one
 *            shared CA bundle; existing http_* callers untouched).
 *
 *            All request functions are BLOCKING and run in the PenPal worker
 *            task (design §3.2) - never on the UI thread. Parsing is fully
 *            defensive (design R1): a missing field yields a default + a
 *            serial log, never a crash.
 */
#pragma once

#include <stdint.h>
#include <string>

using namespace std;

/* ---- caps (design §5 / §4.7) -------------------------------------------- */
#define PP_PAL_MAX        5      /* home icon row: 3 real + headroom */
#define PP_TOPIC_MAX      16     /* topics list */
#define PP_MAILBOX_MAX    24     /* home thread rows; more -> truncated flag */
#define PP_THREAD_MAX     64     /* letter count cap; 16KB budget is the real limiter */
#define PP_THREAD_BUDGET  16384  /* total thread content budget, oldest dropped */
#define PP_LETTER_MAX     4096   /* single letter content, truncation + "(truncated)" */
#define PP_FIX_MAX        12     /* correction items */
#define PP_POLISH_IMP_MAX 8      /* polish improvement lines */
#define PP_COVERAGE_MAX   8      /* polish topic_coverage rows */
#define PP_TIPS_MAX       8      /* reply tips */
#define PP_BASE_MAX       96     /* 95 chars + NUL - aligned with env.cfg value cap (§3.4) */
#define PP_KEY_MAX        17     /* 16 chars + NUL (§4.7 textarea cap) */

/* ---- timeouts (design §2: LLM endpoints can take minutes, CRUD is fast) -- */
#define PP_TIMEOUT_CRUD_MS 20000   /* pals / mailbox / thread / topics / send */
#define PP_TIMEOUT_LLM_MS  180000  /* correction / polish / tips */

/* ---- data model (design §5) ---------------------------------------------- */

typedef struct {
    int id;
    bool is_npc;
    char name[24];
    char status[12];
} pp_pal_t;

typedef struct {
    int id;
    char title[64];      /* display copy, UTF-8-boundary truncated + "..." */
    char tag[12];        /* exam_tag */
    char background[96]; /* display copy */
    char guiding[192];   /* guiding_questions joined with spaces, display copy */
} pp_topic_t;

typedef struct {
    int root_id;             /* thread_root_id anchor - read/reply addressing (§2 ⑨) */
    int pal_id;              /* pen_pal_id; null -> 0 sentinel, read-only residual row (§5, R9) */
    char subject[64];        /* display copy (send side uses pp_send_req_t, §5) */
    char from[24];           /* mailbox "counterpart" */
    char last_sender[24];
    char state[12];          /* pending / replied / sent */
    int unread;
    int count;
    char last_at[20];
} pp_thread_row_t;

typedef struct {
    int id;
    bool mine;          /* sender_user_id != null -> letter I wrote (Fix/Polish) */
    char sender[24];
    char time[20];      /* created_at "2026-08-20T14:11:11" */
    string content;     /* heap-allocated; <= PP_LETTER_MAX after truncation */
} pp_letter_t;

/* ---- feedback payloads (design §2.1 ⑤⑥⑧) -------------------------------- */

typedef struct {
    char type[16];
    char from[96];         /* original text excerpt */
    char to[96];           /* corrected text excerpt */
    char explanation[192];
} pp_correction_t;

typedef struct {
    bool degraded;         /* server-side LLM fallback (FB page marks "(degraded)") */
    int count;
    bool truncated;        /* more than PP_FIX_MAX corrections arrived */
    pp_correction_t items[PP_FIX_MAX];
} pp_fix_t;

typedef struct {
    char question[96];
    char status[24];
} pp_cov_t;

typedef struct {
    bool degraded;
    string improved;       /* improved_email - full text, heap */
    int imp_count;
    char improvements[PP_POLISH_IMP_MAX][128];
    int cov_count;
    pp_cov_t coverage[PP_COVERAGE_MAX];
} pp_polish_t;

typedef struct {
    bool degraded;
    int count;
    bool truncated;        /* more than PP_TIPS_MAX tips arrived */
    char tips[PP_TIPS_MAX][160];
} pp_tips_t;

/* Canonical send payload (§5 "subject two faces"): std::string on the send
 * side so the 56-byte title budget and the payload-snapshot compare (§3.2
 * idempotency-key lifecycle) operate on the exact bytes delivered. */
typedef struct {
    int pen_pal_id;
    string subject;
    bool has_topic;
    int topic_id;
    bool has_thread_root;  /* reply anchor; false = new thread */
    int thread_root_id;
    string content;
} pp_send_req_t;

/* ---- config chain (design §3.4) -------------------------------------------
 * NVS namespace "penpal" (single slot, keys base/key - weather/provider-key
 * precedent; NO dual-slot: base/key have no cross-field consistency need)
 *   -> SPIFFS /env.cfg (PENPAL_BASE / PENPAL_KEY)
 *   -> gitignored config_keys.h (PENPAL_BASE_DEFAULT_DEV / PENPAL_KEY_DEFAULT_DEV)
 *   -> empty (Cfg page guides the user).
 * A field saved to NVS - even as "" - always wins over env.cfg: an explicit
 * empty save must not be silently replaced (openai_load_config principle).
 * load/save run on the UI thread only (Preferences is not re-entrant). */
void penpal_load_config(char *base, int base_len, char *key, int key_len);

/** @brief Single-slot NVS write of both fields + verify round-trip.
 *  @return false on NVS failure (caller shows it in the Cfg status line). */
bool penpal_save_config(const char *base, const char *key);

/**
 * @brief Load/save the selected AI provider name in NVS namespace "penpal".
 *        An empty string means "custom / none".  The actual base/model/key
 *        are resolved from AI Config via openai_api::ai_provider_get().
 */
void penpal_load_ai_provider(char *name, int name_len);
bool penpal_save_ai_provider(const char *name);

/* ---- response cache (SPIFFS /penpal/*.json, 2-day TTL) --------------------
 * Product request 2026-08-26: HOME/THREAD render from cache when present;
 * Sync / per-thread refresh force a network re-fetch (getters overwrite the
 * cache on success). Load functions parse the cached raw body with the same
 * parse-only helpers as the network path. */

/** @brief Drop the pals + mailbox cache files (manual Sync path). */
void penpal_cache_drop_home(void);

/** @brief Load pals from cache (false = miss/expired/bad). */
bool penpal_cache_load_pals(pp_pal_t *out, int max, int *count);

/** @brief Load the mailbox listing from cache (false = miss/expired/bad). */
bool penpal_cache_load_mailbox(pp_thread_row_t *out, int max, int *count,
                               bool *truncated);

/** @brief Load one thread by its root id from cache (false = miss/expired/bad). */
bool penpal_cache_load_thread(int thread_root_id,
                              pp_letter_t *out, int max, int *count, int *dropped);

/* ---- idempotency key (design §2.2) ---------------------------------------- */

/** @brief Fill out with a fresh 32-hex Idempotency-Key (hardware RNG,
 *  16 random bytes). The UI binds it to the send payload snapshot and keeps
 *  it in RAM only (§3.2 lifecycle: reuse on unchanged retry, new on edit,
 *  void on confirmed success - never persisted). */
void penpal_new_idem_key(char out[33]);

/* ---- endpoints (blocking; worker task only) -------------------------------
 * All return true on HTTP 2xx AND successful parse. On failure *err (when
 * non-NULL) gets a human-readable line: transport error, "HTTP <code>" plus
 * the server's {"detail": "..."} when present. */

bool penpal_get_pals(const char *base, const char *key,
                     pp_pal_t *out, int max, int *count, string *err);

/** @brief CFG "Test" probe (user request 2026-09-13): GET /pen-pals with
 *         the key - validates reachability, HTTP and auth in one call.
 * @param detail always filled: "HTTP 200 OK, N pen-pals" on success,
 *         transport/HTTP/auth error text otherwise.
 * @return true on HTTP 200. */
bool penpal_test_base(const char *base, const char *key, string *detail);

/* ---- "Who am I" profile (Whoami app, 2026-09-14) ---------------------------
 * GET /api/v1/users/me/profile - the API-key holder's own record
 * (remote_api_demo.py step 0). */
typedef struct {
    char name[48];
    char age_band[24];
    char level[12];
    char city[48];
    char interests[96];
} pp_profile_t;

/** @brief Fetch the API-key holder's profile. Empty fields arrive as "".
 *  @return true on HTTP 2xx + parse; *err holds the failure line otherwise. */
bool penpal_get_profile(const char *base, const char *key,
                        pp_profile_t *out, string *err);

/* ---- level test (LevelTest app, 2026-09-18; reworked 2026-09-18 v1.17) -----
 * Single-question adaptive staircase ("⓰ 定级测 (单题自适应循环)" in
 * remote_api_demo.py - the ESP32 reference flow):
 *   GET /users/me/level-test/questions/next?level=<CEFR>&exclude=<stems>
 *     one question per call; answer_index ships with it (client grades,
 *     self-reported, 契约 §5); exclude = already-asked stems joined "|||"
 *     (same paper, no repeats); "B2+" must arrive URL-encoded as B2%2B.
 *   POST /users/me/level-test  {answers:[{level,correct}..], mode:"staircase"}
 *     SUBMIT CONDITION IS A TERMINATION EVENT, not a fixed answer count:
 *     B2+ 2-correct top / Pre-A1 2-wrong floor / 2nd demotion / 16-answer
 *     cap; 2..16 answers. The client mirrors the server staircase locally
 *     (2 up / 2 down / max 2 demotions, leveling.py grade_staircase) and
 *     submits exactly the consumed prefix - force-submitting after a fixed
 *     10 questions gets HTTP 400 "session incomplete" (v1.16 device report).
 */
#define PP_LT_A_MAX    16   /* staircase answer cap = server STAIR_MAX_ANSWERS */
#define PP_LT_OPT_MAX  4
#define PP_LT_DET_MAX  8    /* level_detail rows (Pre-A1..B2+ = 5, headroom) */
#define PP_LT_HIST_MAX 4    /* history rows shown on the entry page */

typedef struct {
    int  index;
    char level[8];                        /* CEFR: Pre-A1 / A1 / A2 / B1 / B2+ */
    char type[10];                        /* grammar | vocab */
    char stem[128];                       /* display copy */
    int  opt_count;
    char options[PP_LT_OPT_MAX][48];      /* display copy */
    int  answer_index;                    /* ships with the question (client grades) */
} pp_lt_question_t;

typedef struct {
    char level[8];                        /* probed CEFR level of this answer */
    bool correct;                         /* client-graded outcome (self-report) */
} pp_lt_answer_t;

typedef struct {
    char level[8];
    int  answered;
    int  correct;
    bool passed;
} pp_lt_level_detail_t;

typedef struct {
    int  score;
    char resulting_level[8];
    int  det_count;
    pp_lt_level_detail_t detail[PP_LT_DET_MAX];
} pp_lt_result_t;

typedef struct {
    char created_at[20];                   /* "2026-09-13T16:00:05" (first 10 = date) */
    int  score;
    char resulting_level[8];
} pp_lt_hist_t;

/** @brief GET /users/me/level-test/questions/next - one adaptive question for
 *         the probed level. exclude = already-asked stems joined "|||"
 *         ("" = none); both params are percent-encoded here (B2+ -> B2%2B).
 *         404 "No questions available for this level" arrives as false+err. */
bool penpal_lt_next_question(const char *base, const char *key,
                             const char *level, const char *exclude,
                             pp_lt_question_t *out, string *err);

/** @brief POST /users/me/level-test - self-reported staircase submission.
 *         answers[] must be a TERMINATED session (top/floor/2nd demotion/
 *         16-cap - the client mirrors the server staircase); otherwise the
 *         server answers 400 "session incomplete". */
bool penpal_lt_submit(const char *base, const char *key,
                      const pp_lt_answer_t *answers, int n,
                      pp_lt_result_t *out, string *err);

/** @brief GET /users/me/level-test/history - most recent rows (entry page). */
bool penpal_lt_get_history(const char *base, const char *key,
                           pp_lt_hist_t *out, int max, int *count, string *err);

/* ---- word bank (Word Bank app "new_dict", 2026-09-18) -----------------------
 * "⑫ 词库浏览" in remote_api_demo.py - read-only learn-scope endpoints:
 *   GET /words?skip&limit&cefr&theme&q -> {items, total, skip, limit};
 *   q matches word OR meaning_zh (LIKE), so English prefixes and Chinese
 *   substrings both work. GET /words/{id}/detail -> the VOCABD-2 detail
 *   card (pos-grouped senses, examples, chunks, assoc groups, tags). */
#define PP_WB_ROWS      8    /* list rows fetched per page (device screen) */
#define PP_WB_SENSE_MAX 6    /* flattened "pos: zh (en)" lines kept */
#define PP_WB_EX_MAX    2    /* example lines kept */
#define PP_WB_CHUNK_MAX 2    /* chunk lines kept */

typedef struct {
    int  id;
    char word[28];
    char cefr_level[8];                   /* Pre-A1 / A1 / A2 / B1 / B2+ */
    char meaning_zh[56];                  /* display copy (CJK possible) */
} pp_wb_item_t;

typedef struct {
    int  total;                           /* whole-bank match count */
    int  skip;                            /* server-side echo */
    int  count;                           /* rows stored (<= PP_WB_ROWS) */
    pp_wb_item_t items[PP_WB_ROWS];
} pp_wb_page_t;

typedef struct {
    int  id;
    char word[28];
    char phonetic[24];                    /* "" when absent */
    char cefr_level[8];
    char meaning_zh[64];
    int  sense_count;                     /* "pos: zh (en)" lines */
    char senses[PP_WB_SENSE_MAX][80];
    int  ex_count;                        /* "en / zh" lines */
    char examples[PP_WB_EX_MAX][160];
    int  chunk_count;                     /* "text (zh)" lines */
    char chunks[PP_WB_CHUNK_MAX][80];
    char assoc[96];                       /* first group "type: w1, w2" */
} pp_wb_detail_t;

/** @brief GET /words - one bank page. q may be "" (browse); percent-encoded
 *         here. cefr "" = no filter. */
bool penpal_wb_list(const char *base, const char *key, int skip, const char *q,
                    pp_wb_page_t *out, string *err);

/** @brief GET /words/{id}/detail - the detail card, flattened for EPD. */
bool penpal_wb_detail(const char *base, const char *key, int id,
                      pp_wb_detail_t *out, string *err);

bool penpal_get_topics(const char *base, const char *key,
                       pp_topic_t *out, int max, int *count, string *err);

/** @param truncated set true when the server returned more than max rows. */
bool penpal_get_mailbox(const char *base, const char *key,
                        pp_thread_row_t *out, int max, int *count,
                        bool *truncated, string *err);

/** @brief Fetch one thread by its first-letter anchor (§2 ④). pen_pal_id > 0
 *         goes into the query; <= 0 (null-pal residual row sentinel) OMITS it -
 *         the server's R9 channel reads participant-authorized by
 *         thread_root_id alone (live-verified 2026-08-22; response
 *         pen_pal_id is null on that path).
 *  Letters come back in server order (ascending/oldest-first); per-letter
 *  content is truncated to PP_LETTER_MAX and the oldest letters are dropped
 *  to fit PP_THREAD_BUDGET (§5) - *dropped reports how many (0 = none). */
bool penpal_get_thread(const char *base, const char *key,
                       int pen_pal_id, int thread_root_id,
                       pp_letter_t *out, int max, int *count, int *dropped,
                       string *err);

/** @brief Create (or reply within) a thread. Body carries thread_root_id for
 *         replies + "Re: " subject (§2 ⑦ double anchor). idem_key may be
 *         NULL (legacy behavior); when given, a replayed delivery returns
 *         the SAME letter with *replayed = true (§2.2). 201 first / 200
 *         replay are both success. */
bool penpal_send_email(const char *base, const char *key,
                       const pp_send_req_t *req, const char *idem_key,
                       int *email_id, int *thread_root_id, bool *reply_pending,
                       bool *replayed, string *err);

bool penpal_correction(const char *base, const char *key, int email_id,
                       const char *ai_provider, const char *ai_model,
                       pp_fix_t *out, string *err);

bool penpal_polish(const char *base, const char *key, int email_id,
                   const char *ai_provider, const char *ai_model,
                   pp_polish_t *out, string *err);

bool penpal_tips(const char *base, const char *key, int email_id,
                 const char *ai_provider, const char *ai_model,
                 pp_tips_t *out, string *err);
