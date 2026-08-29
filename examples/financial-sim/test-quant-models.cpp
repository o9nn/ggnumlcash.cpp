#include "quant-models.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::quant;
using ggnucash::tensors::OptionType;

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

namespace ts = ggnucash::tensors;

// ============================================================================
// Task 4.1 - Trinomial Tree Tests
// ============================================================================

void test_trinomial_converges_to_bs() {
    TEST("trinomial tree converges to Black-Scholes");
    double S = 100.0, K = 100.0, T = 1.0, sig = 0.2, r = 0.05;
    double tri_call = trinomial_option_price(S, K, T, sig, r, OptionType::CALL, 200, false);
    double tri_put  = trinomial_option_price(S, K, T, sig, r, OptionType::PUT, 200, false);
    auto bs_call = ts::black_scholes(S, K, T, sig, r, OptionType::CALL);
    auto bs_put  = ts::black_scholes(S, K, T, sig, r, OptionType::PUT);
    ASSERT_NEAR(tri_call, bs_call.price, 0.02);
    ASSERT_NEAR(tri_put, bs_put.price, 0.02);
    TEST_END("trinomial tree converges to Black-Scholes");
}

void test_trinomial_american_vs_european() {
    TEST("trinomial American option >= European, call equal for non-dividend");
    double S = 100.0, K = 90.0, T = 1.0, sig = 0.25, r = 0.05;
    double euro_put = trinomial_option_price(S, K, T, sig, r, OptionType::PUT, 100, false);
    double amer_put = trinomial_option_price(S, K, T, sig, r, OptionType::PUT, 100, true);
    ASSERT_TRUE(amer_put >= euro_put - 1e-10);
    // American call on a non-dividend stock should equal the European call.
    double euro_call = trinomial_option_price(S, K, T, sig, r, OptionType::CALL, 100, false);
    double amer_call = trinomial_option_price(S, K, T, sig, r, OptionType::CALL, 100, true);
    ASSERT_NEAR(amer_call, euro_call, 1e-8);
    TEST_END("trinomial American option >= European, call equal for non-dividend");
}

void test_trinomial_at_expiry() {
    TEST("trinomial at expiry equals intrinsic value");
    double call = trinomial_option_price(110.0, 100.0, 0.0, 0.2, 0.05, OptionType::CALL, 10, false);
    double put  = trinomial_option_price(110.0, 100.0, 0.0, 0.2, 0.05, OptionType::PUT, 10, true);
    ASSERT_NEAR(call, 10.0, 1e-12);
    ASSERT_NEAR(put, 0.0, 1e-12);
    TEST_END("trinomial at expiry equals intrinsic value");
}

// ============================================================================
// Task 4.1 - Monte Carlo Option Pricing Tests
// ============================================================================

void test_monte_carlo_option_converges_to_bs() {
    TEST("Monte Carlo option price converges to Black-Scholes");
    double S = 100.0, K = 100.0, T = 1.0, sig = 0.2, r = 0.05;
    auto mc = monte_carlo_option_price(S, K, T, sig, r, OptionType::CALL, 200000, 42);
    auto bs = ts::black_scholes(S, K, T, sig, r, OptionType::CALL);
    ASSERT_TRUE(mc.std_error > 0.0);
    // Within 3 standard errors of the analytic price.
    ASSERT_NEAR(mc.price, bs.price, 3.0 * mc.std_error + 0.05);
    TEST_END("Monte Carlo option price converges to Black-Scholes");
}

void test_monte_carlo_option_put_and_reproducible() {
    TEST("Monte Carlo put price and seed reproducibility");
    double S = 100.0, K = 100.0, T = 1.0, sig = 0.2, r = 0.05;
    auto mc1 = monte_carlo_option_price(S, K, T, sig, r, OptionType::PUT, 100000, 7);
    auto mc2 = monte_carlo_option_price(S, K, T, sig, r, OptionType::PUT, 100000, 7);
    auto bs = ts::black_scholes(S, K, T, sig, r, OptionType::PUT);
    ASSERT_NEAR(mc1.price, mc2.price, 1e-12);   // same seed => identical
    ASSERT_NEAR(mc1.price, bs.price, 3.0 * mc1.std_error + 0.05);
    TEST_END("Monte Carlo put price and seed reproducibility");
}

// ============================================================================
// Task 4.1 - Implied Volatility Tests
// ============================================================================

void test_implied_volatility_round_trip() {
    TEST("implied volatility round-trips through Black-Scholes");
    double S = 100.0, K = 100.0, T = 1.0, r = 0.05;
    for (double true_sig : {0.10, 0.20, 0.35, 0.60}) {
        auto bs = ts::black_scholes(S, K, T, true_sig, r, OptionType::CALL);
        double iv = implied_volatility(bs.price, S, K, T, r, OptionType::CALL);
        ASSERT_NEAR(iv, true_sig, 1e-5);
    }
    TEST_END("implied volatility round-trips through Black-Scholes");
}

void test_implied_volatility_put_and_invalid() {
    TEST("implied volatility for puts and invalid prices");
    double S = 100.0, K = 105.0, T = 0.5, r = 0.03;
    auto bs = ts::black_scholes(S, K, T, 0.28, r, OptionType::PUT);
    double iv = implied_volatility(bs.price, S, K, T, r, OptionType::PUT);
    ASSERT_NEAR(iv, 0.28, 1e-5);
    // Price above the no-arbitrage bound (call priced above spot) => NaN.
    double bad = implied_volatility(150.0, S, K, T, r, OptionType::CALL);
    ASSERT_TRUE(std::isnan(bad));
    TEST_END("implied volatility for puts and invalid prices");
}

