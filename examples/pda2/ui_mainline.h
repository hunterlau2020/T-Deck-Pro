/**
 * @file      ui_mainline.h
 * @brief     "Mainline" learning-spine app (design docs/mainline-design.md
 *            v3.1): today's recommended lesson -> start -> per-item
 *            answers (recognition options / recall+cloze typing / output
 *            sentences) -> finish score card.
 *
 * One screen (menu page one), three internal pages (HOME / SESSION /
 * RESULT) switched via LV_OBJ_FLAG_HIDDEN - one EPD flush per state
 * change, never scrolled. Keyboard: Enter start/submit/confirm, 1-4 pick
 * recognition options, letters/digits type into recall/cloze/output,
 * Backspace edits or exits. Touch: option rows + back button.
 * Review lessons live in a SEPARATE app (v1.31); here recommend=review
 * only shows a pointer line. Answers never reach this UI: the prompt
 * parser keeps only the per-kind render-set (HW-MAINLINE-1).
 */
#ifndef __UI_MAINLINE_H__
#define __UI_MAINLINE_H__

#include "ui_deckpro.h"
#include "ui_scr_mrg.h"

/* factory loop hook (keyboard + result consumption) */
void mainline_keyboard_poll(void);

/* lifecycle struct definition lives in ui_mainline.cpp */
extern scr_lifecycle_t screen_mainline;

#endif /* __UI_MAINLINE_H__ */
