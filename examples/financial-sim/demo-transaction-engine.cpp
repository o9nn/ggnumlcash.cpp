#include "transaction-engine.h"
#include <iostream>
#include <iomanip>

// Simple Chart of Accounts for demonstration
class DemoChartOfAccounts {
private:
    std::map<std::string, double> balances;
    std::mutex balance_mutex;
    
public:
    DemoChartOfAccounts() {
        // Initialize accounts
        balances["1101"] = 100000.0;  // Cash - Starting balance
        balances["1102"] = 50000.0;   // Accounts Receivable
        balances["1201"] = 75000.0;   // Equipment
        balances["2101"] = 20000.0;   // Accounts Payable
        balances["2201"] = 50000.0;   // Long-term Loans
        balances["3100"] = 100000.0;  // Owner's Equity
        balances["4100"] = 0.0;       // Sales Revenue
        balances["4200"] = 0.0;       // Service Revenue
        balances["5101"] = 0.0;       // Salary Expense
        balances["5102"] = 0.0;       // Rent Expense
        balances["5103"] = 0.0;       // Utilities Expense
    }
    
    bool process_transaction(const Transaction& tx) {
        std::lock_guard<std::mutex> lock(balance_mutex);
        
        if (!tx.is_balanced()) {
            std::cerr << "Transaction not balanced: " << tx.id << std::endl;
            return false;
        }
        
        // Apply transaction
        for (const auto& entry : tx.entries) {
            if (balances.find(entry.account_code) == balances.end()) {
                std::cerr << "Account not found: " << entry.account_code << std::endl;
                return false;
            }
            
            balances[entry.account_code] += (entry.debit_amount - entry.credit_amount);
        }
        
        return true;
    }
    
    void print_balance_sheet() {
        std::lock_guard<std::mutex> lock(balance_mutex);
        
        std::cout << "\n=== BALANCE SHEET ===\n";
        std::cout << std::fixed << std::setprecision(2);
        
        std::cout << "\nASSETS:\n";
        std::cout << "  Cash (1101): $" << balances["1101"] << "\n";
        std::cout << "  Accounts Receivable (1102): $" << balances["1102"] << "\n";
        std::cout << "  Equipment (1201): $" << balances["1201"] << "\n";
        double total_assets = balances["1101"] + balances["1102"] + balances["1201"];
        std::cout << "  Total Assets: $" << total_assets << "\n";
        
        std::cout << "\nLIABILITIES:\n";
        std::cout << "  Accounts Payable (2101): $" << balances["2101"] << "\n";
        std::cout << "  Long-term Loans (2201): $" << balances["2201"] << "\n";
        double total_liabilities = balances["2101"] + balances["2201"];
        std::cout << "  Total Liabilities: $" << total_liabilities << "\n";
        
        std::cout << "\nEQUITY:\n";
        std::cout << "  Owner's Equity (3100): $" << balances["3100"] << "\n";
        
        std::cout << "\n";
    }
    
    void print_income_statement() {
        std::lock_guard<std::mutex> lock(balance_mutex);
        
        std::cout << "\n=== INCOME STATEMENT ===\n";
        std::cout << std::fixed << std::setprecision(2);
        
        std::cout << "\nREVENUE:\n";
        std::cout << "  Sales Revenue (4100): $" << balances["4100"] << "\n";
        std::cout << "  Service Revenue (4200): $" << balances["4200"] << "\n";
        double total_revenue = balances["4100"] + balances["4200"];
        std::cout << "  Total Revenue: $" << total_revenue << "\n";
        
        std::cout << "\nEXPENSES:\n";
        std::cout << "  Salary Expense (5101): $" << balances["5101"] << "\n";
        std::cout << "  Rent Expense (5102): $" << balances["5102"] << "\n";
        std::cout << "  Utilities Expense (5103): $" << balances["5103"] << "\n";
        double total_expenses = balances["5101"] + balances["5102"] + balances["5103"];
        std::cout << "  Total Expenses: $" << total_expenses << "\n";
        
        std::cout << "\nNET INCOME: $" << (total_revenue - total_expenses) << "\n";
        std::cout << "\n";
    }
};

void demo_batch_processing(TransactionEngine& engine) {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 1: BATCH TRANSACTION PROCESSING\n";
    std::cout << "========================================\n";
    
    // Create a batch of sales transactions
    std::vector<Transaction> sales_batch;
    
    for (int i = 1; i <= 5; i++) {
        Transaction tx;
        tx.id = "SALE-" + std::to_string(i);
        tx.description = "Product sale #" + std::to_string(i);
        tx.generate_timestamp();
        
        double amount = 1000.0 * i;
        tx.entries = {
            TransactionEntry("1101", amount, 0.0, "Cash received"),
            TransactionEntry("4100", 0.0, amount, "Sales revenue")
        };
        
        sales_batch.push_back(tx);
    }
    
    std::cout << "Submitting batch of " << sales_batch.size() << " sales transactions...\n";
    std::string batch_id = engine.submit_batch(sales_batch);
    
    std::cout << "Batch ID: " << batch_id << "\n";
    std::cout << "Waiting for batch to complete...\n";
    
    engine.wait_for_batch(batch_id);
    
    auto batch_status = engine.get_batch_status(batch_id);
    if (batch_status && batch_status->completed.load()) {
        std::cout << "✓ Batch completed successfully!\n";
        std::cout << "  Total transactions: " << batch_status->transactions.size() << "\n";
        std::cout << "  Errors: " << (batch_status->has_errors.load() ? "YES" : "NO") << "\n";
    }
}