// ============================================================================
// Task 4.1 - Volatility Surface Tests
// ============================================================================

void test_vol_surface_nodes_and_flat() {
    TEST("vol surface: exact at nodes, flat in degenerate dims");
    std::vector<double> strikes = {80.0, 100.0, 120.0};
    std::vector<double> tenors  = {0.25, 0.50, 1.00};
    std::vector<double> vols    = {0.30, 0.25, 0.28,
                                   0.28, 0.22, 0.26,
                                   0.26, 0.20, 0.24};
    VolatilitySurface surface(strikes, tenors, vols);
    ASSERT_TRUE(surface.is_valid());
    // Exact at nodes.
    ASSERT_NEAR(surface.volatility(100.0, 0.50), 0.22, 1e-12);
    ASSERT_NEAR(surface.volatility(80.0, 0.25), 0.30, 1e-12);
    // Midpoint of a cell: bilinear mean of its four corners.
    double mid = surface.volatility(90.0, 0.375);
    ASSERT_NEAR(mid, 0.25 * (0.30 + 0.25 + 0.28 + 0.22), 1e-12);
    TEST_END("vol surface: exact at nodes, flat in degenerate dims");
}

void test_vol_surface_flat_extrapolation() {
    TEST("vol surface flat extrapolation outside grid");
    std::vector<double> strikes = {80.0, 100.0};
    std::vector<double> tenors  = {0.5, 1.0};
    std::vector<double> vols    = {0.30, 0.25,
                                   0.26, 0.20};
    VolatilitySurface surface(strikes, tenors, vols);
    // Below / above the strike and tenor ranges clamps to the corner values.
    ASSERT_NEAR(surface.volatility(50.0, 0.1), 0.30, 1e-12);
    ASSERT_NEAR(surface.volatility(200.0, 5.0), 0.20, 1e-12);
    // On an edge, interpolation still runs along that edge.
    ASSERT_NEAR(surface.volatility(90.0, 0.1), 0.275, 1e-12);
    // Invalid surface => NaN.
    VolatilitySurface empty;
    ASSERT_FALSE(empty.is_valid());
    ASSERT_TRUE(std::isnan(empty.volatility(100.0, 1.0)));
    TEST_END("vol surface flat extrapolation outside grid");
}

// ============================================================================
// Task 4.2 - Historical VaR / ES Tests
// ============================================================================

void test_historical_var_known_series() {
    TEST("historical VaR / ES on a known return series");
    // 20 ordered returns; sorted ascending they are already in order.
    std::vector<double> rets = {
        -0.10, -0.08, -0.06, -0.05, -0.04, -0.03, -0.02, -0.02, -0.01, -0.01,
         0.00,  0.01,  0.01,  0.02,  0.02,  0.03,  0.04,  0.05,  0.06,  0.08};
    // 95% confidence: alpha = 0.05, index = floor(0.05 * 20) = 1 => -(-0.08).
    ASSERT_NEAR(historical_var(rets.data(), rets.size(), 0.95), 0.08, 1e-12);
    // ES: mean of the two worst losses (0.10 + 0.08) / 2.
    ASSERT_NEAR(historical_es(rets.data(), rets.size(), 0.95), 0.09, 1e-12);
    // 90% confidence: index = floor(0.10 * 20) = 2; ties at the boundary keep
    // the floor-quantile convention => -(-0.08).
    ASSERT_NEAR(historical_var(rets.data(), rets.size(), 0.90), 0.08, 1e-12);
    // And at 85%: index = floor(0.15 * 20) = 3 => -(-0.05).
    ASSERT_NEAR(historical_var(rets.data(), rets.size(), 0.85), 0.05, 1e-12);
    TEST_END("historical VaR / ES on a known return series");
}

void test_historical_var_edge_cases() {
    TEST("historical VaR / ES edge cases");
    ASSERT_NEAR(historical_var(nullptr, 0, 0.95), 0.0, 1e-12);
    std::vector<double> one = {-0.05};
    ASSERT_NEAR(historical_var(one.data(), 1, 0.99), 0.05, 1e-12);
    ASSERT_NEAR(historical_es(one.data(), 1, 0.99), 0.05, 1e-12);
    // ES >= VaR always.
    std::vector<double> rets = {-0.20, -0.01, 0.0, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07};
    ASSERT_TRUE(historical_es(rets.data(), rets.size(), 0.90) >=
                historical_var(rets.data(), rets.size(), 0.90) - 1e-12);
    TEST_END("historical VaR / ES edge cases");
}

// ============================================================================
// Task 4.2 - Parametric VaR / ES Tests
// ============================================================================

