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

    due = 2;
    FakeRepeat stuck{-1, 0};
    assert(skip_elapsed(due, stuck, 20, next).status == SkipStatus::Invalid);
    assert(due == 2);
}
