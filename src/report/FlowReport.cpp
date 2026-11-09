/*******************************************************
Copyright (C) 2012 Guan Lisheng (guanlisheng@gmail.com)
Copyright (C) 2017 James Higley
Copyright (C) 2022 Mark Whalley (mark@ipx.co.uk)

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
********************************************************/

#include "FlowReport.h"

#include "base/_defs.h"
#include "util/_util.h"
#include "data/VirtualRecurringAdvance.h"
#include "data/VirtualRecurringAllocation.h"
#include "data/VirtualRecurringCalendar.h"
#include "model/AccountModel.h"
#include "model/SchedModel.h"
#include "model/CurrencyHistoryModel.h"
#include "htmlbuilder.h"
#include "app/mmFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
virtual_recurring::Period nominal_period(const RepeatFreq& repeats)
{
    switch (repeats.id()) {
    case RepeatFreq::e_1_week:
        return virtual_recurring::Period::Weekly;
    case RepeatFreq::e_1_month:
    case RepeatFreq::e_month_last_day:
    case RepeatFreq::e_month_last_business_day:
        return virtual_recurring::Period::Monthly;
    case RepeatFreq::e_3_months:
        return virtual_recurring::Period::Quarterly;
    case RepeatFreq::e_1_year:
        return virtual_recurring::Period::Yearly;
    default:
        return virtual_recurring::Period::Unsupported;
    }
}

virtual_recurring::CivilDate civil_date(mmDate date)
{
    const wxDateTime value = date.dateTime();
    return { value.GetYear(), static_cast<int>(value.GetMonth()) + 1,
        static_cast<int>(value.GetDay()) };
}

int date_number(mmDate date)
{
    return virtual_recurring::day_number(civil_date(date));
}

// Use a complete previous calendar month for nominal payments due on
// the last day of a month. This also handles 28/29-day February and the
// user's November 30 example without losing or adding a day.
mmDate prior_nominal_due(Repeat& repeat, mmDate due)
{
    const auto current = civil_date(due);
    int months = 0;
    if (repeat.m_freq.id() == RepeatFreq::e_1_month)
        months = 1;
    else if (repeat.m_freq.id() == RepeatFreq::e_3_months)
        months = 3;
    else if (repeat.m_freq.id() == RepeatFreq::e_1_year)
        months = 12;

    if (months > 0 && current.day == virtual_recurring::days_in_month(
        current.year, current.month
    )) {
        const auto previous = virtual_recurring::shift_months_clamped(
            { current.year, current.month, 31 }, -months
        );
        const wxDateTime previous_date(
            previous.day,
            static_cast<wxDateTime::Month>(previous.month - 1),
            previous.year
        );
        return mmDate(previous_date);
    }
    return repeat.next_date(due, true);
}
} // namespace

// -- CashFlow base class

FlowReport::FlowReport(const wxString& name) :
    ReportBase(name),
    m_today(wxDateTime::Now().ResetTime())
{
    m_only_active = true;
}

FlowReport::~FlowReport()
{
}

double FlowReport::trueAmount(const TrxData& trx_d)
{
    double amount = 0.0;
    bool isAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
        trx_d.m_account_id
    ) != m_account_id.end();
    bool isToAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
        trx_d.m_to_account_id_n
    ) != m_account_id.end();
    if (!(isAccountFound && isToAccountFound)) {
        const double convRate = CurrencyHistoryModel::instance().get_id_date_rate(
            AccountModel::instance().get_idN_data_n(trx_d.m_account_id)->m_currency_id,
            trx_d.m_date()
        );
        switch (trx_d.m_type.id()) {
        case TrxType::e_withdrawal:
            amount = -trx_d.m_amount * convRate;
            break;
        case TrxType::e_deposit:
            amount = +trx_d.m_amount * convRate;
            break;
        case TrxType::e_transfer:
            if (isAccountFound)
                amount = -trx_d.m_amount * convRate;
            else {
                const double toConvRate = CurrencyHistoryModel::instance().get_id_date_rate(
                    AccountModel::instance().get_idN_data_n(trx_d.m_to_account_id_n)->m_currency_id,
                    trx_d.m_date()
                );
                amount = +trx_d.m_to_amount * toConvRate;
            }
        }
    }
    return amount;
}

