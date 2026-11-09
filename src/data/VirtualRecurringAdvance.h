// In-memory advancement of expired nominal occurrences.
#pragma once

namespace virtual_recurring
{
enum class SkipStatus
{
    Ready,
    Exhausted,
    Invalid
};

struct SkipResult
{
    SkipStatus status;
    int skipped;
};

// Move to the first nominal due date strictly later than today, consuming
// Payments Left without writing database rows or executing a transaction.
// A finite schedule with no future payments is exhausted, not renewed.
// The callback delegates recurrence semantics to the existing Repeat model.
template<typename Date, typename Schedule, typename NextDate>
SkipResult skip_elapsed(
    Date& next_due,
    Schedule& repeat,
    const Date& today,
    NextDate advance
)
{
    if (repeat.m_num == 0)
        return { SkipStatus::Exhausted, 0 };

    for (int count = 0; count < 10000; ++count) {
        if (next_due > today)
            return { SkipStatus::Ready, count };
        if (repeat.m_num == 1)
            return { SkipStatus::Exhausted, count };

        const Date candidate = advance(repeat, next_due);
        if (!(candidate > next_due))
            return { SkipStatus::Invalid, count };

        next_due = candidate;
        repeat.next_repeat();
    }
    return { SkipStatus::Invalid, 10000 };
}
} // namespace virtual_recurring
