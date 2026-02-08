#include "audit-trail.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace ggnucash::audit;

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

// Helper: create a test transaction
static Transaction make_test_transaction(const std::string & id, double amount) {
    Transaction tx;
    tx.id = id;
    tx.description = "Test transaction " + id;
    tx.generate_timestamp();
    tx.entries = {
        TransactionEntry("1101", amount, 0.0, "Test debit"),
        TransactionEntry("4100", 0.0, amount, "Test credit")
    };
    tx.calculate_hash();
    return tx;
}

// ============================================================================
// Tests
// ============================================================================

TEST(create_audit_trail) {
    ImmutableAuditTrail trail("test-key-123");
    ASSERT_EQ(trail.get_entry_count(), 0u);
    ASSERT_TRUE(trail.verify_integrity());
}

TEST(record_single_transaction) {
    ImmutableAuditTrail trail("test-key-123");

    Transaction tx = make_test_transaction("TX-001", 1000.00);
    std::string entry_id = trail.record_transaction(tx, "auditor", "Initial record");

    ASSERT_FALSE(entry_id.empty());
    ASSERT_EQ(trail.get_entry_count(), 1u);
    ASSERT_TRUE(trail.verify_integrity());
}

TEST(record_multiple_transactions) {
    ImmutableAuditTrail trail("test-key-123");

    for (int i = 0; i < 100; i++) {
        Transaction tx = make_test_transaction("TX-" + std::to_string(i), 100.0 + i);
        trail.record_transaction(tx, "system");
    }

    ASSERT_EQ(trail.get_entry_count(), 100u);
    ASSERT_TRUE(trail.verify_integrity());
}

TEST(record_event) {
    ImmutableAuditTrail trail("test-key-123");

    std::string id = trail.record_event(
        AuditSeverity::WARNING,
        AuditCategory::ACCESS_CONTROL,
        "admin",
        "LOGIN_FAILED",
        "user/jdoe",
        "Invalid password attempt",
        {{"ip", "192.168.1.1"}, {"attempts", "3"}});

    ASSERT_FALSE(id.empty());
    ASSERT_EQ(trail.get_entry_count(), 1u);

    const auto * entry = trail.get_entry(id);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->severity, AuditSeverity::WARNING);
    ASSERT_EQ(entry->category, AuditCategory::ACCESS_CONTROL);
    ASSERT_EQ(entry->actor, "admin");
    ASSERT_EQ(entry->action, "LOGIN_FAILED");
}

TEST(record_batch) {
    ImmutableAuditTrail trail("test-key-123");

    std::vector<Transaction> batch;
    for (int i = 0; i < 10; i++) {
        batch.push_back(make_test_transaction("BATCH-" + std::to_string(i), 500.0));
    }

    auto ids = trail.record_transaction_batch(batch, "batch-processor");

    ASSERT_EQ(ids.size(), 10u);
    // 10 transactions + 2 batch start/end events = 12
    ASSERT_EQ(trail.get_entry_count(), 12u);
    ASSERT_TRUE(trail.verify_integrity());
}

TEST(verify_integrity_pass) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");
    trail.record_transaction(make_test_transaction("TX-2", 200.0), "system");
    trail.record_transaction(make_test_transaction("TX-3", 300.0), "system");

    ASSERT_TRUE(trail.verify_integrity());
}

TEST(verify_entry) {
    ImmutableAuditTrail trail("test-key-123");

    std::string id = trail.record_transaction(
        make_test_transaction("TX-1", 100.0), "system");

    ASSERT_TRUE(trail.verify_entry(id));
    ASSERT_TRUE(trail.verify_signature(id));

    // Non-existent entry
    ASSERT_FALSE(trail.verify_entry("nonexistent"));
    ASSERT_FALSE(trail.verify_signature("nonexistent"));
}

TEST(integrity_report) {
    ImmutableAuditTrail trail("test-key-123");

    for (int i = 0; i < 5; i++) {
        trail.record_transaction(
            make_test_transaction("TX-" + std::to_string(i), 100.0 * i), "system");
    }

    auto report = trail.run_integrity_check();
    ASSERT_TRUE(report.overall_pass);
    ASSERT_EQ(report.entries_checked, 5u);
    ASSERT_EQ(report.entries_passed, 5u);
    ASSERT_EQ(report.entries_failed, 0u);
    ASSERT_TRUE(report.failed_entry_ids.empty());
    ASSERT_TRUE(report.chain_break_ids.empty());
    ASSERT_FALSE(report.report_text.empty());
}

TEST(query_by_severity) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_event(AuditSeverity::INFO, AuditCategory::SYSTEM_EVENT,
                       "system", "STARTUP", "server", "System started");
    trail.record_event(AuditSeverity::WARNING, AuditCategory::ACCESS_CONTROL,
                       "system", "AUTH_FAIL", "user/foo", "Bad password");
    trail.record_event(AuditSeverity::CRITICAL, AuditCategory::TAMPER_DETECTION,
                       "system", "TAMPER", "audit_trail", "Integrity check failed");

    AuditQuery q;
    q.severity_filter = {AuditSeverity::WARNING, AuditSeverity::CRITICAL};
    auto results = trail.query(q);
    ASSERT_EQ(results.size(), 2u);
}

TEST(query_by_category) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");
    trail.record_event(AuditSeverity::INFO, AuditCategory::CONFIGURATION_CHANGE,
                       "admin", "CONFIG_UPDATE", "settings", "Changed threshold");

    AuditQuery q;
    q.category_filter = {AuditCategory::TRANSACTION};
    auto results = trail.query(q);
    ASSERT_EQ(results.size(), 1u);
}

