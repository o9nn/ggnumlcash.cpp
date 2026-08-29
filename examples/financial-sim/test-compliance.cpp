#include "compliance.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ggnucash;
using namespace ggnucash::compliance;

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

#define ASSERT_NEAR(a, b, tol) do { double _va = (a), _vb = (b), _vt = (tol); \
    if (std::fabs(_va - _vb) > _vt) { \
        throw std::runtime_error(std::string("Assertion failed: |") + #a + " - " + #b + \
            "| = " + std::to_string(std::fabs(_va - _vb)) + " > " + std::to_string(_vt) + \
            " at line " + std::to_string(__LINE__)); \
    } } while(0)

// ============================================================================
// Helpers
// ============================================================================

static Transaction make_tx(const std::string & id, double amount) {
    Transaction tx;
    tx.id = id;
    tx.description = "test tx " + id;
    tx.generate_timestamp();
    tx.entries.emplace_back("1000", amount, 0.0, "debit leg");
    tx.entries.emplace_back("2000", 0.0, amount, "credit leg");
    tx.calculate_hash();
    return tx;
}

static Transaction make_unbalanced_tx(const std::string & id, double amount) {
    Transaction tx;
    tx.id = id;
    tx.description = "unbalanced tx " + id;
    tx.generate_timestamp();
    tx.entries.emplace_back("1000", amount, 0.0, "debit leg");
    tx.entries.emplace_back("2000", 0.0, amount + 50.0, "mismatched credit leg");
    tx.calculate_hash();
    return tx;
}

// ============================================================================
// Task 6.1 - SOX Compliance Tests
// ============================================================================

void test_sod_no_conflict_when_roles_compatible() {
    TEST("SoD check passes for non-conflicting roles");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    sox.add_sod_conflict("initiate_payment", "approve_payment");
    sox.assign_role("alice", "initiate_payment", "admin");
    sox.assign_role("alice", "view_reports", "admin");

    auto violations = sox.check_sod("alice", "initiate payment P-1");
    ASSERT_TRUE(violations.empty());
    TEST_END("SoD check passes for non-conflicting roles");
}

void test_sod_conflict_detected() {
    TEST("SoD conflict detected for conflicting role pair");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    sox.add_sod_conflict("initiate_payment", "approve_payment");
    sox.assign_role("bob", "initiate_payment", "admin");
    sox.assign_role("bob", "approve_payment", "admin");

    auto violations = sox.check_sod("bob", "approve payment P-2");
    ASSERT_EQ(violations.size(), (size_t) 1);
    ASSERT_EQ(violations[0].user_id, std::string("bob"));
    ASSERT_TRUE(violations[0].blocking);

    // Violation must be recorded in the audit trail as a compliance event.
    audit::AuditQuery q;
    q.category_filter = {audit::AuditCategory::COMPLIANCE_EVENT};
    auto events = trail.query(q);
    ASSERT_FALSE(events.empty());
    bool found_sod = false;
    for (const auto & e : events) {
        if (e.action == "SOD_VIOLATION") { found_sod = true; }
    }
    ASSERT_TRUE(found_sod);
    TEST_END("SoD conflict detected for conflicting role pair");
}

void test_sod_symmetric_and_static_check() {
    TEST("SoD conflict pairs are symmetric; static find_conflicts works");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    sox.add_sod_conflict("trade_execution", "trade_settlement");
    ASSERT_EQ(sox.sod_conflict_count(), (size_t) 1);

    // Roles presented in the reverse order still conflict.
    auto v = sox.find_conflicts("carol", {"trade_settlement", "trade_execution"});
    ASSERT_EQ(v.size(), (size_t) 1);

    // Removing the conflict clears detection.
    sox.remove_sod_conflict("trade_execution", "trade_settlement");
    ASSERT_EQ(sox.sod_conflict_count(), (size_t) 0);
    ASSERT_TRUE(sox.find_conflicts("carol",
        {"trade_settlement", "trade_execution"}).empty());
    TEST_END("SoD conflict pairs are symmetric; static find_conflicts works");
}

