#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

// ============================================================================
// Fraud Detection & Anomaly Detection - Issue #005 (Task 5.3) & Phase C.1
//
// Hardware-acceleration-ready fraud and anomaly detection. This module
// provides the statistical/ML inference layer that scores financial
// transactions for anomalous behavior. It builds on the financial tensor
// primitives and is designed so the heavy inner loops (tree traversal,
// network forward passes) can later be dispatched to a GGML backend.
//
// Implemented detectors:
//   - Isolation Forest (multivariate point anomalies)
//   - Benford's Law first-digit analysis (ledger fabrication detection)
//   - Autoencoder-style reconstruction-error scorer (behavioral baseline)
//   - Statistical Process Control (z-score / control-chart) monitor
//   - A composite RiskScorer that fuses detector outputs into a single
//     configurable risk score with alert thresholds
//
// The module is standalone and dependency-free; "ML" here means classical,
// deterministic, testable algorithms rather than neural training, which is
// the appropriate foundation for a compliance-grade audit platform.
// ============================================================================

namespace ggnucash {
namespace fraud {

// ============================================================================
// Feature Vector
// ============================================================================

// A single observation to be scored. Features are a flat numeric vector; the
// meaning of each dimension is defined by the caller (e.g. amount, hour of
// day, days-since-last-tx, counterparty count).
struct FeatureVector {
    std::vector<double> values;
    std::string        id;          // optional identifier (e.g. transaction id)

    FeatureVector() {}
    explicit FeatureVector(std::vector<double> v, std::string i = "")
        : values(std::move(v)), id(std::move(i)) {}

    size_t dim() const { return values.size(); }
};

// ============================================================================
// Isolation Forest (C.1 / 5.3)
// ============================================================================

struct IsolationForestConfig {
    int num_trees;          // number of isolation trees
    int subsample_size;     // samples per tree (commonly 256)
    int max_depth;          // <= 0 => ceil(log2(subsample_size))
    uint64_t seed;          // RNG seed for reproducibility

    IsolationForestConfig()
        : num_trees(100), subsample_size(256), max_depth(0), seed(0) {}
};

class IsolationForest {
public:
    explicit IsolationForest(const IsolationForestConfig & config = IsolationForestConfig());
    ~IsolationForest();

    // Non-copyable (owns tree memory).
    IsolationForest(const IsolationForest &) = delete;
    IsolationForest & operator=(const IsolationForest &) = delete;

    // Fit the forest on a set of training observations.
    void fit(const std::vector<FeatureVector> & data);

    // Anomaly score in (0, 1]; higher = more anomalous. ~0.5 = normal.
    double anomaly_score(const FeatureVector & point) const;

    // Convenience: score many points.
    std::vector<double> anomaly_scores(const std::vector<FeatureVector> & points) const;

    bool is_fitted() const { return fitted_; }
    int  num_trees() const { return config_.num_trees; }

private:
    struct Node {
        bool     is_leaf;
        size_t   size;          // samples that reached this node (leaves only)
        int      feature;       // split feature index (internal)
        double   split_value;   // split threshold (internal)
        Node *   left;
        Node *   right;
        Node()
            : is_leaf(true), size(0), feature(-1), split_value(0.0),
              left(nullptr), right(nullptr) {}
    };

    IsolationForestConfig config_;
    std::vector<Node *> trees_;
    bool fitted_;

    Node * build_tree(const std::vector<const FeatureVector *> & points,
                      int depth, std::mt19937_64 & rng);
    double path_length(const Node * node, const FeatureVector & point,
                       int depth) const;
    static double expected_path_length(size_t n);  // c(n) normalization
    void free_tree(Node * node);
    void clear();
};

// ============================================================================
// Benford's Law Analysis (C.1)
// ============================================================================

struct BenfordResult {
    // Observed vs expected first-digit distribution (index 1..9).
    std::vector<double> observed;   // size 10; index 0 unused
    std::vector<double> expected;   // size 10; index 0 unused
    double chi_square;              // Chi-square statistic
    double mean_absolute_deviation; // MAD conformity measure
    uint64_t sample_count;
    // Conformity classification per Nigrini MAD thresholds.
    enum class Conformity { CLOSE, ACCEPTABLE, MARGINAL, NONCONFORMITY };
    Conformity conformity;

