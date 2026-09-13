#pragma once

// ============================================================================
// Membrane Reconciler - Phase B.3 / E.2 (accospace + isabellex + fincosys finops)
//
// A membrane-computing reconciliation engine: the accospace metagraph of an
// ecosystem (entities own accounts, accounts carry statements, payments link
// accounts) is folded into a tree of nested membranes, and every check is an
// annihilation between a "left" and a "right" token species.
//
// Mathematical image
//   Every cent is a token.  A check is   L, R --> #.   Any left token may
//   annihilate any right token, so the normal form of a membrane under
//   exhaustive annihilation holds tokens of ONE side only and their number is
//   |sum L - sum R|.  The residual IS the imbalance; a reconciled membrane
//   annihilates to nothing.  (Proved in isabellex src/HOL/Fin/Fin_Membrane.thy.)
//
// Kind synchrony
//   Membranes are grouped by kind.  One rule set is applied to every membrane
//   of a kind in the same step, all membranes of that kind in parallel -- the
//   whole supply chain is reconciled one kind at a time, not one entity at a
//   time.  A membrane whose clock reaches one dissolves and its residual rises
//   to its parent.  When the last wave settles, the ecosystem membrane holds
//   the SEDIMENT: every unmatched token, indexed by where it came from.
//
//     ECOSYSTEM  (skin)  inter-company legs, round trips, circular flows
//     ENTITY             intra-entity transfers
//     ACCOUNT            statement chain, master closing balance
//     STATEMENT          internal reconciliation  o + c  vs  z + d
//
// The engine is the native twin of the P-Lingua model exported by
// accospace/scripts/export_membrane_psystem.py; to_plingua() emits the same
// system so the two can be cross-checked on the ReZorg/plingua simulator.
// ============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace ggnucash {
namespace membrane {

// ============================================================================
// Multisets and rules
// ============================================================================

// A multiset of tokens: species name (with its indices spelled out, e.g. "zc{3}")
// mapped to a multiplicity in cents.
using multiset = std::map<std::string, int64_t>;

enum class membrane_kind : int {
    ECOSYSTEM = 0,
    ENTITY    = 1,
    ACCOUNT   = 2,
    STATEMENT = 3,
};

inline const char * membrane_kind_name(membrane_kind k) {
    switch (k) {
        case membrane_kind::ECOSYSTEM: return "ecosystem";
        case membrane_kind::ENTITY:    return "entity";
        case membrane_kind::ACCOUNT:   return "account";
        case membrane_kind::STATEMENT: return "statement";
    }
    return "?";
}

// A cooperative evolution rule  lhs --> rhs  applied with maximal parallelism:
// it fires min over lhs of (available / needed) times in one step.
struct evolution_rule {
    multiset lhs;
    multiset rhs;
    bool     dissolve = false;  // @d : the membrane dissolves after this step
    std::string label;          // for reports / to_plingua

    // applications possible against a multiset
    int64_t max_applications(const multiset & ms) const {
        int64_t n = INT64_MAX;
        for (const auto & kv : lhs) {
            auto it = ms.find(kv.first);
            int64_t have = it == ms.end() ? 0 : it->second;
            n = std::min(n, have / kv.second);
        }
        return n == INT64_MAX ? 0 : n;
    }
};

// Species helpers ------------------------------------------------------------

inline std::string tok(const std::string & species, int64_t i) {
    return species + "{" + std::to_string(i) + "}";
}

inline std::string tok(const std::string & species, int64_t i, int64_t j) {
    return species + "{" + std::to_string(i) + "," + std::to_string(j) + "}";
}

inline std::string tok(const std::string & species, int64_t i, int64_t j, int64_t k) {
    return species + "{" + std::to_string(i) + "," + std::to_string(j) + "," + std::to_string(k) + "}";
}

// L, R --> product   (product empty = #)
inline evolution_rule annihilation(const std::string & left, const std::string & right,
                                   const std::string & product = "") {
    evolution_rule r;
    r.lhs[left]  += 1;
    r.lhs[right] += 1;
    if (!product.empty()) {
        r.rhs[product] += 1;
    }
    r.label = "[" + left + ", " + right + " --> " + (product.empty() ? "#" : product) + "]";
    return r;
}

// ============================================================================
// Membranes
// ============================================================================

struct membrane {
    int           id     = -1;
    int           parent = -1;
    membrane_kind kind   = membrane_kind::ECOSYSTEM;
    int           index  = 0;         // 1-based index within its kind
    std::string   name;               // entity code / account number / statement no.
    std::vector<int> children;
    multiset      contents;
    std::vector<evolution_rule> rules;  // this membrane's rule set
    bool          alive = true;

