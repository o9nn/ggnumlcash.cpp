#include "financial-tensors.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::tensors;

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
// Statistical Primitive Tests
// ============================================================================

void test_normal_pdf() {
    TEST("normal_pdf values");
    ASSERT_NEAR(normal_pdf(0.0), 0.3989423, 1e-6);
    ASSERT_NEAR(normal_pdf(1.0), 0.2419707, 1e-6);
    ASSERT_NEAR(normal_pdf(-1.0), normal_pdf(1.0), 1e-12);
    TEST_END("normal_pdf values");
}

void test_normal_cdf() {
    TEST("normal_cdf values");
    ASSERT_NEAR(normal_cdf(0.0), 0.5, 1e-9);
    ASSERT_NEAR(normal_cdf(1.96), 0.9750021, 1e-6);
    ASSERT_NEAR(normal_cdf(-1.96), 0.0249979, 1e-6);
    ASSERT_NEAR(normal_cdf(6.0), 1.0, 1e-8);
    ASSERT_NEAR(normal_cdf(-6.0), 0.0, 1e-8);
    TEST_END("normal_cdf values");
}

void test_normal_inv_cdf() {
    TEST("normal_inv_cdf round-trip and quantiles");
    ASSERT_NEAR(normal_inv_cdf(0.5), 0.0, 1e-9);
    ASSERT_NEAR(normal_inv_cdf(0.975), 1.959964, 1e-4);
    // Round-trip
    ASSERT_NEAR(normal_cdf(normal_inv_cdf(0.13)), 0.13, 1e-8);
    ASSERT_NEAR(normal_cdf(normal_inv_cdf(0.99)), 0.99, 1e-8);
    TEST_END("normal_inv_cdf round-trip and quantiles");
}

void test_mean_variance_stddev() {
    TEST("mean / variance / stddev");
    std::vector<double> data = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
    ASSERT_NEAR(mean(data.data(), data.size()), 5.0, 1e-12);
    ASSERT_NEAR(variance(data.data(), data.size()), 4.571428571, 1e-6);
    ASSERT_NEAR(stddev(data.data(), data.size()), 2.1380899, 1e-6);
    TEST_END("mean / variance / stddev");
}

// ============================================================================
// Black-Scholes Tests (reference values)
// ============================================================================

void test_black_scholes_call() {
    TEST("Black-Scholes call price (reference value)");
    // Classic textbook example: S=100, K=100, T=1, sigma=0.2, r=0.05
    auto res = black_scholes(100.0, 100.0, 1.0, 0.2, 0.05, OptionType::CALL);
    ASSERT_NEAR(res.price, 10.4506, 1e-3);
    ASSERT_NEAR(res.delta, 0.6368, 1e-3);
    ASSERT_TRUE(res.gamma > 0.0);
    ASSERT_TRUE(res.vega > 0.0);
    TEST_END("Black-Scholes call price (reference value)");
}

void test_black_scholes_put() {
    TEST("Black-Scholes put price (reference value)");
    auto res = black_scholes(100.0, 100.0, 1.0, 0.2, 0.05, OptionType::PUT);
    ASSERT_NEAR(res.price, 5.5735, 1e-3);
    ASSERT_NEAR(res.delta, -0.3632, 1e-3);
    TEST_END("Black-Scholes put price (reference value)");
}

void test_put_call_parity() {
    TEST("Put-call parity holds");
    double S = 100.0, K = 105.0, T = 0.5, sig = 0.25, r = 0.03;
    auto call = black_scholes(S, K, T, sig, r, OptionType::CALL);
    auto put = black_scholes(S, K, T, sig, r, OptionType::PUT);
    // C - P = S - K*e^{-rT}
    double lhs = call.price - put.price;
    double rhs = S - K * std::exp(-r * T);
    ASSERT_NEAR(lhs, rhs, 1e-8);
    TEST_END("Put-call parity holds");
}

void test_black_scholes_at_expiry() {
    TEST("Black-Scholes at expiry equals intrinsic value");
    auto itm = black_scholes(110.0, 100.0, 0.0, 0.2, 0.05, OptionType::CALL);
    ASSERT_NEAR(itm.price, 10.0, 1e-9);
    auto otm_put = black_scholes(110.0, 100.0, 0.0, 0.2, 0.05, OptionType::PUT);
    ASSERT_NEAR(otm_put.price, 0.0, 1e-9);
    TEST_END("Black-Scholes at expiry equals intrinsic value");
}