void demo_transaction_templates(TransactionEngine& engine) {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 2: TRANSACTION TEMPLATES\n";
    std::cout << "========================================\n";
    
    // Create common transaction templates
    std::vector<TransactionTemplate> templates = {
        []() {
            TransactionTemplate tmpl;
            tmpl.id = "MONTHLY-SALARY";
            tmpl.name = "Monthly Salary Payment";
            tmpl.description = "Standard monthly salary payment template";
            tmpl.entry_template = {
                TransactionEntry("5101", 1.0, 0.0, "Salary expense"),
                TransactionEntry("1101", 0.0, 1.0, "Cash payment")
            };
            return tmpl;
        }(),
        []() {
            TransactionTemplate tmpl;
            tmpl.id = "MONTHLY-RENT";
            tmpl.name = "Monthly Rent Payment";
            tmpl.description = "Office rent payment template";
            tmpl.entry_template = {
                TransactionEntry("5102", 1.0, 0.0, "Rent expense"),
                TransactionEntry("1101", 0.0, 1.0, "Cash payment")
            };
            return tmpl;
        }(),
        []() {
            TransactionTemplate tmpl;
            tmpl.id = "SERVICE-REVENUE";
            tmpl.name = "Service Revenue";
            tmpl.description = "Service revenue receipt template";
            tmpl.entry_template = {
                TransactionEntry("1101", 1.0, 0.0, "Cash received"),
                TransactionEntry("4200", 0.0, 1.0, "Service revenue")
            };
            return tmpl;
        }()
    };
    
    // Add templates to engine
    for (const auto& tmpl : templates) {
        engine.add_template(tmpl);
        std::cout << "✓ Added template: " << tmpl.name << " (" << tmpl.id << ")\n";
    }
    
    // Use templates to create transactions
    std::cout << "\nCreating transactions from templates:\n";
    
    std::vector<Transaction> template_txs;
    
    // Salary payment
    TransactionTemplate salary_copy = templates[0];
    Transaction salary_tx = salary_copy.instantiate({{"amount", 15000.0}});
    template_txs.push_back(salary_tx);
    std::cout << "  - Salary payment: $15,000\n";
    
    // Rent payment
    TransactionTemplate rent_copy = templates[1];
    Transaction rent_tx = rent_copy.instantiate({{"amount", 3000.0}});
    template_txs.push_back(rent_tx);
    std::cout << "  - Rent payment: $3,000\n";
    
    // Service revenue
    TransactionTemplate service_copy = templates[2];
    Transaction service_tx = service_copy.instantiate({{"amount", 8500.0}});
    template_txs.push_back(service_tx);
    std::cout << "  - Service revenue: $8,500\n";
    
    // Submit batch
    std::string batch_id = engine.submit_batch(template_txs);
    engine.wait_for_batch(batch_id);
    
    std::cout << "✓ Template-based transactions completed\n";
}

void demo_multileg_transactions(TransactionEngine& engine) {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 3: MULTI-LEG TRANSACTIONS\n";
    std::cout << "========================================\n";
    
    // FX Swap example
    std::cout << "\n1. Foreign Exchange Swap Transaction:\n";
    auto now = std::chrono::system_clock::now();
    auto spot_date = now;
    auto forward_date = now + std::chrono::hours(24 * 90); // 90 days
    
    MultiLegTransaction fx_swap = MultiLegTransaction::create_fx_swap(
        "USD", "EUR", 50000.0, 52500.0, spot_date, forward_date
    );
    
    std::cout << "  Spot amount: $50,000 USD\n";
    std::cout << "  Forward amount: $52,500 EUR\n";
    std::cout << "  Forward date: 90 days\n";
    std::cout << "  Number of legs: " << fx_swap.get_legs().size() << "\n";
    
    Transaction fx_tx = fx_swap.to_transaction();
    std::vector<Transaction> multi_leg_batch = {fx_tx};
    
    // Interest Rate Swap example
    std::cout << "\n2. Interest Rate Swap Transaction:\n";
    auto start_date = now;
    auto end_date = now + std::chrono::hours(24 * 365); // 1 year
    
    MultiLegTransaction irs = MultiLegTransaction::create_interest_rate_swap(
        500000.0, 0.045, "LIBOR", start_date, end_date
    );
    
    std::cout << "  Notional: $500,000\n";
    std::cout << "  Fixed rate: 4.5%\n";
    std::cout << "  Floating index: LIBOR\n";
    std::cout << "  Duration: 1 year\n";
    std::cout << "  Number of legs: " << irs.get_legs().size() << "\n";
    
    Transaction irs_tx = irs.to_transaction();
    multi_leg_batch.push_back(irs_tx);
    
    std::string batch_id = engine.submit_batch(multi_leg_batch);
    engine.wait_for_batch(batch_id);
    
    std::cout << "\n✓ Multi-leg transactions completed\n";
}

