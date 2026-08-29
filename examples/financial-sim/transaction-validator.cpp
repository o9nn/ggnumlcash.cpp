#include "transaction-validator.h"
#include <algorithm>
#include <numeric>

namespace ggnucash {
namespace validation {

// ============================================================================
// ValidationReport formatting
// ============================================================================

std::string ValidationReport::to_string() const {
    std::stringstream ss;

    ss << "=== TRANSACTION INTEGRITY VALIDATION REPORT ===\n\n";

    auto gen_t = std::chrono::system_clock::to_time_t(generated_at);
    ss << "Generated: " << std::put_time(std::gmtime(&gen_t), "%Y-%m-%d %H:%M:%S UTC") << "\n";
    ss << "Duration: " << duration.count() << "ms\n";
    ss << "Transactions Checked: " << transactions_checked << "\n\n";

    ss << "--- Summary ---\n";
    ss << "Total Findings: " << findings_total << "\n";
    ss << "  PASS:     " << findings_pass << "\n";
    ss << "  INFO:     " << findings_info << "\n";
    ss << "  WARNING:  " << findings_warning << "\n";
    ss << "  ERROR:    " << findings_error << "\n";
    ss << "  CRITICAL: " << findings_critical << "\n\n";

    ss << "--- Trial Balance ---\n";
    ss << "Total Debits:  " << std::fixed << std::setprecision(2)
       << trial_balance_total_debits << "\n";
    ss << "Total Credits: " << std::fixed << std::setprecision(2)
       << trial_balance_total_credits << "\n";
    ss << "Balanced: " << (trial_balance_balanced ? "YES" : "NO") << "\n\n";

    if (!duplicates.empty()) {
        ss << "--- Potential Duplicates: " << duplicates.size() << " ---\n";
        for (const auto & dup : duplicates) {
            ss << "  " << dup.transaction_id_a << " <-> " << dup.transaction_id_b
               << " (similarity: " << std::fixed << std::setprecision(2)
               << dup.similarity_score * 100.0 << "%) - " << dup.match_reason << "\n";
        }
        ss << "\n";
    }

    if (!gaps.empty()) {
        ss << "--- Transaction Gaps: " << gaps.size() << " ---\n";
        for (const auto & gap : gaps) {
            ss << "  " << gap.description << " (" << gap.gap_duration.count() << " hours)\n";
        }
        ss << "\n";
    }

    if (!currency_conversions.empty()) {
        ss << "--- Currency Conversions: " << currency_conversions.size() << " ---\n";
        for (const auto & conv : currency_conversions) {
            ss << "  " << conv.transaction_id << ": "
               << std::fixed << std::setprecision(2) << conv.foreign_amount
               << " " << conv.from_currency << " -> " << conv.home_amount
               << " " << conv.to_currency
               << " (implied rate: " << std::setprecision(6) << conv.implied_rate
               << ", source: " << (conv.rate_source.empty() ? "NONE" : conv.rate_source)
               << ")\n";
        }
        ss << "\n";
    }

    if (!intercompany_balances.empty()) {
        ss << "--- Inter-company Balances: " << intercompany_balances.size() << " pair(s) ---\n";
        ss << "Transactions Checked: " << intercompany_transactions_checked
           << "  Unreconciled: " << intercompany_unreconciled << "\n";
        for (const auto & bal : intercompany_balances) {
            ss << "  " << bal.entity_a << " <-> " << bal.entity_b
               << ": net " << std::fixed << std::setprecision(2) << bal.net_position
               << " (" << bal.transaction_count << " transaction(s))\n";
        }
        ss << "\n";
    }

    if (has_errors()) {
        ss << "--- Error/Critical Findings ---\n";
        for (const auto & f : findings) {
            if (f.severity == ValidationSeverity::ERROR || f.severity == ValidationSeverity::CRITICAL) {
                ss << "  [" << severity_to_string(f.severity) << "] "
                   << validation_type_to_string(f.type) << ": " << f.description << "\n";
                if (!f.transaction_id.empty()) {
                    ss << "    Transaction: " << f.transaction_id << "\n";
                }
                if (!f.account_code.empty()) {
                    ss << "    Account: " << f.account_code << "\n";
                }
            }
        }
    }

    return ss.str();
}

std::string ValidationReport::to_json() const {
    std::stringstream ss;

    ss << "{\n";
    ss << "  \"validation_report\": {\n";
    ss << "    \"transactions_checked\": " << transactions_checked << ",\n";
    ss << "    \"duration_ms\": " << duration.count() << ",\n";
    ss << "    \"summary\": {\n";
    ss << "      \"total\": " << findings_total << ",\n";
    ss << "      \"pass\": " << findings_pass << ",\n";
    ss << "      \"info\": " << findings_info << ",\n";
    ss << "      \"warning\": " << findings_warning << ",\n";
    ss << "      \"error\": " << findings_error << ",\n";
    ss << "      \"critical\": " << findings_critical << "\n";
    ss << "    },\n";
    ss << "    \"trial_balance\": {\n";
    ss << "      \"total_debits\": " << std::fixed << std::setprecision(2)
       << trial_balance_total_debits << ",\n";
    ss << "      \"total_credits\": " << std::fixed << std::setprecision(2)
       << trial_balance_total_credits << ",\n";
    ss << "      \"balanced\": " << (trial_balance_balanced ? "true" : "false") << "\n";
    ss << "    },\n";
    ss << "    \"duplicates_found\": " << duplicates.size() << ",\n";
    ss << "    \"gaps_found\": " << gaps.size() << ",\n";
    ss << "    \"currency_conversions_found\": " << currency_conversions.size() << ",\n";
    ss << "    \"intercompany\": {\n";
    ss << "      \"entity_pairs\": " << intercompany_balances.size() << ",\n";
    ss << "      \"transactions_checked\": " << intercompany_transactions_checked << ",\n";
    ss << "      \"unreconciled\": " << intercompany_unreconciled << "\n";
    ss << "    },\n";
    ss << "    \"has_errors\": " << (has_errors() ? "true" : "false") << "\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

// ============================================================================
// TransactionValidator Implementation
// ============================================================================

TransactionValidator::TransactionValidator() : config_() {}

TransactionValidator::TransactionValidator(const ValidatorConfig & config)
    : config_(config) {}

void TransactionValidator::set_config(const ValidatorConfig & config) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    config_ = config;
}

void TransactionValidator::register_account(const std::string & code, const std::string & name) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    known_accounts_[code] = name;
    config_.valid_account_codes.insert(code);
}

void TransactionValidator::register_entity_account(const std::string & entity_id,
                                                   const std::string & account_code) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    entity_accounts_[account_code] = entity_id;
}

