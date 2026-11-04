/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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
#include "ecmascript/tests/test_helper.h"

#include "gtest/gtest.h"
#include <cstdlib>
#include <ctime>
#include <string>

namespace panda::test {
using namespace panda::ecmascript;

namespace {
constexpr int64_t TM_YEAR_BASE = 1900;     // tm_year/GetUTCTimestamp year parameter base
constexpr int64_t EPOCH_YEAR = 1970;
constexpr int64_t MS_PER_SECOND = 1000;
constexpr int64_t MS_PER_MINUTE = 60 * MS_PER_SECOND;
constexpr int64_t MS_PER_HOUR = 60 * MS_PER_MINUTE;
constexpr int64_t MS_PER_DAY = 24 * MS_PER_HOUR;
constexpr int64_t DAYS_PER_COMMON_YEAR = 365;
constexpr int64_t LEAP_YEAR_INTERVAL = 4;
constexpr int64_t CENTURY_INTERVAL = 100;
constexpr int64_t GREGORIAN_CYCLE = 400;
constexpr int64_t LEAP_BASE_YEAR = 1969;
constexpr int64_t CENTURY_BASE_YEAR = 1901;
constexpr int64_t CYCLE_BASE_YEAR = 1601;
constexpr int64_t FEBRUARY_INDEX = 1;      // 0-based month index of February
constexpr int64_t MONTHS_PER_YEAR = 12;
// US DST rules since 2007 in POSIX form, so the test does not depend on host tzdata files
// and the rules apply uniformly to every year: UTC-5 standard, UTC-4 daylight.
constexpr char NY_POSIX_TZ[] = "EST5EDT,M3.2.0/2:00,M11.1.0/2:00";
constexpr int64_t NY_STANDARD_OFFSET_HOURS = -5;
constexpr int64_t NY_DAYLIGHT_OFFSET_HOURS = -4;
// Days per month, February holds the common-year value; leap years add one day.
constexpr int64_t MONTH_DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

struct DateTimeFields {
    int64_t year;          // full year, e.g. 2039
    int64_t month;         // 0-based
    int64_t day;           // 1-based
    int64_t hour = 0;
    int64_t minute = 0;
    int64_t second = 0;
    int64_t millisecond = 0;
};

int64_t FloorDivRef(int64_t dividend, int64_t divisor)
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

// ECMA-262 DayFromYear over full years (e.g. 2039).
int64_t DayFromYearRef(int64_t year)
{
    return DAYS_PER_COMMON_YEAR * (year - EPOCH_YEAR) + FloorDivRef(year - LEAP_BASE_YEAR, LEAP_YEAR_INTERVAL) -
           FloorDivRef(year - CENTURY_BASE_YEAR, CENTURY_INTERVAL) +
           FloorDivRef(year - CYCLE_BASE_YEAR, GREGORIAN_CYCLE);
}

bool IsLeapYearRef(int64_t year)
{
    return (year % LEAP_YEAR_INTERVAL == 0 && year % CENTURY_INTERVAL != 0) || year % GREGORIAN_CYCLE == 0;
}

// UTC epoch milliseconds of a date-time read as if the local zone were UTC. The expectations
// in these tests combine this pure ECMA day math with fixed POSIX-rule zone offsets, so they
// hold on both 64-bit and 32-bit time_t hosts.
int64_t UtcMsRef(const DateTimeFields &fields)
{
    int64_t days = DayFromYearRef(fields.year) + fields.day - 1;
    for (int64_t m = 0; m < fields.month && m < MONTHS_PER_YEAR; ++m) {
        days += MONTH_DAYS[m];
    }
    if (fields.month > FEBRUARY_INDEX && IsLeapYearRef(fields.year)) {
        ++days;
    }
    return days * MS_PER_DAY + fields.hour * MS_PER_HOUR + fields.minute * MS_PER_MINUTE +
           fields.second * MS_PER_SECOND + fields.millisecond;
}
} // namespace

class TimeTest : public BaseTestWithOutScope {
public:
    void SetUp() override
    {
        const char *tz = std::getenv("TZ");
        savedTzValid_ = (tz != nullptr);
        if (savedTzValid_) {
            savedTz_ = tz;
        }
    }

