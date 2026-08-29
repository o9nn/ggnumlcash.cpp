#include "quant-models.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

// ============================================================================
// Quantitative Finance Models - Phase 2, Issue #004
// ============================================================================

namespace ggnucash {
namespace quant {

namespace {

// ============================================================================
// Tiny dense linear solver (no BLAS dependency)
// ============================================================================

// Solve A x = b in place by Gaussian elimination with partial pivoting.
//   A   row-major n x n matrix (destroyed)
//   b   right-hand side, overwritten with the solution
// Returns false when the matrix is (numerically) singular.
bool solve_linear_system(std::vector<double> & A, std::vector<double> & b, size_t n) {
    for (size_t col = 0; col < n; col++) {
        // Partial pivoting.
        size_t pivot = col;
        double best = std::fabs(A[col * n + col]);
        for (size_t row = col + 1; row < n; row++) {
            double v = std::fabs(A[row * n + col]);
            if (v > best) {
                best = v;
                pivot = row;
            }
        }
        if (best < 1e-14) {
            return false;
        }
        if (pivot != col) {
            for (size_t j = col; j < n; j++) {
                std::swap(A[col * n + j], A[pivot * n + j]);
            }
            std::swap(b[col], b[pivot]);
        }
        double inv_diag = 1.0 / A[col * n + col];
        for (size_t row = col + 1; row < n; row++) {
            double factor = A[row * n + col] * inv_diag;
            if (factor == 0.0) {
                continue;
            }
            for (size_t j = col; j < n; j++) {
                A[row * n + j] -= factor * A[col * n + j];
            }
            b[row] -= factor * b[col];
        }
    }
    // Back substitution.
    for (size_t i = n; i-- > 0;) {
        double sum = b[i];
        for (size_t j = i + 1; j < n; j++) {
            sum -= A[i * n + j] * b[j];
        }
        b[i] = sum / A[i * n + i];
    }
    return true;
}

double option_payoff(double spot, double strike, OptionType type) {
    return (type == OptionType::CALL) ? std::max(0.0, spot - strike)
                                      : std::max(0.0, strike - spot);
}

} // anonymous namespace

// ============================================================================
// Task 4.1 - Trinomial Tree
// ============================================================================

double trinomial_option_price(double spot,
                              double strike,
                              double time_to_expiry,
                              double volatility,
                              double risk_free_rate,
                              OptionType type,
                              int steps,
                              bool american) {
    if (steps <= 0) steps = 1;
    if (time_to_expiry <= 0.0) {
        return option_payoff(spot, strike, type);
    }

    double dt = time_to_expiry / steps;
    double sig = std::max(volatility, 1e-12);
    double u = std::exp(sig * std::sqrt(2.0 * dt));
    double d = 1.0 / u;
    double m = 1.0;

    // Moment-matched risk-neutral branch probabilities.
    double growth = std::exp(risk_free_rate * dt);
    double denom_u = 1.0 - d;
    double denom_d = u - 1.0;
    double p_u = growth * denom_u / (u * denom_u * denom_u);
    double p_d = growth * denom_d / (d * denom_d * denom_d);
    double p_m = 1.0 - p_u - p_d;
    double disc = std::exp(-risk_free_rate * dt);

    // Terminal layer: index j holds spot * u^j * d^(steps - j).
    std::vector<double> values(2 * steps + 1);
    for (int j = 0; j <= 2 * steps; j++) {
        double s = spot * std::pow(u, j) * std::pow(d, steps - j);
        values[j] = option_payoff(s, strike, type);
    }

    // Backward induction.
    for (int step = steps - 1; step >= 0; step--) {
        int count = 2 * step + 1;
        for (int j = 0; j < count; j++) {
            double hold = disc * (p_u * values[j] + p_m * values[j + 1] + p_d * values[j + 2]);
            if (american) {
                double s = spot * std::pow(u, j) * std::pow(d, step - j);
                values[j] = std::max(hold, option_payoff(s, strike, type));
            } else {
                values[j] = hold;
            }
        }
    }

    return values[0];
}

// ============================================================================
// Task 4.1 - Monte Carlo Option Pricing (antithetic variates)
// ============================================================================

MonteCarloOptionResult monte_carlo_option_price(double spot,
                                                double strike,
                                                double time_to_expiry,
                                                double volatility,
                                                double risk_free_rate,
                                                OptionType type,
                                                uint64_t num_paths,
                                                uint64_t seed) {
    MonteCarloOptionResult res;
    if (num_paths == 0) {
        return res;
    }
    res.num_paths = num_paths;

    std::mt19937_64 rng(seed != 0 ? seed : std::random_device{}());
    std::normal_distribution<double> norm(0.0, 1.0);

    double sig = std::max(volatility, 1e-12);
    double drift = (risk_free_rate - 0.5 * sig * sig) * time_to_expiry;
    double diff = sig * std::sqrt(time_to_expiry);
    double disc = std::exp(-risk_free_rate * time_to_expiry);

    // Antithetic variates: each draw z also contributes -z.
    uint64_t num_pairs = (num_paths + 1) / 2;
    double sum = 0.0;
    double sq_sum = 0.0;
    for (uint64_t i = 0; i < num_pairs; i++) {
        double z = norm(rng);
        for (int sign = 1; sign >= -1; sign -= 2) {
            double s_terminal = spot * std::exp(drift + sign * diff * z);
            double payoff = disc * option_payoff(s_terminal, strike, type);
            sum += payoff;
            sq_sum += payoff * payoff;
        }
    }

    double n = static_cast<double>(2 * num_pairs);
    res.num_paths = 2 * num_pairs;
    double mean = sum / n;
    double var = (sq_sum - n * mean * mean) / (n - 1.0);
    res.price = mean;
    res.std_error = std::sqrt(std::max(var, 0.0) / n);
    return res;
}

// ============================================================================
// Task 4.1 - Implied Volatility
// ============================================================================

double implied_volatility(double market_price,
                          double spot,
                          double strike,
                          double time_to_expiry,
                          double risk_free_rate,
                          OptionType type,
                          double tolerance,
                          int max_iterations) {
    if (time_to_expiry <= 0.0 || spot <= 0.0 || strike <= 0.0) {
        return std::nan("");
    }

    // No-arbitrage bounds: intrinsic <= price <= (call ? spot : K e^{-rT}).
    double lower = (type == OptionType::CALL)
                 ? std::max(0.0, spot - strike * std::exp(-risk_free_rate * time_to_expiry))
                 : std::max(0.0, strike * std::exp(-risk_free_rate * time_to_expiry) - spot);
    double upper = (type == OptionType::CALL) ? spot
                                              : strike * std::exp(-risk_free_rate * time_to_expiry);
    if (market_price < lower - 1e-12 || market_price > upper + 1e-12) {
        return std::nan("");
    }
    if (std::fabs(market_price - lower) < tolerance) {
        return 0.0;
    }

    // Newton-Raphson on vega with bisection fallback.
    double lo = 1e-6;
    double hi = 5.0;
    double sigma = 0.25;
    for (int i = 0; i < max_iterations; i++) {
        auto res = tensors::black_scholes(spot, strike, time_to_expiry, sigma, risk_free_rate, type);
        double diff = res.price - market_price;
        if (std::fabs(diff) < tolerance) {
            return sigma;
        }
        // Maintain the bisection bracket: price is increasing in sigma.
        if (diff > 0.0) {
            hi = sigma;
        } else {
            lo = sigma;
        }
        double step = (res.vega > 1e-10) ? diff / res.vega : 0.0;
        double next = sigma - step;
        if (step == 0.0 || next <= lo || next >= hi) {
            next = 0.5 * (lo + hi);
        }
        sigma = next;
    }
    return std::nan("");
}

// ============================================================================
// Task 4.1 - Volatility Surface
// ============================================================================

VolatilitySurface::VolatilitySurface(const std::vector<double> & strikes_in,
                                     const std::vector<double> & tenors_in,
                                     const std::vector<double> & vols_in)
    : strikes(strikes_in), tenors(tenors_in), vols(vols_in) {}

bool VolatilitySurface::is_valid() const {
    if (strikes.empty() || tenors.empty() || vols.size() != strikes.size() * tenors.size()) {
        return false;
    }
    for (size_t i = 1; i < strikes.size(); i++) {
        if (strikes[i] <= strikes[i - 1]) return false;
    }
    for (size_t i = 1; i < tenors.size(); i++) {
        if (tenors[i] <= tenors[i - 1]) return false;
    }
    return true;
}

double VolatilitySurface::volatility(double strike, double tenor) const {
    if (!is_valid()) {
        return std::nan("");
    }

    // Flat extrapolation: clamp to the nearest grid edge.
    auto clamp_index = [](const std::vector<double> & grid, double x, double & frac) -> size_t {
        if (x <= grid.front()) {
            frac = 0.0;
            return 0;
        }
        if (x >= grid.back()) {
            frac = 0.0;
            return grid.size() - 1;
        }
        auto it = std::lower_bound(grid.begin(), grid.end(), x);
        size_t hi_idx = static_cast<size_t>(it - grid.begin());
        size_t lo_idx = hi_idx - 1;
        frac = (x - grid[lo_idx]) / (grid[hi_idx] - grid[lo_idx]);
        return lo_idx;
    };

    double sk_frac = 0.0;
    double tn_frac = 0.0;
    size_t sk0 = clamp_index(strikes, strike, sk_frac);
    size_t tn0 = clamp_index(tenors, tenor, tn_frac);
    size_t sk1 = std::min(sk0 + 1, strikes.size() - 1);
    size_t tn1 = std::min(tn0 + 1, tenors.size() - 1);

    double v00 = vols[tn0 * strikes.size() + sk0];
    double v01 = vols[tn0 * strikes.size() + sk1];
    double v10 = vols[tn1 * strikes.size() + sk0];
    double v11 = vols[tn1 * strikes.size() + sk1];

    double top = v00 + sk_frac * (v01 - v00);
    double bot = v10 + sk_frac * (v11 - v10);
    return top + tn_frac * (bot - top);
}

// ============================================================================
// Task 4.2 - Historical VaR / ES
// ============================================================================

double historical_var(const double * returns, size_t n, double confidence) {
    if (returns == nullptr || n == 0) {
        return 0.0;
    }
    std::vector<double> sorted(returns, returns + n);
    std::sort(sorted.begin(), sorted.end());
    double alpha = 1.0 - confidence;
    size_t idx = static_cast<size_t>(std::floor(alpha * n));
    if (idx >= n) idx = n - 1;
    return -sorted[idx];
}

double historical_es(const double * returns, size_t n, double confidence) {
    if (returns == nullptr || n == 0) {
        return 0.0;
    }
    std::vector<double> sorted(returns, returns + n);
    std::sort(sorted.begin(), sorted.end());
    double alpha = 1.0 - confidence;
    size_t idx = static_cast<size_t>(std::floor(alpha * n));
    if (idx >= n) idx = n - 1;
    double tail_sum = 0.0;
    for (size_t i = 0; i <= idx; i++) {
        tail_sum += -sorted[i];
    }
    return tail_sum / static_cast<double>(idx + 1);
}

// ============================================================================
// Task 4.2 - Parametric VaR / ES
// ============================================================================

double parametric_var(double mean, double stddev, double confidence, double holding_value) {
    double z = tensors::normal_inv_cdf(confidence);
    return holding_value * (z * stddev - mean);
}

double parametric_es(double mean, double stddev, double confidence, double holding_value) {
    double z = tensors::normal_inv_cdf(confidence);
    double alpha = 1.0 - confidence;
    if (alpha <= 0.0) {
        return holding_value;
    }
    return holding_value * (stddev * tensors::normal_pdf(z) / alpha - mean);
}

// ============================================================================
// Task 4.2 - Monte Carlo VaR (wraps tensors::monte_carlo_risk)
// ============================================================================

tensors::MonteCarloResult monte_carlo_var(double initial_value,
                                          double drift,
                                          double volatility,
                                          double time_horizon,
                                          uint64_t num_paths,
                                          double confidence_level,
                                          uint64_t seed) {
    return tensors::monte_carlo_risk(initial_value, drift, volatility, time_horizon,
                                     num_paths, confidence_level, seed, nullptr);
}

// ============================================================================
// Task 4.2 - Stress Scenarios
// ============================================================================

double apply_stress_scenario(const double * position_values,
                             size_t n,
                             const StressScenario & scenario) {
    if (position_values == nullptr) {
        return 0.0;
    }
    double pnl = 0.0;
    for (const auto & shock : scenario.asset_shocks) {
        size_t idx = shock.first;
        if (idx < n) {
            pnl += position_values[idx] * shock.second;
        }
    }
    return pnl;
}

std::vector<StressScenario> standard_stress_scenarios(size_t n, double rate_sensitivity) {
    std::vector<StressScenario> scenarios;

    auto uniform = [&](const char * name, double shock) {
        StressScenario s;
        s.name = name;
        s.asset_shocks.reserve(n);
        for (size_t i = 0; i < n; i++) {
            s.asset_shocks.emplace_back(i, shock);
        }
        scenarios.push_back(std::move(s));
    };

    uniform("Equity Crash 2008", -0.20);
    uniform("Black Monday 1987", -0.226);

    // Rates +200bp parallel shift: P&L ~ -duration * 0.02 * value.
    StressScenario rates;
    rates.name = "Rates +200bp Parallel";
    rates.asset_shocks.reserve(n);
    for (size_t i = 0; i < n; i++) {
        rates.asset_shocks.emplace_back(i, -0.02 * rate_sensitivity);
    }
    scenarios.push_back(std::move(rates));

    uniform("Vol Spike", -0.10);

    return scenarios;
}

// ============================================================================
// Task 4.2 - Risk Monitor
// ============================================================================

RiskMonitor::RiskMonitor() = default;

void RiskMonitor::set_portfolio(const std::vector<double> & position_values,
                                const std::vector<double> & expected_returns_in,
                                const std::vector<double> & covariance_in) {
    values = position_values;
    expected_returns = expected_returns_in;
    covariance = covariance_in;
    var_breached = false;
    drawdown_breached = false;
}

void RiskMonitor::set_var_limit(double confidence_in, double var_limit_in) {
    confidence = confidence_in;
    var_limit = var_limit_in;
}

void RiskMonitor::set_drawdown_limit(double drawdown_limit_in) {
    drawdown_limit = drawdown_limit_in;
}

void RiskMonitor::set_breach_callback(std::function<void(const RiskBreach &)> cb) {
    callback = std::move(cb);
}

double RiskMonitor::portfolio_value() const {
    return std::accumulate(values.begin(), values.end(), 0.0);
}

double RiskMonitor::current_var() const {
    size_t n = values.size();
    if (n == 0 || confidence <= 0.0 || expected_returns.size() != n ||
        covariance.size() != n * n) {
        return 0.0;
    }
    // Convert position values to portfolio return moments: the portfolio's
    // per-period return mean/stddev are value-weighted aggregates.
    double total = portfolio_value();
    if (total <= 0.0) {
        return 0.0;
    }
    double mean = tensors::portfolio_expected_return(values.data(), expected_returns.data(), n);
    double var = tensors::portfolio_variance(values.data(), covariance.data(), n);
    // mean/var are in currency terms (values act as currency weights); the
    // parametric formula applies directly to the currency P&L distribution.
    double z = tensors::normal_inv_cdf(confidence);
    return z * std::sqrt(std::max(var, 0.0)) - mean;
}

double RiskMonitor::current_drawdown() const {
    if (!has_peak || peak_value <= 0.0) {
        return 0.0;
    }
    double value = portfolio_value();
    if (value >= peak_value) {
        return 0.0;
    }
    return (peak_value - value) / peak_value;
}

void RiskMonitor::update_prices(const std::vector<double> & new_position_values) {
    values = new_position_values;
    double value = portfolio_value();
    if (!has_peak || value > peak_value) {
        peak_value = value;
        has_peak = true;
    }
}

bool RiskMonitor::check_limits() {
    var_breached = false;
    drawdown_breached = false;

    if (confidence > 0.0 && var_limit > 0.0) {
        double var = current_var();
        if (var > var_limit) {
            var_breached = true;
            if (callback) {
                RiskBreach breach;
                breach.type = RiskBreach::Type::VAR_LIMIT;
                breach.limit = var_limit;
                breach.observed = var;
                breach.message = "VaR limit breached: VaR " + std::to_string(var) +
                                 " exceeds limit " + std::to_string(var_limit);
                callback(breach);
            }
        }
    }

    if (drawdown_limit > 0.0 && has_peak) {
        double dd = current_drawdown();
        if (dd > drawdown_limit) {
            drawdown_breached = true;
            if (callback) {
                RiskBreach breach;
                breach.type = RiskBreach::Type::DRAWDOWN_LIMIT;
                breach.limit = drawdown_limit;
                breach.observed = dd;
                breach.message = "Drawdown limit breached: drawdown " + std::to_string(dd) +
                                 " exceeds limit " + std::to_string(drawdown_limit);
                callback(breach);
            }
        }
    }

    return var_breached || drawdown_breached;
}

// ============================================================================
// Task 4.3 - Markowitz Optimization
// ============================================================================

namespace {

// Solve the unconstrained two-constraint Markowitz KKT system on the given
// asset index subset. Returns false when the system is singular.
bool markowitz_kkt(const double * expected_returns,
                   const double * covariance,
                   size_t n,
                   const std::vector<size_t> & free_assets,
                   double target_return,
                   std::vector<double> & weights_out) {
    size_t m = free_assets.size();
    size_t dim = m + 2;

    std::vector<double> A(dim * dim, 0.0);
    std::vector<double> b(dim, 0.0);

    for (size_t i = 0; i < m; i++) {
        for (size_t j = 0; j < m; j++) {
            A[i * dim + j] = covariance[free_assets[i] * n + free_assets[j]];
        }
        A[i * dim + (m + 0)] = 1.0;
        A[(m + 0) * dim + i] = 1.0;
        A[i * dim + (m + 1)] = expected_returns[free_assets[i]];
        A[(m + 1) * dim + i] = expected_returns[free_assets[i]];
    }
    b[m + 0] = 1.0;
    b[m + 1] = target_return;

    if (!solve_linear_system(A, b, dim)) {
        return false;
    }

    weights_out.assign(n, 0.0);
    for (size_t i = 0; i < m; i++) {
        weights_out[free_assets[i]] = b[i];
    }
    return true;
}

} // anonymous namespace

std::vector<double> markowitz_optimize(const double * expected_returns,
                                       const double * covariance,
                                       size_t n,
                                       double target_return,
                                       bool allow_short) {
    if (expected_returns == nullptr || covariance == nullptr || n == 0) {
        return {};
    }

    std::vector<size_t> free_assets(n);
    std::iota(free_assets.begin(), free_assets.end(), 0);

    std::vector<double> weights;
    // Active-set iteration for the no-short-selling constraint: clamp the
    // most negative weight to zero and re-solve on the remaining assets.
    for (size_t iter = 0; iter < n; iter++) {
        if (free_assets.size() == 1) {
            // Degenerate single-asset portfolio: invest everything in it.
            weights.assign(n, 0.0);
            weights[free_assets[0]] = 1.0;
            return weights;
        }
        if (!markowitz_kkt(expected_returns, covariance, n, free_assets,
                           target_return, weights)) {
            return {};
        }
        if (allow_short) {
            return weights;
        }
        // Find the most negative weight among the free assets.
        size_t worst = free_assets.size();
        double min_w = 0.0;
        for (size_t i = 0; i < free_assets.size(); i++) {
            double w = weights[free_assets[i]];
            if (w < min_w) {
                min_w = w;
                worst = i;
            }
        }
        if (worst == free_assets.size()) {
            return weights;  // all weights non-negative
        }
        free_assets.erase(free_assets.begin() + static_cast<long>(worst));
    }
    return weights;
}

std::vector<double> global_minimum_variance_portfolio(const double * covariance, size_t n) {
    if (covariance == nullptr || n == 0) {
        return {};
    }
    // Solve C x = 1, then w = x / (1' x).
    std::vector<double> A(covariance, covariance + n * n);
    std::vector<double> x(n, 1.0);
    if (!solve_linear_system(A, x, n)) {
        return {};
    }
    double denom = std::accumulate(x.begin(), x.end(), 0.0);
    if (std::fabs(denom) < 1e-14) {
        return {};
    }
    for (double & v : x) {
        v /= denom;
    }
    return x;
}

// ============================================================================
// Task 4.3 - Black-Litterman
// ============================================================================

std::vector<double> black_litterman(const double * covariance,
                                    size_t n,
                                    const double * market_weights,
                                    double risk_aversion,
                                    double tau,
                                    const double * P,
                                    const double * Q,
                                    const double * omega,
                                    size_t k) {
    if (covariance == nullptr || market_weights == nullptr || n == 0) {
        return {};
    }

    // Prior equilibrium returns: pi = delta * C * w_mkt.
    std::vector<double> cw(n, 0.0);
    tensors::matvec(covariance, market_weights, cw.data(), n, n);
    std::vector<double> pi(n);
    for (size_t i = 0; i < n; i++) {
        pi[i] = risk_aversion * cw[i];
    }

    if (k == 0 || P == nullptr || Q == nullptr || omega == nullptr) {
        return pi;
    }

    // A = P * (tau C) P' + Omega  (k x k)
    std::vector<double> tcp(n * n);
    for (size_t i = 0; i < n * n; i++) {
        tcp[i] = tau * covariance[i];
    }
    // tmp = P * tau C  (k x n)
    std::vector<double> tmp(k * n, 0.0);
    for (size_t v = 0; v < k; v++) {
        for (size_t j = 0; j < n; j++) {
            double sum = 0.0;
            for (size_t i = 0; i < n; i++) {
                sum += P[v * n + i] * tcp[i * n + j];
            }
            tmp[v * n + j] = sum;
        }
    }
    // A = tmp * P' + Omega
    std::vector<double> A(k * k);
    for (size_t v = 0; v < k; v++) {
        for (size_t u = 0; u < k; u++) {
            double sum = 0.0;
            for (size_t i = 0; i < n; i++) {
                sum += tmp[v * n + i] * P[u * n + i];
            }
            A[v * k + u] = sum + omega[v * k + u];
        }
    }

    // r = Q - P * pi
    std::vector<double> r(k);
    for (size_t v = 0; v < k; v++) {
        double ppi = 0.0;
        for (size_t i = 0; i < n; i++) {
            ppi += P[v * n + i] * pi[i];
        }
        r[v] = Q[v] - ppi;
    }

    // Solve A y = r.
    if (!solve_linear_system(A, r, k)) {
        return {};
    }

    // mu = pi + tau C P' y = pi + tmp' y
    std::vector<double> mu(n);
    for (size_t i = 0; i < n; i++) {
        double adj = 0.0;
        for (size_t v = 0; v < k; v++) {
            adj += tmp[v * n + i] * r[v];
        }
        mu[i] = pi[i] + adj;
    }
    return mu;
}

std::vector<double> black_litterman(const double * covariance,
                                    size_t n,
                                    const double * market_weights,
                                    double risk_aversion,
                                    double tau,
                                    const double * P,
                                    const double * Q,
                                    size_t k) {
    if (P == nullptr || k == 0) {
        return black_litterman(covariance, n, market_weights, risk_aversion,
                               tau, P, Q, static_cast<const double *>(nullptr), k);
    }
    // omega = tau * diag(P C P')
    std::vector<double> omega(k * k, 0.0);
    for (size_t v = 0; v < k; v++) {
        double sum = 0.0;
        for (size_t i = 0; i < n; i++) {
            for (size_t j = 0; j < n; j++) {
                sum += P[v * n + i] * covariance[i * n + j] * P[v * n + j];
            }
        }
        omega[v * k + v] = tau * sum;
    }
    return black_litterman(covariance, n, market_weights, risk_aversion, tau,
                           P, Q, omega.data(), k);
}

// ============================================================================
// Task 4.3 - Risk Parity
// ============================================================================

std::vector<double> risk_parity(const double * covariance, size_t n, int iterations) {
    if (covariance == nullptr || n == 0 || iterations <= 0) {
        return {};
    }
    for (size_t i = 0; i < n; i++) {
        if (covariance[i * n + i] <= 0.0) {
            return {};
        }
    }

    // Spinu formulation: minimize 1/2 x'Cx - sum_i ln(x_i) by cyclical
    // coordinate descent. The optimal x satisfies, for each i,
    //   a x_i^2 + b x_i - 1 = 0   with a = C_ii, b = sum_{j != i} C_ij x_j.
    std::vector<double> x(n, 1.0);
    std::vector<double> cx(n, 0.0);
    tensors::matvec(covariance, x.data(), cx.data(), n, n);

    for (int iter = 0; iter < iterations; iter++) {
        double max_change = 0.0;
        for (size_t i = 0; i < n; i++) {
            double a = covariance[i * n + i];
            double b = cx[i] - a * x[i];
            double disc = std::sqrt(b * b + 4.0 * a);
            double x_new = (-b + disc) / (2.0 * a);
            double delta = x_new - x[i];
            x[i] = x_new;
            max_change = std::max(max_change, std::fabs(delta));
            // Rank-1 update of cx = C x.
            for (size_t j = 0; j < n; j++) {
                cx[j] += covariance[j * n + i] * delta;
            }
        }
        if (max_change < 1e-12) {
            break;
        }
    }

    double total = std::accumulate(x.begin(), x.end(), 0.0);
    if (total <= 0.0) {
        return {};
    }
    for (double & v : x) {
        v /= total;
    }
    return x;
}

// ============================================================================
// Task 4.3 - Efficient Frontier
// ============================================================================

std::vector<FrontierPoint> efficient_frontier(const double * expected_returns,
                                              const double * covariance,
                                              size_t n,
                                              size_t num_points,
                                              bool allow_short) {
    std::vector<FrontierPoint> frontier;
    if (expected_returns == nullptr || covariance == nullptr || n == 0 || num_points == 0) {
        return frontier;
    }

    double mu_min = expected_returns[0];
    double mu_max = expected_returns[0];
    for (size_t i = 1; i < n; i++) {
        mu_min = std::min(mu_min, expected_returns[i]);
        mu_max = std::max(mu_max, expected_returns[i]);
    }

    frontier.reserve(num_points);
    for (size_t p = 0; p < num_points; p++) {
        double t = (num_points > 1) ? static_cast<double>(p) / static_cast<double>(num_points - 1)
                                    : 0.5;
        double target = mu_min + t * (mu_max - mu_min);
        std::vector<double> w = markowitz_optimize(expected_returns, covariance, n,
                                                   target, allow_short);
        if (w.empty()) {
            continue;
        }
        FrontierPoint point;
        point.target_return = target;
        point.expected_return = tensors::portfolio_expected_return(w.data(), expected_returns, n);
        point.risk = tensors::portfolio_volatility(w.data(), covariance, n);
        point.weights = std::move(w);
        frontier.push_back(std::move(point));
    }
    return frontier;
}

// ============================================================================
// Task 4.4 - Yield Curve
// ============================================================================

YieldCurve::YieldCurve(const std::vector<double> & tenors_in,
                       const std::vector<double> & rates_in) {
    size_t n_pts = std::min(tenors_in.size(), rates_in.size());
    for (size_t i = 0; i < n_pts; i++) {
        add_point(tenors_in[i], rates_in[i]);
    }
}

void YieldCurve::add_point(double tenor, double rate) {
    for (size_t i = 0; i < tenors.size(); i++) {
        if (tenors[i] == tenor) {
            rates[i] = rate;
            return;
        }
    }
    tenors.push_back(tenor);
    rates.push_back(rate);
    sort_points();
}

void YieldCurve::sort_points() {
    std::vector<size_t> order(tenors.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return tenors[a] < tenors[b]; });
    std::vector<double> t_sorted(tenors.size());
    std::vector<double> r_sorted(rates.size());
    for (size_t i = 0; i < order.size(); i++) {
        t_sorted[i] = tenors[order[i]];
        r_sorted[i] = rates[order[i]];
    }
    tenors = std::move(t_sorted);
    rates = std::move(r_sorted);
}

double YieldCurve::zero_rate(double t) const {
    if (tenors.empty()) {
        return 0.0;
    }
    if (tenors.size() == 1 || t <= tenors.front()) {
        return rates.front();
    }
    if (t >= tenors.back()) {
        return rates.back();
    }
    auto it = std::lower_bound(tenors.begin(), tenors.end(), t);
    size_t hi_idx = static_cast<size_t>(it - tenors.begin());
    size_t lo_idx = hi_idx - 1;
    double frac = (t - tenors[lo_idx]) / (tenors[hi_idx] - tenors[lo_idx]);
    return rates[lo_idx] + frac * (rates[hi_idx] - rates[lo_idx]);
}

double YieldCurve::discount_factor(double t) const {
    return std::exp(-zero_rate(t) * t);
}

// ============================================================================
// Task 4.4 - Bond Analytics
// ============================================================================

void bond_cashflows(const Bond & bond,
                    std::vector<double> & times_out,
                    std::vector<double> & amounts_out) {
    times_out.clear();
    amounts_out.clear();
    int freq = std::max(bond.frequency, 1);
    int periods = static_cast<int>(std::llround(bond.maturity_years * freq));
    if (periods < 1) {
        return;
    }
    double coupon = bond.face * bond.coupon_rate / freq;
    times_out.reserve(periods);
    amounts_out.reserve(periods);
    for (int k = 1; k <= periods; k++) {
        times_out.push_back(static_cast<double>(k) / freq);
        double amount = coupon;
        if (k == periods) {
            amount += bond.face;
        }
        amounts_out.push_back(amount);
    }
}

double bond_price(const Bond & bond, double ytm) {
    std::vector<double> times, amounts;
    bond_cashflows(bond, times, amounts);
    if (times.empty()) {
        return 0.0;
    }
    double freq = static_cast<double>(std::max(bond.frequency, 1));
    double base = 1.0 + ytm / freq;
    double price = 0.0;
    for (size_t k = 0; k < times.size(); k++) {
        price += amounts[k] / std::pow(base, times[k] * freq);
    }
    return price;
}

double bond_price(const Bond & bond, const YieldCurve & curve) {
    std::vector<double> times, amounts;
    bond_cashflows(bond, times, amounts);
    double price = 0.0;
    for (size_t k = 0; k < times.size(); k++) {
        price += amounts[k] * curve.discount_factor(times[k]);
    }
    return price;
}

double bond_yield_to_maturity(const Bond & bond,
                              double price,
                              double tolerance,
                              int max_iterations) {
    if (price <= 0.0) {
        return std::nan("");
    }

    double lo = -0.95;
    double hi = 10.0;
    double f_lo = bond_price(bond, lo) - price;
    if (f_lo <= 0.0) {
        return std::nan("");  // price above the maximum attainable value
    }

    // Newton-Raphson on modified duration with bisection fallback.
    double y = bond.coupon_rate > 0.0 ? bond.coupon_rate : 0.05;
    for (int i = 0; i < max_iterations; i++) {
        double p = bond_price(bond, y);
        double diff = p - price;
        if (std::fabs(diff) < tolerance) {
            return y;
        }
        if (diff > 0.0) {
            lo = y;
        } else {
            hi = y;
        }
        // dP/dy = -D_mod * P
        double d_mod = modified_duration(bond, y);
        double slope = -d_mod * p;
        double next = y;
        if (std::fabs(slope) > 1e-12) {
            next = y - diff / slope;
        }
        if (std::fabs(slope) <= 1e-12 || next <= lo || next >= hi) {
            next = 0.5 * (lo + hi);
        }
        y = next;
    }
    return std::nan("");
}

double macaulay_duration(const Bond & bond, double ytm) {
    std::vector<double> times, amounts;
    bond_cashflows(bond, times, amounts);
    if (times.empty()) {
        return 0.0;
    }
    double freq = static_cast<double>(std::max(bond.frequency, 1));
    double base = 1.0 + ytm / freq;
    double price = 0.0;
    double weighted = 0.0;
    for (size_t k = 0; k < times.size(); k++) {
        double pv = amounts[k] / std::pow(base, times[k] * freq);
        price += pv;
        weighted += times[k] * pv;
    }
    if (price <= 0.0) {
        return 0.0;
    }
    return weighted / price;
}

double modified_duration(const Bond & bond, double ytm) {
    double freq = static_cast<double>(std::max(bond.frequency, 1));
    return macaulay_duration(bond, ytm) / (1.0 + ytm / freq);
}

double convexity(const Bond & bond, double ytm) {
    std::vector<double> times, amounts;
    bond_cashflows(bond, times, amounts);
    if (times.empty()) {
        return 0.0;
    }
    double freq = static_cast<double>(std::max(bond.frequency, 1));
    double base = 1.0 + ytm / freq;
    double price = 0.0;
    double conv_sum = 0.0;
    for (size_t k = 0; k < times.size(); k++) {
        double t = times[k];
        double pv = amounts[k] / std::pow(base, t * freq);
        price += pv;
        conv_sum += pv * t * (t + 1.0 / freq);
    }
    if (price <= 0.0) {
        return 0.0;
    }
    return conv_sum / (price * base * base);
}

} // namespace quant
} // namespace ggnucash