void FlowReport::getTransactions()
{
    m_balance = 0.0;
    m_account_id.clear();
    m_forecastVector.clear();

    wxDateTime endOfToday = mmDateRange::getDayEnd(m_today);
    wxString todayString = endOfToday.FormatISOCombined();
    wxDateTime endDate = mmDateRange::getDayEnd(
        m_today.Add(wxDateSpan::Months(getForwardMonths()))
    );

    // Get initial Balance as of today
    for (const auto& account_d : AccountModel::instance().find_data_a(
        AccountCol::WHERE_ACCOUNTTYPE(OP_NE, mmNavigatorList::instance().getInvestmentAccountStr()),
        AccountModel::WHERE_STATUS(OP_NE, AccountStatus(AccountStatus::e_closed))
    )) {
        if (m_account_a &&
            std::find(m_account_a->begin(), m_account_a->end(), account_d.m_name) ==
                m_account_a->end()
        ) {
            continue;
        }

        double convRate = CurrencyHistoryModel::instance().get_id_date_rate(
            account_d.m_currency_id
        );
        m_balance += account_d.m_open_balance * convRate;

        m_account_id.push_back(account_d.m_id);

        for (const auto& trx_d : AccountModel::instance().find_id_trx_aBySN(account_d.m_id)) {
            wxString strDate = trx_d.m_isoDateTime();
            // Do not include asset or stock transfers in income expense calculations.
            if (TrxModel::is_foreignAsTransfer(trx_d) || (strDate > todayString))
                continue;
            m_balance += trx_d.account_flow(account_d.m_id) * convRate;
        }
    }

    // Process all transations posted after today
    for (TrxData& trx_d : TrxModel::instance().find_data_a(
        TrxModel::WHERE_DATE(OP_GT, mmDate(endOfToday)),
        TrxModel::WHERE_DATE(OP_LT, mmDate(endDate)),
        TrxModel::WHERE_IS_VOID(false),
        TrxModel::WHERE_IS_DELETED(false)
    )) {
        bool isAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
            trx_d.m_account_id
        ) != m_account_id.end();
        bool isToAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
            trx_d.m_to_account_id_n
        ) != m_account_id.end();
        if (!isAccountFound && !isToAccountFound)
            continue;
        const auto& tp_a = TrxModel::instance().find_id_tp_a(trx_d.m_id);
        if (tp_a.empty()) {
            trx_d.m_amount = trueAmount(trx_d);
            m_forecastVector.push_back({ trx_d, false });
        }
        else {
            for (const auto& tp_d : tp_a) {
                trx_d.m_category_id_n = tp_d.m_category_id;
                trx_d.m_amount        = tp_d.m_amount;
                trx_d.m_amount        = trueAmount(trx_d);
                m_forecastVector.push_back({ trx_d, false });
            }
        }
    }

    // Gather the recurring transaction list
    for (const auto& sched_d : SchedModel::instance().find_data_a(
        SchedModel::WHERE_IS_VOID(false)
    )) {
        // CHECK: use m_date() instead of m_due_date
        mmDate next_date = sched_d.m_due_date;
        if (!sched_d.is_virtual() && next_date > mmDate(endDate))
            continue;

        bool isAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
            sched_d.m_account_id
        ) != m_account_id.end();
        bool isToAccountFound = std::find(m_account_id.begin(), m_account_id.end(),
            sched_d.m_to_account_id_n
        ) != m_account_id.end();
        if (!isAccountFound && !isToAccountFound)
            continue;

        Repeat repeat = sched_d.m_repeat;

        if (sched_d.is_virtual()) {
            // Never post a transfer or a schedule with invalid virtual settings.
            const auto nominal = nominal_period(repeat.m_freq);
            const auto frequency = virtual_recurring::from_key(
                sched_d.m_virtual_frequency.ToStdString()
            );
            if (sched_d.is_transfer() ||
                !virtual_recurring::is_valid_frequency(nominal, frequency)) {
                wxLogWarning("Skipping unsupported virtual schedule %lld",
                    sched_d.m_id.GetValue());
                continue;
            }

            const AccountData* account = AccountModel::instance().get_idN_data_n(
                sched_d.m_account_id
            );
            if (!account)
                continue;
            const CurrencyData* currency = AccountModel::instance().get_data_currency_p(
                *account
            );
            if (!currency || currency->m_scale.GetValue() <= 0) {
                wxLogWarning("Skipping virtual schedule with invalid currency scale");
                continue;
            }
            const std::int64_t scale = currency->m_scale.GetValue();

            const int today_day = date_number(mmDate(m_today));
            const int activation_day = sched_d.m_virtual_start_date.empty()
                ? today_day : date_number(mmDate(sched_d.m_virtual_start_date));
            const int horizon_day = date_number(mmDate(endDate));
            const auto splits = SchedModel::instance().find_id_qp_a(sched_d.m_id);

            // Work on a local Repeat copy. Catching up past due dates must never
            // mutate Next Occurrence or Payments Left in the database.
            const auto skipped = virtual_recurring::skip_elapsed(
                next_date, repeat, mmDate(m_today),
                [](Repeat& r, mmDate due) { return r.next_date(due); }
            );
            if (skipped.status != virtual_recurring::SkipStatus::Ready)
                continue;

            for (int cycle = 0; cycle < 10000 && repeat.m_num != 0; ++cycle) {
                mmDate previous_due = prior_nominal_due(repeat, next_date);
                const int start_day = date_number(previous_due);
                const int end_day = date_number(next_date);
                const int cycle_days = end_day - start_day;
                if (cycle_days <= 0 || start_day >= horizon_day)
                    break;

                const int first_future = virtual_recurring::first_forecast_day(
                    start_day, today_day, activation_day
                );
                const int last_future = std::min(
                    cycle_days - 1, horizon_day - start_day - 1
                );

                if (last_future >= first_future) {
                    const auto calendar_ends = virtual_recurring::month_bucket_ends(
                        civil_date(previous_due), civil_date(next_date), frequency
                    );
                    const auto bucket_end = [&](int day, int length) {
                        switch (frequency) {
                        case virtual_recurring::Period::Daily:
                            return day;
                        case virtual_recurring::Period::Weekly:
                            return virtual_recurring::weekly_bucket_end(day, length);
                        case virtual_recurring::Period::Monthly:
                        case virtual_recurring::Period::Quarterly:
                            return virtual_recurring::month_bucket_end(day, calendar_ends);
                        default:
                            return -1;
                        }
                    };
                    const auto append_amount = [&](double source_amount, int64 category_id) {
                        const std::int64_t cents = std::llround(source_amount * scale);
                        const auto allocated =
                            (frequency == virtual_recurring::Period::Monthly
                                || frequency == virtual_recurring::Period::Quarterly)
                            ? virtual_recurring::allocate_equal_calendar_buckets(
                                cents, cycle_days, first_future, last_future, calendar_ends
                            )
                            : virtual_recurring::allocate(
                                cents, cycle_days, first_future, last_future, bucket_end
                            );
                        for (const auto& item : allocated) {
                            wxDateTime date = previous_due.dateTime();
                            date.Add(wxDateSpan::Days(item.day_offset + 1));
                            TrxData trx_d;
                            trx_d.m_datetime = mmDateTime(mmDate(date));
                            trx_d.m_type = sched_d.m_type;
                            trx_d.m_account_id = sched_d.m_account_id;
                            trx_d.m_to_account_id_n = sched_d.m_to_account_id_n;
                            trx_d.m_payee_id_n = sched_d.m_payee_id_n;
                            trx_d.m_category_id_n = category_id;
                            trx_d.m_amount = static_cast<double>(item.minor_units) / scale;
                            trx_d.m_to_amount = trx_d.m_amount;
                            trx_d.m_amount = trueAmount(trx_d);
                            m_forecastVector.push_back({ trx_d, true });
                        }
                    };
                    if (splits.empty())
                        append_amount(sched_d.m_amount, sched_d.m_category_id_n);
                    else
                        for (const auto& split : splits)
                            append_amount(split.m_amount, split.m_category_id);
                }

                if (repeat.m_num == 1 || end_day >= horizon_day)
                    break;
                mmDate advanced = repeat.next_date(next_date);
                if (!(advanced > next_date))
                    break;
                next_date = advanced;
                repeat.next_repeat();
            }
            continue;
        }

        // Process all possible recurring transactions for this BD
        while (1) {
            if (next_date > mmDate(endDate))
                break;

            TrxData trx_d;
            trx_d.m_datetime        = mmDateTime(next_date);
            trx_d.m_type            = sched_d.m_type;
            trx_d.m_account_id      = sched_d.m_account_id;
            trx_d.m_to_account_id_n = sched_d.m_to_account_id_n;
            trx_d.m_payee_id_n      = sched_d.m_payee_id_n;
            trx_d.m_amount          = sched_d.m_amount;
            trx_d.m_to_amount       = sched_d.m_to_amount;

            const SchedSplitModel::DataA qp_a = SchedModel::instance().find_id_qp_a(
                sched_d.m_id
            );
            if (!qp_a.empty()) {
                for (const auto& qp_d : qp_a) {
                    trx_d.m_category_id_n = qp_d.m_category_id;
                    trx_d.m_amount        = qp_d.m_amount;
                    trx_d.m_amount        = trueAmount(trx_d);
                    m_forecastVector.push_back({ trx_d, false });
                }
            }
            else {
                trx_d.m_category_id_n = sched_d.m_category_id_n;
                trx_d.m_amount        = trueAmount(trx_d);
                m_forecastVector.push_back({ trx_d, false });
            }

            if (repeat.m_num == 1)
                break;

            next_date = repeat.next_date(next_date);
            repeat.next_repeat();
        }
    }

    // Sort by transaction date
    sort(
        m_forecastVector.begin(), m_forecastVector.end(),
        [](ForecastEntry const& a, ForecastEntry const& b) {
            return a.transaction.m_datetime < b.transaction.m_datetime;
        }
    );
}

