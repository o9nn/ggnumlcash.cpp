#include "financial-reporting.h"
#include <ctime>
#include <algorithm>
#include <thread>
#include <future>
#include <cmath>

// Note: We need to access the ChartOfAccounts from financial-sim.cpp
// For now, we'll define minimal interfaces here. In a production system,
// these would be properly separated into shared headers.

namespace ggnucash {
namespace reporting {

// ============================================================================
// ReportPeriod Implementation
// ============================================================================

void ReportPeriod::calculate_start_date() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_now = std::localtime(&time_t_now);
    
    std::tm tm_start = *tm_now;
    
    switch (type) {
        case PeriodType::YEAR_TO_DATE:
            tm_start.tm_mon = 0;  // January
            tm_start.tm_mday = 1; // First day
            break;
            
        case PeriodType::QUARTER_TO_DATE: {
            int quarter = tm_now->tm_mon / 3;
            tm_start.tm_mon = quarter * 3;
            tm_start.tm_mday = 1;
            break;
        }
            
        case PeriodType::MONTH_TO_DATE:
            tm_start.tm_mday = 1; // First day of current month
            break;
            
        case PeriodType::POINT_IN_TIME:
            start_date = end_date;
            return;
            
        case PeriodType::PERIOD:
        case PeriodType::CUSTOM:
            // Already set by caller
            return;
    }
    
