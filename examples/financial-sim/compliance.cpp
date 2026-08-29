#include "compliance.h"

// ============================================================================
// Regulatory Compliance Engine - Issue #006 (implementation)
// ============================================================================

namespace ggnucash {
namespace compliance {

// ============================================================================
// Task 6.1 - SOX Compliance: SoxComplianceManager
// ============================================================================

SoxComplianceManager::SoxComplianceManager(audit::ImmutableAuditTrail & trail,
                                           validation::TransactionValidator & validator)
    : trail_(trail), validator_(validator), change_counter_(0) {
    register_control_test("full-validation",
        [this](const std::vector<Transaction> & txs) {
            return validator_.validate_all(txs);
        });
    register_control_test("double-entry-balance",
        [this](const std::vector<Transaction> & txs) {
            validation::ValidationReport report;
            report.findings = validator_.validate_double_entry(txs);
            report.transactions_checked = txs.size();
            for (const auto & f : report.findings) {
                report.findings_total++;
                switch (f.severity) {
                    case validation::ValidationSeverity::PASS:     report.findings_pass++; break;
                    case validation::ValidationSeverity::INFO:     report.findings_info++; break;
                    case validation::ValidationSeverity::WARNING:  report.findings_warning++; break;
                    case validation::ValidationSeverity::ERROR:    report.findings_error++; break;
                    case validation::ValidationSeverity::CRITICAL: report.findings_critical++; break;
                }
            }
            return report;
        });
}

std::pair<std::string, std::string>
SoxComplianceManager::ordered_pair(const std::string & a, const std::string & b) {
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

// ---- Segregation of Duties ----

void SoxComplianceManager::add_sod_conflict(const std::string & role_a,
                                            const std::string & role_b) {
    std::lock_guard<std::mutex> lock(mutex_);
    sod_conflicts_.insert(ordered_pair(role_a, role_b));
}

void SoxComplianceManager::remove_sod_conflict(const std::string & role_a,
                                               const std::string & role_b) {
    std::lock_guard<std::mutex> lock(mutex_);
    sod_conflicts_.erase(ordered_pair(role_a, role_b));
}

size_t SoxComplianceManager::sod_conflict_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sod_conflicts_.size();
}

void SoxComplianceManager::assign_role(const std::string & user_id,
                                       const std::string & role,
                                       const std::string & actor) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        user_roles_[user_id].insert(role);
    }
    std::map<std::string, std::string> meta;
    meta["user_id"] = user_id;
    meta["role"]    = role;
    trail_.record_event(audit::AuditSeverity::INFO,
                        audit::AuditCategory::ACCESS_CONTROL,
                        actor, "ASSIGN_ROLE", user_id,
                        "Role '" + role + "' assigned to user '" + user_id + "'",
                        meta);
}

void SoxComplianceManager::revoke_role(const std::string & user_id,
                                       const std::string & role,
                                       const std::string & actor) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_roles_.find(user_id);
        if (it != user_roles_.end()) {
            it->second.erase(role);
        }
    }
    std::map<std::string, std::string> meta;
    meta["user_id"] = user_id;
    meta["role"]    = role;
    trail_.record_event(audit::AuditSeverity::INFO,
                        audit::AuditCategory::ACCESS_CONTROL,
                        actor, "REVOKE_ROLE", user_id,
                        "Role '" + role + "' revoked from user '" + user_id + "'",
                        meta);
}

std::set<std::string> SoxComplianceManager::get_user_roles(const std::string & user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = user_roles_.find(user_id);
    return (it != user_roles_.end()) ? it->second : std::set<std::string>();
}

std::vector<SodViolation>
SoxComplianceManager::find_conflicts(const std::string & user_id,
                                     const std::set<std::string> & roles) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SodViolation> violations;
    for (const auto & conflict : sod_conflicts_) {
        if (roles.count(conflict.first) && roles.count(conflict.second)) {
            SodViolation v;
            v.user_id = user_id;
            v.role_a  = conflict.first;
            v.role_b  = conflict.second;
            v.blocking = true;
            v.description = "Segregation-of-duties conflict: user '" + user_id +
                            "' holds conflicting roles '" + conflict.first +
                            "' and '" + conflict.second + "'";
            violations.push_back(v);
        }
    }
    return violations;
}

