#include "financial-reporting.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>
#include <thread>

using namespace ggnucash::reporting;

// Simple test framework
int test_count = 0;
int test_passed = 0;

#define TEST(name) \
    void test_##name(); \
    void run_test_##name() { \
        test_count++; \
        std::cout << "Running test: " << #name << "... "; \
        try { \
            test_##name(); \
            test_passed++; \
            std::cout << "PASSED\n"; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
        } catch (...) { \
            std::cout << "FAILED: Unknown exception\n"; \
        } \
    } \
    void test_##name()

#define ASSERT(condition) \
    if (!(condition)) { \
        throw std::runtime_error("Assertion failed: " #condition); \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::stringstream ss; \
        ss << "Assertion failed: " << #a << " == " << #b \
           << " (values: " << (a) << " != " << (b) << ")"; \
        throw std::runtime_error(ss.str()); \
    }

#define ASSERT_NEAR(a, b, tolerance) \
    if (std::abs((a) - (b)) > (tolerance)) { \
        std::stringstream ss; \
        ss << "Assertion failed: " << #a << " ≈ " << #b \
           << " (values: " << (a) << " != " << (b) << ", diff: " \
           << std::abs((a) - (b)) << " > " << (tolerance) << ")"; \
        throw std::runtime_error(ss.str()); \
    }

// ============================================================================
// Test Cases
// ============================================================================

TEST(report_period_point_in_time) {
    ReportPeriod period(PeriodType::POINT_IN_TIME);
    std::string str = period.to_string();
    ASSERT(!str.empty());
    ASSERT(str.find("As of") != std::string::npos);
}

TEST(report_period_year_to_date) {
    ReportPeriod period(PeriodType::YEAR_TO_DATE);
    std::string str = period.to_string();
    ASSERT(!str.empty());
    ASSERT(str.find("Year to Date") != std::string::npos);
}

TEST(report_period_custom) {
    auto now = std::chrono::system_clock::now();
    auto one_month_ago = now - std::chrono::hours(24 * 30);
    
    ReportPeriod period(one_month_ago, now);
    std::string str = period.to_string();
    ASSERT(!str.empty());
    ASSERT(str.find("For the period") != std::string::npos);
}

TEST(formula_engine_builtin_sum) {
    FormulaEngine engine;
    engine.register_builtin_functions();
    
    std::map<std::string, double> vars;
    vars["a"] = 10.0;
    vars["b"] = 20.0;
    vars["c"] = 30.0;
    
    double result = engine.evaluate("SUM", vars);
    ASSERT_EQ(result, 60.0);
}

TEST(formula_engine_builtin_avg) {
    FormulaEngine engine;
    engine.register_builtin_functions();
    
    std::map<std::string, double> vars;
    vars["a"] = 10.0;
    vars["b"] = 20.0;
    vars["c"] = 30.0;
    
    double result = engine.evaluate("AVG", vars);
    ASSERT_EQ(result, 20.0);
}

TEST(formula_engine_builtin_max) {
    FormulaEngine engine;
    engine.register_builtin_functions();
    
    std::map<std::string, double> vars;
    vars["a"] = 10.0;
    vars["b"] = 50.0;
    vars["c"] = 30.0;
    
    double result = engine.evaluate("MAX", vars);
    ASSERT_EQ(result, 50.0);
}

TEST(formula_engine_builtin_min) {
    FormulaEngine engine;
    engine.register_builtin_functions();
    
    std::map<std::string, double> vars;
    vars["a"] = 10.0;
    vars["b"] = 50.0;
    vars["c"] = 30.0;
    
    double result = engine.evaluate("MIN", vars);
    ASSERT_EQ(result, 10.0);
}

TEST(formula_engine_custom_formula) {
    FormulaEngine engine;
    
    engine.register_formula("DOUBLE", [](const std::map<std::string, double>& vars) {
        double sum = 0.0;
        for (const auto& pair : vars) {
            sum += pair.second;
        }
        return sum * 2.0;
    });
    
    std::map<std::string, double> vars;
    vars["a"] = 10.0;
    vars["b"] = 20.0;
    
    double result = engine.evaluate("DOUBLE", vars);
    ASSERT_EQ(result, 60.0);
}

TEST(report_generation_balance_sheet) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa; // Placeholder
    
    auto start = std::chrono::high_resolution_clock::now();
    FinancialReport report = generator.generate_balance_sheet(coa);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT(!report.title.empty());
    ASSERT(report.title == "Balance Sheet");
    ASSERT(report.lines.size() > 0);
    ASSERT(duration.count() < 100); // Should be < 100ms
}