void test_change_management_workflow() {
    TEST("Change-management workflow records audit entries");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    std::string cid = sox.request_change("Update rate feed", "Switch market data feed", "ops-dave");
    ASSERT_FALSE(cid.empty());
    ASSERT_EQ(sox.get_change(cid)->state, ChangeState::REQUESTED);

    // Requester cannot approve own change (SoD).
    ASSERT_FALSE(sox.approve_change(cid, "ops-dave"));
    ASSERT_TRUE(sox.approve_change(cid, "mgr-erin"));
    ASSERT_EQ(sox.get_change(cid)->state, ChangeState::APPROVED);

    // Approver cannot implement own approval (SoD).
    ASSERT_FALSE(sox.implement_change(cid, "mgr-erin"));
    ASSERT_TRUE(sox.implement_change(cid, "dev-frank"));
    ASSERT_EQ(sox.get_change(cid)->state, ChangeState::IMPLEMENTED);

    // Implementer cannot verify own implementation (SoD).
    ASSERT_FALSE(sox.verify_change(cid, "dev-frank"));
    ASSERT_TRUE(sox.verify_change(cid, "qa-gina"));
    ASSERT_EQ(sox.get_change(cid)->state, ChangeState::VERIFIED);

    // Four lifecycle transitions => four audit entries linked to the record.
    const ChangeRecord * rec = sox.get_change(cid);
    ASSERT_EQ(rec->audit_entry_ids.size(), (size_t) 4);
    for (const auto & eid : rec->audit_entry_ids) {
        ASSERT_TRUE(trail.get_entry(eid) != nullptr);
    }

    // All workflow events are in the compliance-event category.
    audit::AuditQuery q;
    q.category_filter = {audit::AuditCategory::COMPLIANCE_EVENT};
    auto events = trail.query(q);
    ASSERT_EQ(events.size(), (size_t) 4);
    TEST_END("Change-management workflow records audit entries");
}

void test_change_management_reject_and_invalid_transitions() {
    TEST("Change rejection and invalid transitions");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    std::string cid = sox.request_change("Bypass control", "Bad change", "mallory");
    ASSERT_TRUE(sox.reject_change(cid, "mgr-erin", "violates policy"));
    ASSERT_EQ(sox.get_change(cid)->state, ChangeState::REJECTED);

    // Cannot approve/implement a rejected change.
    ASSERT_FALSE(sox.approve_change(cid, "mgr-erin"));
    ASSERT_FALSE(sox.implement_change(cid, "dev-frank"));

    // Unknown change id fails cleanly.
    ASSERT_FALSE(sox.approve_change("CHG-9999", "mgr-erin"));
    ASSERT_TRUE(sox.get_change("CHG-9999") == nullptr);
    TEST_END("Change rejection and invalid transitions");
}

void test_control_test_pass_and_audit_event() {
    TEST("Control test passes on balanced book and records audit event");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    std::vector<Transaction> txs = {make_tx("TX-1", 100.0), make_tx("TX-2", 250.0)};
    auto result = sox.run_control_test("double-entry-balance", txs, "controller-heidi");
    ASSERT_TRUE(result.passed);
    ASSERT_EQ(result.findings_error, (uint64_t) 0);
    ASSERT_FALSE(result.audit_entry_id.empty());

    // Audit event exists in the trail.
    const audit::SignedAuditEntry * entry = trail.get_entry(result.audit_entry_id);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->category, audit::AuditCategory::COMPLIANCE_EVENT);
    ASSERT_EQ(entry->action, std::string("CONTROL_TEST"));
    ASSERT_EQ(entry->resource, std::string("double-entry-balance"));
    TEST_END("Control test passes on balanced book and records audit event");
}

