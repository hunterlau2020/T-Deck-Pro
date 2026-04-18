/**
 * @file      lunar_calendar.cpp
 * @author    LilyGo
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-04-01
 * @brief     US Federal Holidays + Chinese Traditional Holidays implementation.
 */
#include "lunar_calendar.h"
#include <string.h>

#ifdef ARDUINO
#include <pgmspace.h>
#else
#define PROGMEM
#endif

// ---- Day of week using Tomohiko Sakamoto's algorithm ----
int day_of_week(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// ---- US Federal Holidays (algorithmic) ----

// Get the nth occurrence of a weekday in a month (1-based)
// weekday: 0=Sun, 1=Mon, ..., 6=Sat
static int nth_weekday(int year, int month, int weekday, int n)
{
    int first = day_of_week(year, month, 1);
    int day = 1 + ((weekday - first + 7) % 7) + (n - 1) * 7;
    return day;
}

// Get the last occurrence of a weekday in a month
static int last_weekday(int year, int month, int weekday)
{
    // Days in month
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;

    int last_dow = day_of_week(year, month, dim);
    int diff = (last_dow - weekday + 7) % 7;
    return dim - diff;
}

const char *get_us_holiday(int year, int month, int day)
{
    switch (month) {
    case 1:
        if (day == 1) return "New Year's Day";
        if (day == nth_weekday(year, 1, 1, 3)) return "MLK Day";
        break;
    case 2:
        if (day == nth_weekday(year, 2, 1, 3)) return "Presidents' Day";
        break;
    case 5:
        if (day == last_weekday(year, 5, 1)) return "Memorial Day";
        break;
    case 6:
        if (day == 19) return "Juneteenth";
        break;
    case 7:
        if (day == 4) return "Independence Day";
        break;
    case 9:
        if (day == nth_weekday(year, 9, 1, 1)) return "Labor Day";
        break;
    case 10:
        if (day == nth_weekday(year, 10, 1, 2)) return "Columbus Day";
        break;
    case 11:
        if (day == 11) return "Veterans Day";
        if (day == nth_weekday(year, 11, 4, 4)) return "Thanksgiving";
        break;
    case 12:
        if (day == 25) return "Christmas Day";
        break;
    }
    return NULL;
}

// ---- Chinese Traditional Holidays (bilingual) ----

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    const char *name;
} chinese_holiday_entry_t;

static const chinese_holiday_entry_t chinese_holidays[] PROGMEM = {
    // Spring Festival / 春节
    {2024, 2, 10, "Spring Festival 春节"},
    {2025, 1, 29, "Spring Festival 春节"},
    {2026, 2, 17, "Spring Festival 春节"},
    {2027, 2, 6,  "Spring Festival 春节"},
    {2028, 1, 26, "Spring Festival 春节"},
    {2029, 2, 13, "Spring Festival 春节"},
    {2030, 2, 3,  "Spring Festival 春节"},

    // Lantern Festival / 元宵节
    {2024, 2, 24, "Lantern 元宵节"},
    {2025, 2, 12, "Lantern 元宵节"},
    {2026, 3, 3,  "Lantern 元宵节"},
    {2027, 2, 20, "Lantern 元宵节"},
    {2028, 2, 9,  "Lantern 元宵节"},
    {2029, 2, 27, "Lantern 元宵节"},
    {2030, 2, 17, "Lantern 元宵节"},

    // Qingming / 清明
    {2024, 4, 4,  "Qingming 清明"},
    {2025, 4, 4,  "Qingming 清明"},
    {2026, 4, 5,  "Qingming 清明"},
    {2027, 4, 5,  "Qingming 清明"},
    {2028, 4, 4,  "Qingming 清明"},
    {2029, 4, 4,  "Qingming 清明"},
    {2030, 4, 5,  "Qingming 清明"},

    // Dragon Boat / 端午节
    {2024, 6, 10, "Dragon Boat 端午"},
    {2025, 5, 31, "Dragon Boat 端午"},
    {2026, 6, 19, "Dragon Boat 端午"},
    {2027, 6, 9,  "Dragon Boat 端午"},
    {2028, 5, 28, "Dragon Boat 端午"},
    {2029, 6, 16, "Dragon Boat 端午"},
    {2030, 6, 5,  "Dragon Boat 端午"},

    // Mid-Autumn / 中秋节
    {2024, 9, 17, "Mid-Autumn 中秋"},
    {2025, 10, 6, "Mid-Autumn 中秋"},
    {2026, 9, 25, "Mid-Autumn 中秋"},
    {2027, 9, 15, "Mid-Autumn 中秋"},
    {2028, 10, 3, "Mid-Autumn 中秋"},
    {2029, 9, 22, "Mid-Autumn 中秋"},
    {2030, 9, 12, "Mid-Autumn 中秋"},

    // Double Ninth / 重阳节
    {2024, 10, 11, "Double Ninth 重阳"},
    {2025, 10, 29, "Double Ninth 重阳"},
    {2026, 10, 18, "Double Ninth 重阳"},
    {2027, 10, 8,  "Double Ninth 重阳"},
    {2028, 10, 26, "Double Ninth 重阳"},
    {2029, 10, 16, "Double Ninth 重阳"},
    {2030, 10, 5,  "Double Ninth 重阳"},
};

