#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Transaction Flow Analytics & Entity Resolution - Phase B (B.1 + B.2)
//
// Influent-style transaction flow analytics for following money trails,
// combined with entity resolution and community clustering for investigation.
// This module provides the analytical data plane used by the visualization
// and case-management layers:
//
//   B.1  Flow Analytics:
//        - Entity/transaction data model (aligned with the Influent SPI)
//        - Flow graph construction from a transaction set
//        - Left-to-right flow ranking (sources on the left, sinks on right)
//        - Temporal window filtering for time-bounded analysis
//        - Branch-and-converge (fan-out / fan-in) structure detection
//
//   B.2  Entity Resolution & Clustering:
//        - String similarity (Levenshtein, Jaro, Jaro-Winkler)
//        - Address normalization
//        - Multi-signal entity matching with a configurable threshold
//        - Union-Find based entity resolution into canonical clusters
//        - Louvain-style modularity community detection on the flow graph
//
// The module is standalone and dependency-free so it can run in the audit
// pipeline without a GUI. Rendering (grafer/WebGL) is a separate concern.
// ============================================================================

namespace ggnucash {
namespace flow {

// ============================================================================
// Core Data Model (aligned with Influent SPI)
// ============================================================================

enum class EntityType {
    PERSON,
    COMPANY,
    ACCOUNT,
    SHELL_COMPANY,
    INTERMEDIARY,
    UNKNOWN
};

inline std::string entity_type_to_string(EntityType type) {
    switch (type) {
        case EntityType::PERSON:        return "PERSON";
        case EntityType::COMPANY:       return "COMPANY";
        case EntityType::ACCOUNT:       return "ACCOUNT";
        case EntityType::SHELL_COMPANY: return "SHELL_COMPANY";
        case EntityType::INTERMEDIARY:  return "INTERMEDIARY";
        case EntityType::UNKNOWN:       return "UNKNOWN";
        default:                        return "UNKNOWN";
    }
}

struct AuditEntity {
    std::string entity_id;     // Unique identifier
    std::string entity_name;   // Display name
    EntityType  entity_type;
    std::string registration_number;  // Company registration / ID number
    std::string address;
    std::string jurisdiction;
    std::unordered_map<std::string, std::string> properties;

    AuditEntity() : entity_type(EntityType::UNKNOWN) {}

    AuditEntity(const std::string & id, const std::string & name,
                EntityType type = EntityType::UNKNOWN)
        : entity_id(id), entity_name(name), entity_type(type) {}
};

struct AuditTransaction {
    std::string transaction_id;
    std::string source_entity;   // entity_id of the payer
    std::string dest_entity;     // entity_id of the payee
    double      amount;
    std::string currency;
    int64_t     timestamp;       // epoch seconds
    std::unordered_map<std::string, std::string> metadata;

    AuditTransaction() : amount(0.0), timestamp(0) {}
};

// ============================================================================
// Flow Graph (B.1)
// ============================================================================

// Aggregated directional flow between two entities.
struct FlowEdge {
    std::string source;
    std::string dest;
    double total_amount;
    uint64_t transaction_count;
    int64_t first_timestamp;
    int64_t last_timestamp;

    FlowEdge()
        : total_amount(0.0), transaction_count(0),
          first_timestamp(0), last_timestamp(0) {}
};

class FlowGraph {
public:
    FlowGraph();

    // Build the graph from a set of transactions within an optional
    // [start, end] epoch-seconds window (pass 0,0 to include all).
    void build(const std::vector<AuditTransaction> & transactions,
               int64_t window_start = 0, int64_t window_end = 0);

    // Accessors.
    std::vector<std::string> entities() const;
    std::vector<FlowEdge> edges() const;
    const FlowEdge * get_edge(const std::string & source,
                              const std::string & dest) const;

    std::vector<FlowEdge> out_edges(const std::string & entity) const;
    std::vector<FlowEdge> in_edges(const std::string & entity) const;

