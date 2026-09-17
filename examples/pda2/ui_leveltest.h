/**
 * @file      ui_leveltest.h
 * @brief     Level Test app ("定级测·整卷 staircase", user request 2026-09-18).
 *
 * One screen (menu-registered), four internal states (HOME / QUIZ /
 * SUBMIT / RESULT). Keyboard: Enter starts / advances, 1-4 answer,
 * Backspace exits. Touch: option rows answer, back button exits.
 * penpal_api carries the three endpoints; this module is UI + async glue.
 */
#ifndef __UI_LEVELTEST_H__
#define __UI_LEVELTEST_H__

#include "ui_deckpro.h"
#include "ui_scr_mrg.h"

/* factory loop hook (keyboard + result consumption) */
void leveltest_keyboard_poll(void);

/* lifecycle struct definition lives in ui_leveltest.cpp */
extern scr_lifecycle_t screen_leveltest;

#endif /* __UI_LEVELTEST_H__ */