void test_parametric_var_analytic() {
    TEST("parametric VaR matches analytic z*sigma*value");
    // mean = 0: VaR(95%) = 1.644854 * sigma * value.
    double sigma = 0.02, value = 1000000.0;
    double var95 = parametric_var(0.0, sigma, 0.95, value);
    ASSERT_NEAR(var95, 1.644854 * sigma * value, 1e-6 * sigma * value * 5);
    double var99 = parametric_var(0.0, sigma, 0.99, value);
    ASSERT_NEAR(var99, 2.326348 * sigma * value, 1e-6 * sigma * value * 5);
    // Non-zero mean reduces VaR.
    double var_mu = parametric_var(0.001, sigma, 0.95, value);
    ASSERT_NEAR(var_mu, var95 - 0.001 * value, 1e-6);
    TEST_END("parametric VaR matches analytic z*sigma*value");
}

void test_parametric_es_exceeds_var() {
    TEST("parametric ES exceeds VaR and matches analytic tail mean");
    double sigma = 0.02, value = 1000000.0;
    double var95 = parametric_var(0.0, sigma, 0.95, value);
    double es95 = parametric_es(0.0, sigma, 0.95, value);
    ASSERT_TRUE(es95 > var95);
    // Analytic: ES = sigma * pdf(z) / (1 - c) * value, z = 1.644854.
    double z = ts::normal_inv_cdf(0.95);
    ASSERT_NEAR(es95, sigma * ts::normal_pdf(z) / 0.05 * value, 1e-6 * sigma * value * 5);
    TEST_END("parametric ES exceeds VaR and matches analytic tail mean");
}

void test_monte_carlo_var_wrapper() {
    TEST("Monte Carlo VaR wrapper delegates to tensors::monte_carlo_risk");
    auto mc = monte_carlo_var(100000.0, 0.08, 0.20, 1.0 / 12.0, 20000, 0.95, 123);
    ASSERT_EQ(mc.num_paths, 20000);
    ASSERT_TRUE(mc.value_at_risk != 0.0);
    // With a positive drift and a short horizon, VaR should be modest and ES
    // should exceed VaR.
    ASSERT_TRUE(mc.expected_shortfall >= mc.value_at_risk - 1e-6);
    // Reproducible with the same seed.
    auto mc2 = monte_carlo_var(100000.0, 0.08, 0.20, 1.0 / 12.0, 20000, 0.95, 123);
    ASSERT_NEAR(mc.value_at_risk, mc2.value_at_risk, 1e-12);
    TEST_END("Monte Carlo VaR wrapper delegates to tensors::monte_carlo_risk");
}

// ============================================================================
// Task 4.2 - Stress Scenarios Tests
// ============================================================================

void test_apply_stress_scenario() {
    TEST("stress scenario P&L impact");
    std::vector<double> positions = {100000.0, 200000.0, 50000.0};
    StressScenario crash("Test Crash", {{0, -0.20}, {1, -0.10}});
    double pnl = apply_stress_scenario(positions.data(), positions.size(), crash);
    ASSERT_NEAR(pnl, -0.20 * 100000.0 - 0.10 * 200000.0, 1e-6);
    // Out-of-range indices are ignored.
    StressScenario bad("Bad", {{99, -0.50}});
    ASSERT_NEAR(apply_stress_scenario(positions.data(), positions.size(), bad), 0.0, 1e-12);
    TEST_END("stress scenario P&L impact");
}

void test_standard_stress_scenarios() {
    TEST("standard stress scenario library");
    size_t n = 3;
    auto scenarios = standard_stress_scenarios(n);
    ASSERT_EQ(scenarios.size(), static_cast<size_t>(4));
    std::vector<double> positions = {100000.0, 100000.0, 100000.0};
    bool found_equity = false;
    bool found_rates = false;
    for (const auto & s : scenarios) {
        ASSERT_EQ(s.asset_shocks.size(), n);
        double pnl = apply_stress_scenario(positions.data(), n, s);
        ASSERT_TRUE(pnl < 0.0);   // all library scenarios are adverse
        if (s.name == "Equity Crash 2008") {
            found_equity = true;
            ASSERT_NEAR(pnl, -0.20 * 300000.0, 1e-6);
        }
        if (s.name == "Rates +200bp Parallel") {
            found_rates = true;
            ASSERT_NEAR(pnl, -0.02 * 300000.0, 1e-6);
        }
    }
    ASSERT_TRUE(found_equity);
    ASSERT_TRUE(found_rates);
    TEST_END("standard stress scenario library");
}

// ============================================================================
// Task 4.2 - RiskMonitor Tests
// ============================================================================

void test_risk_monitor_var_limit() {
    TEST("RiskMonitor VaR limit breach and callback");
    // Two-asset portfolio with known moments.
    std::vector<double> values = {500000.0, 500000.0};
    std::vector<double> mu     = {0.0, 0.0};
    // Independent assets, each with 2% daily vol.
    std::vector<double> cov = {0.0004, 0.0,
                               0.0,    0.0004};
    RiskMonitor monitor;
    monitor.set_portfolio(values, mu, cov);
    monitor.set_var_limit(0.99, 0.0);   // enable VaR evaluation, no limit yet

    // Portfolio currency stddev = sqrt(0.0004 * (500k^2 + 500k^2)).
    double port_std = std::sqrt(0.0004 * 2.0 * 500000.0 * 500000.0);
    double expected_var = ts::normal_inv_cdf(0.99) * port_std;
    ASSERT_NEAR(monitor.current_var(), expected_var, 1e-6);

    int breach_count = 0;
    RiskBreach last;
    monitor.set_breach_callback([&](const RiskBreach & b) {
        breach_count++;
        last = b;
    });

    // Limit above the VaR: no breach.
    monitor.set_var_limit(0.99, expected_var * 1.10);
    ASSERT_FALSE(monitor.check_limits());
    ASSERT_EQ(breach_count, 0);

    // Limit below the VaR: breach fires.
    monitor.set_var_limit(0.99, expected_var * 0.90);
    ASSERT_TRUE(monitor.check_limits());
    ASSERT_TRUE(monitor.var_limit_breached());
    ASSERT_EQ(breach_count, 1);
    ASSERT_TRUE(last.type == RiskBreach::Type::VAR_LIMIT);
    ASSERT_NEAR(last.observed, expected_var, 1e-6);
    TEST_END("RiskMonitor VaR limit breach and callback");
}