// ---- 24 Jieqi / 二十四节气 (pre-computed 2024-2030) ----

static const chinese_holiday_entry_t jieqi_table[] PROGMEM = {
    // 2025
    {2025, 1, 5,  "Xiaohan 小寒"},
    {2025, 1, 20, "Dahan 大寒"},
    {2025, 2, 3,  "Lichun 立春"},
    {2025, 2, 18, "Yushui 雨水"},
    {2025, 3, 5,  "Jingzhe 惊蛰"},
    {2025, 3, 20, "Chunfen 春分"},
    {2025, 4, 4,  "Qingming 清明"},
    {2025, 4, 20, "Guyu 谷雨"},
    {2025, 5, 5,  "Lixia 立夏"},
    {2025, 5, 21, "Xiaoman 小满"},
    {2025, 6, 5,  "Mangzhong 芒种"},
    {2025, 6, 21, "Xiazhi 夏至"},
    {2025, 7, 7,  "Xiaoshu 小暑"},
    {2025, 7, 22, "Dashu 大暑"},
    {2025, 8, 7,  "Liqiu 立秋"},
    {2025, 8, 23, "Chushu 处暑"},
    {2025, 9, 7,  "Bailu 白露"},
    {2025, 9, 22, "Qiufen 秋分"},
    {2025, 10, 8, "Hanlu 寒露"},
    {2025, 10, 23,"Shuangjing 霜降"},
    {2025, 11, 7, "Lidong 立冬"},
    {2025, 11, 22,"Xiaoxue 小雪"},
    {2025, 12, 7, "Daxue 大雪"},
    {2025, 12, 21,"Dongzhi 冬至"},
    // 2026
    {2026, 1, 5,  "Xiaohan 小寒"},
    {2026, 1, 20, "Dahan 大寒"},
    {2026, 2, 4,  "Lichun 立春"},
    {2026, 2, 18, "Yushui 雨水"},
    {2026, 3, 5,  "Jingzhe 惊蛰"},
    {2026, 3, 20, "Chunfen 春分"},
    {2026, 4, 5,  "Qingming 清明"},
    {2026, 4, 20, "Guyu 谷雨"},
    {2026, 5, 5,  "Lixia 立夏"},
    {2026, 5, 21, "Xiaoman 小满"},
    {2026, 6, 5,  "Mangzhong 芒种"},
    {2026, 6, 21, "Xiazhi 夏至"},
    {2026, 7, 7,  "Xiaoshu 小暑"},
    {2026, 7, 22, "Dashu 大暑"},
    {2026, 8, 7,  "Liqiu 立秋"},
    {2026, 8, 23, "Chushu 处暑"},
    {2026, 9, 7,  "Bailu 白露"},
    {2026, 9, 23, "Qiufen 秋分"},
    {2026, 10, 8, "Hanlu 寒露"},
    {2026, 10, 23,"Shuangjing 霜降"},
    {2026, 11, 7, "Lidong 立冬"},
    {2026, 11, 22,"Xiaoxue 小雪"},
    {2026, 12, 7, "Daxue 大雪"},
    {2026, 12, 22,"Dongzhi 冬至"},
    // 2027
    {2027, 1, 5,  "Xiaohan 小寒"},
    {2027, 1, 20, "Dahan 大寒"},
    {2027, 2, 4,  "Lichun 立春"},
    {2027, 2, 18, "Yushui 雨水"},
    {2027, 3, 5,  "Jingzhe 惊蛰"},
    {2027, 3, 20, "Chunfen 春分"},
    {2027, 4, 5,  "Qingming 清明"},
    {2027, 4, 20, "Guyu 谷雨"},
    {2027, 5, 5,  "Lixia 立夏"},
    {2027, 5, 21, "Xiaoman 小满"},
    {2027, 6, 5,  "Mangzhong 芒种"},
    {2027, 6, 21, "Xiazhi 夏至"},
    {2027, 7, 7,  "Xiaoshu 小暑"},
    {2027, 7, 23, "Dashu 大暑"},
    {2027, 8, 7,  "Liqiu 立秋"},
    {2027, 8, 23, "Chushu 处暑"},
    {2027, 9, 7,  "Bailu 白露"},
    {2027, 9, 23, "Qiufen 秋分"},
    {2027, 10, 8, "Hanlu 寒露"},
    {2027, 10, 23,"Shuangjing 霜降"},
    {2027, 11, 7, "Lidong 立冬"},
    {2027, 11, 22,"Xiaoxue 小雪"},
    {2027, 12, 7, "Daxue 大雪"},
    {2027, 12, 22,"Dongzhi 冬至"},
};