void test_control_test_failure_recorded() {
    TEST("Control test failure detected and audited as CRITICAL");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    std::vector<Transaction> txs = {make_tx("TX-3", 100.0),
                                    make_unbalanced_tx("TX-4", 500.0)};
    auto result = sox.run_control_test("double-entry-balance", txs, "controller-heidi");
    ASSERT_FALSE(result.passed);
    ASSERT_TRUE(result.findings_error > 0 || result.findings_critical > 0);

    const audit::SignedAuditEntry * entry = trail.get_entry(result.audit_entry_id);
    ASSERT_TRUE(entry != nullptr);
    ASSERT_EQ(entry->severity, audit::AuditSeverity::CRITICAL);

    // Unknown control test throws.
    bool threw = false;
    try {
        sox.run_control_test("nonexistent", txs, "x");
    } catch (const std::runtime_error &) {
        threw = true;
    }
    ASSERT_TRUE(threw);
    TEST_END("Control test failure detected and audited as CRITICAL");
}

void test_tamper_evidence_verification() {
    TEST("Tamper-evidence re-verifies the audit trail hash chain");
    audit::ImmutableAuditTrail trail;
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    sox.assign_role("ivan", "auditor", "admin");
    ASSERT_TRUE(sox.verify_tamper_evidence());

    auto report = trail.run_integrity_check();
    ASSERT_TRUE(report.overall_pass);
    ASSERT_TRUE(report.entries_checked > 0);
    TEST_END("Tamper-evidence re-verifies the audit trail hash chain");
}

// ============================================================================
// Task 6.2 - Basel III Tests
// ============================================================================

// Known small book:
//   corporate 1000 @ 100%  -> RWA 1000
//   sovereign  500 @   0%  -> RWA 0
//   retail     200 @  75%  -> RWA 150
//   corporate  300 @ 150%  -> RWA 450
// Total exposure = 2000, RWA = 1600.
static BaselIIIEngine make_known_book() {
    BaselIIIEngine engine;
    engine.add_exposure(BaselExposure("E1", "corporate", 1000.0, 1.00));
    engine.add_exposure(BaselExposure("E2", "sovereign",  500.0, 0.00));
    engine.add_exposure(BaselExposure("E3", "retail",     200.0, 0.75));
    engine.add_exposure(BaselExposure("E4", "corporate",  300.0, 1.50));
    return engine;
}

void test_basel_rwa_computation() {
    TEST("Basel III RWA and total exposure on a known book");
    BaselIIIEngine engine = make_known_book();
    ASSERT_EQ(engine.exposure_count(), (size_t) 4);
    ASSERT_NEAR(engine.compute_rwa(), 1600.0, 1e-9);
    ASSERT_NEAR(engine.compute_total_exposure(), 2000.0, 1e-9);
    TEST_END("Basel III RWA and total exposure on a known book");
}

void test_basel_ratios_pass() {
    TEST("Basel III ratios pass with adequate capital");
    BaselIIIEngine engine = make_known_book();
    CapitalStructure cap;
    cap.cet1_capital      = 120.0;  // CET1  = 120/1600  = 7.5%   (>= 7.0% incl. CCB)
    cap.additional_tier1  = 20.0;   // Tier1 = 140/1600  = 8.75%  (>= 6%)
    cap.tier2_capital     = 20.0;   // CAR   = 160/1600  = 10%    (>= 8%)
    engine.set_capital(cap);

    ASSERT_NEAR(engine.cet1_ratio(), 0.075, 1e-9);
    ASSERT_NEAR(engine.tier1_ratio(), 0.0875, 1e-9);
    ASSERT_NEAR(engine.capital_adequacy_ratio(), 0.10, 1e-9);
    ASSERT_NEAR(engine.leverage_ratio(), 140.0 / 2000.0, 1e-9);  // 7%

    CapitalReport report = engine.assess();
    ASSERT_TRUE(report.overall_pass);
    ASSERT_TRUE(report.ratio_pass("CET1"));
    ASSERT_TRUE(report.ratio_pass("Tier1"));
    ASSERT_TRUE(report.ratio_pass("CAR"));
    ASSERT_TRUE(report.ratio_pass("Leverage"));
    ASSERT_FALSE(report.to_string().empty());
    TEST_END("Basel III ratios pass with adequate capital");
}