void test_risk_monitor_drawdown_limit() {
    TEST("RiskMonitor drawdown limit breach");
    std::vector<double> values = {100000.0};
    std::vector<double> mu     = {0.0};
    std::vector<double> cov    = {0.0004};
    RiskMonitor monitor;
    monitor.set_portfolio(values, mu, cov);
    monitor.set_drawdown_limit(0.10);   // 10% peak-to-trough limit

    monitor.update_prices({100000.0});
    ASSERT_NEAR(monitor.current_drawdown(), 0.0, 1e-12);
    ASSERT_FALSE(monitor.check_limits());

    // New peak, then a 5% decline: under the limit.
    monitor.update_prices({120000.0});
    monitor.update_prices({114000.0});
    ASSERT_NEAR(monitor.current_drawdown(), 0.05, 1e-12);
    ASSERT_FALSE(monitor.check_limits());

    // 12% decline from the peak: breach.
    int breaches = 0;
    RiskBreach last;
    monitor.set_breach_callback([&](const RiskBreach & b) { breaches++; last = b; });
    monitor.update_prices({105600.0});
    ASSERT_NEAR(monitor.current_drawdown(), 0.12, 1e-12);
    ASSERT_TRUE(monitor.check_limits());
    ASSERT_TRUE(monitor.drawdown_limit_breached());
    ASSERT_EQ(breaches, 1);
    ASSERT_TRUE(last.type == RiskBreach::Type::DRAWDOWN_LIMIT);
    TEST_END("RiskMonitor drawdown limit breach");
}

// ============================================================================
// Task 4.3 - Markowitz / GMV Tests
// ============================================================================

namespace {

// Three-asset test problem used across the optimization tests.
struct PortfolioProblem {
    std::vector<double> mu  = {0.08, 0.12, 0.10};
    std::vector<double> cov = {0.04,  0.012, 0.006,
                               0.012, 0.09,  0.015,
                               0.006, 0.015, 0.0225};
};

} // anonymous namespace

void test_markowitz_target_return() {
    TEST("Markowitz portfolio hits the target return and sums to one");
    PortfolioProblem p;
    double target = 0.10;
    auto w = markowitz_optimize(p.mu.data(), p.cov.data(), 3, target);
    ASSERT_EQ(w.size(), static_cast<size_t>(3));
    double sum = w[0] + w[1] + w[2];
    ASSERT_NEAR(sum, 1.0, 1e-10);
    double ret = ts::portfolio_expected_return(w.data(), p.mu.data(), 3);
    ASSERT_NEAR(ret, target, 1e-10);
    TEST_END("Markowitz portfolio hits the target return and sums to one");
}

void test_markowitz_is_minimum_variance() {
    TEST("Markowitz portfolio has minimum variance for its target return");
    PortfolioProblem p;
    double target = 0.10;
    auto w = markowitz_optimize(p.mu.data(), p.cov.data(), 3, target);
    double opt_var = ts::portfolio_variance(w.data(), p.cov.data(), 3);
    // Perturb the weights while preserving both constraints and check the
    // variance can only increase: w' = w + eps * d with 1'd = 0, mu'd = 0.
    // A null-space direction of the constraint matrix in 3 assets:
    // d proportional to (mu2-mu3, mu3-mu1, mu1-mu2) rotated; use the exact
    // null vector of [1 1 1; mu] via cross product.
    double d0 = 1.0 * p.mu[2] - 1.0 * p.mu[1];
    double d1 = 1.0 * p.mu[0] - 1.0 * p.mu[2];
    double d2 = 1.0 * p.mu[1] - 1.0 * p.mu[0];
    for (double eps : {-0.05, 0.05}) {
        std::vector<double> wp = {w[0] + eps * d0, w[1] + eps * d1, w[2] + eps * d2};
        double pert_var = ts::portfolio_variance(wp.data(), p.cov.data(), 3);
        ASSERT_TRUE(pert_var >= opt_var - 1e-12);
    }
    TEST_END("Markowitz portfolio has minimum variance for its target return");
}

