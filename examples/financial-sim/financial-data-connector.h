#pragma once

#include "transaction-engine.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

// ============================================================================
// Multi-Source Financial Data Connector - Phase A.2
//
// Connectors to ingest financial data from multiple accounting systems
// for cross-system audit analysis. Supports:
//   - CSV/Excel universal importer with configurable field mapping
//   - GnuCash XML file reader
//   - Beancount/hledger text file parser
//   - Data normalization layer mapping external accounts to GGNuCash CoA
//   - Extensible connector interface for future sources (Xero, ERPNext)
// ============================================================================

namespace ggnucash {
namespace connector {

// ============================================================================
// Connector Types
// ============================================================================

enum class ConnectorType {
    CSV,
    GNUCASH_XML,
    GNUCASH_SQLITE,
    BEANCOUNT,
    HLEDGER,
    XERO_API,
    ERPNEXT_API,
    CUSTOM
};

inline std::string connector_type_to_string(ConnectorType type) {
    switch (type) {
        case ConnectorType::CSV:             return "CSV";
        case ConnectorType::GNUCASH_XML:     return "GNUCASH_XML";
        case ConnectorType::GNUCASH_SQLITE:  return "GNUCASH_SQLITE";
        case ConnectorType::BEANCOUNT:       return "BEANCOUNT";
        case ConnectorType::HLEDGER:         return "HLEDGER";
        case ConnectorType::XERO_API:        return "XERO_API";
        case ConnectorType::ERPNEXT_API:     return "ERPNEXT_API";
        case ConnectorType::CUSTOM:          return "CUSTOM";
        default:                             return "UNKNOWN";
    }
}

// ============================================================================
// Imported Record - normalized representation of external financial data
// ============================================================================

struct ImportedAccount {
    std::string external_id;        // ID in source system
    std::string external_code;      // Code in source system
    std::string name;
    std::string type;               // Asset, Liability, Equity, Revenue, Expense
    std::string parent_id;          // Parent account ID (for hierarchical CoA)
    std::string currency;
    std::string description;
    std::map<std::string, std::string> metadata;

    ImportedAccount() : currency("USD") {}
};

struct ImportedTransaction {
    std::string external_id;        // ID in source system
    std::string date;               // ISO 8601 date string
    std::string description;
    std::string reference;          // Check number, invoice number, etc.
    std::string source_system;      // Which system this came from
    std::string currency;

    struct Line {
        std::string account_code;   // External account code
        double      debit_amount;
        double      credit_amount;
        std::string memo;
        std::string reconciled;     // y/n/c (yes/no/cleared)

        Line() : debit_amount(0.0), credit_amount(0.0) {}
    };

    std::vector<Line> lines;
    std::map<std::string, std::string> metadata;

    ImportedTransaction() : currency("USD") {}

    bool is_balanced(double tolerance = 0.01) const {
        double total_debits = 0.0, total_credits = 0.0;
        for (const auto & line : lines) {
            total_debits += line.debit_amount;
            total_credits += line.credit_amount;
        }
        return std::abs(total_debits - total_credits) < tolerance;
    }
};

// ============================================================================
// Import Result - statistics and results from an import operation
// ============================================================================

struct ImportResult {
    bool success;
    std::string connector_type;
    std::string source_path;

    // Imported data
    std::vector<ImportedAccount> accounts;
    std::vector<ImportedTransaction> transactions;

    // Statistics
    uint64_t total_records_read;
    uint64_t records_imported;
    uint64_t records_skipped;
    uint64_t records_failed;
    uint64_t accounts_imported;
    uint64_t transactions_imported;

    // Errors and warnings
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    // Timing
    std::chrono::milliseconds duration;

    ImportResult()
        : success(false),
          total_records_read(0),
          records_imported(0),
          records_skipped(0),
          records_failed(0),
          accounts_imported(0),
          transactions_imported(0),
          duration(0) {}

    std::string to_summary() const {
        std::stringstream ss;
        ss << "=== IMPORT RESULT ===\n";
        ss << "Source: " << source_path << " (" << connector_type << ")\n";
        ss << "Status: " << (success ? "SUCCESS" : "FAILED") << "\n";
        ss << "Records read: " << total_records_read << "\n";
        ss << "Records imported: " << records_imported << "\n";
        ss << "Records skipped: " << records_skipped << "\n";
        ss << "Records failed: " << records_failed << "\n";
        ss << "Accounts imported: " << accounts_imported << "\n";
        ss << "Transactions imported: " << transactions_imported << "\n";
        ss << "Duration: " << duration.count() << "ms\n";
        if (!errors.empty()) {
            ss << "\nErrors:\n";
            for (const auto & err : errors) {
                ss << "  - " << err << "\n";
            }
        }
        if (!warnings.empty()) {
            ss << "\nWarnings:\n";
            for (const auto & warn : warnings) {
                ss << "  - " << warn << "\n";
            }
        }
        return ss.str();
    }

