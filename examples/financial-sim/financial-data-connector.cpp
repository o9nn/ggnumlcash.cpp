#include "financial-data-connector.h"
#include <algorithm>
#include <cctype>
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
// ConnectorFactory Implementation
// ============================================================================

std::unique_ptr<FinancialDataConnector> ConnectorFactory::create(ConnectorType type) {
    switch (type) {
        case ConnectorType::CSV:
            return std::make_unique<CsvConnector>();
        case ConnectorType::GNUCASH_XML:
            return std::make_unique<GnuCashXmlConnector>();
        case ConnectorType::BEANCOUNT:
        case ConnectorType::HLEDGER:
            return std::make_unique<BeancountConnector>();
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
