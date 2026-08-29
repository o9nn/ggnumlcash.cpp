#pragma once

#include "audit-trail.h"
#include "transaction-engine.h"
#include "transaction-validator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Regulatory Compliance Engine - Issue #006
//
// Unified regulatory compliance framework consuming the existing audit-trail
// (Phase A.1) and transaction-validator (Phase A.3) modules as its data plane.
//
// Implemented tasks:
//   Task 6.1  SOX Compliance (SoxComplianceManager)
//     - Segregation-of-duties rule engine with configurable conflict pairs
//     - Change-management workflow (request/approve/implement/verify)
//     - Automated financial-controls tests delegating to TransactionValidator
//     - Tamper evidence via audit-trail hash-chain re-verification
//
//   Task 6.2  Basel III Capital Requirements (BaselIIIEngine)
//     - Risk-weighted assets (credit risk, standardized approach)
//     - CET1 / Tier 1 / Total capital ratios and CAR
//     - Leverage ratio (Tier 1 capital / total exposure measure)
//     - Configurable minimum thresholds + capital conservation buffer
//     - Stress scenario re-weighting and re-computation
//
//   Task 6.3  MiFID II Transaction Reporting (MifidReportingEngine)
//     - MifidTransactionReport built from Transaction + instrument metadata
//     - Required-field validation (RTS 22 style)
//     - Deterministic pipe-delimited submission format (documented below)
//     - Best-execution monitoring with basis-point deviation threshold
//     - Daily submission batching
//
//   Task 6.4  GDPR Data Protection (GdprDataProtection)
//     - Data-subject registry (subject id -> personal data categories held)
//     - Consent records (grant/withdraw with timestamp and purpose)
//     - Data-subject access requests (DSAR) with complete export
//     - Deterministic salted-hash pseudonymization; full anonymization
//     - Retention/erasure register with configurable legal deadline
//
// MiFID II submission format (deterministic, one record per line):
//   Header:  #MIFID2-TSV-v1
//   Record:  transaction_id|execution_timestamp_iso8601|instrument|isin|
//            quantity|price|currency|buyer_id|seller_id|venue|trading_capacity
//   Numeric fields are serialized with fixed precision (quantity and price use
//   8 decimal places, trailing zeros kept) so the output is byte-for-byte
//   reproducible for identical inputs. All text fields must be free of '|'
//   and '\n' (enforced by validation).
// ============================================================================

namespace ggnucash {
namespace compliance {

// ============================================================================
// Task 6.1 - SOX Compliance
// ============================================================================

// A single segregation-of-duties violation (or potential conflict) found by
// the SoD rule engine.
struct SodViolation {
    std::string user_id;
    std::string role_a;
    std::string role_b;
    std::string action;         // Action that triggered the check (may be empty)
    std::string description;
    bool        blocking;       // true => action must be denied

    SodViolation() : blocking(true) {}
};

// Change-management workflow lifecycle states (SOX IT general controls).
enum class ChangeState {
    REQUESTED,
    APPROVED,
    IMPLEMENTED,
    VERIFIED,
    REJECTED
};

inline std::string change_state_to_string(ChangeState state) {
    switch (state) {
        case ChangeState::REQUESTED:   return "REQUESTED";
        case ChangeState::APPROVED:    return "APPROVED";
        case ChangeState::IMPLEMENTED: return "IMPLEMENTED";
        case ChangeState::VERIFIED:    return "VERIFIED";
        case ChangeState::REJECTED:    return "REJECTED";
        default:                       return "UNKNOWN";
    }
}

// A change-management record. Every state transition is written to the
// immutable audit trail as a COMPLIANCE_EVENT.
struct ChangeRecord {
    std::string change_id;
    std::string title;
    std::string description;
    std::string requested_by;
    std::string approved_by;
    std::string implemented_by;
    std::string verified_by;
    ChangeState state;
    std::chrono::system_clock::time_point requested_at;
    std::chrono::system_clock::time_point approved_at;
    std::chrono::system_clock::time_point implemented_at;
    std::chrono::system_clock::time_point verified_at;
    std::vector<std::string> audit_entry_ids;   // Trail entries per transition

