#include "audit-trail.h"
#include <algorithm>

namespace ggnucash {
namespace audit {

// ============================================================================
// ImmutableAuditTrail Implementation
// ============================================================================

ImmutableAuditTrail::ImmutableAuditTrail(const std::string & signing_key)
    : signing_key_(signing_key), entry_counter_(0) {}

// ---- Core Operations ----

std::string ImmutableAuditTrail::record_transaction(
    const Transaction & tx,
    const std::string & actor,
    const std::string & details
) {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    SignedAuditEntry entry;
    entry.entry_id = generate_entry_id();
    entry.previous_entry_hash = entries_.empty() ? "GENESIS" : entries_.back().entry_hash;
    entry.severity = AuditSeverity::INFO;
    entry.category = AuditCategory::TRANSACTION;
    entry.actor = actor;
    entry.action = "RECORD_TRANSACTION";
    entry.resource = "transaction/" + tx.id;
    entry.details = details.empty() ? tx.description : details;
    entry.transaction_id = tx.id;
    entry.transaction_hash = tx.hash.empty() ? SHA256::hash_transaction(tx) : tx.hash;

    // Add transaction metadata
    entry.metadata["entry_count"] = std::to_string(tx.entries.size());
    entry.metadata["is_balanced"] = tx.is_balanced() ? "true" : "false";
    if (!tx.template_id.empty()) {
        entry.metadata["template_id"] = tx.template_id;
    }
    if (tx.is_recurring) {
        entry.metadata["recurrence_id"] = tx.recurrence_id;
    }

    // Compute totals for the audit record
    double total_debits = 0.0, total_credits = 0.0;
    for (const auto & te : tx.entries) {
        total_debits += te.debit_amount;
        total_credits += te.credit_amount;
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << total_debits;
    entry.metadata["total_debits"] = ss.str();
    ss.str(""); ss.clear();
    ss << std::fixed << std::setprecision(2) << total_credits;
    entry.metadata["total_credits"] = ss.str();

    sign_entry(entry);

    size_t idx = entries_.size();
    entries_.push_back(entry);
    entry_index_[entry.entry_id] = idx;
    tx_index_[tx.id].push_back(idx);

    return entry.entry_id;
}

std::string ImmutableAuditTrail::record_event(
    AuditSeverity severity,
    AuditCategory category,
    const std::string & actor,
    const std::string & action,
    const std::string & resource,
    const std::string & details,
    const std::map<std::string, std::string> & metadata
) {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    SignedAuditEntry entry;
    entry.entry_id = generate_entry_id();
    entry.previous_entry_hash = entries_.empty() ? "GENESIS" : entries_.back().entry_hash;
    entry.severity = severity;
    entry.category = category;
    entry.actor = actor;
    entry.action = action;
    entry.resource = resource;
    entry.details = details;
    entry.metadata = metadata;

    sign_entry(entry);

    size_t idx = entries_.size();
    entries_.push_back(entry);
    entry_index_[entry.entry_id] = idx;

    return entry.entry_id;
}

std::vector<std::string> ImmutableAuditTrail::record_transaction_batch(
    const std::vector<Transaction> & transactions,
    const std::string & actor
) {
    std::vector<std::string> ids;
    ids.reserve(transactions.size());

    // Record batch start event
    std::map<std::string, std::string> batch_meta;
    batch_meta["batch_size"] = std::to_string(transactions.size());
    record_event(AuditSeverity::INFO, AuditCategory::TRANSACTION,
                 actor, "BATCH_START", "transaction_batch",
                 "Starting batch of " + std::to_string(transactions.size()) + " transactions",
                 batch_meta);

    for (const auto & tx : transactions) {
        ids.push_back(record_transaction(tx, actor));
    }

    // Record batch end event
    record_event(AuditSeverity::INFO, AuditCategory::TRANSACTION,
                 actor, "BATCH_END", "transaction_batch",
                 "Completed batch of " + std::to_string(transactions.size()) + " transactions",
                 batch_meta);

    return ids;
}

// ---- Integrity Verification ----

bool ImmutableAuditTrail::verify_integrity() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    if (entries_.empty()) return true;

    // Verify genesis link
    if (entries_[0].previous_entry_hash != "GENESIS") return false;

    for (size_t i = 0; i < entries_.size(); i++) {
        const auto & entry = entries_[i];

        // Verify hash
        std::string computed_hash = entry.compute_hash();
        if (computed_hash != entry.entry_hash) return false;

        // Verify signature
        std::string computed_sig = entry.compute_signature(signing_key_);
        if (computed_sig != entry.signature) return false;

        // Verify chain linkage
        if (i > 0) {
            if (entry.previous_entry_hash != entries_[i - 1].entry_hash) return false;
        }
    }

    return true;
}

bool ImmutableAuditTrail::verify_entry(const std::string & entry_id) const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    auto it = entry_index_.find(entry_id);
    if (it == entry_index_.end()) return false;