std::vector<SodViolation>
SoxComplianceManager::check_sod(const std::string & user_id, const std::string & action) {
    std::vector<SodViolation> violations;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = user_roles_.find(user_id);
        if (it == user_roles_.end()) {
            return violations;  // No roles => no conflicts.
        }
        for (const auto & conflict : sod_conflicts_) {
            if (it->second.count(conflict.first) && it->second.count(conflict.second)) {
                SodViolation v;
                v.user_id = user_id;
                v.role_a  = conflict.first;
                v.role_b  = conflict.second;
                v.action  = action;
                v.blocking = true;
                v.description = "Segregation-of-duties conflict: user '" + user_id +
                                "' holds conflicting roles '" + conflict.first +
                                "' and '" + conflict.second + "' while attempting '" +
                                action + "'";
                violations.push_back(v);
            }
        }
    }
    for (const auto & v : violations) {
        std::map<std::string, std::string> meta;
        meta["user_id"] = v.user_id;
        meta["role_a"]  = v.role_a;
        meta["role_b"]  = v.role_b;
        meta["action"]  = v.action;
        trail_.record_event(audit::AuditSeverity::CRITICAL,
                            audit::AuditCategory::COMPLIANCE_EVENT,
                            user_id, "SOD_VIOLATION", action, v.description, meta);
    }
    return violations;
}

// ---- Change Management Workflow ----

std::string SoxComplianceManager::request_change(const std::string & title,
                                                 const std::string & description,
                                                 const std::string & requested_by) {
    uint64_t seq = ++change_counter_;
    std::string change_id = "CHG-" + std::to_string(seq);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ChangeRecord rec;
        rec.change_id    = change_id;
        rec.title        = title;
        rec.description  = description;
        rec.requested_by = requested_by;
        rec.state        = ChangeState::REQUESTED;
        rec.requested_at = std::chrono::system_clock::now();
        changes_[change_id] = rec;
    }

    std::map<std::string, std::string> meta;
    meta["change_id"] = change_id;
    meta["state"]     = change_state_to_string(ChangeState::REQUESTED);
    std::string entry_id = trail_.record_event(
        audit::AuditSeverity::INFO, audit::AuditCategory::COMPLIANCE_EVENT,
        requested_by, "CHANGE_REQUESTED", change_id,
        "Change requested: " + title + " - " + description, meta);

    std::lock_guard<std::mutex> lock(mutex_);
    changes_[change_id].audit_entry_ids.push_back(entry_id);
    return change_id;
}

bool SoxComplianceManager::approve_change(const std::string & change_id,
                                          const std::string & approver) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = changes_.find(change_id);
        if (it == changes_.end() || it->second.state != ChangeState::REQUESTED) {
            return false;
        }
        // SoD: requester may not approve their own change.
        if (it->second.requested_by == approver) {
            return false;
        }
        it->second.state       = ChangeState::APPROVED;
        it->second.approved_by = approver;
        it->second.approved_at = std::chrono::system_clock::now();
    }

    std::map<std::string, std::string> meta;
    meta["change_id"] = change_id;
    meta["state"]     = change_state_to_string(ChangeState::APPROVED);
    std::string entry_id = trail_.record_event(
        audit::AuditSeverity::INFO, audit::AuditCategory::COMPLIANCE_EVENT,
        approver, "CHANGE_APPROVED", change_id,
        "Change " + change_id + " approved by " + approver, meta);

    std::lock_guard<std::mutex> lock(mutex_);
    changes_[change_id].audit_entry_ids.push_back(entry_id);
    return true;
}

bool SoxComplianceManager::reject_change(const std::string & change_id,
                                         const std::string & approver,
                                         const std::string & reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = changes_.find(change_id);
        if (it == changes_.end() || it->second.state != ChangeState::REQUESTED) {
            return false;
        }
        it->second.state       = ChangeState::REJECTED;
        it->second.approved_by = approver;
        it->second.approved_at = std::chrono::system_clock::now();
    }

    std::map<std::string, std::string> meta;
    meta["change_id"] = change_id;
    meta["state"]     = change_state_to_string(ChangeState::REJECTED);
    meta["reason"]    = reason;
    std::string entry_id = trail_.record_event(
        audit::AuditSeverity::WARNING, audit::AuditCategory::COMPLIANCE_EVENT,
        approver, "CHANGE_REJECTED", change_id,
        "Change " + change_id + " rejected by " + approver + ": " + reason, meta);

    std::lock_guard<std::mutex> lock(mutex_);
    changes_[change_id].audit_entry_ids.push_back(entry_id);
    return true;
}