void TransactionValidator::register_entity_account_prefix(const std::string & entity_id,
                                                          const std::string & code_prefix) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    config_.entity_account_prefixes[entity_id] = code_prefix;
}

void TransactionValidator::register_exchange_rate(const std::string & from_currency,
                                                  const std::string & to_currency,
                                                  const std::string & date,
                                                  double rate,
                                                  const std::string & source) {
    std::lock_guard<std::mutex> lock(validator_mutex_);

    RegisteredExchangeRate ref;
    ref.from_currency = from_currency;
    ref.to_currency = to_currency;
    ref.date = date;
    ref.rate = rate;
    ref.source = source;
    reference_rates_.push_back(ref);
}

void TransactionValidator::register_account_currency(const std::string & account_code,
                                                     const std::string & currency) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    account_currencies_[account_code] = currency;
}

void TransactionValidator::register_transaction_currency(const std::string & transaction_id,
                                                         const std::string & currency) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    transaction_currencies_[transaction_id] = currency;
}

void TransactionValidator::register_rate_source(const std::string & transaction_id,
                                                const std::string & source) {
    std::lock_guard<std::mutex> lock(validator_mutex_);
    rate_sources_[transaction_id] = source;
}

// ---- Full Validation ----

ValidationReport TransactionValidator::validate_all(
    const std::vector<Transaction> & transactions
) const {
    auto start = std::chrono::steady_clock::now();

    ValidationReport report;
    report.report_id = "VR-" + std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());
    report.transactions_checked = transactions.size();

    // Run all validations
    auto double_entry_findings = validate_double_entry(transactions);
    auto hash_chain_findings = validate_hash_chain(transactions);
    auto account_findings = validate_account_existence(transactions);
    auto amount_findings = validate_amount_reasonableness(transactions);
    auto intercompany_findings = validate_intercompany(transactions);
    auto currency_findings = validate_currency_conversion(transactions);

    // Trial balance
    auto tb = generate_trial_balance(transactions);
    report.trial_balance = tb;
    auto tb_finding = validate_trial_balance(tb);

    // Calculate trial balance totals
    for (const auto & entry : tb) {
        report.trial_balance_total_debits += entry.debit_balance;
        report.trial_balance_total_credits += entry.credit_balance;
    }
    report.trial_balance_balanced =
        std::abs(report.trial_balance_total_debits - report.trial_balance_total_credits)
            < config_.balance_tolerance;

    // Duplicate detection
    report.duplicates = detect_duplicates(transactions);

    // Gap detection
    report.gaps = detect_transaction_gaps(transactions);

    // Currency conversion and inter-company detail results
    report.currency_conversions = extract_currency_conversions(transactions);
    report.intercompany_balances = get_intercompany_balances(transactions);
    report.intercompany_transactions_checked = 0;
    for (const auto & bal : report.intercompany_balances) {
        report.intercompany_transactions_checked += bal.transaction_count;
    }
    report.intercompany_unreconciled = 0;
    for (const auto & f : intercompany_findings) {
        if (f.severity != ValidationSeverity::PASS) {
            report.intercompany_unreconciled++;
        }
    }

    // Collect all findings
    auto collect = [&](const std::vector<ValidationFinding> & f) {
        report.findings.insert(report.findings.end(), f.begin(), f.end());
    };
    collect(double_entry_findings);
    collect(hash_chain_findings);
    collect(account_findings);
    collect(amount_findings);
    collect(intercompany_findings);
    collect(currency_findings);
    report.findings.push_back(tb_finding);

    // Add findings for duplicates
    for (const auto & dup : report.duplicates) {
        ValidationFinding f;
        f.finding_id = generate_finding_id();
        f.type = ValidationType::DUPLICATE_DETECTION;
        f.severity = ValidationSeverity::WARNING;
        f.description = "Potential duplicate: " + dup.transaction_id_a
                        + " and " + dup.transaction_id_b
                        + " (" + dup.match_reason + ")";
        f.transaction_id = dup.transaction_id_a;
        f.actual_value = dup.similarity_score;
        report.findings.push_back(f);
    }

    // Add findings for gaps
    for (const auto & gap : report.gaps) {
        ValidationFinding f;
        f.finding_id = generate_finding_id();
        f.type = ValidationType::TRANSACTION_GAP;
        f.severity = ValidationSeverity::WARNING;
        f.description = gap.description;
        f.account_code = gap.account_code;
        report.findings.push_back(f);
    }

    // Tally findings
    report.findings_total = report.findings.size();
    for (const auto & f : report.findings) {
        switch (f.severity) {
            case ValidationSeverity::PASS:     report.findings_pass++;     break;
            case ValidationSeverity::INFO:     report.findings_info++;     break;
            case ValidationSeverity::WARNING:  report.findings_warning++;  break;
            case ValidationSeverity::ERROR:    report.findings_error++;    break;
            case ValidationSeverity::CRITICAL: report.findings_critical++; break;
        }
    }

    auto end = std::chrono::steady_clock::now();
    report.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    return report;
}

