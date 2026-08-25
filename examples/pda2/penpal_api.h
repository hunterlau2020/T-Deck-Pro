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
