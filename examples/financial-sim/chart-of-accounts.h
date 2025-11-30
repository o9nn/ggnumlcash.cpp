#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <sstream>
#include <iomanip>

// Currency support with real-time conversion
struct Currency {
    std::string code;        // ISO 4217 currency code (USD, EUR, GBP, etc.)
    std::string symbol;      // Currency symbol ($, €, £, etc.)
    std::string name;        // Full currency name
    int decimal_places;      // Number of decimal places (2 for USD, 0 for JPY, etc.)
    
    Currency() : code("USD"), symbol("$"), name("US Dollar"), decimal_places(2) {}
    Currency(const std::string& c, const std::string& s, const std::string& n, int dp)
        : code(c), symbol(s), name(n), decimal_places(dp) {}
};

// Exchange rate manager for multi-currency support
class ExchangeRateManager {
private:
    std::string base_currency;
    std::unordered_map<std::string, double> rates; // Currency code -> rate to base
    
public:
    ExchangeRateManager(const std::string& base = "USD") : base_currency(base) {
        rates[base_currency] = 1.0;
        
        // Initialize with common exchange rates (in practice, these would be real-time)
        rates["EUR"] = 0.85;   // 1 USD = 0.85 EUR
        rates["GBP"] = 0.73;   // 1 USD = 0.73 GBP
        rates["JPY"] = 110.0;  // 1 USD = 110 JPY
        rates["CAD"] = 1.25;   // 1 USD = 1.25 CAD
        rates["AUD"] = 1.35;   // 1 USD = 1.35 AUD
        rates["CHF"] = 0.92;   // 1 USD = 0.92 CHF
        rates["CNY"] = 6.45;   // 1 USD = 6.45 CNY
        rates["INR"] = 74.0;   // 1 USD = 74 INR
        rates["BRL"] = 5.25;   // 1 USD = 5.25 BRL
        rates["MXN"] = 20.0;   // 1 USD = 20 MXN
    }
    
    void set_rate(const std::string& currency, double rate) {
        rates[currency] = rate;
    }
    
    double get_rate(const std::string& currency) const {
        auto it = rates.find(currency);
        return (it != rates.end()) ? it->second : 1.0;
    }
    
    double convert(double amount, const std::string& from_currency, const std::string& to_currency) const {
        if (from_currency == to_currency) return amount;
        
        double from_rate = get_rate(from_currency);
        double to_rate = get_rate(to_currency);
        
        // Convert to base currency, then to target currency
        double base_amount = amount / from_rate;
        return base_amount * to_rate;
    }
    
    double to_base_currency(double amount, const std::string& currency) const {
        return convert(amount, currency, base_currency);
    }
    
    std::string get_base_currency() const { return base_currency; }
};

// Account metadata for enhanced functionality
struct AccountMetadata {
    Currency currency;                           // Account currency
    std::string regulation;                      // Regulatory framework (SOX, Basel III, etc.)
    std::vector<std::string> restrictions;       // Usage restrictions
    std::string tax_category;                    // Tax category
    std::string business_unit;                   // Business unit or department
    std::string cost_center;                     // Cost center code
    bool is_reconcilable;                        // Can be reconciled with bank statements
    bool is_active;                              // Account is active
    std::string created_date;                    // Creation timestamp
    std::string modified_date;                   // Last modification timestamp
    std::map<std::string, std::string> custom_fields; // Custom metadata
    
    AccountMetadata() 
        : currency(),
          regulation(""),
          tax_category(""),
          business_unit(""),
          cost_center(""),
          is_reconcilable(false),
          is_active(true),
          created_date(""),
          modified_date("") {}
};

// Financial Account Types - mapped to hardware subsystems
enum class AccountType {
    ASSET,      // Input registers/storage
    LIABILITY,  // Output buffers/obligations
    EQUITY,     // Core processing unit
    REVENUE,    // Signal generators/inputs
    EXPENSE     // Signal consumers/outputs
};

