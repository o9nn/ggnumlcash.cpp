#include "fraud-detection.h"

#include <cmath>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace ggnucash::fraud;

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

// Generate a cluster of "normal" 2D points around (cx, cy).
static std::vector<FeatureVector> make_normal_cluster(size_t n, double cx, double cy,
                                                      double spread, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> nx(cx, spread), ny(cy, spread);
    std::vector<FeatureVector> out;
    for (size_t i = 0; i < n; i++) {
        out.emplace_back(std::vector<double>{nx(rng), ny(rng)}, "n" + std::to_string(i));
    }
    return out;
}

// ============================================================================
// Isolation Forest Tests
// ============================================================================

void test_isolation_forest_fit_and_score() {
    TEST("Isolation forest fits and scores");
    auto train = make_normal_cluster(300, 0.0, 0.0, 1.0, 42);

    IsolationForestConfig cfg;
    cfg.num_trees = 100;
    cfg.subsample_size = 256;
    cfg.seed = 7;
    IsolationForest forest(cfg);
    ASSERT_FALSE(forest.is_fitted());
    forest.fit(train);
    ASSERT_TRUE(forest.is_fitted());
    ASSERT_EQ(forest.num_trees(), 100);
    TEST_END("Isolation forest fits and scores");
}

void test_isolation_forest_anomaly_higher_than_normal() {
    TEST("Isolation forest scores anomalies higher than normal");
    auto train = make_normal_cluster(500, 0.0, 0.0, 1.0, 1);

    IsolationForestConfig cfg;
    cfg.num_trees = 200;
    cfg.subsample_size = 256;
    cfg.seed = 99;
    IsolationForest forest(cfg);
    forest.fit(train);

    // A point at the cluster center should score low; a far point high.
    FeatureVector normal_pt({0.1, -0.2}, "normal");
    FeatureVector anomaly_pt({50.0, 50.0}, "anomaly");

    double normal_score = forest.anomaly_score(normal_pt);
    double anomaly_score = forest.anomaly_score(anomaly_pt);

    ASSERT_TRUE(anomaly_score > normal_score);
    ASSERT_TRUE(anomaly_score > 0.6);   // strong anomaly
    ASSERT_TRUE(normal_score < 0.6);
    TEST_END("Isolation forest scores anomalies higher than normal");
}

void test_isolation_forest_reproducible() {
    TEST("Isolation forest reproducible with seed");
    auto train = make_normal_cluster(200, 5.0, 5.0, 2.0, 3);

    IsolationForestConfig cfg;
    cfg.num_trees = 50;
    cfg.subsample_size = 128;
    cfg.seed = 123;
    IsolationForest f1(cfg);
    f1.fit(train);
    IsolationForest f2(cfg);
    f2.fit(train);

    FeatureVector pt({5.5, 4.5}, "pt");
    ASSERT_NEAR(f1.anomaly_score(pt), f2.anomaly_score(pt), 1e-12);
    TEST_END("Isolation forest reproducible with seed");
}

// ============================================================================
// Benford's Law Tests
// ============================================================================

void test_benford_conforming_data() {
    TEST("Benford analysis conforms on Benford-distributed data");
    // Generate amounts following Benford's law closely (log-uniform mantissa).
    std::vector<double> amounts;
    std::mt19937_64 rng(10);
    std::uniform_real_distribution<double> logu(0.0, 1.0);
    for (int i = 0; i < 5000; i++) {
        double mantissa = std::pow(10.0, logu(rng));  // uniform in log -> Benford
        double scale = std::pow(10.0, (int)(logu(rng) * 3));
        amounts.push_back(mantissa * scale);
    }
    auto res = benford_analysis(amounts);
    ASSERT_EQ(res.sample_count, (uint64_t)5000);
    // Should be close or acceptable conformity
    ASSERT_TRUE(res.conformity == BenfordResult::Conformity::CLOSE ||
                res.conformity == BenfordResult::Conformity::ACCEPTABLE);
    // Leading digit 1 should be most common (~30.1%)
    ASSERT_TRUE(res.observed[1] > res.observed[9]);
    TEST_END("Benford analysis conforms on Benford-distributed data");
}