void test_basel_ratios_fail_at_thresholds() {
    TEST("Basel III ratios fail below thresholds");
    BaselIIIEngine engine = make_known_book();
    CapitalStructure cap;
    cap.cet1_capital     = 60.0;   // CET1  = 60/1600 = 3.75%  (< 4.5% hard min)
    cap.additional_tier1 = 20.0;   // Tier1 = 80/1600 = 5%     (< 6%)
    cap.tier2_capital    = 20.0;   // CAR   = 100/1600 = 6.25% (< 8%)
    engine.set_capital(cap);

    CapitalReport report = engine.assess();
    ASSERT_FALSE(report.overall_pass);
    ASSERT_FALSE(report.ratio_pass("CET1"));
    ASSERT_FALSE(report.ratio_pass("Tier1"));
    ASSERT_FALSE(report.ratio_pass("CAR"));
    // Leverage = 80/2000 = 4% >= 3% passes.
    ASSERT_TRUE(report.ratio_pass("Leverage"));

    // CET1 above hard minimum but below minimum+CCB must still fail.
    BaselIIIEngine engine2 = make_known_book();
    CapitalStructure cap2;
    cap2.cet1_capital     = 96.0;  // CET1 = 6% >= 4.5% but < 7.0% incl. CCB
    cap2.additional_tier1 = 40.0;  // Tier1 = 8.5%
    cap2.tier2_capital    = 40.0;  // CAR   = 11%
    engine2.set_capital(cap2);
    CapitalReport report2 = engine2.assess();
    ASSERT_FALSE(report2.ratio_pass("CET1"));
    ASSERT_FALSE(report2.overall_pass);
    TEST_END("Basel III ratios fail below thresholds");
}

void test_basel_custom_thresholds() {
    TEST("Basel III configurable thresholds are honored");
    BaselThresholds t;
    t.min_cet1 = 0.10;                    // Stricter 10% CET1
    t.capital_conservation_buffer = 0.0;  // No buffer
    BaselIIIEngine engine(t);
    engine.add_exposure(BaselExposure("E1", "corporate", 1000.0, 1.0));
    CapitalStructure cap;
    cap.cet1_capital = 90.0;  // 9% < 10%
    engine.set_capital(cap);

    CapitalReport report = engine.assess();
    ASSERT_FALSE(report.ratio_pass("CET1"));
    ASSERT_NEAR(report.assessments[0].minimum, 0.10, 1e-12);
    TEST_END("Basel III configurable thresholds are honored");
}

void test_basel_stress_scenario() {
    TEST("Basel III stress scenario re-weights and recomputes");
    BaselIIIEngine engine = make_known_book();
    CapitalStructure cap;
    cap.cet1_capital     = 120.0;
    cap.additional_tier1 = 20.0;
    cap.tier2_capital    = 20.0;
    engine.set_capital(cap);

    StressScenario stress;
    stress.name = "severe-recession";
    stress.risk_weight_multipliers["corporate"] = 1.5;   // corporate RW x1.5
    stress.exposure_haircuts["retail"] = 0.25;           // retail EAD -25%

    // Stressed book:
    //   corporate 1000 @ 150% -> 1500
    //   sovereign  500 @   0% -> 0
    //   retail     150 @  75% -> 112.5
    //   corporate  300 @ 225% -> 675
    // Stressed RWA = 2287.5; base RWA unchanged = 1600.
    CapitalReport stressed = engine.assess_stress(stress);
    ASSERT_EQ(stressed.scenario_name, std::string("severe-recession"));
    ASSERT_NEAR(stressed.risk_weighted_assets, 2287.5, 1e-9);
    ASSERT_NEAR(engine.compute_rwa(), 1600.0, 1e-9);  // base book untouched

    // CET1 under stress = 120 / 2287.5 = 5.249% < 7% -> fails.
    ASSERT_NEAR(stressed.cet1_ratio, 120.0 / 2287.5, 1e-9);
    ASSERT_FALSE(stressed.ratio_pass("CET1"));
    ASSERT_FALSE(stressed.overall_pass);
    TEST_END("Basel III stress scenario re-weights and recomputes");
}

void test_basel_empty_book_is_safe() {
    TEST("Basel III empty book yields zero ratios without crashing");
    BaselIIIEngine engine;
    ASSERT_NEAR(engine.compute_rwa(), 0.0, 1e-12);
    ASSERT_NEAR(engine.cet1_ratio(), 0.0, 1e-12);
    CapitalReport report = engine.assess();
    ASSERT_FALSE(report.overall_pass);
    TEST_END("Basel III empty book yields zero ratios without crashing");
}

