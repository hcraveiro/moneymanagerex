#include "src/data/VirtualRecurringAllocation.h"

#include <cassert>
#include <cstdint>
#include <numeric>
#include <stdexcept>

using virtual_recurring::Allocation;
using virtual_recurring::allocate;
using virtual_recurring::first_forecast_day;
using virtual_recurring::amount_for_day;
using virtual_recurring::weekly_bucket_end;

static_assert(amount_for_day(3000, 30, 0) == 100);
static_assert(amount_for_day(3000, 30, 29) == 100);
static_assert(weekly_bucket_end(0, 30) == 1);
static_assert(weekly_bucket_end(20, 30) == 22);
static_assert(weekly_bucket_end(23, 30) == 29);
static_assert(first_forecast_day(0, 20, 10) == 20);
static_assert(first_forecast_day(0, 20, 25) == 25);
static_assert(first_forecast_day(0, 20, 20) == 20);
static_assert(first_forecast_day(25, 20, 10) == 0);

std::int64_t sum(const std::vector<Allocation>& entries)
{
    std::int64_t result = 0;
    for (const auto& allocation : entries)
        result += allocation.minor_units;
    return result;
}

int main()
{
    const auto daily = [](int day, int) { return day; };
    const auto weekly = [](int day, int length) {
        return weekly_bucket_end(day, length);
    };

    // A 30-euro period due November 30 covers November 1 through 30.
    const auto full_weekly = allocate(3000, 30, 0, weekly);
    assert(full_weekly.size() == 5);
    const int expected_days[] = { 1, 8, 15, 22, 29 };
    const std::int64_t expected_cents[] = { 200, 700, 700, 700, 700 };
    for (size_t i = 0; i < full_weekly.size(); ++i) {
        assert(full_weekly[i].day_offset == expected_days[i]);
        assert(full_weekly[i].minor_units == expected_cents[i]);
    }
    assert(sum(full_weekly) == 3000);

    // November 20: only November 21-30 is forecast, totaling 10 euros.
    const auto remaining_weekly = allocate(3000, 30, 20, weekly);
    assert(remaining_weekly.size() == 2);
    assert(remaining_weekly[0].day_offset == 22);
    assert(remaining_weekly[0].minor_units == 300);
    assert(remaining_weekly[1].day_offset == 29);
    assert(remaining_weekly[1].minor_units == 700);
    assert(sum(remaining_weekly) == 1000);

    // Weekly buckets are stable and anchored backwards from November 30.
    // On Nov 9: 21 future days => EUR 7 on Nov 16, 23 and 30.
    const auto november_nine = allocate(3000, 30, 9, weekly);
    assert(november_nine.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        assert(november_nine[i].day_offset == 15 + static_cast<int>(i) * 7);
        assert(november_nine[i].minor_units == 700);
    }

    // On Nov 8: Nov 9 carries EUR 1, then three EUR 7 installments.
    const auto november_eight = allocate(3000, 30, 8, weekly);
    assert(november_eight.size() == 4);
    assert(november_eight[0].day_offset == 8);
    assert(november_eight[0].minor_units == 100);
    for (size_t i = 1; i < 4; ++i) {
        assert(november_eight[i].day_offset == 8 + static_cast<int>(i) * 7);
        assert(november_eight[i].minor_units == 700);
    }

    // Activated on November 25: only November 26-30 must be forecast.
    const int activated_mid_cycle = first_forecast_day(0, 20, 25);
    const auto activated_daily = allocate(3000, 30, activated_mid_cycle, daily);
    assert(activated_daily.size() == 5);
    assert(sum(activated_daily) == 500);

    // Updating the amount uses the same activation cut-off and changes only
    // the remaining future portion, without restarting the cycle.
    const auto amount_updated = allocate(6000, 30, activated_mid_cycle, daily);
    assert(sum(amount_updated) == 1000);

    const auto remaining_daily = allocate(3000, 30, 20, daily);
    assert(remaining_daily.size() == 10);
    assert(sum(remaining_daily) == 1000);

    // Residual cents must be spread throughout the cycle, not added at the end.
    for (int days : { 28, 29, 30, 31, 90, 91, 92, 365, 366 }) {
        for (std::int64_t cents : { 1LL, 99LL, 3001LL, -3001LL }) {
            const auto full = allocate(cents, days, 0, daily);
            assert(sum(full) == cents);
            for (int offset = 0; offset < days; ++offset) {
                const std::int64_t share = amount_for_day(cents, days, offset);
                assert(share >= -108 && share <= 108);
                assert(share == amount_for_day(cents, days, offset));
            }
        }
    }

    // Calendar-aware aggregation can provide its own bucket endpoints.
    const auto calendar = [](int day, int) {
        return day < 10 ? 9 : day < 20 ? 19 : 29;
    };
    assert(sum(allocate(3000, 30, 20, calendar)) == 1000);

    // A report ending midweek must never count future days outside its horizon.
    const auto partial_window = allocate(3000, 30, 20, 24, weekly);
    assert(partial_window.size() == 2);
    assert(partial_window[0].day_offset == 22);
    assert(partial_window[0].minor_units == 300);
    assert(partial_window[1].day_offset == 24);
    assert(partial_window[1].minor_units == 200);
    assert(sum(partial_window) == 500);

    assert(allocate(1000, 30, 30, daily).empty());
    assert(allocate(1000, 0, 0, daily).empty());

    bool invalid_bucket_detected = false;
    try {
        allocate(1000, 30, 0, [](int day, int) { return day - 1; });
    }
    catch (const std::invalid_argument&) {
        invalid_bucket_detected = true;
    }
    assert(invalid_bucket_detected);
}
