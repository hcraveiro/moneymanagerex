#include "src/data/VirtualRecurringAllocation.h"

#include <cassert>
#include <cstdint>
#include <numeric>
#include <stdexcept>

using virtual_recurring::Allocation;
using virtual_recurring::allocate;
using virtual_recurring::amount_for_day;
using virtual_recurring::weekly_bucket_end;

static_assert(amount_for_day(3000, 30, 0) == 100);
static_assert(amount_for_day(3000, 30, 29) == 100);
static_assert(weekly_bucket_end(0, 30) == 1);
static_assert(weekly_bucket_end(20, 30) == 22);
static_assert(weekly_bucket_end(23, 30) == 29);

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