// ---- Individual Validations ----

std::vector<ValidationFinding> TransactionValidator::validate_double_entry(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    for (const auto & tx : transactions) {
        double total_debits = 0.0;
        double total_credits = 0.0;

        for (const auto & entry : tx.entries) {
            total_debits += entry.debit_amount;
            total_credits += entry.credit_amount;
        }

        double diff = std::abs(total_debits - total_credits);

        ValidationFinding finding;
        finding.finding_id = generate_finding_id();
        finding.type = ValidationType::DOUBLE_ENTRY_BALANCE;
        finding.transaction_id = tx.id;
        finding.expected_value = 0.0;
        finding.actual_value = diff;

        if (diff < config_.balance_tolerance) {
            finding.severity = ValidationSeverity::PASS;
            finding.description = "Transaction " + tx.id + " is balanced";
        } else {
            finding.severity = ValidationSeverity::ERROR;
            finding.description = "Transaction " + tx.id + " is UNBALANCED: "
                                  "debits=" + std::to_string(total_debits)
                                  + " credits=" + std::to_string(total_credits)
                                  + " diff=" + std::to_string(diff);
        }

        findings.push_back(finding);
    }

    return findings;
}

std::vector<TrialBalanceEntry> TransactionValidator::generate_trial_balance(
    const std::vector<Transaction> & transactions
) const {
    // Accumulate debits and credits per account
    std::map<std::string, double> account_debits;
    std::map<std::string, double> account_credits;

    for (const auto & tx : transactions) {
        for (const auto & entry : tx.entries) {
            account_debits[entry.account_code] += entry.debit_amount;
            account_credits[entry.account_code] += entry.credit_amount;
        }
    }

    // Build trial balance entries
    std::vector<TrialBalanceEntry> trial_balance;
    std::set<std::string> all_accounts;
    for (const auto & kv : account_debits) all_accounts.insert(kv.first);
    for (const auto & kv : account_credits) all_accounts.insert(kv.first);

    for (const auto & code : all_accounts) {
        double debit = account_debits.count(code) ? account_debits[code] : 0.0;
        double credit = account_credits.count(code) ? account_credits[code] : 0.0;

        std::string name = "";
        auto it = known_accounts_.find(code);
        if (it != known_accounts_.end()) {
            name = it->second;
        }

        trial_balance.emplace_back(code, name, debit, credit);
    }

    return trial_balance;
}

ValidationFinding TransactionValidator::validate_trial_balance(
    const std::vector<TrialBalanceEntry> & trial_balance
) const {
    double total_debits = 0.0;
    double total_credits = 0.0;

    for (const auto & entry : trial_balance) {
        total_debits += entry.debit_balance;
        total_credits += entry.credit_balance;
    }

    double diff = std::abs(total_debits - total_credits);

    ValidationFinding finding;
    finding.finding_id = generate_finding_id();
    finding.type = ValidationType::TRIAL_BALANCE;
    finding.expected_value = 0.0;
    finding.actual_value = diff;

    if (diff < config_.balance_tolerance) {
        finding.severity = ValidationSeverity::PASS;
        finding.description = "Trial balance is balanced. Total debits: "
                              + std::to_string(total_debits) + " Total credits: "
                              + std::to_string(total_credits);
    } else {
        finding.severity = ValidationSeverity::CRITICAL;
        finding.description = "Trial balance is UNBALANCED. Total debits: "
                              + std::to_string(total_debits) + " Total credits: "
                              + std::to_string(total_credits) + " Difference: "
                              + std::to_string(diff);
    }

    return finding;
}

std::vector<TransactionGap> TransactionValidator::detect_transaction_gaps(
    const std::vector<Transaction> & transactions
) const {
    std::vector<TransactionGap> gaps;

    if (transactions.size() < 2) return gaps;

    // Parse and sort timestamps
    struct TimedTx {
        std::chrono::system_clock::time_point ts;
        std::string id;
    };

    std::vector<TimedTx> timed;
    timed.reserve(transactions.size());

    for (const auto & tx : transactions) {
        TimedTx t;
        t.ts = parse_timestamp(tx.timestamp);
        t.id = tx.id;
        timed.push_back(t);
    }

    std::sort(timed.begin(), timed.end(),
              [](const TimedTx & a, const TimedTx & b) { return a.ts < b.ts; });

    // Detect gaps
    uint64_t gap_counter = 0;
    for (size_t i = 1; i < timed.size(); i++) {
        auto duration = std::chrono::duration_cast<std::chrono::hours>(
            timed[i].ts - timed[i - 1].ts);

        if (duration >= config_.min_gap_duration) {
            TransactionGap gap;
            gap.gap_id = "GAP-" + std::to_string(++gap_counter);
            gap.gap_start = timed[i - 1].ts;
            gap.gap_end = timed[i].ts;
            gap.gap_duration = duration;

            auto start_t = std::chrono::system_clock::to_time_t(gap.gap_start);
            auto end_t = std::chrono::system_clock::to_time_t(gap.gap_end);
            std::stringstream ss;
            ss << "Gap of " << duration.count() << " hours between "
               << std::put_time(std::gmtime(&start_t), "%Y-%m-%d %H:%M")
               << " and "
               << std::put_time(std::gmtime(&end_t), "%Y-%m-%d %H:%M")
               << " (between " << timed[i - 1].id << " and " << timed[i].id << ")";
            gap.description = ss.str();

            gaps.push_back(gap);
        }
    }

    return gaps;
}

