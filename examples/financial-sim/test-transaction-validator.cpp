#include "transaction-validator.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cmath>

using namespace ggnucash::validation;

// ============================================================================
// Test Utilities
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct test_register_##name { \
        test_register_##name() { \
            std::cout << "Running: " #name "... "; \
            try { \
                test_##name(); \
                std::cout << "PASSED\n"; \
                tests_passed++; \
            } catch (const std::exception & e) { \
                std::cout << "FAILED: " << e.what() << "\n"; \
                tests_failed++; \
            } catch (...) { \
                std::cout << "FAILED: unknown exception\n"; \
                tests_failed++; \
            } \
        } \
    } test_instance_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond); \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if (cond) throw std::runtime_error("Assertion failed: !(" #cond ")"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) throw std::runtime_error("Assertion failed: " #a " == " #b); \
} while(0)

#define ASSERT_GT(a, b) do { \
    if (!((a) > (b))) throw std::runtime_error("Assertion failed: " #a " > " #b); \
} while(0)

// Helper: create a balanced test transaction
static Transaction make_balanced_tx(const std::string & id, double amount,
                                     const std::string & debit_acct = "1101",
                                     const std::string & credit_acct = "4100") {
    Transaction tx;
    tx.id = id;
    tx.description = "Test transaction " + id;
    tx.timestamp = "2025-01-15 10:00:00";
    tx.entries = {
        TransactionEntry(debit_acct, amount, 0.0, "Debit"),
        TransactionEntry(credit_acct, 0.0, amount, "Credit")
    };
    return tx;
}

// Helper: create an unbalanced transaction
static Transaction make_unbalanced_tx(const std::string & id) {
    Transaction tx;
    tx.id = id;
    tx.description = "Unbalanced transaction " + id;
    tx.timestamp = "2025-01-15 10:00:00";
    tx.entries = {
        TransactionEntry("1101", 1000.0, 0.0, "Debit"),
        TransactionEntry("4100", 0.0, 500.0, "Credit")  // Wrong amount
    };
    return tx;
}

// ============================================================================
// Tests: Double Entry Validation
// ============================================================================

TEST(balanced_transactions_pass) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0),
        make_balanced_tx("TX-2", 200.0),
        make_balanced_tx("TX-3", 300.0)
    };

    auto findings = validator.validate_double_entry(txs);
    ASSERT_EQ(findings.size(), 3u);
    for (const auto & f : findings) {
        ASSERT_EQ(f.severity, ValidationSeverity::PASS);
    }
}

TEST(unbalanced_transaction_detected) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0),
        make_unbalanced_tx("TX-2"),
        make_balanced_tx("TX-3", 300.0)
    };

    auto findings = validator.validate_double_entry(txs);
    ASSERT_EQ(findings.size(), 3u);
    ASSERT_EQ(findings[0].severity, ValidationSeverity::PASS);
    ASSERT_EQ(findings[1].severity, ValidationSeverity::ERROR);
    ASSERT_EQ(findings[2].severity, ValidationSeverity::PASS);
}

// ============================================================================
// Tests: Trial Balance
// ============================================================================

TEST(trial_balance_balanced) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0, "1101", "4100"),
        make_balanced_tx("TX-2", 200.0, "1102", "4200"),
        make_balanced_tx("TX-3", 50.0, "5101", "1101"),
    };

    auto tb = validator.generate_trial_balance(txs);
    ASSERT_GT(tb.size(), 0u);

    auto finding = validator.validate_trial_balance(tb);
    ASSERT_EQ(finding.severity, ValidationSeverity::PASS);
}

TEST(trial_balance_has_correct_accounts) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0, "1101", "4100"),
    };

    auto tb = validator.generate_trial_balance(txs);
    ASSERT_EQ(tb.size(), 2u);

    // Find account 1101 - should have 100.0 debit
    bool found_1101 = false;
    for (const auto & entry : tb) {
        if (entry.account_code == "1101") {
            ASSERT_TRUE(std::abs(entry.debit_balance - 100.0) < 0.01);
            ASSERT_TRUE(std::abs(entry.credit_balance) < 0.01);
            found_1101 = true;
        }
    }
    ASSERT_TRUE(found_1101);
}

// ============================================================================
// Tests: Duplicate Detection
// ============================================================================

TEST(identical_transactions_flagged) {
    TransactionValidator validator;
    ValidatorConfig config;
    config.duplicate_similarity_threshold = 0.80;
    validator.set_config(config);

    Transaction tx1 = make_balanced_tx("TX-1", 1000.0, "1101", "4100");
    tx1.description = "Monthly rent payment";

    Transaction tx2 = make_balanced_tx("TX-2", 1000.0, "1101", "4100");
    tx2.description = "Monthly rent payment";

    std::vector<Transaction> txs = {tx1, tx2};
    auto dups = validator.detect_duplicates(txs);
    ASSERT_GT(dups.size(), 0u);
    ASSERT_GT(dups[0].similarity_score, 0.80);
}

