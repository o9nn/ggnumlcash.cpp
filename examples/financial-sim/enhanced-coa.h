#pragma once

#include "chart-of-accounts.h"
#include <algorithm>
#include <functional>

// Enhanced Chart of Accounts - the virtual hardware architecture with full metadata support
class EnhancedChartOfAccounts {
private:
    std::map<std::string, Account> accounts;
    std::vector<Transaction> transaction_log;
    ExchangeRateManager exchange_rates;
    std::map<std::string, AccountTemplate> templates;
    
public:
    EnhancedChartOfAccounts() : exchange_rates("USD") {}
    
    // Initialize with standard chart of accounts (expanded)
    void initialize_standard_coa() {
        // Asset Subsystem (Input/Storage)
        add_account("1000", "Assets", AccountType::ASSET);
        add_account("1100", "Current Assets", AccountType::ASSET, "1000");
        add_account("1101", "Cash", AccountType::ASSET, "1100");
        add_account("1102", "Accounts Receivable", AccountType::ASSET, "1100");
        add_account("1103", "Inventory", AccountType::ASSET, "1100");
        add_account("1104", "Prepaid Expenses", AccountType::ASSET, "1100");
        add_account("1105", "Short-term Investments", AccountType::ASSET, "1100");
        add_account("1200", "Fixed Assets", AccountType::ASSET, "1000");
        add_account("1201", "Equipment", AccountType::ASSET, "1200");
        add_account("1202", "Buildings", AccountType::ASSET, "1200");
        add_account("1203", "Land", AccountType::ASSET, "1200");
        add_account("1204", "Vehicles", AccountType::ASSET, "1200");
        add_account("1300", "Intangible Assets", AccountType::ASSET, "1000");
        add_account("1301", "Patents", AccountType::ASSET, "1300");
        add_account("1302", "Trademarks", AccountType::ASSET, "1300");
        add_account("1303", "Goodwill", AccountType::ASSET, "1300");
        
        // Liability Subsystem (Output/Obligations)
        add_account("2000", "Liabilities", AccountType::LIABILITY);
        add_account("2100", "Current Liabilities", AccountType::LIABILITY, "2000");
        add_account("2101", "Accounts Payable", AccountType::LIABILITY, "2100");
        add_account("2102", "Short-term Loans", AccountType::LIABILITY, "2100");
        add_account("2103", "Accrued Expenses", AccountType::LIABILITY, "2100");
        add_account("2104", "Deferred Revenue", AccountType::LIABILITY, "2100");
        add_account("2200", "Long-term Liabilities", AccountType::LIABILITY, "2000");
        add_account("2201", "Long-term Loans", AccountType::LIABILITY, "2200");
        add_account("2202", "Bonds Payable", AccountType::LIABILITY, "2200");
        add_account("2203", "Deferred Tax Liability", AccountType::LIABILITY, "2200");
        
        // Equity Subsystem (Core Processing)
        add_account("3000", "Equity", AccountType::EQUITY);
        add_account("3100", "Owner's Equity", AccountType::EQUITY, "3000");
        add_account("3200", "Retained Earnings", AccountType::EQUITY, "3000");
        add_account("3300", "Common Stock", AccountType::EQUITY, "3000");
        add_account("3400", "Additional Paid-in Capital", AccountType::EQUITY, "3000");
        
        // Revenue Subsystem (Signal Sources)
        add_account("4000", "Revenue", AccountType::REVENUE);
        add_account("4100", "Sales Revenue", AccountType::REVENUE, "4000");
        add_account("4200", "Service Revenue", AccountType::REVENUE, "4000");
        add_account("4300", "Interest Income", AccountType::REVENUE, "4000");
        add_account("4400", "Rental Income", AccountType::REVENUE, "4000");
        
        // Expense Subsystem (Signal Sinks)
        add_account("5000", "Expenses", AccountType::EXPENSE);
        add_account("5100", "Operating Expenses", AccountType::EXPENSE, "5000");
        add_account("5101", "Salaries Expense", AccountType::EXPENSE, "5100");
        add_account("5102", "Rent Expense", AccountType::EXPENSE, "5100");
        add_account("5103", "Utilities Expense", AccountType::EXPENSE, "5100");
        add_account("5104", "Marketing Expense", AccountType::EXPENSE, "5100");
        add_account("5105", "Insurance Expense", AccountType::EXPENSE, "5100");
        add_account("5200", "Cost of Goods Sold", AccountType::EXPENSE, "5000");
        add_account("5300", "Depreciation Expense", AccountType::EXPENSE, "5000");
        add_account("5400", "Interest Expense", AccountType::EXPENSE, "5000");
    }
    
