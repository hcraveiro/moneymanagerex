#include "src/data/VirtualRecurringAdvance.h"

#include <cassert>

using namespace virtual_recurring;

struct FakeRepeat
{
    int m_num;
    int interval;

    void next_repeat()
    {
        if (m_num > 0)
            --m_num;
    }
};

int main()
{
    const auto next = [](FakeRepeat& repeat, int due) {
        return due + repeat.interval;
    };

    int due = 2;
    FakeRepeat limited{5, 5};
    const auto first = skip_elapsed(due, limited, 20, next);
    assert(first.status == SkipStatus::Ready);
    assert(first.skipped == 4);
    assert(due == 22);
    assert(limited.m_num == 1);

    due = 2;
    FakeRepeat expired{5, 5};
    const auto exhausted = skip_elapsed(due, expired, 22, next);
    assert(exhausted.status == SkipStatus::Exhausted);
    assert(exhausted.skipped == 4);
    assert(expired.m_num == 1);

    due = 2;
    FakeRepeat unlimited{-1, 5};
    const auto forever = skip_elapsed(due, unlimited, 20, next);
    assert(forever.status == SkipStatus::Ready);
    assert(forever.skipped == 4);
    assert(unlimited.m_num == -1);

    due = 23;
    FakeRepeat future{2, 5};
    const auto unchanged = skip_elapsed(due, future, 20, next);
    assert(unchanged.status == SkipStatus::Ready);
    assert(unchanged.skipped == 0);
    assert(future.m_num == 2);

    due = 2;
    FakeRepeat zero{0, 5};
    assert(skip_elapsed(due, zero, 20, next).status == SkipStatus::Exhausted);

    // Today is never part of the future forecast. Advance to a later due.
    due = 20;
    FakeRepeat due_today{2, 10};
    const auto advanced_today = skip_elapsed(due, due_today, 20, next);
    assert(advanced_today.status == SkipStatus::Ready);
    assert(advanced_today.skipped == 1);
    assert(due == 30);
    assert(due_today.m_num == 1);

    // The final nominal occurrence is exhausted when it is due today.
    due = 20;
    FakeRepeat final_today{1, 10};
    const auto exhausted_today = skip_elapsed(due, final_today, 20, next);
    assert(exhausted_today.status == SkipStatus::Exhausted);
    assert(exhausted_today.skipped == 0);
    assert(due == 20);
    assert(final_today.m_num == 1);

    // A remaining finite payment after today must never be consumed early.
    due = 21;
    FakeRepeat final_future{1, 10};
    const auto ready_final = skip_elapsed(due, final_future, 20, next);
    assert(ready_final.status == SkipStatus::Ready);
    assert(ready_final.skipped == 0);
    assert(final_future.m_num == 1);

    // Prevent unbounded simulation for corrupt or extremely old schedules.
    due = -20000;
    FakeRepeat very_old{-1, 1};
    const auto capped = skip_elapsed(due, very_old, 0, next);
    assert(capped.status == SkipStatus::Invalid);
    assert(capped.skipped == 10000);
    assert(due == -10000);

    due = 2;
    FakeRepeat stuck{-1, 0};
    assert(skip_elapsed(due, stuck, 20, next).status == SkipStatus::Invalid);
    assert(due == 2);
}