void test_markowitz_no_short() {
    TEST("Markowitz no-short-selling constraint clamps negative weights");
    // Three assets where the low-return asset also has the lowest variance;
    // a target return far above the GMV return forces the unconstrained
    // optimizer to short the low-return asset to finance the riskier ones.
    std::vector<double> mu  = {0.02, 0.10, 0.14};
    std::vector<double> cov = {0.01, 0.0,  0.0,
                               0.0,  0.09, 0.0,
                               0.0,  0.0,  0.16};
    auto w_short = markowitz_optimize(mu.data(), cov.data(), 3, 0.12, true);
    ASSERT_EQ(w_short.size(), static_cast<size_t>(3));
    ASSERT_TRUE(w_short[0] < 0.0);   // unconstrained solution shorts asset 0
    auto w_long = markowitz_optimize(mu.data(), cov.data(), 3, 0.12, false);
    ASSERT_EQ(w_long.size(), static_cast<size_t>(3));
    for (double w : w_long) {
        ASSERT_TRUE(w >= -1e-12);
    }
    ASSERT_NEAR(w_long[0] + w_long[1] + w_long[2], 1.0, 1e-10);
    // With asset 0 clamped out, the long-only optimum splits between the two
    // remaining assets to hit the target: w = (0.14-0.12)/(0.14-0.10) = 0.5.
    ASSERT_NEAR(w_long[0], 0.0, 1e-10);
    ASSERT_NEAR(w_long[1], 0.5, 1e-9);
    ASSERT_NEAR(w_long[2], 0.5, 1e-9);
    ASSERT_NEAR(ts::portfolio_expected_return(w_long.data(), mu.data(), 3), 0.12, 1e-10);
    TEST_END("Markowitz no-short-selling constraint clamps negative weights");
}

void test_gmv_lower_variance_than_assets() {
    TEST("GMV portfolio variance <= every single-asset variance");
    PortfolioProblem p;
    auto w = global_minimum_variance_portfolio(p.cov.data(), 3);
    ASSERT_EQ(w.size(), static_cast<size_t>(3));
    ASSERT_NEAR(w[0] + w[1] + w[2], 1.0, 1e-10);
    double gmv_var = ts::portfolio_variance(w.data(), p.cov.data(), 3);
    for (size_t i = 0; i < 3; i++) {
        std::vector<double> single(3, 0.0);
        single[i] = 1.0;
        double asset_var = ts::portfolio_variance(single.data(), p.cov.data(), 3);
        ASSERT_TRUE(gmv_var <= asset_var + 1e-12);
    }
    // Diagonal (uncorrelated) case: w_i proportional to 1/sigma_i^2.
    std::vector<double> diag_cov = {0.04, 0.0,  0.0,
                                    0.0,  0.01, 0.0,
                                    0.0,  0.0,  0.09};
    auto wd = global_minimum_variance_portfolio(diag_cov.data(), 3);
    // 1/0.04 = 25, 1/0.01 = 100, 1/0.09 ~ 11.111; total ~ 136.111.
    ASSERT_NEAR(wd[0], 25.0 / 136.111111, 1e-4);
    ASSERT_NEAR(wd[1], 100.0 / 136.111111, 1e-4);
    TEST_END("GMV portfolio variance <= every single-asset variance");
}

// ============================================================================
// Task 4.3 - Black-Litterman Tests
// ============================================================================

void test_black_litterman_prior_and_views() {
    TEST("Black-Litterman: no views returns prior, views tilt returns");
    PortfolioProblem p;
    std::vector<double> w_mkt = {0.5, 0.3, 0.2};
    double delta = 2.5, tau = 0.05;

    // No views: posterior equals the prior pi = delta * C * w_mkt.
    auto prior = black_litterman(p.cov.data(), 3, w_mkt.data(), delta, tau,
                                 nullptr, nullptr, nullptr, 0);
    ASSERT_EQ(prior.size(), static_cast<size_t>(3));
    for (size_t i = 0; i < 3; i++) {
        double expected = delta * (p.cov[i * 3 + 0] * w_mkt[0] +
                                   p.cov[i * 3 + 1] * w_mkt[1] +
                                   p.cov[i * 3 + 2] * w_mkt[2]);
        ASSERT_NEAR(prior[i], expected, 1e-12);
    }

    // One view: asset 1 will return 18%. Posterior for asset 1 must move up
    // toward the view (but stay between prior and view for moderate omega).
    std::vector<double> P = {0.0, 1.0, 0.0};
    std::vector<double> Q = {0.18};
    auto post = black_litterman(p.cov.data(), 3, w_mkt.data(), delta, tau,
                                P.data(), Q.data(), 1);
    ASSERT_EQ(post.size(), static_cast<size_t>(3));
    ASSERT_TRUE(post[1] > prior[1]);          // bullish view raises the return
    ASSERT_TRUE(post[1] < Q[0] + 1e-9);       // but does not overshoot far
    TEST_END("Black-Litterman: no views returns prior, views tilt returns");
}

void test_black_litterman_confident_view_converges() {
    TEST("Black-Litterman: very confident view dominates the prior");
    PortfolioProblem p;
    std::vector<double> w_mkt = {0.5, 0.3, 0.2};
    double delta = 2.5, tau = 0.05;
    std::vector<double> P = {0.0, 1.0, 0.0};
    std::vector<double> Q = {0.18};
    // Tiny omega => the posterior asset-1 return approaches the view.
    std::vector<double> omega = {1e-10};
    auto post = black_litterman(p.cov.data(), 3, w_mkt.data(), delta, tau,
                                P.data(), Q.data(), omega.data(), 1);
    ASSERT_NEAR(post[1], 0.18, 1e-4);
    TEST_END("Black-Litterman: very confident view dominates the prior");
}

// ============================================================================
// Task 4.3 - Risk Parity Tests
// ============================================================================