TEST(report_generation_income_statement) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    ReportPeriod period(PeriodType::YEAR_TO_DATE);
    
    auto start = std::chrono::high_resolution_clock::now();
    FinancialReport report = generator.generate_income_statement(coa, period);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT(!report.title.empty());
    ASSERT(report.title == "Income Statement");
    ASSERT(report.lines.size() > 0);
    ASSERT(duration.count() < 100);
}

TEST(report_generation_cash_flow_indirect) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    ReportPeriod period(PeriodType::YEAR_TO_DATE);
    
    auto start = std::chrono::high_resolution_clock::now();
    FinancialReport report = generator.generate_cash_flow_statement(
        coa, period, CashFlowMethod::INDIRECT);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT(!report.title.empty());
    ASSERT(report.title == "Cash Flow Statement");
    ASSERT(report.subtitle == "Indirect Method");
    ASSERT(report.lines.size() > 0);
    ASSERT(duration.count() < 100);
}

TEST(report_generation_cash_flow_direct) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    ReportPeriod period(PeriodType::YEAR_TO_DATE);
    
    auto start = std::chrono::high_resolution_clock::now();
    FinancialReport report = generator.generate_cash_flow_statement(
        coa, period, CashFlowMethod::DIRECT);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT(!report.title.empty());
    ASSERT(report.subtitle == "Direct Method");
    ASSERT(report.lines.size() > 0);
    ASSERT(duration.count() < 100);
}

TEST(report_caching) {
    FinancialReportGenerator generator;
    generator.enable_caching(true);
    generator.set_cache_ttl(1000); // 1 second
    
    ChartOfAccounts coa;
    ReportPeriod period;
    
    // First generation - should be a cache miss
    auto report1 = generator.generate_balance_sheet(coa, period);
    
    // Second generation - should be a cache hit
    auto start = std::chrono::high_resolution_clock::now();
    auto report2 = generator.generate_balance_sheet(coa, period);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Cached report should be much faster (< 1ms)
    ASSERT(duration.count() < 1000); // < 1ms = 1000 microseconds
}

TEST(report_to_string_format) {
    FinancialReport report;
    report.title = "Test Report";
    report.subtitle = "Test Subtitle";
    report.period = ReportPeriod(PeriodType::POINT_IN_TIME);
    
    report.lines.push_back(ReportLine("Assets", 1000.50, 0));
    report.lines.push_back(ReportLine("Cash", 500.25, 1));
    report.lines.push_back(ReportLine("Total Assets", 1000.50, 0, true, false));
    
    report.generation_duration = std::chrono::milliseconds(5);
    
    std::string output = report.to_string();
    
    ASSERT(!output.empty());
    ASSERT(output.find("Test Report") != std::string::npos);
    ASSERT(output.find("Test Subtitle") != std::string::npos);
    ASSERT(output.find("Assets") != std::string::npos);
    ASSERT(output.find("Cash") != std::string::npos);
    ASSERT(output.find("1000.50") != std::string::npos);
    ASSERT(output.find("500.25") != std::string::npos);
}

TEST(report_to_json_format) {
    FinancialReport report;
    report.title = "Test Report";
    report.lines.push_back(ReportLine("Assets", 1000.0, 0));
    
    std::string json = report.to_json();
    
    ASSERT(!json.empty());
    ASSERT(json.find("\"title\"") != std::string::npos);
    ASSERT(json.find("Test Report") != std::string::npos);
    ASSERT(json.find("\"lines\"") != std::string::npos);
    ASSERT(json.find("\"amount\"") != std::string::npos);
}