std::vector<DuplicateCandidate> TransactionValidator::detect_duplicates(
    const std::vector<Transaction> & transactions
) const {
    std::vector<DuplicateCandidate> duplicates;

    for (size_t i = 0; i < transactions.size(); i++) {
        for (size_t j = i + 1; j < transactions.size(); j++) {
            double similarity = calculate_transaction_similarity(
                transactions[i], transactions[j]);

            if (similarity >= config_.duplicate_similarity_threshold) {
                DuplicateCandidate dup;
                dup.transaction_id_a = transactions[i].id;
                dup.transaction_id_b = transactions[j].id;
                dup.similarity_score = similarity;

                // Determine match reason
                std::stringstream reason;
                bool same_amount = true;
                if (transactions[i].entries.size() == transactions[j].entries.size()) {
                    for (size_t k = 0; k < transactions[i].entries.size(); k++) {
                        if (std::abs(transactions[i].entries[k].debit_amount
                                     - transactions[j].entries[k].debit_amount) > 0.01 ||
                            std::abs(transactions[i].entries[k].credit_amount
                                     - transactions[j].entries[k].credit_amount) > 0.01) {
                            same_amount = false;
                            break;
                        }
                    }
                } else {
                    same_amount = false;
                }

                if (same_amount && transactions[i].entries.size() == transactions[j].entries.size()) {
                    reason << "Same amounts and account codes";
                } else {
                    reason << "High description/structure similarity ("
                           << std::fixed << std::setprecision(0)
                           << similarity * 100.0 << "%)";
                }
                dup.match_reason = reason.str();

                duplicates.push_back(dup);
            }
        }
    }

    return duplicates;
}

std::vector<ValidationFinding> TransactionValidator::validate_hash_chain(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    for (size_t i = 0; i < transactions.size(); i++) {
        const auto & tx = transactions[i];

        // Skip transactions without hashes (not all transactions have them)
        if (tx.hash.empty()) continue;

        // Verify hash
        std::string computed = SHA256::hash_transaction(tx);
        ValidationFinding finding;
        finding.finding_id = generate_finding_id();
        finding.type = ValidationType::HASH_CHAIN_INTEGRITY;
        finding.transaction_id = tx.id;

        if (computed == tx.hash) {
            finding.severity = ValidationSeverity::PASS;
            finding.description = "Hash verified for transaction " + tx.id;
        } else {
            finding.severity = ValidationSeverity::CRITICAL;
            finding.description = "Hash MISMATCH for transaction " + tx.id
                                  + ". Expected: " + tx.hash + " Computed: " + computed;
        }

        findings.push_back(finding);

        // Verify chain linkage
        if (i > 0 && !tx.prev_hash.empty() && !transactions[i - 1].hash.empty()) {
            if (tx.prev_hash != transactions[i - 1].hash) {
                ValidationFinding chain_finding;
                chain_finding.finding_id = generate_finding_id();
                chain_finding.type = ValidationType::HASH_CHAIN_INTEGRITY;
                chain_finding.severity = ValidationSeverity::CRITICAL;
                chain_finding.transaction_id = tx.id;
                chain_finding.description = "Chain break at transaction " + tx.id
                                            + ": prev_hash does not match previous transaction hash";
                findings.push_back(chain_finding);
            }
        }
    }

    return findings;
}

std::vector<ValidationFinding> TransactionValidator::validate_account_existence(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    if (config_.valid_account_codes.empty()) return findings;

    for (const auto & tx : transactions) {
        for (const auto & entry : tx.entries) {
            if (config_.valid_account_codes.find(entry.account_code)
                    == config_.valid_account_codes.end()) {
                ValidationFinding finding;
                finding.finding_id = generate_finding_id();
                finding.type = ValidationType::ACCOUNT_EXISTENCE;
                finding.severity = ValidationSeverity::ERROR;
                finding.transaction_id = tx.id;
                finding.account_code = entry.account_code;
                finding.description = "Unknown account code '" + entry.account_code
                                      + "' in transaction " + tx.id;
                findings.push_back(finding);
            }
        }
    }

    return findings;
}

std::vector<ValidationFinding> TransactionValidator::validate_amount_reasonableness(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    for (const auto & tx : transactions) {
        for (const auto & entry : tx.entries) {
            double max_amount = std::max(entry.debit_amount, entry.credit_amount);

            if (max_amount > config_.large_transaction_threshold) {
                ValidationFinding finding;
                finding.finding_id = generate_finding_id();
                finding.type = ValidationType::AMOUNT_REASONABLENESS;
                finding.severity = ValidationSeverity::WARNING;
                finding.transaction_id = tx.id;
                finding.account_code = entry.account_code;
                finding.actual_value = max_amount;
                finding.expected_value = config_.large_transaction_threshold;
                finding.description = "Large transaction amount " + std::to_string(max_amount)
                                      + " exceeds threshold " + std::to_string(config_.large_transaction_threshold)
                                      + " in transaction " + tx.id;
                findings.push_back(finding);
            }

            // Check for negative amounts
            if (entry.debit_amount < 0.0 || entry.credit_amount < 0.0) {
                ValidationFinding finding;
                finding.finding_id = generate_finding_id();
                finding.type = ValidationType::AMOUNT_REASONABLENESS;
                finding.severity = ValidationSeverity::ERROR;
                finding.transaction_id = tx.id;
                finding.account_code = entry.account_code;
                finding.description = "Negative amount in transaction " + tx.id
                                      + " account " + entry.account_code;
                findings.push_back(finding);
            }
        }
    }

    return findings;
}

