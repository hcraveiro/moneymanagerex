#include "src/data/VirtualRecurringAllocation.h"
#include "src/data/VirtualRecurringCalendar.h"
#include "src/data/VirtualRecurringRules.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace virtual_recurring;

static CivilDate parse_date(const std::string& value)
{
    if (value.size() != 10 || value[4] != '-' || value[7] != '-')
        throw std::invalid_argument("Invalid ISO date: " + value);
    return {std::stoi(value.substr(0, 4)), std::stoi(value.substr(5, 2)),
        std::stoi(value.substr(8, 2))};
}

static std::vector<std::string> split(const std::string& line, char delimiter)
{
    std::vector<std::string> values;
    std::istringstream stream(line);
    std::string item;
    while (std::getline(stream, item, delimiter))
        values.push_back(item);
    // Preserve trailing blank activation or expected cells.
    if (!line.empty() && line.back() == delimiter)
        values.emplace_back();
    return values;
}

struct Expected
{
    int day;
    std::int64_t minor_units;
};

static std::vector<Expected> parse_expected(const std::string& cell)
{
    std::vector<Expected> expected;
    if (cell.empty())
        return expected;
    for (const auto& entry : split(cell, ';')) {
        const auto parts = split(entry, ':');
        if (parts.size() != 2)
            throw std::invalid_argument("Invalid contract item: " + entry);
        expected.push_back({day_number(parse_date(parts[0])), std::stoll(parts[1])});
    }
    return expected;
}

static void verify_case(const std::vector<std::string>& cols)
{
    if (cols.size() != 9)
        throw std::runtime_error("Acceptance row must contain 9 columns");
    const auto& name = cols[0];
    const Period frequency = from_key(cols[1]);
    if (frequency == Period::Unsupported)
        throw std::invalid_argument("Unsupported contract frequency");

    const std::int64_t amount = std::stoll(cols[2]);
    const CivilDate previous = parse_date(cols[3]);
    const CivilDate due = parse_date(cols[4]);
    const int previous_day = day_number(previous);
    const int due_day = day_number(due);
    const int today_day = day_number(parse_date(cols[5]));
    const int activation_day = cols[6].empty()
        ? today_day : day_number(parse_date(cols[6]));
    const int horizon_day = day_number(parse_date(cols[7]));
    const auto expected = parse_expected(cols[8]);
    const int cycle_days = due_day - previous_day;
    const int first = first_forecast_day(previous_day, today_day, activation_day);
    const int last = std::min(cycle_days - 1, horizon_day - previous_day - 1);

    const auto calendar_ends = month_bucket_ends(previous, due, frequency);
    const auto bucket_end = [&](int day, int length) {
        switch (frequency) {
        case Period::Daily:
            return day;
        case Period::Weekly:
            return weekly_bucket_end(day, length);
        case Period::Monthly:
        case Period::Quarterly:
            return month_bucket_end(day, calendar_ends);
        default:
            return -1;
        }
    };
    const auto result =
        (frequency == Period::Monthly || frequency == Period::Quarterly)
        ? allocate_equal_calendar_buckets(
            amount, cycle_days, first, last, calendar_ends
        )
        : allocate(amount, cycle_days, first, last, bucket_end);
    if (result.size() != expected.size()) {
        throw std::runtime_error(name + ": allocation count mismatch: got "
            + std::to_string(result.size()) + ", expected "
            + std::to_string(expected.size()));
    }
    for (size_t i = 0; i < result.size(); ++i) {
        const int actual_day = previous_day + result[i].day_offset + 1;
        if (actual_day != expected[i].day
            || result[i].minor_units != expected[i].minor_units) {
            throw std::runtime_error(name + ": mismatch in allocation index "
                + std::to_string(i));
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
        throw std::invalid_argument("Usage: test <acceptance-fixture.csv>");
    std::ifstream file(argv[1]);
    if (!file)
        throw std::runtime_error("Cannot open acceptance contract fixture");
    std::string line;
    if (!std::getline(file, line) ||
        line != "case_id,frequency,amount_minor,previous_due,due,today,activated,horizon,expected")
        throw std::runtime_error("Unexpected acceptance fixture format");

    int count = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#')
            continue;
        verify_case(split(line, ','));
        ++count;
    }
    if (count < 20)
        throw std::runtime_error("Acceptance fixture is incomplete");
    std::cout << "Passed " << count << " shared virtual recurrence cases\n";
}
