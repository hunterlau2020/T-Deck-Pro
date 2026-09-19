/**
 * @file      ui_newdict.h
 * @brief     Word Bank app "new_dict" ("⑫ 词库浏览", user request
 *            2026-09-18): browses the server vocab bank.
 *
 * One screen (menu page one since v1.20), top tabs HOME (search-first) /
 * LIST (exam books -> that book's words) plus a shared DETAIL page (body
 * locally paged with +/-, never scrolled - EPD discipline).
 * Keyboard: letters+digits build the search query (typing on List jumps
 * to HOME), Enter fetches (or opens the focused row), +/- turn whole
 * pages (touch picks rows; DETAIL: +/- page the card), Backspace deletes
 * a query char, climbs words->exams or exits. Touch: tabs and rows open,
 * back button exits.
 * Device-side search is English prefix/substring only (the keypad has no
 * Chinese input) - the server's meaning_zh substring match is unreachable
 * from this UI and not a defect (review ds4 Nit-2).
 * penpal_api carries the two endpoints; this module is UI + async glue.
 */
#ifndef __UI_NEWDICT_H__
#define __UI_NEWDICT_H__

#include "ui_deckpro.h"
#include "ui_scr_mrg.h"

/* factory loop hook (keyboard + result consumption) */
void newdict_keyboard_poll(void);

/* lifecycle struct definition lives in ui_newdict.cpp */
extern scr_lifecycle_t screen_newdict;

#endif /* __UI_NEWDICT_H__ */