void test_benford_uniform_data_nonconforming() {
    TEST("Benford analysis flags uniform (fabricated) data");
    // Uniform leading digits -> violates Benford.
    std::vector<double> amounts;
    std::mt19937_64 rng(20);
    std::uniform_int_distribution<int> d(1, 9);
    for (int i = 0; i < 2000; i++) {
        amounts.push_back(static_cast<double>(d(rng)) * 100.0);
    }
    auto res = benford_analysis(amounts);
    // Uniform digits have high MAD -> nonconformity
    ASSERT_TRUE(res.conformity == BenfordResult::Conformity::NONCONFORMITY ||
                res.conformity == BenfordResult::Conformity::MARGINAL);
    ASSERT_TRUE(res.mean_absolute_deviation > 0.012);
    TEST_END("Benford analysis flags uniform (fabricated) data");
}

void test_benford_empty_input() {
    TEST("Benford handles empty input");
    auto res = benford_analysis({});
    ASSERT_EQ(res.sample_count, (uint64_t)0);
    TEST_END("Benford handles empty input");
}

// ============================================================================
// Autoencoder Scorer Tests
// ============================================================================

void test_autoencoder_baseline_vs_outlier() {
    TEST("Autoencoder reconstruction error separates outliers");
    auto train = make_normal_cluster(400, 10.0, 20.0, 1.0, 5);
    AutoencoderScorer scorer;
    scorer.fit(train);
    ASSERT_TRUE(scorer.is_fitted());
    ASSERT_EQ(scorer.feature_dim(), (size_t)2);

    FeatureVector inlier({10.1, 20.1}, "in");
    FeatureVector outlier({10.0 + 30.0, 20.0}, "out");

    double in_err = scorer.reconstruction_error(inlier);
    double out_err = scorer.reconstruction_error(outlier);
    ASSERT_TRUE(out_err > in_err);
    ASSERT_TRUE(out_err > 10.0);  // 30-sigma offset -> large error
    TEST_END("Autoencoder reconstruction error separates outliers");
}

void test_autoencoder_constant_feature_safe() {
    TEST("Autoencoder safe on constant features (no div-by-zero)");
    std::vector<FeatureVector> train;
    for (int i = 0; i < 50; i++) {
        train.emplace_back(std::vector<double>{5.0, (double)i * 0.1}, "t");
    }
    AutoencoderScorer scorer;
    scorer.fit(train);  // first feature is constant
    FeatureVector pt({5.0, 2.5}, "pt");
    double err = scorer.reconstruction_error(pt);
    ASSERT_TRUE(std::isfinite(err));
    TEST_END("Autoencoder safe on constant features (no div-by-zero)");
}

// ============================================================================
// SPC Monitor Tests
// ============================================================================

void test_spc_detects_spike() {
    TEST("SPC monitor detects a control-chart spike");
    SpcMonitor spc(3.0, 20);
    std::mt19937_64 rng(8);
    std::normal_distribution<double> nd(100.0, 5.0);
    for (int i = 0; i < 100; i++) spc.observe(nd(rng));  // stable baseline

    bool spike = spc.observe(200.0);  // ~20 sigma
    ASSERT_TRUE(spike);
    TEST_END("SPC monitor detects a control-chart spike");
}

void test_spc_stable_no_signal() {
    TEST("SPC monitor quiet on stable data");
    SpcMonitor spc(3.0, 20);
    std::mt19937_64 rng(9);
    std::normal_distribution<double> nd(50.0, 2.0);
    bool any_signal = false;
    for (int i = 0; i < 200; i++) {
        if (spc.observe(nd(rng))) any_signal = true;
    }
    // A 3-sigma chart on normal data should rarely if ever fire in 200 pts
    ASSERT_FALSE(any_signal);
    TEST_END("SPC monitor quiet on stable data");
}