// ============================================================================
// Task 6.3 - MiFID II Tests
// ============================================================================

void test_mifid_build_and_validate() {
    TEST("MiFID report built from Transaction + instrument metadata validates");
    MifidReportingEngine engine;
    Transaction tx = make_tx("TX-M1", 1000.0);

    InstrumentMeta meta("ACME Corp Ordinary Shares", "US0378331005", "USD", "XNAS");
    auto report = engine.build_report(tx, meta, 25.0, 40.0,
                                      "LEI-BUYER-001", "LEI-SELLER-001",
                                      TradingCapacity::AOTC);
    ASSERT_EQ(report.transaction_id, std::string("TX-M1"));
    ASSERT_EQ(report.isin, std::string("US0378331005"));
    ASSERT_EQ(report.currency, std::string("USD"));
    ASSERT_EQ(report.venue, std::string("XNAS"));
    ASSERT_EQ(report.trading_capacity, std::string("AOTC"));
    ASSERT_FALSE(report.execution_timestamp_iso8601.empty());
    ASSERT_TRUE(report.is_valid());
    ASSERT_TRUE(report.missing_fields().empty());
    TEST_END("MiFID report built from Transaction + instrument metadata validates");
}

void test_mifid_missing_fields_rejected() {
    TEST("MiFID report with missing required fields is rejected");
    MifidReportingEngine engine;

    MifidTransactionReport bad;   // Almost everything missing.
    bad.transaction_id = "TX-BAD";
    auto missing = bad.missing_fields();
    ASSERT_FALSE(missing.empty());
    ASSERT_FALSE(bad.is_valid());

    ASSERT_FALSE(engine.submit_report(bad));
    ASSERT_EQ(engine.rejected_count(), (uint64_t) 1);
    ASSERT_EQ(engine.pending_count(), (size_t) 0);

    // Zero/negative quantity and price are invalid.
    MifidTransactionReport bad2;
    bad2.transaction_id = "T"; bad2.instrument = "I"; bad2.isin = "X";
    bad2.quantity = 0.0; bad2.price = -1.0;
    bad2.currency = "USD"; bad2.buyer_id = "B"; bad2.seller_id = "S";
    bad2.venue = "XNAS"; bad2.trading_capacity = "DEAL";
    auto missing2 = bad2.missing_fields();
    ASSERT_TRUE(std::find(missing2.begin(), missing2.end(), "quantity") != missing2.end());
    ASSERT_TRUE(std::find(missing2.begin(), missing2.end(), "price") != missing2.end());
    TEST_END("MiFID report with missing required fields is rejected");
}

void test_mifid_serialize_roundtrip_and_batch() {
    TEST("MiFID serialization is deterministic and batches drain the queue");
    MifidReportingEngine engine;
    Transaction tx1 = make_tx("TX-B1", 100.0);
    Transaction tx2 = make_tx("TX-B2", 200.0);
    InstrumentMeta meta("Globex Bond 2030", "XS1234567890", "EUR", "XEUR");

    auto r1 = engine.build_report(tx1, meta, 10.0, 99.5, "B1", "S1", TradingCapacity::DEAL);
    auto r2 = engine.build_report(tx2, meta, 5.0, 100.25, "B2", "S2", TradingCapacity::MTCH);

    ASSERT_TRUE(engine.submit_report(r1));
    ASSERT_TRUE(engine.submit_report(r2));
    ASSERT_EQ(engine.pending_count(), (size_t) 2);

    // Deterministic serialization: identical report serializes identically.
    std::string s1a = MifidReportingEngine::serialize_report(r1);
    std::string s1b = MifidReportingEngine::serialize_report(r1);
    ASSERT_EQ(s1a, s1b);

    // Format: 11 pipe-delimited fields.
    size_t pipes = std::count(s1a.begin(), s1a.end(), '|');
    ASSERT_EQ(pipes, (size_t) 10);
    ASSERT_TRUE(s1a.find("TX-B1") != std::string::npos);
    ASSERT_TRUE(s1a.find("XS1234567890") != std::string::npos);
    ASSERT_TRUE(s1a.find("DEAL") != std::string::npos);

    std::string batch = engine.build_submission_batch();
    ASSERT_EQ(engine.pending_count(), (size_t) 0);   // Queue drained.
    ASSERT_TRUE(batch.find("#MIFID2-TSV-v1") == 0);
    ASSERT_TRUE(batch.find("#COUNT=2") != std::string::npos);
    ASSERT_TRUE(batch.find("TX-B1") != std::string::npos);
    ASSERT_TRUE(batch.find("TX-B2") != std::string::npos);

    // Record lines round-trip: re-serialized record content present in batch.
    ASSERT_TRUE(batch.find(s1a) != std::string::npos);
    TEST_END("MiFID serialization is deterministic and batches drain the queue");
}

