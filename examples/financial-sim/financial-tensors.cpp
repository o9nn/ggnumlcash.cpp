#include "financial-tensors.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace ggnucash {
namespace tensors {

namespace {
constexpr double kInvSqrt2Pi = 0.39894228040143267794;  // 1/sqrt(2*pi)
constexpr double kSqrt2Pi    = 2.50662827463100050242;  // sqrt(2*pi)
} // namespace

// ============================================================================
// Core Scalar Math
// ============================================================================

double normal_pdf(double x) {
    return kInvSqrt2Pi * std::exp(-0.5 * x * x);
}

double normal_cdf(double x) {
    // Abramowitz & Stegun 26.2.17 via erfc for full-range stability.
    // Phi(x) = 0.5 * erfc(-x / sqrt(2))
    return 0.5 * std::erfc(-x * 0.70710678118654752440);
}

double normal_inv_cdf(double p) {
    if (p <= 0.0) return -std::numeric_limits<double>::infinity();
    if (p >= 1.0) return std::numeric_limits<double>::infinity();

    // Beasley-Springer-Moro rational approximation.
    static const double a[] = {2.50662823884, -18.61500062529, 41.39119773534,
                               -25.44106049637};
    static const double b[] = {-8.47351093090, 23.08336743743, -21.06224101826,
                               3.13082909833};
    static const double c[] = {0.3374754822726147, 0.9761690190917186,
                               0.1607979714918209, 0.0276438810333863,
                               0.0038405729373609, 0.0003951896511919,
                               0.0000321767881768, 0.0000002888167364,
                               0.0000003960315187};

    double u = p - 0.5;
    if (std::fabs(u) < 0.42) {
        double r = u * u;
        double num = ((a[3] * r + a[2]) * r + a[1]) * r + a[0];
        double den = (((b[3] * r + b[2]) * r + b[1]) * r + b[0]) * r + 1.0;
        return u * num / den;
    }

    double r = (u < 0.0) ? p : (1.0 - p);
    r = std::log(-std::log(r));
    double x = c[0];
    for (int i = 1; i < 9; i++) {
        x += c[i] * std::pow(r, i);
    }
    return (u < 0.0) ? -x : x;
}

// ============================================================================
// Black-Scholes
// ============================================================================

BlackScholesResult black_scholes(double spot,
                                 double strike,
                                 double time_to_expiry,
                                 double volatility,
                                 double risk_free_rate,
                                 OptionType type) {
    BlackScholesResult res;

    if (spot <= 0.0 || strike <= 0.0) {
        return res;  // Degenerate input
    }

    // At expiry the option is worth its intrinsic value.
    if (time_to_expiry <= 0.0) {
        bool is_call = (type == OptionType::CALL);
        double intrinsic = is_call ? std::max(0.0, spot - strike)
                                   : std::max(0.0, strike - spot);
        res.price = intrinsic;
        res.delta = is_call ? (spot > strike ? 1.0 : 0.0)
                            : (spot < strike ? -1.0 : 0.0);
        return res;
    }

    double sig = std::max(volatility, 1e-12);
    double sqrt_t = std::sqrt(time_to_expiry);
    double d1 = (std::log(spot / strike) +
                 (risk_free_rate + 0.5 * sig * sig) * time_to_expiry) /
                (sig * sqrt_t);
    double d2 = d1 - sig * sqrt_t;
    double df = std::exp(-risk_free_rate * time_to_expiry);
    double pdf_d1 = normal_pdf(d1);

    if (type == OptionType::CALL) {
        res.price = spot * normal_cdf(d1) - strike * df * normal_cdf(d2);
        res.delta = normal_cdf(d1);
        res.rho = strike * time_to_expiry * df * normal_cdf(d2) / 100.0;
        res.theta = (-(spot * pdf_d1 * sig) / (2.0 * sqrt_t) -
                     risk_free_rate * strike * df * normal_cdf(d2)) /
                    365.0;
    } else {
        res.price = strike * df * normal_cdf(-d2) - spot * normal_cdf(-d1);
        res.delta = normal_cdf(d1) - 1.0;
        res.rho = -strike * time_to_expiry * df * normal_cdf(-d2) / 100.0;
        res.theta = (-(spot * pdf_d1 * sig) / (2.0 * sqrt_t) +
                     risk_free_rate * strike * df * normal_cdf(-d2)) /
                    365.0;
    }

    // Gamma and Vega are identical for calls and puts.
    res.gamma = pdf_d1 / (spot * sig * sqrt_t);
    res.vega = spot * pdf_d1 * sqrt_t / 100.0;  // per 1% vol change

    return res;
}

void black_scholes_batch(const double * spot,
                         const double * strike,
                         const double * time_to_expiry,
                         const double * volatility,
                         double risk_free_rate,
                         const int * is_call,
                         size_t count,
                         BlackScholesResult * out) {
    // Vectorization-friendly loop: independent iterations, no loop-carried
    // dependencies, so the compiler/BLAS can vectorize and a GGML backend can
    // map each element to a thread.
    for (size_t i = 0; i < count; i++) {
        OptionType t = (is_call && is_call[i]) ? OptionType::CALL : OptionType::PUT;
        out[i] = black_scholes(spot[i], strike[i], time_to_expiry[i],
                               volatility[i], risk_free_rate, t);
    }
}

// ============================================================================
// Binomial Tree (Cox-Ross-Rubinstein)
// ============================================================================

double binomial_option_price(double spot,
                             double strike,
                             double time_to_expiry,
                             double volatility,
                             double risk_free_rate,
                             OptionType type,
                             int steps,
                             bool american) {
    if (steps <= 0) steps = 1;
    if (time_to_expiry <= 0.0) {
        return (type == OptionType::CALL) ? std::max(0.0, spot - strike)
                                          : std::max(0.0, strike - spot);
    }

    double dt = time_to_expiry / steps;
    double sig = std::max(volatility, 1e-12);
    double u = std::exp(sig * std::sqrt(dt));
    double d = 1.0 / u;
    double disc = std::exp(-risk_free_rate * dt);
    double p = (std::exp(risk_free_rate * dt) - d) / (u - d);
    bool is_call = (type == OptionType::CALL);

    // Terminal asset prices and option payoffs.
    std::vector<double> prices(steps + 1);
    for (int i = 0; i <= steps; i++) {
        double s = spot * std::pow(u, steps - i) * std::pow(d, i);
        prices[i] = is_call ? std::max(0.0, s - strike)
                            : std::max(0.0, strike - s);
    }

    // Backward induction.
    for (int step = steps - 1; step >= 0; step--) {
        for (int i = 0; i <= step; i++) {
            double hold = disc * (p * prices[i] + (1.0 - p) * prices[i + 1]);
            if (american) {
                double s = spot * std::pow(u, step - i) * std::pow(d, i);
                double exercise = is_call ? std::max(0.0, s - strike)
                                          : std::max(0.0, strike - s);
                prices[i] = std::max(hold, exercise);
            } else {
                prices[i] = hold;
            }
        }
    }

    return prices[0];
}

// ============================================================================
// Monte Carlo Risk Simulation
// ============================================================================

MonteCarloResult monte_carlo_risk(double initial_value,
                                  double drift,
                                  double volatility,
                                  double time_horizon,
                                  uint64_t num_paths,
                                  double confidence_level,
                                  uint64_t seed,
                                  double * terminal_values_out) {
    MonteCarloResult res;
    res.confidence_level = confidence_level;
    res.num_paths = num_paths;

    if (num_paths == 0 || initial_value <= 0.0) {
        return res;
    }

    std::mt19937_64 rng(seed != 0 ? seed : std::random_device{}());
    std::normal_distribution<double> norm(0.0, 1.0);

    double sig = std::max(volatility, 1e-12);
    double drift_term = (drift - 0.5 * sig * sig) * time_horizon;
    double diff_term = sig * std::sqrt(time_horizon);

    std::vector<double> local;
    double * terminal = terminal_values_out;
    if (!terminal) {
        local.resize(num_paths);
        terminal = local.data();
    }

    // Simulate terminal values under GBM.
    for (uint64_t i = 0; i < num_paths; i++) {
        double z = norm(rng);
        terminal[i] = initial_value * std::exp(drift_term + diff_term * z);
    }

    // Aggregate statistics.
    double sum = std::accumulate(terminal, terminal + num_paths, 0.0);
    res.mean_terminal_value = sum / static_cast<double>(num_paths);

    double sq_sum = 0.0;
    for (uint64_t i = 0; i < num_paths; i++) {
        double diff = terminal[i] - res.mean_terminal_value;
        sq_sum += diff * diff;
    }
    res.std_dev = std::sqrt(sq_sum / static_cast<double>(num_paths));

    // Sort for percentile-based VaR / ES (loss = initial - terminal).
    std::vector<double> sorted(terminal, terminal + num_paths);
    std::sort(sorted.begin(), sorted.end());
    res.min_value = sorted.front();
    res.max_value = sorted.back();

    // VaR: the loss at the (1 - confidence) lower-tail percentile.
    double alpha = 1.0 - confidence_level;
    size_t var_index = static_cast<size_t>(std::floor(alpha * num_paths));
    if (var_index >= num_paths) var_index = num_paths - 1;
    double var_terminal = sorted[var_index];
    res.value_at_risk = initial_value - var_terminal;

    // Expected shortfall: mean loss beyond the VaR threshold.
    double es_sum = 0.0;
    size_t es_count = var_index + 1;
    for (size_t i = 0; i <= var_index; i++) {
        es_sum += (initial_value - sorted[i]);
    }
    res.expected_shortfall = es_count > 0 ? es_sum / static_cast<double>(es_count)
                                          : res.value_at_risk;

    return res;
}

// ============================================================================
// Portfolio / Linear-Algebra Kernels
// ============================================================================

double dot(const double * a, const double * b, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) {
        acc += a[i] * b[i];
    }
    return acc;
}

