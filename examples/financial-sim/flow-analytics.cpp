#include "flow-analytics.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace ggnucash {
namespace flow {

// ============================================================================
// FlowGraph
// ============================================================================

FlowGraph::FlowGraph() {}

void FlowGraph::build(const std::vector<AuditTransaction> & transactions,
                      int64_t window_start, int64_t window_end) {
    entities_.clear();
    edges_.clear();

    bool has_window = (window_start != 0 || window_end != 0);

    for (const auto & tx : transactions) {
        if (has_window) {
            if (window_start != 0 && tx.timestamp < window_start) continue;
            if (window_end != 0 && tx.timestamp > window_end) continue;
        }
        if (tx.source_entity.empty() || tx.dest_entity.empty()) continue;

        entities_.insert(tx.source_entity);
        entities_.insert(tx.dest_entity);

        EdgeKey key{tx.source_entity, tx.dest_entity};
        auto & edge = edges_[key];
        edge.source = tx.source_entity;
        edge.dest = tx.dest_entity;
        edge.total_amount += tx.amount;
        edge.transaction_count++;
        if (edge.first_timestamp == 0 || tx.timestamp < edge.first_timestamp) {
            edge.first_timestamp = tx.timestamp;
        }
        if (tx.timestamp > edge.last_timestamp) {
            edge.last_timestamp = tx.timestamp;
        }
    }
}

std::vector<std::string> FlowGraph::entities() const {
    return std::vector<std::string>(entities_.begin(), entities_.end());
}

std::vector<FlowEdge> FlowGraph::edges() const {
    std::vector<FlowEdge> out;
    out.reserve(edges_.size());
    for (const auto & kv : edges_) out.push_back(kv.second);
    return out;
}

const FlowEdge * FlowGraph::get_edge(const std::string & source,
                                     const std::string & dest) const {
    auto it = edges_.find(EdgeKey{source, dest});
    return it == edges_.end() ? nullptr : &it->second;
}

std::vector<FlowEdge> FlowGraph::out_edges(const std::string & entity) const {
    std::vector<FlowEdge> out;
    for (const auto & kv : edges_) {
        if (kv.second.source == entity) out.push_back(kv.second);
    }
    return out;
}

std::vector<FlowEdge> FlowGraph::in_edges(const std::string & entity) const {
    std::vector<FlowEdge> out;
    for (const auto & kv : edges_) {
        if (kv.second.dest == entity) out.push_back(kv.second);
    }
    return out;
}

double FlowGraph::total_outflow(const std::string & entity) const {
    double sum = 0.0;
    for (const auto & kv : edges_) {
        if (kv.second.source == entity) sum += kv.second.total_amount;
    }
    return sum;
}

double FlowGraph::total_inflow(const std::string & entity) const {
    double sum = 0.0;
    for (const auto & kv : edges_) {
        if (kv.second.dest == entity) sum += kv.second.total_amount;
    }
    return sum;
}

uint64_t FlowGraph::out_degree(const std::string & entity) const {
    uint64_t n = 0;
    for (const auto & kv : edges_) {
        if (kv.second.source == entity) n++;
    }
    return n;
}

uint64_t FlowGraph::in_degree(const std::string & entity) const {
    uint64_t n = 0;
    for (const auto & kv : edges_) {
        if (kv.second.dest == entity) n++;
    }
    return n;
}

std::map<std::string, int> FlowGraph::compute_flow_ranks() const {
    // Kahn-style topological leveling. Pure sources get rank 0; each node's
    // rank is one more than the max rank of its predecessors. Cycles are
    // broken by processing remaining nodes in stable order.
    std::map<std::string, int> rank;
    std::map<std::string, int> indeg;

    for (const auto & e : entities_) {
        rank[e] = 0;
        indeg[e] = static_cast<int>(in_degree(e));
    }

    std::vector<std::string> frontier;
    for (const auto & e : entities_) {
        if (indeg[e] == 0) frontier.push_back(e);
    }
    std::sort(frontier.begin(), frontier.end());

    std::set<std::string> visited;
    while (true) {
        if (frontier.empty()) {
            // Pick an unvisited node with the smallest id to break a cycle.
            std::string next;
            for (const auto & e : entities_) {
                if (!visited.count(e)) { next = e; break; }
            }
            if (next.empty()) break;
            frontier.push_back(next);
        }

        std::string node = frontier.front();
        frontier.erase(frontier.begin());
        if (visited.count(node)) continue;
        visited.insert(node);

        // Set rank from predecessors.
        int max_pred = -1;
        for (const auto & kv : edges_) {
            if (kv.second.dest == node && visited.count(kv.second.source)) {
                max_pred = std::max(max_pred, rank[kv.second.source]);
            }
        }
        rank[node] = max_pred + 1;

        for (const auto & edge : out_edges(node)) {
            if (--indeg[edge.dest] == 0 && !visited.count(edge.dest)) {
                frontier.push_back(edge.dest);
            }
        }
        std::sort(frontier.begin(), frontier.end());
    }

    return rank;
}

std::vector<std::string> FlowGraph::detect_fan_out(uint64_t min_destinations,
                                                   double min_total_amount) const {
    std::vector<std::string> out;
    for (const auto & e : entities_) {
        if (out_degree(e) >= min_destinations &&
            total_outflow(e) >= min_total_amount) {
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> FlowGraph::detect_fan_in(uint64_t min_sources,
                                                  double min_total_amount) const {
    std::vector<std::string> out;
    for (const auto & e : entities_) {
        if (in_degree(e) >= min_sources &&
            total_inflow(e) >= min_total_amount) {
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ============================================================================
// String Similarity
// ============================================================================

static std::string to_lower(const std::string & s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

double levenshtein_similarity(const std::string & a, const std::string & b) {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;

    const size_t la = a.size(), lb = b.size();
    std::vector<size_t> prev(lb + 1), cur(lb + 1);
    for (size_t j = 0; j <= lb; j++) prev[j] = j;

    for (size_t i = 1; i <= la; i++) {
        cur[0] = i;
        for (size_t j = 1; j <= lb; j++) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    size_t dist = prev[lb];
    size_t max_len = std::max(la, lb);
    return 1.0 - static_cast<double>(dist) / static_cast<double>(max_len);
}

double jaro_similarity(const std::string & a, const std::string & b) {
    if (a == b) return 1.0;
    size_t la = a.size(), lb = b.size();
    if (la == 0 || lb == 0) return 0.0;

    size_t match_distance = std::max(la, lb) / 2;
    if (match_distance == 0) match_distance = 1;
    match_distance -= 1;

    std::vector<bool> a_matches(la, false), b_matches(lb, false);
    size_t matches = 0;

    for (size_t i = 0; i < la; i++) {
        size_t start = (i > match_distance) ? i - match_distance : 0;
        size_t end = std::min(i + match_distance + 1, lb);
        for (size_t j = start; j < end; j++) {
            if (b_matches[j] || a[i] != b[j]) continue;
            a_matches[i] = true;
            b_matches[j] = true;
            matches++;
            break;
        }
    }

    if (matches == 0) return 0.0;

    // Count transpositions.
    double transpositions = 0.0;
    size_t k = 0;
    for (size_t i = 0; i < la; i++) {
        if (!a_matches[i]) continue;
        while (!b_matches[k]) k++;
        if (a[i] != b[k]) transpositions++;
        k++;
    }
    transpositions /= 2.0;

    double m = static_cast<double>(matches);
    return (m / la + m / lb + (m - transpositions) / m) / 3.0;
}

double jaro_winkler_similarity(const std::string & a, const std::string & b,
                               double prefix_weight) {
    double jaro = jaro_similarity(a, b);
    // Common prefix length up to 4.
    size_t prefix = 0;
    size_t max_prefix = std::min(std::min(a.size(), b.size()), (size_t)4);
    while (prefix < max_prefix && a[prefix] == b[prefix]) prefix++;
    return jaro + prefix * prefix_weight * (1.0 - jaro);
}

std::string normalize_address(const std::string & address) {
    // 1. Lowercase and strip punctuation to single-space-separated tokens.
    std::string lower = to_lower(address);
    std::string cleaned;
    bool last_space = true;
    for (char c : lower) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cleaned += c;
            last_space = false;
        } else {
            if (!last_space) {
                cleaned += ' ';
                last_space = true;
            }
        }
    }
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();

    // 2. Tokenize and expand common abbreviations per token.
    static const std::map<std::string, std::string> abbrev = {
        {"st", "street"}, {"rd", "road"}, {"ave", "avenue"},
        {"blvd", "boulevard"}, {"dr", "drive"}, {"ln", "lane"},
        {"apt", "apartment"}, {"ste", "suite"}, {"no", "number"},
        {"ct", "court"}, {"pl", "place"}, {"pkwy", "parkway"},
        {"hwy", "highway"}, {"ter", "terrace"}, {"cres", "crescent"},
    };

    std::string out;
    std::istringstream iss(cleaned);
    std::string token;
    bool first = true;
    while (iss >> token) {
        auto it = abbrev.find(token);
        if (it != abbrev.end()) token = it->second;
        if (!first) out += ' ';
        out += token;
        first = false;
    }
    return out;
}

// ============================================================================
// EntityResolver
// ============================================================================

EntityResolver::EntityResolver(const EntityMatchConfig & config) : config_(config) {}

void EntityResolver::UnionFind::make(const std::string & x) {
    if (parent.find(x) == parent.end()) parent[x] = x;
}

std::string EntityResolver::UnionFind::find(const std::string & x) {
    auto it = parent.find(x);
    if (it == parent.end()) return x;
    if (it->second != x) {
        it->second = find(it->second);  // path compression
    }
    return it->second;
}

void EntityResolver::UnionFind::unite(const std::string & a, const std::string & b) {
    std::string ra = find(a), rb = find(b);
    if (ra != rb) parent[rb] = ra;
}

double EntityResolver::match_score(const AuditEntity & a, const AuditEntity & b) const {
    // Exact registration-number match is decisive.
    if (!a.registration_number.empty() &&
        a.registration_number == b.registration_number) {
        return 1.0;
    }

    std::string name_a = to_lower(a.entity_name);
    std::string name_b = to_lower(b.entity_name);
    double name_sim = config_.use_jaro_winkler
        ? jaro_winkler_similarity(name_a, name_b)
        : levenshtein_similarity(name_a, name_b);

    // Accumulate weighted scores only over signals that are actually present,
    // then renormalize so a missing signal (e.g. no address on either side)
    // does not dilute an otherwise strong name match.
    double weighted = 0.0;
    double weight_total = 0.0;

    weighted += config_.name_weight * name_sim;
    weight_total += config_.name_weight;

    if (!a.address.empty() && !b.address.empty()) {
        double addr_sim = levenshtein_similarity(normalize_address(a.address),
                                                 normalize_address(b.address));
        weighted += config_.address_weight * addr_sim;
        weight_total += config_.address_weight;
    }

    if (weight_total <= 0.0) return 0.0;
    return weighted / weight_total;
}

std::vector<EntityCluster> EntityResolver::resolve(
    const std::vector<AuditEntity> & entities) const {
    UnionFind uf;
    for (const auto & e : entities) uf.make(e.entity_id);

    // Accumulate pairwise scores for confidence computation.
    std::map<std::string, std::vector<double>> cluster_scores;

    for (size_t i = 0; i < entities.size(); i++) {
        for (size_t j = i + 1; j < entities.size(); j++) {
            double score = match_score(entities[i], entities[j]);
            if (score >= config_.match_threshold) {
                uf.unite(entities[i].entity_id, entities[j].entity_id);
                cluster_scores[entities[i].entity_id].push_back(score);
                cluster_scores[entities[j].entity_id].push_back(score);
            }
        }
    }

    // Group by root.
    std::map<std::string, std::vector<std::string>> groups;
    for (const auto & e : entities) {
        groups[uf.find(e.entity_id)].push_back(e.entity_id);
    }

    std::vector<EntityCluster> clusters;
    int idx = 0;
    for (auto & kv : groups) {
        EntityCluster cluster;
        cluster.cluster_id = "cluster-" + std::to_string(idx++);
        auto & members = kv.second;
        std::sort(members.begin(), members.end());
        cluster.member_ids = members;
        cluster.canonical_entity_id = members.front();

        double sum = 0.0;
        size_t n = 0;
        for (const auto & m : members) {
            auto it = cluster_scores.find(m);
            if (it != cluster_scores.end()) {
                for (double s : it->second) { sum += s; n++; }
            }
        }
        cluster.confidence = n > 0 ? sum / n : (members.size() == 1 ? 1.0 : 0.0);
        clusters.push_back(cluster);
    }

    return clusters;
}

// ============================================================================
// Community Detection (single-level Louvain / greedy modularity)
// ============================================================================

std::map<std::string, int> detect_communities(const FlowGraph & graph) {
    std::map<std::string, int> community;
    auto nodes = graph.entities();
    if (nodes.empty()) return community;

    // Build weighted undirected adjacency.
    std::map<std::string, std::map<std::string, double>> adj;
    double total_weight = 0.0;
    for (const auto & edge : graph.edges()) {
        adj[edge.source][edge.dest] += edge.total_amount;
        adj[edge.dest][edge.source] += edge.total_amount;
        total_weight += edge.total_amount;
    }
    if (total_weight <= 0.0) total_weight = 1.0;

    // Initialize: each node in its own community.
    int next_comm = 0;
    for (const auto & n : nodes) community[n] = next_comm++;

    // Degree of each node.
    std::map<std::string, double> degree;
    for (const auto & kv : adj) {
        for (const auto & inner : kv.second) {
            degree[kv.first] += inner.second;
        }
    }

    double m2 = 2.0 * total_weight;

    // Sum of degrees per community (maintained incrementally).
    std::map<int, double> comm_degree;
    for (const auto & n : nodes) comm_degree[community[n]] += degree[n];

    // Proper Louvain local-move gain for moving `node` from its community to
    // community C:
    //   dQ = [ k_i,in - (k_i * sum_tot_C) / m2 ] / m2
    // where k_i,in is the weight of edges from node into C, k_i the node's
    // degree, and sum_tot_C the total degree of community C (excluding node).
    auto move_gain = [&](const std::string & node, int target,
                         double k_i_in, int current_comm) {
        double k_i = degree[node];
        double sum_tot = comm_degree[target];
        if (target == current_comm) sum_tot -= k_i;  // exclude node itself
        return (k_i_in - (k_i * sum_tot) / m2) / m2;
    };

    bool improved = true;
    int iterations = 0;
    const int max_iterations = 100;

    while (improved && iterations < max_iterations) {
        improved = false;
        iterations++;
        for (const auto & node : nodes) {
            int current = community[node];

            // Tally edge weight from node into each neighboring community.
            std::map<int, double> comm_weight;
            for (const auto & kv : adj[node]) {
                comm_weight[community[kv.first]] += kv.second;
            }

            // Gain of staying put.
            double stay_gain = move_gain(node, current, comm_weight[current], current);
            double best_gain = stay_gain;
            int best_comm = current;

            for (const auto & kv : comm_weight) {
                int c = kv.first;
                if (c == current) continue;
                double gain = move_gain(node, c, kv.second, current);
                if (gain > best_gain + 1e-12) {
                    best_gain = gain;
                    best_comm = c;
                }
            }

            if (best_comm != current) {
                comm_degree[current] -= degree[node];
                comm_degree[best_comm] += degree[node];
                community[node] = best_comm;
                improved = true;
            }
        }
    }

    // Renumber communities densely.
    std::map<int, int> remap;
    int dense = 0;
    for (auto & kv : community) {
        if (remap.find(kv.second) == remap.end()) {
            remap[kv.second] = dense++;
        }
        kv.second = remap[kv.second];
    }
    return community;
}

} // namespace flow
} // namespace ggnucash