    const auto & entry = entries_[it->second];
    std::string computed_hash = entry.compute_hash();
    return computed_hash == entry.entry_hash;
}

bool ImmutableAuditTrail::verify_signature(const std::string & entry_id) const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    auto it = entry_index_.find(entry_id);
    if (it == entry_index_.end()) return false;

    const auto & entry = entries_[it->second];
    std::string computed_sig = entry.compute_signature(signing_key_);
    return computed_sig == entry.signature;
}

ImmutableAuditTrail::IntegrityReport ImmutableAuditTrail::run_integrity_check() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    IntegrityReport report;
    report.overall_pass = true;
    report.entries_checked = entries_.size();
    report.entries_passed = 0;
    report.entries_failed = 0;

    if (entries_.empty()) {
        report.report_text = "Audit trail is empty. No entries to verify.";
        return report;
    }

    // Verify genesis
    if (entries_[0].previous_entry_hash != "GENESIS") {
        report.overall_pass = false;
        report.chain_break_ids.push_back(entries_[0].entry_id);
    }

    for (size_t i = 0; i < entries_.size(); i++) {
        const auto & entry = entries_[i];
        bool entry_ok = true;

        // Verify hash
        std::string computed_hash = entry.compute_hash();
        if (computed_hash != entry.entry_hash) {
            entry_ok = false;
            report.failed_entry_ids.push_back(entry.entry_id);
        }

        // Verify signature
        std::string computed_sig = entry.compute_signature(signing_key_);
        if (computed_sig != entry.signature) {
            entry_ok = false;
            if (std::find(report.failed_entry_ids.begin(), report.failed_entry_ids.end(),
                          entry.entry_id) == report.failed_entry_ids.end()) {
                report.failed_entry_ids.push_back(entry.entry_id);
            }
        }

        // Verify chain linkage
        if (i > 0 && entry.previous_entry_hash != entries_[i - 1].entry_hash) {
            entry_ok = false;
            report.chain_break_ids.push_back(entry.entry_id);
        }

        if (entry_ok) {
            report.entries_passed++;
        } else {
            report.entries_failed++;
            report.overall_pass = false;
        }
    }

    // Generate report text
    std::stringstream ss;
    ss << "=== AUDIT TRAIL INTEGRITY REPORT ===\n";
    ss << "Result: " << (report.overall_pass ? "PASS" : "FAIL") << "\n";
    ss << "Entries checked: " << report.entries_checked << "\n";
    ss << "Entries passed: " << report.entries_passed << "\n";
    ss << "Entries failed: " << report.entries_failed << "\n";

    if (!report.failed_entry_ids.empty()) {
        ss << "\nFailed entries:\n";
        for (const auto & id : report.failed_entry_ids) {
            ss << "  - " << id << "\n";
        }
    }
    if (!report.chain_break_ids.empty()) {
        ss << "\nChain breaks at:\n";
        for (const auto & id : report.chain_break_ids) {
            ss << "  - " << id << "\n";
        }
    }

    report.report_text = ss.str();
    return report;
}

// ---- Tamper Detection ----

void ImmutableAuditTrail::set_tamper_alert_callback(TamperAlertCallback callback) {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    tamper_callback_ = callback;
}

std::vector<TamperAlert> ImmutableAuditTrail::get_tamper_alerts() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    return tamper_alerts_;
}

void ImmutableAuditTrail::clear_tamper_alerts() {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    tamper_alerts_.clear();
}

