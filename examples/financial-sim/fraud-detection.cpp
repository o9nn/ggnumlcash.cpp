#include "fraud-detection.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace ggnucash {
namespace fraud {

// ============================================================================
// Isolation Forest
// ============================================================================

IsolationForest::IsolationForest(const IsolationForestConfig & config)
    : config_(config), fitted_(false) {
    if (config_.max_depth <= 0) {
        config_.max_depth = static_cast<int>(
            std::ceil(std::log2(std::max(2, config_.subsample_size))));
    }
}

IsolationForest::~IsolationForest() { clear(); }

void IsolationForest::clear() {
    for (Node * t : trees_) free_tree(t);
    trees_.clear();
    fitted_ = false;
}

void IsolationForest::free_tree(Node * node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    delete node;
}

double IsolationForest::expected_path_length(size_t n) {
    if (n <= 1) return 0.0;
    // c(n) = 2*H(n-1) - 2*(n-1)/n, H(i) = ln(i) + Euler-Mascheroni
    double h = std::log(static_cast<double>(n - 1)) + 0.5772156649015329;
    return 2.0 * h - (2.0 * static_cast<double>(n - 1) / static_cast<double>(n));
}

IsolationForest::Node * IsolationForest::build_tree(
    const std::vector<const FeatureVector *> & points, int depth,
    std::mt19937_64 & rng) {
    Node * node = new Node();
    node->size = points.size();

    size_t dim = points.empty() ? 0 : points[0]->dim();
    if (points.size() <= 1 || depth >= config_.max_depth || dim == 0) {
        node->is_leaf = true;
        return node;
    }

    // Pick a random feature with a non-degenerate range.
    std::uniform_int_distribution<size_t> feat_dist(0, dim - 1);
    int feature = -1;
    double lo = 0.0, hi = 0.0;
    for (int attempt = 0; attempt < static_cast<int>(dim); attempt++) {
        size_t f = feat_dist(rng);
        lo = hi = points[0]->values[f];
        for (const auto * p : points) {
            lo = std::min(lo, p->values[f]);
            hi = std::max(hi, p->values[f]);
        }
        if (hi > lo) {
            feature = static_cast<int>(f);
            break;
        }
    }

    if (feature < 0) {
        node->is_leaf = true;  // all features constant
        return node;
    }

    std::uniform_real_distribution<double> split_dist(lo, hi);
    double split = split_dist(rng);

    node->is_leaf = false;
    node->feature = feature;
    node->split_value = split;

    std::vector<const FeatureVector *> left_pts, right_pts;
    for (const auto * p : points) {
        if (p->values[feature] < split) left_pts.push_back(p);
        else                           right_pts.push_back(p);
    }

    node->left = build_tree(left_pts, depth + 1, rng);
    node->right = build_tree(right_pts, depth + 1, rng);
    return node;
}

void IsolationForest::fit(const std::vector<FeatureVector> & data) {
    clear();
    if (data.empty()) return;

    std::mt19937_64 rng(config_.seed != 0 ? config_.seed : std::random_device{}());

    int sample_size = std::min(config_.subsample_size,
                               static_cast<int>(data.size()));
    // Recompute max depth for the actual subsample size.
    if (config_.max_depth <= 0) {
        config_.max_depth = static_cast<int>(std::ceil(std::log2(sample_size)));
    }

    std::vector<const FeatureVector *> all;
    all.reserve(data.size());
    for (const auto & d : data) all.push_back(&d);

    for (int t = 0; t < config_.num_trees; t++) {
        std::vector<const FeatureVector *> sample;
        sample.reserve(sample_size);
        std::uniform_int_distribution<size_t> pick(0, all.size() - 1);
        for (int i = 0; i < sample_size; i++) {
            sample.push_back(all[pick(rng)]);
        }
        trees_.push_back(build_tree(sample, 0, rng));
    }

    fitted_ = true;
}

double IsolationForest::path_length(const Node * node, const FeatureVector & point,
                                    int depth) const {
    if (node->is_leaf) {
        return static_cast<double>(depth) + expected_path_length(node->size);
    }
    if (point.values[node->feature] < node->split_value) {
        return path_length(node->left, point, depth + 1);
    }
    return path_length(node->right, point, depth + 1);
}

double IsolationForest::anomaly_score(const FeatureVector & point) const {
    if (!fitted_ || trees_.empty()) return 0.5;

    double total_path = 0.0;
    for (const Node * t : trees_) {
        total_path += path_length(t, point, 0);
    }
    double avg_path = total_path / static_cast<double>(trees_.size());

    double c = expected_path_length(static_cast<size_t>(config_.subsample_size));
    if (c <= 0.0) return 0.5;

    // s = 2^(-E(h) / c)
    return std::pow(2.0, -avg_path / c);
}

std::vector<double> IsolationForest::anomaly_scores(
    const std::vector<FeatureVector> & points) const {
    std::vector<double> out;
    out.reserve(points.size());
    for (const auto & p : points) out.push_back(anomaly_score(p));
    return out;
}

// ============================================================================
// Benford's Law
// ============================================================================

std::string BenfordResult::conformity_to_string(Conformity c) {
    switch (c) {
        case Conformity::CLOSE:          return "CLOSE";
        case Conformity::ACCEPTABLE:     return "ACCEPTABLE";
        case Conformity::MARGINAL:       return "MARGINAL";
        case Conformity::NONCONFORMITY:  return "NONCONFORMITY";
        default:                         return "UNKNOWN";
    }
}

BenfordResult benford_analysis(const std::vector<double> & amounts) {
    BenfordResult res;

    // Expected Benford first-digit probabilities: P(d) = log10(1 + 1/d).
    for (int d = 1; d <= 9; d++) {
        res.expected[d] = std::log10(1.0 + 1.0 / static_cast<double>(d));
    }

    // Count observed first digits.
    std::vector<uint64_t> counts(10, 0);
    for (double amt : amounts) {
        double a = std::fabs(amt);
        if (a < 1e-12) continue;  // skip zeros
        // Extract leading digit by scaling into [1,10).
        while (a >= 10.0) a /= 10.0;
        while (a < 1.0)  a *= 10.0;
        int digit = static_cast<int>(a);
        if (digit >= 1 && digit <= 9) {
            counts[digit]++;
            res.sample_count++;
        }
    }

    if (res.sample_count == 0) return res;

    double chi2 = 0.0;
    double mad = 0.0;
    for (int d = 1; d <= 9; d++) {
        res.observed[d] =
            static_cast<double>(counts[d]) / static_cast<double>(res.sample_count);
        double expected_count = res.expected[d] * res.sample_count;
        if (expected_count > 0.0) {
            double diff = counts[d] - expected_count;
            chi2 += (diff * diff) / expected_count;
        }
        mad += std::fabs(res.observed[d] - res.expected[d]);
    }
    res.chi_square = chi2;
    res.mean_absolute_deviation = mad / 9.0;

    // Nigrini MAD conformity thresholds (first-digit).
    double m = res.mean_absolute_deviation;
    if (m <= 0.006)      res.conformity = BenfordResult::Conformity::CLOSE;
    else if (m <= 0.012) res.conformity = BenfordResult::Conformity::ACCEPTABLE;
    else if (m <= 0.015) res.conformity = BenfordResult::Conformity::MARGINAL;
    else                 res.conformity = BenfordResult::Conformity::NONCONFORMITY;

    return res;
}

// ============================================================================
// Autoencoder-style Reconstruction Scorer
// ============================================================================

AutoencoderScorer::AutoencoderScorer() : fitted_(false) {}

void AutoencoderScorer::fit(const std::vector<FeatureVector> & normal_data) {
    fitted_ = false;
    if (normal_data.empty()) return;

    size_t dim = normal_data[0].dim();
    if (dim == 0) return;

    mean_.assign(dim, 0.0);
    std_.assign(dim, 0.0);

    for (const auto & d : normal_data) {
        for (size_t i = 0; i < dim; i++) mean_[i] += d.values[i];
    }
    double n = static_cast<double>(normal_data.size());
    for (size_t i = 0; i < dim; i++) mean_[i] /= n;

    for (const auto & d : normal_data) {
        for (size_t i = 0; i < dim; i++) {
            double diff = d.values[i] - mean_[i];
            std_[i] += diff * diff;
        }
    }
    for (size_t i = 0; i < dim; i++) {
        std_[i] = std::sqrt(std_[i] / n);
        if (std_[i] < 1e-9) std_[i] = 1e-9;  // avoid division by zero
    }

    fitted_ = true;
}

double AutoencoderScorer::reconstruction_error(const FeatureVector & point) const {
    if (!fitted_) return 0.0;
    // Standardized squared distance from the learned baseline (diagonal
    // Mahalanobis). Higher = more anomalous.
    double acc = 0.0;
    size_t dim = std::min(mean_.size(), point.dim());
    for (size_t i = 0; i < dim; i++) {
        double z = (point.values[i] - mean_[i]) / std_[i];
        acc += z * z;
    }
    return dim > 0 ? acc / static_cast<double>(dim) : 0.0;
}

// ============================================================================
// SPC Monitor (Welford online mean/variance)
// ============================================================================

SpcMonitor::SpcMonitor(double z_threshold, size_t min_samples)
    : z_threshold_(z_threshold), min_samples_(min_samples),
      count_(0), mean_(0.0), m2_(0.0) {}

void SpcMonitor::reset() {
    count_ = 0;
    mean_ = 0.0;
    m2_ = 0.0;
}

bool SpcMonitor::observe(double value) {
    // Compute signal against the running stats BEFORE incorporating the point.
    bool signal = false;
    if (count_ >= min_samples_) {
        double sd = stddev();
        if (sd > 1e-12) {
            double z = (value - mean_) / sd;
            signal = std::fabs(z) > z_threshold_;
        }
    }

    // Welford update.
    count_++;
    double delta = value - mean_;
    mean_ += delta / static_cast<double>(count_);
    double delta2 = value - mean_;
    m2_ += delta * delta2;

    return signal;
}

double SpcMonitor::mean() const { return mean_; }

double SpcMonitor::stddev() const {
    if (count_ < 2) return 0.0;
    return std::sqrt(m2_ / static_cast<double>(count_ - 1));
}

// ============================================================================
// Composite Risk Scorer
// ============================================================================

RiskScorer::RiskScorer(const RiskScoreConfig & config)
    : config_(config), forest_(nullptr), autoencoder_(nullptr),
      spc_(3.0, 30) {}

void RiskScorer::set_isolation_forest(const IsolationForest * forest) {
    forest_ = forest;
}

void RiskScorer::set_autoencoder(const AutoencoderScorer * autoencoder) {
    autoencoder_ = autoencoder;
}

void RiskScorer::set_spc_threshold(double z) {
    spc_ = SpcMonitor(z, 30);
}

double RiskScorer::squash(double x) {
    // Map [0, inf) reconstruction error to [0, 1) via 1 - exp(-x).
    return 1.0 - std::exp(-x);
}

RiskScore RiskScorer::score(const FeatureVector & point, double scalar_value) {
    RiskScore rs;

    double weighted = 0.0;
    double weight_total = 0.0;

    if (forest_ && forest_->is_fitted()) {
        rs.isolation_score = forest_->anomaly_score(point);
        weighted += config_.isolation_forest_weight * rs.isolation_score;
        weight_total += config_.isolation_forest_weight;
    }

    if (autoencoder_ && autoencoder_->is_fitted()) {
        rs.reconstruction_score =
            squash(autoencoder_->reconstruction_error(point));
        weighted += config_.autoencoder_weight * rs.reconstruction_score;
        weight_total += config_.autoencoder_weight;
    }

    bool spc_signal = spc_.observe(scalar_value);
    rs.spc_signal = spc_signal ? 1.0 : 0.0;
    weighted += config_.spc_weight * rs.spc_signal;
    weight_total += config_.spc_weight;

    rs.composite = weight_total > 0.0 ? weighted / weight_total : 0.0;
    rs.alert = rs.composite >= config_.alert_threshold;
    return rs;
}

} // namespace fraud
} // namespace ggnucash