bool SoxComplianceManager::implement_change(const std::string & change_id,
                                            const std::string & implementer) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = changes_.find(change_id);
        if (it == changes_.end() || it->second.state != ChangeState::APPROVED) {
            return false;
        }
        // SoD: approver may not implement their own approval.
        if (it->second.approved_by == implementer) {
            return false;
        }
        it->second.state          = ChangeState::IMPLEMENTED;
        it->second.implemented_by = implementer;
        it->second.implemented_at = std::chrono::system_clock::now();
    }

    std::map<std::string, std::string> meta;
    meta["change_id"] = change_id;
    meta["state"]     = change_state_to_string(ChangeState::IMPLEMENTED);
    std::string entry_id = trail_.record_event(
        audit::AuditSeverity::INFO, audit::AuditCategory::COMPLIANCE_EVENT,
        implementer, "CHANGE_IMPLEMENTED", change_id,
        "Change " + change_id + " implemented by " + implementer, meta);

    std::lock_guard<std::mutex> lock(mutex_);
    changes_[change_id].audit_entry_ids.push_back(entry_id);
    return true;
}

bool SoxComplianceManager::verify_change(const std::string & change_id,
                                         const std::string & verifier) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = changes_.find(change_id);
        if (it == changes_.end() || it->second.state != ChangeState::IMPLEMENTED) {
            return false;
        }
        // SoD: implementer may not verify their own implementation.
        if (it->second.implemented_by == verifier) {
            return false;
        }
        it->second.state       = ChangeState::VERIFIED;
        it->second.verified_by = verifier;
        it->second.verified_at = std::chrono::system_clock::now();
    }

    std::map<std::string, std::string> meta;
    meta["change_id"] = change_id;
    meta["state"]     = change_state_to_string(ChangeState::VERIFIED);
    std::string entry_id = trail_.record_event(
        audit::AuditSeverity::INFO, audit::AuditCategory::COMPLIANCE_EVENT,
        verifier, "CHANGE_VERIFIED", change_id,
        "Change " + change_id + " verified by " + verifier, meta);

    std::lock_guard<std::mutex> lock(mutex_);
    changes_[change_id].audit_entry_ids.push_back(entry_id);
    return true;
}

const ChangeRecord * SoxComplianceManager::get_change(const std::string & change_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = changes_.find(change_id);
    return (it != changes_.end()) ? &it->second : nullptr;
}

std::vector<ChangeRecord> SoxComplianceManager::list_changes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChangeRecord> out;
    out.reserve(changes_.size());
    for (const auto & kv : changes_) {
        out.push_back(kv.second);
    }
    return out;
}

// ---- Automated Financial-Controls Tests ----

void SoxComplianceManager::register_control_test(const std::string & control_name,
                                                 ControlTestFn fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    control_tests_[control_name] = std::move(fn);
}

std::vector<std::string> SoxComplianceManager::list_control_tests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    names.reserve(control_tests_.size());
    for (const auto & kv : control_tests_) {
        names.push_back(kv.first);
    }
    return names;
}

ControlTestResult
SoxComplianceManager::run_control_test(const std::string & control_name,
                                       const std::vector<Transaction> & transactions,
                                       const std::string & actor) {
    ControlTestFn fn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = control_tests_.find(control_name);
        if (it == control_tests_.end()) {
            throw std::runtime_error("Unknown control test: " + control_name);
        }
        fn = it->second;
    }

    validation::ValidationReport report = fn(transactions);

    ControlTestResult result;
    result.control_name      = control_name;
    result.findings_total    = report.findings_total;
    result.findings_error    = report.findings_error;
    result.findings_critical = report.findings_critical;
    result.passed            = !report.has_errors();

    std::stringstream ss;
    ss << "Control test '" << control_name << "' "
       << (result.passed ? "PASSED" : "FAILED") << ": "
       << report.transactions_checked << " transactions checked, "
       << result.findings_total << " findings ("
       << result.findings_error << " errors, "
       << result.findings_critical << " critical)";
    result.summary = ss.str();

    std::map<std::string, std::string> meta;
    meta["control_name"]      = control_name;
    meta["passed"]            = result.passed ? "true" : "false";
    meta["findings_total"]    = std::to_string(result.findings_total);
    meta["findings_error"]    = std::to_string(result.findings_error);
    meta["findings_critical"] = std::to_string(result.findings_critical);

    result.audit_entry_id = trail_.record_event(
        result.passed ? audit::AuditSeverity::INFO : audit::AuditSeverity::CRITICAL,
        audit::AuditCategory::COMPLIANCE_EVENT,
        actor, "CONTROL_TEST", control_name, result.summary, meta);

    return result;
}