    int label() const { return static_cast<int>(kind) * 100 + index; }
};

// ============================================================================
// Reconciler
// ============================================================================

struct statement_spec {
    std::string entity;
    std::string account;
    int64_t     number  = -1;   // -1 = unnumbered (chained by order)
    int64_t     opening = 0;    // cents
    int64_t     credits = 0;
    int64_t     debits  = 0;
    int64_t     closing = 0;
};

struct payment_spec {
    int64_t     ref = 0;
    std::string from_account;
    std::string to_account;
    int64_t     amount    = 0;
    bool        from_seen = true;
    bool        to_seen   = true;
};

// Wave schedule: the clock value each kind starts with (dissolves when it hits 1)
struct wave_schedule {
    int statement_clock = 2;
    int account_clock   = 4;
    int entity_clock    = 6;
};

class membrane_reconciler {
public:
    explicit membrane_reconciler(wave_schedule schedule = wave_schedule{}) : schedule_(schedule) {
        membrane skin;
        skin.id   = 0;
        skin.kind = membrane_kind::ECOSYSTEM;
        skin.name = "ecosystem";
        membranes_.push_back(skin);
    }

    // ---- building the ecosystem --------------------------------------------

    int add_entity(const std::string & code) {
        auto it = entity_ids_.find(code);
        if (it != entity_ids_.end()) {
            return it->second;
        }
        membrane m;
        m.kind   = membrane_kind::ENTITY;
        m.index  = static_cast<int>(entity_ids_.size()) + 1;
        m.name   = code;
        m.parent = 0;
        int id = push(m);
        entity_ids_[code] = id;
        return id;
    }

    int add_account(const std::string & entity, const std::string & number, int64_t master_closing = -1) {
        auto it = account_ids_.find(number);
        if (it != account_ids_.end()) {
            if (master_closing >= 0) {
                master_closing_[it->second] = master_closing;
            }
            return it->second;
        }
        int eid = add_entity(entity);
        membrane m;
        m.kind   = membrane_kind::ACCOUNT;
        m.index  = static_cast<int>(account_ids_.size()) + 1;
        m.name   = number;
        m.parent = eid;
        int id = push(m);
        account_ids_[number] = id;
        if (master_closing >= 0) {
            master_closing_[id] = master_closing;
        }
        return id;
    }

    int add_statement(const statement_spec & s) {
        int aid = add_account(s.entity, s.account);
        membrane m;
        m.kind   = membrane_kind::STATEMENT;
        m.index  = static_cast<int>(statements_.size()) + 1;
        m.name   = s.number < 0 ? "unnumbered" : std::to_string(s.number);
        m.parent = aid;
        int id = push(m);
        statements_.push_back({id, s});
        return id;
    }

    void add_payment(const payment_spec & p) { payments_.push_back(p); }

