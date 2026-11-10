// Calendar boundaries for virtual cash-flow forecasts (C++17, no UI or database).
#pragma once

#include "VirtualRecurringRules.h"

#include <algorithm>
#include <vector>

namespace virtual_recurring
{
struct CivilDate
{
    int year;
    int month;
    int day;
};

constexpr bool is_leap_year(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

constexpr int days_in_month(int year, int month)
{
    return month == 2 ? (is_leap_year(year) ? 29 : 28)
        : month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31;
}

// Proleptic Gregorian civil date to days since 1970-01-01.
constexpr int day_number(CivilDate date)
{
    int year = date.year - (date.month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned month = static_cast<unsigned>(date.month);
    const unsigned day = static_cast<unsigned>(date.day);
    const unsigned day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned year_of_era_day = year_of_era * 365 + year_of_era / 4
        - year_of_era / 100 + day_of_year;
    return era * 146097 + static_cast<int>(year_of_era_day) - 719468;
}

constexpr CivilDate shift_months_clamped(CivilDate date, int months)
{
    const int absolute = date.year * 12 + date.month - 1 + months;
    const int year = (absolute >= 0 ? absolute : absolute - 11) / 12;
    const int month = absolute - year * 12 + 1;
    return { year, month, date.day < days_in_month(year, month)
        ? date.day : days_in_month(year, month) };
}

// Return 0-based indices of the last day of each virtual month/quarter.
// Boundaries are anchored backwards to the nominal due date, not the
// calendar month's first day, and the final bucket ends exactly on due.
inline std::vector<int> month_bucket_ends(
    CivilDate previous_due, CivilDate current_due, Period frequency
)
{
    std::vector<int> ends;
    const int first_day = day_number(previous_due) + 1;
    const int due_day = day_number(current_due);
    if (due_day < first_day || (frequency != Period::Monthly && frequency != Period::Quarterly))
        return ends;

    const int step = frequency == Period::Monthly ? 1 : 3;
    // If the nominal due date is month-end, virtual month boundaries are
    // calendar month-ends too: Nov 30 -> Oct 31 -> Sep 30, rather than
    // Nov 30 -> Oct 30. Each bucket then covers one full calendar month.
    const bool last_day = current_due.day == days_in_month(
        current_due.year, current_due.month
    );
    const CivilDate anchor = last_day
        ? CivilDate{ current_due.year, current_due.month, 31 }
        : current_due;
    for (int months = 0; months <= 15; months += step) {
        const int candidate = day_number(shift_months_clamped(anchor, -months));
        if (candidate < first_day)
            break;
        ends.push_back(candidate - first_day);
    }
    std::reverse(ends.begin(), ends.end());
    return ends;
}

// Every day in the cycle belongs to the next future bucket end.
// The caller can clamp the returned index to the report horizon.
inline int month_bucket_end(int day_offset, const std::vector<int>& ends)
{
    const auto match = std::lower_bound(ends.begin(), ends.end(), day_offset);
    return match == ends.end() ? -1 : *match;
}
} // namespace virtual_recurring