// ============================================================================
// Inter-company Transaction Reconciliation
// ============================================================================

std::vector<ValidationFinding> TransactionValidator::validate_intercompany(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    if (entity_accounts_.empty() && config_.entity_account_prefixes.empty()) {
        return findings;
    }

    struct EntityLeg {
        std::string entity;
        double debits;
        double credits;
        double net; // debits - credits on this entity's accounts
    };

    for (const auto & tx : transactions) {
        // Group entry activity by entity (unmapped accounts are ignored)
        std::map<std::string, std::pair<double, double>> entity_dc; // entity -> (debits, credits)
        for (const auto & entry : tx.entries) {
            std::string entity = get_account_entity(entry.account_code);
            if (entity.empty()) continue;
            entity_dc[entity].first += entry.debit_amount;
            entity_dc[entity].second += entry.credit_amount;
        }

        if (entity_dc.size() < 2) continue; // not inter-company

        std::vector<EntityLeg> legs;
        for (const auto & kv : entity_dc) {
            legs.push_back({kv.first, kv.second.first, kv.second.second,
                            kv.second.first - kv.second.second});
        }

        auto emit = [&](ValidationSeverity severity, const std::string & desc,
                        const std::string & entity_a, const std::string & entity_b,
                        double expected, double actual) {
            ValidationFinding f;
            f.finding_id = generate_finding_id();
            f.type = ValidationType::INTERCOMPANY_RECONCILIATION;
            f.severity = severity;
            f.description = desc;
            f.transaction_id = tx.id;
            f.expected_value = expected;
            f.actual_value = actual;
            f.context["entity_a"] = entity_a;
            f.context["entity_b"] = entity_b;
            findings.push_back(f);
        };

        if (legs.size() == 2) {
            const EntityLeg & a = legs[0];
            const EntityLeg & b = legs[1];

            // A reconciled inter-company transaction mirrors the legs:
            // one side's debits match the other side's credits, so the
            // entity nets cancel exactly (a_net + b_net == 0).
            double net_gap = std::abs(a.net + b.net);
            double gross = std::max(std::max(a.debits, a.credits),
                                    std::max(b.debits, b.credits));

            if (net_gap < config_.balance_tolerance) {
                emit(ValidationSeverity::PASS,
                     "Inter-company transaction " + tx.id + " reconciled between entity '"
                         + a.entity + "' and entity '" + b.entity + "'",
                     a.entity, b.entity, 0.0, net_gap);
                continue;
            }

            // Missing counterparty leg: the weaker entity side offsets less
            // than half of the stronger entity side's flow.
            double a_flow = a.debits + a.credits;
            double b_flow = b.debits + b.credits;
            double strong_flow = std::max(a_flow, b_flow);
            double weak_flow = std::min(a_flow, b_flow);

            if (weak_flow < 0.5 * strong_flow) {
                const EntityLeg & dominant = (a_flow >= b_flow) ? a : b;
                const EntityLeg & other = (a_flow >= b_flow) ? b : a;
                emit(ValidationSeverity::ERROR,
                     "Inter-company transaction " + tx.id
                         + " is missing the offsetting leg on entity '" + other.entity
                         + "': entity '" + dominant.entity + "' records "
                         + std::to_string(dominant.debits) + " debits / "
                         + std::to_string(dominant.credits) + " credits"
                         + " without a matching counterparty entry",
                     dominant.entity, other.entity,
                     dominant.debits + dominant.credits,
                     other.credits + other.debits);
                continue;
            }

            emit(ValidationSeverity::ERROR,
                 "Inter-company amount mismatch in transaction " + tx.id
                     + ": entity '" + a.entity + "' net " + std::to_string(a.net)
                     + " vs entity '" + b.entity + "' net " + std::to_string(b.net)
                     + " (gap " + std::to_string(net_gap) + " on gross "
                     + std::to_string(gross) + ")",
                 a.entity, b.entity, a.net, b.net);
        } else {
            // More than two entities: mirror legs must net to zero overall
            double total_net = 0.0;
            for (const auto & leg : legs) total_net += leg.net;

            if (std::abs(total_net) < config_.balance_tolerance) {
                emit(ValidationSeverity::PASS,
                     "Multi-entity inter-company transaction " + tx.id
                         + " reconciled across " + std::to_string(legs.size()) + " entities",
                     legs.front().entity, legs.back().entity, 0.0, total_net);
            } else {
                emit(ValidationSeverity::ERROR,
                     "Multi-entity inter-company transaction " + tx.id
                         + " does not net to zero across "
                         + std::to_string(legs.size()) + " entities (net "
                         + std::to_string(total_net) + ")",
                     legs.front().entity, legs.back().entity, 0.0, total_net);
            }
        }
    }

    // Net inter-company balance per entity pair (should net to zero)
    for (const auto & bal : get_intercompany_balances(transactions)) {
        ValidationFinding f;
        f.finding_id = generate_finding_id();
        f.type = ValidationType::INTERCOMPANY_RECONCILIATION;
        f.expected_value = 0.0;
        f.actual_value = bal.net_position;
        f.context["entity_a"] = bal.entity_a;
        f.context["entity_b"] = bal.entity_b;
        f.context["transaction_count"] = std::to_string(bal.transaction_count);

        if (std::abs(bal.net_position) < config_.balance_tolerance) {
            f.severity = ValidationSeverity::INFO;
            f.description = "Net inter-company balance for entity pair " + bal.entity_a
                            + " / " + bal.entity_b + " is zero across "
                            + std::to_string(bal.transaction_count) + " transaction(s)";
        } else {
            f.severity = ValidationSeverity::WARNING;
            f.description = "Net inter-company balance for entity pair " + bal.entity_a
                            + " / " + bal.entity_b + " is " + std::to_string(bal.net_position)
                            + " (expected 0.00) across "
                            + std::to_string(bal.transaction_count) + " transaction(s)";
        }
        findings.push_back(f);
    }

    return findings;
}

