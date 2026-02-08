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
    config_ = config;
}

void TransactionValidator::register_account(const std::string & code, const std::string & name) {
    known_accounts_[code] = name;
    config_.valid_account_codes.insert(code);
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

    // Collect all findings
    auto collect = [&](const std::vector<ValidationFinding> & f) {
        report.findings.insert(report.findings.end(), f.begin(), f.end());
    };
    collect(double_entry_findings);
    collect(hash_chain_findings);
    collect(account_findings);
    collect(amount_findings);
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

// ---- Utility ----

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
