#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <memory>
#include <functional>
#include <mutex>

namespace ggnucash {
namespace reporting {

// Forward declarations
struct Account;
struct Transaction;
struct TransactionEntry;

// Placeholder ChartOfAccounts for standalone reporting module
struct ChartOfAccounts {
    ChartOfAccounts() = default;
};

// ============================================================================
// Report Period Types
// ============================================================================

enum class PeriodType {
    POINT_IN_TIME,      // Balance sheet - snapshot at specific time
    PERIOD,             // Income statement - period from start to end
    YEAR_TO_DATE,       // From beginning of year to now
    QUARTER_TO_DATE,    // From beginning of quarter to now
    MONTH_TO_DATE,      // From beginning of month to now
    CUSTOM              // Custom date range
};

struct ReportPeriod {
    PeriodType type;
    std::chrono::system_clock::time_point start_date;
    std::chrono::system_clock::time_point end_date;
    
    ReportPeriod() : type(PeriodType::POINT_IN_TIME) {
        end_date = std::chrono::system_clock::now();
        start_date = end_date;
    }
    
    ReportPeriod(PeriodType t) : type(t) {
        end_date = std::chrono::system_clock::now();
        calculate_start_date();
    }
    
    ReportPeriod(std::chrono::system_clock::time_point start, 
                 std::chrono::system_clock::time_point end)
        : type(PeriodType::CUSTOM), start_date(start), end_date(end) {}
    
    void calculate_start_date();
    std::string to_string() const;
};

// ============================================================================
// Cash Flow Statement Methods
// ============================================================================

enum class CashFlowMethod {
    INDIRECT,   // Start with net income, adjust for non-cash items
    DIRECT      // Show actual cash receipts and payments
};

// ============================================================================
// Report Data Structures
// ============================================================================

struct ReportLine {
    std::string label;
    double amount;
    int indent_level;
    bool is_total;
    bool is_subtotal;
    std::string account_code;
    
    ReportLine(const std::string& lbl, double amt, int indent = 0, 
               bool total = false, bool subtotal = false, const std::string& code = "")
        : label(lbl), amount(amt), indent_level(indent), 
          is_total(total), is_subtotal(subtotal), account_code(code) {}
};

struct FinancialReport {
    std::string title;
    std::string subtitle;
    ReportPeriod period;
    std::vector<ReportLine> lines;
    std::chrono::system_clock::time_point generation_time;
    std::chrono::milliseconds generation_duration;
    
    FinancialReport() {
        generation_time = std::chrono::system_clock::now();
        generation_duration = std::chrono::milliseconds(0);
    }
    
    std::string to_string() const;
    std::string to_json() const;
};

// ============================================================================
// Formula Engine for Custom Reports
// ============================================================================

class FormulaEngine {
public:
    using FormulaFunction = std::function<double(const std::map<std::string, double>&)>;
    
    FormulaEngine() {}
    
    // Register built-in functions
    void register_builtin_functions();
    
    // Register custom formula
    void register_formula(const std::string& name, FormulaFunction func);
    
    // Evaluate formula
    double evaluate(const std::string& formula, const std::map<std::string, double>& variables);
    
    // Parse simple expressions (e.g., "REVENUE - EXPENSES", "ASSETS - LIABILITIES")
    double parse_expression(const std::string& expression, 
                           const std::map<std::string, double>& variables);
    
private:
    std::map<std::string, FormulaFunction> formulas_;
    std::mutex formula_mutex_;
};

// ============================================================================
// Financial Report Generator
// ============================================================================

class FinancialReportGenerator {
public:
    FinancialReportGenerator() {
        formula_engine_.register_builtin_functions();
        enable_caching_ = true;
        cache_ttl_ms_ = 1000; // 1 second default cache TTL
    }
    
    // Balance Sheet Generation
    FinancialReport generate_balance_sheet(
        const ChartOfAccounts& coa,
        const ReportPeriod& period = ReportPeriod(PeriodType::POINT_IN_TIME),
        bool comparative = false);
    
    // Income Statement Generation
    FinancialReport generate_income_statement(
        const ChartOfAccounts& coa,
        const ReportPeriod& period);
    
    // Cash Flow Statement Generation
    FinancialReport generate_cash_flow_statement(
        const ChartOfAccounts& coa,
        const ReportPeriod& period,
        CashFlowMethod method = CashFlowMethod::INDIRECT);
    
    // Custom Report Generation
    FinancialReport generate_custom_report(
        const ChartOfAccounts& coa,
        const std::string& report_name,
        const std::vector<std::string>& account_codes,
        const std::map<std::string, std::string>& formulas,
        const ReportPeriod& period);
    
    // Batch Report Generation (for concurrent testing)
    std::vector<FinancialReport> generate_batch_reports(
        const ChartOfAccounts& coa,
        const std::vector<std::string>& report_types,
        const ReportPeriod& period,
        size_t count = 1);
    
    // Performance configuration
    void enable_caching(bool enable) { enable_caching_ = enable; }
    void set_cache_ttl(int milliseconds) { cache_ttl_ms_ = milliseconds; }
    
    // Formula engine access
    FormulaEngine& get_formula_engine() { return formula_engine_; }
    
private:
    FormulaEngine formula_engine_;
    bool enable_caching_;
    int cache_ttl_ms_;
    
    // Cache structures
    struct CachedReport {
        FinancialReport report;
        std::chrono::system_clock::time_point cached_time;
    };
    std::map<std::string, CachedReport> report_cache_;
    std::mutex cache_mutex_;
    
    // Helper methods
    std::string generate_cache_key(const std::string& report_type, 
                                   const ReportPeriod& period) const;
    bool get_cached_report(const std::string& key, FinancialReport& report);
    void cache_report(const std::string& key, const FinancialReport& report);
    void clear_expired_cache();
    
    // Report generation helpers
    void add_account_hierarchy(
        std::vector<ReportLine>& lines,
        const ChartOfAccounts& coa,
        const std::string& root_account,
        const ReportPeriod& period,
        int indent_level = 0);
    
    double calculate_account_balance(
        const ChartOfAccounts& coa,
        const std::string& account_code,
        const ReportPeriod& period);
    
    double calculate_subtree_total(
        const ChartOfAccounts& coa,
        const std::string& root_account,
        const ReportPeriod& period);
};

// ============================================================================
// Performance Monitoring
// ============================================================================

struct ReportingMetrics {
    size_t total_reports_generated;
    std::chrono::milliseconds total_generation_time;
    std::chrono::milliseconds min_generation_time;
    std::chrono::milliseconds max_generation_time;
    std::chrono::milliseconds avg_generation_time;
    size_t cache_hits;
    size_t cache_misses;
    
    ReportingMetrics() : total_reports_generated(0), 
                        total_generation_time(0), 
                        min_generation_time(std::chrono::milliseconds::max()),
                        max_generation_time(0),
                        avg_generation_time(0),
                        cache_hits(0),
                        cache_misses(0) {}
    
    void record_report(std::chrono::milliseconds duration);
    void record_cache_hit() { cache_hits++; }
    void record_cache_miss() { cache_misses++; }
    std::string to_string() const;
};

class ReportingPerformanceMonitor {
public:
    static ReportingPerformanceMonitor& instance();
    
    void record_report_generation(std::chrono::milliseconds duration);
    void record_cache_hit();
    void record_cache_miss();
    
    ReportingMetrics get_metrics() const;
    void reset_metrics();
    
private:
    ReportingPerformanceMonitor() {}
    ReportingMetrics metrics_;
    mutable std::mutex metrics_mutex_;
};

} // namespace reporting
} // namespace ggnucash