void test_mifid_best_execution_monitoring() {
    TEST("MiFID best-execution flags deviation beyond bp threshold");
    MifidReportingEngine engine(10.0);  // 10 bps threshold

    // 5 bps deviation: fine.
    auto ok = engine.record_execution("TX-E1", "XNAS", 100.05, 100.0);
    ASSERT_NEAR(ok.deviation_bps, 5.0, 1e-6);
    ASSERT_FALSE(ok.breach);

    // 25 bps deviation: breach.
    auto bad = engine.record_execution("TX-E2", "XOFF", 100.25, 100.0);
    ASSERT_NEAR(bad.deviation_bps, 25.0, 1e-6);
    ASSERT_TRUE(bad.breach);

    auto records = engine.get_best_execution_records();
    ASSERT_EQ(records.size(), (size_t) 2);
    auto breaches = engine.get_breaches();
    ASSERT_EQ(breaches.size(), (size_t) 1);
    ASSERT_EQ(breaches[0].transaction_id, std::string("TX-E2"));
    TEST_END("MiFID best-execution flags deviation beyond bp threshold");
}

// ============================================================================
// Task 6.4 - GDPR Tests
// ============================================================================

void test_gdpr_consent_grant_withdraw() {
    TEST("GDPR consent grant/withdraw with last-event-wins semantics");
    GdprDataProtection gdpr;
    gdpr.register_subject("subject-1");

    ASSERT_FALSE(gdpr.has_consent("subject-1", "marketing"));
    gdpr.grant_consent("subject-1", "marketing");
    ASSERT_TRUE(gdpr.has_consent("subject-1", "marketing"));

    gdpr.withdraw_consent("subject-1", "marketing");
    ASSERT_FALSE(gdpr.has_consent("subject-1", "marketing"));

    // Independent purpose unaffected.
    gdpr.grant_consent("subject-1", "analytics");
    ASSERT_TRUE(gdpr.has_consent("subject-1", "analytics"));
    ASSERT_FALSE(gdpr.has_consent("subject-1", "marketing"));

    auto history = gdpr.get_consent_history("subject-1");
    ASSERT_EQ(history.size(), (size_t) 3);
    ASSERT_TRUE(history[0].granted);
    ASSERT_FALSE(history[1].granted);
    ASSERT_TRUE(history[2].granted);
    ASSERT_EQ(history[0].purpose, std::string("marketing"));
    TEST_END("GDPR consent grant/withdraw with last-event-wins semantics");
}