// ---- Tamper Evidence ----

bool SoxComplianceManager::verify_tamper_evidence() const {
    bool ok = trail_.verify_integrity();
    if (!ok) {
        // Const_cast-free: record_event is a non-const append, which is the
        // intended use of the trail even from a logically-const check.
        const_cast<audit::ImmutableAuditTrail &>(trail_).record_event(
            audit::AuditSeverity::ALERT, audit::AuditCategory::TAMPER_DETECTION,
            "sox-compliance-manager", "TAMPER_EVIDENCE_FAILURE",
            "audit-trail",
            "Audit trail hash-chain verification failed during SOX tamper-evidence check");
    }
    return ok;
}

// ============================================================================
// Task 6.2 - Basel III: BaselIIIEngine
// ============================================================================

BaselIIIEngine::BaselIIIEngine() : thresholds_() {}

BaselIIIEngine::BaselIIIEngine(const BaselThresholds & thresholds)
    : thresholds_(thresholds) {}

void BaselIIIEngine::set_thresholds(const BaselThresholds & thresholds) {
    std::lock_guard<std::mutex> lock(mutex_);
    thresholds_ = thresholds;
}

void BaselIIIEngine::add_exposure(const BaselExposure & exposure) {
    std::lock_guard<std::mutex> lock(mutex_);
    exposures_.push_back(exposure);
}

void BaselIIIEngine::clear_exposures() {
    std::lock_guard<std::mutex> lock(mutex_);
    exposures_.clear();
}

size_t BaselIIIEngine::exposure_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return exposures_.size();
}

void BaselIIIEngine::set_capital(const CapitalStructure & capital) {
    std::lock_guard<std::mutex> lock(mutex_);
    capital_ = capital;
}

double BaselIIIEngine::compute_rwa() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double rwa = 0.0;
    for (const auto & e : exposures_) {
        rwa += e.amount * e.risk_weight;
    }
    return rwa;
}

double BaselIIIEngine::compute_total_exposure() const {
    std::lock_guard<std::mutex> lock(mutex_);
    double total = 0.0;
    for (const auto & e : exposures_) {
        total += e.amount;
    }
    return total;
}