    void add_account(const std::string& code, const std::string& name, AccountType type, 
                     const std::string& parent = "", const std::string& currency = "USD") {
        int depth = 0;
        if (!parent.empty() && accounts.find(parent) != accounts.end()) {
            depth = accounts[parent].depth + 1;
        }
        
        accounts[code] = Account(code, name, type, parent, depth);
        
        // Set appropriate currency symbol and name based on currency code
        std::string symbol = "$";
        std::string name_str = "US Dollar";
        int decimal_places = 2;
        
        if (currency == "EUR") { symbol = "€"; name_str = "Euro"; }
        else if (currency == "GBP") { symbol = "£"; name_str = "British Pound"; }
        else if (currency == "JPY") { symbol = "¥"; name_str = "Japanese Yen"; decimal_places = 0; }
        else if (currency == "CAD") { symbol = "C$"; name_str = "Canadian Dollar"; }
        else if (currency == "AUD") { symbol = "A$"; name_str = "Australian Dollar"; }
        else if (currency == "CHF") { symbol = "Fr"; name_str = "Swiss Franc"; }
        else if (currency == "CNY") { symbol = "¥"; name_str = "Chinese Yuan"; }
        else if (currency == "INR") { symbol = "₹"; name_str = "Indian Rupee"; }
        else if (currency == "BRL") { symbol = "R$"; name_str = "Brazilian Real"; }
        else if (currency == "MXN") { symbol = "$"; name_str = "Mexican Peso"; }
        
        accounts[code].metadata.currency = Currency(currency, symbol, name_str, decimal_places);
        
        if (!parent.empty() && accounts.find(parent) != accounts.end()) {
            accounts[parent].children.push_back(code);
        }
    }
    
    bool account_exists(const std::string& code) const {
        return accounts.find(code) != accounts.end();
    }
    
    Account* get_account(const std::string& code) {
        auto it = accounts.find(code);
        return (it != accounts.end()) ? &it->second : nullptr;
    }
    
    const Account* get_account(const std::string& code) const {
        auto it = accounts.find(code);
        return (it != accounts.end()) ? &it->second : nullptr;
    }
    
    // Get all accounts at a specific depth
    std::vector<const Account*> get_accounts_at_depth(int target_depth) const {
        std::vector<const Account*> result;
        for (const auto& pair : accounts) {
            if (pair.second.depth == target_depth) {
                result.push_back(&pair.second);
            }
        }
        return result;
    }
    
    // Get maximum depth of account hierarchy
    int get_max_depth() const {
        int max_depth = 0;
        for (const auto& pair : accounts) {
            if (pair.second.depth > max_depth) {
                max_depth = pair.second.depth;
            }
        }
        return max_depth;
    }
    
    // Get all leaf accounts (accounts with no children)
    std::vector<const Account*> get_leaf_accounts() const {
        std::vector<const Account*> result;
        for (const auto& pair : accounts) {
            if (pair.second.children.empty()) {
                result.push_back(&pair.second);
            }
        }
        return result;
    }
    
    // Get account path from root to account
    std::vector<std::string> get_account_path(const std::string& code) const {
        std::vector<std::string> path;
        std::string current = code;
        
        while (!current.empty()) {
            auto it = accounts.find(current);
            if (it == accounts.end()) break;
            
            path.insert(path.begin(), current);
            current = it->second.parent_code;
        }
        
        return path;
    }
    
    // Process transaction with multi-currency support
    bool process_transaction(const Transaction& transaction) {
        if (!transaction.is_balanced()) {
            return false;
        }
        
        // Validate all accounts exist
        for (const auto& entry : transaction.entries) {
            if (!account_exists(entry.account_code)) {
                return false;
            }
        }
        
        // Apply signal routing with currency conversion
        for (const auto& entry : transaction.entries) {
            Account* account = get_account(entry.account_code);
            
            // Convert amounts to account's currency if needed
            std::string entry_currency = entry.currency.empty() ? transaction.currency : entry.currency;
            double debit_in_account_currency = exchange_rates.convert(
                entry.debit_amount, entry_currency, account->metadata.currency.code);
            double credit_in_account_currency = exchange_rates.convert(
                entry.credit_amount, entry_currency, account->metadata.currency.code);
            
            if (account->is_debit_normal) {
                account->balance += debit_in_account_currency - credit_in_account_currency;
            } else {
                account->balance += credit_in_account_currency - debit_in_account_currency;
            }
            
            // Track multi-currency balances
            account->add_currency_balance(entry_currency, entry.debit_amount - entry.credit_amount);
        }
        
        // Log the transaction
        transaction_log.push_back(transaction);
        return true;
    }
    
    // Set account metadata
    void set_account_metadata(const std::string& code, const AccountMetadata& metadata) {
        Account* account = get_account(code);
        if (account) {
            account->metadata = metadata;
        }
    }
    
    // Set account currency
    void set_account_currency(const std::string& code, const Currency& currency) {
        Account* account = get_account(code);
        if (account) {
            account->metadata.currency = currency;
        }
    }
    
    // Add restriction to account
    void add_account_restriction(const std::string& code, const std::string& restriction) {
        Account* account = get_account(code);
        if (account) {
            account->metadata.restrictions.push_back(restriction);
        }
    }
    
    // Set account regulation
    void set_account_regulation(const std::string& code, const std::string& regulation) {
        Account* account = get_account(code);
        if (account) {
            account->metadata.regulation = regulation;
        }
    }
    
    // Exchange rate management
    void set_exchange_rate(const std::string& currency, double rate) {
        exchange_rates.set_rate(currency, rate);
    }
    