void test_gdpr_dsar_export_completeness() {
    TEST("GDPR DSAR export contains all held data for a subject");
    GdprDataProtection gdpr;
    gdpr.register_subject("subject-2");
    gdpr.add_data_category("subject-2", gdpr_categories::IDENTITY);
    gdpr.add_data_category("subject-2", gdpr_categories::CONTACT);
    gdpr.add_data_category("subject-2", gdpr_categories::FINANCIAL);
    gdpr.set_personal_data("subject-2", "full_name", "Jane Doe");
    gdpr.set_personal_data("subject-2", "email", "jane@example.com");
    gdpr.set_personal_data("subject-2", "account", "ACC-12345");
    gdpr.grant_consent("subject-2", "service-provisioning");

    DsarExport dsar = gdpr.process_dsar("subject-2");
    ASSERT_EQ(dsar.subject_id, std::string("subject-2"));
    ASSERT_EQ(dsar.data_categories.size(), (size_t) 3);
    ASSERT_TRUE(dsar.data_categories.count("identity") == 1);
    ASSERT_TRUE(dsar.data_categories.count("financial") == 1);
    ASSERT_EQ(dsar.personal_data.size(), (size_t) 3);
    ASSERT_EQ(dsar.personal_data.at("email"), std::string("jane@example.com"));
    ASSERT_EQ(dsar.consent_history.size(), (size_t) 1);
    ASSERT_FALSE(dsar.to_string().empty());

    // Unknown subject produces an empty (but well-formed) export.
    DsarExport empty = gdpr.process_dsar("ghost-subject");
    ASSERT_TRUE(empty.data_categories.empty());
    ASSERT_TRUE(empty.personal_data.empty());
    TEST_END("GDPR DSAR export contains all held data for a subject");
}

void test_gdpr_pseudonymization() {
    TEST("GDPR pseudonymization is deterministic and differs by subject");
    std::string salt = "deployment-secret-salt";
    std::string p1a = GdprDataProtection::pseudonymize("Jane Doe", "subject-1", salt);
    std::string p1b = GdprDataProtection::pseudonymize("Jane Doe", "subject-1", salt);
    std::string p2  = GdprDataProtection::pseudonymize("Jane Doe", "subject-2", salt);

    // Deterministic for the same subject+data+salt.
    ASSERT_EQ(p1a, p1b);
    // Differs across subjects for the same data.
    ASSERT_TRUE(p1a != p2);
    // Does not leak the raw value.
    ASSERT_TRUE(p1a.find("Jane Doe") == std::string::npos);
    ASSERT_TRUE(p1a.find("PSN:") == 0);

    // Anonymization is subject-independent and deterministic.
    std::string a1 = GdprDataProtection::anonymize("Jane Doe");
    std::string a2 = GdprDataProtection::anonymize("Jane Doe");
    std::string a3 = GdprDataProtection::anonymize("John Smith");
    ASSERT_EQ(a1, a2);
    ASSERT_TRUE(a1 != a3);
    ASSERT_TRUE(a1.find("ANON:") == 0);
    ASSERT_TRUE(a1.find("Jane Doe") == std::string::npos);
    TEST_END("GDPR pseudonymization is deterministic and differs by subject");
}

void test_gdpr_erasure_register_and_deadline() {
    TEST("GDPR erasure register tracks deadlines and completion");
    GdprDataProtection gdpr(std::chrono::hours(24 * 30));  // 30-day legal window
    gdpr.register_subject("subject-3");
    gdpr.add_data_category("subject-3", gdpr_categories::CONTACT);
    gdpr.set_personal_data("subject-3", "email", "s3@example.com");
    gdpr.grant_consent("subject-3", "service");

    std::string rid = gdpr.request_erasure("subject-3");
    ASSERT_FALSE(rid.empty());

    const ErasureRequest * req = gdpr.get_erasure_request(rid);
    ASSERT_TRUE(req != nullptr);
    ASSERT_EQ(req->status, ErasureStatus::PENDING);
    ASSERT_TRUE(req->legal_deadline > req->requested_at);

    // ~30 days remaining, definitely not overdue.
    double days_left = gdpr.days_until_deadline(rid);
    ASSERT_TRUE(days_left > 29.0 && days_left <= 30.0);
    ASSERT_TRUE(gdpr.get_overdue_requests().empty());

    // Completing erasure wipes personal data and categories.
    ASSERT_TRUE(gdpr.complete_erasure(rid));
    ASSERT_EQ(gdpr.get_erasure_request(rid)->status, ErasureStatus::COMPLETED);
    ASSERT_TRUE(gdpr.get_data_categories("subject-3").empty());
    DsarExport post = gdpr.process_dsar("subject-3");
    ASSERT_TRUE(post.personal_data.empty());

    // Double completion fails; unknown id fails.
    ASSERT_FALSE(gdpr.complete_erasure(rid));
    ASSERT_FALSE(gdpr.complete_erasure("ERS-9999"));
    TEST_END("GDPR erasure register tracks deadlines and completion");
}