double BaselIIIEngine::cet1_ratio() const {
    double rwa = compute_rwa();
    if (rwa <= 0.0) {
        return 0.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return capital_.cet1_capital / rwa;
}

double BaselIIIEngine::tier1_ratio() const {
    double rwa = compute_rwa();
    if (rwa <= 0.0) {
        return 0.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return capital_.tier1_capital() / rwa;
}

double BaselIIIEngine::capital_adequacy_ratio() const {
    double rwa = compute_rwa();
    if (rwa <= 0.0) {
        return 0.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return capital_.total_capital() / rwa;
}

double BaselIIIEngine::leverage_ratio() const {
    double exposure = compute_total_exposure();
    if (exposure <= 0.0) {
        return 0.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return capital_.tier1_capital() / exposure;
}

CapitalReport BaselIIIEngine::assess() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return build_report(exposures_, "");
}

CapitalReport BaselIIIEngine::assess_stress(const StressScenario & scenario) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BaselExposure> stressed;
    stressed.reserve(exposures_.size());
    for (const auto & e : exposures_) {
        BaselExposure s = e;

        double haircut = 0.0;
        auto hit = scenario.exposure_haircuts.find(e.exposure_class);
        if (hit != scenario.exposure_haircuts.end()) {
            haircut = hit->second;
        }
        if (haircut < 0.0) haircut = 0.0;
        if (haircut > 1.0) haircut = 1.0;
        s.amount = e.amount * (1.0 - haircut);

        double multiplier = scenario.default_risk_weight_multiplier;
        auto mit = scenario.risk_weight_multipliers.find(e.exposure_class);
        if (mit != scenario.risk_weight_multipliers.end()) {
            multiplier = mit->second;
        }
        s.risk_weight = e.risk_weight * multiplier;

        stressed.push_back(s);
    }
    return build_report(stressed, scenario.name);
}

CapitalReport
BaselIIIEngine::build_report(const std::vector<BaselExposure> & book,
                             const std::string & scenario_name) const {
    // Caller holds mutex_.
    CapitalReport report;
    report.scenario_name = scenario_name;

    for (const auto & e : book) {
        report.risk_weighted_assets += e.amount * e.risk_weight;
        report.total_exposure       += e.amount;
    }

    if (report.risk_weighted_assets > 0.0) {
        report.cet1_ratio  = capital_.cet1_capital / report.risk_weighted_assets;
        report.tier1_ratio = capital_.tier1_capital() / report.risk_weighted_assets;
        report.total_car   = capital_.total_capital() / report.risk_weighted_assets;
    }
    if (report.total_exposure > 0.0) {
        report.leverage_ratio = capital_.tier1_capital() / report.total_exposure;
    }

    report.assessments.clear();

    {
        RatioAssessment a;
        a.name    = "CET1";
        a.ratio   = report.cet1_ratio;
        a.minimum = thresholds_.cet1_with_buffer();
        a.pass    = report.cet1_ratio >= a.minimum;
        a.note    = a.pass ? "Meets CET1 minimum incl. conservation buffer"
                           : "Below CET1 minimum incl. conservation buffer";
        report.assessments.push_back(a);
    }
    {
        RatioAssessment a;
        a.name    = "Tier1";
        a.ratio   = report.tier1_ratio;
        a.minimum = thresholds_.min_tier1;
        a.pass    = report.tier1_ratio >= a.minimum;
        a.note    = a.pass ? "Meets Tier 1 minimum" : "Below Tier 1 minimum";
        report.assessments.push_back(a);
    }
    {
        RatioAssessment a;
        a.name    = "CAR";
        a.ratio   = report.total_car;
        a.minimum = thresholds_.min_total_car;
        a.pass    = report.total_car >= a.minimum;
        a.note    = a.pass ? "Meets total capital adequacy minimum"
                           : "Below total capital adequacy minimum";
        report.assessments.push_back(a);
    }
    {
        RatioAssessment a;
        a.name    = "Leverage";
        a.ratio   = report.leverage_ratio;
        a.minimum = thresholds_.min_leverage;
        a.pass    = report.leverage_ratio >= a.minimum;
        a.note    = a.pass ? "Meets leverage ratio minimum"
                           : "Below leverage ratio minimum";
        report.assessments.push_back(a);
    }

    report.overall_pass = true;
    for (const auto & a : report.assessments) {
        if (!a.pass) {
            report.overall_pass = false;
            break;
        }
    }
    return report;
}

bool CapitalReport::ratio_pass(const std::string & name) const {
    for (const auto & a : assessments) {
        if (a.name == name) {
            return a.pass;
        }
    }
    return false;
}

std::string CapitalReport::to_string() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "Basel III Capital Report";
    if (!scenario_name.empty()) {
        ss << " [scenario: " << scenario_name << "]";
    }
    ss << "\n";
    ss << "  Risk-weighted assets : " << std::setprecision(2) << risk_weighted_assets << "\n";
    ss << "  Total exposure       : " << total_exposure << "\n";
    ss << std::setprecision(4);
    for (const auto & a : assessments) {
        ss << "  " << a.name << " ratio: " << (a.ratio * 100.0) << "%"
           << " (min " << (a.minimum * 100.0) << "%) -> "
           << (a.pass ? "PASS" : "FAIL") << "\n";
    }
    ss << "  Overall: " << (overall_pass ? "PASS" : "FAIL") << "\n";
    return ss.str();
}

// ============================================================================
// Task 6.3 - MiFID II: MifidReportingEngine
// ============================================================================

std::vector<std::string> MifidTransactionReport::missing_fields() const {
    std::vector<std::string> missing;
    if (transaction_id.empty()) missing.push_back("transaction_id");
    if (instrument.empty())     missing.push_back("instrument");
    if (isin.empty())           missing.push_back("isin");
    if (quantity <= 0.0)        missing.push_back("quantity");
    if (price <= 0.0)           missing.push_back("price");
    if (currency.empty())       missing.push_back("currency");
    if (buyer_id.empty())       missing.push_back("buyer_id");
    if (seller_id.empty())      missing.push_back("seller_id");
    if (venue.empty())          missing.push_back("venue");
    if (trading_capacity.empty() || trading_capacity == "UNKNOWN") {
        missing.push_back("trading_capacity");
    }
    // Field-content hygiene for the deterministic record format.
    const std::string forbidden = "|\n\r";
    auto dirty = [&](const std::string & s) {
        return s.find_first_of(forbidden) != std::string::npos;
    };
    if (dirty(transaction_id) || dirty(instrument) || dirty(isin) ||
        dirty(currency) || dirty(buyer_id) || dirty(seller_id) ||
        dirty(venue) || dirty(trading_capacity)) {
        missing.push_back("field-content");
    }
    return missing;
}

MifidReportingEngine::MifidReportingEngine()
    : best_exec_threshold_bps_(5.0), rejected_(0), batch_counter_(0) {}

MifidReportingEngine::MifidReportingEngine(double best_exec_threshold_bps)
    : best_exec_threshold_bps_(best_exec_threshold_bps), rejected_(0), batch_counter_(0) {}

void MifidReportingEngine::set_best_exec_threshold_bps(double threshold_bps) {
    std::lock_guard<std::mutex> lock(mutex_);
    best_exec_threshold_bps_ = threshold_bps;
}

std::string MifidReportingEngine::format_fixed(double value) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(8) << value;
    return ss.str();
}