    ChangeRecord() : state(ChangeState::REQUESTED) {}
};

// Result of running one automated financial-controls test.
struct ControlTestResult {
    std::string control_name;
    bool        passed;
    uint64_t    findings_total;
    uint64_t    findings_error;
    uint64_t    findings_critical;
    std::string summary;
    std::string audit_entry_id;     // Compliance event recorded in the trail

    ControlTestResult()
        : passed(false), findings_total(0), findings_error(0),
          findings_critical(0) {}
};

// SOX compliance manager. Wraps a shared ImmutableAuditTrail (the data plane)
// and delegates control tests to a shared TransactionValidator.
class SoxComplianceManager {
public:
    // Both references must outlive this manager.
    SoxComplianceManager(audit::ImmutableAuditTrail & trail,
                         validation::TransactionValidator & validator);
    ~SoxComplianceManager() = default;

    // ---- Segregation of Duties ----

    // Configure a conflicting role pair (symmetric: a<->b).
    void add_sod_conflict(const std::string & role_a, const std::string & role_b);
    void remove_sod_conflict(const std::string & role_a, const std::string & role_b);
    size_t sod_conflict_count() const;

    // Assign / revoke a role for a user (audited).
    void assign_role(const std::string & user_id, const std::string & role,
                     const std::string & actor);
    void revoke_role(const std::string & user_id, const std::string & role,
                     const std::string & actor);

    // Roles currently held by a user.
    std::set<std::string> get_user_roles(const std::string & user_id) const;

    // Check whether `user_id` performing `action` violates any SoD rule given
    // the user's current role assignments. Detected violations are recorded
    // in the audit trail (CRITICAL if blocking). Returns all violations.
    std::vector<SodViolation> check_sod(const std::string & user_id,
                                        const std::string & action);

    // Static check: would holding this exact role set create a conflict?
    std::vector<SodViolation> find_conflicts(
        const std::string & user_id,
        const std::set<std::string> & roles) const;

    // ---- Change Management Workflow ----

    std::string request_change(const std::string & title,
                               const std::string & description,
                               const std::string & requested_by);
    bool approve_change(const std::string & change_id, const std::string & approver);
    bool reject_change(const std::string & change_id, const std::string & approver,
                       const std::string & reason);
    bool implement_change(const std::string & change_id, const std::string & implementer);
    bool verify_change(const std::string & change_id, const std::string & verifier);

    const ChangeRecord * get_change(const std::string & change_id) const;
    std::vector<ChangeRecord> list_changes() const;

    // ---- Automated Financial-Controls Tests ----

    using ControlTestFn =
        std::function<validation::ValidationReport(const std::vector<Transaction> &)>;

    // Register a named control test (e.g. "double-entry-balance",
    // "full-validation"). The functor delegates to the TransactionValidator.
    void register_control_test(const std::string & control_name, ControlTestFn fn);
    std::vector<std::string> list_control_tests() const;

    // Run a named control test against the given transactions, record the
    // outcome as a COMPLIANCE_EVENT in the audit trail, and return the result.
    ControlTestResult run_control_test(const std::string & control_name,
                                       const std::vector<Transaction> & transactions,
                                       const std::string & actor);

    // ---- Tamper Evidence ----

    // Re-verify the wrapped audit trail's hash chain. If verification fails,
    // a CRITICAL TAMPER_DETECTION alert is raised through the trail itself.
    bool verify_tamper_evidence() const;

    // Direct access to the wrapped trail (read-only queries/exports).
    const audit::ImmutableAuditTrail & get_audit_trail() const { return trail_; }

private:
    audit::ImmutableAuditTrail &       trail_;
    validation::TransactionValidator & validator_;

    std::set<std::pair<std::string, std::string>> sod_conflicts_;   // ordered pairs
    std::map<std::string, std::set<std::string>>  user_roles_;      // user -> roles
    std::map<std::string, ChangeRecord>           changes_;
    std::map<std::string, ControlTestFn>          control_tests_;

    mutable std::mutex  mutex_;
    std::atomic<uint64_t> change_counter_;