void test_risk_parity_equal_contributions() {
    TEST("risk parity equalizes risk contributions");
    PortfolioProblem p;
    auto w = risk_parity(p.cov.data(), 3);
    ASSERT_EQ(w.size(), static_cast<size_t>(3));
    ASSERT_NEAR(w[0] + w[1] + w[2], 1.0, 1e-9);
    for (double wi : w) {
        ASSERT_TRUE(wi > 0.0);
    }
    // Risk contribution RC_i = w_i (Cw)_i; all RCs must be ~equal.
    std::vector<double> cw(3, 0.0);
    ts::matvec(p.cov.data(), w.data(), cw.data(), 3, 3);
    double rc0 = w[0] * cw[0];
    double rc1 = w[1] * cw[1];
    double rc2 = w[2] * cw[2];
    ASSERT_NEAR(rc0, rc1, 1e-6);
    ASSERT_NEAR(rc1, rc2, 1e-6);
    TEST_END("risk parity equalizes risk contributions");
}

void test_risk_parity_uncorrelated_inverse_vol() {
    TEST("risk parity on diagonal covariance is inverse-volatility weighted");
    std::vector<double> cov = {0.04, 0.0,  0.0,
                               0.0,  0.01, 0.0,
                               0.0,  0.0,  0.09};
    auto w = risk_parity(cov.data(), 3);
    // Inverse vols: 1/0.2 = 5, 1/0.1 = 10, 1/0.3 = 3.333; total 18.333.
    ASSERT_NEAR(w[0], 5.0 / 18.333333, 1e-4);
    ASSERT_NEAR(w[1], 10.0 / 18.333333, 1e-4);
    ASSERT_NEAR(w[2], 3.333333 / 18.333333, 1e-4);
    TEST_END("risk parity on diagonal covariance is inverse-volatility weighted");
}

// ============================================================================
// Task 4.3 - Efficient Frontier Tests
// ============================================================================

void test_efficient_frontier_shape() {
    TEST("efficient frontier is increasing along its efficient branch");
    PortfolioProblem p;
    auto frontier = efficient_frontier(p.mu.data(), p.cov.data(), 3, 9);
    ASSERT_EQ(frontier.size(), static_cast<size_t>(9));
    auto gmv = global_minimum_variance_portfolio(p.cov.data(), 3);
    double gmv_risk = ts::portfolio_volatility(gmv.data(), p.cov.data(), 3);

    for (size_t i = 0; i < frontier.size(); i++) {
        ASSERT_NEAR(frontier[i].expected_return, frontier[i].target_return, 1e-8);
        ASSERT_TRUE(frontier[i].risk > 0.0);
        if (i > 0) {
            ASSERT_TRUE(frontier[i].expected_return >= frontier[i - 1].expected_return - 1e-9);
        }
        // No minimum-variance portfolio can beat the GMV risk.
        ASSERT_TRUE(frontier[i].risk >= gmv_risk - 1e-9);
    }
    // The sweep starts at the minimum asset return; below the GMV return the
    // frontier sits on the inefficient branch (risk decreasing toward the
    // GMV point), and above it risk strictly increases. Locate the minimum-
    // risk point and verify both branches.
    size_t gmv_idx = 0;
    for (size_t i = 1; i < frontier.size(); i++) {
        if (frontier[i].risk < frontier[gmv_idx].risk) {
            gmv_idx = i;
        }
    }
    ASSERT_NEAR(frontier[gmv_idx].risk, gmv_risk, 1e-4);
    for (size_t i = gmv_idx + 1; i < frontier.size(); i++) {
        ASSERT_TRUE(frontier[i].risk > frontier[i - 1].risk);
    }
    for (size_t i = 1; i <= gmv_idx; i++) {
        ASSERT_TRUE(frontier[i - 1].risk >= frontier[i].risk - 1e-9);
    }
    TEST_END("efficient frontier is increasing along its efficient branch");
}

// ============================================================================
// Task 4.4 - Yield Curve Tests
// ============================================================================

void test_yield_curve_interpolation() {
    TEST("yield curve linear zero-rate interpolation and flat extrapolation");
    YieldCurve curve({1.0, 2.0, 5.0, 10.0}, {0.02, 0.025, 0.03, 0.035});
    ASSERT_EQ(curve.size(), static_cast<size_t>(4));
    // Exact at nodes.
    ASSERT_NEAR(curve.zero_rate(2.0), 0.025, 1e-12);
    // Linear between nodes: halfway between 2y (2.5%) and 5y (3.0%).
    ASSERT_NEAR(curve.zero_rate(3.5), 0.0275, 1e-12);
    // Flat extrapolation beyond the ends.
    ASSERT_NEAR(curve.zero_rate(0.1), 0.02, 1e-12);
    ASSERT_NEAR(curve.zero_rate(30.0), 0.035, 1e-12);
    // Discount factor consistency: df = exp(-r t).
    ASSERT_NEAR(curve.discount_factor(3.5), std::exp(-0.0275 * 3.5), 1e-12);
    TEST_END("yield curve linear zero-rate interpolation and flat extrapolation");
}