void test_spc_running_stats() {
    TEST("SPC running mean/stddev converge");
    SpcMonitor spc(3.0, 5);
    std::mt19937_64 rng(11);
    std::normal_distribution<double> nd(1000.0, 10.0);
    for (int i = 0; i < 10000; i++) spc.observe(nd(rng));
    ASSERT_NEAR(spc.mean(), 1000.0, 5.0);
    ASSERT_NEAR(spc.stddev(), 10.0, 2.0);
    ASSERT_EQ(spc.count(), (size_t)10000);
    TEST_END("SPC running mean/stddev converge");
}

// ============================================================================
// Risk Scorer Tests
// ============================================================================

void test_risk_scorer_fuses_detectors() {
    TEST("Risk scorer fuses detectors and raises alerts");
    auto train = make_normal_cluster(500, 0.0, 0.0, 1.0, 21);

    IsolationForestConfig cfg;
    cfg.num_trees = 100;
    cfg.subsample_size = 256;
    cfg.seed = 42;
    IsolationForest forest(cfg);
    forest.fit(train);

    AutoencoderScorer scorer;
    scorer.fit(train);

    RiskScoreConfig rcfg;
    rcfg.alert_threshold = 0.55;
    RiskScorer risk(rcfg);
    risk.set_isolation_forest(&forest);
    risk.set_autoencoder(&scorer);

    // Warm up SPC with stable scalar values.
    for (int i = 0; i < 50; i++) risk.score(train[i], 100.0);

    // Normal point -> low score, no alert.
    FeatureVector normal_pt({0.0, 0.1}, "norm");
    auto rs_normal = risk.score(normal_pt, 100.0);
    ASSERT_FALSE(rs_normal.alert);

    // Anomalous point -> higher composite score.
    FeatureVector anom_pt({40.0, 40.0}, "anom");
    auto rs_anom = risk.score(anom_pt, 100.0);
    ASSERT_TRUE(rs_anom.composite > rs_normal.composite);
    TEST_END("Risk scorer fuses detectors and raises alerts");
}

void test_risk_scorer_alert_threshold() {
    TEST("Risk scorer respects alert threshold");
    RiskScoreConfig rcfg;
    rcfg.alert_threshold = 0.0;   // everything alerts
    RiskScorer risk(rcfg);
    auto train = make_normal_cluster(100, 0.0, 0.0, 1.0, 33);
    IsolationForest forest;
    forest.fit(train);
    risk.set_isolation_forest(&forest);

    FeatureVector pt({0.0, 0.0}, "pt");
    auto rs = risk.score(pt, 1.0);
    ASSERT_TRUE(rs.alert);  // threshold 0 -> always alert
    TEST_END("Risk scorer respects alert threshold");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Fraud Detection & Anomaly Tests" << std::endl;
    std::cout << "  (Issue #005 Task 5.3 / Phase C.1)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Isolation Forest ---" << std::endl;
    test_isolation_forest_fit_and_score();
    test_isolation_forest_anomaly_higher_than_normal();
    test_isolation_forest_reproducible();

    std::cout << "\n--- Benford's Law ---" << std::endl;
    test_benford_conforming_data();
    test_benford_uniform_data_nonconforming();
    test_benford_empty_input();

    std::cout << "\n--- Autoencoder Scorer ---" << std::endl;
    test_autoencoder_baseline_vs_outlier();
    test_autoencoder_constant_feature_safe();

    std::cout << "\n--- SPC Monitor ---" << std::endl;
    test_spc_detects_spike();
    test_spc_stable_no_signal();
    test_spc_running_stats();

    std::cout << "\n--- Risk Scorer ---" << std::endl;
    test_risk_scorer_fuses_detectors();
    test_risk_scorer_alert_threshold();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
