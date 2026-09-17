/**
 * @file      fw_version.h
 * @brief     Project firmware version (x.y.build), user decision 2026-09-16.
 *
 * Bump policy:
 *   x (major) - big upgrade batches / feature milestones (manual bump)
 *   y (minor) - small changes and fixes (manual bump)
 *   build     - COMPILE DATE, filled in automatically from __DATE__ -
 *               never maintained by hand.
 * Displayed on the boot splash, System info ("SF Version") and the
 * Whoami Cfg tab. 1.0 = baseline (`095e41a`); 1.1 = EPD grey + wifi CJK fixes; 1.2 = review round (whoami cfg-epoch P1, OTA NTP, sleep/OTA mutex); 1.3 = wifi scan vs reconnect-loop fix (-2); 1.4 = 4_2 async scan (no UI blocking) + scan-pick never clobbers a slot.
 */
#pragma once

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 4

/** "v<x>.<y> build <yyyy-mm-dd>" - compile-date build, static buffer. */
const char *fw_version_string(void);
