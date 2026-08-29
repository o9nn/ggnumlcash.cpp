#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "financial-tensors.h"

// ============================================================================
// Quantitative Finance Models - Phase 2, Issue #004
//
// Higher-level quantitative finance models built on top of the tensor kernels
// in financial-tensors.h (namespace ggnucash::tensors). This module adds:
//
//   Task 4.1  Options pricing extras
//     - Trinomial tree pricing (European + American)
//     - Monte Carlo European option pricing with antithetic variates
//     - Implied volatility (bisection + Newton inversion of Black-Scholes)
//     - VolatilitySurface with bilinear interpolation / flat extrapolation
//
//   Task 4.2  Risk management engine
//     - Historical VaR / Expected Shortfall
//     - Parametric (variance-covariance / delta-normal) VaR / ES
//     - Monte Carlo VaR (wraps tensors::monte_carlo_risk)
//     - StressScenario library + portfolio shock P&L
//     - RiskMonitor with VaR / drawdown limit breach callbacks
//
//   Task 4.3  Portfolio optimization
//     - Markowitz minimum-variance portfolio for a target return
//       (closed-form KKT solution; optional no-short-selling active set)
//     - Global minimum variance portfolio
//     - Black-Litterman posterior expected returns
//     - Risk parity (equal risk contribution) weights
//     - Efficient frontier sweep
//
//   Task 4.4  Fixed income analytics
//     - YieldCurve with linear zero-rate interpolation
//     - Bond pricing from yield or from a YieldCurve
//     - Yield-to-maturity solver
//     - Macaulay / modified duration and convexity
//
// All entry points are dependency-free CPU reference implementations that map
// cleanly onto GGML tensor operations for future hardware acceleration.
// ============================================================================

namespace ggnucash {
namespace quant {

using tensors::OptionType;

// ============================================================================
// Task 4.1 - Options Pricing Extras
// ============================================================================

// Trinomial tree option pricing. Supports European and American exercise.
// Uses the standard three-branch (up / mid / down) lattice with
//   u = exp(sigma * sqrt(2 dt)), d = 1/u, m = 1
// and moment-matched risk-neutral probabilities. Converges to Black-Scholes
// for European options as steps increase.
//
//   spot            current price of the underlying
//   strike          option strike price
//   time_to_expiry  time to expiration in years
//   volatility      annualized volatility (sigma)
//   risk_free_rate  continuously-compounded annual risk-free rate
//   type            CALL or PUT
//   steps           number of tree steps (>= 1)
//   american        true => American exercise, false => European
double trinomial_option_price(double spot,
                              double strike,
                              double time_to_expiry,
                              double volatility,
                              double risk_free_rate,
                              OptionType type,
                              int steps,
                              bool american);

// Result of a Monte Carlo option pricing run.
struct MonteCarloOptionResult {
    double   price;          // discounted mean payoff
    double   std_error;      // standard error of the price estimate
    uint64_t num_paths;      // number of GBM paths simulated (antithetic pairs count as 2)

    MonteCarloOptionResult() : price(0.0), std_error(0.0), num_paths(0) {}
};

// Monte Carlo European option pricing under geometric Brownian motion with
// antithetic variates (each draw z also contributes -z, which halves the
// number of RNG draws and reduces variance for the symmetric GBM payoff).
//
//   seed   RNG seed for reproducibility (0 => nondeterministic)
MonteCarloOptionResult monte_carlo_option_price(double spot,
                                                double strike,
                                                double time_to_expiry,
                                                double volatility,
                                                double risk_free_rate,
                                                OptionType type,
                                                uint64_t num_paths,
                                                uint64_t seed = 0);

// Implied volatility: invert Black-Scholes to recover the volatility implied
// by an observed market price. Uses Newton-Raphson on vega with a bisection
// fallback bracketed on [1e-6, 5.0]. Returns NaN when the price is outside
// the no-arbitrage bounds or no root is found within max_iterations.
//
//   tolerance       absolute price tolerance for convergence
//   max_iterations  iteration cap for both Newton and bisection phases
double implied_volatility(double market_price,
                          double spot,
                          double strike,
                          double time_to_expiry,
                          double risk_free_rate,
                          OptionType type,
                          double tolerance = 1e-8,
                          int max_iterations = 100);

// Implied volatility surface over a strike x tenor grid. Volatilities are
// stored row-major as tenors x strikes (vols[tenor_i * num_strikes + strike_j]).
// Interpolation is bilinear inside the grid with flat (nearest-edge)
// extrapolation outside it.
struct VolatilitySurface {
    std::vector<double> strikes;   // ascending strike grid
    std::vector<double> tenors;    // ascending tenor grid (years)
    std::vector<double> vols;      // row-major tenors x strikes

