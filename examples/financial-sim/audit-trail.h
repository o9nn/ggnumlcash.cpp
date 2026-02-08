#pragma once

#include "transaction-engine.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>
#include <algorithm>

// ============================================================================
// Immutable Audit Trail Engine - Phase A.1
//
// Cryptographically secured, append-only audit trail for all financial
// transactions. SOX compliant with 7-year retention and instant retrieval.
// Builds on the existing AuditTrail/AuditBlock infrastructure from
// transaction-engine.h, adding:
//   - Cryptographic signing with timestamp attestation
//   - Tamper detection with automatic alerting
//   - 7-year retention with instant retrieval
//   - Export formats: JSON, CSV for external auditors
//   - Integration with database persistence layer
// ============================================================================

namespace ggnucash {
namespace audit {

// ============================================================================
// Audit Entry Severity & Category
// ============================================================================

enum class AuditSeverity {
    INFO,
    WARNING,
    CRITICAL,
    ALERT
};

enum class AuditCategory {
    TRANSACTION,
    ACCOUNT_MODIFICATION,
    ACCESS_CONTROL,
    CONFIGURATION_CHANGE,
    COMPLIANCE_EVENT,
    TAMPER_DETECTION,
    SYSTEM_EVENT
};

inline std::string severity_to_string(AuditSeverity severity) {
    switch (severity) {
        case AuditSeverity::INFO:     return "INFO";
        case AuditSeverity::WARNING:  return "WARNING";
        case AuditSeverity::CRITICAL: return "CRITICAL";
        case AuditSeverity::ALERT:    return "ALERT";
        default:                      return "UNKNOWN";
    }
}

inline std::string category_to_string(AuditCategory category) {
    switch (category) {
        case AuditCategory::TRANSACTION:          return "TRANSACTION";
        case AuditCategory::ACCOUNT_MODIFICATION: return "ACCOUNT_MODIFICATION";
        case AuditCategory::ACCESS_CONTROL:       return "ACCESS_CONTROL";
        case AuditCategory::CONFIGURATION_CHANGE: return "CONFIGURATION_CHANGE";
        case AuditCategory::COMPLIANCE_EVENT:     return "COMPLIANCE_EVENT";
        case AuditCategory::TAMPER_DETECTION:      return "TAMPER_DETECTION";
        case AuditCategory::SYSTEM_EVENT:         return "SYSTEM_EVENT";
        default:                                  return "UNKNOWN";
    }
}

// ============================================================================
// Signed Audit Entry - individual audit log entry with cryptographic signature
// ============================================================================

struct SignedAuditEntry {
    std::string entry_id;
    std::string previous_entry_hash;    // Hash chain link
    std::string entry_hash;             // SHA-256 of this entry
    std::string signature;              // HMAC-SHA256 signature

    // Timestamp attestation
    std::chrono::system_clock::time_point timestamp;
    std::string timestamp_iso8601;      // Human-readable ISO 8601

    // Entry content
    AuditSeverity severity;
    AuditCategory category;
    std::string actor;                  // User/system performing the action
    std::string action;                 // Description of the action
    std::string resource;               // Affected resource (account, transaction, etc.)
    std::string details;                // Additional details

    // Associated transaction (if applicable)
    std::string transaction_id;
    std::string transaction_hash;

    // Metadata
    std::map<std::string, std::string> metadata;

    SignedAuditEntry()
        : severity(AuditSeverity::INFO),
          category(AuditCategory::SYSTEM_EVENT) {
        timestamp = std::chrono::system_clock::now();
        generate_timestamp_string();
    }

    void generate_timestamp_string() {
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch()) % 1000;
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
        ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        timestamp_iso8601 = ss.str();
    }

    // Compute hash of this entry's content (excluding entry_hash and signature)
    std::string compute_hash() const {
        std::stringstream ss;
        ss << entry_id << "|"
           << previous_entry_hash << "|"
           << timestamp_iso8601 << "|"
           << severity_to_string(severity) << "|"
           << category_to_string(category) << "|"
           << actor << "|"
           << action << "|"
           << resource << "|"
           << details << "|"
           << transaction_id << "|"
           << transaction_hash;
        return SHA256::hash(ss.str());
    }

    // Compute HMAC-style signature using a signing key
    std::string compute_signature(const std::string & signing_key) const {
        std::string payload = entry_hash + "|" + signing_key;
        return SHA256::hash(payload);
    }
};

// ============================================================================
// Tamper Detection Alert
// ============================================================================

struct TamperAlert {
    std::string alert_id;
    std::chrono::system_clock::time_point detected_at;
    std::string description;
    std::string affected_entry_id;
    std::string expected_hash;
    std::string actual_hash;

    TamperAlert()
        : detected_at(std::chrono::system_clock::now()) {}
};

// ============================================================================
// Retention Policy
// ============================================================================

struct RetentionPolicy {
    std::string name;
    std::chrono::hours retention_period;
    bool allow_deletion;        // Whether entries can be deleted after retention
    bool require_verification;  // Verify integrity before archival
    std::string archive_format; // json, csv

    RetentionPolicy()
        : name("SOX-7-Year"),
          retention_period(std::chrono::hours(24 * 365 * 7)),
          allow_deletion(false),
          require_verification(true),
          archive_format("json") {}
};

// ============================================================================
// Audit Trail Query
// ============================================================================

struct AuditQuery {
    // Time range
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    bool has_time_range;

    // Filters
    std::vector<AuditSeverity> severity_filter;
    std::vector<AuditCategory> category_filter;
    std::string actor_filter;
    std::string resource_filter;
    std::string transaction_id_filter;

    // Pagination
    size_t offset;
    size_t limit;