std::string MifidReportingEngine::to_iso8601(std::chrono::system_clock::time_point tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        tp.time_since_epoch()) % 1000;
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
    return ss.str();
}

MifidTransactionReport
MifidReportingEngine::build_report(const Transaction & tx,
                                   const InstrumentMeta & instrument,
                                   double quantity,
                                   double price,
                                   const std::string & buyer_id,
                                   const std::string & seller_id,
                                   TradingCapacity capacity) const {
    MifidTransactionReport report;
    report.transaction_id   = tx.id;
    report.instrument       = instrument.instrument;
    report.isin             = instrument.isin;
    report.quantity         = quantity;
    report.price            = price;
    report.currency         = instrument.currency;
    report.buyer_id         = buyer_id;
    report.seller_id        = seller_id;
    report.venue            = instrument.venue;
    report.trading_capacity = trading_capacity_to_string(capacity);

    if (!tx.timestamp.empty()) {
        // Parse the transaction-engine timestamp ("%Y-%m-%d %H:%M:%S", local).
        std::tm tm = {};
        std::stringstream ss(tx.timestamp);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (!ss.fail()) {
            report.execution_timestamp =
                std::chrono::system_clock::from_time_t(std::mktime(&tm));
        }
    }
    report.execution_timestamp_iso8601 = to_iso8601(report.execution_timestamp);
    return report;
}

bool MifidReportingEngine::submit_report(const MifidTransactionReport & report) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!report.is_valid()) {
        rejected_++;
        return false;
    }
    pending_.push_back(report);
    return true;
}

size_t MifidReportingEngine::pending_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

uint64_t MifidReportingEngine::rejected_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rejected_;
}

std::string
MifidReportingEngine::serialize_report(const MifidTransactionReport & report) {
    std::stringstream ss;
    ss << report.transaction_id << "|"
       << report.execution_timestamp_iso8601 << "|"
       << report.instrument << "|"
       << report.isin << "|"
       << format_fixed(report.quantity) << "|"
       << format_fixed(report.price) << "|"
       << report.currency << "|"
       << report.buyer_id << "|"
       << report.seller_id << "|"
       << report.venue << "|"
       << report.trading_capacity;
    return ss.str();
}

std::string MifidReportingEngine::build_submission_batch() {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t seq = ++batch_counter_;
    std::string batch_id = "MIFID2-BATCH-" + std::to_string(seq);

    std::stringstream ss;
    ss << "#MIFID2-TSV-v1\n";
    ss << "#BATCH=" << batch_id << "\n";
    ss << "#COUNT=" << pending_.size() << "\n";
    for (const auto & report : pending_) {
        ss << serialize_report(report) << "\n";
    }
    pending_.clear();
    return ss.str();
}

