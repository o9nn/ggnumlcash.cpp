#include "transaction-validator.h"
#include <iostream>
using namespace ggnucash::validation;
int main() {
    // IC matched pair debug
    {
        TransactionValidator v;
        v.register_entity_account("E1", "1900");
        v.register_entity_account("E2", "2900");
        Transaction tx;
        tx.id = "TX-IC-1";
        tx.timestamp = "2025-01-15 10:00:00";
        tx.entries = {
            TransactionEntry("1900", 500.0, 0.0, "E1 receivable"),
            TransactionEntry("4100", 0.0, 500.0, "E1 revenue"),
            TransactionEntry("2900", 500.0, 0.0, "E2 payable reduced"),
            TransactionEntry("5100", 0.0, 500.0, "E2 expense reversal")
        };
        auto f = v.validate_intercompany({tx});
        for (auto & x : f) std::cout << "[" << severity_to_string(x.severity) << "] " << x.description
            << " exp=" << x.expected_value << " act=" << x.actual_value << "\n";
        auto b = v.get_intercompany_balances({tx});
        for (auto & x : b) std::cout << "BAL " << x.entity_a << "/" << x.entity_b << " net=" << x.net_position << " n=" << x.transaction_count << "\n";
    }
    // FX within tolerance debug
    {
        TransactionValidator v;
        v.register_transaction_currency("TX-FX-1", "USD");
        v.register_account_currency("1200", "EUR");
        v.register_exchange_rate("EUR", "USD", "2025-01-15", 1.08, "ECB");
        v.register_rate_source("TX-FX-1", "ECB");
        Transaction tx;
        tx.id = "TX-FX-1";
        tx.timestamp = "2025-01-15 10:00:00";
        tx.entries = {
            TransactionEntry("1200", 1000.0, 0.0, "EUR receivable"),
            TransactionEntry("1101", 0.0, 80.0, "USD cash leg")
        };
        auto recs = v.extract_currency_conversions({tx});
        for (auto & r : recs) std::cout << "REC " << r.from_currency << "->" << r.to_currency
            << " foreign=" << r.foreign_amount << " home=" << r.home_amount
            << " implied=" << r.implied_rate << " date='" << r.date << "' src='" << r.rate_source << "'\n";
        auto f = v.validate_currency_conversion({tx});
        for (auto & x : f) std::cout << "[" << severity_to_string(x.severity) << "] " << x.description << "\n";
    }
    // round trip debug
    {
        TransactionValidator v;
        v.register_transaction_currency("TX-RT-1", "USD");
        v.register_transaction_currency("TX-RT-2", "EUR");
        v.register_account_currency("1200", "EUR");
        v.register_account_currency("1300", "USD");
        Transaction fwd;
        fwd.id = "TX-RT-1";
        fwd.timestamp = "2025-01-15 09:00:00";
        fwd.entries = {
            TransactionEntry("1200", 1000.0, 0.0, "EUR leg"),
            TransactionEntry("1101", 0.0, 100.0, "USD leg")
        };
        Transaction rev;
        rev.id = "TX-RT-2";
        rev.timestamp = "2025-01-15 09:05:00";
        rev.entries = {
            TransactionEntry("1300", 1100.0, 0.0, "USD leg"),
            TransactionEntry("3100", 0.0, 70.0, "EUR leg")
        };
        auto recs = v.extract_currency_conversions({fwd, rev});
        for (auto & r : recs) std::cout << "REC " << r.transaction_id << " " << r.from_currency << "->" << r.to_currency
            << " foreign=" << r.foreign_amount << " home=" << r.home_amount << " implied=" << r.implied_rate << "\n";
        auto f = v.validate_currency_conversion({fwd, rev});
        for (auto & x : f) std::cout << "[" << severity_to_string(x.severity) << "] " << x.description << "\n";
    }
    return 0;
}