    // Compile the rule sets and initial multisets.  Must be called once after
    // building and before step()/run().
    void build() {
        built_ = true;
        const int E = static_cast<int>(entity_ids_.size());

        // statements ---------------------------------------------------------
        for (const auto & entry : statements_) {
            membrane & m = membranes_[entry.first];
            const statement_spec & s = entry.second;
            const int64_t si = m.index;
            put(m.contents, tok("o", si), s.opening);
            put(m.contents, tok("c", si), s.credits);
            put(m.contents, tok("d", si), s.debits);
            put(m.contents, tok("z", si), s.closing);
            put(m.contents, tok("oc", si), s.opening);
            put(m.contents, tok("zc", si), s.closing);
            m.contents[tok("k", schedule_.statement_clock)] = 1;

            m.rules.push_back(annihilation(tok("o", si), tok("z", si)));
            m.rules.push_back(annihilation(tok("c", si), tok("d", si)));
            m.rules.push_back(annihilation(tok("o", si), tok("d", si)));
            m.rules.push_back(annihilation(tok("c", si), tok("z", si)));
            add_clock_rules(m, "k", schedule_.statement_clock);
        }

        // accounts -----------------------------------------------------------
        for (auto & kv : account_ids_) {
            membrane & acc = membranes_[kv.second];
            acc.contents[tok("ka", schedule_.account_clock)] = 1;

            // ordered statements of this account
            std::vector<std::pair<int, statement_spec>> sts;
            for (const auto & entry : statements_) {
                if (membranes_[entry.first].parent == acc.id) {
                    sts.push_back(entry);
                }
            }
            std::stable_sort(sts.begin(), sts.end(), [](const auto & a, const auto & b) {
                if ((a.second.number < 0) != (b.second.number < 0)) {
                    return a.second.number >= 0;
                }
                return a.second.number < b.second.number;
            });
            for (size_t i = 0; i + 1 < sts.size(); ++i) {
                const statement_spec & prev = sts[i].second;
                const statement_spec & next = sts[i + 1].second;
                const int64_t ps = membranes_[sts[i].first].index;
                const int64_t ns = membranes_[sts[i + 1].first].index;
                if (linkable(prev, next)) {
                    acc.rules.push_back(annihilation(tok("zc", ps), tok("oc", ns)));
                }
                // else: MISSING_WINDOW -- no pairing, both copies survive
            }
            if (!sts.empty()) {
                const int64_t first = membranes_[sts.front().first].index;
                const int64_t last  = membranes_[sts.back().first].index;
                evolution_rule drop;
                drop.lhs[tok("oc", first)] = 1;
                drop.label = "[" + tok("oc", first) + " --> #]";
                acc.rules.push_back(drop);
                auto mc = master_closing_.find(acc.id);
                if (mc != master_closing_.end()) {
                    put(acc.contents, tok("mz", acc.index), mc->second);
                    acc.rules.push_back(annihilation(tok("zc", last), tok("mz", acc.index)));
                } else {
                    // no inventory balance to check the last closing against: an open end, not a break
                    evolution_rule drop_last;
                    drop_last.lhs[tok("zc", last)] = 1;
                    drop_last.label = "[" + tok("zc", last) + " --> #]";
                    acc.rules.push_back(drop_last);
                }
            }
            add_clock_rules(acc, "ka", schedule_.account_clock);
        }

        // payments -----------------------------------------------------------
        int64_t max_ref = 0;
        for (const payment_spec & p : payments_) {
            max_ref = std::max(max_ref, p.ref);
            auto src = account_ids_.find(p.from_account);
            auto dst = account_ids_.find(p.to_account);
            if (src == account_ids_.end() || dst == account_ids_.end()) {
                continue;
            }
            membrane & sa = membranes_[src->second];
            membrane & da = membranes_[dst->second];
            const int se = membranes_[sa.parent].index;
            const int de = membranes_[da.parent].index;
            const bool intra = se == de;
            if (p.from_seen) {
                if (intra) {
                    sa.contents[tok("xo", p.ref)] += 1;
                } else {
                    sa.contents[tok("out", p.ref)] += 1;
                    sa.contents[tok("flow", se, de)] += 1;
                    sa.contents[tok("tri", se, de)] += 1;
                }
            }
            if (p.to_seen) {
                da.contents[intra ? tok("xi", p.ref) : tok("in", p.ref)] += 1;
            }
        }

        // entities -----------------------------------------------------------
        for (auto & kv : entity_ids_) {
            membrane & ent = membranes_[kv.second];
            ent.contents[tok("ke", schedule_.entity_clock)] = 1;
            for (const payment_spec & p : payments_) {
                ent.rules.push_back(annihilation(tok("xo", p.ref), tok("xi", p.ref)));
            }
            add_clock_rules(ent, "ke", schedule_.entity_clock);
        }

        // ecosystem ----------------------------------------------------------
        membrane & skin = membranes_[0];
        skin.rules.clear();
        for (const payment_spec & p : payments_) {
            skin.rules.push_back(annihilation(tok("out", p.ref), tok("in", p.ref), tok("icm", p.ref)));
        }
        for (int x = 1; x <= E; ++x) {
            for (int y = x + 1; y <= E; ++y) {
                skin.rules.push_back(annihilation(tok("flow", x, y), tok("flow", y, x), tok("rt", x, y)));
            }
        }
        for (int x = 1; x <= E; ++x) {
            for (int y = x + 1; y <= E; ++y) {
                for (int w = x + 1; w <= E; ++w) {
                    if (w == y) {
                        continue;
                    }
                    evolution_rule r;
                    r.lhs[tok("tri", x, y)] += 1;
                    r.lhs[tok("tri", y, w)] += 1;
                    r.lhs[tok("tri", w, x)] += 1;
                    r.rhs[tok("cyc", x, y, w)] += 1;
                    r.label = "[" + tok("tri", x, y) + ", " + tok("tri", y, w) + ", " + tok("tri", w, x) +
                              " --> " + tok("cyc", x, y, w) + "]";
                    skin.rules.push_back(r);
                }
            }
        }
        (void) max_ref;
    }