    static std::pair<std::string, std::string> ordered_pair(const std::string & a,
                                                            const std::string & b);
};

// ============================================================================
// Task 6.2 - Basel III Capital Requirements
// ============================================================================

// A single credit exposure under the standardized approach.
struct BaselExposure {
    std::string exposure_id;
    std::string exposure_class;     // e.g. "corporate", "sovereign", "retail"
    double      amount;             // Exposure at default (EAD)
    double      risk_weight;        // 0.0 .. 1.5 (0% .. 150%+)

    BaselExposure() : amount(0.0), risk_weight(1.0) {}
    BaselExposure(const std::string & id, const std::string & cls,
                  double amt, double rw)
        : exposure_id(id), exposure_class(cls), amount(amt), risk_weight(rw) {}
};

// Regulatory capital structure (all amounts in the reporting currency).
struct CapitalStructure {
    double cet1_capital;            // Common Equity Tier 1
    double additional_tier1;        // Additional Tier 1 instruments
    double tier2_capital;           // Tier 2 instruments

    CapitalStructure() : cet1_capital(0.0), additional_tier1(0.0), tier2_capital(0.0) {}

    double tier1_capital()  const { return cet1_capital + additional_tier1; }
    double total_capital()  const { return tier1_capital() + tier2_capital; }
};

// Minimum ratio thresholds (fractions, not percentages). Defaults follow the
// Basel III framework: CET1 4.5%, Tier 1 6%, Total CAR 8%, plus a 2.5%
// capital conservation buffer (CCB) applied to CET1, and a 3% leverage ratio.
struct BaselThresholds {
    double min_cet1;                // Hard minimum CET1 ratio
    double min_tier1;               // Hard minimum Tier 1 ratio
    double min_total_car;           // Hard minimum total capital ratio
    double capital_conservation_buffer;  // Extra CET1 buffer above hard minimum
    double min_leverage;            // Minimum leverage ratio

    BaselThresholds()
        : min_cet1(0.045),
          min_tier1(0.06),
          min_total_car(0.08),
          capital_conservation_buffer(0.025),
          min_leverage(0.03) {}

    // CET1 requirement including the conservation buffer.
    double cet1_with_buffer() const { return min_cet1 + capital_conservation_buffer; }
};

// Pass/fail assessment of a single capital ratio.
struct RatioAssessment {
    std::string name;
    double      ratio;
    double      minimum;            // Threshold assessed against
    bool        pass;
    std::string note;

    RatioAssessment() : ratio(0.0), minimum(0.0), pass(false) {}
};

// Full capital adequacy report.
struct CapitalReport {
    double      risk_weighted_assets;
    double      total_exposure;         // Leverage exposure measure
    double      cet1_ratio;
    double      tier1_ratio;
    double      total_car;
    double      leverage_ratio;

    std::vector<RatioAssessment> assessments;
    bool        overall_pass;
    std::string scenario_name;          // "" for the base scenario
    std::chrono::system_clock::time_point generated_at;

    CapitalReport()
        : risk_weighted_assets(0.0),
          total_exposure(0.0),
          cet1_ratio(0.0),
          tier1_ratio(0.0),
          total_car(0.0),
          leverage_ratio(0.0),
          overall_pass(false) {
        generated_at = std::chrono::system_clock::now();
    }

    bool ratio_pass(const std::string & name) const;
    std::string to_string() const;
};

// A named stress scenario: per-exposure-class risk-weight multipliers and/or
// absolute exposure haircuts. Multipliers default to 1.0 (no change).
struct StressScenario {
    std::string name;
    std::map<std::string, double> risk_weight_multipliers;  // class -> multiplier
    std::map<std::string, double> exposure_haircuts;        // class -> fractional haircut
    double      default_risk_weight_multiplier;

    StressScenario() : default_risk_weight_multiplier(1.0) {}
};

// Basel III capital requirements engine (credit-risk standardized approach).
class BaselIIIEngine {
public:
    BaselIIIEngine();
    explicit BaselIIIEngine(const BaselThresholds & thresholds);
    ~BaselIIIEngine() = default;