TEST(query_by_actor) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "alice");
    trail.record_transaction(make_test_transaction("TX-2", 200.0), "bob");
    trail.record_transaction(make_test_transaction("TX-3", 300.0), "alice");

    AuditQuery q;
    q.actor_filter = "alice";
    auto results = trail.query(q);
    ASSERT_EQ(results.size(), 2u);
}

TEST(query_pagination) {
    ImmutableAuditTrail trail("test-key-123");

    for (int i = 0; i < 20; i++) {
        trail.record_transaction(
            make_test_transaction("TX-" + std::to_string(i), 100.0), "system");
    }

    AuditQuery q;
    q.offset = 5;
    q.limit = 10;
    auto results = trail.query(q);
    ASSERT_EQ(results.size(), 10u);
}

TEST(get_transaction_entries) {
    ImmutableAuditTrail trail("test-key-123");

    Transaction tx = make_test_transaction("TX-SPECIAL", 999.0);
    trail.record_transaction(tx, "auditor");
    trail.record_transaction(make_test_transaction("TX-OTHER", 100.0), "system");

    auto entries = trail.get_transaction_entries("TX-SPECIAL");
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].transaction_id, "TX-SPECIAL");
}

TEST(statistics) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");
    trail.record_transaction(make_test_transaction("TX-2", 200.0), "system");
    trail.record_event(AuditSeverity::WARNING, AuditCategory::ACCESS_CONTROL,
                       "admin", "AUTH", "user", "test");

    auto stats = trail.get_statistics();
    ASSERT_EQ(stats.total_entries, 3u);
    ASSERT_EQ(stats.total_transactions_audited, 2u);
    ASSERT_GT(stats.entries_by_category.size(), 0u);
    ASSERT_GT(stats.entries_by_severity.size(), 0u);
}

TEST(export_json) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");
    trail.record_transaction(make_test_transaction("TX-2", 200.0), "system");

    std::string json = trail.export_json();
    ASSERT_FALSE(json.empty());
    ASSERT_TRUE(json.find("audit_trail") != std::string::npos);
    ASSERT_TRUE(json.find("TX-1") != std::string::npos);
    ASSERT_TRUE(json.find("TX-2") != std::string::npos);
}

TEST(export_csv) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");

    std::string csv = trail.export_csv();
    ASSERT_FALSE(csv.empty());
    ASSERT_TRUE(csv.find("entry_id") != std::string::npos);  // Header
    ASSERT_TRUE(csv.find("TX-1") != std::string::npos);
}

TEST(export_summary) {
    ImmutableAuditTrail trail("test-key-123");

    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");

    std::string summary = trail.export_summary();
    ASSERT_FALSE(summary.empty());
    ASSERT_TRUE(summary.find("IMMUTABLE AUDIT TRAIL SUMMARY") != std::string::npos);
    ASSERT_TRUE(summary.find("VERIFIED") != std::string::npos);
}

TEST(retention_policy) {
    ImmutableAuditTrail trail("test-key-123");

    RetentionPolicy policy;
    policy.name = "SOX-7-Year";
    policy.retention_period = std::chrono::hours(24 * 365 * 7);
    policy.allow_deletion = false;
    trail.set_retention_policy(policy);

    ASSERT_EQ(trail.get_retention_policy().name, "SOX-7-Year");
    ASSERT_FALSE(trail.get_retention_policy().allow_deletion);
}

TEST(tamper_alert_callback) {
    ImmutableAuditTrail trail("test-key-123");

    bool alert_received = false;
    trail.set_tamper_alert_callback([&](const TamperAlert & alert) {
        alert_received = true;
    });

    // Record entries and verify no tamper alerts
    trail.record_transaction(make_test_transaction("TX-1", 100.0), "system");
    auto alerts = trail.get_tamper_alerts();
    ASSERT_TRUE(alerts.empty());
    ASSERT_FALSE(alert_received);
}

TEST(empty_trail_integrity) {
    ImmutableAuditTrail trail("test-key-123");
    ASSERT_TRUE(trail.verify_integrity());

    auto report = trail.run_integrity_check();
    ASSERT_TRUE(report.overall_pass);
    ASSERT_EQ(report.entries_checked, 0u);
}

TEST(hash_chain_consistency) {
    ImmutableAuditTrail trail("test-key-123");

    // Add several entries and verify the chain is consistent
    for (int i = 0; i < 50; i++) {
        trail.record_transaction(
            make_test_transaction("TX-" + std::to_string(i), 100.0 + i), "system");
    }

    ASSERT_EQ(trail.get_entry_count(), 50u);
    ASSERT_TRUE(trail.verify_integrity());

    // Verify each individual entry
    for (int i = 0; i < 50; i++) {
        AuditQuery q;
        q.offset = i;
        q.limit = 1;
        auto entries = trail.query(q);
        ASSERT_EQ(entries.size(), 1u);
        ASSERT_TRUE(trail.verify_entry(entries[0].entry_id));
    }
}

TEST(metadata_in_transaction_entry) {
    ImmutableAuditTrail trail("test-key-123");

    Transaction tx = make_test_transaction("TX-META", 5000.0);
    tx.template_id = "TMPL-SALARY";
    tx.is_recurring = true;
    tx.recurrence_id = "REC-001";

    trail.record_transaction(tx, "payroll-system", "Monthly salary payment");

    auto entries = trail.get_transaction_entries("TX-META");
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_EQ(entries[0].metadata.at("template_id"), "TMPL-SALARY");
    ASSERT_EQ(entries[0].metadata.at("recurrence_id"), "REC-001");
    ASSERT_EQ(entries[0].metadata.at("is_balanced"), "true");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "\n=== IMMUTABLE AUDIT TRAIL TEST SUITE ===\n\n";

    // Tests are auto-registered and run via static initialization above.
    // This main() just reports summary.

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
