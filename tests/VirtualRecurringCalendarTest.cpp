#include "src/data/VirtualRecurringCalendar.h"
#include "src/data/VirtualRecurringAllocation.h"

#include <cassert>

using namespace virtual_recurring;

static_assert(day_number({1970, 1, 1}) == 0);
static_assert(day_number({2026, 11, 30}) - day_number({2026, 10, 31}) == 30);
static_assert(day_number({2024, 3, 1}) - day_number({2024, 2, 1}) == 29);
static_assert(day_number({2025, 3, 1}) - day_number({2025, 2, 1}) == 28);
static_assert(shift_months_clamped({2024, 3, 31}, -1).day == 29);
static_assert(shift_months_clamped({2025, 3, 31}, -1).day == 28);
static_assert(shift_months_clamped({2026, 12, 30}, -1).month == 11);
static_assert(shift_months_clamped({2026, 1, 1}, -1).year == 2025);

int main()
{
    // Calendar months within a three-month nominal period.
    const CivilDate start{2026, 8, 31};
    const CivilDate due{2026, 11, 30};
    const auto ends = month_bucket_ends(start, due, Period::Monthly);
    assert(ends.size() == 3);
    assert(ends[0] == day_number({2026, 9, 30}) - day_number(start) - 1);
    assert(ends[1] == day_number({2026, 10, 30}) - day_number(start) - 1);
    assert(ends[2] == day_number(due) - day_number(start) - 1);
    assert(month_bucket_end(0, ends) == ends.front());
    assert(month_bucket_end(ends[0] + 1, ends) == ends[1]);

    // Annual cycle divided in calendar quarters, anchored on November 30.
    const CivilDate year_start{2025, 11, 30};
    const auto quarters = month_bucket_ends(year_start, due, Period::Quarterly);
    assert(quarters.size() == 4);
    assert(quarters[0] == day_number({2026, 2, 28}) - day_number(year_start) - 1);
    assert(quarters[3] == day_number(due) - day_number(year_start) - 1);
    const int year_days = day_number(due) - day_number(year_start);
    const auto quarter_forecast = allocate(
        12000, year_days, 0,
        [&](int day, int) { return month_bucket_end(day, quarters); }
    );
    assert(quarter_forecast.size() == 4);
    std::int64_t sum = 0;
    for (const auto& entry : quarter_forecast) {
        sum += entry.minor_units;
        assert(entry.minor_units > 0);
    }
    assert(sum == 12000);

    const auto leap_ends = month_bucket_ends(
        {2023, 12, 31}, {2024, 3, 31}, Period::Monthly
    );
    assert(leap_ends.size() == 3);
    assert(leap_ends[1] == day_number({2024, 2, 29}) - day_number({2023, 12, 31}) - 1);
    assert(month_bucket_ends(start, due, Period::Daily).empty());
    assert(month_bucket_end(1, {}) == -1);
}