    void set_thresholds(const BaselThresholds & thresholds);
    const BaselThresholds & get_thresholds() const { return thresholds_; }

    // Book management.
    void add_exposure(const BaselExposure & exposure);
    void clear_exposures();
    size_t exposure_count() const;

    void set_capital(const CapitalStructure & capital);
    const CapitalStructure & get_capital() const { return capital_; }

    // Risk-weighted assets: sum(amount * risk_weight) over the book.
    double compute_rwa() const;

    // Leverage exposure measure: sum of gross exposure amounts.
    double compute_total_exposure() const;

    // Individual ratios (0.0 when RWA/exposure is zero).
    double cet1_ratio() const;
    double tier1_ratio() const;
    double capital_adequacy_ratio() const;
    double leverage_ratio() const;

    // Full assessment against configured thresholds.
    CapitalReport assess() const;

    // Apply a stress scenario to the book and recompute without mutating the
    // base book. Re-weighted amounts: amount' = amount * (1 - haircut),
    // risk_weight' = risk_weight * multiplier.
    CapitalReport assess_stress(const StressScenario & scenario) const;

private:
    BaselThresholds            thresholds_;
    CapitalStructure           capital_;
    std::vector<BaselExposure> exposures_;
    mutable std::mutex         mutex_;

    CapitalReport build_report(const std::vector<BaselExposure> & book,
                               const std::string & scenario_name) const;
};

// ============================================================================
// Task 6.3 - MiFID II Transaction Reporting
// ============================================================================

// Trading capacity values per MiFID II RTS 22 (field simplified to text).
enum class TradingCapacity {
    DEAL,       // Dealing on own account
    MTCH,       // Matched principal
    AOTC        // Any other capacity (agency)
};

inline std::string trading_capacity_to_string(TradingCapacity capacity) {
    switch (capacity) {
        case TradingCapacity::DEAL: return "DEAL";
        case TradingCapacity::MTCH: return "MTCH";
        case TradingCapacity::AOTC: return "AOTC";
        default:                    return "UNKNOWN";
    }
}

// Instrument metadata needed to enrich a Transaction into a MiFID report.
struct InstrumentMeta {
    std::string instrument;         // Human-readable instrument name/symbol
    std::string isin;               // ISO 6166 identifier
    std::string currency;           // ISO 4217 price currency
    std::string venue;              // MIC of the execution venue

    InstrumentMeta() = default;
    InstrumentMeta(const std::string & name, const std::string & isin_code,
                   const std::string & ccy, const std::string & mic)
        : instrument(name), isin(isin_code), currency(ccy), venue(mic) {}
};

// MiFID II transaction report (subset of RTS 22 fields).
struct MifidTransactionReport {
    std::string transaction_id;
    std::string instrument;
    std::string isin;
    double      quantity;
    double      price;
    std::string currency;
    std::string buyer_id;           // LEI or national id of buyer
    std::string seller_id;          // LEI or national id of seller
    std::chrono::system_clock::time_point execution_timestamp;
    std::string execution_timestamp_iso8601;
    std::string venue;              // MIC
    std::string trading_capacity;   // DEAL / MTCH / AOTC

    MifidTransactionReport() : quantity(0.0), price(0.0) {
        execution_timestamp = std::chrono::system_clock::now();
    }

    // Validate that all required fields are populated. Returns the list of
    // missing/invalid field names (empty => valid).
    std::vector<std::string> missing_fields() const;
    bool is_valid() const { return missing_fields().empty(); }
};

// Best-execution observation: executed price vs. reference price.
struct BestExecutionRecord {
    std::string transaction_id;
    std::string venue;
    double      executed_price;
    double      reference_price;
    double      deviation_bps;      // |executed - reference| / reference * 1e4
    bool        breach;             // deviation_bps > configured threshold
    std::chrono::system_clock::time_point observed_at;

    BestExecutionRecord()
        : executed_price(0.0), reference_price(0.0), deviation_bps(0.0),
          breach(false) {
        observed_at = std::chrono::system_clock::now();
    }
};

// MiFID II reporting engine: builds, validates, serializes, monitors best
// execution, and batches transaction reports for daily submission.
class MifidReportingEngine {
public:
    MifidReportingEngine();
    explicit MifidReportingEngine(double best_exec_threshold_bps);
    ~MifidReportingEngine() = default;

