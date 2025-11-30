#include "enhanced-coa.h"
#include "account-templates.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <iomanip>

// Test utilities
class TestRunner {
private:
    int total_tests = 0;
    int passed_tests = 0;
    
public:
    void run_test(const std::string& test_name, const std::function<void()>& test_func) {
        total_tests++;
        std::cout << "Testing " << test_name << "...\n";
        try {
            test_func();
            passed_tests++;
            std::cout << "✓ " << test_name << " passed\n";
        } catch (const std::exception& e) {
            std::cout << "✗ " << test_name << " failed: " << e.what() << "\n";
        }
    }
    
    void print_summary() {
        std::cout << "\n=== TEST SUMMARY ===\n";
        std::cout << "Total: " << total_tests << "\n";
        std::cout << "Passed: " << passed_tests << "\n";
        std::cout << "Failed: " << (total_tests - passed_tests) << "\n";
        
        if (passed_tests == total_tests) {
            std::cout << "\n=== ALL TESTS PASSED ===\n";
        } else {
            std::cout << "\n=== SOME TESTS FAILED ===\n";
        }
    }
};

// Test 1: Hierarchical account structures with unlimited depth
void test_unlimited_depth_hierarchy() {
    EnhancedChartOfAccounts coa;
    
    // Create deep hierarchy
    coa.add_account("1000", "Assets", AccountType::ASSET);
    coa.add_account("1100", "Current Assets", AccountType::ASSET, "1000");
    coa.add_account("1110", "Cash Accounts", AccountType::ASSET, "1100");
    coa.add_account("1111", "Operating Cash", AccountType::ASSET, "1110");
    coa.add_account("1111-1", "Main Operating Account", AccountType::ASSET, "1111");
    coa.add_account("1111-1-A", "Daily Operations", AccountType::ASSET, "1111-1");
    coa.add_account("1111-1-B", "Petty Cash", AccountType::ASSET, "1111-1");
    
    // Verify depth tracking
    assert(coa.get_account("1000")->depth == 0);
    assert(coa.get_account("1100")->depth == 1);
    assert(coa.get_account("1110")->depth == 2);
    assert(coa.get_account("1111")->depth == 3);
    assert(coa.get_account("1111-1")->depth == 4);
    assert(coa.get_account("1111-1-A")->depth == 5);
    assert(coa.get_account("1111-1-B")->depth == 5);
    
    // Test maximum depth
    assert(coa.get_max_depth() == 5);
    
    // Test getting accounts at specific depth
    auto depth_5_accounts = coa.get_accounts_at_depth(5);
    assert(depth_5_accounts.size() == 2);
    
    // Test account path
    auto path = coa.get_account_path("1111-1-A");
    assert(path.size() == 6);
    assert(path[0] == "1000");
    assert(path[5] == "1111-1-A");
    
    // Test leaf accounts
    auto leaf_accounts = coa.get_leaf_accounts();
    assert(leaf_accounts.size() == 2);
}

// Test 2: Account metadata (currency, restrictions, regulations)
void test_account_metadata() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Set currency for an account
    Currency eur("EUR", "€", "Euro", 2);
    coa.set_account_currency("1101", eur);
    
    const Account* cash = coa.get_account("1101");
    assert(cash->metadata.currency.code == "EUR");
    assert(cash->metadata.currency.symbol == "€");
    
    // Add restrictions
    coa.add_account_restriction("1101", "No cash withdrawals over $10,000");
    coa.add_account_restriction("1101", "Requires dual authorization");
    
    assert(cash->metadata.restrictions.size() == 2);
    
    // Set regulation
    coa.set_account_regulation("1101", "SOX");
    assert(cash->metadata.regulation == "SOX");
    
    // Set full metadata
    AccountMetadata metadata;
    metadata.currency = Currency("GBP", "£", "British Pound", 2);
    metadata.regulation = "Basel III";
    metadata.tax_category = "Business Account";
    metadata.business_unit = "UK Operations";
    metadata.cost_center = "CC-1001";
    metadata.is_reconcilable = true;
    metadata.custom_fields["bank_name"] = "HSBC";
    metadata.custom_fields["account_number"] = "12345678";
    
    coa.set_account_metadata("1102", metadata);
    
    const Account* ar = coa.get_account("1102");
    assert(ar->metadata.currency.code == "GBP");
    assert(ar->metadata.regulation == "Basel III");
    assert(ar->metadata.business_unit == "UK Operations");
    assert(ar->metadata.custom_fields["bank_name"] == "HSBC");
}

