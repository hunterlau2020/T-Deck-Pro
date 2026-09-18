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
 * Whoami Cfg tab. 1.0 = baseline (`095e41a`); 1.1 = EPD grey + wifi CJK fixes; 1.2 = review round (whoami cfg-epoch P1, OTA NTP, sleep/OTA mutex); 1.3 = wifi scan vs reconnect-loop fix (-2); 1.4 = 4_2 async scan (no UI blocking) + scan-pick never clobbers a slot; 1.5 = bounded wifi auto-reconnect (5 tries + backoff, then idle); 1.6 = idle sleep 10 min while USB/charging (VBUS in), else 5; 1.7 = 4_2 scan loop holds the autoconn parked (kick-vs-begin collision fix); 1.8 = Disc button, scan-failure code on glass + kick/collect diagnostics, release-pending kick guard; 1.9 = connected async scans aborted by driver - always scan from idle, dropped link reconnects at once; 1.10 = single visible field cursor (DEFOCUSED pairing + static EPD cursor) + no silent field jump on empty-backspace; 1.11 = cursor.show driven directly (draw gate) + unconditional 4_2 exit reconnect; 1.12 = 4_1 cursors fully off ('>' label = focus) + status line tracks the live link state; 1.13 = LevelTest app (staircase placement test) + 3-page menu rearrange; 1.14 = review round - idempotent wifi event hooks (scan-done + autoconn), sticky dropped-link, whoami partial-save epoch bump; 1.15 = ds4 round - terminal-scan release fast-path, cursor.show writes removed (bg_opa is the real gate), status-line transitional states; 1.16 = LevelTest Chinese support (Font_Hanzi_16: SimSun 16px ASCII+3955 vocab hanzi, per-label CJK font switch); 1.17 = LevelTest rework - single-question adaptive staircase (termination-event submit, not fixed count); 1.18 = LevelTest pending-Enter buffer (first-entry key no longer eaten during the history fetch).
 */
#pragma once

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 18