    start_date = std::chrono::system_clock::from_time_t(std::mktime(&tm_start));
}

std::string ReportPeriod::to_string() const {
    auto end_time_t = std::chrono::system_clock::to_time_t(end_date);
    std::tm* tm = std::localtime(&end_time_t);
    
    std::stringstream ss;
    
    switch (type) {
        case PeriodType::POINT_IN_TIME:
            ss << "As of " << std::put_time(tm, "%B %d, %Y");
            break;
            
        case PeriodType::YEAR_TO_DATE:
            ss << "Year to Date (Through " << std::put_time(tm, "%B %d, %Y") << ")";
            break;
            
        case PeriodType::QUARTER_TO_DATE:
            ss << "Quarter to Date (Through " << std::put_time(tm, "%B %d, %Y") << ")";
            break;
            
        case PeriodType::MONTH_TO_DATE:
            ss << "Month to Date (Through " << std::put_time(tm, "%B %d, %Y") << ")";
            break;
            
        case PeriodType::PERIOD:
        case PeriodType::CUSTOM: {
            auto start_time_t = std::chrono::system_clock::to_time_t(start_date);
            std::tm* tm_start = std::localtime(&start_time_t);
            ss << "For the period " << std::put_time(tm_start, "%B %d, %Y") 
               << " to " << std::put_time(tm, "%B %d, %Y");
            break;
        }
    }
    
    return ss.str();
}

// ============================================================================
// FinancialReport Implementation
// ============================================================================

std::string FinancialReport::to_string() const {
    std::stringstream ss;
    
    // Header
    ss << "=" << std::string(78, '=') << "=\n";
    ss << " " << title << "\n";
    if (!subtitle.empty()) {
        ss << " " << subtitle << "\n";
    }
    ss << " " << period.to_string() << "\n";
    ss << "=" << std::string(78, '=') << "=\n\n";
    
    // Find max label width for alignment
    size_t max_label_width = 0;
    for (const auto& line : lines) {
        size_t label_width = line.label.length() + (line.indent_level * 2);
        max_label_width = std::max(max_label_width, label_width);
    }
    max_label_width = std::min(max_label_width, size_t(60)); // Cap at 60
    
    // Body
    for (const auto& line : lines) {
        // Indent
        for (int i = 0; i < line.indent_level; i++) {
            ss << "  ";
        }
        
        // Label
        std::string label = line.label;
        if (line.is_total || line.is_subtotal) {
            label = "  " + label;
        }
        ss << std::left << std::setw(max_label_width - line.indent_level * 2) << label;
        
        // Amount
        ss << std::right << std::setw(15);
        if (std::abs(line.amount) < 0.01 && !line.is_total && !line.is_subtotal) {
            ss << "-";
        } else {
            ss << std::fixed << std::setprecision(2) << line.amount;
        }
        
        ss << "\n";
        
        // Add separator line after subtotals and totals
        if (line.is_subtotal || line.is_total) {
            ss << std::string(max_label_width + 15, '-') << "\n";
        }
    }
    
    // Footer
    ss << "\n";
    ss << "Generated in " << generation_duration.count() << " ms\n";
    
    auto gen_time_t = std::chrono::system_clock::to_time_t(generation_time);
    ss << "Report generated at " << std::put_time(std::localtime(&gen_time_t), "%Y-%m-%d %H:%M:%S") << "\n";
    
    return ss.str();
}

std::string FinancialReport::to_json() const {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"title\": \"" << title << "\",\n";
    ss << "  \"subtitle\": \"" << subtitle << "\",\n";
    ss << "  \"period\": \"" << period.to_string() << "\",\n";
    ss << "  \"generation_duration_ms\": " << generation_duration.count() << ",\n";
    ss << "  \"lines\": [\n";
    
    for (size_t i = 0; i < lines.size(); i++) {
        const auto& line = lines[i];
        ss << "    {\n";
        ss << "      \"label\": \"" << line.label << "\",\n";
        ss << "      \"amount\": " << std::fixed << std::setprecision(2) << line.amount << ",\n";
        ss << "      \"indent_level\": " << line.indent_level << ",\n";
        ss << "      \"is_total\": " << (line.is_total ? "true" : "false") << ",\n";
        ss << "      \"is_subtotal\": " << (line.is_subtotal ? "true" : "false");
        if (!line.account_code.empty()) {
            ss << ",\n      \"account_code\": \"" << line.account_code << "\"";
        }
        ss << "\n    }";
        if (i < lines.size() - 1) ss << ",";
        ss << "\n";
    }
    
    ss << "  ]\n";
    ss << "}\n";
    
    return ss.str();
}

// ============================================================================
// FormulaEngine Implementation
// ============================================================================

void FormulaEngine::register_builtin_functions() {
    std::lock_guard<std::mutex> lock(formula_mutex_);
    
    // SUM function
    formulas_["SUM"] = [](const std::map<std::string, double>& vars) {
        double sum = 0.0;
        for (const auto& pair : vars) {
            sum += pair.second;
        }
        return sum;
    };
    
    // AVG function
    formulas_["AVG"] = [](const std::map<std::string, double>& vars) {
        if (vars.empty()) return 0.0;
        double sum = 0.0;
        for (const auto& pair : vars) {
            sum += pair.second;
        }
        return sum / vars.size();
    };
    
    // MAX function
    formulas_["MAX"] = [](const std::map<std::string, double>& vars) {
        if (vars.empty()) return 0.0;
        double max_val = vars.begin()->second;
        for (const auto& pair : vars) {
            max_val = std::max(max_val, pair.second);
        }
        return max_val;
    };
    
    // MIN function
    formulas_["MIN"] = [](const std::map<std::string, double>& vars) {
        if (vars.empty()) return 0.0;
        double min_val = vars.begin()->second;
        for (const auto& pair : vars) {
            min_val = std::min(min_val, pair.second);
        }
        return min_val;
    };
}

void FormulaEngine::register_formula(const std::string& name, FormulaFunction func) {
    std::lock_guard<std::mutex> lock(formula_mutex_);
    formulas_[name] = func;
}

double FormulaEngine::evaluate(const std::string& formula, 
                               const std::map<std::string, double>& variables) {
    std::lock_guard<std::mutex> lock(formula_mutex_);
    
    auto it = formulas_.find(formula);
    if (it != formulas_.end()) {
        return it->second(variables);
    }
    
    // Try parsing as expression
    return parse_expression(formula, variables);
}

double FormulaEngine::parse_expression(const std::string& expression,
                                      const std::map<std::string, double>& variables) {
    // Simple expression parser for basic arithmetic
    // Supports: variable names, +, -, *, /, (, )
    
    std::string expr = expression;
    // Remove whitespace
    expr.erase(std::remove_if(expr.begin(), expr.end(), ::isspace), expr.end());
    
    // Replace variable names with values
    for (const auto& pair : variables) {
        size_t pos = 0;
        while ((pos = expr.find(pair.first, pos)) != std::string::npos) {
            // Check if it's a whole word (not part of another word)
            bool is_whole_word = true;
            if (pos > 0 && std::isalnum(expr[pos - 1])) {
                is_whole_word = false;
            }
            if (pos + pair.first.length() < expr.length() && 
                std::isalnum(expr[pos + pair.first.length()])) {
                is_whole_word = false;
            }
            
            if (is_whole_word) {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(10) << pair.second;
                std::string replacement = ss.str();
                expr.replace(pos, pair.first.length(), replacement);
                pos += replacement.length(); // Advance by replacement length, not variable length
            } else {
                pos += pair.first.length();
            }
        }
    }
    
    // Simple evaluation (for basic +/- operations)
    // This is a simplified implementation. A full parser would be more complex.
    double result = 0.0;
    char op = '+';
    std::stringstream ss(expr);
    std::string token;
    
    while (ss >> token) {
        try {
            double value = std::stod(token);
            if (op == '+') result += value;
            else if (op == '-') result -= value;
            else if (op == '*') result *= value;
            else if (op == '/') {
                if (std::abs(value) < 1e-10) {
                    // Division by zero protection
                    return 0.0; // or throw exception, depending on requirements
                }
                result /= value;
            }
        } catch (...) {
            // Token might be an operator
            if (token.length() == 1 && (token[0] == '+' || token[0] == '-' || 
                                        token[0] == '*' || token[0] == '/')) {
                op = token[0];
            }
        }
    }
    
    return result;
}

// ============================================================================
// FinancialReportGenerator Implementation
// ============================================================================

// Note: These implementations are stubs since we don't have direct access
// to ChartOfAccounts class here. In the full integration, we'll need to
// either move ChartOfAccounts to a shared header or use forward declarations
// with proper linking.

std::string FinancialReportGenerator::generate_cache_key(
    const std::string& report_type, 
    const ReportPeriod& period) const {
    
    auto end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        period.end_date.time_since_epoch()).count();
    auto start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        period.start_date.time_since_epoch()).count();
    
    return report_type + "_" + std::to_string(start_ms) + "_" + std::to_string(end_ms);
}