void demo_audit_trail(TransactionEngine& engine) {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 4: CRYPTOGRAPHIC AUDIT TRAIL\n";
    std::cout << "========================================\n";
    
    const AuditTrail& trail = engine.get_audit_trail();
    
    std::cout << "Audit Trail Statistics:\n";
    std::cout << "  Total blocks: " << trail.get_block_count() << "\n";
    std::cout << "  Total transactions: " << trail.get_transaction_count() << "\n";
    std::cout << "  Integrity verified: " << (engine.verify_audit_trail() ? "YES" : "NO") << "\n";
    
    // Show sample of recent transactions
    std::cout << "\nRecent Transactions (last block):\n";
    if (trail.get_block_count() > 0) {
        const AuditBlock* last_block = trail.get_block(trail.get_block_count() - 1);
        if (last_block) {
            std::cout << "  Block #" << last_block->block_number << "\n";
            std::cout << "  Block hash: " << last_block->block_hash.substr(0, 16) << "...\n";
            std::cout << "  Transactions in block: " << last_block->transactions.size() << "\n";
            
            for (size_t i = 0; i < std::min(size_t(3), last_block->transactions.size()); i++) {
                const auto& tx = last_block->transactions[i];
                std::cout << "    - " << tx.id << ": " << tx.description << "\n";
                std::cout << "      Hash: " << tx.hash.substr(0, 16) << "...\n";
            }
        }
    }
}

void demo_performance_metrics(TransactionEngine& engine) {
    std::cout << "\n========================================\n";
    std::cout << "DEMO 5: PERFORMANCE METRICS\n";
    std::cout << "========================================\n";
    
    std::cout << engine.get_performance_report();
    
    // Run a quick performance test
    std::cout << "\nRunning performance test (1000 transactions)...\n";
    auto test_batch = TransactionUtils::generate_test_transactions(1000);
    
    auto start_time = std::chrono::steady_clock::now();
    std::string batch_id = engine.submit_batch(test_batch);
    engine.wait_for_batch(batch_id);
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    double tps = 1000.0 / (duration.count() / 1000.0);
    
    std::cout << "  Duration: " << duration.count() << " ms\n";
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2) << tps << " TPS\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "ADVANCED TRANSACTION ENGINE DEMO\n";
    std::cout << "Task 1.2: Advanced Transaction Engine\n";
    std::cout << "========================================\n";
    
    // Initialize Chart of Accounts
    DemoChartOfAccounts coa;
    
    std::cout << "\nInitial Financial State:\n";
    coa.print_balance_sheet();
    coa.print_income_statement();
    
    // Create transaction engine with 4 worker threads
    TransactionEngine engine(4);
    
    // Set transaction processor
    engine.set_transaction_processor([&coa](const Transaction& tx) {
        return coa.process_transaction(tx);
    });
    
    // Start the engine
    engine.start();
    std::cout << "\n✓ Transaction engine started with 4 worker threads\n";
    
    // Run demonstrations
    demo_batch_processing(engine);
    demo_transaction_templates(engine);
    demo_multileg_transactions(engine);
    demo_audit_trail(engine);
    demo_performance_metrics(engine);
    
    // Show final state
    std::cout << "\n========================================\n";
    std::cout << "FINAL FINANCIAL STATE\n";
    std::cout << "========================================\n";
    
    coa.print_balance_sheet();
    coa.print_income_statement();
    
    // Stop the engine
    engine.stop();
    std::cout << "\n✓ Transaction engine stopped\n";
    
    std::cout << "\n========================================\n";
    std::cout << "DEMO COMPLETED SUCCESSFULLY\n";
    std::cout << "========================================\n";
    std::cout << "\nKey Features Demonstrated:\n";
    std::cout << "  ✓ Batch transaction processing\n";
    std::cout << "  ✓ Transaction templates for common operations\n";
    std::cout << "  ✓ Multi-leg transactions (FX Swaps, Interest Rate Swaps)\n";
    std::cout << "  ✓ Cryptographic audit trail with blockchain structure\n";
    std::cout << "  ✓ High-throughput processing capabilities\n";
    std::cout << "  ✓ Real-time performance metrics\n";
    
    return 0;
}