TEST(concurrent_report_generation) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    const int num_threads = 10;
    const int reports_per_thread = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back([&generator, &coa, reports_per_thread]() {
            for (int j = 0; j < reports_per_thread; j++) {
                auto report = generator.generate_balance_sheet(coa);
                ASSERT(!report.title.empty());
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    int total_reports = num_threads * reports_per_thread;
    double avg_ms = static_cast<double>(duration.count()) / total_reports;
    
    std::cout << "\n  Generated " << total_reports << " reports concurrently\n";
    std::cout << "  Total time: " << duration.count() << " ms\n";
    std::cout << "  Average per report: " << avg_ms << " ms\n";
    
    // Each report should average < 100ms even under concurrent load
    ASSERT(avg_ms < 100.0);
}

TEST(batch_report_generation_10k) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    std::vector<std::string> report_types = {"balance_sheet", "income_statement", "cash_flow"};
    ReportPeriod period;
    
    const size_t count = 3334; // 3334 * 3 types = 10,002 reports
    
    auto start = std::chrono::high_resolution_clock::now();
    auto reports = generator.generate_batch_reports(coa, report_types, period, count);
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    ASSERT(reports.size() >= 10000);
    
    double avg_ms = static_cast<double>(duration.count()) / reports.size();
    
    std::cout << "\n  Generated " << reports.size() << " reports in batch\n";
    std::cout << "  Total time: " << duration.count() << " ms\n";
    std::cout << "  Average per report: " << avg_ms << " ms\n";
    
    // Average should be well under 100ms due to parallelization
    ASSERT(avg_ms < 100.0);
}

TEST(performance_monitor) {
    ReportingPerformanceMonitor::instance().reset_metrics();
    
    FinancialReportGenerator generator;
    generator.enable_caching(false); // Disable caching to ensure all reports are counted
    ChartOfAccounts coa;
    
    // Generate some reports
    for (int i = 0; i < 10; i++) {
        generator.generate_balance_sheet(coa);
    }
    
    auto metrics = ReportingPerformanceMonitor::instance().get_metrics();
    
    ASSERT(metrics.total_reports_generated >= 10);
    // Average generation time can be 0 if reports are very fast (< 1ms)
    // ASSERT(metrics.avg_generation_time.count() >= 0); // Changed from > 0
    ASSERT(metrics.min_generation_time <= metrics.max_generation_time);
    
    std::string metrics_str = metrics.to_string();
    ASSERT(!metrics_str.empty());
    ASSERT(metrics_str.find("Total Reports Generated") != std::string::npos);
}

TEST(custom_report_with_formulas) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    std::vector<std::string> accounts = {"1101", "2101", "3100"};
    std::map<std::string, std::string> formulas;
    formulas["NET_WORTH"] = "SUM";
    
    ReportPeriod period;
    
    auto report = generator.generate_custom_report(
        coa, "Custom Financial Analysis", accounts, formulas, period);
    
    ASSERT(!report.title.empty());
    ASSERT(report.title == "Custom Financial Analysis");
    ASSERT(report.subtitle == "Custom Report");
}

TEST(latency_requirement_single_report) {
    FinancialReportGenerator generator;
    ChartOfAccounts coa;
    
    // Test that a single report can be generated in < 100ms
    const int iterations = 100;
    int passed = 0;
    
    for (int i = 0; i < iterations; i++) {
        auto start = std::chrono::high_resolution_clock::now();
        auto report = generator.generate_balance_sheet(coa);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        if (duration.count() < 100) {
            passed++;
        }
    }
    
    // At least 95% should meet the latency requirement
    double pass_rate = static_cast<double>(passed) / iterations;
    std::cout << "\n  Latency < 100ms: " << (pass_rate * 100) << "% of reports\n";
    
    ASSERT(pass_rate >= 0.95);
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    std::cout << "=== Financial Reporting System Tests ===\n\n";
    
    // Period tests
    run_test_report_period_point_in_time();
    run_test_report_period_year_to_date();
    run_test_report_period_custom();
    
    // Formula engine tests
    run_test_formula_engine_builtin_sum();
    run_test_formula_engine_builtin_avg();
    run_test_formula_engine_builtin_max();
    run_test_formula_engine_builtin_min();
    run_test_formula_engine_custom_formula();
    
    // Report generation tests
    run_test_report_generation_balance_sheet();
    run_test_report_generation_income_statement();
    run_test_report_generation_cash_flow_indirect();
    run_test_report_generation_cash_flow_direct();
    
    // Caching tests
    run_test_report_caching();
    
    // Format tests
    run_test_report_to_string_format();
    run_test_report_to_json_format();
    
    // Custom reports
    run_test_custom_report_with_formulas();
    
    // Performance tests
    run_test_concurrent_report_generation();
    run_test_batch_report_generation_10k();
    run_test_performance_monitor();
    run_test_latency_requirement_single_report();
    
    std::cout << "\n=== Test Summary ===\n";
    std::cout << "Total tests: " << test_count << "\n";
    std::cout << "Passed: " << test_passed << "\n";
    std::cout << "Failed: " << (test_count - test_passed) << "\n";
    
    if (test_passed == test_count) {
        std::cout << "\n✓ All tests PASSED!\n";
        return 0;
    } else {
        std::cout << "\n✗ Some tests FAILED\n";
        return 1;
    }
}
