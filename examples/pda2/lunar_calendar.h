/**
 * @file      lunar_calendar.h
 * @brief     Holiday lookup: Calendarific API (primary) + computed tables (fallback) + jieqi.
 */
#pragma once

#include <stdint.h>

int day_of_week(int year, int month, int day);
const char *get_us_holiday(int year, int month, int day);
const char *get_chinese_holiday(int year, int month, int day);
const char *get_holiday_name(int year, int month, int day);

/**
 * @brief Get all holidays and jieqi for a given month.
 *        Tries Calendarific API first (cached in NVS), falls back to computed tables.
 * @param days Output array of days (at least 31 elements). A day may appear multiple times.
 * @param names Output array of name pointers (static/cached strings).
 * @return Number of entries found.
 */
int get_month_holidays(int year, int month, int *days, const char **names);

/**
 * @brief Fetch holidays from Calendarific API for a year+country and cache in NVS.
 *        Call from a FreeRTOS task (blocks on HTTP). Safe to call multiple times —
 *        skips if already cached for the given year+country.
 */
void holidays_fetch_api(int year, int month);