    // ---- running -------------------------------------------------------------

    // One synchronous step: every kind's rule set fires in every live membrane
    // of that kind with maximal parallelism; dissolutions then release contents
    // upward.  Membranes are independent within a step, so the kinds are swept
    // with a thread per chunk of membranes.  Returns the number of rule
    // applications.
    int64_t step() {
        if (!built_) {
            build();
        }
        std::vector<int64_t> applications(membranes_.size(), 0);
        std::vector<char>    dissolving(membranes_.size(), 0);

        // Phase 1 -- local evolution, parallel over membranes.
        auto work = [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                membrane & m = membranes_[i];
                if (!m.alive) {
                    continue;
                }
                multiset produced;
                bool     progress = true;
                while (progress) {          // exhaust: maximal parallelism
                    progress = false;
                    for (const evolution_rule & r : m.rules) {
                        int64_t n = r.max_applications(m.contents);
                        if (n <= 0) {
                            continue;
                        }
                        for (const auto & kv : r.lhs) {
                            m.contents[kv.first] -= kv.second * n;
                            if (m.contents[kv.first] == 0) {
                                m.contents.erase(kv.first);
                            }
                        }
                        for (const auto & kv : r.rhs) {
                            produced[kv.first] += kv.second * n;
                        }
                        if (r.dissolve) {
                            dissolving[i] = 1;
                        }
                        applications[i] += n;
                        progress = true;
                    }
                }
                for (const auto & kv : produced) {
                    m.contents[kv.first] += kv.second;
                }
            }
        };
        run_parallel(membranes_.size(), work);

        // Phase 2 -- dissolution: contents rise to the parent (sequential, tree edit).
        for (size_t i = 1; i < membranes_.size(); ++i) {
            if (!dissolving[i]) {
                continue;
            }
            membrane & m = membranes_[i];
            membrane & p = membranes_[m.parent];
            for (const auto & kv : m.contents) {
                p.contents[kv.first] += kv.second;
            }
            m.contents.clear();
            m.alive = false;
            p.children.erase(std::remove(p.children.begin(), p.children.end(), m.id), p.children.end());
        }

        int64_t total = 0;
        for (int64_t a : applications) {
            total += a;
        }
        ++time_;
        return total;
    }

