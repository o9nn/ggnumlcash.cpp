#include "flow-analytics.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::flow;

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

// Helper to build a transaction.
static AuditTransaction make_tx(const std::string & id, const std::string & src,
                                const std::string & dst, double amount,
                                int64_t ts) {
    AuditTransaction tx;
    tx.transaction_id = id;
    tx.source_entity = src;
    tx.dest_entity = dst;
    tx.amount = amount;
    tx.currency = "USD";
    tx.timestamp = ts;
    return tx;
}

// ============================================================================
// FlowGraph Construction Tests
// ============================================================================

void test_flow_graph_aggregation() {
    TEST("Flow graph aggregates edges");
    FlowGraph g;
    std::vector<AuditTransaction> txs = {
        make_tx("t1", "A", "B", 100.0, 1000),
        make_tx("t2", "A", "B", 50.0, 2000),
        make_tx("t3", "B", "C", 75.0, 3000),
    };
    g.build(txs);

    ASSERT_EQ(g.entity_count(), (size_t)3);
    ASSERT_EQ(g.edge_count(), (size_t)2);

    auto * ab = g.get_edge("A", "B");
    ASSERT_TRUE(ab != nullptr);
    ASSERT_NEAR(ab->total_amount, 150.0, 1e-9);
    ASSERT_EQ(ab->transaction_count, (uint64_t)2);
    ASSERT_EQ(ab->first_timestamp, (int64_t)1000);
    ASSERT_EQ(ab->last_timestamp, (int64_t)2000);
    TEST_END("Flow graph aggregates edges");
}

void test_flow_graph_inflow_outflow() {
    TEST("Flow graph inflow/outflow totals");
    FlowGraph g;
    g.build({make_tx("t1", "A", "B", 100.0, 1000),
             make_tx("t2", "A", "C", 60.0, 1000),
             make_tx("t3", "D", "B", 40.0, 1000)});

    ASSERT_NEAR(g.total_outflow("A"), 160.0, 1e-9);
    ASSERT_NEAR(g.total_inflow("B"), 140.0, 1e-9);
    ASSERT_EQ(g.out_degree("A"), (uint64_t)2);
    ASSERT_EQ(g.in_degree("B"), (uint64_t)2);
    ASSERT_EQ(g.in_degree("A"), (uint64_t)0);
    TEST_END("Flow graph inflow/outflow totals");
}

void test_flow_graph_time_window() {
    TEST("Flow graph respects time window");
    FlowGraph g;
    std::vector<AuditTransaction> txs = {
        make_tx("t1", "A", "B", 100.0, 1000),
        make_tx("t2", "A", "B", 100.0, 5000),
        make_tx("t3", "A", "B", 100.0, 9000),
    };
    g.build(txs, 2000, 8000);  // only t2 in window
    auto * ab = g.get_edge("A", "B");
    ASSERT_TRUE(ab != nullptr);
    ASSERT_EQ(ab->transaction_count, (uint64_t)1);
    ASSERT_NEAR(ab->total_amount, 100.0, 1e-9);
    TEST_END("Flow graph respects time window");
}

void test_flow_ranks_left_to_right() {
    TEST("Flow ranks order sources left, sinks right");
    FlowGraph g;
    // A -> B -> C, A -> C
    g.build({make_tx("t1", "A", "B", 1, 1),
             make_tx("t2", "B", "C", 1, 2),
             make_tx("t3", "A", "C", 1, 3)});
    auto ranks = g.compute_flow_ranks();
    ASSERT_TRUE(ranks["A"] < ranks["B"]);
    ASSERT_TRUE(ranks["B"] < ranks["C"]);
    ASSERT_EQ(ranks["A"], 0);  // pure source
    TEST_END("Flow ranks order sources left, sinks right");
}

