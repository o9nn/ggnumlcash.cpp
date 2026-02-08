#include "audit-persistence.h"
#include "audit-trail.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::audit;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                 \
    do {                                                                           \
        std::cout << "  Testing: " << name << "... ";                              \
        try {

#define TEST_END(name)                                                             \
            std::cout << "PASSED" << std::endl;                                    \
            tests_passed++;                                                        \
        } catch (const std::exception & e) {                                       \
            std::cout << "FAILED: " << e.what() << std::endl;                      \
            tests_failed++;                                                        \
        } catch (...) {                                                            \
            std::cout << "FAILED: Unknown exception" << std::endl;                 \
            tests_failed++;                                                        \
        }                                                                          \
    } while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } } while(0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { throw std::runtime_error("Assertion failed: " #a " != " #b " at line " + std::to_string(__LINE__)); } } while(0)

// ============================================================================
// Schema Generation Tests
// ============================================================================

void test_schema_generation() {
    TEST("Schema DDL generation");

    AuditPersistenceAdapter adapter;
    auto sql = adapter.generate_schema_sql();

    ASSERT_TRUE(!sql.empty());
    ASSERT_TRUE(sql.find("CREATE TABLE") != std::string::npos);
    ASSERT_TRUE(sql.find("audit_entries") != std::string::npos);
    ASSERT_TRUE(sql.find("entry_id") != std::string::npos);
    ASSERT_TRUE(sql.find("entry_hash") != std::string::npos);
    ASSERT_TRUE(sql.find("signature") != std::string::npos);
    ASSERT_TRUE(sql.find("timestamp_iso8601") != std::string::npos);
    ASSERT_TRUE(sql.find("severity") != std::string::npos);
    ASSERT_TRUE(sql.find("metadata_json") != std::string::npos);
    // Check indexes
    ASSERT_TRUE(sql.find("idx_audit_timestamp") != std::string::npos);
    ASSERT_TRUE(sql.find("idx_audit_severity") != std::string::npos);
    ASSERT_TRUE(sql.find("idx_audit_transaction_id") != std::string::npos);

    TEST_END("Schema DDL generation");
}

// ============================================================================
// Serialization Tests
// ============================================================================

void test_entry_serialization() {
    TEST("Entry serialization round-trip");

    AuditPersistenceAdapter adapter;

    SignedAuditEntry original;
    original.entry_id = "AUD-12345-0";
    original.previous_entry_hash = "GENESIS";
    original.entry_hash = "abc123def456";
    original.signature = "sig789";
    original.timestamp_iso8601 = "2024-01-15T10:30:00.000Z";
    original.severity = AuditSeverity::WARNING;
    original.category = AuditCategory::TRANSACTION;
    original.actor = "admin@system";
    original.action = "RECORD_TRANSACTION";
    original.resource = "transaction/TX-001";
    original.details = "Large payment detected";
    original.transaction_id = "TX-001";
    original.transaction_hash = "txhash456";
    original.metadata["entry_count"] = "2";
    original.metadata["is_balanced"] = "true";

    auto serialized = adapter.serialize_entry(original);
    auto restored = adapter.deserialize_entry(serialized);

    ASSERT_EQ(restored.entry_id, original.entry_id);
    ASSERT_EQ(restored.previous_entry_hash, original.previous_entry_hash);
    ASSERT_EQ(restored.entry_hash, original.entry_hash);
    ASSERT_EQ(restored.signature, original.signature);
    ASSERT_EQ(restored.timestamp_iso8601, original.timestamp_iso8601);
    ASSERT_EQ(severity_to_string(restored.severity), severity_to_string(original.severity));
    ASSERT_EQ(category_to_string(restored.category), category_to_string(original.category));
    ASSERT_EQ(restored.actor, original.actor);
    ASSERT_EQ(restored.action, original.action);
    ASSERT_EQ(restored.resource, original.resource);
    ASSERT_EQ(restored.details, original.details);
    ASSERT_EQ(restored.transaction_id, original.transaction_id);
    ASSERT_EQ(restored.transaction_hash, original.transaction_hash);
    ASSERT_EQ(restored.metadata.size(), original.metadata.size());
    ASSERT_EQ(restored.metadata["entry_count"], "2");
    ASSERT_EQ(restored.metadata["is_balanced"], "true");

    TEST_END("Entry serialization round-trip");
}

void test_metadata_serialization() {
    TEST("Metadata JSON serialization");

    AuditPersistenceAdapter adapter;

    std::map<std::string, std::string> metadata;
    metadata["key1"] = "value1";
    metadata["key2"] = "value with spaces";
    metadata["key3"] = "value with \"quotes\"";

    auto json = adapter.serialize_metadata(metadata);
    ASSERT_TRUE(json.find("key1") != std::string::npos);
    ASSERT_TRUE(json.find("value1") != std::string::npos);

    auto restored = adapter.deserialize_metadata(json);
    ASSERT_EQ(restored.size(), (size_t)3);
    ASSERT_EQ(restored["key1"], "value1");
    ASSERT_EQ(restored["key2"], "value with spaces");
    ASSERT_EQ(restored["key3"], "value with \"quotes\"");

    TEST_END("Metadata JSON serialization");
}

void test_empty_metadata() {
    TEST("Empty metadata serialization");

    AuditPersistenceAdapter adapter;

    std::map<std::string, std::string> empty;
    auto json = adapter.serialize_metadata(empty);
    ASSERT_EQ(json, "{}");

    auto restored = adapter.deserialize_metadata(json);
    ASSERT_TRUE(restored.empty());

    auto also_empty = adapter.deserialize_metadata("");
    ASSERT_TRUE(also_empty.empty());

    TEST_END("Empty metadata serialization");
}

// ============================================================================
// Batch Operation Tests
// ============================================================================

void test_batch_serialization() {
    TEST("Batch entry serialization");

    AuditPersistenceAdapter adapter;

    std::vector<SignedAuditEntry> entries;
    for (int i = 0; i < 5; i++) {
        SignedAuditEntry entry;
        entry.entry_id = "AUD-" + std::to_string(i);
        entry.entry_hash = "hash" + std::to_string(i);
        entry.signature = "sig" + std::to_string(i);
        entry.severity = AuditSeverity::INFO;
        entry.category = AuditCategory::TRANSACTION;
        entry.actor = "system";
        entry.action = "TEST";
        entries.push_back(entry);
    }

    auto serialized = adapter.serialize_batch(entries);
    ASSERT_EQ(serialized.size(), (size_t)5);
    ASSERT_EQ(serialized[0].entry_id, "AUD-0");
    ASSERT_EQ(serialized[4].entry_id, "AUD-4");

    TEST_END("Batch entry serialization");
}

void test_batch_insert_sql() {
    TEST("Batch INSERT SQL generation");

    AuditPersistenceAdapter adapter;

    std::vector<SignedAuditEntry> entries;
    for (int i = 0; i < 3; i++) {
        SignedAuditEntry entry;
        entry.entry_id = "AUD-" + std::to_string(i);
        entry.previous_entry_hash = (i == 0) ? "GENESIS" : "hash" + std::to_string(i - 1);
        entry.entry_hash = "hash" + std::to_string(i);
        entry.signature = "sig" + std::to_string(i);
        entry.timestamp_iso8601 = "2024-01-15T00:00:00.000Z";
        entry.severity = AuditSeverity::INFO;
        entry.category = AuditCategory::TRANSACTION;
        entry.actor = "system";
        entry.action = "TEST";
        entries.push_back(entry);
    }

    auto serialized = adapter.serialize_batch(entries);
    auto sql = adapter.generate_batch_insert_sql(serialized);

    ASSERT_TRUE(!sql.empty());
    ASSERT_TRUE(sql.find("INSERT INTO audit_entries") != std::string::npos);
    ASSERT_TRUE(sql.find("AUD-0") != std::string::npos);
    ASSERT_TRUE(sql.find("AUD-1") != std::string::npos);
    ASSERT_TRUE(sql.find("AUD-2") != std::string::npos);
    ASSERT_TRUE(sql.find("GENESIS") != std::string::npos);

    TEST_END("Batch INSERT SQL generation");
}

void test_empty_batch_insert() {
    TEST("Empty batch INSERT SQL");

    AuditPersistenceAdapter adapter;
    std::vector<SerializedAuditEntry> empty;
    auto sql = adapter.generate_batch_insert_sql(empty);
    ASSERT_TRUE(sql.empty());

    TEST_END("Empty batch INSERT SQL");
}

// ============================================================================
// Query SQL Generation Tests
// ============================================================================

void test_query_sql_basic() {
    TEST("Basic query SQL generation");

    AuditPersistenceAdapter adapter;

    AuditQuery query;
    auto sql = adapter.generate_query_sql(query);

    ASSERT_TRUE(!sql.empty());
    ASSERT_TRUE(sql.find("SELECT") != std::string::npos);
    ASSERT_TRUE(sql.find("FROM audit_entries") != std::string::npos);
    ASSERT_TRUE(sql.find("ORDER BY timestamp_iso8601 ASC") != std::string::npos);
    ASSERT_TRUE(sql.find("LIMIT 1000") != std::string::npos);

    TEST_END("Basic query SQL generation");
}

void test_query_sql_with_filters() {
    TEST("Query SQL with filters");

    AuditPersistenceAdapter adapter;

    AuditQuery query;
    query.severity_filter.push_back(AuditSeverity::WARNING);
    query.severity_filter.push_back(AuditSeverity::CRITICAL);
    query.actor_filter = "admin";
    query.limit = 50;
    query.offset = 10;

    auto sql = adapter.generate_query_sql(query);

    ASSERT_TRUE(sql.find("WHERE") != std::string::npos);
    ASSERT_TRUE(sql.find("severity IN") != std::string::npos);
    ASSERT_TRUE(sql.find("WARNING") != std::string::npos);
    ASSERT_TRUE(sql.find("CRITICAL") != std::string::npos);
    ASSERT_TRUE(sql.find("actor = 'admin'") != std::string::npos);
    ASSERT_TRUE(sql.find("LIMIT 50") != std::string::npos);
    ASSERT_TRUE(sql.find("OFFSET 10") != std::string::npos);

    TEST_END("Query SQL with filters");
}

void test_query_sql_category_filter() {
    TEST("Query SQL with category filter");

    AuditPersistenceAdapter adapter;

    AuditQuery query;
    query.category_filter.push_back(AuditCategory::TRANSACTION);
    query.transaction_id_filter = "TX-001";

    auto sql = adapter.generate_query_sql(query);

    ASSERT_TRUE(sql.find("category IN") != std::string::npos);
    ASSERT_TRUE(sql.find("TRANSACTION") != std::string::npos);
    ASSERT_TRUE(sql.find("transaction_id = 'TX-001'") != std::string::npos);

    TEST_END("Query SQL with category filter");
}

// ============================================================================
// Export Format Tests
// ============================================================================

void test_sql_export() {
    TEST("Full SQL export");

    AuditPersistenceAdapter adapter;

    // Create entries via audit trail
    ImmutableAuditTrail trail("test-key");
    Transaction tx;
    tx.id = "TX-001";
    tx.description = "Test transaction";
    tx.entries.push_back(TransactionEntry("1000", 500.0, 0.0, "Debit"));
    tx.entries.push_back(TransactionEntry("2000", 0.0, 500.0, "Credit"));
    tx.calculate_hash();
    trail.record_transaction(tx, "test-actor");

    AuditQuery q;
    auto entries = trail.query(q);

    auto sql = adapter.export_to_sql(entries);

    ASSERT_TRUE(!sql.empty());
    ASSERT_TRUE(sql.find("BEGIN TRANSACTION") != std::string::npos);
    ASSERT_TRUE(sql.find("CREATE TABLE") != std::string::npos);
    ASSERT_TRUE(sql.find("INSERT INTO") != std::string::npos);
    ASSERT_TRUE(sql.find("COMMIT") != std::string::npos);
    ASSERT_TRUE(sql.find("TX-001") != std::string::npos);

    TEST_END("Full SQL export");
}

void test_structured_text_export() {
    TEST("Structured text export for PDF");

    AuditPersistenceAdapter adapter;

    ImmutableAuditTrail trail("test-key");
    Transaction tx;
    tx.id = "TX-001";
    tx.description = "Test transaction";
    tx.entries.push_back(TransactionEntry("1000", 500.0, 0.0));
    tx.entries.push_back(TransactionEntry("2000", 0.0, 500.0));
    tx.calculate_hash();
    trail.record_transaction(tx, "auditor");

    trail.record_event(AuditSeverity::WARNING, AuditCategory::COMPLIANCE_EVENT,
                       "compliance-bot", "THRESHOLD_CHECK", "transaction/TX-001",
                       "Large transaction flagged");

    AuditQuery q;
    auto entries = trail.query(q);

    auto text = adapter.export_to_structured_text(entries, "SOX Compliance Report");

    ASSERT_TRUE(!text.empty());
    ASSERT_TRUE(text.find("SOX Compliance Report") != std::string::npos);
    ASSERT_TRUE(text.find("SEVERITY BREAKDOWN") != std::string::npos);
    ASSERT_TRUE(text.find("CATEGORY BREAKDOWN") != std::string::npos);
    ASSERT_TRUE(text.find("DETAILED ENTRIES") != std::string::npos);
    ASSERT_TRUE(text.find("TX-001") != std::string::npos);
    ASSERT_TRUE(text.find("SOX Section 302/404") != std::string::npos);
    ASSERT_TRUE(text.find("END OF REPORT") != std::string::npos);

    TEST_END("Structured text export for PDF");
}

// ============================================================================
// SQL Injection Prevention Tests
// ============================================================================

void test_sql_escaping() {
    TEST("SQL escaping prevents injection");

    AuditPersistenceAdapter adapter;

    SignedAuditEntry entry;
    entry.entry_id = "AUD-1";
    entry.entry_hash = "hash1";
    entry.signature = "sig1";
    entry.severity = AuditSeverity::INFO;
    entry.category = AuditCategory::SYSTEM_EVENT;
    entry.actor = "user'; DROP TABLE audit_entries; --";
    entry.action = "TEST";
    entry.details = "O'Brien's payment";

    auto serialized = adapter.serialize_entry(entry);
    std::vector<SerializedAuditEntry> batch = {serialized};
    auto sql = adapter.generate_batch_insert_sql(batch);

    // Single quotes should be escaped as ''
    ASSERT_TRUE(sql.find("user''; DROP TABLE") != std::string::npos);
    ASSERT_TRUE(sql.find("O''Brien") != std::string::npos);
    // Should NOT contain unescaped SQL injection
    ASSERT_TRUE(sql.find("user'; DROP TABLE") == std::string::npos);

    TEST_END("SQL escaping prevents injection");
}

// ============================================================================
// Integration with ImmutableAuditTrail Tests
// ============================================================================

void test_full_trail_serialize_restore() {
    TEST("Full audit trail serialize and verify");

    AuditPersistenceAdapter adapter;
    ImmutableAuditTrail trail("integration-key");

    // Record several transactions
    for (int i = 0; i < 5; i++) {
        Transaction tx;
        tx.id = "TX-" + std::to_string(i);
        tx.description = "Transaction " + std::to_string(i);
        tx.entries.push_back(TransactionEntry("1000", 100.0 * (i + 1), 0.0));
        tx.entries.push_back(TransactionEntry("2000", 0.0, 100.0 * (i + 1)));
        tx.calculate_hash();
        trail.record_transaction(tx, "batch-actor");
    }

    // Verify original trail integrity
    ASSERT_TRUE(trail.verify_integrity());

    // Serialize all entries
    AuditQuery q;
    q.limit = 1000;
    auto entries = trail.query(q);
    auto serialized = adapter.serialize_batch(entries);

    ASSERT_EQ(serialized.size(), entries.size());

    // Deserialize and verify each entry matches
    for (size_t i = 0; i < serialized.size(); i++) {
        auto restored = adapter.deserialize_entry(serialized[i]);
        ASSERT_EQ(restored.entry_id, entries[i].entry_id);
        ASSERT_EQ(restored.entry_hash, entries[i].entry_hash);
        ASSERT_EQ(restored.signature, entries[i].signature);
        ASSERT_EQ(restored.previous_entry_hash, entries[i].previous_entry_hash);
    }

    TEST_END("Full audit trail serialize and verify");
}

void test_persistence_stats() {
    TEST("Persistence statistics tracking");

    AuditPersistenceAdapter adapter;
    auto stats = adapter.get_stats();

    ASSERT_EQ(stats.entries_saved, (uint64_t)0);
    ASSERT_EQ(stats.entries_loaded, (uint64_t)0);
    ASSERT_EQ(stats.save_failures, (uint64_t)0);

    TEST_END("Persistence statistics tracking");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Audit Persistence Adapter Tests" << std::endl;
    std::cout << "  (Phase A.1 DB Integration)" << std::endl;
    std::cout << "============================================" << std::endl;

    std::cout << "\n--- Schema Tests ---" << std::endl;
    test_schema_generation();

    std::cout << "\n--- Serialization Tests ---" << std::endl;
    test_entry_serialization();
    test_metadata_serialization();
    test_empty_metadata();

    std::cout << "\n--- Batch Operation Tests ---" << std::endl;
    test_batch_serialization();
    test_batch_insert_sql();
    test_empty_batch_insert();

    std::cout << "\n--- Query SQL Tests ---" << std::endl;
    test_query_sql_basic();
    test_query_sql_with_filters();
    test_query_sql_category_filter();

    std::cout << "\n--- Export Format Tests ---" << std::endl;
    test_sql_export();
    test_structured_text_export();

    std::cout << "\n--- Security Tests ---" << std::endl;
    test_sql_escaping();

    std::cout << "\n--- Integration Tests ---" << std::endl;
    test_full_trail_serialize_restore();
    test_persistence_stats();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