    // Run until no rule applies anywhere (or max_steps).  Returns steps taken.
    int run(int max_steps = 64) {
        int steps = 0;
        while (steps < max_steps) {
            int64_t n = step();
            ++steps;
            if (n == 0) {
                break;
            }
        }
        return steps;
    }

    // ---- inspection ----------------------------------------------------------

    const multiset & sediment() const { return membranes_[0].contents; }
    const std::vector<membrane> & membranes() const { return membranes_; }
    int time() const { return time_; }

    int live_count(membrane_kind k) const {
        int n = 0;
        for (const membrane & m : membranes_) {
            if (m.alive && m.kind == k) {
                ++n;
            }
        }
        return n;
    }

    // Read the sediment: one line per token species with what it means.
    std::string report() const {
        std::ostringstream os;
        const multiset & sed = sediment();
        os << "membrane reconciliation -- sediment after " << time_ << " steps\n";
        if (sed.empty()) {
            os << "  (empty: the ecosystem is fully reconciled)\n";
        }
        for (const auto & kv : sed) {
            os << "  " << kv.first << " * " << kv.second << "   " << explain(kv.first) << "\n";
        }
        return os.str();
    }

    static std::string explain(const std::string & token) {
        const std::string sp = token.substr(0, token.find('{'));
        if (sp == "o" || sp == "c") return "statement: inflow side (opening/credits) exceeds by this many cents";
        if (sp == "z" || sp == "d") return "statement: outflow side (closing/debits) exceeds by this many cents";
        if (sp == "zc")  return "closing copy unmatched: chain break, missing window or master-balance gap";
        if (sp == "oc")  return "opening copy unmatched: chain break or missing window";
        if (sp == "mz")  return "master closing balance exceeds the last statement's closing";
        if (sp == "out") return "inter-company payment left the source but never arrived: diversion suspect";
        if (sp == "in")  return "inter-company receipt with no matching outgoing leg: unexplained inflow";
        if (sp == "xo")  return "intra-entity transfer left an account but never arrived";
        if (sp == "xi")  return "intra-entity receipt with no matching outgoing leg";
        if (sp == "icm") return "inter-company payment matched on both sides";
        if (sp == "rt")  return "round trip between two entities";
        if (sp == "cyc") return "circular flow through three entities";
        if (sp == "flow" || sp == "tri") return "inter-company flow marker (no cycle pattern)";
        return "";
    }

    // Emit the equivalent P-Lingua system (ReZorg/plingua, transition model).
    std::string to_plingua(const std::string & include = "../transition_model.pli") const {
        std::ostringstream os;
        os << "/* generated by ggnucash membrane_reconciler::to_plingua */\n";
        os << "@model<transition>\n@include \"" << include << "\"\n\ndef main()\n{\n";
        os << "  @mu = " << mu(0) << ";\n";
        for (const membrane & m : membranes_) {
            if (m.id == 0 || m.contents.empty()) {
                continue;
            }
            os << "  @ms(" << m.label() << ") = " << ms(m.contents) << ";\n";
        }
        for (const membrane & m : membranes_) {
            for (const evolution_rule & r : m.rules) {
                os << "  [" << ms(r.lhs) << " --> " << (r.rhs.empty() && !r.dissolve ? "#" : ms(r.rhs))
                   << (r.dissolve ? (r.rhs.empty() ? "@d" : ", @d") : "") << "]'" << m.label() << ";\n";
            }
        }
        os << "}\n";
        return os.str();
    }

    static bool linkable(const statement_spec & prev, const statement_spec & next) {
        if (prev.number < 0 || next.number < 0) {
            return prev.number < 0 && next.number < 0;
        }
        return next.number == prev.number + 1;
    }

private:
    int push(membrane m) {
        m.id = static_cast<int>(membranes_.size());
        membranes_.push_back(m);
        if (m.parent >= 0) {
            membranes_[m.parent].children.push_back(m.id);
        }
        return m.id;
    }