void test_yield_curve_flat_curve_discount() {
    TEST("flat yield curve discounts match continuous compounding");
    YieldCurve curve({5.0}, {0.04});
    ASSERT_NEAR(curve.zero_rate(0.0), 0.04, 1e-12);
    ASSERT_NEAR(curve.zero_rate(7.3), 0.04, 1e-12);
    ASSERT_NEAR(curve.discount_factor(2.5), std::exp(-0.04 * 2.5), 1e-12);
    // Unsorted construction is handled.
    YieldCurve messy({10.0, 1.0, 5.0}, {0.035, 0.02, 0.03});
    ASSERT_NEAR(messy.zero_rate(3.0), 0.02 + (0.03 - 0.02) * (3.0 - 1.0) / (5.0 - 1.0), 1e-12);
    TEST_END("flat yield curve discounts match continuous compounding");
}

// ============================================================================
// Task 4.4 - Bond Pricing Tests
// ============================================================================

void test_bond_par_when_coupon_equals_ytm() {
    TEST("bond prices at par when coupon rate equals YTM");
    Bond bond(100.0, 0.06, 2, 5.0);   // 6% semi-annual coupon, 5y maturity
    ASSERT_NEAR(bond_price(bond, 0.06), 100.0, 1e-8);
    Bond annual(1000.0, 0.05, 1, 10.0);
    ASSERT_NEAR(bond_price(annual, 0.05), 1000.0, 1e-6);
    TEST_END("bond prices at par when coupon rate equals YTM");
}

void test_bond_price_reference_value() {
    TEST("bond price reference value (textbook 8% coupon, 10% YTM)");
    // Classic: 10y semi-annual 8% bond at 10% YTM => price ~ 87.5377 % of par.
    Bond bond(100.0, 0.08, 2, 10.0);
    ASSERT_NEAR(bond_price(bond, 0.10), 87.5377, 1e-3);
    // Zero-coupon bond: P = face / (1 + y/f)^{fT}.
    Bond zero(100.0, 0.0, 2, 5.0);
    ASSERT_NEAR(bond_price(zero, 0.06), 100.0 / std::pow(1.03, 10.0), 1e-8);
    TEST_END("bond price reference value (textbook 8% coupon, 10% YTM)");
}

void test_bond_price_from_yield_curve() {
    TEST("bond price from a flat yield curve matches exp(-y t) discounting");
    Bond bond(100.0, 0.05, 2, 4.0);
    YieldCurve flat({10.0}, {0.04});   // 4% continuous, flat
    double price = bond_price(bond, flat);
    // Manual: coupons of 2.5 at 0.5..4.0 plus 100 at 4.0, discounted at 4% cont.
    double expected = 0.0;
    for (int k = 1; k <= 8; k++) {
        double t = k * 0.5;
        double cf = (k == 8) ? 102.5 : 2.5;
        expected += cf * std::exp(-0.04 * t);
    }
    ASSERT_NEAR(price, expected, 1e-8);
    TEST_END("bond price from a flat yield curve matches exp(-y t) discounting");
}

void test_bond_ytm_round_trip() {
    TEST("yield-to-maturity solver round-trips through the price");
    Bond bond(100.0, 0.08, 2, 10.0);
    for (double ytm : {0.04, 0.08, 0.10, 0.15}) {
        double price = bond_price(bond, ytm);
        double solved = bond_yield_to_maturity(bond, price);
        ASSERT_NEAR(solved, ytm, 1e-7);
    }
    // Par bond solves back to the coupon rate.
    Bond par(100.0, 0.06, 2, 5.0);
    ASSERT_NEAR(bond_yield_to_maturity(par, 100.0), 0.06, 1e-7);
    // Invalid price => NaN.
    ASSERT_TRUE(std::isnan(bond_yield_to_maturity(par, -5.0)));
    TEST_END("yield-to-maturity solver round-trips through the price");
}

void test_bond_duration_zero_coupon() {
    TEST("zero-coupon bond duration equals maturity");
    Bond zero(100.0, 0.0, 2, 7.0);
    ASSERT_NEAR(macaulay_duration(zero, 0.05), 7.0, 1e-9);
    ASSERT_NEAR(modified_duration(zero, 0.05), 7.0 / (1.0 + 0.05 / 2.0), 1e-9);
    TEST_END("zero-coupon bond duration equals maturity");
}

void test_bond_duration_reference_value() {
    TEST("Macaulay/modified duration reference values");
    // 5y, 6% semi-annual bond at 7% YTM: price 95.8417, D_mac ~ 4.3774,
    // D_mod ~ 4.2294 (standard bond-table values).
    Bond bond(100.0, 0.06, 2, 5.0);
    ASSERT_NEAR(bond_price(bond, 0.07), 95.8417, 1e-3);
    ASSERT_NEAR(macaulay_duration(bond, 0.07), 4.3774, 5e-3);
    ASSERT_NEAR(modified_duration(bond, 0.07), 4.3774 / 1.035, 5e-3);
    // Coupon bond duration must be below its maturity.
    ASSERT_TRUE(macaulay_duration(bond, 0.07) < 5.0);
    TEST_END("Macaulay/modified duration reference values");
}