TEST(different_transactions_not_flagged) {
    TransactionValidator validator;

    Transaction tx1 = make_balanced_tx("TX-1", 100.0, "1101", "4100");
    tx1.description = "Office supplies purchase";

    Transaction tx2 = make_balanced_tx("TX-2", 5000.0, "5101", "2101");
    tx2.description = "Monthly salary payment to employees";

    std::vector<Transaction> txs = {tx1, tx2};
    auto dups = validator.detect_duplicates(txs);
    ASSERT_EQ(dups.size(), 0u);
}

TEST(similarity_score_calculation) {
    TransactionValidator validator;

    Transaction tx1 = make_balanced_tx("TX-1", 1000.0, "1101", "4100");
    tx1.description = "Payment for services";

    Transaction tx2 = make_balanced_tx("TX-2", 1000.0, "1101", "4100");
    tx2.description = "Payment for services";

    double similarity = validator.calculate_transaction_similarity(tx1, tx2);
    ASSERT_GT(similarity, 0.8);

    // Completely different transaction
    Transaction tx3 = make_balanced_tx("TX-3", 50.0, "5103", "2101");
    tx3.description = "Utility bill";

    double low_sim = validator.calculate_transaction_similarity(tx1, tx3);
    ASSERT_TRUE(low_sim < 0.5);
}

// ============================================================================
// Tests: Transaction Gaps
// ============================================================================

TEST(detect_large_gap) {
    TransactionValidator validator;
    ValidatorConfig config;
    config.min_gap_duration = std::chrono::hours(24 * 5);  // 5 days
    validator.set_config(config);

    Transaction tx1 = make_balanced_tx("TX-1", 100.0);
    tx1.timestamp = "2025-01-01 10:00:00";

    Transaction tx2 = make_balanced_tx("TX-2", 200.0);
    tx2.timestamp = "2025-01-15 10:00:00";  // 14 days later

    Transaction tx3 = make_balanced_tx("TX-3", 300.0);
    tx3.timestamp = "2025-01-16 10:00:00";  // 1 day later

    std::vector<Transaction> txs = {tx1, tx2, tx3};
    auto gaps = validator.detect_transaction_gaps(txs);
    // Should find exactly 1 gap (between tx1 and tx2)
    ASSERT_EQ(gaps.size(), 1u);
    ASSERT_GT(gaps[0].gap_duration.count(), static_cast<long>(24 * 5));
}

TEST(no_gap_when_continuous) {
    TransactionValidator validator;
    ValidatorConfig config;
    config.min_gap_duration = std::chrono::hours(24 * 7);  // 1 week
    validator.set_config(config);

    Transaction tx1 = make_balanced_tx("TX-1", 100.0);
    tx1.timestamp = "2025-01-01 10:00:00";

    Transaction tx2 = make_balanced_tx("TX-2", 200.0);
    tx2.timestamp = "2025-01-02 10:00:00";

    Transaction tx3 = make_balanced_tx("TX-3", 300.0);
    tx3.timestamp = "2025-01-03 10:00:00";

    std::vector<Transaction> txs = {tx1, tx2, tx3};
    auto gaps = validator.detect_transaction_gaps(txs);
    ASSERT_EQ(gaps.size(), 0u);
}

// ============================================================================
// Tests: Hash Chain Integrity
// ============================================================================

TEST(valid_hash_chain) {
    TransactionValidator validator;

    Transaction tx1 = make_balanced_tx("TX-1", 100.0);
    tx1.prev_hash = "";
    tx1.calculate_hash();

    Transaction tx2 = make_balanced_tx("TX-2", 200.0);
    tx2.prev_hash = tx1.hash;
    tx2.calculate_hash();

    std::vector<Transaction> txs = {tx1, tx2};
    auto findings = validator.validate_hash_chain(txs);

    // All should pass
    for (const auto & f : findings) {
        ASSERT_EQ(f.severity, ValidationSeverity::PASS);
    }
}

TEST(broken_hash_chain) {
    TransactionValidator validator;

    Transaction tx1 = make_balanced_tx("TX-1", 100.0);
    tx1.calculate_hash();

    Transaction tx2 = make_balanced_tx("TX-2", 200.0);
    tx2.prev_hash = "WRONG_HASH_VALUE";  // Intentionally wrong
    tx2.calculate_hash();

    std::vector<Transaction> txs = {tx1, tx2};
    auto findings = validator.validate_hash_chain(txs);

    // Should find a chain break
    bool found_break = false;
    for (const auto & f : findings) {
        if (f.severity == ValidationSeverity::CRITICAL &&
            f.type == ValidationType::HASH_CHAIN_INTEGRITY) {
            found_break = true;
        }
    }
    ASSERT_TRUE(found_break);
}

// ============================================================================
// Tests: Account Existence
// ============================================================================

TEST(known_accounts_pass) {
    TransactionValidator validator;
    validator.register_account("1101", "Cash");
    validator.register_account("4100", "Sales Revenue");

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0, "1101", "4100")
    };

    auto findings = validator.validate_account_existence(txs);
    ASSERT_EQ(findings.size(), 0u);  // No unknown accounts
}