std::vector<IntercompanyBalance> TransactionValidator::get_intercompany_balances(
    const std::vector<Transaction> & transactions
) const {
    std::vector<IntercompanyBalance> balances;

    if (entity_accounts_.empty() && config_.entity_account_prefixes.empty()) {
        return balances;
    }

    struct PairData {
        std::string entity_a;
        std::string entity_b;
        double net;         // Sum of a's (debits - credits) over pair transactions
        uint64_t count;
    };

    std::map<std::string, PairData> pair_data;

    for (const auto & tx : transactions) {
        std::map<std::string, double> entity_net;
        for (const auto & entry : tx.entries) {
            std::string entity = get_account_entity(entry.account_code);
            if (entity.empty()) continue;
            entity_net[entity] += entry.debit_amount - entry.credit_amount;
        }

        if (entity_net.size() < 2) continue;

        for (auto a = entity_net.begin(); a != entity_net.end(); ++a) {
            for (auto b = std::next(a); b != entity_net.end(); ++b) {
                std::string key = a->first + "|" + b->first;
                auto & pd = pair_data[key];
                if (pd.count == 0 && pd.entity_a.empty()) {
                    pd.entity_a = a->first;
                    pd.entity_b = b->first;
                }
                pd.net += a->second;
                pd.count++;
            }
        }
    }

    for (const auto & kv : pair_data) {
        IntercompanyBalance bal;
        bal.entity_a = kv.second.entity_a;
        bal.entity_b = kv.second.entity_b;
        bal.net_position = kv.second.net;
        bal.transaction_count = kv.second.count;
        balances.push_back(bal);
    }

    return balances;
}

// ============================================================================
// Currency Conversion Audit
// ============================================================================

std::vector<CurrencyConversionRecord> TransactionValidator::extract_currency_conversions(
    const std::vector<Transaction> & transactions
) const {
    std::vector<CurrencyConversionRecord> records;

    for (const auto & tx : transactions) {
        std::string home = get_transaction_currency(tx);

        // Sum activity per foreign currency across entries
        std::map<std::string, std::pair<double, std::string>> foreign; // ccy -> (net, account)
        for (const auto & entry : tx.entries) {
            auto it = account_currencies_.find(entry.account_code);
            if (it == account_currencies_.end()) continue;
            const std::string & ccy = it->second;
            if (ccy.empty() || ccy == home) continue;
            foreign[ccy].first += entry.debit_amount - entry.credit_amount;
            if (foreign[ccy].second.empty()) {
                foreign[ccy].second = entry.account_code;
            }
        }

        if (foreign.empty()) continue;

        // The home-currency side equals the residual on either side of the
        // transaction once the foreign leg's own amount is removed.
        double total_debits = 0.0;
        double total_credits = 0.0;
        for (const auto & entry : tx.entries) {
            total_debits += entry.debit_amount;
            total_credits += entry.credit_amount;
        }
        double home_residual = std::max(std::abs(total_debits - total_credits),
                                        config_.balance_tolerance);

        for (const auto & kv : foreign) {
            double foreign_amount = std::abs(kv.second.first);
            if (foreign_amount < config_.balance_tolerance) continue;

            CurrencyConversionRecord rec;
            rec.transaction_id = tx.id;
            rec.entry_account = kv.second.second;
            rec.from_currency = kv.first;
            rec.to_currency = home;
            rec.foreign_amount = foreign_amount;
            rec.home_amount = home_residual;
            rec.implied_rate = rec.home_amount / rec.foreign_amount;
            rec.rate_source = get_rate_source(tx.id);
            rec.date = tx.timestamp.size() >= 10 ? tx.timestamp.substr(0, 10) : tx.timestamp;
            records.push_back(rec);
        }
    }

    return records;
}