void test_bond_convexity() {
    TEST("bond convexity positive and matches price sensitivity");
    Bond bond(100.0, 0.08, 2, 10.0);
    double y = 0.10;
    double cx = convexity(bond, y);
    ASSERT_TRUE(cx > 0.0);
    // Finite-difference check: C ~ (P+ + P- - 2P0) / (P0 dy^2).
    double dy = 1e-4;
    double p0 = bond_price(bond, y);
    double p_up = bond_price(bond, y + dy);
    double p_dn = bond_price(bond, y - dy);
    double fd = (p_up + p_dn - 2.0 * p0) / (p0 * dy * dy);
    ASSERT_NEAR(cx, fd, 1e-2);
    // Duration-convexity price approximation for a +100bp move.
    double d_mod = modified_duration(bond, y);
    double approx = p0 * (1.0 - d_mod * 0.01 + 0.5 * cx * 0.01 * 0.01);
    ASSERT_NEAR(approx, bond_price(bond, y + 0.01), 0.02);
    TEST_END("bond convexity positive and matches price sensitivity");
}

// ============================================================================
// Cross-Module Consistency
// ============================================================================

void test_put_call_parity_across_models() {
    TEST("put-call parity consistent across BS, trinomial, and Monte Carlo");
    double S = 100.0, K = 105.0, T = 0.5, sig = 0.25, r = 0.03;
    double parity_rhs = S - K * std::exp(-r * T);

    auto bs_c = ts::black_scholes(S, K, T, sig, r, OptionType::CALL);
    auto bs_p = ts::black_scholes(S, K, T, sig, r, OptionType::PUT);
    ASSERT_NEAR(bs_c.price - bs_p.price, parity_rhs, 1e-8);

    double tri_c = trinomial_option_price(S, K, T, sig, r, OptionType::CALL, 200, false);
    double tri_p = trinomial_option_price(S, K, T, sig, r, OptionType::PUT, 200, false);
    ASSERT_NEAR(tri_c - tri_p, parity_rhs, 0.02);

    auto mc_c = monte_carlo_option_price(S, K, T, sig, r, OptionType::CALL, 200000, 99);
    auto mc_p = monte_carlo_option_price(S, K, T, sig, r, OptionType::PUT, 200000, 99);
    double mc_se = std::sqrt(mc_c.std_error * mc_c.std_error +
                             mc_p.std_error * mc_p.std_error);
    ASSERT_NEAR(mc_c.price - mc_p.price, parity_rhs, 3.0 * mc_se + 0.05);
    TEST_END("put-call parity consistent across BS, trinomial, and Monte Carlo");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Quantitative Finance Model Tests" << std::endl;
    std::cout << "  (Phase 2, Issue #004)" << std::endl;
    std::cout << "============================================\n" << std::endl;

    std::cout << "--- Task 4.1: Trinomial Tree ---" << std::endl;
    test_trinomial_converges_to_bs();
    test_trinomial_american_vs_european();
    test_trinomial_at_expiry();

    std::cout << "\n--- Task 4.1: Monte Carlo Option Pricing ---" << std::endl;
    test_monte_carlo_option_converges_to_bs();
    test_monte_carlo_option_put_and_reproducible();

    std::cout << "\n--- Task 4.1: Implied Volatility ---" << std::endl;
    test_implied_volatility_round_trip();
    test_implied_volatility_put_and_invalid();

    std::cout << "\n--- Task 4.1: Volatility Surface ---" << std::endl;
    test_vol_surface_nodes_and_flat();
    test_vol_surface_flat_extrapolation();

    std::cout << "\n--- Task 4.2: Historical VaR / ES ---" << std::endl;
    test_historical_var_known_series();
    test_historical_var_edge_cases();

    std::cout << "\n--- Task 4.2: Parametric / Monte Carlo VaR ---" << std::endl;
    test_parametric_var_analytic();
    test_parametric_es_exceeds_var();
    test_monte_carlo_var_wrapper();

    std::cout << "\n--- Task 4.2: Stress Scenarios ---" << std::endl;
    test_apply_stress_scenario();
    test_standard_stress_scenarios();

    std::cout << "\n--- Task 4.2: Risk Monitor ---" << std::endl;
    test_risk_monitor_var_limit();
    test_risk_monitor_drawdown_limit();

    std::cout << "\n--- Task 4.3: Markowitz / GMV ---" << std::endl;
    test_markowitz_target_return();
    test_markowitz_is_minimum_variance();
    test_markowitz_no_short();
    test_gmv_lower_variance_than_assets();

    std::cout << "\n--- Task 4.3: Black-Litterman ---" << std::endl;
    test_black_litterman_prior_and_views();
    test_black_litterman_confident_view_converges();

    std::cout << "\n--- Task 4.3: Risk Parity ---" << std::endl;
    test_risk_parity_equal_contributions();
    test_risk_parity_uncorrelated_inverse_vol();

    std::cout << "\n--- Task 4.3: Efficient Frontier ---" << std::endl;
    test_efficient_frontier_shape();

    std::cout << "\n--- Task 4.4: Yield Curve ---" << std::endl;
    test_yield_curve_interpolation();
    test_yield_curve_flat_curve_discount();

    std::cout << "\n--- Task 4.4: Bond Analytics ---" << std::endl;
    test_bond_par_when_coupon_equals_ytm();
    test_bond_price_reference_value();
    test_bond_price_from_yield_curve();
    test_bond_ytm_round_trip();
    test_bond_duration_zero_coupon();
    test_bond_duration_reference_value();
    test_bond_convexity();

    std::cout << "\n--- Cross-Module Consistency ---" << std::endl;
    test_put_call_parity_across_models();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