#define CHINESE_HOLIDAY_COUNT (sizeof(chinese_holidays) / sizeof(chinese_holidays[0]))
#define JIEQI_COUNT (sizeof(jieqi_table) / sizeof(jieqi_table[0]))

static const char *lookup_table(const chinese_holiday_entry_t *tbl, int tbl_len,
                                int year, int month, int day)
{
    for (int i = 0; i < tbl_len; i++) {
        if (tbl[i].year == year && tbl[i].month == month && tbl[i].day == day)
            return tbl[i].name;
    }
    return NULL;
}

const char *get_chinese_holiday(int year, int month, int day)
{
    return lookup_table(chinese_holidays, CHINESE_HOLIDAY_COUNT, year, month, day);
}

const char *get_holiday_name(int year, int month, int day)
{
    const char *name = get_us_holiday(year, month, day);
    if (name) return name;
    name = get_chinese_holiday(year, month, day);
    if (name) return name;
    return lookup_table(jieqi_table, JIEQI_COUNT, year, month, day);
}

int get_month_holidays(int year, int month, int *days, const char **names)
{
    int count = 0;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = mdays[month - 1];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) dim = 29;

    for (int d = 1; d <= dim && count < 31; d++) {
        /* Collect US holiday */
        const char *us = get_us_holiday(year, month, d);
        if (us && count < 31) { days[count] = d; names[count] = us; count++; }
        /* Collect Chinese holiday (may be same day as US) */
        const char *cn = get_chinese_holiday(year, month, d);
        if (cn && count < 31) { days[count] = d; names[count] = cn; count++; }
        /* Collect Jieqi */
        const char *jq = lookup_table(jieqi_table, JIEQI_COUNT, year, month, d);
        if (jq && jq != cn && count < 31) { days[count] = d; names[count] = jq; count++; }
    }
    return count;
}