void test_fan_out_fan_in_detection() {
    TEST("Fan-out and fan-in detection");
    FlowGraph g;
    // Hub H fans out to many; sink S gathers from many.
    std::vector<AuditTransaction> txs;
    for (int i = 0; i < 5; i++) {
        txs.push_back(make_tx("o" + std::to_string(i), "H", "d" + std::to_string(i), 10.0, 100));
        txs.push_back(make_tx("i" + std::to_string(i), "s" + std::to_string(i), "S", 20.0, 100));
    }
    g.build(txs);

    auto fan_out = g.detect_fan_out(4, 0.0);
    ASSERT_TRUE(std::find(fan_out.begin(), fan_out.end(), "H") != fan_out.end());

    auto fan_in = g.detect_fan_in(4, 0.0);
    ASSERT_TRUE(std::find(fan_in.begin(), fan_in.end(), "S") != fan_in.end());
    TEST_END("Fan-out and fan-in detection");
}

// ============================================================================
// String Similarity Tests
// ============================================================================

void test_levenshtein() {
    TEST("Levenshtein similarity");
    ASSERT_NEAR(levenshtein_similarity("kitten", "kitten"), 1.0, 1e-9);
    ASSERT_TRUE(levenshtein_similarity("kitten", "sitting") < 1.0);
    ASSERT_NEAR(levenshtein_similarity("", ""), 1.0, 1e-9);
    ASSERT_NEAR(levenshtein_similarity("abc", ""), 0.0, 1e-9);
    TEST_END("Levenshtein similarity");
}

void test_jaro_winkler() {
    TEST("Jaro-Winkler similarity");
    ASSERT_NEAR(jaro_winkler_similarity("MARTHA", "MARHTA"), 0.961, 0.01);
    ASSERT_TRUE(jaro_winkler_similarity("DIXON", "DICKSONX") >
                jaro_similarity("DIXON", "DICKSONX"));  // prefix boost
    ASSERT_NEAR(jaro_similarity("abc", "abc"), 1.0, 1e-9);
    TEST_END("Jaro-Winkler similarity");
}

void test_address_normalization() {
    TEST("Address normalization");
    ASSERT_EQ(normalize_address("123 Main St."), normalize_address("123 main street"));
    ASSERT_EQ(normalize_address("5  OAK   AVE"), normalize_address("5 oak avenue"));
    ASSERT_TRUE(normalize_address("Apt 4, 10 Elm Rd.").find("apartment") != std::string::npos);
    TEST_END("Address normalization");
}

// ============================================================================
// Entity Resolution Tests
// ============================================================================

void test_registration_match_decisive() {
    TEST("Exact registration number match is decisive");
    EntityResolver resolver;
    AuditEntity a("e1", "ABC Holdings", EntityType::COMPANY);
    AuditEntity b("e2", "Totally Different Name", EntityType::COMPANY);
    a.registration_number = "2020/123456/07";
    b.registration_number = "2020/123456/07";
    ASSERT_NEAR(resolver.match_score(a, b), 1.0, 1e-9);
    TEST_END("Exact registration number match is decisive");
}

void test_name_similarity_resolution() {
    TEST("Similar names resolve into one cluster");
    EntityResolver resolver;
    std::vector<AuditEntity> entities = {
        AuditEntity("e1", "Fincosys (Pty) Ltd", EntityType::COMPANY),
        AuditEntity("e2", "Fincosys Pty Ltd", EntityType::COMPANY),
        AuditEntity("e3", "Completely Unrelated Corp", EntityType::COMPANY),
    };
    auto clusters = resolver.resolve(entities);

    // e1 and e2 should cluster; e3 separate
    ASSERT_EQ(clusters.size(), (size_t)2);
    bool found_pair = false;
    for (const auto & c : clusters) {
        if (c.member_ids.size() == 2) {
            found_pair = true;
            ASSERT_TRUE(std::find(c.member_ids.begin(), c.member_ids.end(), "e1") != c.member_ids.end());
            ASSERT_TRUE(std::find(c.member_ids.begin(), c.member_ids.end(), "e2") != c.member_ids.end());
        }
    }
    ASSERT_TRUE(found_pair);
    TEST_END("Similar names resolve into one cluster");
}