    std::string to_json() const {
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"success\": " << (success ? "true" : "false") << ",\n";
        ss << "  \"connector_type\": \"" << connector_type << "\",\n";
        ss << "  \"source_path\": \"" << source_path << "\",\n";
        ss << "  \"total_records_read\": " << total_records_read << ",\n";
        ss << "  \"records_imported\": " << records_imported << ",\n";
        ss << "  \"records_skipped\": " << records_skipped << ",\n";
        ss << "  \"records_failed\": " << records_failed << ",\n";
        ss << "  \"accounts_imported\": " << accounts_imported << ",\n";
        ss << "  \"transactions_imported\": " << transactions_imported << ",\n";
        ss << "  \"duration_ms\": " << duration.count() << ",\n";
        ss << "  \"errors\": [";
        for (size_t i = 0; i < errors.size(); i++) {
            ss << "\"" << errors[i] << "\"";
            if (i < errors.size() - 1) ss << ", ";
        }
        ss << "],\n";
        ss << "  \"warnings\": [";
        for (size_t i = 0; i < warnings.size(); i++) {
            ss << "\"" << warnings[i] << "\"";
            if (i < warnings.size() - 1) ss << ", ";
        }
        ss << "]\n";
        ss << "}\n";
        return ss.str();
    }
};

// ============================================================================
// Account Mapping - maps external account codes to GGNuCash CoA
// ============================================================================

struct AccountMapping {
    std::string external_code;
    std::string ggnucash_code;
    std::string external_name;
    std::string ggnucash_name;
    std::string account_type;       // Asset, Liability, etc.
    bool is_auto_mapped;            // True if mapped by heuristic

    AccountMapping() : is_auto_mapped(false) {}
};

// ============================================================================
// Field Mapping - for CSV connector field configuration
// ============================================================================

struct CsvFieldMapping {
    int date_column;                // Column index for date (-1 if not present)
    int description_column;
    int reference_column;
    int account_column;
    int debit_column;
    int credit_column;
    int amount_column;              // Single amount column (positive=debit, negative=credit)
    int currency_column;
    int balance_column;

    std::string date_format;        // strftime format string
    char delimiter;
    char quote_char;
    bool has_header;
    int skip_rows;                  // Number of rows to skip at start

    CsvFieldMapping()
        : date_column(0),
          description_column(1),
          reference_column(-1),
          account_column(-1),
          debit_column(2),
          credit_column(3),
          amount_column(-1),
          currency_column(-1),
          balance_column(-1),
          date_format("%Y-%m-%d"),
          delimiter(','),
          quote_char('"'),
          has_header(true),
          skip_rows(0) {}
};

// ============================================================================
// Data Normalizer - maps external data to GGNuCash format
// ============================================================================

class DataNormalizer {
public:
    DataNormalizer();

    // Add an explicit account mapping
    void add_account_mapping(const std::string & external_code,
                             const std::string & ggnucash_code,
                             const std::string & account_type = "");

    // Auto-map accounts based on name heuristics
    void auto_map_accounts(const std::vector<ImportedAccount> & accounts);

    // Normalize an external account code to GGNuCash code
    std::string normalize_account_code(const std::string & external_code) const;

    // Check if an external code has a mapping
    bool has_mapping(const std::string & external_code) const;

    // Convert ImportedTransaction to GGNuCash Transaction
    Transaction normalize_transaction(const ImportedTransaction & imported) const;

    // Convert a batch of imported transactions
    std::vector<Transaction> normalize_transactions(
        const std::vector<ImportedTransaction> & imported) const;

    // Get all mappings
    std::vector<AccountMapping> get_all_mappings() const;

    // Get unmapped external codes
    std::vector<std::string> get_unmapped_codes(
        const std::vector<ImportedTransaction> & transactions) const;

    // Clear all mappings
    void clear_mappings();

    size_t get_mapping_count() const;

private:
    std::map<std::string, AccountMapping> mappings_; // external_code -> mapping
    mutable std::mutex normalizer_mutex_;

    // Heuristic type detection from account name
    std::string detect_account_type(const std::string & name) const;

    // Generate a GGNuCash account code from type and sequence
    std::string generate_ggnucash_code(const std::string & type, int sequence) const;
};

// ============================================================================
// Abstract Financial Data Connector
// ============================================================================

class FinancialDataConnector {
public:
    virtual ~FinancialDataConnector() = default;

