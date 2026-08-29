#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Financial Tensor Operations Library - Phase 1, Issue #002 (Task 2.1)
//
// Hardware-acceleration-ready financial computation kernels. This module
// provides the CPU reference implementations of the core financial math used
// across the platform. The API is deliberately tensor-shaped (flat buffers +
// explicit element counts) so each kernel maps cleanly onto GGML tensor
// operations when a GGML backend is available, while remaining fully
// functional and testable as standalone, dependency-free C++.
//
// Implemented kernels:
//   - Black-Scholes option pricing (scalar + batch)
//   - Option Greeks (Delta, Gamma, Vega, Theta, Rho) (scalar + batch)
//   - Binomial tree option pricing (European + American)
//   - Monte Carlo risk simulation (GBM paths, VaR / Expected Shortfall)
//   - Portfolio linear algebra (covariance, matrix-vector, dot products)
//   - Statistical helpers (mean, variance, stddev, normal CDF/PDF, inverse CDF)
//
// Backend dispatch:
//   The kernels run on the CPU by default with vectorization-friendly loops.
//   `FinancialBackend` (financial-backend.h) can be used to detect and select
//   a hardware backend at runtime; when a GGML backend is selected these same
//   entry points can route through GGML. The CPU path is always available as
//   a correctness reference and fallback.
// ============================================================================

namespace ggnucash {
namespace tensors {

// ============================================================================
// Option Types
// ============================================================================

enum class OptionType {
    CALL,
    PUT
};

// ============================================================================
// Black-Scholes Results
// ============================================================================

struct BlackScholesResult {
    double price;
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;

    BlackScholesResult()
        : price(0.0), delta(0.0), gamma(0.0), vega(0.0), theta(0.0), rho(0.0) {}
};

// ============================================================================
// Monte Carlo Risk Results
// ============================================================================

struct MonteCarloResult {
    double mean_terminal_value;     // Expected portfolio value at horizon
    double std_dev;                 // Std deviation of terminal values
    double value_at_risk;           // VaR at the requested confidence level
    double expected_shortfall;      // ES (CVaR) at the requested confidence level
    double confidence_level;        // e.g. 0.95, 0.99
    uint64_t num_paths;             // Paths actually simulated
    double min_value;               // Worst simulated outcome
    double max_value;               // Best simulated outcome

    MonteCarloResult()
        : mean_terminal_value(0.0),
          std_dev(0.0),
          value_at_risk(0.0),
          expected_shortfall(0.0),
          confidence_level(0.95),
          num_paths(0),
          min_value(0.0),
          max_value(0.0) {}
};

// ============================================================================
// Core Scalar Math (statistical primitives)
// ============================================================================

// Standard normal probability density function.
double normal_pdf(double x);

// Standard normal cumulative distribution function (Abramowitz-Stegun 7-digit
// accuracy, monotonic, numerically stable across the full range).
double normal_cdf(double x);

// Inverse standard normal CDF (quantile function). Rational approximation
// (Beasley-Springer-Moro) accurate to ~1e-9 for 1e-12 < p < 1-1e-12.
double normal_inv_cdf(double p);

// ============================================================================
// Black-Scholes Option Pricing (Task 4.1 / Issue #002)
// ============================================================================

// Price a European option and compute all Greeks.
//
//   spot          current price of the underlying
//   strike        option strike price
//   time_to_expiry  time to expiration in years (ACT/365)
//   volatility    annualized volatility (sigma), e.g. 0.20 for 20%
//   risk_free_rate  continuously-compounded annual risk-free rate
//   type          CALL or PUT
BlackScholesResult black_scholes(double spot,
                                 double strike,
                                 double time_to_expiry,
                                 double volatility,
                                 double risk_free_rate,
                                 OptionType type);

// Vectorized Black-Scholes pricing across an option chain. Each input array
// must have `count` elements. Results are written into the pre-sized `out`
// array (also `count` elements). This is the batch form intended for GPU
// dispatch; the CPU reference loops over the chain.
void black_scholes_batch(const double * spot,
                         const double * strike,
                         const double * time_to_expiry,
                         const double * volatility,
                         double risk_free_rate,
                         const int * is_call,      // non-zero => CALL
                         size_t count,
                         BlackScholesResult * out);

// ============================================================================
// Binomial Tree Option Pricing (Task 4.1)
// ============================================================================

// Cox-Ross-Rubinstein binomial tree. Supports European and American exercise.
double binomial_option_price(double spot,
                             double strike,
                             double time_to_expiry,
                             double volatility,
                             double risk_free_rate,
                             OptionType type,
                             int steps,
                             bool american);

// ============================================================================
// Monte Carlo Risk Simulation (Task 4.2 / Issue #002)
// ============================================================================

// Simulate a single asset/portfolio under geometric Brownian motion and
// derive VaR and Expected Shortfall from the terminal value distribution.
//
//   initial_value     starting portfolio value
//   drift             expected annual return (mu)
//   volatility        annualized volatility (sigma)
//   time_horizon      horizon in years
//   num_paths         number of Monte Carlo paths
//   confidence_level  e.g. 0.95 or 0.99
//   seed              RNG seed for reproducibility (0 => nondeterministic)
//
// Returns aggregated risk statistics. The per-path terminal values can be
// retrieved via the optional `terminal_values_out` buffer (num_paths elems)
// when non-null.
MonteCarloResult monte_carlo_risk(double initial_value,
                                  double drift,
                                  double volatility,
                                  double time_horizon,
                                  uint64_t num_paths,
                                  double confidence_level,
                                  uint64_t seed = 0,
                                  double * terminal_values_out = nullptr);

// ============================================================================
// Portfolio / Linear-Algebra Kernels (Task 4.3)
// ============================================================================

// Dot product of two vectors.
double dot(const double * a, const double * b, size_t n);

// Portfolio expected return: dot(weights, expected_returns).
double portfolio_expected_return(const double * weights,
                                 const double * expected_returns,
                                 size_t n);

// Portfolio variance: w^T * Cov * w.
//   covariance is row-major n x n.
double portfolio_variance(const double * weights,
                          const double * covariance,
                          size_t n);

// Portfolio volatility (std dev) = sqrt(portfolio_variance).
double portfolio_volatility(const double * weights,
                            const double * covariance,
                            size_t n);

// Sample covariance matrix of a set of return series.
//   returns   row-major: num_assets series each of length num_observations
//             (i.e. returns[asset * num_observations + t])
//   cov_out   row-major num_assets x num_assets output buffer
void covariance_matrix(const double * returns,
                       size_t num_assets,
                       size_t num_observations,
                       double * cov_out);

// General matrix-vector product y = A x (A is row-major m x n).
void matvec(const double * A, const double * x, double * y, size_t m, size_t n);

// ============================================================================
// Statistical Helpers
// ============================================================================

double mean(const double * data, size_t n);
double variance(const double * data, size_t n);   // sample variance (n-1)
double stddev(const double * data, size_t n);

} // namespace tensors
} // namespace ggnucash