wxString FlowReport::getHTMLText_DayOrMonth(bool monthly)
{
    // Grab the data
    getTransactions();

    std::map<wxString, double> dateMap;
    std::map<wxString, double> virtualDateMap;

    // Initialise data
    wxDateTime dt = m_today;
    wxDateTime et = dt;
    et.Add(wxDateSpan::Months(getForwardMonths()));
    if (monthly)
        dt.SetDay(1);

    while(dt.IsEarlierThan(et)) {
        dateMap[dt.FormatISODate()] = 0;
        if (monthly)
            dt.Add(wxDateSpan::Month());
        else
            dt.Add(wxDateSpan::Day());
    }

    // squash the data by month or day
    for (auto& forecast : m_forecastVector) {
        auto& trx_d = forecast.transaction;
        dt = trx_d.m_datetime.dateTime();
        wxString date = dt.FormatISODate();
        if (monthly) {
            date = dt.SetDay(1).FormatISODate();
        }
        dateMap[date] += trx_d.m_amount;
        if (forecast.is_virtual)
            virtualDateMap[date] += trx_d.m_amount;
    }

    // Build the report
    mmHTMLBuilder hb;
    hb.init();
    const wxString& headingStr = wxString::Format(
        wxPLURAL("%1$s (%2$i month)", "%1$s (%2$i months)", getForwardMonths()),
        getTitle(), getForwardMonths()
    );
    hb.addReportHeader(headingStr, 1, false);
    hb.displayFooter(getAccountNames());

    GraphData gd;
    GraphSeries gs;

    double runningBalance = m_balance;
    for (const auto& entry : dateMap) {
        runningBalance += entry.second;
        gs.values.push_back(runningBalance);
        gd.labels.push_back(entry.first);
    }
    gd.series.push_back(gs);

    if (getChartSelection() == 0 && !gd.series.empty())
    {
        gd.type = GraphData::LINE_DATETIME;
        hb.addChart(gd);
    }

    hb.addDivContainer("shadow");
    {
        hb.startTable();
        {
            hb.startThead();
            {
                hb.startTableRow();
                hb.addTableHeaderCell(_t("Date"));
                hb.addTableHeaderCell(_t("Total"), "text-right");
                hb.addTableHeaderCell(_t("Virtual"), "text-right");
                hb.addTableHeaderCell(_t("Difference"), "text-right");
                hb.addTableHeaderCell(_t("Cumulative Difference"), "text-right");
                hb.endTableRow();
            }
            hb.endThead();

            runningBalance = m_balance;
            hb.startTbody();
            {
                int lastRowDate = -1;
                int rowDate;
                bool rowType = false;
                double lastBalance = runningBalance;
                for (const auto& entry : dateMap)
                {
                    dt.ParseDate(entry.first);
                    if (monthly)
                        rowDate = dt.GetYear();
                    else
                        rowDate = dt.GetMonth();
                    if (rowDate != lastRowDate)
                    {
                        lastRowDate = rowDate;
                        rowType = !rowType;
                    }
                    if (rowType)
                        hb.startTableRow();
                    else
                        hb.startAltTableRow();

                    hb.addTableCell(entry.first);
                    runningBalance += entry.second;
                    hb.addMoneyCell(runningBalance);
                    hb.addMoneyCell(virtualDateMap[entry.first]);
                    hb.addMoneyCell(runningBalance - lastBalance);
                    hb.addMoneyCell(runningBalance - m_balance);
                    lastBalance = runningBalance;
                    hb.endTableRow();
                }
            }
            hb.endTbody();
        }
        hb.endTable();
    }
    hb.endDiv();

    hb.end();

    wxLogDebug("======= FlowReport:getHTMLText =======");
    wxLogDebug("%s", hb.getHTMLText());

    return hb.getHTMLText();
}