    static void put(multiset & ms, const std::string & species, int64_t n) {
        if (n > 0) {
            ms[species] += n;
        }
    }

    static void add_clock_rules(membrane & m, const std::string & clock, int start) {
        for (int i = start; i >= 2; --i) {
            evolution_rule r;
            r.lhs[tok(clock, i)]     = 1;
            r.rhs[tok(clock, i - 1)] = 1;
            r.label = "[" + tok(clock, i) + " --> " + tok(clock, i - 1) + "]";
            m.rules.push_back(r);
        }
        evolution_rule d;
        d.lhs[tok(clock, 1)] = 1;
        d.dissolve = true;
        d.label = "[" + tok(clock, 1) + " --> @d]";
        m.rules.push_back(d);
    }

    template <typename F> static void run_parallel(size_t n, F && f) {
        unsigned hw = std::thread::hardware_concurrency();
        size_t   threads = std::max<size_t>(1, std::min<size_t>(hw == 0 ? 1 : hw, n / 64 + 1));
        if (threads <= 1) {
            f(0, n);
            return;
        }
        std::vector<std::thread> pool;
        size_t chunk = (n + threads - 1) / threads;
        for (size_t t = 0; t < threads; ++t) {
            size_t b = t * chunk;
            size_t e = std::min(n, b + chunk);
            if (b >= e) {
                break;
            }
            pool.emplace_back([&f, b, e]() { f(b, e); });
        }
        for (std::thread & th : pool) {
            th.join();
        }
    }

    std::string mu(int id) const {
        const membrane & m = membranes_[id];
        std::ostringstream os;
        os << "[ ";
        for (int c : m.children) {
            os << mu(c) << " ";
        }
        os << "]'" << m.label();
        return os.str();
    }

    static std::string ms(const multiset & m) {
        std::ostringstream os;
        bool first = true;
        for (const auto & kv : m) {
            if (kv.second <= 0) {
                continue;
            }
            os << (first ? "" : ", ") << kv.first;
            if (kv.second > 1) {
                os << "*" << kv.second;
            }
            first = false;
        }
        return os.str();
    }

    wave_schedule                       schedule_;
    std::vector<membrane>               membranes_;
    std::map<std::string, int>          entity_ids_;   // code   -> membrane id
    std::map<std::string, int>          account_ids_;  // number -> membrane id
    std::map<int, int64_t>              master_closing_;
    std::vector<std::pair<int, statement_spec>> statements_;  // membrane id, spec
    std::vector<payment_spec>           payments_;
    bool                                built_ = false;
    int                                 time_  = 0;
};

// ============================================================================
// accospace balance-schedule loader
//
// `accospace records` writes one CSV per account (records/atomese/balances/
// <RECORD>.csv) listing every statement with its opening and closing balance,
// credit and debit totals, placeability and the link class to the previous
// statement.  This loads such a schedule straight into a reconciler: every
// placeable statement becomes a statement membrane, and the schedule's own link
// classes decide the chain pairings (CONSECUTIVE, NIL_WINDOW, NUMBERING_ARTEFACT
// and BALANCE_BREAK pair; MISSING_WINDOW and DUPLICATE_NUMBER leave both copies
// unpaired).  Statement numbers are taken from the schedule; an unpaired link is
// forced by giving the next statement a non-consecutive number.
// ============================================================================

struct schedule_row {
    std::string statement;
    int64_t     number  = -1;
    int64_t     opening = 0;
    int64_t     closing = 0;
    int64_t     credits = 0;
    int64_t     debits  = 0;
    bool        placeable = true;
    std::string link_class;
};

inline int64_t schedule_cents(const std::string & text) {
    if (text.empty()) {
        return 0;
    }
    double v = std::strtod(text.c_str(), nullptr);
    double scaled = std::fabs(v) * 100.0;
    int64_t q = static_cast<int64_t>(scaled + 0.5);
    return v < 0 ? -q : q;
}