    double total_outflow(const std::string & entity) const;
    double total_inflow(const std::string & entity) const;
    uint64_t out_degree(const std::string & entity) const;
    uint64_t in_degree(const std::string & entity) const;

    size_t entity_count() const { return entities_.size(); }
    size_t edge_count() const { return edges_.size(); }

    // ---- Flow layout (B.1) ----
    // Rank entities for a left-to-right flow layout. Pure sources (only
    // outflow) rank 0 (leftmost); pure sinks rank highest (rightmost).
    // Entities in cycles share intermediate ranks. Returns entity -> rank.
    std::map<std::string, int> compute_flow_ranks() const;

    // ---- Branch-and-converge detection (B.3 support) ----
    // Entities that split flow to many destinations (fan-out) or gather flow
    // from many sources (fan-in) are classic money-movement indicators.
    std::vector<std::string> detect_fan_out(uint64_t min_destinations,
                                            double min_total_amount = 0.0) const;
    std::vector<std::string> detect_fan_in(uint64_t min_sources,
                                           double min_total_amount = 0.0) const;

private:
    struct EdgeKey {
        std::string source;
        std::string dest;
        bool operator<(const EdgeKey & o) const {
            if (source != o.source) return source < o.source;
            return dest < o.dest;
        }
    };

    std::set<std::string> entities_;
    std::map<EdgeKey, FlowEdge> edges_;
};

// ============================================================================
// String Similarity (B.2)
// ============================================================================

// All similarity functions return a score in [0.0, 1.0].
double levenshtein_similarity(const std::string & a, const std::string & b);
double jaro_similarity(const std::string & a, const std::string & b);
double jaro_winkler_similarity(const std::string & a, const std::string & b,
                               double prefix_weight = 0.1);

// Normalize an address for comparison (lowercase, collapse whitespace,
// expand common abbreviations, strip punctuation).
std::string normalize_address(const std::string & address);

// ============================================================================
// Entity Resolution (B.2)
// ============================================================================

struct EntityMatchConfig {
    double name_weight;
    double address_weight;
    double registration_weight;   // exact registration match is decisive
    double match_threshold;       // combined score >= threshold => same entity
    bool   use_jaro_winkler;      // else use Levenshtein for names

    EntityMatchConfig()
        : name_weight(0.5),
          address_weight(0.3),
          registration_weight(0.2),
          match_threshold(0.8),
          use_jaro_winkler(true) {}
};

// A resolved cluster of entity IDs believed to be the same real-world entity.
struct EntityCluster {
    std::string cluster_id;
    std::string canonical_entity_id;   // representative member
    std::vector<std::string> member_ids;
    double confidence;                 // mean pairwise match score

    EntityCluster() : confidence(0.0) {}
};

class EntityResolver {
public:
    explicit EntityResolver(const EntityMatchConfig & config = EntityMatchConfig());

    // Compute a match score in [0,1] between two entities.
    double match_score(const AuditEntity & a, const AuditEntity & b) const;

    // Resolve a collection of entities into canonical clusters using
    // Union-Find over pairs whose match score clears the threshold.
    std::vector<EntityCluster> resolve(const std::vector<AuditEntity> & entities) const;

    const EntityMatchConfig & config() const { return config_; }

private:
    EntityMatchConfig config_;

    struct UnionFind {
        std::unordered_map<std::string, std::string> parent;
        void make(const std::string & x);
        std::string find(const std::string & x);
        void unite(const std::string & a, const std::string & b);
    };
};

// ============================================================================
// Community Detection (B.2) - Louvain-style modularity optimization
// ============================================================================

// Detect communities in the flow graph. Returns entity_id -> community id.
// Uses a greedy modularity-optimization pass (single-level Louvain) suitable
// for the moderate graph sizes used in interactive audit exploration.
std::map<std::string, int> detect_communities(const FlowGraph & graph);

} // namespace flow
} // namespace ggnucash
