// Exact minor-unit allocations for virtual recurring cash-flow forecasts.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <vector>

namespace virtual_recurring
{
struct Allocation
{
    int day_offset;              // Zero-based day when the forecast is displayed.
    std::int64_t minor_units;    // Signed amount in the account currency's minor units.
};

// Distribute all remainder units across the period instead of using a
// large correction at its end. Integer division truncates toward zero,
// preserving the same behavior for withdrawals and deposits.
constexpr std::int64_t amount_for_day(
    std::int64_t total_minor_units, int cycle_days, int day_offset
)
{
    return cycle_days <= 0 || day_offset < 0 || day_offset >= cycle_days
        ? 0
        : total_minor_units / cycle_days
        + ((day_offset + 1) * (total_minor_units % cycle_days)) / cycle_days
        - (day_offset * (total_minor_units % cycle_days)) / cycle_days;
}

// End of the seven-day block measured backward from the nominal due date.
// For a 30-day period, block endpoints are days 2, 9, 16, 23 and 30.
constexpr int weekly_bucket_end(int day_offset, int cycle_days)
{
    return cycle_days - 1 - (cycle_days - 1 - day_offset) / 7 * 7;
}

// bucket_end determines where each future day's allocation appears in the
// report. For daily forecasts use day_offset, for weekly use weekly_bucket_end,
// and for calendar months/quarters the caller supplies calendar-aware ends.
// first_future_day excludes today and all earlier days.
template<typename BucketEnd>
std::vector<Allocation> allocate(
    std::int64_t total_minor_units,
    int cycle_days,
    int first_future_day,
    int last_future_day,
    BucketEnd bucket_end
)
{
    std::vector<Allocation> result;
    if (cycle_days <= 0 || first_future_day >= cycle_days || last_future_day < 0)
        return result;

    std::map<int, std::int64_t> totals;
    const int start = first_future_day < 0 ? 0 : first_future_day;
    const int last = last_future_day < cycle_days ? last_future_day : cycle_days - 1;
    for (int day = start; day <= last; ++day) {
        const int end = bucket_end(day, cycle_days);
        if (end < day || end >= cycle_days)
            throw std::invalid_argument("Forecast bucket must end inside the cycle");
        totals[std::min(end, last)] += amount_for_day(total_minor_units, cycle_days, day);
    }

    for (const auto& entry : totals) {
        if (entry.second != 0)
            result.push_back({ entry.first, entry.second });
    }
    return result;
}

// Entire remaining nominal cycle (no explicit report-horizon clipping).
template<typename BucketEnd>
std::vector<Allocation> allocate(
    std::int64_t total_minor_units,
    int cycle_days,
    int first_future_day,
    BucketEnd bucket_end
)
{
    return allocate(
        total_minor_units, cycle_days, first_future_day, cycle_days - 1, bucket_end
    );
}

} // namespace virtual_recurring