inline std::vector<std::string> schedule_split(const std::string & line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') {
            quoted = !quoted;
        } else if (ch == ',' && !quoted) {
            out.push_back(cur);
            cur.clear();
        } else if (ch != '\r') {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}

// Parse the CSV text of one accospace balance schedule.  Rows without an
// opening or closing balance (UNTESTABLE) are dropped.
inline std::vector<schedule_row> parse_balance_schedule(const std::string & csv) {
    std::vector<schedule_row> rows;
    std::istringstream in(csv);
    std::string line;
    std::map<std::string, size_t> col;
    bool header = true;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        std::vector<std::string> f = schedule_split(line);
        if (header) {
            for (size_t i = 0; i < f.size(); ++i) {
                col[f[i]] = i;
            }
            header = false;
            continue;
        }
        auto get = [&](const char * name) -> std::string {
            auto it = col.find(name);
            return it == col.end() || it->second >= f.size() ? std::string() : f[it->second];
        };
        if (get("opening_balance").empty() || get("closing_balance").empty()) {
            continue;
        }
        schedule_row r;
        r.statement  = get("statement");
        std::string num = get("statement_number");
        r.number     = num.empty() ? -1 : std::strtoll(num.c_str(), nullptr, 10);
        r.opening    = schedule_cents(get("opening_balance"));
        r.closing    = schedule_cents(get("closing_balance"));
        r.credits    = std::llabs(schedule_cents(get("total_credits")));
        r.debits     = std::llabs(schedule_cents(get("total_debits")));
        r.placeable  = get("placeable") != "false";
        r.link_class = get("link_class");
        rows.push_back(r);
    }
    return rows;
}

inline bool schedule_link_pairs(const std::string & link_class) {
    return link_class == "CONSECUTIVE" || link_class == "NIL_WINDOW" || link_class == "NUMBERING_ARTEFACT" ||
           link_class == "BALANCE_BREAK";
}

// Add every placeable statement of a schedule to the reconciler under the given
// entity and account, using the schedule's link classes for the chain.
//
// Negative balances (credit cards, overdrafts) cannot be token counts, so every
// opening, closing and the master closing of the account are translated by one
// constant that makes them all non-negative.  Both checked identities are
// translation invariant (o + c = z + d shifts o and z together; z_s = o_{s+1}
// shifts both sides), so every residual is unchanged.  The shift applied is
// returned through `shift_out` when given.
// Returns the number of statements added.
inline int add_balance_schedule(membrane_reconciler & r, const std::string & entity, const std::string & account,
                                const std::string & csv, int64_t master_closing = -1, int64_t * shift_out = nullptr) {
    std::vector<schedule_row> rows = parse_balance_schedule(csv);
    int64_t low = master_closing >= 0 ? 0 : master_closing;
    if (master_closing < 0 && master_closing != -1) {
        low = master_closing;   // -1 means "no master closing"
    } else {
        low = 0;
    }
    for (const schedule_row & row : rows) {
        low = std::min(low, std::min(row.opening, row.closing));
    }
    const int64_t shift = low < 0 ? -low : 0;
    if (shift_out) {
        *shift_out = shift;
    }
    r.add_account(entity, account, master_closing == -1 ? -1 : master_closing + shift);
    int64_t synthetic = 0;   // renumber so that linkable() follows the recorded link classes
    int     added = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const schedule_row & row = rows[i];
        if (!row.placeable) {
            continue;   // recorded outside the chain; the P-Lingua export keeps its internal check only
        }
        bool pairs = added == 0 || schedule_link_pairs(row.link_class);
        synthetic += pairs ? 1 : 2;   // a skipped number breaks the pairing
        statement_spec s;
        s.entity  = entity;
        s.account = account;
        s.number  = synthetic;
        s.opening = row.opening + shift;
        s.credits = row.credits;
        s.debits  = row.debits;
        s.closing = row.closing + shift;
        r.add_statement(s);
        ++added;
    }
    return added;
}

} // namespace membrane
} // namespace ggnucash
