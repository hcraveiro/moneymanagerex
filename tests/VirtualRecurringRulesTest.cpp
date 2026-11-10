#include "src/data/VirtualRecurringRules.h"

#include <cassert>

using virtual_recurring::Period;
using virtual_recurring::from_key;
using virtual_recurring::is_supported_nominal;
using virtual_recurring::is_valid_frequency;
using virtual_recurring::to_key;

static_assert(!is_valid_frequency(Period::Unsupported, Period::Daily));
static_assert(!is_valid_frequency(Period::Daily, Period::Daily));
static_assert(is_valid_frequency(Period::Weekly, Period::Daily));
static_assert(!is_valid_frequency(Period::Weekly, Period::Weekly));
static_assert(is_valid_frequency(Period::Monthly, Period::Daily));
static_assert(is_valid_frequency(Period::Monthly, Period::Weekly));
static_assert(!is_valid_frequency(Period::Monthly, Period::Monthly));
static_assert(is_valid_frequency(Period::Quarterly, Period::Monthly));
static_assert(!is_valid_frequency(Period::Quarterly, Period::Quarterly));
static_assert(is_valid_frequency(Period::Yearly, Period::Quarterly));
static_assert(!is_valid_frequency(Period::Yearly, Period::Yearly));
static_assert(!is_valid_frequency(Period::Yearly, Period::Unsupported));

int main()
{
    const Period nominal[] = {
        Period::Unsupported, Period::Daily, Period::Weekly,
        Period::Monthly, Period::Quarterly, Period::Yearly
    };
    const Period virtual_frequencies[] = {
        Period::Daily, Period::Weekly, Period::Monthly, Period::Quarterly
    };
    for (Period n : nominal) {
        for (Period v : virtual_frequencies) {
            const bool expected = is_supported_nominal(n) && v < n;
            assert(is_valid_frequency(n, v) == expected);
            assert(from_key(to_key(v)) == v);
        }
    }
    assert(from_key("Yearly") == Period::Unsupported);
    assert(from_key("Invalid") == Period::Unsupported);
    assert(from_key("") == Period::Unsupported);
}
