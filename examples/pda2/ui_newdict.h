/**
 * @file      ui_newdict.h
 * @brief     Word Bank app "new_dict" ("⑫ 词库浏览", user request
 *            2026-09-18): browses the server vocab bank.
 *
 * One screen (menu page two), two internal states (LIST / DETAIL).
 * Keyboard: letters+digits build the search query, Enter fetches (or opens
 * the focused row), +/- move focus / turn pages, Backspace deletes a query
 * char or exits. Touch: rows open, back button exits.
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