    VolatilitySurface() = default;
    VolatilitySurface(const std::vector<double> & strikes_in,
                      const std::vector<double> & tenors_in,
                      const std::vector<double> & vols_in);

    // True when grid dimensions are consistent and both axes are non-empty
    // and strictly ascending.
    bool is_valid() const;

    // Bilinear interpolated volatility at (strike, tenor) with flat
    // extrapolation outside the grid. Returns NaN if the surface is invalid.
    double volatility(double strike, double tenor) const;
};

// ============================================================================
// Task 4.2 - Risk Management Engine
// ============================================================================

// Historical VaR / Expected Shortfall from an empirical return series.
//   returns      array of n periodic returns (e.g. -0.02 for a 2% loss)
//   confidence   e.g. 0.95 or 0.99
// Returns the VaR / ES as a positive loss fraction (e.g. 0.05 = 5% loss).
// Historical VaR is the negative of the (1 - confidence) empirical quantile;
// historical ES is the mean loss of the tail beyond the VaR threshold.
double historical_var(const double * returns, size_t n, double confidence);
double historical_es(const double * returns, size_t n, double confidence);

// Parametric (variance-covariance / delta-normal) VaR / ES. Assumes portfolio
// returns are normally distributed with the given per-period mean and stddev.
//   holding_value   current market value of the position / portfolio
// Returns VaR / ES as a positive currency loss:
//   VaR = value * (z_alpha * sigma - mean),  z_alpha = normal_inv_cdf(confidence)
//   ES  = value * (sigma * pdf(z_alpha) / (1 - confidence) - mean)
double parametric_var(double mean, double stddev, double confidence, double holding_value);
double parametric_es(double mean, double stddev, double confidence, double holding_value);

// Monte Carlo VaR / ES under GBM. Thin wrapper over
// ggnucash::tensors::monte_carlo_risk for API symmetry with the other
// estimators. Returns the full MonteCarloResult (VaR, ES, moments, extremes).
tensors::MonteCarloResult monte_carlo_var(double initial_value,
                                          double drift,
                                          double volatility,
                                          double time_horizon,
                                          uint64_t num_paths,
                                          double confidence_level,
                                          uint64_t seed = 0);

// A named stress scenario: a set of per-asset percentage shocks (e.g. -0.20
// for a 20% drop) applied to matching portfolio positions.
struct StressScenario {
    std::string name;
    // (asset_index, fractional_shock) pairs; e.g. {0, -0.20} => asset 0 -20%.
    std::vector<std::pair<size_t, double>> asset_shocks;

    StressScenario() = default;
    StressScenario(const std::string & name_in,
                   const std::vector<std::pair<size_t, double>> & shocks_in)
        : name(name_in), asset_shocks(shocks_in) {}
};

// Apply a stress scenario to a portfolio and return the P&L impact.
//   position_values   market value per asset (n elements)
// P&L = sum over shocked assets of position_values[i] * shock. Shock indices
// outside [0, n) are ignored.
double apply_stress_scenario(const double * position_values,
                             size_t n,
                             const StressScenario & scenario);

// Small library of named historical-style scenarios applied uniformly to all
// n positions (per-asset overrides can be layered on afterwards if needed):
//   "Equity Crash 2008"      equity -20% across the book
//   "Black Monday 1987"      equity -22.6% across the book
//   "Rates +200bp Parallel"  fixed-income-style -2% per unit of position
//                            scaled by `rate_sensitivity` (duration proxy)
//   "Vol Spike"              -10% across the book
// The rate scenario multiplies each position by -0.02 * rate_sensitivity.
std::vector<StressScenario> standard_stress_scenarios(size_t n, double rate_sensitivity = 1.0);

// Notification payload delivered when a RiskMonitor limit is breached.
struct RiskBreach {
    enum class Type {
        VAR_LIMIT,
        DRAWDOWN_LIMIT
    };

    Type        type;
    double      limit;      // configured limit that was breached
    double      observed;   // observed metric (VaR or drawdown fraction)
    std::string message;

    RiskBreach() : type(Type::VAR_LIMIT), limit(0.0), observed(0.0) {}
};

// Monitors a portfolio and raises a callback when a configured VaR or
// drawdown limit is breached. VaR is recomputed on demand with the parametric
// (delta-normal) estimator from the portfolio's return moments; drawdown is
// tracked against the peak portfolio value seen via update_prices().
class RiskMonitor {
public:
    RiskMonitor();

    // Define the monitored portfolio.
    //   position_values    market value per asset
    //   expected_returns   per-period expected return per asset
    //   covariance         row-major n x n per-period return covariance
    void set_portfolio(const std::vector<double> & position_values,
                       const std::vector<double> & expected_returns,
                       const std::vector<double> & covariance);

