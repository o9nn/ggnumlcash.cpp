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
//   - Inter-company transaction reconciliation across legal entities
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
    AMOUNT_REASONABLENESS,
    INTERCOMPANY_RECONCILIATION
};

inline std::string validation_type_to_string(ValidationType type) {
    switch (type) {
        case ValidationType::DOUBLE_ENTRY_BALANCE:       return "DOUBLE_ENTRY_BALANCE";
        case ValidationType::TRIAL_BALANCE:              return "TRIAL_BALANCE";
        case ValidationType::TRANSACTION_GAP:            return "TRANSACTION_GAP";
        case ValidationType::DUPLICATE_DETECTION:        return "DUPLICATE_DETECTION";
        case ValidationType::CURRENCY_CONVERSION:        return "CURRENCY_CONVERSION";
        case ValidationType::HASH_CHAIN_INTEGRITY:       return "HASH_CHAIN_INTEGRITY";
        case ValidationType::ACCOUNT_EXISTENCE:          return "ACCOUNT_EXISTENCE";
        case ValidationType::SEQUENCE_CONTINUITY:        return "SEQUENCE_CONTINUITY";
        case ValidationType::AMOUNT_REASONABLENESS:      return "AMOUNT_REASONABLENESS";
        case ValidationType::INTERCOMPANY_RECONCILIATION: return "INTERCOMPANY_RECONCILIATION";
        default:                                         return "UNKNOWN";
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
// Registered Exchange Rate - reference rate from a verified external source
// ============================================================================

struct RegisteredExchangeRate {
    std::string from_currency;
    std::string to_currency;
    std::string date;           // Rate effective date (YYYY-MM-DD), empty = any
    double      rate;           // Units of to_currency per 1 unit of from_currency
    std::string source;         // Rate provider (e.g. "ECB", "XE", "manual")

    RegisteredExchangeRate() : rate(0.0) {}
};

// ============================================================================
// Currency Conversion Record - implied rate extracted from a transaction
// ============================================================================

struct CurrencyConversionRecord {
    std::string transaction_id;
    std::string entry_account;      // Foreign-currency leg account
    std::string from_currency;      // Foreign currency
    std::string to_currency;        // Home currency (transaction currency)
    double      foreign_amount;     // Amount in from_currency
    double      home_amount;        // Amount in to_currency
    double      implied_rate;       // home_amount / foreign_amount
    std::string rate_source;        // Recorded rate source ("" if none)
    std::string date;               // Transaction date (YYYY-MM-DD)

    CurrencyConversionRecord()
        : foreign_amount(0.0), home_amount(0.0), implied_rate(0.0) {}
};

// ============================================================================
// Intercompany Balance - net position between a pair of legal entities
// ============================================================================

struct IntercompanyBalance {
    std::string entity_a;           // Lexicographically smaller entity id
    std::string entity_b;
    double      net_position;       // Net of (a -> b flows) - (b -> a flows)
    uint64_t    transaction_count;  // Cross-entity transactions for this pair

    IntercompanyBalance() : net_position(0.0), transaction_count(0) {}
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
    std::vector<CurrencyConversionRecord> currency_conversions;
    std::vector<IntercompanyBalance> intercompany_balances;

    // Inter-company totals
    uint64_t intercompany_transactions_checked;
    uint64_t intercompany_unreconciled;

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
          intercompany_transactions_checked(0),
          intercompany_unreconciled(0),
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

    // Entity account-code prefixes for inter-company reconciliation
    // (entity id -> account-code prefix, e.g. "ENT1" matches "ENT1-1000")
    std::map<std::string, std::string> entity_account_prefixes;

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

    // Associate an account with a legal entity for inter-company checks
    void register_entity_account(const std::string & entity_id, const std::string & account_code);

    // Associate all accounts sharing a code prefix with a legal entity
    void register_entity_account_prefix(const std::string & entity_id, const std::string & code_prefix);

    // Register a reference exchange rate from a verified source
    void register_exchange_rate(const std::string & from_currency,
                                const std::string & to_currency,
                                const std::string & date,
                                double rate,
                                const std::string & source);

    // Set the currency of an account (used to derive transaction currencies)
    void register_account_currency(const std::string & account_code, const std::string & currency);

    // Explicitly set the currency of a transaction (overrides account-derived)
    void register_transaction_currency(const std::string & transaction_id, const std::string & currency);

    // Record the rate source used for a transaction's currency conversion
    void register_rate_source(const std::string & transaction_id, const std::string & source);

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

    // Reconcile inter-company transactions across legal entities
    std::vector<ValidationFinding> validate_intercompany(
        const std::vector<Transaction> & transactions) const;

    // Compute net inter-company balance per entity pair
    std::vector<IntercompanyBalance> get_intercompany_balances(
        const std::vector<Transaction> & transactions) const;

    // Audit currency conversions against registered reference rates
    std::vector<ValidationFinding> validate_currency_conversion(
        const std::vector<Transaction> & transactions) const;

    // Extract implied-rate conversion records from multi-currency transactions
    std::vector<CurrencyConversionRecord> extract_currency_conversions(
        const std::vector<Transaction> & transactions) const;

    // ---- Utility ----

    // Look up the entity owning an account ("" if unmapped)
    std::string get_account_entity(const std::string & account_code) const;

    // Look up a registered reference rate for a pair/date (0.0 if none)
    double lookup_reference_rate(const std::string & from_currency,
                                 const std::string & to_currency,
                                 const std::string & date) const;

    // Calculate similarity between two transactions (0.0-1.0)
    double calculate_transaction_similarity(const Transaction & a, const Transaction & b) const;

    // Parse timestamp string to time_point
    static std::chrono::system_clock::time_point parse_timestamp(const std::string & ts);

private:
    ValidatorConfig config_;
    std::map<std::string, std::string> known_accounts_;         // code -> name
    std::map<std::string, std::string> entity_accounts_;        // account code -> entity id
    std::map<std::string, std::string> account_currencies_;     // account code -> currency
    std::map<std::string, std::string> transaction_currencies_; // transaction id -> currency
    std::map<std::string, std::string> rate_sources_;           // transaction id -> rate source
    std::vector<RegisteredExchangeRate> reference_rates_;
    mutable std::mutex validator_mutex_;

    // Internal helpers
    std::string generate_finding_id() const;
    double levenshtein_similarity(const std::string & a, const std::string & b) const;
    std::string get_transaction_currency(const Transaction & tx) const;
    bool has_rate_source(const std::string & transaction_id) const;
    std::string get_rate_source(const std::string & transaction_id) const;
    const RegisteredExchangeRate * find_reference_rate(const std::string & from_currency,
                                                       const std::string & to_currency,
                                                       const std::string & date) const;
    // Effective reference rate oriented from->to, inverting the stored rate
    // when the matching registered entry is in the reverse direction.
    // Returns true (and sets rate/source) when a reference rate is available.
    bool effective_reference_rate(const std::string & from_currency,
                                  const std::string & to_currency,
                                  const std::string & date,
                                  double & rate,
                                  std::string & source) const;
};

} // namespace validation
} // namespace ggnucash
