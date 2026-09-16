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
 * Whoami Cfg tab. Initial baseline: 1.0 (firmware up to `095e41a`).
 */
#pragma once

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0

/** "v<x>.<y> build <yyyy-mm-dd>" - compile-date build, static buffer. */
const char *fw_version_string(void);
