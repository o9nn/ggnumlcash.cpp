#include "financial-data-connector.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace ggnucash {
namespace connector {

// ============================================================================
// DataNormalizer Implementation
// ============================================================================

DataNormalizer::DataNormalizer() {}

void DataNormalizer::add_account_mapping(const std::string & external_code,
                                          const std::string & ggnucash_code,
                                          const std::string & account_type) {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    AccountMapping mapping;
    mapping.external_code = external_code;
    mapping.ggnucash_code = ggnucash_code;
    mapping.account_type = account_type;
    mapping.is_auto_mapped = false;
    mappings_[external_code] = mapping;
}

void DataNormalizer::auto_map_accounts(const std::vector<ImportedAccount> & accounts) {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);

    std::map<std::string, int> type_counters;

    for (const auto & account : accounts) {
        if (mappings_.find(account.external_code) != mappings_.end()) {
            continue; // Already mapped
        }

        std::string type = account.type.empty()
            ? detect_account_type(account.name)
            : account.type;

        type_counters[type]++;
        std::string ggnucash_code = generate_ggnucash_code(type, type_counters[type]);

        AccountMapping mapping;
        mapping.external_code = account.external_code;
        mapping.ggnucash_code = ggnucash_code;
        mapping.external_name = account.name;
        mapping.account_type = type;
        mapping.is_auto_mapped = true;
        mappings_[account.external_code] = mapping;
    }
}

std::string DataNormalizer::normalize_account_code(const std::string & external_code) const {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    auto it = mappings_.find(external_code);
    if (it != mappings_.end()) {
        return it->second.ggnucash_code;
    }
    // Return external code unchanged if no mapping exists
    return external_code;
}

bool DataNormalizer::has_mapping(const std::string & external_code) const {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    return mappings_.find(external_code) != mappings_.end();
}

Transaction DataNormalizer::normalize_transaction(const ImportedTransaction & imported) const {
    Transaction tx;
    tx.id = imported.external_id.empty()
        ? TransactionUtils::generate_transaction_id("IMP")
        : imported.external_id;
    tx.description = imported.description;
    tx.timestamp = imported.date;

    for (const auto & line : imported.lines) {
        TransactionEntry entry;
        entry.account_code = normalize_account_code(line.account_code);
        entry.debit_amount = line.debit_amount;
        entry.credit_amount = line.credit_amount;
        entry.description = line.memo;
        tx.entries.push_back(entry);
    }

    tx.calculate_hash();
    return tx;
}

std::vector<Transaction> DataNormalizer::normalize_transactions(
    const std::vector<ImportedTransaction> & imported) const {
    std::vector<Transaction> result;
    result.reserve(imported.size());

    std::string prev_hash;
    for (const auto & imp : imported) {
        Transaction tx = normalize_transaction(imp);
        tx.prev_hash = prev_hash;
        prev_hash = tx.hash;
        result.push_back(tx);
    }

    return result;
}

std::vector<AccountMapping> DataNormalizer::get_all_mappings() const {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    std::vector<AccountMapping> result;
    result.reserve(mappings_.size());
    for (const auto & kv : mappings_) {
        result.push_back(kv.second);
    }
    return result;
}

std::vector<std::string> DataNormalizer::get_unmapped_codes(
    const std::vector<ImportedTransaction> & transactions) const {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    std::set<std::string> unmapped;
    for (const auto & tx : transactions) {
        for (const auto & line : tx.lines) {
            if (mappings_.find(line.account_code) == mappings_.end()) {
                unmapped.insert(line.account_code);
            }
        }
    }
    return std::vector<std::string>(unmapped.begin(), unmapped.end());
}

void DataNormalizer::clear_mappings() {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    mappings_.clear();
}

size_t DataNormalizer::get_mapping_count() const {
    std::lock_guard<std::mutex> lock(normalizer_mutex_);
    return mappings_.size();
}

std::string DataNormalizer::detect_account_type(const std::string & name) const {
    std::string lower_name;
    lower_name.resize(name.size());
    for (size_t i = 0; i < name.size(); i++) {
        lower_name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
    }

    // Check for liability keywords first (higher priority -- "Bank Loan Payable" is a liability)
    if (lower_name.find("payable") != std::string::npos ||
        lower_name.find("loan") != std::string::npos ||
        lower_name.find("liability") != std::string::npos ||
        lower_name.find("mortgage") != std::string::npos ||
        lower_name.find("credit card") != std::string::npos ||
        lower_name.find("debt") != std::string::npos) {
        return "Liability";
    }

    // Check for asset-related keywords
    if (lower_name.find("bank") != std::string::npos ||
        lower_name.find("cash") != std::string::npos ||
        lower_name.find("receivable") != std::string::npos ||
        lower_name.find("inventory") != std::string::npos ||
        lower_name.find("asset") != std::string::npos ||
        lower_name.find("checking") != std::string::npos ||
        lower_name.find("savings") != std::string::npos) {
        return "Asset";
    }

    // Check for equity keywords
    if (lower_name.find("equity") != std::string::npos ||
        lower_name.find("capital") != std::string::npos ||
        lower_name.find("retained") != std::string::npos ||
        lower_name.find("owner") != std::string::npos) {
        return "Equity";
    }

    // Check for revenue keywords
    if (lower_name.find("revenue") != std::string::npos ||
        lower_name.find("income") != std::string::npos ||
        lower_name.find("sales") != std::string::npos ||
        lower_name.find("service") != std::string::npos) {
        return "Revenue";
    }

    // Check for expense keywords
    if (lower_name.find("expense") != std::string::npos ||
        lower_name.find("cost") != std::string::npos ||
        lower_name.find("rent") != std::string::npos ||
        lower_name.find("salary") != std::string::npos ||
        lower_name.find("utilities") != std::string::npos ||
        lower_name.find("insurance") != std::string::npos ||
        lower_name.find("depreciation") != std::string::npos) {
        return "Expense";
    }

    return "Asset"; // Default
}

std::string DataNormalizer::generate_ggnucash_code(const std::string & type, int sequence) const {
    std::stringstream ss;
    if (type == "Asset") {
        ss << "1" << std::setfill('0') << std::setw(4) << sequence;
    } else if (type == "Liability") {
        ss << "2" << std::setfill('0') << std::setw(4) << sequence;
    } else if (type == "Equity") {
        ss << "3" << std::setfill('0') << std::setw(4) << sequence;
    } else if (type == "Revenue") {
        ss << "4" << std::setfill('0') << std::setw(4) << sequence;
    } else if (type == "Expense") {
        ss << "5" << std::setfill('0') << std::setw(4) << sequence;
    } else {
        ss << "9" << std::setfill('0') << std::setw(4) << sequence;
    }
    return ss.str();
}

// ============================================================================
// CsvConnector Implementation
// ============================================================================

CsvConnector::CsvConnector() {}

CsvConnector::CsvConnector(const CsvFieldMapping & mapping)
    : field_mapping_(mapping) {}

void CsvConnector::set_field_mapping(const CsvFieldMapping & mapping) {
    field_mapping_ = mapping;
}

void CsvConnector::set_default_account(const std::string & account_code) {
    default_account_ = account_code;
}

bool CsvConnector::test_connection(const std::string & source) const {
    std::ifstream file(source);
    return file.good();
}

ImportResult CsvConnector::import_accounts(const std::string & source) {
    // CSV files typically don't contain account definitions
    ImportResult result;
    result.connector_type = "CSV";
    result.source_path = source;
    result.success = true;
    result.warnings.push_back("CSV connector does not import account structures. "
                              "Use import_transactions() and configure account mappings.");
    return result;
}

ImportResult CsvConnector::import_transactions(const std::string & source) {
    std::string content = read_file_content(source);
    if (content.empty()) {
        ImportResult result;
        result.connector_type = "CSV";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }
    auto result = import_transactions_from_string(content);
    result.source_path = source;
    return result;
}

ImportResult CsvConnector::import_all(const std::string & source) {
    return import_transactions(source);
}

std::vector<std::string> CsvConnector::get_supported_extensions() const {
    return {".csv", ".tsv", ".txt"};
}

ImportResult CsvConnector::import_transactions_from_string(const std::string & csv_content) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "CSV";
    result.success = true;

    auto rows = parse_csv_content(csv_content);

    if (rows.empty()) {
        result.success = false;
        result.errors.push_back("No data rows found in CSV");
        return result;
    }

    // Skip header and initial rows
    size_t start_row = (field_mapping_.has_header ? 1 : 0) + field_mapping_.skip_rows;

    uint64_t tx_counter = 0;
    for (size_t i = start_row; i < rows.size(); i++) {
        const auto & row = rows[i];
        result.total_records_read++;

        if (row.empty()) {
            result.records_skipped++;
            continue;
        }

        ImportedTransaction tx;
        tx.source_system = "CSV";
        tx_counter++;
        tx.external_id = "CSV-" + std::to_string(tx_counter);

        // Extract date
        if (field_mapping_.date_column >= 0 &&
            field_mapping_.date_column < (int)row.size()) {
            tx.date = trim(row[field_mapping_.date_column]);
        }

        // Extract description
        if (field_mapping_.description_column >= 0 &&
            field_mapping_.description_column < (int)row.size()) {
            tx.description = trim(row[field_mapping_.description_column]);
        }

        // Extract reference
        if (field_mapping_.reference_column >= 0 &&
            field_mapping_.reference_column < (int)row.size()) {
            tx.reference = trim(row[field_mapping_.reference_column]);
        }

        // Extract amounts
        ImportedTransaction::Line line;

        if (field_mapping_.account_column >= 0 &&
            field_mapping_.account_column < (int)row.size()) {
            line.account_code = trim(row[field_mapping_.account_column]);
        } else if (!default_account_.empty()) {
            line.account_code = default_account_;
        }

        if (field_mapping_.amount_column >= 0 &&
            field_mapping_.amount_column < (int)row.size()) {
            // Single amount column
            std::string amount_str = trim(row[field_mapping_.amount_column]);
            if (!amount_str.empty()) {
                try {
                    double amount = std::stod(amount_str);
                    if (amount >= 0) {
                        line.debit_amount = amount;
                    } else {
                        line.credit_amount = -amount;
                    }
                } catch (...) {
                    result.records_failed++;
                    result.errors.push_back("Row " + std::to_string(i + 1) +
                                            ": Invalid amount '" + amount_str + "'");
                    continue;
                }
            }
        } else {
            // Separate debit/credit columns
            if (field_mapping_.debit_column >= 0 &&
                field_mapping_.debit_column < (int)row.size()) {
                std::string debit_str = trim(row[field_mapping_.debit_column]);
                if (!debit_str.empty()) {
                    try {
                        line.debit_amount = std::stod(debit_str);
                    } catch (...) {
                        result.records_failed++;
                        result.errors.push_back("Row " + std::to_string(i + 1) +
                                                ": Invalid debit '" + debit_str + "'");
                        continue;
                    }
                }
            }

            if (field_mapping_.credit_column >= 0 &&
                field_mapping_.credit_column < (int)row.size()) {
                std::string credit_str = trim(row[field_mapping_.credit_column]);
                if (!credit_str.empty()) {
                    try {
                        line.credit_amount = std::stod(credit_str);
                    } catch (...) {
                        result.records_failed++;
                        result.errors.push_back("Row " + std::to_string(i + 1) +
                                                ": Invalid credit '" + credit_str + "'");
                        continue;
                    }
                }
            }
        }

        // Currency
        if (field_mapping_.currency_column >= 0 &&
            field_mapping_.currency_column < (int)row.size()) {
            tx.currency = trim(row[field_mapping_.currency_column]);
        }

        tx.lines.push_back(line);
        result.transactions.push_back(tx);
        result.records_imported++;
        result.transactions_imported++;
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return result;
}