    BenfordResult()
        : observed(10, 0.0), expected(10, 0.0), chi_square(0.0),
          mean_absolute_deviation(0.0), sample_count(0),
          conformity(Conformity::NONCONFORMITY) {}

    static std::string conformity_to_string(Conformity c);
};

// Analyze the leading-digit distribution of a set of positive amounts
// against Benford's Law.
BenfordResult benford_analysis(const std::vector<double> & amounts);

// ============================================================================
// Autoencoder-style Reconstruction Scorer (C.1)
// ============================================================================

// A linear autoencoder using PCA-style reconstruction: the model learns the
// mean and per-feature standard deviation of a "normal" training set and
// scores new points by their standardized reconstruction error (Mahalanobis-
// like distance in the diagonal case). This is a deterministic, fast,
// explainable baseline anomaly model.
class AutoencoderScorer {
public:
    AutoencoderScorer();

    // Fit on normal (non-anomalous) reference data.
    void fit(const std::vector<FeatureVector> & normal_data);

    // Reconstruction error; higher = more anomalous relative to baseline.
    double reconstruction_error(const FeatureVector & point) const;

    bool is_fitted() const { return fitted_; }
    size_t feature_dim() const { return mean_.size(); }

private:
    std::vector<double> mean_;
    std::vector<double> std_;   // per-feature stddev (clamped > 0)
    bool fitted_;
};

// ============================================================================
// Statistical Process Control Monitor (C.1)
// ============================================================================

// Online z-score / control-chart monitor for a single scalar series (e.g.
// transaction amounts over time). Flags points beyond `z_threshold` standard
// deviations from the running mean.
class SpcMonitor {
public:
    explicit SpcMonitor(double z_threshold = 3.0, size_t min_samples = 30);

    // Observe a new value. Returns true if it is an out-of-control signal.
    bool observe(double value);

    double mean() const;
    double stddev() const;
    size_t count() const { return count_; }
    void reset();

private:
    double z_threshold_;
    size_t min_samples_;
    size_t count_;
    double mean_;   // running mean (Welford)
    double m2_;     // running sum of squared deviations (Welford)
};

// ============================================================================
// Composite Risk Scorer (C.1)
// ============================================================================

struct RiskScoreConfig {
    double isolation_forest_weight;
    double autoencoder_weight;
    double spc_weight;
    double alert_threshold;     // composite score >= this => alert

    RiskScoreConfig()
        : isolation_forest_weight(0.5),
          autoencoder_weight(0.3),
          spc_weight(0.2),
          alert_threshold(0.6) {}
};

struct RiskScore {
    double composite;           // fused score in [0, 1]
    double isolation_score;
    double reconstruction_score;
    double spc_signal;          // 0.0 or 1.0
    bool   alert;

    RiskScore()
        : composite(0.0), isolation_score(0.0), reconstruction_score(0.0),
          spc_signal(0.0), alert(false) {}
};

// Fuses the individual detectors into a single normalized risk score.
class RiskScorer {
public:
    explicit RiskScorer(const RiskScoreConfig & config = RiskScoreConfig());

    // Provide fitted detectors. The scorer does not take ownership.
    void set_isolation_forest(const IsolationForest * forest);
    void set_autoencoder(const AutoencoderScorer * autoencoder);
    void set_spc_threshold(double z);

    // Score a single observation. `scalar_value` is the raw monitored value
    // (e.g. amount) used by the SPC component.
    RiskScore score(const FeatureVector & point, double scalar_value);

    const RiskScoreConfig & config() const { return config_; }

private:
    RiskScoreConfig config_;
    const IsolationForest * forest_;
    const AutoencoderScorer * autoencoder_;
    SpcMonitor spc_;

    // Normalize an unbounded reconstruction error into [0,1].
    static double squash(double x);
};

} // namespace fraud
} // namespace ggnucash