    AuditQuery()
        : has_time_range(false),
          offset(0),
          limit(1000) {}

    bool matches(const SignedAuditEntry & entry) const {
        if (has_time_range) {
            if (entry.timestamp < start_time || entry.timestamp > end_time) {
                return false;
            }
        }

        if (!severity_filter.empty()) {
            bool found = false;
            for (auto sev : severity_filter) {
                if (entry.severity == sev) { found = true; break; }
            }
            if (!found) return false;
        }

        if (!category_filter.empty()) {
            bool found = false;
            for (auto cat : category_filter) {
                if (entry.category == cat) { found = true; break; }
            }
            if (!found) return false;
        }

        if (!actor_filter.empty() && entry.actor != actor_filter) {
            return false;
        }

        if (!resource_filter.empty() && entry.resource != resource_filter) {
            return false;
        }

        if (!transaction_id_filter.empty() && entry.transaction_id != transaction_id_filter) {
            return false;
        }

        return true;
    }
};

// ============================================================================
// Audit Trail Statistics
// ============================================================================

struct AuditTrailStats {
    uint64_t total_entries;
    uint64_t total_transactions_audited;
    uint64_t tamper_alerts;
    uint64_t integrity_checks_passed;
    uint64_t integrity_checks_failed;
    std::chrono::system_clock::time_point oldest_entry;
    std::chrono::system_clock::time_point newest_entry;
    std::map<std::string, uint64_t> entries_by_category;
    std::map<std::string, uint64_t> entries_by_severity;

    AuditTrailStats() : total_entries(0), total_transactions_audited(0),
                        tamper_alerts(0), integrity_checks_passed(0),
                        integrity_checks_failed(0) {}
};

// ============================================================================
// Immutable Audit Trail Manager
// ============================================================================

class ImmutableAuditTrail {
public:
    using TamperAlertCallback = std::function<void(const TamperAlert &)>;

    ImmutableAuditTrail(const std::string & signing_key = "GGNUCASH_DEFAULT_SIGNING_KEY");
    ~ImmutableAuditTrail() = default;

    // ---- Core Operations (append-only) ----

    // Record a transaction in the audit trail
    std::string record_transaction(const Transaction & tx,
                                   const std::string & actor,
                                   const std::string & details = "");

    // Record a generic audit event
    std::string record_event(AuditSeverity severity,
                             AuditCategory category,
                             const std::string & actor,
                             const std::string & action,
                             const std::string & resource,
                             const std::string & details = "",
                             const std::map<std::string, std::string> & metadata = {});

    // Record multiple transactions as a batch
    std::vector<std::string> record_transaction_batch(
        const std::vector<Transaction> & transactions,
        const std::string & actor);

    // ---- Integrity Verification ----

    // Verify the entire audit trail hash chain
    bool verify_integrity() const;

    // Verify a specific entry
    bool verify_entry(const std::string & entry_id) const;

    // Verify signature of a specific entry
    bool verify_signature(const std::string & entry_id) const;

    // Run full integrity check and return detailed results
    struct IntegrityReport {
        bool overall_pass;
        uint64_t entries_checked;
        uint64_t entries_passed;
        uint64_t entries_failed;
        std::vector<std::string> failed_entry_ids;
        std::vector<std::string> chain_break_ids;
        std::string report_text;
    };
    IntegrityReport run_integrity_check() const;

    // ---- Tamper Detection ----

    // Set callback for tamper alerts
    void set_tamper_alert_callback(TamperAlertCallback callback);

    // Get all tamper alerts
    std::vector<TamperAlert> get_tamper_alerts() const;

    // Clear tamper alerts (after investigation)
    void clear_tamper_alerts();

    // ---- Query & Retrieval ----

    // Query entries with filters
    std::vector<SignedAuditEntry> query(const AuditQuery & query) const;

    // Get entry by ID
    const SignedAuditEntry * get_entry(const std::string & entry_id) const;

    // Get entries for a specific transaction
    std::vector<SignedAuditEntry> get_transaction_entries(const std::string & transaction_id) const;

    // Get entry count
    size_t get_entry_count() const;

    // Get statistics
    AuditTrailStats get_statistics() const;

    // ---- Export ----

    // Export to JSON format
    std::string export_json(const AuditQuery & query = AuditQuery()) const;

    // Export to CSV format
    std::string export_csv(const AuditQuery & query = AuditQuery()) const;

    // Export full trail summary (human-readable)
    std::string export_summary() const;

    // ---- Retention ----

    // Set retention policy
    void set_retention_policy(const RetentionPolicy & policy);

    // Get current retention policy
    const RetentionPolicy & get_retention_policy() const;

    // Check if any entries are past retention period (read-only check)
    std::vector<std::string> get_expired_entry_ids() const;

private:
    std::string signing_key_;
    RetentionPolicy retention_policy_;

    // Append-only entry store (never modified after insertion)
    std::vector<SignedAuditEntry> entries_;
    std::map<std::string, size_t> entry_index_;     // entry_id -> index
    std::map<std::string, std::vector<size_t>> tx_index_; // tx_id -> entry indices

    mutable std::mutex trail_mutex_;

    // Tamper detection
    std::vector<TamperAlert> tamper_alerts_;
    TamperAlertCallback tamper_callback_;

    // Counters
    std::atomic<uint64_t> entry_counter_;

    // Internal helpers
    std::string generate_entry_id();
    void sign_entry(SignedAuditEntry & entry);
    void raise_tamper_alert(const std::string & description,
                            const std::string & entry_id,
                            const std::string & expected_hash,
                            const std::string & actual_hash);
};

} // namespace audit
} // namespace ggnucash
