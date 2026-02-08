#pragma once

#include "transaction-engine.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <functional>

// ============================================================================
// Transaction Integrity Validator - Phase A.3
//
// Core validation engine for detecting accounting inconsistencies:
//   - Double-entry balance verification across all accounts
//   - Missing transaction gap detection with date-range analysis
//   - Duplicate transaction identification using fuzzy matching
//   - Currency conversion audit with rate source verification
//   - Trial balance validation and automated discrepancy reporting
// ============================================================================

namespace ggnucash {
namespace validation {

// ============================================================================
// Validation Result Types
// ============================================================================

enum class ValidationSeverity {
    PASS,       // Validation passed
    INFO,       // Informational finding
    WARNING,    // Potential issue
    ERROR,      // Definite problem
    CRITICAL    // Critical integrity failure
};

enum class ValidationType {
    DOUBLE_ENTRY_BALANCE,
    TRIAL_BALANCE,
    TRANSACTION_GAP,
    DUPLICATE_DETECTION,
    CURRENCY_CONVERSION,
    HASH_CHAIN_INTEGRITY,
    ACCOUNT_EXISTENCE,
    SEQUENCE_CONTINUITY,
    AMOUNT_REASONABLENESS
};

inline std::string validation_type_to_string(ValidationType type) {
    switch (type) {
        case ValidationType::DOUBLE_ENTRY_BALANCE:  return "DOUBLE_ENTRY_BALANCE";
        case ValidationType::TRIAL_BALANCE:         return "TRIAL_BALANCE";
        case ValidationType::TRANSACTION_GAP:       return "TRANSACTION_GAP";
        case ValidationType::DUPLICATE_DETECTION:   return "DUPLICATE_DETECTION";
        case ValidationType::CURRENCY_CONVERSION:   return "CURRENCY_CONVERSION";
        case ValidationType::HASH_CHAIN_INTEGRITY:  return "HASH_CHAIN_INTEGRITY";
        case ValidationType::ACCOUNT_EXISTENCE:     return "ACCOUNT_EXISTENCE";
        case ValidationType::SEQUENCE_CONTINUITY:   return "SEQUENCE_CONTINUITY";
        case ValidationType::AMOUNT_REASONABLENESS: return "AMOUNT_REASONABLENESS";
        default:                                    return "UNKNOWN";
    }
}

inline std::string severity_to_string(ValidationSeverity severity) {
    switch (severity) {
        case ValidationSeverity::PASS:     return "PASS";
        case ValidationSeverity::INFO:     return "INFO";
        case ValidationSeverity::WARNING:  return "WARNING";
        case ValidationSeverity::ERROR:    return "ERROR";
        case ValidationSeverity::CRITICAL: return "CRITICAL";
        default:                           return "UNKNOWN";
    }
}

// ============================================================================
// Validation Finding - individual issue found during validation
// ============================================================================

struct ValidationFinding {
    std::string finding_id;
    ValidationType type;
    ValidationSeverity severity;
    std::string description;
    std::string transaction_id;         // Related transaction (if applicable)
    std::string account_code;           // Related account (if applicable)
    double expected_value;
    double actual_value;
    std::chrono::system_clock::time_point detected_at;
    std::map<std::string, std::string> context;

    ValidationFinding()
        : type(ValidationType::DOUBLE_ENTRY_BALANCE),
          severity(ValidationSeverity::PASS),
          expected_value(0.0),
          actual_value(0.0) {
        detected_at = std::chrono::system_clock::now();
    }

    ValidationFinding(ValidationType t, ValidationSeverity s,
                      const std::string & desc)
        : type(t), severity(s), description(desc),
          expected_value(0.0), actual_value(0.0) {
        detected_at = std::chrono::system_clock::now();
    }
};

// ============================================================================
// Duplicate Candidate - pair of transactions flagged as potential duplicates
// ============================================================================

struct DuplicateCandidate {
    std::string transaction_id_a;
    std::string transaction_id_b;
    double similarity_score;    // 0.0 to 1.0
    std::string match_reason;   // Description of why they match

    DuplicateCandidate()
        : similarity_score(0.0) {}
};

// ============================================================================
// Transaction Gap - detected gap in transaction sequence
// ============================================================================

struct TransactionGap {
    std::string gap_id;
    std::chrono::system_clock::time_point gap_start;
    std::chrono::system_clock::time_point gap_end;
    std::chrono::hours gap_duration;
    std::string account_code;           // Account affected (if per-account analysis)
    std::string description;

    TransactionGap() : gap_duration(0) {}
};

// ============================================================================
// Trial Balance Entry
// ============================================================================

struct TrialBalanceEntry {
    std::string account_code;
    std::string account_name;
    double debit_balance;
    double credit_balance;

    TrialBalanceEntry()
        : debit_balance(0.0), credit_balance(0.0) {}