    void TearDown() override
    {
        if (savedTzValid_) {
            setenv("TZ", savedTz_.c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }

    void SetTimezone(const char *tz)
    {
        setenv("TZ", tz, 1);
        tzset();
    }

private:
    std::string savedTz_ {};
    bool savedTzValid_ = false;
};

HWTEST_F_L0(TimeTest, GetUTCTimestamp_Epoch_Utc)
{
    SetTimezone("UTC");
    EXPECT_EQ(GetUTCTimestamp(EPOCH_YEAR - TM_YEAR_BASE, 0, 1, 0, 0, 0, 0), 0);
}

HWTEST_F_L0(TimeTest, GetUTCTimestamp_InRangeDate_Utc)
{
    SetTimezone("UTC");
    EXPECT_EQ(GetUTCTimestamp(2024 - TM_YEAR_BASE, 0, 1, 0, 0, 0, 0), UtcMsRef({2024, 0, 1}));
    EXPECT_EQ(GetUTCTimestamp(2024 - TM_YEAR_BASE, 5, 15, 12, 34, 56, 789),
              UtcMsRef({2024, 5, 15, 12, 34, 56, 789}));
}

// Beyond 2038-01-19 a 32-bit time_t cannot represent the date and mktime() fails. The
// implementation must still return the correct timestamp instead of a negative error value.
HWTEST_F_L0(TimeTest, GetUTCTimestamp_Beyond32BitTimeT_Utc)
{
    SetTimezone("UTC");
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 0, 1, 0, 0, 0, 0), UtcMsRef({2039, 0, 1}));
    EXPECT_EQ(GetUTCTimestamp(2040 - TM_YEAR_BASE, 1, 29, 0, 0, 0, 0), UtcMsRef({2040, 1, 29}));
    EXPECT_EQ(GetUTCTimestamp(2100 - TM_YEAR_BASE, 11, 31, 23, 59, 59, 999),
              UtcMsRef({2100, 11, 31, 23, 59, 59, 999}));
}

HWTEST_F_L0(TimeTest, GetUTCTimestamp_PreEpochDate_Utc)
{
    SetTimezone("UTC");
    EXPECT_EQ(GetUTCTimestamp(1969 - TM_YEAR_BASE, 11, 31, 0, 0, 0, 0), UtcMsRef({1969, 11, 31}));
    EXPECT_EQ(UtcMsRef({1969, 11, 31}), -MS_PER_DAY);
}

HWTEST_F_L0(TimeTest, GetUTCTimestamp_NormalizesOverflowingFields_Utc)
{
    SetTimezone("UTC");
    // 25:30 on Jan 1st carries into 01:30 on Jan 2nd
    EXPECT_EQ(GetUTCTimestamp(2024 - TM_YEAR_BASE, 0, 1, 25, 30, 0, 0), UtcMsRef({2024, 0, 2, 1, 30}));
    // 1500 ms carry one second
    EXPECT_EQ(GetUTCTimestamp(2024 - TM_YEAR_BASE, 0, 1, 0, 0, 0, 1500), UtcMsRef({2024, 0, 1, 0, 0, 1, 500}));
}

// Month/day carry-overs can push the date across a year boundary; the equivalent-year
// fallback must stay exact even when the carried date lands on a leap year.
HWTEST_F_L0(TimeTest, GetUTCTimestamp_CarryAcrossYearBoundary_Utc)
{
    SetTimezone("UTC");
    // month 15 carries into April of the next year; 2040 is a leap year
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 15, 1, 0, 0, 0, 0), UtcMsRef({2040, 3, 1}));
    // negative months carry backwards across the boundary
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, -3, 1, 0, 0, 0, 0), UtcMsRef({2038, 9, 1}));
    // extreme day carry must neither fail nor drift
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 0, 1000001, 0, 0, 0, 0),
              UtcMsRef({2039, 0, 1}) + 1000000 * MS_PER_DAY); // 1000000: days carried forward
}

// A carry across the year boundary must also keep the correct DST offset of the carried date.
HWTEST_F_L0(TimeTest, GetUTCTimestamp_CarryAcrossYearBoundary_KeepsDstOffset)
{
    SetTimezone(NY_POSIX_TZ);
    // month 15 carries into April 2040 (daylight time): local 12:00 is 16:00 UTC
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 15, 1, 12, 0, 0, 0),
              UtcMsRef({2040, 3, 1, 12}) - NY_DAYLIGHT_OFFSET_HOURS * MS_PER_HOUR);
    // month -4 carries into September 2038 (daylight time as well)
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, -4, 1, 12, 0, 0, 0),
              UtcMsRef({2038, 8, 1, 12}) - NY_DAYLIGHT_OFFSET_HOURS * MS_PER_HOUR);
}