BestExecutionRecord
MifidReportingEngine::record_execution(const std::string & transaction_id,
                                       const std::string & venue,
                                       double executed_price,
                                       double reference_price) {
    BestExecutionRecord rec;
    rec.transaction_id  = transaction_id;
    rec.venue           = venue;
    rec.executed_price  = executed_price;
    rec.reference_price = reference_price;
    if (reference_price > 0.0) {
        rec.deviation_bps =
            std::fabs(executed_price - reference_price) / reference_price * 10000.0;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    rec.breach = rec.deviation_bps > best_exec_threshold_bps_;
    best_exec_records_.push_back(rec);
    return rec;
}

std::vector<BestExecutionRecord>
MifidReportingEngine::get_best_execution_records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return best_exec_records_;
}

std::vector<BestExecutionRecord> MifidReportingEngine::get_breaches() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BestExecutionRecord> out;
    for (const auto & rec : best_exec_records_) {
        if (rec.breach) {
            out.push_back(rec);
        }
    }
    return out;
}

// ============================================================================
// Task 6.4 - GDPR: GdprDataProtection
// ============================================================================

namespace gdpr_categories {
    const char * const IDENTITY   = "identity";
    const char * const CONTACT    = "contact";
    const char * const FINANCIAL  = "financial";
    const char * const EMPLOYMENT = "employment";
    const char * const LOCATION   = "location";
    const char * const ONLINE_ID  = "online_id";
}

std::string DsarExport::to_string() const {
    std::stringstream ss;
    ss << "DSAR Export for subject: " << subject_id << "\n";
    ss << "  Categories held:";
    for (const auto & c : data_categories) {
        ss << " " << c;
    }
    ss << "\n";
    ss << "  Personal data fields: " << personal_data.size() << "\n";
    for (const auto & kv : personal_data) {
        ss << "    " << kv.first << " = " << kv.second << "\n";
    }
    ss << "  Consent events: " << consent_history.size() << "\n";
    for (const auto & c : consent_history) {
        ss << "    " << c.purpose << " -> " << (c.granted ? "GRANTED" : "WITHDRAWN")
           << "\n";
    }
    return ss.str();
}

GdprDataProtection::GdprDataProtection()
    : legal_deadline_(std::chrono::hours(24 * 30)), erasure_counter_(0) {}

GdprDataProtection::GdprDataProtection(std::chrono::hours legal_deadline)
    : legal_deadline_(legal_deadline), erasure_counter_(0) {}

void GdprDataProtection::set_legal_deadline(std::chrono::hours deadline) {
    std::lock_guard<std::mutex> lock(mutex_);
    legal_deadline_ = deadline;
}

void GdprDataProtection::register_subject(const std::string & subject_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subjects_[subject_id];  // Default-construct if absent.
}

void GdprDataProtection::add_data_category(const std::string & subject_id,
                                           const std::string & category) {
    std::lock_guard<std::mutex> lock(mutex_);
    subjects_[subject_id].categories.insert(category);
}

bool GdprDataProtection::has_subject(const std::string & subject_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subjects_.count(subject_id) > 0;
}

std::set<std::string>
GdprDataProtection::get_data_categories(const std::string & subject_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subjects_.find(subject_id);
    return (it != subjects_.end()) ? it->second.categories : std::set<std::string>();
}

void GdprDataProtection::set_personal_data(const std::string & subject_id,
                                           const std::string & field,
                                           const std::string & value) {
    std::lock_guard<std::mutex> lock(mutex_);
    subjects_[subject_id].personal_data[field] = value;
}

void GdprDataProtection::grant_consent(const std::string & subject_id,
                                       const std::string & purpose) {
    std::lock_guard<std::mutex> lock(mutex_);
    ConsentRecord rec;
    rec.subject_id = subject_id;
    rec.purpose    = purpose;
    rec.granted    = true;
    rec.timestamp  = std::chrono::system_clock::now();
    subjects_[subject_id].consent_history.push_back(rec);
}

void GdprDataProtection::withdraw_consent(const std::string & subject_id,
                                          const std::string & purpose) {
    std::lock_guard<std::mutex> lock(mutex_);
    ConsentRecord rec;
    rec.subject_id = subject_id;
    rec.purpose    = purpose;
    rec.granted    = false;
    rec.timestamp  = std::chrono::system_clock::now();
    subjects_[subject_id].consent_history.push_back(rec);
}