    TrialBalanceEntry(const std::string & code, const std::string & name,
                      double debit, double credit)
        : account_code(code), account_name(name),
          debit_balance(debit), credit_balance(credit) {}
};

// ============================================================================
// Validation Report - comprehensive results from a validation run
// ============================================================================

struct ValidationReport {
    std::string report_id;
    std::chrono::system_clock::time_point generated_at;
    std::chrono::milliseconds duration;

    // Summary counts
    uint64_t transactions_checked;
    uint64_t findings_total;
    uint64_t findings_pass;
    uint64_t findings_info;
    uint64_t findings_warning;
    uint64_t findings_error;
    uint64_t findings_critical;

    // Detailed findings
    std::vector<ValidationFinding> findings;

    // Specific results
    std::vector<DuplicateCandidate> duplicates;
    std::vector<TransactionGap> gaps;
    std::vector<TrialBalanceEntry> trial_balance;

    // Trial balance totals
    double trial_balance_total_debits;
    double trial_balance_total_credits;
    bool trial_balance_balanced;

    ValidationReport()
        : duration(0),
          transactions_checked(0),
          findings_total(0),
          findings_pass(0),
          findings_info(0),
          findings_warning(0),
          findings_error(0),
          findings_critical(0),
          trial_balance_total_debits(0.0),
          trial_balance_total_credits(0.0),
          trial_balance_balanced(false) {
        generated_at = std::chrono::system_clock::now();
    }

    bool has_errors() const { return findings_error > 0 || findings_critical > 0; }

    std::string to_string() const;
    std::string to_json() const;
};

// ============================================================================
// Validator Configuration
// ============================================================================

struct ValidatorConfig {
    // Balance tolerance for floating-point comparison
    double balance_tolerance;

    // Duplicate detection threshold (0.0-1.0)
    double duplicate_similarity_threshold;

    // Gap detection: minimum gap duration to flag
    std::chrono::hours min_gap_duration;

    // Amount reasonableness: flag transactions above this amount
    double large_transaction_threshold;

    // Known valid account codes (for account existence checks)
    std::set<std::string> valid_account_codes;

    // Currency conversion tolerance (percentage)
    double currency_conversion_tolerance_pct;

    ValidatorConfig()
        : balance_tolerance(0.01),
          duplicate_similarity_threshold(0.85),
          min_gap_duration(std::chrono::hours(24 * 7)),  // 1 week
          large_transaction_threshold(1000000.0),
          currency_conversion_tolerance_pct(1.0) {}
};

// ============================================================================
// Transaction Integrity Validator
// ============================================================================

class TransactionValidator {
public:
    TransactionValidator();
    explicit TransactionValidator(const ValidatorConfig & config);
    ~TransactionValidator() = default;

    // ---- Configuration ----
    void set_config(const ValidatorConfig & config);
    const ValidatorConfig & get_config() const { return config_; }

    // Register valid account codes for existence checks
    void register_account(const std::string & code, const std::string & name = "");

    // ---- Full Validation ----

    // Run all validations on a set of transactions
    ValidationReport validate_all(const std::vector<Transaction> & transactions) const;

    // ---- Individual Validations ----

    // Verify each transaction has balanced debits and credits
    std::vector<ValidationFinding> validate_double_entry(
        const std::vector<Transaction> & transactions) const;

    // Generate and validate trial balance
    std::vector<TrialBalanceEntry> generate_trial_balance(
        const std::vector<Transaction> & transactions) const;
    ValidationFinding validate_trial_balance(
        const std::vector<TrialBalanceEntry> & trial_balance) const;

    // Detect gaps in transaction sequences
    std::vector<TransactionGap> detect_transaction_gaps(
        const std::vector<Transaction> & transactions) const;

    // Identify potential duplicate transactions
    std::vector<DuplicateCandidate> detect_duplicates(
        const std::vector<Transaction> & transactions) const;

    // Verify hash chain integrity
    std::vector<ValidationFinding> validate_hash_chain(
        const std::vector<Transaction> & transactions) const;

    // Verify all referenced accounts exist
    std::vector<ValidationFinding> validate_account_existence(
        const std::vector<Transaction> & transactions) const;

    // Check for unreasonably large amounts
    std::vector<ValidationFinding> validate_amount_reasonableness(
        const std::vector<Transaction> & transactions) const;

    // ---- Utility ----

    // Calculate similarity between two transactions (0.0-1.0)
    double calculate_transaction_similarity(const Transaction & a, const Transaction & b) const;

    // Parse timestamp string to time_point
    static std::chrono::system_clock::time_point parse_timestamp(const std::string & ts);

private:
    ValidatorConfig config_;
    std::map<std::string, std::string> known_accounts_; // code -> name
    mutable std::mutex validator_mutex_;

    // Internal helpers
    std::string generate_finding_id() const;
    double levenshtein_similarity(const std::string & a, const std::string & b) const;
};

} // namespace validation
} // namespace ggnucash