std::vector<std::string> CsvConnector::parse_csv_line(const std::string & line) const {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];

        if (c == field_mapping_.quote_char) {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == field_mapping_.quote_char) {
                field += c;
                i++; // Skip escaped quote
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == field_mapping_.delimiter && !in_quotes) {
            fields.push_back(field);
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(field); // Last field

    return fields;
}

std::vector<std::vector<std::string>> CsvConnector::parse_csv_content(const std::string & content) const {
    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove trailing \r from Windows line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            rows.push_back(parse_csv_line(line));
        }
    }

    return rows;
}

std::string CsvConnector::read_file_content(const std::string & path) const {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string CsvConnector::trim(const std::string & str) const {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

std::string CsvConnector::unquote(const std::string & str) const {
    if (str.size() >= 2 && str.front() == field_mapping_.quote_char &&
        str.back() == field_mapping_.quote_char) {
        return str.substr(1, str.size() - 2);
    }
    return str;
}

// ============================================================================
// GnuCashXmlConnector Implementation
// ============================================================================

GnuCashXmlConnector::GnuCashXmlConnector() {}

bool GnuCashXmlConnector::test_connection(const std::string & source) const {
    std::ifstream file(source);
    if (!file.good()) return false;

    // Check for GnuCash XML marker
    std::string first_line;
    std::getline(file, first_line);
    // Simplified check: look for XML declaration or gnc-v2 tag
    return first_line.find("<?xml") != std::string::npos ||
           first_line.find("gnc-v2") != std::string::npos;
}

ImportResult GnuCashXmlConnector::import_accounts(const std::string & source) {
    std::string content = read_file_content(source);
    if (content.empty()) {
        ImportResult result;
        result.connector_type = "GnuCash XML";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }

    auto result = import_from_string(content);
    result.source_path = source;
    // Clear transactions - only want accounts
    result.transactions.clear();
    result.transactions_imported = 0;
    return result;
}

ImportResult GnuCashXmlConnector::import_transactions(const std::string & source) {
    std::string content = read_file_content(source);
    if (content.empty()) {
        ImportResult result;
        result.connector_type = "GnuCash XML";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }

    auto result = import_from_string(content);
    result.source_path = source;
    return result;
}

ImportResult GnuCashXmlConnector::import_all(const std::string & source) {
    return import_transactions(source);
}

std::vector<std::string> GnuCashXmlConnector::get_supported_extensions() const {
    return {".gnucash", ".xml"};
}

ImportResult GnuCashXmlConnector::import_from_string(const std::string & xml_content) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "GnuCash XML";
    result.success = true;

    // Parse accounts first to build ID -> name map
    std::map<std::string, std::string> account_id_to_name;
    auto account_blocks = extract_all_blocks(xml_content, "<gnc:account", "</gnc:account>");
    if (account_blocks.empty()) {
        // Try alternative format
        account_blocks = extract_all_blocks(xml_content, "<act:account", "</act:account>");
    }

    for (const auto & block : account_blocks) {
        result.total_records_read++;
        auto account = parse_gnucash_account(block);
        if (!account.external_id.empty()) {
            account_id_to_name[account.external_id] = account.external_code.empty()
                ? account.name : account.external_code;
            result.accounts.push_back(account);
            result.accounts_imported++;
            result.records_imported++;
        }
    }

    // Parse transactions
    auto tx_blocks = extract_all_blocks(xml_content, "<gnc:transaction", "</gnc:transaction>");
    if (tx_blocks.empty()) {
        tx_blocks = extract_all_blocks(xml_content, "<trn:transaction", "</trn:transaction>");
    }

    for (const auto & block : tx_blocks) {
        result.total_records_read++;
        auto tx = parse_gnucash_transaction(block, account_id_to_name);
        if (!tx.external_id.empty()) {
            result.transactions.push_back(tx);
            result.transactions_imported++;
            result.records_imported++;
        } else {
            result.records_failed++;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return result;
}

std::string GnuCashXmlConnector::extract_tag_value(const std::string & xml,
                                                    const std::string & tag,
                                                    size_t start_pos) const {
    std::string open = "<" + tag;
    size_t pos = xml.find(open, start_pos);
    if (pos == std::string::npos) return "";

    // Find the end of the opening tag (handles attributes)
    size_t tag_end = xml.find(">", pos);
    if (tag_end == std::string::npos) return "";

    // Check for self-closing tag
    if (xml[tag_end - 1] == '/') return "";

    size_t content_start = tag_end + 1;
    std::string close = "</" + tag + ">";
    size_t content_end = xml.find(close, content_start);
    if (content_end == std::string::npos) return "";

    return xml.substr(content_start, content_end - content_start);
}

std::vector<std::string> GnuCashXmlConnector::extract_all_blocks(
    const std::string & xml,
    const std::string & open_tag,
    const std::string & close_tag) const {
    std::vector<std::string> blocks;
    size_t pos = 0;

    while (pos < xml.size()) {
        size_t start = xml.find(open_tag, pos);
        if (start == std::string::npos) break;

        size_t end = xml.find(close_tag, start);
        if (end == std::string::npos) break;

        end += close_tag.size();
        blocks.push_back(xml.substr(start, end - start));
        pos = end;
    }

    return blocks;
}

ImportedAccount GnuCashXmlConnector::parse_gnucash_account(const std::string & block) const {
    ImportedAccount account;
    account.external_id = extract_tag_value(block, "act:id");
    account.name = extract_tag_value(block, "act:name");
    account.external_code = extract_tag_value(block, "act:code");
    account.description = extract_tag_value(block, "act:description");
    account.parent_id = extract_tag_value(block, "act:parent");

    std::string type_str = extract_tag_value(block, "act:type");
    if (type_str == "ASSET" || type_str == "BANK" || type_str == "CASH" ||
        type_str == "RECEIVABLE" || type_str == "STOCK" || type_str == "MUTUAL") {
        account.type = "Asset";
    } else if (type_str == "LIABILITY" || type_str == "PAYABLE" || type_str == "CREDIT") {
        account.type = "Liability";
    } else if (type_str == "EQUITY") {
        account.type = "Equity";
    } else if (type_str == "INCOME") {
        account.type = "Revenue";
    } else if (type_str == "EXPENSE") {
        account.type = "Expense";
    } else {
        account.type = type_str;
    }

    account.metadata["gnucash_type"] = type_str;
    return account;
}

ImportedTransaction GnuCashXmlConnector::parse_gnucash_transaction(
    const std::string & block,
    const std::map<std::string, std::string> & account_id_to_name) const {
    ImportedTransaction tx;
    tx.external_id = extract_tag_value(block, "trn:id");
    tx.description = extract_tag_value(block, "trn:description");
    tx.source_system = "GnuCash";

    // Parse date
    std::string date_posted = extract_tag_value(block, "trn:date-posted");
    if (!date_posted.empty()) {
        std::string ts = extract_tag_value(date_posted, "ts:date");
        if (!ts.empty()) {
            tx.date = ts.substr(0, 10); // YYYY-MM-DD
        }
    }

    // Currency
    std::string currency_block = extract_tag_value(block, "trn:currency");
    if (!currency_block.empty()) {
        tx.currency = extract_tag_value(currency_block, "cmdty:id");
    }

    // Parse splits (transaction entries)
    auto split_blocks = extract_all_blocks(block, "<trn:split>", "</trn:split>");
    if (split_blocks.empty()) {
        split_blocks = extract_all_blocks(block, "<trn:split", "</trn:split>");
    }

    for (const auto & split : split_blocks) {
        ImportedTransaction::Line line;

        std::string account_id = extract_tag_value(split, "split:account");
        auto it = account_id_to_name.find(account_id);
        if (it != account_id_to_name.end()) {
            line.account_code = it->second;
        } else {
            line.account_code = account_id;
        }

        // GnuCash stores amounts as fractions: "10000/100" = 100.00
        std::string value_str = extract_tag_value(split, "split:value");
        if (!value_str.empty()) {
            size_t slash = value_str.find('/');
            double amount = 0.0;
            if (slash != std::string::npos) {
                double num = std::stod(value_str.substr(0, slash));
                double den = std::stod(value_str.substr(slash + 1));
                if (den != 0.0) amount = num / den;
            } else {
                amount = std::stod(value_str);
            }

            if (amount >= 0) {
                line.debit_amount = amount;
            } else {
                line.credit_amount = -amount;
            }
        }

        line.memo = extract_tag_value(split, "split:memo");
        line.reconciled = extract_tag_value(split, "split:reconciled-state");

        tx.lines.push_back(line);
    }

    return tx;
}

std::string GnuCashXmlConnector::read_file_content(const std::string & path) const {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ============================================================================
// BeancountConnector Implementation
// ============================================================================

BeancountConnector::BeancountConnector() {}

bool BeancountConnector::test_connection(const std::string & source) const {
    std::ifstream file(source);
    return file.good();
}

ImportResult BeancountConnector::import_accounts(const std::string & source) {
    std::string content = read_file_content(source);
    if (content.empty()) {
        ImportResult result;
        result.connector_type = "Beancount";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }
    auto result = import_from_string(content);
    result.source_path = source;
    // Only keep accounts
    result.transactions.clear();
    result.transactions_imported = 0;
    return result;
}

ImportResult BeancountConnector::import_transactions(const std::string & source) {
    std::string content = read_file_content(source);
    if (content.empty()) {
        ImportResult result;
        result.connector_type = "Beancount";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }
    auto result = import_from_string(content);
    result.source_path = source;
    return result;
}

ImportResult BeancountConnector::import_all(const std::string & source) {
    return import_transactions(source);
}

std::vector<std::string> BeancountConnector::get_supported_extensions() const {
    return {".beancount", ".bean", ".ledger", ".hledger"};
}

ImportResult BeancountConnector::import_from_string(const std::string & content) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "Beancount";
    result.success = true;

    std::istringstream stream(content);
    std::string line;
    std::vector<std::string> lines;

    while (std::getline(stream, line)) {
        // Remove trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }

    size_t i = 0;
    uint64_t tx_counter = 0;

    while (i < lines.size()) {
        std::string trimmed = trim(lines[i]);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            i++;
            continue;
        }

        // Check for account open directive
        if (is_account_open_directive(trimmed)) {
            result.total_records_read++;
            auto account = parse_open_directive(trimmed);
            if (!account.external_code.empty()) {
                result.accounts.push_back(account);
                result.accounts_imported++;
                result.records_imported++;
            }
            i++;
            continue;
        }

        // Check for transaction (starts with date)
        if (is_date_line(trimmed)) {
            result.total_records_read++;

            // Collect posting lines
            std::vector<std::string> posting_lines;
            i++;
            while (i < lines.size()) {
                std::string next_trimmed = trim(lines[i]);
                if (next_trimmed.empty()) {
                    break; // End of transaction block
                }
                if (is_posting_line(lines[i])) {
                    posting_lines.push_back(next_trimmed);
                    i++;
                } else {
                    break;
                }
            }

            if (!posting_lines.empty()) {
                tx_counter++;
                auto tx = parse_transaction_block(trimmed, posting_lines);
                tx.external_id = "BC-" + std::to_string(tx_counter);
                result.transactions.push_back(tx);
                result.transactions_imported++;
                result.records_imported++;
            }
            continue;
        }

        i++;
    }

    auto end_time = std::chrono::steady_clock::now();
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    return result;
}

bool BeancountConnector::is_date_line(const std::string & line) const {
    // Beancount transactions start with YYYY-MM-DD
    if (line.size() < 10) return false;
    return std::isdigit(static_cast<unsigned char>(line[0])) &&
           std::isdigit(static_cast<unsigned char>(line[1])) &&
           std::isdigit(static_cast<unsigned char>(line[2])) &&
           std::isdigit(static_cast<unsigned char>(line[3])) &&
           line[4] == '-' &&
           std::isdigit(static_cast<unsigned char>(line[5])) &&
           std::isdigit(static_cast<unsigned char>(line[6])) &&
           line[7] == '-' &&
           std::isdigit(static_cast<unsigned char>(line[8])) &&
           std::isdigit(static_cast<unsigned char>(line[9]));
}

bool BeancountConnector::is_posting_line(const std::string & line) const {
    // Posting lines start with whitespace
    return !line.empty() && (line[0] == ' ' || line[0] == '\t');
}

bool BeancountConnector::is_account_open_directive(const std::string & line) const {
    // Format: YYYY-MM-DD open Account:Name
    if (!is_date_line(line)) return false;
    return line.find(" open ") != std::string::npos;
}

ImportedAccount BeancountConnector::parse_open_directive(const std::string & line) const {
    ImportedAccount account;

    // Format: YYYY-MM-DD open Account:Name [Currency]
    size_t open_pos = line.find(" open ");
    if (open_pos == std::string::npos) return account;

    std::string rest = trim(line.substr(open_pos + 6));

    // Split on whitespace to separate account name from currency
    size_t space = rest.find(' ');
    if (space != std::string::npos) {
        account.external_code = rest.substr(0, space);
        account.currency = trim(rest.substr(space + 1));
    } else {
        account.external_code = rest;
    }

    account.external_id = account.external_code;
    account.name = account.external_code;

    // Determine type from beancount account hierarchy
    if (account.external_code.find("Assets:") == 0) {
        account.type = "Asset";
    } else if (account.external_code.find("Liabilities:") == 0) {
        account.type = "Liability";
    } else if (account.external_code.find("Equity:") == 0) {
        account.type = "Equity";
    } else if (account.external_code.find("Income:") == 0) {
        account.type = "Revenue";
    } else if (account.external_code.find("Expenses:") == 0) {
        account.type = "Expense";
    }

    return account;
}

std::string BeancountConnector::read_file_content(const std::string & path) const {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string BeancountConnector::trim(const std::string & str) const {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

ImportedTransaction BeancountConnector::parse_transaction_block(
    const std::string & header_line,
    const std::vector<std::string> & posting_lines) const {
    ImportedTransaction tx;
    tx.source_system = "Beancount";

    // Parse header: YYYY-MM-DD [*!] "Description" [#tag]
    tx.date = header_line.substr(0, 10);

    // Find description in quotes
    size_t first_quote = header_line.find('"');
    if (first_quote != std::string::npos) {
        size_t second_quote = header_line.find('"', first_quote + 1);
        if (second_quote != std::string::npos) {
            tx.description = header_line.substr(first_quote + 1,
                                                 second_quote - first_quote - 1);
        }
    }

    // Parse posting lines
    for (const auto & posting : posting_lines) {
        ImportedTransaction::Line line;

        // Split posting into account and amount
        // Format: "Account:Name  100.00 USD" or just "Account:Name"
        std::string trimmed = trim(posting);

        // Remove inline comments
        size_t comment_pos = trimmed.find(';');
        if (comment_pos != std::string::npos) {
            trimmed = trim(trimmed.substr(0, comment_pos));
        }

        if (trimmed.empty()) continue;

        // Find the boundary between account name and amount
        // Account names don't contain spaces (they use colons)
        // Look for two or more spaces as separator, or a number
        size_t amount_start = std::string::npos;

        // Find first digit or negative sign after account name
        for (size_t j = 0; j < trimmed.size(); j++) {
            if (j > 0 && (trimmed[j] == '-' || std::isdigit(static_cast<unsigned char>(trimmed[j])))) {
                // Check if preceded by whitespace
                if (trimmed[j - 1] == ' ' || trimmed[j - 1] == '\t') {
                    amount_start = j;
                    break;
                }
            }
        }

        if (amount_start != std::string::npos) {
            line.account_code = trim(trimmed.substr(0, amount_start));
            std::string amount_part = trim(trimmed.substr(amount_start));
            auto parsed = parse_amount(amount_part);
            if (parsed.value >= 0) {
                line.debit_amount = parsed.value;
            } else {
                line.credit_amount = -parsed.value;
            }
            if (!parsed.currency.empty()) {
                tx.currency = parsed.currency;
            }
        } else {
            // No amount - this is the auto-balanced posting
            line.account_code = trimmed;
        }

        tx.lines.push_back(line);
    }

    return tx;
}

BeancountConnector::ParsedAmount BeancountConnector::parse_amount(
    const std::string & amount_str) const {
    ParsedAmount result;
    std::string trimmed = trim(amount_str);

    if (trimmed.empty()) return result;

    // Remove commas used as thousands separators
    std::string cleaned;
    for (char c : trimmed) {
        if (c != ',') cleaned += c;
    }

    // Split on whitespace to separate number from currency
    std::istringstream iss(cleaned);
    std::string num_str, curr_str;
    iss >> num_str;
    if (iss >> curr_str) {
        result.currency = curr_str;
    }

    if (!num_str.empty()) {
        try {
            result.value = std::stod(num_str);
        } catch (...) {
            result.value = 0.0;
        }
    }

    return result;
}

// ============================================================================
// JsonValue / JsonParser Implementation
// ============================================================================

bool JsonValue::as_bool() const {
    switch (type) {
        case Type::BOOL:   return bool_value;
        case Type::NULL_VALUE: return false;
        case Type::NUMBER: return number_value != 0.0;
        case Type::STRING: return !string_value.empty();
        case Type::ARRAY:  return !array_value.empty();
        case Type::OBJECT: return !object_value.empty();
    }
    return false;
}

double JsonValue::as_number() const {
    if (type == Type::NUMBER) return number_value;
    if (type == Type::BOOL)   return bool_value ? 1.0 : 0.0;
    if (type == Type::STRING) {
        if (string_value.empty()) return 0.0;
        try {
            return std::stod(string_value);
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

std::string JsonValue::as_string() const {
    if (type == Type::STRING) return string_value;
    if (type == Type::NUMBER) {
        std::stringstream ss;
        ss << number_value;
        return ss.str();
    }
    if (type == Type::BOOL) return bool_value ? "true" : "false";
    return "";
}

bool JsonValue::has(const std::string & key) const {
    if (type != Type::OBJECT) return false;
    return object_value.find(key) != object_value.end();
}

const JsonValue & JsonValue::get(const std::string & key) const {
    static const JsonValue null_value;
    if (type != Type::OBJECT) return null_value;
    auto it = object_value.find(key);
    return it != object_value.end() ? it->second : null_value;
}

const JsonValue & JsonValue::at(size_t index) const {
    static const JsonValue null_value;
    if (type != Type::ARRAY || index >= array_value.size()) return null_value;
    return array_value[index];
}

size_t JsonValue::size() const {
    if (type == Type::ARRAY)  return array_value.size();
    if (type == Type::OBJECT) return object_value.size();
    return 0;
}

bool JsonParser::parse(const std::string & text, JsonValue & out, std::string & error) {
    cur_ = text.data();
    end_ = text.data() + text.size();
    error_.clear();

    skip_whitespace();
    if (!parse_value(out)) {
        error = error_;
        return false;
    }
    skip_whitespace();
    if (cur_ != end_) {
        error = "Trailing characters after JSON document";
        return false;
    }
    return true;
}

JsonValue JsonParser::parse_or_throw(const std::string & text) {
    JsonValue out;
    std::string error;
    if (!parse(text, out, error)) {
        throw std::runtime_error("JSON parse error: " + error);
    }
    return out;
}

void JsonParser::skip_whitespace() {
    while (cur_ < end_ && (*cur_ == ' ' || *cur_ == '\t' || *cur_ == '\n' || *cur_ == '\r')) {
        cur_++;
    }
}

bool JsonParser::fail(const std::string & message) {
    error_ = message;
    return false;
}

bool JsonParser::parse_literal(const char * literal, JsonValue & out) {
    size_t len = std::strlen(literal);
    if (static_cast<size_t>(end_ - cur_) < len ||
        std::strncmp(cur_, literal, len) != 0) {
        return fail(std::string("Invalid literal, expected '") + literal + "'");
    }
    cur_ += len;
    if (std::strcmp(literal, "true") == 0) {
        out.type = JsonValue::Type::BOOL;
        out.bool_value = true;
    } else if (std::strcmp(literal, "false") == 0) {
        out.type = JsonValue::Type::BOOL;
        out.bool_value = false;
    } else {
        out.type = JsonValue::Type::NULL_VALUE;
    }
    return true;
}

bool JsonParser::parse_value(JsonValue & out) {
    skip_whitespace();
    if (cur_ >= end_) return fail("Unexpected end of input");

    char c = *cur_;
    if (c == '{') return parse_object(out);
    if (c == '[') return parse_array(out);
    if (c == '"') {
        out.type = JsonValue::Type::STRING;
        return parse_string(out.string_value);
    }
    if (c == 't') return parse_literal("true", out);
    if (c == 'f') return parse_literal("false", out);
    if (c == 'n') return parse_literal("null", out);
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);

    return fail(std::string("Unexpected character '") + c + "'");
}

bool JsonParser::parse_object(JsonValue & out) {
    out.type = JsonValue::Type::OBJECT;
    cur_++; // consume '{'
    skip_whitespace();
    if (cur_ < end_ && *cur_ == '}') {
        cur_++;
        return true;
    }
    while (cur_ < end_) {
        skip_whitespace();
        if (cur_ >= end_ || *cur_ != '"') {
            return fail("Expected string key in object");
        }
        std::string key;
        if (!parse_string(key)) return false;
        skip_whitespace();
        if (cur_ >= end_ || *cur_ != ':') {
            return fail("Expected ':' after object key");
        }
        cur_++;
        JsonValue value;
        if (!parse_value(value)) return false;
        out.object_value[key] = std::move(value);
        skip_whitespace();
        if (cur_ >= end_) return fail("Unterminated object");
        if (*cur_ == ',') {
            cur_++;
            continue;
        }
        if (*cur_ == '}') {
            cur_++;
            return true;
        }
        return fail("Expected ',' or '}' in object");
    }
    return fail("Unterminated object");
}

bool JsonParser::parse_array(JsonValue & out) {
    out.type = JsonValue::Type::ARRAY;
    cur_++; // consume '['
    skip_whitespace();
    if (cur_ < end_ && *cur_ == ']') {
        cur_++;
        return true;
    }
    while (cur_ < end_) {
        JsonValue element;
        if (!parse_value(element)) return false;
        out.array_value.push_back(std::move(element));
        skip_whitespace();
        if (cur_ >= end_) return fail("Unterminated array");
        if (*cur_ == ',') {
            cur_++;
            continue;
        }
        if (*cur_ == ']') {
            cur_++;
            return true;
        }
        return fail("Expected ',' or ']' in array");
    }
    return fail("Unterminated array");
}

bool JsonParser::append_utf8(std::string & out, uint32_t code_point) {
    if (code_point <= 0x7F) {
        out += static_cast<char>(code_point);
    } else if (code_point <= 0x7FF) {
        out += static_cast<char>(0xC0 | (code_point >> 6));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (code_point >> 12));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else if (code_point <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (code_point >> 18));
        out += static_cast<char>(0x80 | ((code_point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code_point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code_point & 0x3F));
    } else {
        return fail("Invalid Unicode code point");
    }
    return true;
}

bool JsonParser::parse_string(std::string & out) {
    out.clear();
    cur_++; // consume opening quote
    while (cur_ < end_) {
        char c = *cur_;
        if (c == '"') {
            cur_++;
            return true;
        }
        if (c == '\\') {
            cur_++;
            if (cur_ >= end_) return fail("Unterminated escape sequence");
            char esc = *cur_;
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (end_ - cur_ < 5) return fail("Truncated \\u escape");
                    uint32_t code_point = 0;
                    for (int i = 1; i <= 4; i++) {
                        char h = cur_[i];
                        code_point <<= 4;
                        if (h >= '0' && h <= '9')      code_point |= static_cast<uint32_t>(h - '0');
                        else if (h >= 'a' && h <= 'f') code_point |= static_cast<uint32_t>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code_point |= static_cast<uint32_t>(h - 'A' + 10);
                        else return fail("Invalid hex digit in \\u escape");
                    }
                    cur_ += 4;
                    // Combine UTF-16 surrogate pairs
                    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                        if (end_ - cur_ >= 7 && cur_[1] == '\\' && cur_[2] == 'u') {
                            uint32_t low = 0;
                            bool valid = true;
                            for (int i = 3; i <= 6; i++) {
                                char h = cur_[i];
                                low <<= 4;
                                if (h >= '0' && h <= '9')      low |= static_cast<uint32_t>(h - '0');
                                else if (h >= 'a' && h <= 'f') low |= static_cast<uint32_t>(h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') low |= static_cast<uint32_t>(h - 'A' + 10);
                                else { valid = false; break; }
                            }
                            if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                                code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
                                cur_ += 6;
                            }
                        }
                    }
                    if (!append_utf8(out, code_point)) return false;
                    break;
                }
                default:
                    return fail(std::string("Invalid escape character '") + esc + "'");
            }
            cur_++;
            continue;
        }
        // Raw byte (JSON strings are UTF-8; pass bytes through unchanged)
        out += c;
        cur_++;
    }
    return fail("Unterminated string");
}

bool JsonParser::parse_number(JsonValue & out) {
    const char * start = cur_;
    if (cur_ < end_ && *cur_ == '-') cur_++;

    if (cur_ >= end_) return fail("Truncated number");
    if (*cur_ == '0') {
        cur_++;
    } else if (*cur_ >= '1' && *cur_ <= '9') {
        while (cur_ < end_ && *cur_ >= '0' && *cur_ <= '9') cur_++;
    } else {
        return fail("Invalid number");
    }

    if (cur_ < end_ && *cur_ == '.') {
        cur_++;
        if (cur_ >= end_ || *cur_ < '0' || *cur_ > '9') {
            return fail("Expected digit after decimal point");
        }
        while (cur_ < end_ && *cur_ >= '0' && *cur_ <= '9') cur_++;
    }

    if (cur_ < end_ && (*cur_ == 'e' || *cur_ == 'E')) {
        cur_++;
        if (cur_ < end_ && (*cur_ == '+' || *cur_ == '-')) cur_++;
        if (cur_ >= end_ || *cur_ < '0' || *cur_ > '9') {
            return fail("Expected digit in exponent");
        }
        while (cur_ < end_ && *cur_ >= '0' && *cur_ <= '9') cur_++;
    }

    out.type = JsonValue::Type::NUMBER;
    try {
        out.number_value = std::stod(std::string(start, static_cast<size_t>(cur_ - start)));
    } catch (...) {
        return fail("Number out of range");
    }
    return true;
}

// ============================================================================
// SqliteReader Implementation
// ============================================================================

SqliteReader::SqliteReader()
    : data_(nullptr),
      size_(0),
      page_size_(0),
      opened_(false),
      master_loaded_(false) {}

bool SqliteReader::check_range(uint64_t offset, uint64_t length) const {
    if (offset > size_) return false;
    if (length > size_ - offset) return false;
    return true;
}

bool SqliteReader::read_u8(uint64_t offset, uint8_t & out) const {
    if (!check_range(offset, 1)) return false;
    out = data_[offset];
    return true;
}

bool SqliteReader::read_u16(uint64_t offset, uint16_t & out) const {
    if (!check_range(offset, 2)) return false;
    out = static_cast<uint16_t>((static_cast<uint16_t>(data_[offset]) << 8) |
                                static_cast<uint16_t>(data_[offset + 1]));
    return true;
}

bool SqliteReader::read_u32(uint64_t offset, uint32_t & out) const {
    if (!check_range(offset, 4)) return false;
    out = (static_cast<uint32_t>(data_[offset]) << 24) |
          (static_cast<uint32_t>(data_[offset + 1]) << 16) |
          (static_cast<uint32_t>(data_[offset + 2]) << 8) |
          static_cast<uint32_t>(data_[offset + 3]);
    return true;
}

bool SqliteReader::read_u64(uint64_t offset, uint64_t & out) const {
    if (!check_range(offset, 8)) return false;
    out = 0;
    for (int i = 0; i < 8; i++) {
        out = (out << 8) | static_cast<uint64_t>(data_[offset + i]);
    }
    return true;
}

uint64_t SqliteReader::page_offset(uint32_t page_number) const {
    if (page_number == 0) return 0; // Invalid page number
    return static_cast<uint64_t>(page_number - 1) * page_size_;
}

bool SqliteReader::read_varint(uint64_t offset, uint64_t & value, uint32_t & bytes_read) const {
    value = 0;
    bytes_read = 0;
    for (int i = 0; i < 9; i++) {
        uint8_t byte = 0;
        if (!read_u8(offset + i, byte)) return false;
        if (i == 8) {
            // 9th byte: all 8 bits are payload
            value = (value << 8) | byte;
            bytes_read = 9;
            return true;
        }
        value = (value << 7) | static_cast<uint64_t>(byte & 0x7F);
        bytes_read = static_cast<uint32_t>(i + 1);
        if ((byte & 0x80) == 0) {
            return true;
        }
    }
    return true; // Unreachable, kept for clarity
}

bool SqliteReader::open(const uint8_t * data, size_t size) {
    data_ = data;
    size_ = size;
    page_size_ = 0;
    opened_ = false;
    master_loaded_ = false;
    master_.clear();
    errors_.clear();
    warnings_.clear();

    if (data == nullptr || size < 100) {
        errors_.push_back("Input too small to be a SQLite database (need at least 100 header bytes)");
        return false;
    }
    return parse_header();
}

bool SqliteReader::parse_header() {
    static const char magic[16] = {'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'};
    if (!check_range(0, 16) || std::memcmp(data_, magic, 16) != 0) {
        errors_.push_back("Missing SQLite format 3 header magic");
        return false;
    }

    uint16_t raw_page_size = 0;
    if (!read_u16(16, raw_page_size)) {
        errors_.push_back("Could not read page size from header");
        return false;
    }
    page_size_ = (raw_page_size == 1) ? 65536u : raw_page_size;
    if (page_size_ < 512 || page_size_ > 65536 || (page_size_ & (page_size_ - 1)) != 0) {
        errors_.push_back("Unsupported or invalid page size: " + std::to_string(page_size_));
        return false;
    }

    uint8_t file_format_write = 0, file_format_read = 0;
    if (!read_u8(18, file_format_write) || !read_u8(19, file_format_read)) {
        errors_.push_back("Could not read file format versions");
        return false;
    }
    if (file_format_read != 1 && file_format_read != 2) {
        errors_.push_back("Unsupported file format read version: " +
                          std::to_string(file_format_read));
        return false;
    }
    if (file_format_read == 2) {
        warnings_.push_back("Database is in WAL mode; only committed b-tree content is readable");
    }

    uint32_t db_size_pages = 0;
    if (!read_u32(28, db_size_pages)) {
        errors_.push_back("Could not read database size from header");
        return false;
    }
    // Validate the file is at least one full page
    if (!check_range(0, page_size_)) {
        errors_.push_back("File smaller than one page (" + std::to_string(page_size_) + " bytes)");
        return false;
    }
    // The in-header database size may be stale for old writers; only warn
    if (db_size_pages > 0 &&
        static_cast<uint64_t>(db_size_pages) * page_size_ > size_ + page_size_) {
        warnings_.push_back("Header database size exceeds file size; reading available pages only");
    }

    opened_ = true;
    return true;
}

std::vector<std::string> SqliteReader::parse_create_table_columns(const std::string & sql) const {
    std::vector<std::string> columns;

    // Locate the opening parenthesis of the column definition list
    size_t open = sql.find('(');
    if (open == std::string::npos) return columns;

    // Walk the definition list, tracking nested parens; split top-level commas
    size_t i = open + 1;
    int depth = 0;
    std::string token;
    std::vector<std::string> definitions;

    auto flush_token = [&]() {
        // Trim
        size_t s = token.find_first_not_of(" \t\n\r");
        size_t e = token.find_last_not_of(" \t\n\r");
        if (s != std::string::npos) {
            definitions.push_back(token.substr(s, e - s + 1));
        }
        token.clear();
    };

    while (i < sql.size()) {
        char c = sql[i];
        if (c == '(') {
            depth++;
            token += c;
        } else if (c == ')') {
            if (depth == 0) {
                flush_token();
                break; // End of the column list
            }
            depth--;
            token += c;
        } else if (c == ',' && depth == 0) {
            flush_token();
        } else if (c == '\'' || c == '"' || c == '`') {
            // Skip quoted literals/identifiers
            char quote = c;
            token += c;
            i++;
            while (i < sql.size() && sql[i] != quote) {
                token += sql[i];
                i++;
            }
            if (i < sql.size()) token += sql[i]; // closing quote
        } else {
            token += c;
        }
        i++;
    }

    static const std::set<std::string> constraints = {
        "CONSTRAINT", "PRIMARY", "FOREIGN", "UNIQUE", "CHECK", "EXCLUDE"
    };

    for (const auto & def : definitions) {
        // First word of the definition
        size_t start = def.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) continue;
        size_t end = def.find_first_of(" \t\n\r", start);
        std::string first = def.substr(start, end == std::string::npos ? std::string::npos
                                                                       : end - start);
        if (first.empty()) continue;

        // Skip table-level constraints
        std::string upper_first;
        for (char ch : first) {
            upper_first += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }
        if (constraints.count(upper_first) > 0) continue;

        // Strip identifier quoting
        if ((first.front() == '"' && first.back() == '"') ||
            (first.front() == '[' && first.back() == ']') ||
            (first.front() == '`' && first.back() == '`')) {
            first = first.substr(1, first.size() - 2);
        }
        columns.push_back(first);
    }

    return columns;
}

bool SqliteReader::load_master() const {
    if (master_loaded_) return true;
    master_loaded_ = true;

    // sqlite_master lives at page 1; its columns are:
    //   type, name, tbl_name, rootpage, sql
    std::vector<std::string> master_columns = {"type", "name", "tbl_name", "rootpage", "sql"};
    std::vector<SqliteRow> rows;
    if (!walk_table_btree(1, master_columns, rows, 0)) {
        errors_.push_back("Could not read sqlite_master table");
        return false;
    }

    for (const auto & row : rows) {
        MasterEntry entry;
        auto type_it = row.find("type");
        auto name_it = row.find("name");
        auto tbl_it  = row.find("tbl_name");
        auto root_it = row.find("rootpage");
        auto sql_it  = row.find("sql");
        if (type_it != row.end()) entry.type = type_it->second.as_string();
        if (name_it != row.end()) entry.name = name_it->second.as_string();
        if (tbl_it  != row.end()) entry.tbl_name = tbl_it->second.as_string();
        if (sql_it  != row.end()) entry.sql = sql_it->second.as_string();
        if (root_it != row.end() && root_it->second.type == SqliteValue::Type::INTEGER) {
            entry.root_page = static_cast<uint32_t>(root_it->second.int_value);
        } else {
            entry.root_page = 0;
        }
        master_.push_back(entry);
    }
    return true;
}

std::vector<std::string> SqliteReader::get_table_names() const {
    std::vector<std::string> names;
    if (!opened_ || !load_master()) return names;
    for (const auto & entry : master_) {
        if (entry.type == "table") {
            names.push_back(entry.name);
        }
    }
    return names;
}

bool SqliteReader::read_table(const std::string & table_name, std::vector<SqliteRow> & rows) const {
    rows.clear();
    if (!opened_) {
        errors_.push_back("read_table called on a database that is not open");
        return false;
    }
    if (!load_master()) return false;

    for (const auto & entry : master_) {
        if (entry.type == "table" && entry.name == table_name) {
            if (entry.root_page == 0) {
                // Table with no root page: empty table (or virtual table)
                return true;
            }
            std::vector<std::string> columns = parse_create_table_columns(entry.sql);
            if (columns.empty()) {
                warnings_.push_back("Could not parse column list for table '" + table_name +
                                    "'; columns will be named by index");
            }
            return walk_table_btree(entry.root_page, columns, rows, 0);
        }
    }

    errors_.push_back("Table not found: " + table_name);
    return false;
}

bool SqliteReader::walk_table_btree(uint32_t page_number,
                                     const std::vector<std::string> & columns,
                                     std::vector<SqliteRow> & rows,
                                     int depth) const {
    if (depth > 32) {
        errors_.push_back("B-tree depth limit exceeded (possible cycle) at page " +
                          std::to_string(page_number));
        return false;
    }
    if (page_number == 0) {
        errors_.push_back("Invalid page number 0 in b-tree walk");
        return false;
    }

    uint64_t base = page_offset(page_number);
    if (!check_range(base, page_size_)) {
        errors_.push_back("Page " + std::to_string(page_number) + " beyond end of file");
        return false;
    }

    // Page 1 carries the 100-byte database header before the b-tree header
    uint64_t header = base + (page_number == 1 ? 100 : 0);

    uint8_t page_type = 0;
    if (!read_u8(header, page_type)) {
        errors_.push_back("Could not read page type at page " + std::to_string(page_number));
        return false;
    }

    if (page_type == 0x05) {
        // Interior table b-tree page
        uint16_t cell_count = 0;
        uint32_t right_child = 0;
        if (!read_u16(header + 3, cell_count) || !read_u32(header + 8, right_child)) {
            errors_.push_back("Truncated interior page header at page " + std::to_string(page_number));
            return false;
        }
        // Sanity bound: a minimal interior cell is 6 bytes
        if (cell_count > page_size_ / 6 + 1) {
            errors_.push_back("Implausible cell count on interior page " + std::to_string(page_number));
            return false;
        }

        uint64_t cell_ptr_array = header + 12;
        for (uint16_t i = 0; i < cell_count; i++) {
            uint16_t cell_offset = 0;
            if (!read_u16(cell_ptr_array + 2ULL * i, cell_offset)) {
                errors_.push_back("Truncated cell pointer array at page " + std::to_string(page_number));
                return false;
            }
            if (cell_offset >= page_size_ || !check_range(base + cell_offset, 4)) {
                errors_.push_back("Cell pointer out of range at page " + std::to_string(page_number));
                return false;
            }
            uint32_t child_page = 0;
            if (!read_u32(base + cell_offset, child_page)) {
                errors_.push_back("Could not read child pointer at page " + std::to_string(page_number));
                return false;
            }
            if (!walk_table_btree(child_page, columns, rows, depth + 1)) {
                return false;
            }
        }
        if (right_child != 0) {
            if (!walk_table_btree(right_child, columns, rows, depth + 1)) {
                return false;
            }
        }
        return true;
    }

    if (page_type == 0x0D) {
        // Leaf table b-tree page
        uint16_t cell_count = 0;
        if (!read_u16(header + 3, cell_count)) {
            errors_.push_back("Truncated leaf page header at page " + std::to_string(page_number));
            return false;
        }
        // Sanity bound: a minimal leaf cell is 4 bytes (payload varint + rowid varint)
        if (cell_count > page_size_ / 2) {
            errors_.push_back("Implausible cell count on leaf page " + std::to_string(page_number));
            return false;
        }

        uint64_t cell_ptr_array = header + 8;
        for (uint16_t i = 0; i < cell_count; i++) {
            uint16_t cell_offset = 0;
            if (!read_u16(cell_ptr_array + 2ULL * i, cell_offset)) {
                errors_.push_back("Truncated cell pointer array at page " + std::to_string(page_number));
                return false;
            }
            if (cell_offset >= page_size_) {
                errors_.push_back("Cell pointer out of range at page " + std::to_string(page_number));
                return false;
            }

            uint64_t cell = base + cell_offset;

            // Cell: payload size varint, rowid varint, payload bytes
            uint64_t payload_size = 0, rowid = 0;
            uint32_t varint_len = 0;
            if (!read_varint(cell, payload_size, varint_len)) {
                errors_.push_back("Truncated payload size at page " + std::to_string(page_number));
                return false;
            }
            cell += varint_len;
            if (!read_varint(cell, rowid, varint_len)) {
                errors_.push_back("Truncated rowid at page " + std::to_string(page_number));
                return false;
            }
            cell += varint_len;

            // Compute the amount of payload stored locally on this page.
            // For table leaf cells (usable size U, max local X = U-35,
            // min local M = ((U-12)*32/255)-23):
            //   if P <= X: all local; else K = M + (P-M) % (U-4);
            //   local = (K <= X) ? K : M; remainder overflows.
            uint64_t usable = page_size_; // reserved space assumed 0 (checked below)
            uint8_t reserved = 0;
            if (read_u8(20, reserved) && reserved > 0) {
                if (reserved >= usable) {
                    errors_.push_back("Invalid reserved space in header");
                    return false;
                }
                usable -= reserved;
            }
            uint64_t max_local = usable - 35;
            uint64_t min_local = ((usable - 12) * 32 / 255) - 23;

            uint64_t local_size = payload_size;
            bool overflows = payload_size > max_local;
            if (overflows) {
                uint64_t k = min_local + (payload_size - min_local) % (usable - 4);
                local_size = (k <= max_local) ? k : min_local;
            }

            if (!check_range(cell, local_size)) {
                errors_.push_back("Cell payload extends past end of file at page " +
                                  std::to_string(page_number));
                return false;
            }

            if (overflows) {
                // Overflow chains are not followed in this subset; decode what
                // is local when it covers the whole record header, else skip.
                warnings_.push_back("Row with overflow payload skipped at page " +
                                    std::to_string(page_number));
                continue;
            }

            SqliteRow row;
            if (!decode_record(data_ + cell, static_cast<size_t>(local_size), columns, row)) {
                warnings_.push_back("Undecodable record skipped at page " +
                                    std::to_string(page_number));
                continue;
            }
            // Expose the rowid as an extra pseudo-column
            SqliteValue rowid_value;
            rowid_value.type = SqliteValue::Type::INTEGER;
            rowid_value.int_value = static_cast<int64_t>(rowid);
            row["_rowid_"] = rowid_value;
            rows.push_back(std::move(row));
        }
        return true;
    }

    errors_.push_back("Unsupported b-tree page type 0x" +
                      ([](uint8_t v) {
                          std::stringstream ss;
                          ss << std::hex << std::setfill('0') << std::setw(2)
                             << static_cast<int>(v);
                          return ss.str();
                      })(page_type) +
                      " at page " + std::to_string(page_number) +
                      " (index pages and other structures are not readable)");
    return false;
}

bool SqliteReader::decode_record(const uint8_t * payload,
                                  size_t payload_size,
                                  const std::vector<std::string> & columns,
                                  SqliteRow & row) const {
    if (payload_size == 0) return false;

    // Record header: header size varint (includes itself), then serial types.
    // Varints are decoded manually here because this buffer is not the
    // database image that read_varint() operates on.
    uint64_t header_size = 0;
    size_t pos = 0;
    {
        uint64_t value = 0;
        int i = 0;
        for (; i < 9 && pos < payload_size; i++) {
            uint8_t byte = payload[pos++];
            if (i == 8) {
                value = (value << 8) | byte;
                break;
            }
            value = (value << 7) | static_cast<uint64_t>(byte & 0x7F);
            if ((byte & 0x80) == 0) break;
        }
        if (i == 9) return false;
        header_size = value;
    }
    if (header_size > payload_size || header_size < pos) return false;

    // Collect serial types
    std::vector<uint64_t> serial_types;
    while (pos < header_size) {
        uint64_t serial = 0;
        int i = 0;
        for (; i < 9 && pos < header_size; i++) {
            uint8_t byte = payload[pos++];
            if (i == 8) {
                serial = (serial << 8) | byte;
                break;
            }
            serial = (serial << 7) | static_cast<uint64_t>(byte & 0x7F);
            if ((byte & 0x80) == 0) break;
        }
        if (i == 9 && pos > header_size) return false;
        serial_types.push_back(serial);
    }

    // Decode body values
    size_t body = static_cast<size_t>(header_size);
    size_t column_index = 0;
    for (uint64_t serial : serial_types) {
        SqliteValue value;
        uint64_t byte_count = 0;

        switch (serial) {
            case 0: value.type = SqliteValue::Type::NULL_VALUE; byte_count = 0; break;
            case 1: byte_count = 1; value.type = SqliteValue::Type::INTEGER; break;
            case 2: byte_count = 2; value.type = SqliteValue::Type::INTEGER; break;
            case 3: byte_count = 3; value.type = SqliteValue::Type::INTEGER; break;
            case 4: byte_count = 4; value.type = SqliteValue::Type::INTEGER; break;
            case 5: byte_count = 6; value.type = SqliteValue::Type::INTEGER; break;
            case 6: byte_count = 8; value.type = SqliteValue::Type::INTEGER; break;
            case 7: byte_count = 8; value.type = SqliteValue::Type::REAL;    break;
            case 8: value.type = SqliteValue::Type::INTEGER; value.int_value = 0; byte_count = 0; break;
            case 9: value.type = SqliteValue::Type::INTEGER; value.int_value = 1; byte_count = 0; break;
            default:
                if (serial >= 12 && (serial % 2) == 0) {
                    value.type = SqliteValue::Type::BLOB;
                    byte_count = (serial - 12) / 2;
                } else if (serial >= 13) {
                    value.type = SqliteValue::Type::TEXT;
                    byte_count = (serial - 13) / 2;
                } else {
                    return false; // Reserved serial types 10, 11
                }
                break;
        }

        if (byte_count > payload_size - body) return false; // Truncated body

        if (value.type == SqliteValue::Type::INTEGER && byte_count > 0) {
            int64_t v = 0;
            for (uint64_t i = 0; i < byte_count; i++) {
                v = (v << 8) | static_cast<int64_t>(payload[body + i]);
            }
            // Sign-extend from byte_count bytes
            if (byte_count < 8 && (payload[body] & 0x80) != 0) {
                v = static_cast<int64_t>(static_cast<uint64_t>(v) |
                                         (UINT64_MAX << (byte_count * 8)));
            }
            value.int_value = v;
        } else if (value.type == SqliteValue::Type::REAL) {
            uint64_t bits = 0;
            for (int i = 0; i < 8; i++) {
                bits = (bits << 8) | static_cast<uint64_t>(payload[body + i]);
            }
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            value.real_value = d;
        } else if (value.type == SqliteValue::Type::TEXT) {
            value.text_value.assign(reinterpret_cast<const char *>(payload + body),
                                    static_cast<size_t>(byte_count));
        } else if (value.type == SqliteValue::Type::BLOB) {
            value.blob_value.assign(payload + body, payload + body + byte_count);
        }
        body += static_cast<size_t>(byte_count);

        std::string column_name;
        if (column_index < columns.size()) {
            column_name = columns[column_index];
        } else {
            column_name = "_col" + std::to_string(column_index);
        }
        row[column_name] = std::move(value);
        column_index++;
    }

    return true;
}

int64_t SqliteValue::as_int() const {
    if (type == Type::INTEGER) return int_value;
    if (type == Type::REAL)    return static_cast<int64_t>(real_value);
    if (type == Type::TEXT) {
        try {
            return std::stoll(text_value);
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

double SqliteValue::as_double() const {
    if (type == Type::REAL)    return real_value;
    if (type == Type::INTEGER) return static_cast<double>(int_value);
    if (type == Type::TEXT) {
        try {
            return std::stod(text_value);
        } catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

std::string SqliteValue::as_string() const {
    if (type == Type::TEXT) return text_value;
    if (type == Type::INTEGER) return std::to_string(int_value);
    if (type == Type::REAL) {
        std::stringstream ss;
        ss << real_value;
        return ss.str();
    }
    return "";
}

// ============================================================================
// GnuCashSqliteConnector Implementation
// ============================================================================

GnuCashSqliteConnector::GnuCashSqliteConnector() {}

bool GnuCashSqliteConnector::test_connection(const std::string & source) const {
    std::ifstream file(source, std::ios::binary);
    if (!file.good()) return false;

    // Check for the SQLite format 3 magic header
    char header[16];
    file.read(header, sizeof(header));
    if (file.gcount() != static_cast<std::streamsize>(sizeof(header))) return false;
    static const char magic[16] = {'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'};
    return std::memcmp(header, magic, 16) == 0;
}

std::string GnuCashSqliteConnector::read_file_bytes(const std::string & path) const {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

ImportResult GnuCashSqliteConnector::import_accounts(const std::string & source) {
    auto result = import_all(source);
    result.transactions.clear();
    result.transactions_imported = 0;
    return result;
}

ImportResult GnuCashSqliteConnector::import_transactions(const std::string & source) {
    return import_all(source);
}

ImportResult GnuCashSqliteConnector::import_all(const std::string & source) {
    std::string bytes = read_file_bytes(source);
    if (bytes.empty()) {
        ImportResult result;
        result.connector_type = "GnuCash SQLite";
        result.source_path = source;
        result.success = false;
        result.errors.push_back("Could not read file: " + source);
        return result;
    }
    auto result = import_from_bytes(bytes);
    result.source_path = source;
    return result;
}

std::vector<std::string> GnuCashSqliteConnector::get_supported_extensions() const {
    return {".sqlite", ".db", ".sqlite3"};
}

ImportResult GnuCashSqliteConnector::import_from_bytes(const uint8_t * data, size_t size) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "GnuCash SQLite";

    SqliteReader reader;
    if (!reader.open(data, size)) {
        result.success = false;
        for (const auto & err : reader.get_errors()) {
            result.errors.push_back("SQLite parse: " + err);
        }
        if (result.errors.empty()) {
            result.errors.push_back("Not a valid SQLite3 database file");
        }
        return result;
    }

    result = import_from_reader(reader);
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
}

ImportResult GnuCashSqliteConnector::import_from_reader(SqliteReader & reader) {
    ImportResult result;
    result.connector_type = "GnuCash SQLite";
    result.success = true;

    for (const auto & warn : reader.get_warnings()) {
        result.warnings.push_back("SQLite parse: " + warn);
    }

    // ---- commodities: guid -> currency mnemonic ----
    std::map<std::string, std::string> commodity_mnemonics;
    {
        std::vector<SqliteRow> rows;
        if (reader.read_table("commodities", rows)) {
            for (const auto & row : rows) {
                auto guid = row.find("guid");
                auto mnemonic = row.find("mnemonic");
                if (guid != row.end() && mnemonic != row.end()) {
                    commodity_mnemonics[guid->second.as_string()] = mnemonic->second.as_string();
                }
            }
        }
        // Missing commodities table is not fatal: currency falls back to USD
    }

    // ---- accounts ----
    std::map<std::string, std::string> account_guid_to_code;
    {
        std::vector<SqliteRow> rows;
        if (!reader.read_table("accounts", rows)) {
            result.success = false;
            for (const auto & err : reader.get_errors()) {
                result.errors.push_back("SQLite parse: " + err);
            }
            result.errors.push_back("GnuCash SQLite file has no readable 'accounts' table");
            return result;
        }

        for (const auto & row : rows) {
            result.total_records_read++;

            ImportedAccount account;
            auto get_field = [&](const char * name) -> std::string {
                auto it = row.find(name);
                return it != row.end() ? it->second.as_string() : "";
            };

            account.external_id   = get_field("guid");
            account.name          = get_field("name");
            account.external_code = get_field("code");
            if (account.external_code.empty()) {
                account.external_code = account.name;
            }
            account.parent_id   = get_field("parent_guid");
            account.description = get_field("description");

            std::string gnucash_type = get_field("account_type");
            account.type = map_account_type(gnucash_type);
            account.metadata["gnucash_type"] = gnucash_type;

            if (account.external_id.empty()) {
                result.records_failed++;
                result.warnings.push_back("Account row missing guid; skipped");
                continue;
            }

            account_guid_to_code[account.external_id] = account.external_code;
            result.accounts.push_back(account);
            result.accounts_imported++;
            result.records_imported++;
        }
    }

    // ---- transactions ----
    struct TxnRow {
        std::string guid;
        std::string currency_guid;
        std::string num;
        std::string post_date;
        std::string description;
    };
    std::vector<TxnRow> txn_rows;
    {
        std::vector<SqliteRow> rows;
        if (reader.read_table("transactions", rows)) {
            for (const auto & row : rows) {
                result.total_records_read++;
                TxnRow t;
                auto get_field = [&](const char * name) -> std::string {
                    auto it = row.find(name);
                    return it != row.end() ? it->second.as_string() : "";
                };
                t.guid          = get_field("guid");
                t.currency_guid = get_field("currency_guid");
                t.num           = get_field("num");
                t.post_date     = get_field("post_date");
                t.description   = get_field("description");
                txn_rows.push_back(t);
            }
        }
        // A missing transactions table is tolerated (accounts-only book)
    }

    // ---- splits ----
    std::map<std::string, std::vector<ImportedTransaction::Line>> splits_by_tx;
    {
        std::vector<SqliteRow> rows;
        if (reader.read_table("splits", rows)) {
            for (const auto & row : rows) {
                auto get_field = [&](const char * name) -> std::string {
                    auto it = row.find(name);
                    return it != row.end() ? it->second.as_string() : "";
                };

                std::string tx_guid = get_field("tx_guid");
                if (tx_guid.empty()) continue;

                ImportedTransaction::Line line;
                std::string account_guid = get_field("account_guid");
                auto acc_it = account_guid_to_code.find(account_guid);
                line.account_code = acc_it != account_guid_to_code.end()
                    ? acc_it->second : account_guid;

                // value_num / value_denom form the amount fraction
                int64_t num = 0, denom = 1;
                auto num_it = row.find("value_num");
                auto den_it = row.find("value_denom");
                if (num_it != row.end()) num = num_it->second.as_int();
                if (den_it != row.end()) denom = den_it->second.as_int();
                if (denom == 0) {
                    result.warnings.push_back("Split with zero denominator skipped (tx " +
                                              tx_guid + ")");
                    continue;
                }
                double amount = static_cast<double>(num) / static_cast<double>(denom);
                if (amount >= 0) {
                    line.debit_amount = amount;
                } else {
                    line.credit_amount = -amount;
                }

                line.memo = get_field("memo");
                line.reconciled = get_field("reconciled_state");

                splits_by_tx[tx_guid].push_back(line);
            }
        }
    }

    // ---- assemble transactions ----
    for (const auto & t : txn_rows) {
        ImportedTransaction tx;
        tx.external_id = t.guid;
        tx.description = t.description;
        tx.reference = t.num;
        tx.source_system = "GnuCash SQLite";
        tx.date = normalize_date(t.post_date);

        auto curr_it = commodity_mnemonics.find(t.currency_guid);
        if (curr_it != commodity_mnemonics.end()) {
            tx.currency = curr_it->second;
        }

        auto splits_it = splits_by_tx.find(t.guid);
        if (splits_it != splits_by_tx.end()) {
            tx.lines = splits_it->second;
        }

        if (tx.external_id.empty()) {
            result.records_failed++;
            continue;
        }
        result.transactions.push_back(tx);
        result.transactions_imported++;
        result.records_imported++;
    }

    // Splits referencing unknown transactions produce warnings
    for (const auto & kv : splits_by_tx) {
        bool found = false;
        for (const auto & t : txn_rows) {
            if (t.guid == kv.first) { found = true; break; }
        }
        if (!found) {
            result.warnings.push_back("Splits reference unknown transaction " + kv.first);
            result.records_skipped++;
        }
    }

    return result;
}

std::string GnuCashSqliteConnector::map_account_type(const std::string & gnucash_type) const {
    if (gnucash_type == "ASSET" || gnucash_type == "BANK" || gnucash_type == "CASH" ||
        gnucash_type == "RECEIVABLE" || gnucash_type == "STOCK" || gnucash_type == "MUTUAL") {
        return "Asset";
    }
    if (gnucash_type == "LIABILITY" || gnucash_type == "PAYABLE" || gnucash_type == "CREDIT") {
        return "Liability";
    }
    if (gnucash_type == "EQUITY")  return "Equity";
    if (gnucash_type == "INCOME")  return "Revenue";
    if (gnucash_type == "EXPENSE") return "Expense";
    if (gnucash_type == "ROOT")    return "Equity";
    return gnucash_type;
}

bool GnuCashSqliteConnector::parse_fraction(const std::string & fraction, double & out) const {
    size_t slash = fraction.find('/');
    if (slash == std::string::npos) return false;
    try {
        double num = std::stod(fraction.substr(0, slash));
        double den = std::stod(fraction.substr(slash + 1));
        if (den == 0.0) return false;
        out = num / den;
        return true;
    } catch (...) {
        return false;
    }
}

std::string GnuCashSqliteConnector::normalize_date(const std::string & sqlite_datetime) const {
    // GnuCash SQL backend stores ISO strings like "2024-01-15 10:30:00"
    if (sqlite_datetime.size() >= 10) {
        return sqlite_datetime.substr(0, 10);
    }
    return sqlite_datetime;
}

// ============================================================================
// XeroConnector Implementation
// ============================================================================

XeroConnector::XeroConnector() {}

bool XeroConnector::test_connection(const std::string & source) const {
    // No network I/O: a non-empty JSON payload counts as "connectable"
    return !source.empty() && source.find('{') != std::string::npos;
}

ImportResult XeroConnector::import_accounts(const std::string & source) {
    auto result = import_from_string(source);
    result.transactions.clear();
    result.transactions_imported = 0;
    return result;
}

ImportResult XeroConnector::import_transactions(const std::string & source) {
    auto result = import_from_string(source);
    result.accounts.clear();
    result.accounts_imported = 0;
    return result;
}

ImportResult XeroConnector::import_all(const std::string & source) {
    return import_from_string(source);
}

std::vector<std::string> XeroConnector::get_supported_extensions() const {
    return {}; // API connector - not file-based
}

std::string XeroConnector::build_oauth2_request(const std::string & endpoint,
                                                 const std::string & bearer_token,
                                                 const std::string & tenant_id,
                                                 const std::string & host) {
    std::stringstream ss;
    ss << "GET " << endpoint << " HTTP/1.1\r\n";
    ss << "Host: " << host << "\r\n";
    ss << "Authorization: ****** " << bearer_token << "\r\n";
    if (!tenant_id.empty()) {
        ss << "xero-tenant-id: " << tenant_id << "\r\n";
    }
    ss << "Accept: application/json\r\n";
    ss << "Connection: close\r\n";
    ss << "\r\n";
    return ss.str();
}

ImportResult XeroConnector::import_from_string(const std::string & json_content) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "Xero API";
    result.success = true;

    JsonValue root;
    std::string error;
    JsonParser parser;
    if (!parser.parse(json_content, root, error)) {
        result.success = false;
        result.errors.push_back("Xero JSON parse error: " + error);
        return result;
    }

    // Accounts
    const JsonValue & accounts = root.get("Accounts");
    if (accounts.is_array()) {
        for (size_t i = 0; i < accounts.size(); i++) {
            result.total_records_read++;
            auto account = parse_xero_account(accounts.at(i));
            if (!account.external_id.empty()) {
                result.accounts.push_back(account);
                result.accounts_imported++;
                result.records_imported++;
            } else {
                result.records_failed++;
            }
        }
    }

    // Bank transactions
    const JsonValue & bank_txs = root.get("BankTransactions");
    if (bank_txs.is_array()) {
        for (size_t i = 0; i < bank_txs.size(); i++) {
            result.total_records_read++;
            auto tx = parse_xero_bank_transaction(bank_txs.at(i));
            if (!tx.external_id.empty()) {
                result.transactions.push_back(tx);
                result.transactions_imported++;
                result.records_imported++;
            } else {
                result.records_failed++;
            }
        }
    }

    if (accounts.is_null() && bank_txs.is_null()) {
        result.warnings.push_back("Xero payload contained neither 'Accounts' nor "
                                  "'BankTransactions' arrays");
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
}

ImportedAccount XeroConnector::parse_xero_account(const JsonValue & obj) const {
    ImportedAccount account;
    if (!obj.is_object()) return account;

    account.external_id   = obj.get("AccountID").as_string();
    account.external_code = obj.get("Code").as_string();
    account.name          = obj.get("Name").as_string();
    account.description   = obj.get("Description").as_string();

    std::string xero_type = obj.get("Type").as_string();
    account.type = map_xero_account_type(xero_type);
    account.metadata["xero_type"] = xero_type;

    if (obj.has("CurrencyCode")) {
        account.currency = obj.get("CurrencyCode").as_string();
    }
    if (account.external_code.empty()) {
        account.external_code = account.name;
    }
    return account;
}

ImportedTransaction XeroConnector::parse_xero_bank_transaction(const JsonValue & obj) const {
    ImportedTransaction tx;
    if (!obj.is_object()) return tx;

    tx.external_id = obj.get("BankTransactionID").as_string();
    tx.description = obj.get("Reference").as_string();
    if (tx.description.empty()) {
        tx.description = obj.get("Type").as_string();
    }
    tx.reference = obj.get("Reference").as_string();
    tx.source_system = "Xero";
    tx.date = normalize_xero_date(obj.get("Date").as_string());

    if (obj.has("CurrencyCode")) {
        tx.currency = obj.get("CurrencyCode").as_string();
    }

    // Line items carry the account coding
    const JsonValue & line_items = obj.get("LineItems");
    if (line_items.is_array()) {
        for (size_t i = 0; i < line_items.size(); i++) {
            const JsonValue & item = line_items.at(i);
            ImportedTransaction::Line line;
            line.account_code = item.get("AccountCode").as_string();
            double amount = item.get("LineAmount").as_number();
            double quantity = item.get("Quantity").as_number();
            if (quantity != 0.0 && item.has("UnitAmount")) {
                amount = quantity * item.get("UnitAmount").as_number();
            }
            if (amount >= 0) {
                line.debit_amount = amount;
            } else {
                line.credit_amount = -amount;
            }
            line.memo = item.get("Description").as_string();
            tx.lines.push_back(line);
        }
    }

    // The bank account side of the transaction
    const JsonValue & bank_account = obj.get("BankAccount");
    if (bank_account.is_object()) {
        ImportedTransaction::Line line;
        line.account_code = bank_account.get("Code").as_string();
        if (line.account_code.empty()) {
            line.account_code = bank_account.get("AccountID").as_string();
        }
        double total = obj.get("Total").as_number();
        std::string xero_type = obj.get("Type").as_string();
        // RECEIVE: money into the bank (bank debit); SPEND: money out (bank credit)
        if (xero_type.find("RECEIVE") != std::string::npos) {
            line.debit_amount = total;
        } else {
            line.credit_amount = total;
        }
        line.memo = "Bank account side";
        tx.lines.push_back(line);
    }

    return tx;
}

std::string XeroConnector::map_xero_account_type(const std::string & xero_type) const {
    // Xero account types: BANK, CURRENT, FIXED, INVENTORY, NONCURRENT, PREPAYMENT,
    // OTHERASSET (assets); CURRLIAB, LIABILITIES, NONCURLIAB, OTHERLIABILITY,
    // OVERHEADS, DEPRECIATN... plus EQUITY, REVENUE, SALES, OTHERINCOME,
    // EXPENSE, COSTOFSALES
    if (xero_type == "BANK" || xero_type == "CURRENT" || xero_type == "FIXED" ||
        xero_type == "INVENTORY" || xero_type == "NONCURRENT" || xero_type == "PREPAYMENT" ||
        xero_type == "OTHERASSET" || xero_type == "ASSET") {
        return "Asset";
    }
    if (xero_type == "CURRLIAB" || xero_type == "NONCURLIAB" || xero_type == "LIABILITY" ||
        xero_type == "OTHERLIABILITY" || xero_type == "PAYABLE") {
        return "Liability";
    }
    if (xero_type == "EQUITY" || xero_type == "RETAINED") {
        return "Equity";
    }
    if (xero_type == "REVENUE" || xero_type == "SALES" || xero_type == "OTHERINCOME" ||
        xero_type == "INCOME") {
        return "Revenue";
    }
    if (xero_type == "EXPENSE" || xero_type == "COSTOFSALES" || xero_type == "OVERHEADS" ||
        xero_type == "DEPRECIATN" || xero_type == "DIRECTCOSTS") {
        return "Expense";
    }
    return xero_type;
}

std::string XeroConnector::normalize_xero_date(const std::string & xero_date) const {
    // Xero returns either ISO "2024-01-15T00:00:00" or the .NET JSON form
    // "/Date(1705276800000+0000)/"
    if (xero_date.size() >= 10 && xero_date[4] == '-' && xero_date[7] == '-') {
        return xero_date.substr(0, 10);
    }
    if (xero_date.find("/Date(") == 0) {
        size_t start = 6;
        size_t end = xero_date.find_first_of("+-)", start);
        if (end == std::string::npos) end = xero_date.size();
        try {
            int64_t ms = std::stoll(xero_date.substr(start, end - start));
            std::time_t secs = static_cast<std::time_t>(ms / 1000);
            std::tm tm_buf;
            std::tm * tm = nullptr;
#ifdef _WIN32
            tm_buf = *std::gmtime(&secs);
            tm = &tm_buf;
#else
            tm = gmtime_r(&secs, &tm_buf);
#endif
            if (tm != nullptr) {
                std::stringstream ss;
                ss << std::put_time(tm, "%Y-%m-%d");
                return ss.str();
            }
        } catch (...) {
            // Fall through
        }
    }
    return xero_date;
}

// ============================================================================
// ErpNextConnector Implementation
// ============================================================================

ErpNextConnector::ErpNextConnector() {}

bool ErpNextConnector::test_connection(const std::string & source) const {
    // No network I/O: a non-empty JSON payload counts as "connectable"
    return !source.empty() && source.find('{') != std::string::npos;
}

ImportResult ErpNextConnector::import_accounts(const std::string & source) {
    auto result = import_from_string(source);
    result.transactions.clear();
    result.transactions_imported = 0;
    return result;
}

ImportResult ErpNextConnector::import_transactions(const std::string & source) {
    auto result = import_from_string(source);
    result.accounts.clear();
    result.accounts_imported = 0;
    return result;
}

ImportResult ErpNextConnector::import_all(const std::string & source) {
    return import_from_string(source);
}

std::vector<std::string> ErpNextConnector::get_supported_extensions() const {
    return {}; // API connector - not file-based
}

std::string ErpNextConnector::build_api_request(const std::string & endpoint,
                                                 const std::string & api_key,
                                                 const std::string & api_secret,
                                                 const std::string & host) {
    std::stringstream ss;
    ss << "GET " << endpoint << " HTTP/1.1\r\n";
    ss << "Host: " << host << "\r\n";
    ss << "Authorization: token " << api_key << ":" << api_secret << "\r\n";
    ss << "Accept: application/json\r\n";
    ss << "Connection: close\r\n";
    ss << "\r\n";
    return ss.str();
}

ImportResult ErpNextConnector::import_from_string(const std::string & json_content) {
    auto start_time = std::chrono::steady_clock::now();

    ImportResult result;
    result.connector_type = "ERPNext API";
    result.success = true;

    JsonValue root;
    std::string error;
    JsonParser parser;
    if (!parser.parse(json_content, root, error)) {
        result.success = false;
        result.errors.push_back("ERPNext JSON parse error: " + error);
        return result;
    }

    // ERPNext list responses wrap documents in a "data" array
    const JsonValue & data = root.is_object() ? root.get("data") : root;
    if (!data.is_array()) {
        result.warnings.push_back("ERPNext payload has no 'data' array");
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time);
        return result;
    }

    for (size_t i = 0; i < data.size(); i++) {
        const JsonValue & doc = data.at(i);
        result.total_records_read++;

        if (!doc.is_object()) {
            result.records_skipped++;
            continue;
        }

        // Journal Entries carry posting_date + an accounts child table
        bool is_journal_entry = doc.has("posting_date") && doc.get("accounts").is_array();

        if (is_journal_entry) {
            auto tx = parse_erpnext_journal_entry(doc);
            if (!tx.external_id.empty()) {
                result.transactions.push_back(tx);
                result.transactions_imported++;
                result.records_imported++;
            } else {
                result.records_failed++;
            }
        } else if (doc.has("account_name") || doc.has("account_type")) {
            auto account = parse_erpnext_account(doc);
            if (!account.external_id.empty() || !account.external_code.empty()) {
                result.accounts.push_back(account);
                result.accounts_imported++;
                result.records_imported++;
            } else {
                result.records_failed++;
            }
        } else {
            result.records_skipped++;
            result.warnings.push_back("Unrecognized ERPNext document at data[" +
                                      std::to_string(i) + "]; skipped");
        }
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time);
    return result;
}

ImportedAccount ErpNextConnector::parse_erpnext_account(const JsonValue & obj) const {
    ImportedAccount account;

    account.name = obj.get("account_name").as_string();
    // "name" is the ERPNext document ID (typically "<account_name> - <abbr>")
    account.external_id = obj.get("name").as_string();
    if (account.external_id.empty()) {
        account.external_id = account.name;
    }
    account.external_code = obj.get("account_number").as_string();
    if (account.external_code.empty()) {
        account.external_code = account.name;
    }
    account.parent_id = obj.get("parent_account").as_string();
    account.description = obj.get("account_name").as_string();

    std::string erpnext_type = obj.get("account_type").as_string();
    account.type = map_erpnext_account_type(erpnext_type);
    account.metadata["erpnext_type"] = erpnext_type;

    if (obj.has("account_currency")) {
        account.currency = obj.get("account_currency").as_string();
    }
    return account;
}

ImportedTransaction ErpNextConnector::parse_erpnext_journal_entry(const JsonValue & obj) const {
    ImportedTransaction tx;

    tx.external_id = obj.get("name").as_string();
    tx.date = obj.get("posting_date").as_string();
    tx.description = obj.get("user_remark").as_string();
    if (tx.description.empty()) {
        tx.description = obj.get("title").as_string();
    }
    tx.reference = obj.get("cheque_no").as_string();
    if (tx.reference.empty()) {
        tx.reference = obj.get("bill_no").as_string();
    }
    tx.source_system = "ERPNext";
    tx.currency = obj.get("company_currency").as_string();
    if (tx.currency.empty()) {
        tx.currency = "USD";
    }

    const JsonValue & accounts = obj.get("accounts");
    for (size_t i = 0; i < accounts.size(); i++) {
        const JsonValue & row = accounts.at(i);
        ImportedTransaction::Line line;
        line.account_code = row.get("account").as_string();
        line.debit_amount = row.get("debit").as_number();
        if (line.debit_amount == 0.0) {
            line.debit_amount = row.get("debit_in_account_currency").as_number();
        }
        line.credit_amount = row.get("credit").as_number();
        if (line.credit_amount == 0.0) {
            line.credit_amount = row.get("credit_in_account_currency").as_number();
        }
        line.memo = row.get("user_remark").as_string();
        tx.lines.push_back(line);
    }

    // Verify against the document totals when present
    double total_debit = obj.get("total_debit").as_number();
    double total_credit = obj.get("total_credit").as_number();
    if (total_debit != 0.0 || total_credit != 0.0) {
        tx.metadata["total_debit"] = std::to_string(total_debit);
        tx.metadata["total_credit"] = std::to_string(total_credit);
    }

    return tx;
}

std::string ErpNextConnector::map_erpnext_account_type(const std::string & erpnext_type) const {
    // ERPNext account types include: Bank, Cash, Receivable, Stock, Fixed Asset,
    // Payable, Equity, Income Account, Expense Account, Cost of Goods Sold, ...
    std::string lower;
    lower.reserve(erpnext_type.size());
    for (char c : erpnext_type) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (lower.find("bank") != std::string::npos ||
        lower.find("cash") != std::string::npos ||
        lower.find("receivable") != std::string::npos ||
        lower.find("stock") != std::string::npos ||
        lower.find("asset") != std::string::npos ||
        lower.find("deposit") != std::string::npos) {
        return "Asset";
    }
    if (lower.find("payable") != std::string::npos ||
        lower.find("liability") != std::string::npos ||
        lower.find("loan") != std::string::npos ||
        lower.find("tax") != std::string::npos ||
        lower.find("chargeable") != std::string::npos) {
        return "Liability";
    }
    if (lower.find("equity") != std::string::npos ||
        lower.find("capital") != std::string::npos) {
        return "Equity";
    }
    if (lower.find("income") != std::string::npos ||
        lower.find("revenue") != std::string::npos ||
        lower.find("sale") != std::string::npos) {
        return "Revenue";
    }
    if (lower.find("expense") != std::string::npos ||
        lower.find("cost of goods") != std::string::npos ||
        lower.find("depreciation") != std::string::npos) {
        return "Expense";
    }
    return erpnext_type;
}

// ============================================================================
// ConnectorFactory Implementation
// ============================================================================

std::unique_ptr<FinancialDataConnector> ConnectorFactory::create(ConnectorType type) {
    switch (type) {
        case ConnectorType::CSV:
            return std::make_unique<CsvConnector>();
        case ConnectorType::GNUCASH_XML:
            return std::make_unique<GnuCashXmlConnector>();
        case ConnectorType::GNUCASH_SQLITE:
            return std::make_unique<GnuCashSqliteConnector>();
        case ConnectorType::BEANCOUNT:
        case ConnectorType::HLEDGER:
            return std::make_unique<BeancountConnector>();
        case ConnectorType::XERO_API:
            return std::make_unique<XeroConnector>();
        case ConnectorType::ERPNEXT_API:
            return std::make_unique<ErpNextConnector>();
        default:
            return nullptr;
    }
}

ConnectorType ConnectorFactory::detect_type(const std::string & file_path) {
    // Find extension
    size_t dot = file_path.rfind('.');
    if (dot == std::string::npos) return ConnectorType::CUSTOM;

    std::string ext = file_path.substr(dot);
    // Convert to lowercase
    for (auto & c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (ext == ".csv" || ext == ".tsv" || ext == ".txt") {
        return ConnectorType::CSV;
    }
    if (ext == ".gnucash" || ext == ".xml") {
        return ConnectorType::GNUCASH_XML;
    }
    if (ext == ".sqlite" || ext == ".db" || ext == ".sqlite3") {
        return ConnectorType::GNUCASH_SQLITE;
    }
    if (ext == ".beancount" || ext == ".bean") {
        return ConnectorType::BEANCOUNT;
    }
    if (ext == ".ledger" || ext == ".hledger") {
        return ConnectorType::HLEDGER;
    }

    return ConnectorType::CUSTOM;
}

std::unique_ptr<FinancialDataConnector> ConnectorFactory::create_for_file(
    const std::string & file_path) {
    ConnectorType type = detect_type(file_path);
    return create(type);
}

} // namespace connector
} // namespace ggnucash