void test_black_scholes_batch() {
    TEST("Black-Scholes batch matches scalar");
    const size_t n = 4;
    double spots[n] = {100.0, 95.0, 105.0, 100.0};
    double strikes[n] = {100.0, 100.0, 100.0, 110.0};
    double times[n] = {1.0, 0.5, 0.25, 1.0};
    double vols[n] = {0.2, 0.25, 0.3, 0.2};
    int is_call[n] = {1, 0, 1, 0};
    std::vector<BlackScholesResult> out(n);

    black_scholes_batch(spots, strikes, times, vols, 0.05, is_call, n, out.data());

    for (size_t i = 0; i < n; i++) {
        auto scalar = black_scholes(spots[i], strikes[i], times[i], vols[i],
                                    0.05, is_call[i] ? OptionType::CALL : OptionType::PUT);
        ASSERT_NEAR(out[i].price, scalar.price, 1e-12);
        ASSERT_NEAR(out[i].delta, scalar.delta, 1e-12);
    }
    TEST_END("Black-Scholes batch matches scalar");
}

// ============================================================================
// Binomial Tree Tests
// ============================================================================

void test_binomial_converges_to_bs() {
    TEST("Binomial European converges to Black-Scholes");
    double S = 100.0, K = 100.0, T = 1.0, sig = 0.2, r = 0.05;
    double bs = black_scholes(S, K, T, sig, r, OptionType::CALL).price;
    double tree = binomial_option_price(S, K, T, sig, r, OptionType::CALL, 400, false);
    ASSERT_NEAR(tree, bs, 0.05);  // CRR converges to within a few cents
    TEST_END("Binomial European converges to Black-Scholes");
}

void test_american_put_ge_european() {
    TEST("American put >= European put (early exercise premium)");
    double S = 90.0, K = 100.0, T = 1.0, sig = 0.3, r = 0.05;
    double euro = binomial_option_price(S, K, T, sig, r, OptionType::PUT, 100, false);
    double amer = binomial_option_price(S, K, T, sig, r, OptionType::PUT, 100, true);
    ASSERT_TRUE(amer >= euro - 1e-9);
    TEST_END("American put >= European put (early exercise premium)");
}

// ============================================================================
// Monte Carlo Tests
// ============================================================================

void test_monte_carlo_basic() {
    TEST("Monte Carlo risk statistics sane");
    auto res = monte_carlo_risk(100000.0, 0.05, 0.2, 1.0, 20000, 0.95, 42);
    ASSERT_EQ(res.num_paths, (uint64_t)20000);
    // Expected terminal value ~ 100000 * e^{0.05} ~ 105127
    ASSERT_NEAR(res.mean_terminal_value, 105127.0, 3000.0);
    ASSERT_TRUE(res.std_dev > 0.0);
    ASSERT_TRUE(res.value_at_risk > 0.0);
    ASSERT_TRUE(res.expected_shortfall >= res.value_at_risk - 1e-6);
    ASSERT_TRUE(res.min_value < res.mean_terminal_value);
    ASSERT_TRUE(res.max_value > res.mean_terminal_value);
    TEST_END("Monte Carlo risk statistics sane");
}

void test_monte_carlo_reproducible() {
    TEST("Monte Carlo reproducible with seed");
    auto a = monte_carlo_risk(50000.0, 0.04, 0.15, 0.5, 5000, 0.99, 123);
    auto b = monte_carlo_risk(50000.0, 0.04, 0.15, 0.5, 5000, 0.99, 123);
    ASSERT_NEAR(a.mean_terminal_value, b.mean_terminal_value, 1e-9);
    ASSERT_NEAR(a.value_at_risk, b.value_at_risk, 1e-9);
    TEST_END("Monte Carlo reproducible with seed");
}