bool FinancialReportGenerator::get_cached_report(const std::string& key, 
                                                 FinancialReport& report) {
    if (!enable_caching_) return false;
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = report_cache_.find(key);
    if (it == report_cache_.end()) {
        ReportingPerformanceMonitor::instance().record_cache_miss();
        return false;
    }
    
    // Check if cache is expired
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - it->second.cached_time);
    
    if (age.count() > cache_ttl_ms_) {
        report_cache_.erase(it);
        ReportingPerformanceMonitor::instance().record_cache_miss();
        return false;
    }
    
    report = it->second.report;
    ReportingPerformanceMonitor::instance().record_cache_hit();
    return true;
}

void FinancialReportGenerator::cache_report(const std::string& key, 
                                           const FinancialReport& report) {
    if (!enable_caching_) return;
    
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    CachedReport cached;
    cached.report = report;
    cached.cached_time = std::chrono::system_clock::now();
    
    report_cache_[key] = cached;
}

void FinancialReportGenerator::clear_expired_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto now = std::chrono::system_clock::now();
    
    for (auto it = report_cache_.begin(); it != report_cache_.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.cached_time);
        
        if (age.count() > cache_ttl_ms_) {
            it = report_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

// Placeholder implementations - will be integrated with actual ChartOfAccounts
// The ChartOfAccounts struct is defined in the header file

FinancialReport FinancialReportGenerator::generate_balance_sheet(
    const ChartOfAccounts& coa,
    const ReportPeriod& period,
    bool comparative) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string cache_key = generate_cache_key("balance_sheet", period);
    FinancialReport report;
    
    if (get_cached_report(cache_key, report)) {
        return report;
    }
    
    report.title = "Balance Sheet";
    report.subtitle = "Hardware Circuit State Report";
    report.period = period;
    
    // This is a placeholder - actual implementation will integrate with ChartOfAccounts
    report.lines.push_back(ReportLine("Assets", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Total Assets", 0.0, 0, true, false));
    report.lines.push_back(ReportLine("Liabilities", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Total Liabilities", 0.0, 0, true, false));
    report.lines.push_back(ReportLine("Equity", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Total Equity", 0.0, 0, true, false));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    report.generation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    report.generation_time = std::chrono::system_clock::now();
    
    cache_report(cache_key, report);
    ReportingPerformanceMonitor::instance().record_report_generation(report.generation_duration);
    
    [[maybe_unused]] const ChartOfAccounts& _coa = coa; // Placeholder for future integration
    [[maybe_unused]] bool _comparative = comparative;
    
    return report;
}

FinancialReport FinancialReportGenerator::generate_income_statement(
    const ChartOfAccounts& coa,
    const ReportPeriod& period) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string cache_key = generate_cache_key("income_statement", period);
    FinancialReport report;
    
    if (get_cached_report(cache_key, report)) {
        return report;
    }
    
    report.title = "Income Statement";
    report.subtitle = "Signal Flow Analysis Report";
    report.period = period;
    
    // Placeholder implementation
    report.lines.push_back(ReportLine("Revenue", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Total Revenue", 0.0, 0, true, false));
    report.lines.push_back(ReportLine("Expenses", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Total Expenses", 0.0, 0, true, false));
    report.lines.push_back(ReportLine("Net Income", 0.0, 0, true, false));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    report.generation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    report.generation_time = std::chrono::system_clock::now();
    
    cache_report(cache_key, report);
    ReportingPerformanceMonitor::instance().record_report_generation(report.generation_duration);
    
    (void)coa;
    
    return report;
}

FinancialReport FinancialReportGenerator::generate_cash_flow_statement(
    const ChartOfAccounts& coa,
    const ReportPeriod& period,
    CashFlowMethod method) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string cache_key = generate_cache_key(
        method == CashFlowMethod::INDIRECT ? "cash_flow_indirect" : "cash_flow_direct", 
        period);
    
    FinancialReport report;
    
    if (get_cached_report(cache_key, report)) {
        return report;
    }
    
    report.title = "Cash Flow Statement";
    report.subtitle = method == CashFlowMethod::INDIRECT ? 
        "Indirect Method" : "Direct Method";
    report.period = period;
    
    // Placeholder implementation
    if (method == CashFlowMethod::INDIRECT) {
        report.lines.push_back(ReportLine("Operating Activities", 0.0, 0, false, true));
        report.lines.push_back(ReportLine("Net Income", 0.0, 1));
        report.lines.push_back(ReportLine("Adjustments", 0.0, 1, false, true));
        report.lines.push_back(ReportLine("Net Cash from Operations", 0.0, 0, true, false));
    } else {
        report.lines.push_back(ReportLine("Operating Activities", 0.0, 0, false, true));
        report.lines.push_back(ReportLine("Cash Receipts", 0.0, 1));
        report.lines.push_back(ReportLine("Cash Payments", 0.0, 1));
        report.lines.push_back(ReportLine("Net Cash from Operations", 0.0, 0, true, false));
    }
    
    report.lines.push_back(ReportLine("Investing Activities", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Net Cash from Investing", 0.0, 0, true, false));
    
    report.lines.push_back(ReportLine("Financing Activities", 0.0, 0, false, true));
    report.lines.push_back(ReportLine("Net Cash from Financing", 0.0, 0, true, false));
    
    report.lines.push_back(ReportLine("Net Change in Cash", 0.0, 0, true, false));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    report.generation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    report.generation_time = std::chrono::system_clock::now();
    
    cache_report(cache_key, report);
    ReportingPerformanceMonitor::instance().record_report_generation(report.generation_duration);
    
    (void)coa;
    
    return report;
}

FinancialReport FinancialReportGenerator::generate_custom_report(
    const ChartOfAccounts& coa,
    const std::string& report_name,
    const std::vector<std::string>& account_codes,
    const std::map<std::string, std::string>& formulas,
    const ReportPeriod& period) {
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    FinancialReport report;
    report.title = report_name;
    report.subtitle = "Custom Report";
    report.period = period;
    
    // Placeholder - would evaluate formulas and build report
    for (const auto& code : account_codes) {
        report.lines.push_back(ReportLine("Account " + code, 0.0, 0));
    }
    
    // Evaluate formulas
    std::map<std::string, double> variables;
    for (const auto& pair : formulas) {
        double value = formula_engine_.evaluate(pair.second, variables);
        report.lines.push_back(ReportLine(pair.first, value, 0, true, false));
        variables[pair.first] = value;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    report.generation_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time);
    report.generation_time = std::chrono::system_clock::now();
    
    ReportingPerformanceMonitor::instance().record_report_generation(report.generation_duration);
    
    (void)coa;
    
    return report;
}

std::vector<FinancialReport> FinancialReportGenerator::generate_batch_reports(
    const ChartOfAccounts& coa,
    const std::vector<std::string>& report_types,
    const ReportPeriod& period,
    size_t count) {
    
    std::vector<FinancialReport> reports;
    reports.reserve(count * report_types.size());
    
    // Use parallel execution for batch generation
    std::vector<std::future<FinancialReport>> futures;
    
    for (size_t i = 0; i < count; i++) {
        for (const auto& report_type : report_types) {
            // Capture by value to avoid dangling references
            futures.push_back(std::async(std::launch::async, 
                [this, coa, report_type, period]() -> FinancialReport {
                    if (report_type == "balance_sheet") {
                        return generate_balance_sheet(coa, period);
                    } else if (report_type == "income_statement") {
                        return generate_income_statement(coa, period);
                    } else if (report_type == "cash_flow") {
                        return generate_cash_flow_statement(coa, period);
                    } else {
                        return FinancialReport();
                    }
                }
            ));
        }
    }
    
    // Collect results
    for (auto& future : futures) {
        reports.push_back(future.get());
    }
    
    return reports;
}

// ============================================================================
// ReportingMetrics Implementation
// ============================================================================

void ReportingMetrics::record_report(std::chrono::milliseconds duration) {
    total_reports_generated++;
    total_generation_time += duration;
    min_generation_time = std::min(min_generation_time, duration);
    max_generation_time = std::max(max_generation_time, duration);
    
    if (total_reports_generated > 0) {
        avg_generation_time = std::chrono::milliseconds(
            total_generation_time.count() / total_reports_generated);
    }
}

std::string ReportingMetrics::to_string() const {
    std::stringstream ss;
    ss << "=== Reporting Performance Metrics ===\n";
    ss << "Total Reports Generated: " << total_reports_generated << "\n";
    ss << "Total Generation Time: " << total_generation_time.count() << " ms\n";
    ss << "Average Generation Time: " << avg_generation_time.count() << " ms\n";
    ss << "Min Generation Time: " << min_generation_time.count() << " ms\n";
    ss << "Max Generation Time: " << max_generation_time.count() << " ms\n";
    ss << "Cache Hits: " << cache_hits << "\n";
    ss << "Cache Misses: " << cache_misses << "\n";
    
    if (cache_hits + cache_misses > 0) {
        double hit_rate = (double)cache_hits / (cache_hits + cache_misses) * 100.0;
        ss << "Cache Hit Rate: " << std::fixed << std::setprecision(1) << hit_rate << "%\n";
    }
    
    return ss.str();
}

// ============================================================================
// ReportingPerformanceMonitor Implementation
// ============================================================================

ReportingPerformanceMonitor& ReportingPerformanceMonitor::instance() {
    static ReportingPerformanceMonitor instance;
    return instance;
}

void ReportingPerformanceMonitor::record_report_generation(std::chrono::milliseconds duration) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_.record_report(duration);
}

void ReportingPerformanceMonitor::record_cache_hit() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_.record_cache_hit();
}

void ReportingPerformanceMonitor::record_cache_miss() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_.record_cache_miss();
}

ReportingMetrics ReportingPerformanceMonitor::get_metrics() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    return metrics_;
}

void ReportingPerformanceMonitor::reset_metrics() {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    metrics_ = ReportingMetrics();
}

} // namespace reporting
} // namespace ggnucash