TEST(unknown_accounts_flagged) {
    TransactionValidator validator;
    validator.register_account("1101", "Cash");
    // Not registering 9999

    Transaction tx = make_balanced_tx("TX-1", 100.0, "1101", "9999");
    std::vector<Transaction> txs = {tx};

    auto findings = validator.validate_account_existence(txs);
    ASSERT_EQ(findings.size(), 1u);
    ASSERT_EQ(findings[0].severity, ValidationSeverity::ERROR);
    ASSERT_EQ(findings[0].account_code, "9999");
}

// ============================================================================
// Tests: Amount Reasonableness
// ============================================================================

TEST(large_amount_flagged) {
    TransactionValidator validator;
    ValidatorConfig config;
    config.large_transaction_threshold = 10000.0;
    validator.set_config(config);

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 5000.0),     // Under threshold
        make_balanced_tx("TX-2", 50000.0),    // Over threshold
    };

    auto findings = validator.validate_amount_reasonableness(txs);
    // TX-2 has both debit and credit entries over threshold
    ASSERT_GT(findings.size(), 0u);

    bool found_large = false;
    for (const auto & f : findings) {
        if (f.transaction_id == "TX-2") {
            found_large = true;
            ASSERT_EQ(f.severity, ValidationSeverity::WARNING);
        }
    }
    ASSERT_TRUE(found_large);
}

TEST(negative_amount_flagged) {
    TransactionValidator validator;

    Transaction tx;
    tx.id = "TX-NEG";
    tx.description = "Negative amount test";
    tx.timestamp = "2025-01-15 10:00:00";
    tx.entries = {
        TransactionEntry("1101", -100.0, 0.0, "Negative debit"),
        TransactionEntry("4100", 0.0, -100.0, "Negative credit")
    };

    std::vector<Transaction> txs = {tx};
    auto findings = validator.validate_amount_reasonableness(txs);
    ASSERT_GT(findings.size(), 0u);

    bool found_negative = false;
    for (const auto & f : findings) {
        if (f.type == ValidationType::AMOUNT_REASONABLENESS &&
            f.severity == ValidationSeverity::ERROR) {
            found_negative = true;
        }
    }
    ASSERT_TRUE(found_negative);
}

// ============================================================================
// Tests: Full Validation
// ============================================================================

TEST(validate_all_clean) {
    TransactionValidator validator;
    validator.register_account("1101", "Cash");
    validator.register_account("4100", "Sales Revenue");

    ValidatorConfig config;
    config.min_gap_duration = std::chrono::hours(24 * 30);  // 30 days
    config.large_transaction_threshold = 100000.0;
    validator.set_config(config);
    // Re-register after config set
    validator.register_account("1101", "Cash");
    validator.register_account("4100", "Sales Revenue");

    std::vector<Transaction> txs;
    for (int i = 0; i < 10; i++) {
        Transaction tx = make_balanced_tx("TX-" + std::to_string(i), 100.0 + i);
        tx.timestamp = "2025-01-" + std::string(i + 1 < 10 ? "0" : "")
                       + std::to_string(i + 1) + " 10:00:00";
        txs.push_back(tx);
    }

    auto report = validator.validate_all(txs);
    ASSERT_EQ(report.transactions_checked, 10u);
    ASSERT_TRUE(report.trial_balance_balanced);
    ASSERT_FALSE(report.has_errors());
}

TEST(validate_all_with_issues) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0),
        make_unbalanced_tx("TX-BAD"),
        make_balanced_tx("TX-3", 300.0)
    };

    auto report = validator.validate_all(txs);
    ASSERT_EQ(report.transactions_checked, 3u);
    ASSERT_TRUE(report.has_errors());
    ASSERT_GT(report.findings_error, 0u);
}

TEST(report_to_string) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0),
        make_balanced_tx("TX-2", 200.0)
    };

    auto report = validator.validate_all(txs);
    std::string text = report.to_string();
    ASSERT_FALSE(text.empty());
    ASSERT_TRUE(text.find("VALIDATION REPORT") != std::string::npos);
}

TEST(report_to_json) {
    TransactionValidator validator;

    std::vector<Transaction> txs = {
        make_balanced_tx("TX-1", 100.0)
    };

    auto report = validator.validate_all(txs);
    std::string json = report.to_json();
    ASSERT_FALSE(json.empty());
    ASSERT_TRUE(json.find("validation_report") != std::string::npos);
}

TEST(empty_transaction_list) {
    TransactionValidator validator;

    std::vector<Transaction> txs;
    auto report = validator.validate_all(txs);
    ASSERT_EQ(report.transactions_checked, 0u);
    ASSERT_TRUE(report.trial_balance_balanced);
    ASSERT_FALSE(report.has_errors());
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n=== TRANSACTION INTEGRITY VALIDATOR TEST SUITE ===\n\n";

    // Tests are auto-registered and run via static initialization above.

    std::cout << "\n--- Results ---\n";
    std::cout << "Passed: " << tests_passed << "\n";
    std::cout << "Failed: " << tests_failed << "\n";
    std::cout << "Total:  " << (tests_passed + tests_failed) << "\n";

    if (tests_failed > 0) {
        std::cout << "\nSOME TESTS FAILED!\n";
        return 1;
    }

    std::cout << "\nAll tests passed.\n";
    return 0;
}