    // Configure limits. confidence selects the VaR tail (e.g. 0.95);
    // var_limit is an absolute currency loss limit; drawdown_limit is a
    // fractional peak-to-trough decline (e.g. 0.10 for 10%). A limit of
    // <= 0 disables that check.
    void set_var_limit(double confidence, double var_limit);
    void set_drawdown_limit(double drawdown_limit);

    // Register a callback invoked once per check_limits() call for each
    // breached limit.
    void set_breach_callback(std::function<void(const RiskBreach &)> callback);

    // Current portfolio market value (sum of position values).
    double portfolio_value() const;

    // Parametric VaR of the current portfolio at the configured confidence
    // (0.0 if no portfolio / no confidence configured).
    double current_var() const;

    // Current peak-to-trough drawdown fraction based on update_prices()
    // history (0.0 if never updated or value only rose).
    double current_drawdown() const;

    // Replace position market values (e.g. after a market move). Updates the
    // peak-value history used for drawdown tracking.
    void update_prices(const std::vector<double> & new_position_values);

    // Recompute metrics and fire the breach callback for each breached limit.
    // Returns true when at least one limit is breached.
    bool check_limits();

    bool var_limit_breached() const { return var_breached; }
    bool drawdown_limit_breached() const { return drawdown_breached; }

private:
    std::vector<double> values;
    std::vector<double> expected_returns;
    std::vector<double> covariance;

    double   confidence       = 0.0;   // 0 => VaR check disabled
    double   var_limit        = 0.0;   // <= 0 => disabled
    double   drawdown_limit   = 0.0;   // <= 0 => disabled
    double   peak_value       = 0.0;
    bool     has_peak         = false;
    bool     var_breached      = false;
    bool     drawdown_breached = false;

    std::function<void(const RiskBreach &)> callback;
};

// ============================================================================
// Task 4.3 - Portfolio Optimization
// ============================================================================

// Minimum-variance portfolio for a target expected return (Markowitz).
// Solves the KKT system
//   min 1/2 w' C w   s.t.  1'w = 1,  mu'w = target_return
// in closed form via Gaussian elimination with partial pivoting (no BLAS
// dependency). When allow_short is false, negative weights are handled with
// an active-set iteration: offending assets are clamped to zero and the
// problem re-solved on the remaining free assets until all weights are
// non-negative (or a single asset remains). Returns the weight vector
// (n elements); weights always sum to 1. Returns an empty vector when the
// covariance matrix is singular and the system cannot be solved.
std::vector<double> markowitz_optimize(const double * expected_returns,
                                       const double * covariance,
                                       size_t n,
                                       double target_return,
                                       bool allow_short = true);

// Global minimum variance portfolio: w = C^{-1} 1 / (1' C^{-1} 1).
// Returns an empty vector when the covariance matrix is singular.
std::vector<double> global_minimum_variance_portfolio(const double * covariance, size_t n);

// Black-Litterman posterior expected returns.
//   covariance      row-major n x n return covariance
//   market_weights  equilibrium (market-cap) weights, n elements
//   risk_aversion   delta; implied equilibrium returns pi = delta * C * w
//   tau             uncertainty scaling on the prior covariance
//   P               row-major k x n view matrix (which assets each view touches)
//   Q               k-element view returns
//   omega           row-major k x k view uncertainty covariance
//   k               number of views (0 => posterior equals the prior pi)
// Returns the n-element posterior expected return vector
//   mu_BL = pi + (tau C P') (P tau C P' + Omega)^{-1} (Q - P pi)
// or an empty vector when a required matrix is singular.
std::vector<double> black_litterman(const double * covariance,
                                    size_t n,
                                    const double * market_weights,
                                    double risk_aversion,
                                    double tau,
                                    const double * P,
                                    const double * Q,
                                    const double * omega,
                                    size_t k);

// Convenience overload that derives omega = tau * diag(P C P'), the standard
// "uncertainty proportional to prior variance" choice.
std::vector<double> black_litterman(const double * covariance,
                                    size_t n,
                                    const double * market_weights,
                                    double risk_aversion,
                                    double tau,
                                    const double * P,
                                    const double * Q,
                                    size_t k);

// Risk parity (equal risk contribution) portfolio weights. Minimizes the
// Spinu objective f(x) = 1/2 x'Cx - sum_i ln(x_i) by cyclical coordinate
// descent, then normalizes w = x / sum(x). At the optimum each asset
// contributes equally to total portfolio volatility. `iterations` caps the
// number of full coordinate sweeps. Returns an empty vector on failure
// (e.g. non-positive covariance diagonal).
std::vector<double> risk_parity(const double * covariance, size_t n, int iterations = 100);

// One point on the efficient frontier.
struct FrontierPoint {
    double              target_return;    // requested target return
    double              expected_return;  // achieved portfolio return (mu'w)
    double              risk;             // portfolio volatility (sqrt(w'Cw))
    std::vector<double> weights;          // optimal weights at this point