// Account Node - represents a hardware pin/register with enhanced metadata
struct Account {
    std::string code;                    // Pin/node identifier
    std::string name;                    // Human readable name
    AccountType type;                    // Hardware subsystem type
    double balance;                      // Current signal level/voltage
    std::string parent_code;             // Parent node in hierarchy
    std::vector<std::string> children;   // Child nodes
    bool is_debit_normal;                // Signal polarity (true = positive signal increases balance)
    int depth;                           // Depth in hierarchy (0 = root)
    AccountMetadata metadata;            // Enhanced metadata
    std::map<std::string, double> multi_currency_balances; // Balances in different currencies
    
    Account() 
        : code(""), 
          name(""), 
          type(AccountType::ASSET), 
          balance(0.0), 
          parent_code(""), 
          is_debit_normal(true),
          depth(0),
          metadata() {}
    
    Account(const std::string& c, const std::string& n, AccountType t, const std::string& parent = "", int d = 0)
        : code(c), 
          name(n), 
          type(t), 
          balance(0.0), 
          parent_code(parent), 
          is_debit_normal(t == AccountType::ASSET || t == AccountType::EXPENSE),
          depth(d),
          metadata() {
        metadata.is_active = true;
    }
    
    // Get balance in specific currency
    double get_balance_in_currency(const std::string& currency_code, const ExchangeRateManager& rates) const {
        if (currency_code == metadata.currency.code) {
            return balance;
        }
        return rates.convert(balance, metadata.currency.code, currency_code);
    }
    
    // Add amount in specific currency
    void add_currency_balance(const std::string& currency_code, double amount) {
        multi_currency_balances[currency_code] += amount;
    }
};

// Account template for creating standard account structures
struct AccountTemplate {
    std::string name;
    std::string description;
    std::string industry;        // retail, manufacturing, services, etc.
    std::vector<Account> accounts;
    
    AccountTemplate() : name(""), description(""), industry("") {}
    AccountTemplate(const std::string& n, const std::string& d, const std::string& i)
        : name(n), description(d), industry(i) {}
};

// Transaction Entry - represents signal flow between nodes
struct TransactionEntry {
    std::string account_code;   // Target node
    double debit_amount;        // Positive signal flow
    double credit_amount;       // Negative signal flow
    std::string description;    // Signal description
    std::string currency;       // Transaction currency
    
    TransactionEntry() : account_code(""), debit_amount(0.0), credit_amount(0.0), description(""), currency("USD") {}
};

// Transaction - represents a complete signal routing operation
struct Transaction {
    std::string id;
    std::string description;
    std::vector<TransactionEntry> entries;
    std::string timestamp;
    std::string currency;       // Base currency for this transaction
    
    Transaction() : id(""), description(""), timestamp(""), currency("USD") {}
    
    // Validate double-entry (conservation law)
    bool is_balanced() const {
        double total_debits = 0.0, total_credits = 0.0;
        for (const auto& entry : entries) {
            total_debits += entry.debit_amount;
            total_credits += entry.credit_amount;
        }
        return std::abs(total_debits - total_credits) < 0.01; // Allow small floating point errors
    }
};

// Inline helper functions
inline std::string get_account_type_name(AccountType type) {
    switch (type) {
        case AccountType::ASSET: return "Asset (Storage/Input)";
        case AccountType::LIABILITY: return "Liability (Output/Buffer)";
        case AccountType::EQUITY: return "Equity (Core Processing)";
        case AccountType::REVENUE: return "Revenue (Signal Source)";
        case AccountType::EXPENSE: return "Expense (Signal Sink)";
        default: return "Unknown";
    }
}

inline AccountType parse_account_type(const std::string& type_str) {
    if (type_str == "ASSET") return AccountType::ASSET;
    if (type_str == "LIABILITY") return AccountType::LIABILITY;
    if (type_str == "EQUITY") return AccountType::EQUITY;
    if (type_str == "REVENUE") return AccountType::REVENUE;
    if (type_str == "EXPENSE") return AccountType::EXPENSE;
    return AccountType::ASSET; // Default
}