    // Get connector type
    virtual ConnectorType get_type() const = 0;
    virtual std::string get_type_name() const = 0;

    // Test connectivity / file access
    virtual bool test_connection(const std::string & source) const = 0;

    // Import accounts from the source
    virtual ImportResult import_accounts(const std::string & source) = 0;

    // Import transactions from the source
    virtual ImportResult import_transactions(const std::string & source) = 0;

    // Import everything (accounts + transactions)
    virtual ImportResult import_all(const std::string & source) = 0;

    // Get supported file extensions (for file-based connectors)
    virtual std::vector<std::string> get_supported_extensions() const = 0;
};

// ============================================================================
// CSV Connector - Universal CSV/Excel importer
// ============================================================================

class CsvConnector : public FinancialDataConnector {
public:
    CsvConnector();
    explicit CsvConnector(const CsvFieldMapping & mapping);

    // Configure field mapping
    void set_field_mapping(const CsvFieldMapping & mapping);
    const CsvFieldMapping & get_field_mapping() const { return field_mapping_; }

    // Set the default account code for single-account CSV files
    void set_default_account(const std::string & account_code);

    // FinancialDataConnector interface
    ConnectorType get_type() const override { return ConnectorType::CSV; }
    std::string get_type_name() const override { return "CSV"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Parse a CSV string directly (for testing)
    ImportResult import_transactions_from_string(const std::string & csv_content);

private:
    CsvFieldMapping field_mapping_;
    std::string default_account_;

    // CSV parsing helpers
    std::vector<std::string> parse_csv_line(const std::string & line) const;
    std::vector<std::vector<std::string>> parse_csv_content(const std::string & content) const;
    std::string read_file_content(const std::string & path) const;
    std::string trim(const std::string & str) const;
    std::string unquote(const std::string & str) const;
};

// ============================================================================
// GnuCash XML Connector
// ============================================================================

class GnuCashXmlConnector : public FinancialDataConnector {
public:
    GnuCashXmlConnector();

    ConnectorType get_type() const override { return ConnectorType::GNUCASH_XML; }
    std::string get_type_name() const override { return "GnuCash XML"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Parse GnuCash XML from string (for testing)
    ImportResult import_from_string(const std::string & xml_content);

private:
    // Simplified XML tag extraction (no external XML dependency)
    std::string extract_tag_value(const std::string & xml,
                                  const std::string & tag,
                                  size_t start_pos = 0) const;
    std::vector<std::string> extract_all_blocks(const std::string & xml,
                                                 const std::string & open_tag,
                                                 const std::string & close_tag) const;
    ImportedAccount parse_gnucash_account(const std::string & account_block) const;
    ImportedTransaction parse_gnucash_transaction(
        const std::string & tx_block,
        const std::map<std::string, std::string> & account_id_to_name) const;
    std::string read_file_content(const std::string & path) const;
};

// ============================================================================
// Beancount Connector
// ============================================================================

class BeancountConnector : public FinancialDataConnector {
public:
    BeancountConnector();

    ConnectorType get_type() const override { return ConnectorType::BEANCOUNT; }
    std::string get_type_name() const override { return "Beancount"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Parse beancount from string (for testing)
    ImportResult import_from_string(const std::string & beancount_content);

private:
    // Beancount parsing helpers
    bool is_date_line(const std::string & line) const;
    bool is_posting_line(const std::string & line) const;
    bool is_account_open_directive(const std::string & line) const;
    ImportedAccount parse_open_directive(const std::string & line) const;
    std::string read_file_content(const std::string & path) const;
    std::string trim(const std::string & str) const;

    // Parse a full transaction block (date line + posting lines)
    ImportedTransaction parse_transaction_block(
        const std::string & header_line,
        const std::vector<std::string> & posting_lines) const;

    // Parse amount string like "100.00 USD" or "-50.00"
    struct ParsedAmount {
        double value;
        std::string currency;
        ParsedAmount() : value(0.0), currency("USD") {}
    };
    ParsedAmount parse_amount(const std::string & amount_str) const;
};

// ============================================================================
// Connector Factory
// ============================================================================

class ConnectorFactory {
public:
    // Create a connector by type
    static std::unique_ptr<FinancialDataConnector> create(ConnectorType type);

    // Auto-detect connector type from file extension
    static ConnectorType detect_type(const std::string & file_path);

    // Create a connector with auto-detection
    static std::unique_ptr<FinancialDataConnector> create_for_file(const std::string & file_path);
};

} // namespace connector
} // namespace ggnucash