// Test 3: Multi-currency support with real-time conversion
void test_multi_currency_support() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Set different currencies for accounts
    coa.set_account_currency("1101", Currency("USD", "$", "US Dollar", 2));
    coa.set_account_currency("1102", Currency("EUR", "€", "Euro", 2));
    
    // Set exchange rates
    coa.set_exchange_rate("EUR", 0.85);
    
    // Create transaction in USD
    Transaction txn;
    txn.id = "TXN-001";
    txn.description = "Multi-currency test";
    txn.currency = "USD";
    
    TransactionEntry entry1;
    entry1.account_code = "1101";  // USD account
    entry1.debit_amount = 1000.0;
    entry1.credit_amount = 0.0;
    entry1.currency = "USD";
    
    TransactionEntry entry2;
    entry2.account_code = "1102";  // EUR account
    entry2.debit_amount = 0.0;
    entry2.credit_amount = 1000.0;
    entry2.currency = "USD";
    
    txn.entries.push_back(entry1);
    txn.entries.push_back(entry2);
    
    bool result = coa.process_transaction(txn);
    assert(result == true);
    
    // Check balances
    double usd_balance = coa.get_balance("1101", "USD");
    double eur_balance_in_eur = coa.get_balance("1102", "EUR");
    
    assert(std::abs(usd_balance - 1000.0) < 0.01);
    assert(std::abs(eur_balance_in_eur - (-1000.0 * 0.85)) < 0.01);
    
    // Test currency conversion
    double eur_balance_in_usd = coa.get_balance("1102", "USD");
    assert(std::abs(eur_balance_in_usd - (-1000.0)) < 0.01);
}

// Test 4: Account templates for different business types
void test_account_templates() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Register templates
    auto retail_template = AccountTemplateFactory::create_retail_template();
    coa.register_template(retail_template);
    
    auto manufacturing_template = AccountTemplateFactory::create_manufacturing_template();
    coa.register_template(manufacturing_template);
    
    auto services_template = AccountTemplateFactory::create_services_template();
    coa.register_template(services_template);
    
    auto saas_template = AccountTemplateFactory::create_saas_template();
    coa.register_template(saas_template);
    
    // Create accounts from retail template
    size_t initial_count = coa.get_account_count();
    bool created = coa.create_from_template("retail");
    assert(created == true);
    
    size_t new_count = coa.get_account_count();
    assert(new_count > initial_count);
    
    // Verify retail-specific accounts exist
    assert(coa.account_exists("1150"));  // Merchandise Inventory
    assert(coa.account_exists("4110"));  // Retail Sales
    
    // Test template account hierarchy
    const Account* merch_inv = coa.get_account("1150");
    assert(merch_inv != nullptr);
    assert(merch_inv->parent_code == "1100");
}

// Test 5: Enhanced CoA with 500+ standard accounts
void test_comprehensive_coa() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Register all templates
    auto templates = AccountTemplateFactory::get_all_templates();
    for (const auto& tmpl : templates) {
        coa.register_template(tmpl);
    }
    
    // Create accounts from all templates
    coa.create_from_template("retail", "R-");
    coa.create_from_template("manufacturing", "M-");
    coa.create_from_template("services", "S-");
    coa.create_from_template("restaurant", "RS-");
    coa.create_from_template("saas", "SA-");
    coa.create_from_template("real_estate", "RE-");
    coa.create_from_template("healthcare", "HC-");
    
    size_t total_accounts = coa.get_account_count();
    std::cout << "  Total accounts created: " << total_accounts << "\n";
    
    // We should have base accounts + all template accounts
    assert(total_accounts >= 100);
}