double portfolio_expected_return(const double * weights,
                                 const double * expected_returns,
                                 size_t n) {
    return dot(weights, expected_returns, n);
}

double portfolio_variance(const double * weights,
                          const double * covariance,
                          size_t n) {
    // w^T * Cov * w computed as dot(w, Cov*w)
    std::vector<double> tmp(n, 0.0);
    matvec(covariance, weights, tmp.data(), n, n);
    return dot(weights, tmp.data(), n);
}

double portfolio_volatility(const double * weights,
                            const double * covariance,
                            size_t n) {
    double var = portfolio_variance(weights, covariance, n);
    return std::sqrt(std::max(var, 0.0));
}

void covariance_matrix(const double * returns,
                       size_t num_assets,
                       size_t num_observations,
                       double * cov_out) {
    if (num_assets == 0 || num_observations < 2) return;

    // Compute per-asset means.
    std::vector<double> means(num_assets, 0.0);
    for (size_t a = 0; a < num_assets; a++) {
        const double * series = returns + a * num_observations;
        means[a] = mean(series, num_observations);
    }

    // Cov(i,j) = sum_t (r_i,t - mu_i)(r_j,t - mu_j) / (n-1)
    double inv = 1.0 / static_cast<double>(num_observations - 1);
    for (size_t i = 0; i < num_assets; i++) {
        const double * si = returns + i * num_observations;
        for (size_t j = 0; j <= i; j++) {
            const double * sj = returns + j * num_observations;
            double acc = 0.0;
            for (size_t t = 0; t < num_observations; t++) {
                acc += (si[t] - means[i]) * (sj[t] - means[j]);
            }
            double cov = acc * inv;
            cov_out[i * num_assets + j] = cov;
            cov_out[j * num_assets + i] = cov;  // symmetric
        }
    }
}

void matvec(const double * A, const double * x, double * y, size_t m, size_t n) {
    for (size_t i = 0; i < m; i++) {
        const double * row = A + i * n;
        double acc = 0.0;
        for (size_t j = 0; j < n; j++) {
            acc += row[j] * x[j];
        }
        y[i] = acc;
    }
}

// ============================================================================
// Statistical Helpers
// ============================================================================

double mean(const double * data, size_t n) {
    if (n == 0) return 0.0;
    return std::accumulate(data, data + n, 0.0) / static_cast<double>(n);
}

double variance(const double * data, size_t n) {
    if (n < 2) return 0.0;
    double mu = mean(data, n);
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = data[i] - mu;
        acc += d * d;
    }
    return acc / static_cast<double>(n - 1);
}

double stddev(const double * data, size_t n) {
    return std::sqrt(variance(data, n));
}

} // namespace tensors
} // namespace ggnucash