void test_distinct_entities_separate_clusters() {
    TEST("Distinct entities stay in separate clusters");
    EntityResolver resolver;
    std::vector<AuditEntity> entities = {
        AuditEntity("a", "Alpha Corp", EntityType::COMPANY),
        AuditEntity("b", "Beta Industries", EntityType::COMPANY),
        AuditEntity("c", "Gamma Holdings", EntityType::COMPANY),
    };
    auto clusters = resolver.resolve(entities);
    ASSERT_EQ(clusters.size(), (size_t)3);
    TEST_END("Distinct entities stay in separate clusters");
}

void test_transitive_resolution() {
    TEST("Transitive resolution merges chains");
    EntityMatchConfig cfg;
    cfg.match_threshold = 0.75;
    EntityResolver resolver(cfg);
    std::vector<AuditEntity> entities = {
        AuditEntity("x", "Smith John", EntityType::PERSON),
        AuditEntity("y", "Smith Jon", EntityType::PERSON),
        AuditEntity("z", "Smyth John", EntityType::PERSON),
    };
    auto clusters = resolver.resolve(entities);
    // All three should collapse into a single cluster
    ASSERT_EQ(clusters.size(), (size_t)1);
    ASSERT_EQ(clusters[0].member_ids.size(), (size_t)3);
    TEST_END("Transitive resolution merges chains");
}

// ============================================================================
// Community Detection Tests
// ============================================================================

void test_community_detection_two_clusters() {
    TEST("Community detection separates two dense groups");
    FlowGraph g;
    std::vector<AuditTransaction> txs;
    // Dense group 1: A,B,C fully interconnected with large amounts
    txs.push_back(make_tx("1", "A", "B", 100, 1));
    txs.push_back(make_tx("2", "B", "C", 100, 2));
    txs.push_back(make_tx("3", "A", "C", 100, 3));
    // Dense group 2: X,Y,Z
    txs.push_back(make_tx("4", "X", "Y", 100, 4));
    txs.push_back(make_tx("5", "Y", "Z", 100, 5));
    txs.push_back(make_tx("6", "X", "Z", 100, 6));
    // Weak bridge between groups
    txs.push_back(make_tx("7", "C", "X", 1, 7));
    g.build(txs);

    auto comm = detect_communities(g);
    ASSERT_EQ(comm.size(), (size_t)6);
    // Within-group members share a community
    ASSERT_EQ(comm["A"], comm["B"]);
    ASSERT_EQ(comm["B"], comm["C"]);
    ASSERT_EQ(comm["X"], comm["Y"]);
    ASSERT_EQ(comm["Y"], comm["Z"]);
    // Across groups differ
    ASSERT_TRUE(comm["A"] != comm["X"]);
    TEST_END("Community detection separates two dense groups");
}

void test_community_detection_empty() {
    TEST("Community detection on empty graph");
    FlowGraph g;
    g.build({});
    auto comm = detect_communities(g);
    ASSERT_TRUE(comm.empty());
    TEST_END("Community detection on empty graph");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Flow Analytics & Entity Resolution Tests" << std::endl;
    std::cout << "  (Phase B.1 / B.2)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Flow Graph ---" << std::endl;
    test_flow_graph_aggregation();
    test_flow_graph_inflow_outflow();
    test_flow_graph_time_window();
    test_flow_ranks_left_to_right();
    test_fan_out_fan_in_detection();

    std::cout << "\n--- String Similarity ---" << std::endl;
    test_levenshtein();
    test_jaro_winkler();
    test_address_normalization();

    std::cout << "\n--- Entity Resolution ---" << std::endl;
    test_registration_match_decisive();
    test_name_similarity_resolution();
    test_distinct_entities_separate_clusters();
    test_transitive_resolution();

    std::cout << "\n--- Community Detection ---" << std::endl;
    test_community_detection_two_clusters();
    test_community_detection_empty();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
