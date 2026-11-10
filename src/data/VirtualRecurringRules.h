// Forecast-frequency validation shared by the desktop UI and engine.
// Periods are ordered from most frequent to least frequent.
#pragma once

#include <string_view>

namespace virtual_recurring
{
enum class Period
{
    Unsupported = 0,
    Daily,
    Weekly,
    Monthly,
    Quarterly,
    Yearly
};

constexpr Period from_key(std::string_view key)
{
    return key == "Daily" ? Period::Daily
        : key == "Weekly" ? Period::Weekly
        : key == "Monthly" ? Period::Monthly
        : key == "Quarterly" ? Period::Quarterly
        : Period::Unsupported;
}

constexpr const char* to_key(Period period)
{
    return period == Period::Daily ? "Daily"
        : period == Period::Weekly ? "Weekly"
        : period == Period::Monthly ? "Monthly"
        : period == Period::Quarterly ? "Quarterly"
        : "";
}

constexpr bool is_supported_nominal(Period nominal)
{
    return nominal == Period::Weekly || nominal == Period::Monthly
        || nominal == Period::Quarterly || nominal == Period::Yearly;
}

constexpr bool is_valid_frequency(Period nominal, Period virtual_frequency)
{
    return is_supported_nominal(nominal)
        && virtual_frequency >= Period::Daily
        && virtual_frequency <= Period::Quarterly
        && virtual_frequency < nominal;
}
} // namespace virtual_recurring
