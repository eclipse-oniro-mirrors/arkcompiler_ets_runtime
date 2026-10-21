/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/platform/time.h"

#include <ctime>
#include <cmath>

namespace panda::ecmascript {
static constexpr int64_t MS_PER_SECOND = 1000;
static constexpr int64_t HOUR_PER_DAY = 24;
static constexpr int64_t MINUTE_PER_DAY = 24 * 60;
static constexpr int64_t SECOND_PER_DAY = 24 * 60 * 60;
static constexpr int64_t MS_PER_DAY = SECOND_PER_DAY * MS_PER_SECOND;
// Window of years the C library can always handle, even with 32-bit time_t
// (32-bit time_t covers roughly 1901-12-13 to 2038-01-19).
static constexpr int64_t MIN_SAFE_YEAR = 1902;
static constexpr int64_t MAX_SAFE_YEAR = 2037;

// Calendar constants of the ECMA-262 DayFromYear/leap-year formulas below.
static constexpr int64_t DAYS_PER_COMMON_YEAR = 365;
static constexpr int64_t DAYS_PER_WEEK = 7;
static constexpr int64_t LEAP_YEAR_INTERVAL = 4;   // leap year every 4 years,
static constexpr int64_t CENTURY_INTERVAL = 100;   // except every 100 years,
static constexpr int64_t GREGORIAN_CYCLE = 400;    // unless every 400 years
static constexpr int64_t LEAP_BASE_YEAR = 1969;    // anchor of the 4-year term
static constexpr int64_t CENTURY_BASE_YEAR = 1901; // anchor of the 100-year term
static constexpr int64_t CYCLE_BASE_YEAR = 1601;   // anchor of the 400-year term
static constexpr int64_t EPOCH_YEAR = 1970;        // ECMA-262 epoch year
static constexpr int64_t MONTHS_PER_YEAR = 12;
static constexpr int64_t FEBRUARY_INDEX = 1;       // 0-based month index of February
// Days in each month for a common year; February gains one day in leap years.
static constexpr int64_t MONTH_LENGTHS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static int64_t FloorDiv(int64_t dividend, int64_t divisor)
{
    if (divisor == 0) {
        return 0;
    }
    int64_t quotient = dividend / divisor;
    if ((dividend % divisor != 0) && ((dividend < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

// ECMA-262 DayFromYear: day number of January 1st of the year, relative to the epoch.
// Pure 64-bit arithmetic, independent of time_t and the C library.
static int64_t DayFromYear(int64_t year)
{
    return DAYS_PER_COMMON_YEAR * (year - EPOCH_YEAR) + FloorDiv(year - LEAP_BASE_YEAR, LEAP_YEAR_INTERVAL) -
           FloorDiv(year - CENTURY_BASE_YEAR, CENTURY_INTERVAL) + FloorDiv(year - CYCLE_BASE_YEAR, GREGORIAN_CYCLE);
}

static bool IsLeapYear(int64_t year)
{
    return (year % LEAP_YEAR_INTERVAL == 0 && year % CENTURY_INTERVAL != 0) || year % GREGORIAN_CYCLE == 0;
}

// Find the latest year in [MIN_SAFE_YEAR, MAX_SAFE_YEAR] whose calendar is identical to
// the given year: same leap-ness and same weekday of January 1st. With an identical
// calendar, the month/day layout and DST transition dates (e.g. "second Sunday of March")
// line up, so the C library applies the same time zone rules to the equivalent year.
// The latest match is used so that out-of-range years inherit the most recent DST rules.
static int64_t FindEquivalentYear(int64_t year)
{
    int64_t jan1Weekday = ((DayFromYear(year) % DAYS_PER_WEEK) + DAYS_PER_WEEK) % DAYS_PER_WEEK;
    for (int64_t candidate = MAX_SAFE_YEAR; candidate >= MIN_SAFE_YEAR; --candidate) {
        if (IsLeapYear(candidate) == IsLeapYear(year) &&
            ((DayFromYear(candidate) % DAYS_PER_WEEK) + DAYS_PER_WEEK) % DAYS_PER_WEEK == jan1Weekday) {
            return candidate;
        }
    }
    return -1;
}

static int64_t MonthLength(int64_t year, int64_t month)
{
    int64_t length = MONTH_LENGTHS[month];
    if (month == FEBRUARY_INDEX && IsLeapYear(year)) {
        ++length;
    }
    return length;
}

// Split an absolute day number back into a normalized (year, month 0-based, day 1-based).
static void CalendarDateFromDays(int64_t days, int64_t &year, int64_t &month, int64_t &day)
{
    // 1461: days per 4-year leap cycle; the estimate is off by at most one year.
    year = EPOCH_YEAR + FloorDiv(days * 4, 1461); // 4, 1461: 4-year leap cycle
    while (DayFromYear(year) > days) {
        --year;
    }
    while (DayFromYear(year + 1) <= days) {
        ++year;
    }
    int64_t dayOfYear = days - DayFromYear(year);
    month = 0;
    while (dayOfYear >= MonthLength(year, month)) {
        dayOfYear -= MonthLength(year, month);
        ++month;
    }
    day = dayOfYear + 1;
}

// mktime() fails for dates it cannot represent (out of the range of time_t, e.g. beyond
// 2038-01-19 with 32-bit time_t). Recompute with a calendar-equivalent year that mktime()
// can handle, then shift the result back by the exact day difference.
// The fields are normalized with pure 64-bit math first: month/day carry-overs can push the
// date across a year boundary, and the equivalent year must mirror the year the date actually
// falls in — otherwise a leap day present in only one of the two frames shifts the result.
static int64_t MktimeWithEquivalentYear(const tm &localTime)
{
    int64_t year = static_cast<int64_t>(localTime.tm_year) + 1900; // 1900: tm_year base
    int64_t totalMonths = year * MONTHS_PER_YEAR + localTime.tm_mon;
    year = FloorDiv(totalMonths, MONTHS_PER_YEAR);
    int64_t month = totalMonths - year * MONTHS_PER_YEAR;
    int64_t days = DayFromYear(year);
    for (int64_t m = 0; m < month; ++m) {
        days += MonthLength(year, m);
    }
    days += localTime.tm_mday - 1;
    int64_t day = 0;
    CalendarDateFromDays(days, year, month, day);
    int64_t equivalentYear = FindEquivalentYear(year);
    if (equivalentYear < 0) {
        return -1;
    }
    tm equivalentTime = localTime;
    equivalentTime.tm_year = static_cast<int>(equivalentYear - 1900); // 1900: tm_year base
    equivalentTime.tm_mon = static_cast<int>(month);
    equivalentTime.tm_mday = static_cast<int>(day);
    time_t equivalentSeconds = mktime(&equivalentTime);
    if (equivalentSeconds == static_cast<time_t>(-1)) {
        return -1;
    }
    int64_t dayDelta = DayFromYear(year) - DayFromYear(equivalentYear);
    return static_cast<int64_t>(equivalentSeconds) + dayDelta * SECOND_PER_DAY;
}

// localtime_r() only accepts time_t, so seconds outside its range (e.g. beyond 2038 with
// 32-bit time_t) get wrapped to a wrong time. Shift them by whole calendar-equivalent
// years instead; the UTC offset and DST status of the equivalent date are the same.
static int64_t RemapToEquivalentTime(int64_t seconds)
{
    int64_t days = FloorDiv(seconds, SECOND_PER_DAY);
    int64_t year = EPOCH_YEAR + FloorDiv(seconds, 31556952); // 31556952: average seconds per Gregorian year
    while (DayFromYear(year) > days) {
        --year;
    }
    while (DayFromYear(year + 1) <= days) {
        ++year;
    }
    int64_t equivalentYear = FindEquivalentYear(year);
    if (equivalentYear < 0 || equivalentYear == year) {
        return seconds;
    }
    return seconds - (DayFromYear(year) - DayFromYear(equivalentYear)) * SECOND_PER_DAY;
}

int64_t GetLocalOffsetFromOS(int64_t timeMs, bool isLocal)
{
    if (!isLocal) {
        return 0;
    }
    timeMs /= MS_PER_SECOND;
    time_t tv = static_cast<time_t>(timeMs);
    if (static_cast<int64_t>(tv) != timeMs) {
        tv = static_cast<time_t>(RemapToEquivalentTime(timeMs));
    }
    tm localTime {};
    // localtime_r is only suitable for linux.
    tm *t = localtime_r(&tv, &localTime);
    // tm_gmtoff includes any daylight savings offset.
    if (t == nullptr) {
        return 0;
    }

    return static_cast<int64_t>(t->tm_gmtoff) * MS_PER_SECOND;
}

int64_t GetUTCTimestamp(int64_t year, int64_t month, int64_t day, int64_t hour, int64_t minute, int64_t second,
                        int64_t millisecond)
{
    if (hour >= HOUR_PER_DAY) {
        int64_t tmp = hour / HOUR_PER_DAY;
        day += tmp;
        hour -= tmp * HOUR_PER_DAY;
    }
    if (minute >= MINUTE_PER_DAY) {
        int64_t tmp = minute / MINUTE_PER_DAY;
        day += tmp;
        minute -= tmp * MINUTE_PER_DAY;
    }
    if (second >= SECOND_PER_DAY) {
        int64_t tmp = second / SECOND_PER_DAY;
        day += tmp;
        second -= tmp * SECOND_PER_DAY;
    }
    if (millisecond >= MS_PER_DAY) {
        int64_t tmp = millisecond / MS_PER_DAY;
        day += tmp;
        millisecond -= tmp * MS_PER_DAY;
    }
    tm localTime = {
        .tm_sec = second + (millisecond / MS_PER_SECOND),
        .tm_min = minute,
        .tm_hour = hour,
        .tm_mday = day,
        .tm_mon = month,
        .tm_year = year,
        .tm_isdst = -1 // -1: let the system decide whether to use DST.
    };
    tm originalTime = localTime;
    // With 32-bit time_t the product mktime() * MS_PER_SECOND must be computed in 64 bits;
    // the cast (and the int64_t MS_PER_SECOND) keeps it from overflowing.
    int64_t seconds = static_cast<int64_t>(mktime(&localTime));
    if (seconds == -1) {
        seconds = MktimeWithEquivalentYear(originalTime);
    }
    return seconds * MS_PER_SECOND + (millisecond % MS_PER_SECOND);
}

bool IsDst(int64_t timeMs)
{
    timeMs /= MS_PER_SECOND;
    time_t tv = static_cast<time_t>(timeMs);
    if (static_cast<int64_t>(tv) != timeMs) {
        tv = static_cast<time_t>(RemapToEquivalentTime(timeMs));
    }
    struct tm tm {
    };
    struct tm *t = nullptr;
    t = localtime_r(&tv, &tm);
    return (t != nullptr) && t->tm_isdst;
}
}  // namespace panda::ecmascript