std::vector<ValidationFinding> TransactionValidator::validate_currency_conversion(
    const std::vector<Transaction> & transactions
) const {
    std::vector<ValidationFinding> findings;

    auto records = extract_currency_conversions(transactions);
    double tolerance = config_.currency_conversion_tolerance_pct / 100.0;

    for (const auto & rec : records) {
        const RegisteredExchangeRate * ref = find_reference_rate(
            rec.from_currency, rec.to_currency, rec.date);

        if (ref != nullptr) {
            double deviation = std::abs(rec.implied_rate - ref->rate) / ref->rate;

            ValidationFinding f;
            f.finding_id = generate_finding_id();
            f.type = ValidationType::CURRENCY_CONVERSION;
            f.transaction_id = rec.transaction_id;
            f.account_code = rec.entry_account;
            f.expected_value = ref->rate;
            f.actual_value = rec.implied_rate;
            f.context["from_currency"] = rec.from_currency;
            f.context["to_currency"] = rec.to_currency;
            f.context["rate_source"] = rec.rate_source.empty() ? ref->source : rec.rate_source;
            f.context["reference_source"] = ref->source;
            f.context["deviation_pct"] = std::to_string(deviation * 100.0);

            if (deviation <= tolerance) {
                f.severity = ValidationSeverity::PASS;
                f.description = "Currency conversion " + rec.from_currency + "->"
                                + rec.to_currency + " in transaction " + rec.transaction_id
                                + " within tolerance (implied "
                                + std::to_string(rec.implied_rate) + " vs reference "
                                + std::to_string(ref->rate) + " from " + ref->source + ")";
            } else {
                f.severity = ValidationSeverity::WARNING;
                f.description = "Currency conversion " + rec.from_currency + "->"
                                + rec.to_currency + " in transaction " + rec.transaction_id
                                + " deviates "
                                + std::to_string(deviation * 100.0) + "% from reference rate "
                                + std::to_string(ref->rate) + " (" + ref->source
                                + ", tolerance "
                                + std::to_string(config_.currency_conversion_tolerance_pct)
                                + "%)";
            }
            findings.push_back(f);
        } else if (has_rate_source(rec.transaction_id)) {
            ValidationFinding f;
            f.finding_id = generate_finding_id();
            f.type = ValidationType::CURRENCY_CONVERSION;
            f.severity = ValidationSeverity::INFO;
            f.transaction_id = rec.transaction_id;
            f.account_code = rec.entry_account;
            f.expected_value = 0.0;
            f.actual_value = rec.implied_rate;
            f.context["from_currency"] = rec.from_currency;
            f.context["to_currency"] = rec.to_currency;
            f.context["rate_source"] = rec.rate_source;
            f.description = "Currency conversion " + rec.from_currency + "->"
                            + rec.to_currency + " in transaction " + rec.transaction_id
                            + " has no registered reference rate (recorded source: "
                            + rec.rate_source + ")";
            findings.push_back(f);
        } else {
            ValidationFinding f;
            f.finding_id = generate_finding_id();
            f.type = ValidationType::CURRENCY_CONVERSION;
            f.severity = ValidationSeverity::WARNING;
            f.transaction_id = rec.transaction_id;
            f.account_code = rec.entry_account;
            f.expected_value = 0.0;
            f.actual_value = rec.implied_rate;
            f.context["from_currency"] = rec.from_currency;
            f.context["to_currency"] = rec.to_currency;
            f.context["rate_source"] = "";
            f.description = "Currency conversion " + rec.from_currency + "->"
                            + rec.to_currency + " in transaction " + rec.transaction_id
                            + " has no recorded rate source and no registered reference rate";
            findings.push_back(f);
        }
    }

    // Round-trip rate arbitrage: profitable forward/reverse conversion cycles
    for (size_t i = 0; i < records.size(); i++) {
        for (size_t j = i + 1; j < records.size(); j++) {
            const auto & a = records[i];
            const auto & b = records[j];

            if (a.from_currency != b.to_currency || a.to_currency != b.from_currency) {
                continue;
            }
            if (a.implied_rate <= 0.0 || b.implied_rate <= 0.0) continue;

            double product = a.implied_rate * b.implied_rate;
            double gain_pct = (product - 1.0) * 100.0;

            if (gain_pct > config_.currency_conversion_tolerance_pct) {
                ValidationFinding f;
                f.finding_id = generate_finding_id();
                f.type = ValidationType::CURRENCY_CONVERSION;
                f.severity = ValidationSeverity::WARNING;
                f.transaction_id = a.transaction_id;
                f.expected_value = 1.0;
                f.actual_value = product;
                f.context["from_currency"] = a.from_currency;
                f.context["to_currency"] = a.to_currency;
                f.context["forward_transaction"] = a.transaction_id;
                f.context["reverse_transaction"] = b.transaction_id;
                f.context["gain_pct"] = std::to_string(gain_pct);
                if (!a.rate_source.empty()) f.context["rate_source"] = a.rate_source;
                f.description = "Potential round-trip rate arbitrage between transactions "
                                + a.transaction_id + " (" + a.from_currency + "->"
                                + a.to_currency + " @ " + std::to_string(a.implied_rate)
                                + ") and " + b.transaction_id + " (" + b.from_currency
                                + "->" + b.to_currency + " @ " + std::to_string(b.implied_rate)
                                + "): implied gain " + std::to_string(gain_pct) + "%";
                findings.push_back(f);
            }
        }
    }

    return findings;
}

// ---- Utility ----

std::string TransactionValidator::get_account_entity(const std::string & account_code) const {
    // Explicit mapping takes precedence over prefix convention
    auto it = entity_accounts_.find(account_code);
    if (it != entity_accounts_.end()) {
        return it->second;
    }

    // Longest registered prefix wins
    std::string best_entity;
    size_t best_len = 0;
    for (const auto & kv : config_.entity_account_prefixes) {
        const std::string & prefix = kv.second;
        if (prefix.size() > best_len && account_code.compare(0, prefix.size(), prefix) == 0) {
            best_entity = kv.first;
            best_len = prefix.size();
        }
    }
    return best_entity;
}

double TransactionValidator::lookup_reference_rate(const std::string & from_currency,
                                                   const std::string & to_currency,
                                                   const std::string & date) const {
    const RegisteredExchangeRate * ref = find_reference_rate(from_currency, to_currency, date);
    return (ref != nullptr) ? ref->rate : 0.0;
}