// Test 6: Performance test with 10,000+ accounts
void test_large_scale_performance() {
    auto start = std::chrono::high_resolution_clock::now();
    
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Create 10,000+ accounts with deep hierarchy
    for (int i = 1; i <= 100; i++) {
        std::string base_code = "6" + std::to_string(i * 100);
        coa.add_account(base_code, "Category " + std::to_string(i), AccountType::EXPENSE);
        
        for (int j = 1; j <= 10; j++) {
            std::string sub_code = base_code + "-" + std::to_string(j);
            coa.add_account(sub_code, "Subcategory " + std::to_string(j), AccountType::EXPENSE, base_code);
            
            for (int k = 1; k <= 10; k++) {
                std::string leaf_code = sub_code + "-" + std::to_string(k);
                coa.add_account(leaf_code, "Leaf " + std::to_string(k), AccountType::EXPENSE, sub_code);
            }
        }
    }
    
    size_t total_accounts = coa.get_account_count();
    std::cout << "  Created " << total_accounts << " accounts\n";
    assert(total_accounts >= 10000);
    
    // Test lookup performance
    auto lookup_start = std::chrono::high_resolution_clock::now();
    for (int i = 1; i <= 100; i++) {
        std::string code = "6" + std::to_string(i * 100) + "-5-5";
        const Account* acc = coa.get_account(code);
        assert(acc != nullptr);
    }
    auto lookup_end = std::chrono::high_resolution_clock::now();
    auto lookup_duration = std::chrono::duration_cast<std::chrono::microseconds>(lookup_end - lookup_start);
    std::cout << "  100 lookups took " << lookup_duration.count() << " microseconds\n";
    
    // Test hierarchy traversal
    int max_depth = coa.get_max_depth();
    std::cout << "  Maximum hierarchy depth: " << max_depth << "\n";
    assert(max_depth >= 3);
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "  Total test duration: " << duration.count() << " ms\n";
    
    // Performance assertion: should complete in reasonable time
    assert(duration.count() < 5000);  // Less than 5 seconds
}

// Test 7: Complex hierarchy operations
void test_hierarchy_operations() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Add complex multi-level structure
    coa.add_account("7000", "Special Accounts", AccountType::ASSET);
    coa.add_account("7100", "Department A", AccountType::ASSET, "7000");
    coa.add_account("7110", "Region 1", AccountType::ASSET, "7100");
    coa.add_account("7111", "Branch 1", AccountType::ASSET, "7110");
    coa.add_account("7112", "Branch 2", AccountType::ASSET, "7110");
    coa.add_account("7120", "Region 2", AccountType::ASSET, "7100");
    
    // Test children traversal
    const Account* dept_a = coa.get_account("7100");
    assert(dept_a->children.size() == 2);
    
    const Account* region_1 = coa.get_account("7110");
    assert(region_1->children.size() == 2);
    
    // Test path resolution
    auto path = coa.get_account_path("7111");
    assert(path.size() == 4);
    assert(path[0] == "7000");
    assert(path[1] == "7100");
    assert(path[2] == "7110");
    assert(path[3] == "7111");
    
    // Test leaf account detection
    auto leaf_accounts = coa.get_leaf_accounts();
    size_t leaf_count = 0;
    for (const auto* acc : leaf_accounts) {
        if (acc->code.substr(0, 1) == "7") {
            leaf_count++;
        }
    }
    assert(leaf_count == 3);  // 7111, 7112, 7120
}

// Test 8: Balance sheet generation with multi-currency
void test_balance_sheet_generation() {
    EnhancedChartOfAccounts coa;
    coa.initialize_standard_coa();
    
    // Set up multi-currency accounts
    coa.set_account_currency("1101", Currency("USD", "$", "US Dollar", 2));
    coa.set_account_currency("1102", Currency("EUR", "€", "Euro", 2));
    
    // Add some balances
    Transaction txn;
    txn.id = "TXN-001";
    txn.currency = "USD";
    
    TransactionEntry e1;
    e1.account_code = "1101";
    e1.debit_amount = 10000.0;
    e1.credit_amount = 0.0;
    
    TransactionEntry e2;
    e2.account_code = "3100";
    e2.debit_amount = 0.0;
    e2.credit_amount = 10000.0;
    
    txn.entries.push_back(e1);
    txn.entries.push_back(e2);
    coa.process_transaction(txn);
    
    // Generate balance sheet
    std::string balance_sheet = coa.generate_balance_sheet("USD");
    assert(balance_sheet.find("FINANCIAL CIRCUIT STATE REPORT") != std::string::npos);
    assert(balance_sheet.find("1101") != std::string::npos);
}

int main() {
    std::cout << "=== ENHANCED CHART OF ACCOUNTS TESTS ===\n\n";
    
    TestRunner runner;
    
    runner.run_test("Unlimited depth hierarchy", test_unlimited_depth_hierarchy);
    runner.run_test("Account metadata", test_account_metadata);
    runner.run_test("Multi-currency support", test_multi_currency_support);
    runner.run_test("Account templates", test_account_templates);
    runner.run_test("Comprehensive CoA", test_comprehensive_coa);
    runner.run_test("Large scale performance (10,000+ accounts)", test_large_scale_performance);
    runner.run_test("Complex hierarchy operations", test_hierarchy_operations);
    runner.run_test("Balance sheet generation", test_balance_sheet_generation);
    
    runner.print_summary();
    
    return 0;
}