    void set_best_exec_threshold_bps(double threshold_bps);
    double get_best_exec_threshold_bps() const { return best_exec_threshold_bps_; }

    // Build a report from a financial Transaction plus instrument metadata.
    // Convention: quantity is derived from the transaction's total debits
    // divided by `price`; buyer_id/seller_id/trading capacity are supplied by
    // the caller. The transaction id and timestamp carry over directly.
    MifidTransactionReport build_report(const Transaction & tx,
                                        const InstrumentMeta & instrument,
                                        double quantity,
                                        double price,
                                        const std::string & buyer_id,
                                        const std::string & seller_id,
                                        TradingCapacity capacity) const;

    // Queue a report for the next daily submission batch. Returns false and
    // rejects the report if required fields are missing (the rejection is
    // counted for operational monitoring).
    bool submit_report(const MifidTransactionReport & report);

    // Number of reports currently queued.
    size_t pending_count() const;
    uint64_t rejected_count() const;

    // Serialize one report to the deterministic pipe-delimited record format
    // documented in the module header.
    static std::string serialize_report(const MifidTransactionReport & report);

    // Serialize and drain the current queue into a dated submission batch.
    // Output format:
    //   #MIFID2-TSV-v1
    //   #BATCH=<batch_id>
    //   #COUNT=<n>
    //   <record lines...>
    std::string build_submission_batch();

    // ---- Best-Execution Monitoring ----

    // Record an execution and compare against the reference price. A record
    // is always stored; if the absolute deviation exceeds the configured
    // basis-point threshold it is flagged as a breach.
    BestExecutionRecord record_execution(const std::string & transaction_id,
                                         const std::string & venue,
                                         double executed_price,
                                         double reference_price);

    std::vector<BestExecutionRecord> get_best_execution_records() const;
    std::vector<BestExecutionRecord> get_breaches() const;

private:
    double best_exec_threshold_bps_;
    std::vector<MifidTransactionReport> pending_;
    std::vector<BestExecutionRecord>  best_exec_records_;
    uint64_t                          rejected_;
    std::atomic<uint64_t>             batch_counter_;
    mutable std::mutex                mutex_;

    static std::string format_fixed(double value);
    static std::string to_iso8601(std::chrono::system_clock::time_point tp);
};

// ============================================================================
// Task 6.4 - GDPR Data Protection
// ============================================================================

// Categories of personal data, used for registry tagging. Extensible: the
// registry stores category strings so callers can add custom categories.
namespace gdpr_categories {
    extern const char * const IDENTITY;     // name, id numbers
    extern const char * const CONTACT;      // address, email, phone
    extern const char * const FINANCIAL;    // account numbers, transactions
    extern const char * const EMPLOYMENT;   // HR records
    extern const char * const LOCATION;     // geolocation
    extern const char * const ONLINE_ID;    // IP, device identifiers
}

// Consent record for a single purpose.
struct ConsentRecord {
    std::string subject_id;
    std::string purpose;
    bool        granted;
    std::chrono::system_clock::time_point timestamp;

    ConsentRecord() : granted(false) {
        timestamp = std::chrono::system_clock::now();
    }
};

// Data-subject access request (GDPR Art. 15) and its export.
struct DsarExport {
    std::string subject_id;
    std::chrono::system_clock::time_point generated_at;
    std::set<std::string> data_categories;
    std::map<std::string, std::string> personal_data;   // field -> value
    std::vector<ConsentRecord> consent_history;
    std::string to_string() const;
};

// Right-to-erasure (GDPR Art. 17) request tracked in the erasure register.
enum class ErasureStatus {
    PENDING,
    COMPLETED,
    REJECTED
};

inline std::string erasure_status_to_string(ErasureStatus status) {
    switch (status) {
        case ErasureStatus::PENDING:   return "PENDING";
        case ErasureStatus::COMPLETED: return "COMPLETED";
        case ErasureStatus::REJECTED:  return "REJECTED";
        default:                       return "UNKNOWN";
    }
}

struct ErasureRequest {
    std::string request_id;
    std::string subject_id;
    std::chrono::system_clock::time_point requested_at;
    std::chrono::system_clock::time_point legal_deadline;   // requested_at + legal window
    ErasureStatus status;
    std::chrono::system_clock::time_point completed_at;
    std::string notes;