void test_gdpr_erasure_overdue_and_rejection() {
    TEST("GDPR overdue erasure detection and rejection flow");
    // Zero-hour deadline: any pending request is immediately overdue.
    GdprDataProtection gdpr(std::chrono::hours(0));
    gdpr.register_subject("subject-4");
    std::string rid = gdpr.request_erasure("subject-4");

    auto overdue = gdpr.get_overdue_requests();
    ASSERT_EQ(overdue.size(), (size_t) 1);
    ASSERT_EQ(overdue[0].request_id, rid);
    ASSERT_TRUE(gdpr.days_until_deadline(rid) <= 0.0);

    // Rejection path.
    ASSERT_TRUE(gdpr.reject_erasure(rid, "legal hold: ongoing litigation"));
    ASSERT_EQ(gdpr.get_erasure_request(rid)->status, ErasureStatus::REJECTED);
    ASSERT_TRUE(gdpr.get_overdue_requests().empty());   // No longer pending.
    ASSERT_FALSE(gdpr.reject_erasure(rid, "again"));    // Not pending anymore.
    TEST_END("GDPR overdue erasure detection and rejection flow");
}

// ============================================================================
// Cross-module integration: SOX over the shared data plane
// ============================================================================

void test_integration_sox_trail_and_validator() {
    TEST("Integration: full-validation control + tamper evidence over shared trail");
    audit::ImmutableAuditTrail trail("COMPLIANCE_TEST_SIGNING_KEY");
    validation::TransactionValidator validator;
    SoxComplianceManager sox(trail, validator);

    // Record some transactions in the shared trail, then run the full control.
    std::vector<Transaction> txs = {make_tx("TX-I1", 10.0), make_tx("TX-I2", 20.0)};
    for (const auto & tx : txs) {
        trail.record_transaction(tx, "integration-test");
    }

    auto result = sox.run_control_test("full-validation", txs, "integration-test");
    ASSERT_TRUE(result.passed);

    // Trail integrity holds after all compliance activity.
    ASSERT_TRUE(sox.verify_tamper_evidence());

    // Statistics reflect both transaction and compliance entries.
    auto stats = trail.get_statistics();
    ASSERT_TRUE(stats.total_entries >= 3);   // 2 transactions + >=1 compliance event
    TEST_END("Integration: full-validation control + tamper evidence over shared trail");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Regulatory Compliance Engine Tests" << std::endl;
    std::cout << "  (Issue #006 - Tasks 6.1-6.4)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Task 6.1: SOX Compliance ---" << std::endl;
    test_sod_no_conflict_when_roles_compatible();
    test_sod_conflict_detected();
    test_sod_symmetric_and_static_check();
    test_change_management_workflow();
    test_change_management_reject_and_invalid_transitions();
    test_control_test_pass_and_audit_event();
    test_control_test_failure_recorded();
    test_tamper_evidence_verification();

    std::cout << "\n--- Task 6.2: Basel III Capital Requirements ---" << std::endl;
    test_basel_rwa_computation();
    test_basel_ratios_pass();
    test_basel_ratios_fail_at_thresholds();
    test_basel_custom_thresholds();
    test_basel_stress_scenario();
    test_basel_empty_book_is_safe();

    std::cout << "\n--- Task 6.3: MiFID II Transaction Reporting ---" << std::endl;
    test_mifid_build_and_validate();
    test_mifid_missing_fields_rejected();
    test_mifid_serialize_roundtrip_and_batch();
    test_mifid_best_execution_monitoring();

    std::cout << "\n--- Task 6.4: GDPR Data Protection ---" << std::endl;
    test_gdpr_consent_grant_withdraw();
    test_gdpr_dsar_export_completeness();
    test_gdpr_pseudonymization();
    test_gdpr_erasure_register_and_deadline();
    test_gdpr_erasure_overdue_and_rejection();

    std::cout << "\n--- Cross-Module Integration ---" << std::endl;
    test_integration_sox_trail_and_validator();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