void ImmutableAuditTrail::raise_tamper_alert(
    const std::string & description,
    const std::string & entry_id,
    const std::string & expected_hash,
    const std::string & actual_hash
) {
    TamperAlert alert;
    alert.alert_id = "TAMPER-" + std::to_string(tamper_alerts_.size() + 1);
    alert.description = description;
    alert.affected_entry_id = entry_id;
    alert.expected_hash = expected_hash;
    alert.actual_hash = actual_hash;

    tamper_alerts_.push_back(alert);

    // Also record as audit event (within already-held lock, so use internal path)
    // Note: we don't call record_event here to avoid deadlock; the alert is stored separately.

    if (tamper_callback_) {
        tamper_callback_(alert);
    }
}

// ---- Query & Retrieval ----

std::vector<SignedAuditEntry> ImmutableAuditTrail::query(const AuditQuery & q) const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    std::vector<SignedAuditEntry> results;
    size_t skipped = 0;

    for (const auto & entry : entries_) {
        if (!q.matches(entry)) continue;

        if (skipped < q.offset) {
            skipped++;
            continue;
        }

        results.push_back(entry);
        if (results.size() >= q.limit) break;
    }

    return results;
}

const SignedAuditEntry * ImmutableAuditTrail::get_entry(const std::string & entry_id) const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    auto it = entry_index_.find(entry_id);
    if (it == entry_index_.end()) return nullptr;
    return &entries_[it->second];
}

std::vector<SignedAuditEntry> ImmutableAuditTrail::get_transaction_entries(
    const std::string & transaction_id
) const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    std::vector<SignedAuditEntry> results;
    auto it = tx_index_.find(transaction_id);
    if (it == tx_index_.end()) return results;

    for (size_t idx : it->second) {
        results.push_back(entries_[idx]);
    }
    return results;
}

size_t ImmutableAuditTrail::get_entry_count() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    return entries_.size();
}

AuditTrailStats ImmutableAuditTrail::get_statistics() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    AuditTrailStats stats;
    stats.total_entries = entries_.size();
    stats.total_transactions_audited = tx_index_.size();
    stats.tamper_alerts = tamper_alerts_.size();

    if (!entries_.empty()) {
        stats.oldest_entry = entries_.front().timestamp;
        stats.newest_entry = entries_.back().timestamp;
    }

    for (const auto & entry : entries_) {
        stats.entries_by_category[category_to_string(entry.category)]++;
        stats.entries_by_severity[severity_to_string(entry.severity)]++;
    }

    return stats;
}

// ---- Export ----