    ErasureRequest() : status(ErasureStatus::PENDING) {}

    bool is_overdue(std::chrono::system_clock::time_point now) const {
        return status == ErasureStatus::PENDING && now > legal_deadline;
    }
};

// GDPR data-protection manager.
class GdprDataProtection {
public:
    // legal_deadline_hours defaults to 30 days (GDPR Art. 12(3) one-month
    // window, applied here to erasure handling).
    GdprDataProtection();
    explicit GdprDataProtection(std::chrono::hours legal_deadline);
    ~GdprDataProtection() = default;

    void set_legal_deadline(std::chrono::hours deadline);
    std::chrono::hours get_legal_deadline() const { return legal_deadline_; }

    // ---- Data-Subject Registry ----

    // Register a data subject and/or record that a category of personal data
    // is held for them.
    void register_subject(const std::string & subject_id);
    void add_data_category(const std::string & subject_id, const std::string & category);
    bool has_subject(const std::string & subject_id) const;
    std::set<std::string> get_data_categories(const std::string & subject_id) const;

    // Store a personal data field value (used by DSAR export).
    void set_personal_data(const std::string & subject_id,
                           const std::string & field,
                           const std::string & value);

    // ---- Consent Management ----

    void grant_consent(const std::string & subject_id, const std::string & purpose);
    void withdraw_consent(const std::string & subject_id, const std::string & purpose);

    // Currently-effective consent state (last event wins).
    bool has_consent(const std::string & subject_id, const std::string & purpose) const;
    std::vector<ConsentRecord> get_consent_history(const std::string & subject_id) const;

    // ---- Data-Subject Access Requests (Art. 15) ----

    // Produce a complete export of all data held for the subject.
    DsarExport process_dsar(const std::string & subject_id) const;

    // ---- Pseudonymization / Anonymization ----

    // Deterministic salted-hash pseudonymization: SHA-256(salt|subject|data).
    // Same inputs always produce the same pseudonym; different subjects
    // produce different pseudonyms for the same data. The salt is a per-
    // deployment secret held by the caller, not stored here.
    static std::string pseudonymize(const std::string & data,
                                    const std::string & subject_id,
                                    const std::string & salt);

    // Full anonymization: one-way, subject-independent, non-reversible
    // redaction. Output is a fixed marker plus a plain (unsalted) hash so
    // anonymized records remain joinable on exact equality only.
    static std::string anonymize(const std::string & data);

    // ---- Retention / Erasure Register (Art. 17) ----

    // Register a right-to-be-forgotten request. Returns the request id.
    std::string request_erasure(const std::string & subject_id);

    // Mark a request completed (erases the subject's personal data fields and
    // categories; consent history is retained pseudonymized for accountability).
    bool complete_erasure(const std::string & request_id);
    bool reject_erasure(const std::string & request_id, const std::string & reason);

    const ErasureRequest * get_erasure_request(const std::string & request_id) const;
    std::vector<ErasureRequest> list_erasure_requests() const;
    std::vector<ErasureRequest> get_overdue_requests() const;

    // Days remaining until the legal deadline (negative => overdue).
    double days_until_deadline(const std::string & request_id) const;

private:
    std::chrono::hours legal_deadline_;

    struct SubjectRecord {
        std::set<std::string> categories;
        std::map<std::string, std::string> personal_data;
        std::vector<ConsentRecord> consent_history;
    };

    std::map<std::string, SubjectRecord> subjects_;
    std::map<std::string, ErasureRequest> erasure_requests_;
    std::atomic<uint64_t> erasure_counter_;
    mutable std::mutex mutex_;
};

} // namespace compliance
} // namespace ggnucash