std::string TransactionValidator::get_transaction_currency(const Transaction & tx) const {
    auto it = transaction_currencies_.find(tx.id);
    if (it != transaction_currencies_.end()) {
        return it->second;
    }

    // Derive the majority currency across entries with a known account currency
    std::map<std::string, int> counts;
    for (const auto & entry : tx.entries) {
        auto cc = account_currencies_.find(entry.account_code);
        if (cc != account_currencies_.end() && !cc->second.empty()) {
            counts[cc->second]++;
        }
    }

    std::string best;
    int best_count = 0;
    for (const auto & kv : counts) {
        if (kv.second > best_count) {
            best = kv.first;
            best_count = kv.second;
        }
    }
    return best;
}

bool TransactionValidator::has_rate_source(const std::string & transaction_id) const {
    auto it = rate_sources_.find(transaction_id);
    return it != rate_sources_.end() && !it->second.empty();
}

std::string TransactionValidator::get_rate_source(const std::string & transaction_id) const {
    auto it = rate_sources_.find(transaction_id);
    return (it != rate_sources_.end()) ? it->second : "";
}

const RegisteredExchangeRate * TransactionValidator::find_reference_rate(
    const std::string & from_currency,
    const std::string & to_currency,
    const std::string & date
) const {
    const RegisteredExchangeRate * undated = nullptr;
    const RegisteredExchangeRate * inverse_undated = nullptr;

    for (const auto & ref : reference_rates_) {
        if (ref.from_currency == from_currency && ref.to_currency == to_currency) {
            if (!ref.date.empty() && ref.date == date) return &ref;
            if (ref.date.empty() && undated == nullptr) undated = &ref;
        } else if (ref.from_currency == to_currency && ref.to_currency == from_currency) {
            if (!ref.date.empty() && ref.date == date) return &ref;
            if (ref.date.empty() && inverse_undated == nullptr) inverse_undated = &ref;
        }
    }

    if (undated != nullptr) return undated;
    return inverse_undated;
}

double TransactionValidator::calculate_transaction_similarity(
    const Transaction & a, const Transaction & b
) const {
    double score = 0.0;
    double weight_total = 0.0;

    // Description similarity (weight: 0.2)
    double desc_sim = levenshtein_similarity(a.description, b.description);
    score += 0.2 * desc_sim;
    weight_total += 0.2;

    // Entry count match (weight: 0.1)
    if (a.entries.size() == b.entries.size()) {
        score += 0.1;
    }
    weight_total += 0.1;

    // Account codes match (weight: 0.3)
    std::set<std::string> accounts_a, accounts_b;
    for (const auto & e : a.entries) accounts_a.insert(e.account_code);
    for (const auto & e : b.entries) accounts_b.insert(e.account_code);

    size_t common = 0;
    for (const auto & acc : accounts_a) {
        if (accounts_b.count(acc)) common++;
    }
    size_t union_size = accounts_a.size() + accounts_b.size() - common;
    if (union_size > 0) {
        score += 0.3 * (static_cast<double>(common) / union_size);
    }
    weight_total += 0.3;

    // Amount similarity (weight: 0.4)
    if (a.entries.size() == b.entries.size() && !a.entries.empty()) {
        double amount_match = 0.0;
        for (size_t i = 0; i < a.entries.size(); i++) {
            double debit_diff = std::abs(a.entries[i].debit_amount - b.entries[i].debit_amount);
            double credit_diff = std::abs(a.entries[i].credit_amount - b.entries[i].credit_amount);
            double max_debit = std::max(a.entries[i].debit_amount, b.entries[i].debit_amount);
            double max_credit = std::max(a.entries[i].credit_amount, b.entries[i].credit_amount);

            double debit_sim = (max_debit > 0) ? (1.0 - debit_diff / max_debit) : 1.0;
            double credit_sim = (max_credit > 0) ? (1.0 - credit_diff / max_credit) : 1.0;
            amount_match += (debit_sim + credit_sim) / 2.0;
        }
        amount_match /= a.entries.size();
        score += 0.4 * amount_match;
    }
    weight_total += 0.4;

    return (weight_total > 0) ? score / weight_total : 0.0;
}

std::chrono::system_clock::time_point TransactionValidator::parse_timestamp(
    const std::string & ts
) {
    if (ts.empty()) return std::chrono::system_clock::now();

    std::tm tm = {};
    std::istringstream ss(ts);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

    if (ss.fail()) {
        return std::chrono::system_clock::now();
    }

    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

// ---- Internal Helpers ----

std::string TransactionValidator::generate_finding_id() const {
    static std::atomic<uint64_t> counter(0);
    return "F-" + std::to_string(counter.fetch_add(1));
}

double TransactionValidator::levenshtein_similarity(
    const std::string & a, const std::string & b
) const {
    if (a.empty() && b.empty()) return 1.0;
    if (a.empty() || b.empty()) return 0.0;

    size_t len_a = a.size();
    size_t len_b = b.size();
    std::vector<std::vector<size_t>> dp(len_a + 1, std::vector<size_t>(len_b + 1));

    for (size_t i = 0; i <= len_a; i++) dp[i][0] = i;
    for (size_t j = 0; j <= len_b; j++) dp[0][j] = j;

    for (size_t i = 1; i <= len_a; i++) {
        for (size_t j = 1; j <= len_b; j++) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1,
                                 dp[i][j - 1] + 1,
                                 dp[i - 1][j - 1] + cost});
        }
    }

    size_t max_len = std::max(len_a, len_b);
    return 1.0 - static_cast<double>(dp[len_a][len_b]) / max_len;
}

} // namespace validation
} // namespace ggnucash