// mktime() resolves the local zone offset itself; dates beyond 32-bit time_t must get the same
// offset as a present-day date in the same DST season.
HWTEST_F_L0(TimeTest, GetUTCTimestamp_Beyond32BitTimeT_KeepsDstOffset)
{
    SetTimezone(NY_POSIX_TZ);
    int64_t est = NY_STANDARD_OFFSET_HOURS * MS_PER_HOUR;
    int64_t edt = NY_DAYLIGHT_OFFSET_HOURS * MS_PER_HOUR;
    // January is standard time: local 12:00 is 17:00 UTC
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0), UtcMsRef({2039, 0, 15, 12}) - est);
    // July is daylight time: local 12:00 is 16:00 UTC
    EXPECT_EQ(GetUTCTimestamp(2039 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0), UtcMsRef({2039, 6, 1, 12}) - edt);
    // Same expectations for present-day dates, as a baseline
    EXPECT_EQ(GetUTCTimestamp(2026 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0), UtcMsRef({2026, 0, 15, 12}) - est);
    EXPECT_EQ(GetUTCTimestamp(2026 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0), UtcMsRef({2026, 6, 1, 12}) - edt);
}

HWTEST_F_L0(TimeTest, GetLocalOffsetFromOS_NotLocal_ReturnsZero)
{
    SetTimezone(NY_POSIX_TZ);
    EXPECT_EQ(GetLocalOffsetFromOS(0, false), 0);
    EXPECT_EQ(GetLocalOffsetFromOS(UtcMsRef({2039, 6, 1, 12}), false), 0);
}

// On 32-bit time_t the raw timeMs value wraps before localtime_r() sees it; the offset must
// still match the offset of a present-day date in the same DST season.
HWTEST_F_L0(TimeTest, GetLocalOffsetFromOS_Beyond32BitTimeT_MatchesSeason)
{
    SetTimezone(NY_POSIX_TZ);
    int64_t summer2026 = GetUTCTimestamp(2026 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0);
    int64_t winter2026 = GetUTCTimestamp(2026 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0);
    int64_t summer2039 = GetUTCTimestamp(2039 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0);
    int64_t winter2039 = GetUTCTimestamp(2039 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0);
    EXPECT_EQ(GetLocalOffsetFromOS(summer2026, true), NY_DAYLIGHT_OFFSET_HOURS * MS_PER_HOUR);
    EXPECT_EQ(GetLocalOffsetFromOS(winter2026, true), NY_STANDARD_OFFSET_HOURS * MS_PER_HOUR);
    EXPECT_EQ(GetLocalOffsetFromOS(summer2039, true), GetLocalOffsetFromOS(summer2026, true));
    EXPECT_EQ(GetLocalOffsetFromOS(winter2039, true), GetLocalOffsetFromOS(winter2026, true));
}

HWTEST_F_L0(TimeTest, IsDst_Beyond32BitTimeT_MatchesSeason)
{
    SetTimezone(NY_POSIX_TZ);
    int64_t summer2026 = GetUTCTimestamp(2026 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0);
    int64_t winter2026 = GetUTCTimestamp(2026 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0);
    int64_t summer2039 = GetUTCTimestamp(2039 - TM_YEAR_BASE, 6, 1, 12, 0, 0, 0);
    int64_t winter2039 = GetUTCTimestamp(2039 - TM_YEAR_BASE, 0, 15, 12, 0, 0, 0);
    EXPECT_TRUE(IsDst(summer2026));
    EXPECT_FALSE(IsDst(winter2026));
    EXPECT_EQ(IsDst(summer2039), IsDst(summer2026));
    EXPECT_EQ(IsDst(winter2039), IsDst(winter2026));
}

HWTEST_F_L0(TimeTest, IsDst_Utc_AlwaysFalse)
{
    SetTimezone("UTC");
    EXPECT_FALSE(IsDst(0));
    EXPECT_FALSE(IsDst(UtcMsRef({2039, 6, 1, 12})));
}
} // namespace panda::test