    FrontierPoint() : target_return(0.0), expected_return(0.0), risk(0.0) {}
};

// Sweep the efficient frontier: `num_points` minimum-variance portfolios with
// target returns spaced linearly from the GMV portfolio's return up to the
// maximum single-asset expected return. Points whose optimization fails are
// skipped, so the result may contain fewer than num_points entries.
std::vector<FrontierPoint> efficient_frontier(const double * expected_returns,
                                              const double * covariance,
                                              size_t n,
                                              size_t num_points,
                                              bool allow_short = true);

// ============================================================================
// Task 4.4 - Fixed Income Analytics
// ============================================================================

// Zero-coupon yield curve built from (tenor, continuously-compounded zero
// rate) points. zero_rate(t) is linearly interpolated between nodes with flat
// extrapolation at both ends; discount_factor(t) = exp(-zero_rate(t) * t).
class YieldCurve {
public:
    YieldCurve() = default;
    YieldCurve(const std::vector<double> & tenors, const std::vector<double> & rates);

    // Add a (tenor, zero rate) node. Duplicate tenors overwrite the rate.
    void add_point(double tenor, double zero_rate);

    size_t size() const { return tenors.size(); }
    bool   empty() const { return tenors.empty(); }

    // Interpolated continuously-compounded zero rate at time t (years).
    // Flat extrapolation outside the node range. Returns 0 for an empty curve.
    double zero_rate(double t) const;

    // Discount factor for a cashflow at time t: exp(-zero_rate(t) * t).
    double discount_factor(double t) const;

    const std::vector<double> & get_tenors() const { return tenors; }
    const std::vector<double> & get_rates() const { return rates; }

private:
    std::vector<double> tenors;   // ascending years
    std::vector<double> rates;    // matching continuously-compounded zero rates

    void sort_points();
};

// Plain fixed-coupon bond. Cashflows occur at whole periods k / frequency for
// k = 1 .. round(maturity_years * frequency); each period pays
// face * coupon_rate / frequency, plus face at maturity.
struct Bond {
    double face;            // par / notional, e.g. 100.0
    double coupon_rate;     // annual coupon rate, e.g. 0.05 for 5%
    int    frequency;       // coupon payments per year (1, 2, 4, ...)
    double maturity_years;  // time to maturity in years

    Bond()
        : face(100.0), coupon_rate(0.0), frequency(2), maturity_years(1.0) {}
    Bond(double face_in, double coupon_rate_in, int frequency_in, double maturity_in)
        : face(face_in), coupon_rate(coupon_rate_in),
          frequency(frequency_in), maturity_years(maturity_in) {}
};

// Cashflow times and amounts for a bond (times in years, amounts in currency).
// Useful for custom analytics; both vectors have the same length.
void bond_cashflows(const Bond & bond,
                    std::vector<double> & times_out,
                    std::vector<double> & amounts_out);

// Price a bond from a yield-to-maturity using periodic (bond-equivalent)
// compounding at the bond's coupon frequency:
//   P = sum_k c_k / (1 + y/f)^{k} + face / (1 + y/f)^{N}
double bond_price(const Bond & bond, double ytm);

// Price a bond by discounting each cashflow off a YieldCurve:
//   P = sum_k cf_k * curve.discount_factor(t_k)
double bond_price(const Bond & bond, const YieldCurve & curve);

// Solve the yield-to-maturity implied by an observed price. Newton-Raphson
// with a bisection fallback bracketed on (-0.95, 10.0). Returns NaN when no
// root is found within tolerance / max_iterations.
double bond_yield_to_maturity(const Bond & bond,
                              double price,
                              double tolerance = 1e-10,
                              int max_iterations = 100);

// Macaulay duration (in years) at the given yield:
//   D_mac = (1/P) sum_k t_k cf_k (1 + y/f)^{-f t_k}
double macaulay_duration(const Bond & bond, double ytm);

// Modified duration: D_mod = D_mac / (1 + y/f).
double modified_duration(const Bond & bond, double ytm);

// Bond convexity at the given yield:
//   C = (1/P) sum_k cf_k (1 + y/f)^{-f t_k} t_k (t_k + 1/f) / (1 + y/f)^2
double convexity(const Bond & bond, double ytm);

} // namespace quant
} // namespace ggnucash