std::string ImmutableAuditTrail::export_json(const AuditQuery & q) const {
    auto results = query(q);

    std::stringstream ss;
    ss << "{\n";
    ss << "  \"audit_trail\": {\n";
    ss << "    \"total_entries\": " << get_entry_count() << ",\n";
    ss << "    \"exported_entries\": " << results.size() << ",\n";
    ss << "    \"integrity_verified\": " << (verify_integrity() ? "true" : "false") << ",\n";
    ss << "    \"entries\": [\n";

    for (size_t i = 0; i < results.size(); i++) {
        const auto & entry = results[i];
        ss << "      {\n";
        ss << "        \"entry_id\": \"" << entry.entry_id << "\",\n";
        ss << "        \"timestamp\": \"" << entry.timestamp_iso8601 << "\",\n";
        ss << "        \"severity\": \"" << severity_to_string(entry.severity) << "\",\n";
        ss << "        \"category\": \"" << category_to_string(entry.category) << "\",\n";
        ss << "        \"actor\": \"" << entry.actor << "\",\n";
        ss << "        \"action\": \"" << entry.action << "\",\n";
        ss << "        \"resource\": \"" << entry.resource << "\",\n";
        ss << "        \"details\": \"" << entry.details << "\",\n";
        ss << "        \"transaction_id\": \"" << entry.transaction_id << "\",\n";
        ss << "        \"entry_hash\": \"" << entry.entry_hash << "\",\n";
        ss << "        \"previous_hash\": \"" << entry.previous_entry_hash << "\",\n";
        ss << "        \"signature\": \"" << entry.signature << "\",\n";
        ss << "        \"metadata\": {";

        bool first_meta = true;
        for (const auto & kv : entry.metadata) {
            if (!first_meta) ss << ",";
            ss << "\n          \"" << kv.first << "\": \"" << kv.second << "\"";
            first_meta = false;
        }
        if (!entry.metadata.empty()) ss << "\n        ";
        ss << "}\n";
        ss << "      }";
        if (i < results.size() - 1) ss << ",";
        ss << "\n";
    }

    ss << "    ]\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

std::string ImmutableAuditTrail::export_csv(const AuditQuery & q) const {
    auto results = query(q);

    std::stringstream ss;
    // Header
    ss << "entry_id,timestamp,severity,category,actor,action,resource,"
       << "details,transaction_id,entry_hash,previous_hash,signature\n";

    for (const auto & entry : results) {
        ss << entry.entry_id << ","
           << entry.timestamp_iso8601 << ","
           << severity_to_string(entry.severity) << ","
           << category_to_string(entry.category) << ","
           << entry.actor << ","
           << entry.action << ","
           << entry.resource << ","
           << "\"" << entry.details << "\","
           << entry.transaction_id << ","
           << entry.entry_hash << ","
           << entry.previous_entry_hash << ","
           << entry.signature << "\n";
    }

    return ss.str();
}

std::string ImmutableAuditTrail::export_summary() const {
    auto stats = get_statistics();

    std::stringstream ss;
    ss << "=== IMMUTABLE AUDIT TRAIL SUMMARY ===\n\n";
    ss << "Total Entries: " << stats.total_entries << "\n";
    ss << "Transactions Audited: " << stats.total_transactions_audited << "\n";
    ss << "Tamper Alerts: " << stats.tamper_alerts << "\n";
    ss << "Integrity: " << (verify_integrity() ? "VERIFIED" : "COMPROMISED") << "\n\n";

    if (stats.total_entries > 0) {
        auto oldest_t = std::chrono::system_clock::to_time_t(stats.oldest_entry);
        auto newest_t = std::chrono::system_clock::to_time_t(stats.newest_entry);
        ss << "Date Range:\n";
        ss << "  Oldest: " << std::put_time(std::gmtime(&oldest_t), "%Y-%m-%d %H:%M:%S UTC") << "\n";
        ss << "  Newest: " << std::put_time(std::gmtime(&newest_t), "%Y-%m-%d %H:%M:%S UTC") << "\n\n";
    }

    ss << "Entries by Category:\n";
    for (const auto & kv : stats.entries_by_category) {
        ss << "  " << kv.first << ": " << kv.second << "\n";
    }

    ss << "\nEntries by Severity:\n";
    for (const auto & kv : stats.entries_by_severity) {
        ss << "  " << kv.first << ": " << kv.second << "\n";
    }

    return ss.str();
}

// ---- Retention ----

void ImmutableAuditTrail::set_retention_policy(const RetentionPolicy & policy) {
    std::lock_guard<std::mutex> lock(trail_mutex_);
    retention_policy_ = policy;
}

const RetentionPolicy & ImmutableAuditTrail::get_retention_policy() const {
    return retention_policy_;
}

std::vector<std::string> ImmutableAuditTrail::get_expired_entry_ids() const {
    std::lock_guard<std::mutex> lock(trail_mutex_);

    std::vector<std::string> expired;
    auto cutoff = std::chrono::system_clock::now() - retention_policy_.retention_period;

    for (const auto & entry : entries_) {
        if (entry.timestamp < cutoff) {
            expired.push_back(entry.entry_id);
        }
    }

    return expired;
}

// ---- Internal Helpers ----

std::string ImmutableAuditTrail::generate_entry_id() {
    uint64_t count = entry_counter_.fetch_add(1);
    auto now = std::chrono::system_clock::now().time_since_epoch().count();

    std::stringstream ss;
    ss << "AUD-" << now << "-" << count;
    return ss.str();
}

void ImmutableAuditTrail::sign_entry(SignedAuditEntry & entry) {
    // Compute content hash
    entry.entry_hash = entry.compute_hash();

    // Compute signature using signing key
    entry.signature = entry.compute_signature(signing_key_);
}

} // namespace audit
} // namespace ggnucash
