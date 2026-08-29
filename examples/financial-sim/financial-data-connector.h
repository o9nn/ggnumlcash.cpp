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
//   - GnuCash SQLite file reader (self-contained read-only parser, no libsqlite3)
//   - Beancount/hledger text file parser
//   - Xero API response parser (JSON payload supplied by caller, no curl/openssl)
//   - ERPNext API response parser (JSON payload supplied by caller)
//   - Data normalization layer mapping external accounts to GGNuCash CoA
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
// Minimal JSON Parser - self-contained, no external JSON library
//
// Supports the JSON subset used by accounting REST APIs (Xero, ERPNext):
// objects, arrays, strings (with escapes and UTF-16 surrogate pairs), numbers,
// booleans, and null. Duplicate object keys resolve to the last occurrence.
// ============================================================================

struct JsonValue {
    enum class Type { NULL_VALUE, BOOL, NUMBER, STRING, ARRAY, OBJECT };

    Type type;

    bool                                             bool_value;
    double                                           number_value;
    std::string                                      string_value;
    std::vector<JsonValue>                           array_value;
    std::map<std::string, JsonValue>                 object_value;

    JsonValue() : type(Type::NULL_VALUE), bool_value(false), number_value(0.0) {}

    bool is_null()   const { return type == Type::NULL_VALUE; }
    bool is_bool()   const { return type == Type::BOOL; }
    bool is_number() const { return type == Type::NUMBER; }
    bool is_string() const { return type == Type::STRING; }
    bool is_array()  const { return type == Type::ARRAY; }
    bool is_object() const { return type == Type::OBJECT; }

    // True unless the value is explicitly JSON false or null
    bool as_bool() const;

    // Numbers parse to number_value; numeric strings ("123.45") also convert
    double as_number() const;

    // Strings return their contents; numbers/bools are rendered as text
    std::string as_string() const;

    // Object member access - returns a reference to a static null value when
    // the key is absent, so call sites can chain has()/get() without checks
    bool              has(const std::string & key) const;
    const JsonValue & get(const std::string & key) const;

    // Array access - returns a reference to a static null value when the index
    // is out of range or the value is not an array
    const JsonValue & at(size_t index) const;
    size_t            size() const;
};

class JsonParser {
public:
    // Parse a JSON document. Returns true on success; on failure returns false
    // and fills error with a human-readable position description.
    bool        parse(const std::string & text, JsonValue & out, std::string & error);

    // Convenience wrapper - throws std::runtime_error on malformed input
    JsonValue   parse_or_throw(const std::string & text);

private:
    const char * cur_;
    const char * end_;
    std::string  error_;

    bool parse_value(JsonValue & out);
    bool parse_object(JsonValue & out);
    bool parse_array(JsonValue & out);
    bool parse_string(std::string & out);
    bool parse_number(JsonValue & out);
    bool parse_literal(const char * literal, JsonValue & out);

    void skip_whitespace();
    bool append_utf8(std::string & out, uint32_t code_point);
    bool fail(const std::string & message);
};

// ============================================================================
// Minimal read-only SQLite3 file-format parser (no libsqlite3 dependency)
//
// Supported subset:
//   - SQLite database header validation ("SQLite format 3\0")
//   - Page sizes 512..65536 (including the 65536 special case)
//   - Table b-trees: interior (0x05) and leaf (0x0D) pages, right-most pointers
//   - Record format: varints, header serial types 0..9 (NULL/ints/float/text/blob)
//   - sqlite_master schema walk to locate table root pages
//
// Explicitly NOT supported (rows are skipped with a warning, never crash):
//   - Overflow pages (payload exceeding the usable page area)
//   - Index b-trees, WAL journal files, freelist management, any write path
//   - Serial types >= 12 are parsed (even/odd blob/text rule) but not decoded
//
// All reads are bounds-checked: a malformed file produces an error entry in
// SqliteReader::get_errors() and an empty row set, never an out-of-bounds read.
// ============================================================================