//--------- Cash Flow - Daily

mmReportCashFlowDaily::mmReportCashFlowDaily()
    : FlowReport(_n("Cash Flow - Daily"))
{
    this->setForwardMonths(12);
    setReportParameters(REPORT_ID::DailyCashFlow);
}

wxString mmReportCashFlowDaily::getHTMLText()
{
    return getHTMLText_DayOrMonth(false);
}

//--------- Cash Flow - Monthly

mmReportCashFlowMonthly::mmReportCashFlowMonthly()
    : FlowReport(_n("Cash Flow - Monthly"))
{
    this->setForwardMonths(120);
    setReportParameters(REPORT_ID::MonthlyCashFlow);
}

wxString mmReportCashFlowMonthly::getHTMLText()
{
    return getHTMLText_DayOrMonth(true);
}

//--------- Cash Flow - Transactions

mmReportCashFlowTransactions::mmReportCashFlowTransactions()
    : FlowReport(_n("Cash Flow - Transactions"))
{
    setReportParameters(REPORT_ID::TransactionsCashFlow);
}

wxString mmReportCashFlowTransactions::getHTMLText()
{
    // Grab the data
    getTransactions();

    // Build the report
    mmHTMLBuilder hb;
    hb.init();
    const wxString& headingStr = wxString::Format(
        wxPLURAL("%1$s (%2$i month)", "%1$s (%2$i months)" , getForwardMonths()),
        getTitle(), getForwardMonths()
    );
    hb.addReportHeader(headingStr, 1, false);
    hb.displayFooter(getAccountNames());

    // Display graph
    GraphData gd;
    GraphSeries gs;

    double runningBalance = m_balance;
    for (const auto& forecast : m_forecastVector) {
        const auto& trx_d = forecast.transaction;
        runningBalance += trx_d.m_amount;
        gs.values.push_back(runningBalance);
        gd.labels.push_back(trx_d.m_isoDateTime());
    }
    gd.series.push_back(gs);

    if (getChartSelection() == 0 && !gd.series.empty()) {
        gd.type = GraphData::LINE_DATETIME;
        hb.addChart(gd);
    }

    // Now display the  transaction detail

    hb.addDivContainer("shadow");
    hb.startTable();
    hb.startThead();
    hb.startTableRow();
    hb.addTableHeaderCell(_t("Date"));
    hb.addTableHeaderCell(_t("Type"));
    hb.addTableHeaderCell(_t("Account"));
    hb.addTableHeaderCell(_t("Payee"));
    hb.addTableHeaderCell(_t("Category"));
    hb.addTableHeaderCell(_t("Amount"), "text-right");
    hb.addTableHeaderCell(_t("Balance"), "text-right");
    hb.addTableHeaderCell(_t("Cumulative Difference"), "text-right");
    hb.endTableRow();
    hb.endThead();
    hb.startTbody();

    int lastRowDate = -1;
    bool rowType = false;
    runningBalance = m_balance;
    for (auto& forecast : m_forecastVector) {
        auto& trx_d = forecast.transaction;
        int rowDate = trx_d.m_datetime.dateTime(false).GetMonth();
        if (rowDate != lastRowDate) {
            lastRowDate = rowDate;
            rowType = !rowType;
        }
        if (rowType)
            hb.startTableRow();
        else
            hb.startAltTableRow();
        hb.addTableCellDate(trx_d.m_isoDateTime());
        hb.addTableCell(forecast.is_virtual ? _t("Virtual") : "");
        hb.addTableCell(AccountModel::instance().get_id_name(trx_d.m_account_id));
        // TOACCOUNTID can be NULL for deposits and withdrawals.
        // Show a destination account only for actual transfers.
        hb.addTableCell((trx_d.m_type.id() != TrxType::e_transfer)
            ? PayeeModel::instance().get_id_name(trx_d.m_payee_id_n)
            : "> " + AccountModel::instance().get_id_name(trx_d.m_to_account_id_n)
        );
        hb.addTableCell(CategoryModel::instance().get_id_fullname(trx_d.m_category_id_n));
        double amount = trx_d.m_amount;
        hb.addMoneyCell(amount);
        runningBalance += amount;
        hb.addMoneyCell(runningBalance);
        hb.addMoneyCell(runningBalance - m_balance);
        hb.endTableRow();
    }

    hb.endTbody();
    hb.endTable();
    hb.endDiv();
    hb.end();

    wxLogDebug("======= mmReportCashFlowTransactions:getHTMLText =======");
    wxLogDebug("%s", hb.getHTMLText());
    return hb.getHTMLText();
}
