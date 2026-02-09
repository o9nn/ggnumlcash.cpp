#pragma once

#include "audit-trail.h"
#include "database-persistence.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

// ============================================================================
// Audit Trail Persistence Adapter - Phase A.1 Completion
//
// Bridges the ImmutableAuditTrail (in-memory, Phase A.1) with the
// DatabasePersistenceManager (Phase 1.4) to provide durable storage
// for audit entries. Implements:
//   - Serialization/deserialization of SignedAuditEntry to/from DB rows
//   - Batch persistence for high throughput
//   - Schema creation for audit entry storage
//   - Load and restore from persistent storage
//   - Integrity verification after load
// ============================================================================

namespace ggnucash {
namespace audit {

// ============================================================================
// Audit Entry Serialized Form (for database storage)
// ============================================================================

struct SerializedAuditEntry {
    std::string entry_id;
    std::string previous_entry_hash;
    std::string entry_hash;
    std::string signature;
    std::string timestamp_iso8601;
    std::string severity;
    std::string category;
    std::string actor;
    std::string action;
    std::string resource;
    std::string details;
    std::string transaction_id;
    std::string transaction_hash;
    std::string metadata_json;      // Metadata serialized as JSON

    SerializedAuditEntry() {}
};

// ============================================================================
// Persistence Statistics
// ============================================================================

struct AuditPersistenceStats {
    uint64_t entries_saved;
    uint64_t entries_loaded;
    uint64_t save_failures;
    uint64_t load_failures;
    uint64_t batches_saved;
    std::chrono::milliseconds total_save_time;
    std::chrono::milliseconds total_load_time;
    std::chrono::milliseconds last_save_time;

    AuditPersistenceStats()
        : entries_saved(0),
          entries_loaded(0),
          save_failures(0),
          load_failures(0),
          batches_saved(0),
          total_save_time(0),
          total_load_time(0),
          last_save_time(0) {}
};

// ============================================================================
// Audit Persistence Adapter
// ============================================================================

class AuditPersistenceAdapter {
public:
    AuditPersistenceAdapter();
    ~AuditPersistenceAdapter() = default;

    // ---- Schema Management ----

    // Generate SQL DDL for audit trail tables
    std::string generate_schema_sql() const;

    // ---- Serialization ----

    // Serialize a single audit entry for storage
    SerializedAuditEntry serialize_entry(const SignedAuditEntry & entry) const;

    // Deserialize a stored entry back to SignedAuditEntry
    SignedAuditEntry deserialize_entry(const SerializedAuditEntry & serialized) const;

    // Serialize metadata map to JSON string
    std::string serialize_metadata(const std::map<std::string, std::string> & metadata) const;

    // Deserialize JSON string to metadata map
    std::map<std::string, std::string> deserialize_metadata(const std::string & json) const;

    // ---- Batch Operations ----

    // Serialize a batch of entries
    std::vector<SerializedAuditEntry> serialize_batch(
        const std::vector<SignedAuditEntry> & entries) const;

    // Generate INSERT SQL for a batch of entries
    std::string generate_batch_insert_sql(
        const std::vector<SerializedAuditEntry> & entries) const;

    // Generate SELECT SQL for querying entries
    std::string generate_query_sql(const AuditQuery & query) const;

    // ---- Export Formats ----

    // Export all entries to a self-contained SQL file (for backup/transfer)
    std::string export_to_sql(const std::vector<SignedAuditEntry> & entries) const;

    // Export to structured text format for PDF generation
    std::string export_to_structured_text(
        const std::vector<SignedAuditEntry> & entries,
        const std::string & report_title = "Audit Trail Report") const;

    // ---- Statistics ----
    AuditPersistenceStats get_stats() const;

private:
    mutable AuditPersistenceStats stats_;
    mutable std::mutex stats_mutex_;

    // SQL helpers
    std::string escape_sql(const std::string & str) const;
    std::string severity_to_sql(AuditSeverity severity) const;
    std::string category_to_sql(AuditCategory category) const;
    AuditSeverity sql_to_severity(const std::string & str) const;
    AuditCategory sql_to_category(const std::string & str) const;
};

} // namespace audit
} // namespace ggnucash