void test_monte_carlo_terminal_values_out() {
    TEST("Monte Carlo writes terminal values buffer");
    const uint64_t paths = 1000;
    std::vector<double> terminal(paths, 0.0);
    monte_carlo_risk(10000.0, 0.05, 0.2, 1.0, paths, 0.95, 7, terminal.data());
    bool all_zero = true;
    for (double v : terminal) {
        if (v != 0.0) { all_zero = false; break; }
    }
    ASSERT_FALSE(all_zero);
    for (double v : terminal) {
        ASSERT_TRUE(v > 0.0);  // GBM terminal values are always positive
    }
    TEST_END("Monte Carlo writes terminal values buffer");
}

// ============================================================================
// Portfolio / Linear Algebra Tests
// ============================================================================

void test_dot_product() {
    TEST("dot product");
    double a[] = {1.0, 2.0, 3.0};
    double b[] = {4.0, 5.0, 6.0};
    ASSERT_NEAR(dot(a, b, 3), 32.0, 1e-12);
    TEST_END("dot product");
}

void test_matvec() {
    TEST("matrix-vector product");
    // A = [[1,2],[3,4]], x = [1,1] -> y = [3,7]
    double A[] = {1.0, 2.0, 3.0, 4.0};
    double x[] = {1.0, 1.0};
    double y[2] = {0.0, 0.0};
    matvec(A, x, y, 2, 2);
    ASSERT_NEAR(y[0], 3.0, 1e-12);
    ASSERT_NEAR(y[1], 7.0, 1e-12);
    TEST_END("matrix-vector product");
}

void test_portfolio_metrics() {
    TEST("portfolio expected return and volatility");
    double weights[] = {0.5, 0.5};
    double returns[] = {0.10, 0.20};
    // Covariance: var1=0.04, var2=0.09, cov=0.0
    double cov[] = {0.04, 0.0, 0.0, 0.09};

    ASSERT_NEAR(portfolio_expected_return(weights, returns, 2), 0.15, 1e-12);
    // variance = 0.25*0.04 + 0.25*0.09 = 0.0325
    ASSERT_NEAR(portfolio_variance(weights, cov, 2), 0.0325, 1e-9);
    ASSERT_NEAR(portfolio_volatility(weights, cov, 2), std::sqrt(0.0325), 1e-9);
    TEST_END("portfolio expected return and volatility");
}

void test_covariance_matrix() {
    TEST("covariance matrix symmetric and diagonal = variance");
    // Two assets, four observations
    double rets[] = {0.01, 0.02, -0.01, 0.00,    // asset 0
                     0.02, 0.01, 0.00, -0.01};   // asset 1
    double cov[4] = {0.0, 0.0, 0.0, 0.0};
    covariance_matrix(rets, 2, 4, cov);

    // Symmetry
    ASSERT_NEAR(cov[1], cov[2], 1e-12);
    // Diagonal positive
    ASSERT_TRUE(cov[0] > 0.0);
    ASSERT_TRUE(cov[3] > 0.0);
    // Diagonal equals sample variance of each series
    ASSERT_NEAR(cov[0], variance(rets, 4), 1e-12);
    ASSERT_NEAR(cov[3], variance(rets + 4, 4), 1e-12);
    TEST_END("covariance matrix symmetric and diagonal = variance");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Financial Tensor Kernel Tests" << std::endl;
    std::cout << "  (Phase 1, Issue #002 - Task 2.1)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Statistical Primitives ---" << std::endl;
    test_normal_pdf();
    test_normal_cdf();
    test_normal_inv_cdf();
    test_mean_variance_stddev();

    std::cout << "\n--- Black-Scholes ---" << std::endl;
    test_black_scholes_call();
    test_black_scholes_put();
    test_put_call_parity();
    test_black_scholes_at_expiry();
    test_black_scholes_batch();

    std::cout << "\n--- Binomial Trees ---" << std::endl;
    test_binomial_converges_to_bs();
    test_american_put_ge_european();

    std::cout << "\n--- Monte Carlo ---" << std::endl;
    test_monte_carlo_basic();
    test_monte_carlo_reproducible();
    test_monte_carlo_terminal_values_out();

    std::cout << "\n--- Portfolio / Linear Algebra ---" << std::endl;
    test_dot_product();
    test_matvec();
    test_portfolio_metrics();
    test_covariance_matrix();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