bool GdprDataProtection::has_consent(const std::string & subject_id,
                                     const std::string & purpose) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subjects_.find(subject_id);
    if (it == subjects_.end()) {
        return false;
    }
    // Last consent event for this purpose wins.
    for (auto rit = it->second.consent_history.rbegin();
         rit != it->second.consent_history.rend(); ++rit) {
        if (rit->purpose == purpose) {
            return rit->granted;
        }
    }
    return false;
}

std::vector<ConsentRecord>
GdprDataProtection::get_consent_history(const std::string & subject_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subjects_.find(subject_id);
    return (it != subjects_.end()) ? it->second.consent_history
                                   : std::vector<ConsentRecord>();
}

DsarExport GdprDataProtection::process_dsar(const std::string & subject_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    DsarExport out;
    out.subject_id   = subject_id;
    out.generated_at = std::chrono::system_clock::now();
    auto it = subjects_.find(subject_id);
    if (it != subjects_.end()) {
        out.data_categories  = it->second.categories;
        out.personal_data    = it->second.personal_data;
        out.consent_history  = it->second.consent_history;
    }
    return out;
}

std::string GdprDataProtection::pseudonymize(const std::string & data,
                                             const std::string & subject_id,
                                             const std::string & salt) {
    return "PSN:" + SHA256::hash(salt + "|" + subject_id + "|" + data);
}

std::string GdprDataProtection::anonymize(const std::string & data) {
    return "ANON:" + SHA256::hash(std::string("GGNUCASH_ANON|") + data);
}

std::string GdprDataProtection::request_erasure(const std::string & subject_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t seq = ++erasure_counter_;
    std::string request_id = "ERS-" + std::to_string(seq);

    ErasureRequest req;
    req.request_id     = request_id;
    req.subject_id     = subject_id;
    req.requested_at   = std::chrono::system_clock::now();
    req.legal_deadline = req.requested_at + legal_deadline_;
    req.status         = ErasureStatus::PENDING;
    erasure_requests_[request_id] = req;
    return request_id;
}

bool GdprDataProtection::complete_erasure(const std::string & request_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = erasure_requests_.find(request_id);
    if (it == erasure_requests_.end() || it->second.status != ErasureStatus::PENDING) {
        return false;
    }
    it->second.status       = ErasureStatus::COMPLETED;
    it->second.completed_at = std::chrono::system_clock::now();

    // Erase the subject's personal data and categories; pseudonymize consent
    // history subject reference for accountability retention.
    auto sit = subjects_.find(it->second.subject_id);
    if (sit != subjects_.end()) {
        sit->second.personal_data.clear();
        sit->second.categories.clear();
        for (auto & c : sit->second.consent_history) {
            c.subject_id = pseudonymize(c.subject_id, c.subject_id,
                                        "GGNUCASH_GDPR_ERASURE");
        }
    }
    return true;
}

bool GdprDataProtection::reject_erasure(const std::string & request_id,
                                        const std::string & reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = erasure_requests_.find(request_id);
    if (it == erasure_requests_.end() || it->second.status != ErasureStatus::PENDING) {
        return false;
    }
    it->second.status = ErasureStatus::REJECTED;
    it->second.notes  = reason;
    return true;
}

const ErasureRequest *
GdprDataProtection::get_erasure_request(const std::string & request_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = erasure_requests_.find(request_id);
    return (it != erasure_requests_.end()) ? &it->second : nullptr;
}

std::vector<ErasureRequest> GdprDataProtection::list_erasure_requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ErasureRequest> out;
    out.reserve(erasure_requests_.size());
    for (const auto & kv : erasure_requests_) {
        out.push_back(kv.second);
    }
    return out;
}

std::vector<ErasureRequest> GdprDataProtection::get_overdue_requests() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::system_clock::now();
    std::vector<ErasureRequest> out;
    for (const auto & kv : erasure_requests_) {
        if (kv.second.is_overdue(now)) {
            out.push_back(kv.second);
        }
    }
    return out;
}

double GdprDataProtection::days_until_deadline(const std::string & request_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = erasure_requests_.find(request_id);
    if (it == erasure_requests_.end()) {
        return 0.0;
    }
    auto now = std::chrono::system_clock::now();
    auto remaining = it->second.legal_deadline - now;
    return std::chrono::duration<double, std::ratio<86400>>(remaining).count();
}

} // namespace compliance
} // namespace ggnucash