struct SqliteValue {
    enum class Type { NULL_VALUE, INTEGER, REAL, TEXT, BLOB };

    Type                type;
    int64_t             int_value;
    double              real_value;
    std::string         text_value;
    std::vector<uint8_t> blob_value;

    SqliteValue() : type(Type::NULL_VALUE), int_value(0), real_value(0.0) {}

    bool        is_null() const { return type == Type::NULL_VALUE; }
    int64_t     as_int() const;
    double      as_double() const;
    std::string as_string() const;
};

// One decoded table row: column name -> value
using SqliteRow = std::map<std::string, SqliteValue>;

class SqliteReader {
public:
    SqliteReader();

    // Parse an entire database image from memory. Returns true when the header
    // and page map are structurally valid.
    bool open(const uint8_t * data, size_t size);
    bool open(const std::string & bytes) {
        return open(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    }

    bool is_open() const { return opened_; }

    // Read all rows of a table. Returns false when the database is not open or
    // the table does not exist (check get_errors() for details).
    bool read_table(const std::string & table_name, std::vector<SqliteRow> & rows) const;

    std::vector<std::string> get_table_names() const;

    // Names of tables whose rows were skipped due to unsupported features
    std::vector<std::string> get_warnings() const { return warnings_; }

    // Structural problems encountered while parsing
    std::vector<std::string> get_errors() const { return errors_; }

private:
    // sqlite_master row
    struct MasterEntry {
        std::string type;           // "table", "index", ...
        std::string name;
        std::string tbl_name;
        uint32_t    root_page;
        std::string sql;
    };

    // Lazily-populated column names per table root page (parsed from CREATE TABLE sql)
    struct TableInfo {
        uint32_t                     root_page;
        std::vector<std::string>     columns;
    };

    const uint8_t * data_;
    size_t          size_;
    uint32_t        page_size_;
    bool            opened_;

    mutable std::vector<MasterEntry> master_;
    mutable bool                     master_loaded_;
    mutable std::vector<std::string> errors_;
    mutable std::vector<std::string> warnings_;

    // Page access helpers - all bounds-checked, return false on bad input
    bool     check_range(uint64_t offset, uint64_t length) const;
    bool     read_u8(uint64_t offset, uint8_t & out) const;
    bool     read_u16(uint64_t offset, uint16_t & out) const;
    bool     read_u32(uint64_t offset, uint32_t & out) const;
    bool     read_u64(uint64_t offset, uint64_t & out) const;
    uint64_t page_offset(uint32_t page_number) const;   // 1-based page number

    // SQLite varint: 1-9 bytes, big-endian, high bit = continuation
    bool read_varint(uint64_t offset, uint64_t & value, uint32_t & bytes_read) const;

    bool parse_header();
    bool load_master() const;

    // Recursive b-tree walk (table b-trees only); depth-limited for safety
    bool walk_table_btree(uint32_t page_number,
                          const std::vector<std::string> & columns,
                          std::vector<SqliteRow> & rows,
                          int depth) const;

    // Decode one leaf cell's record payload into a row
    bool decode_record(const uint8_t * payload,
                       size_t payload_size,
                       const std::vector<std::string> & columns,
                       SqliteRow & row) const;

    // Extract column names from a CREATE TABLE statement (pragmatic subset)
    std::vector<std::string> parse_create_table_columns(const std::string & sql) const;
};

// ============================================================================
// GnuCash SQLite Connector - reads .gnucash/.sqlite database files directly
//
// Maps GnuCash SQL schema to the normalized import structs:
//   accounts     (guid, name, account_type, parent_guid, code, description)
//   transactions (guid, currency_guid, num, post_date, description)
//   splits       (guid, tx_guid, account_guid, memo, reconciled_state,
//                 value_num, value_denom)
//   commodities  (guid, mnemonic) for currency resolution
// ============================================================================

class GnuCashSqliteConnector : public FinancialDataConnector {
public:
    GnuCashSqliteConnector();