    double get_exchange_rate(const std::string& currency) const {
        return exchange_rates.get_rate(currency);
    }
    
    // Register account template
    void register_template(const AccountTemplate& template_def) {
        templates[template_def.name] = template_def;
    }
    
    // Create accounts from template
    bool create_from_template(const std::string& template_name, const std::string& code_prefix = "") {
        auto it = templates.find(template_name);
        if (it == templates.end()) return false;
        
        const AccountTemplate& tmpl = it->second;
        for (const Account& acc : tmpl.accounts) {
            std::string new_code = code_prefix.empty() ? acc.code : (code_prefix + acc.code);
            std::string new_parent = acc.parent_code.empty() ? "" : (code_prefix + acc.parent_code);
            
            add_account(new_code, acc.name, acc.type, new_parent, acc.metadata.currency.code);
            
            // Copy metadata
            if (account_exists(new_code)) {
                Account* new_account = get_account(new_code);
                new_account->metadata = acc.metadata;
            }
        }
        
        return true;
    }
    
    // Generate hardware state report
    std::string generate_balance_sheet(const std::string& currency = "USD") const {
        std::stringstream ss;
        ss << "=== FINANCIAL CIRCUIT STATE REPORT ===\n";
        ss << "Currency: " << currency << "\n\n";
        
        ss << "ASSET SUBSYSTEM (Input/Storage Nodes):\n";
        print_account_tree(ss, "1000", 0, currency);
        
        ss << "\nLIABILITY SUBSYSTEM (Output/Obligation Nodes):\n";
        print_account_tree(ss, "2000", 0, currency);
        
        ss << "\nEQUITY SUBSYSTEM (Core Processing Unit):\n";
        print_account_tree(ss, "3000", 0, currency);
        
        return ss.str();
    }
    
    std::string generate_income_statement(const std::string& currency = "USD") const {
        std::stringstream ss;
        ss << "=== SIGNAL FLOW ANALYSIS REPORT ===\n";
        ss << "Currency: " << currency << "\n\n";
        
        ss << "REVENUE SUBSYSTEM (Signal Sources):\n";
        print_account_tree(ss, "4000", 0, currency);
        
        ss << "\nEXPENSE SUBSYSTEM (Signal Sinks):\n";
        print_account_tree(ss, "5000", 0, currency);
        
        // Calculate net signal flow
        double total_revenue = calculate_subtotal("4000", currency);
        double total_expenses = calculate_subtotal("5000", currency);
        double net_income = total_revenue - total_expenses;
        
        ss << "\n--- SIGNAL FLOW SUMMARY ---\n";
        ss << "Total Input Signals (Revenue): " << format_currency(total_revenue, currency) << "\n";
        ss << "Total Output Signals (Expenses): " << format_currency(total_expenses, currency) << "\n";
        ss << "Net Signal Flow: " << format_currency(net_income, currency) << "\n";
        
        return ss.str();
    }
    
    double get_balance(const std::string& code, const std::string& currency = "") const {
        auto it = accounts.find(code);
        if (it == accounts.end()) return 0.0;
        
        if (currency.empty() || currency == it->second.metadata.currency.code) {
            return it->second.balance;
        }
        
        return exchange_rates.convert(it->second.balance, it->second.metadata.currency.code, currency);
    }
    
    size_t get_account_count() const {
        return accounts.size();
    }
    
    size_t get_transaction_count() const {
        return transaction_log.size();
    }
    
    // Get all accounts (for iteration)
    const std::map<std::string, Account>& get_all_accounts() const {
        return accounts;
    }
    
private:
    void print_account_tree(std::stringstream& ss, const std::string& account_code, int indent_depth, 
                           const std::string& currency) const {
        auto it = accounts.find(account_code);
        if (it == accounts.end()) return;
        
        const Account& account = it->second;
        
        // Indentation for hierarchy
        for (int i = 0; i < indent_depth; i++) ss << "  ";
        
        double balance = get_balance(account_code, currency);
        ss << account.code << " " << account.name << ": " 
           << format_currency(balance, currency);
        
        // Add metadata indicators
        if (!account.metadata.restrictions.empty()) {
            ss << " [R]";
        }
        if (!account.metadata.regulation.empty()) {
            ss << " [" << account.metadata.regulation << "]";
        }
        if (account.metadata.currency.code != currency) {
            ss << " (" << account.metadata.currency.code << ")";
        }
        
        ss << "\n";
        
        // Print children
        for (const std::string& child_code : account.children) {
            print_account_tree(ss, child_code, indent_depth + 1, currency);
        }
    }
    
    double calculate_subtotal(const std::string& account_code, const std::string& currency) const {
        auto it = accounts.find(account_code);
        if (it == accounts.end()) return 0.0;
        
        const Account& account = it->second;
        double total = get_balance(account_code, currency);
        
        for (const std::string& child_code : account.children) {
            total += calculate_subtotal(child_code, currency);
        }
        
        return total;
    }
    
    std::string format_currency(double amount, const std::string& currency) const {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        
        if (currency == "USD") {
            ss << "$" << amount;
        } else {
            ss << amount << " " << currency;
        }
        
        return ss.str();
    }
};