    ConnectorType get_type() const override { return ConnectorType::GNUCASH_SQLITE; }
    std::string get_type_name() const override { return "GnuCash SQLite"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Import from an in-memory database image (for testing)
    ImportResult import_from_bytes(const uint8_t * data, size_t size);
    ImportResult import_from_bytes(const std::string & bytes) {
        return import_from_bytes(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
    }

private:
    ImportResult import_from_reader(SqliteReader & reader);

    std::string map_account_type(const std::string & gnucash_type) const;
    bool        parse_fraction(const std::string & fraction, double & out) const;
    std::string normalize_date(const std::string & sqlite_datetime) const;
    std::string read_file_bytes(const std::string & path) const;
};

// ============================================================================
// Xero Connector - parses Xero Accounting API v2 JSON responses
//
// NOTE: This connector performs NO network I/O. The caller is responsible for
// OAuth2 authorization and HTTPS transport (build_oauth2_request() constructs
// the request text). Feed the JSON response body to import_from_string().
//
// Handled endpoints:
//   GET /api.xro/2.0/Accounts         -> ImportedAccount records
//   GET /api.xro/2.0/BankTransactions -> ImportedTransaction records
// ============================================================================

class XeroConnector : public FinancialDataConnector {
public:
    XeroConnector();

    ConnectorType get_type() const override { return ConnectorType::XERO_API; }
    std::string get_type_name() const override { return "Xero API"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Parse a Xero API JSON response body (for testing / offline use)
    ImportResult import_from_string(const std::string & json_content);

    // Build an OAuth2 HTTP/1.1 GET request. Returns the raw request text with
    // Authorization: ****** header; transport is the caller's job.
    static std::string build_oauth2_request(const std::string & endpoint,
                                            const std::string & bearer_token,
                                            const std::string & tenant_id = "",
                                            const std::string & host = "api.xero.com");

private:
    ImportedAccount     parse_xero_account(const JsonValue & obj) const;
    ImportedTransaction parse_xero_bank_transaction(const JsonValue & obj) const;
    std::string         map_xero_account_type(const std::string & xero_type) const;
    std::string         normalize_xero_date(const std::string & xero_date) const;
};

// ============================================================================
// ERPNext Connector - parses ERPNext REST API JSON responses
//
// NOTE: This connector performs NO network I/O. The caller fetches documents
// over HTTPS (build_api_request() constructs the request text with token
// authentication) and passes the response body to import_from_string().
//
// Handled resources:
//   /api/resource/Account        -> ImportedAccount records
//   /api/resource/Journal Entry  -> ImportedTransaction records
// ============================================================================

class ErpNextConnector : public FinancialDataConnector {
public:
    ErpNextConnector();

    ConnectorType get_type() const override { return ConnectorType::ERPNEXT_API; }
    std::string get_type_name() const override { return "ERPNext API"; }
    bool test_connection(const std::string & source) const override;
    ImportResult import_accounts(const std::string & source) override;
    ImportResult import_transactions(const std::string & source) override;
    ImportResult import_all(const std::string & source) override;
    std::vector<std::string> get_supported_extensions() const override;

    // Parse an ERPNext API JSON response body (for testing / offline use)
    ImportResult import_from_string(const std::string & json_content);

    // Build an HTTP/1.1 GET request using ERPNext token authentication
    // ("Authorization: token <api_key>:<api_secret>").
    static std::string build_api_request(const std::string & endpoint,
                                         const std::string & api_key,
                                         const std::string & api_secret,
                                         const std::string & host);

private:
    ImportedAccount     parse_erpnext_account(const JsonValue & obj) const;
    ImportedTransaction parse_erpnext_journal_entry(const JsonValue & obj) const;
    std::string         map_erpnext_account_type(const std::string & erpnext_type) const;
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
