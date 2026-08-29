#include "financial-data-connector.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace ggnucash::connector;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                                 \
    do {                                                                           \
        std::cout << "  Testing: " << name << "... ";                              \
        try {

#define TEST_END(name)                                                             \
            std::cout << "PASSED" << std::endl;                                    \
            tests_passed++;                                                        \
        } catch (const std::exception & e) {                                       \
            std::cout << "FAILED: " << e.what() << std::endl;                      \
            tests_failed++;                                                        \
        } catch (...) {                                                            \
            std::cout << "FAILED: Unknown exception" << std::endl;                 \
            tests_failed++;                                                        \
        }                                                                          \
    } while (0)

#define ASSERT_TRUE(cond) do { if (!(cond)) { throw std::runtime_error("Assertion failed: " #cond " at line " + std::to_string(__LINE__)); } } while(0)
#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { throw std::runtime_error("Assertion failed: " #a " != " #b " at line " + std::to_string(__LINE__)); } } while(0)
#define ASSERT_NEAR(a, b, tol) do { if (std::abs((a) - (b)) > (tol)) { throw std::runtime_error("Assertion failed: abs(" #a " - " #b ") > " #tol " at line " + std::to_string(__LINE__)); } } while(0)

// ============================================================================
// CSV Connector Tests
// ============================================================================

void test_csv_basic_import() {
    TEST("CSV basic import with debit/credit columns");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Debit,Credit\n"
        "2024-01-15,Office Supplies,150.00,\n"
        "2024-01-16,Client Payment,,5000.00\n"
        "2024-01-17,Rent Payment,2500.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)3);
    ASSERT_EQ(result.transactions.size(), (size_t)3);
    ASSERT_EQ(result.transactions[0].description, "Office Supplies");
    ASSERT_NEAR(result.transactions[0].lines[0].debit_amount, 150.0, 0.01);
    ASSERT_NEAR(result.transactions[1].lines[0].credit_amount, 5000.0, 0.01);

    TEST_END("CSV basic import with debit/credit columns");
}

void test_csv_amount_column() {
    TEST("CSV import with single amount column");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.amount_column = 2;
    mapping.debit_column = -1;
    mapping.credit_column = -1;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Amount\n"
        "2024-01-15,Deposit,1000.00\n"
        "2024-01-16,Withdrawal,-500.00\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_NEAR(result.transactions[0].lines[0].debit_amount, 1000.0, 0.01);
    ASSERT_NEAR(result.transactions[1].lines[0].credit_amount, 500.0, 0.01);

    TEST_END("CSV import with single amount column");
}

void test_csv_with_account_column() {
    TEST("CSV import with account column");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.account_column = 2;
    mapping.debit_column = 3;
    mapping.credit_column = 4;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Account,Debit,Credit\n"
        "2024-01-15,Office Supplies,5100,150.00,\n"
        "2024-01-15,Office Supplies,1000,,150.00\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_EQ(result.transactions[0].lines[0].account_code, "5100");
    ASSERT_EQ(result.transactions[1].lines[0].account_code, "1000");

    TEST_END("CSV import with account column");
}

void test_csv_default_account() {
    TEST("CSV import with default account");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);
    connector.set_default_account("1000-CHECKING");

    std::string csv =
        "Date,Description,Debit,Credit\n"
        "2024-01-15,Deposit,1000.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions[0].lines[0].account_code, "1000-CHECKING");

    TEST_END("CSV import with default account");
}

void test_csv_quoted_fields() {
    TEST("CSV import with quoted fields");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Debit,Credit\n"
        "2024-01-15,\"Office Supplies, Inc.\",150.00,\n"
        "2024-01-16,\"Rent for \"\"Main Office\"\"\",2500.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_EQ(result.transactions[0].description, "Office Supplies, Inc.");

    TEST_END("CSV import with quoted fields");
}

void test_csv_skip_rows() {
    TEST("CSV import with skip rows");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.has_header = true;
    mapping.skip_rows = 2; // Skip 2 additional rows after header
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Debit,Credit\n"
        "Generated: 2024-01-01,,,\n"
        "Account: Checking,,,\n"
        "2024-01-15,Real Transaction,100.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)1);
    ASSERT_EQ(result.transactions[0].description, "Real Transaction");

    TEST_END("CSV import with skip rows");
}

void test_csv_invalid_amounts() {
    TEST("CSV import with invalid amounts");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Debit,Credit\n"
        "2024-01-15,Valid,100.00,\n"
        "2024-01-16,Invalid,abc,\n"
        "2024-01-17,Also Valid,200.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_EQ(result.records_failed, (uint64_t)1);
    ASSERT_TRUE(result.errors.size() >= 1);

    TEST_END("CSV import with invalid amounts");
}

void test_csv_empty_content() {
    TEST("CSV import with empty content");

    CsvConnector connector;
    auto result = connector.import_transactions_from_string("");

    ASSERT_FALSE(result.success);
    ASSERT_TRUE(result.errors.size() >= 1);

    TEST_END("CSV import with empty content");
}

void test_csv_tsv_delimiter() {
    TEST("CSV import with tab delimiter (TSV)");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.debit_column = 2;
    mapping.credit_column = 3;
    mapping.delimiter = '\t';
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string tsv =
        "Date\tDescription\tDebit\tCredit\n"
        "2024-01-15\tOffice Supplies\t150.00\t\n"
        "2024-01-16\tPayment\t\t5000.00\n";

    auto result = connector.import_transactions_from_string(tsv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);

    TEST_END("CSV import with tab delimiter (TSV)");
}

void test_csv_reference_column() {
    TEST("CSV import with reference column");

    CsvConnector connector;
    CsvFieldMapping mapping;
    mapping.date_column = 0;
    mapping.description_column = 1;
    mapping.reference_column = 2;
    mapping.debit_column = 3;
    mapping.credit_column = 4;
    mapping.has_header = true;
    connector.set_field_mapping(mapping);

    std::string csv =
        "Date,Description,Reference,Debit,Credit\n"
        "2024-01-15,Check Payment,CHK-1234,500.00,\n";

    auto result = connector.import_transactions_from_string(csv);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions[0].reference, "CHK-1234");

    TEST_END("CSV import with reference column");
}

void test_csv_supported_extensions() {
    TEST("CSV supported extensions");

    CsvConnector connector;
    auto exts = connector.get_supported_extensions();
    ASSERT_TRUE(exts.size() >= 3);

    TEST_END("CSV supported extensions");
}

// ============================================================================
// GnuCash XML Connector Tests
// ============================================================================

void test_gnucash_xml_basic() {
    TEST("GnuCash XML basic account and transaction parsing");

    GnuCashXmlConnector connector;

    std::string xml = R"(<?xml version="1.0" encoding="utf-8" ?>
<gnc-v2>
  <gnc:book>
    <gnc:account version="2.0.0">
      <act:name>Checking Account</act:name>
      <act:id type="guid">acc-001</act:id>
      <act:type>BANK</act:type>
      <act:code>1000</act:code>
      <act:description>Main checking account</act:description>
    </gnc:account>
    <gnc:account version="2.0.0">
      <act:name>Office Expenses</act:name>
      <act:id type="guid">acc-002</act:id>
      <act:type>EXPENSE</act:type>
      <act:code>5100</act:code>
    </gnc:account>
    <gnc:transaction version="2.0.0">
      <trn:id type="guid">tx-001</trn:id>
      <trn:description>Office supplies purchase</trn:description>
      <trn:date-posted>
        <ts:date>2024-01-15 00:00:00 +0000</ts:date>
      </trn:date-posted>
      <trn:currency>
        <cmdty:id>USD</cmdty:id>
      </trn:currency>
      <trn:splits>
        <trn:split>
          <split:account>acc-002</split:account>
          <split:value>15000/100</split:value>
          <split:memo>Staples order</split:memo>
        </trn:split>
        <trn:split>
          <split:account>acc-001</split:account>
          <split:value>-15000/100</split:value>
        </trn:split>
      </trn:splits>
    </gnc:transaction>
  </gnc:book>
</gnc-v2>)";

    auto result = connector.import_from_string(xml);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)2);
    ASSERT_EQ(result.transactions_imported, (uint64_t)1);

    // Verify accounts
    ASSERT_EQ(result.accounts[0].name, "Checking Account");
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[0].external_code, "1000");

    ASSERT_EQ(result.accounts[1].name, "Office Expenses");
    ASSERT_EQ(result.accounts[1].type, "Expense");

    // Verify transaction
    auto & tx = result.transactions[0];
    ASSERT_EQ(tx.external_id, "tx-001");
    ASSERT_EQ(tx.description, "Office supplies purchase");
    ASSERT_EQ(tx.date, "2024-01-15");
    ASSERT_EQ(tx.currency, "USD");
    ASSERT_EQ(tx.lines.size(), (size_t)2);

    // First split: debit to expense (positive)
    ASSERT_NEAR(tx.lines[0].debit_amount, 150.0, 0.01);
    ASSERT_EQ(tx.lines[0].account_code, "5100");

    // Second split: credit from bank (negative)
    ASSERT_NEAR(tx.lines[1].credit_amount, 150.0, 0.01);
    ASSERT_EQ(tx.lines[1].account_code, "1000");

    TEST_END("GnuCash XML basic account and transaction parsing");
}

void test_gnucash_xml_account_types() {
    TEST("GnuCash XML account type mapping");

    GnuCashXmlConnector connector;

    std::string xml = R"(<?xml version="1.0" ?>
<gnc-v2>
  <gnc:book>
    <gnc:account version="2.0.0">
      <act:name>Savings</act:name>
      <act:id>a1</act:id>
      <act:type>BANK</act:type>
    </gnc:account>
    <gnc:account version="2.0.0">
      <act:name>Credit Card</act:name>
      <act:id>a2</act:id>
      <act:type>CREDIT</act:type>
    </gnc:account>
    <gnc:account version="2.0.0">
      <act:name>Sales</act:name>
      <act:id>a3</act:id>
      <act:type>INCOME</act:type>
    </gnc:account>
    <gnc:account version="2.0.0">
      <act:name>Owner Equity</act:name>
      <act:id>a4</act:id>
      <act:type>EQUITY</act:type>
    </gnc:account>
  </gnc:book>
</gnc-v2>)";

    auto result = connector.import_from_string(xml);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)4);
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[1].type, "Liability");
    ASSERT_EQ(result.accounts[2].type, "Revenue");
    ASSERT_EQ(result.accounts[3].type, "Equity");

    TEST_END("GnuCash XML account type mapping");
}

void test_gnucash_xml_multiple_transactions() {
    TEST("GnuCash XML multiple transactions");

    GnuCashXmlConnector connector;

    std::string xml = R"(<?xml version="1.0" ?>
<gnc-v2>
  <gnc:book>
    <gnc:account version="2.0.0">
      <act:name>Bank</act:name>
      <act:id>b1</act:id>
      <act:type>BANK</act:type>
      <act:code>1000</act:code>
    </gnc:account>
    <gnc:account version="2.0.0">
      <act:name>Revenue</act:name>
      <act:id>r1</act:id>
      <act:type>INCOME</act:type>
      <act:code>4000</act:code>
    </gnc:account>
    <gnc:transaction version="2.0.0">
      <trn:id>t1</trn:id>
      <trn:description>Sale 1</trn:description>
      <trn:date-posted><ts:date>2024-01-01 00:00:00</ts:date></trn:date-posted>
      <trn:splits>
        <trn:split><split:account>b1</split:account><split:value>100000/100</split:value></trn:split>
        <trn:split><split:account>r1</split:account><split:value>-100000/100</split:value></trn:split>
      </trn:splits>
    </gnc:transaction>
    <gnc:transaction version="2.0.0">
      <trn:id>t2</trn:id>
      <trn:description>Sale 2</trn:description>
      <trn:date-posted><ts:date>2024-01-02 00:00:00</ts:date></trn:date-posted>
      <trn:splits>
        <trn:split><split:account>b1</split:account><split:value>200000/100</split:value></trn:split>
        <trn:split><split:account>r1</split:account><split:value>-200000/100</split:value></trn:split>
      </trn:splits>
    </gnc:transaction>
  </gnc:book>
</gnc-v2>)";

    auto result = connector.import_from_string(xml);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_NEAR(result.transactions[0].lines[0].debit_amount, 1000.0, 0.01);
    ASSERT_NEAR(result.transactions[1].lines[0].debit_amount, 2000.0, 0.01);

    TEST_END("GnuCash XML multiple transactions");
}

void test_gnucash_xml_supported_extensions() {
    TEST("GnuCash XML supported extensions");

    GnuCashXmlConnector connector;
    auto exts = connector.get_supported_extensions();
    ASSERT_TRUE(exts.size() >= 2);

    TEST_END("GnuCash XML supported extensions");
}

// ============================================================================
// Beancount Connector Tests
// ============================================================================

void test_beancount_basic_import() {
    TEST("Beancount basic import");

    BeancountConnector connector;

    std::string content = R"(
; Main ledger file
option "operating_currency" "USD"

2024-01-01 open Assets:Bank:Checking USD
2024-01-01 open Expenses:Office USD
2024-01-01 open Income:Sales USD

2024-01-15 * "Office supplies purchase"
  Expenses:Office  150.00 USD
  Assets:Bank:Checking  -150.00 USD

2024-01-20 * "Client payment received"
  Assets:Bank:Checking  5000.00 USD
  Income:Sales  -5000.00 USD
)";

    auto result = connector.import_from_string(content);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)3);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);

    // Verify accounts
    ASSERT_EQ(result.accounts[0].external_code, "Assets:Bank:Checking");
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[1].external_code, "Expenses:Office");
    ASSERT_EQ(result.accounts[1].type, "Expense");
    ASSERT_EQ(result.accounts[2].external_code, "Income:Sales");
    ASSERT_EQ(result.accounts[2].type, "Revenue");

    // Verify first transaction
    auto & tx1 = result.transactions[0];
    ASSERT_EQ(tx1.date, "2024-01-15");
    ASSERT_EQ(tx1.description, "Office supplies purchase");
    ASSERT_EQ(tx1.lines.size(), (size_t)2);
    ASSERT_NEAR(tx1.lines[0].debit_amount, 150.0, 0.01);
    ASSERT_NEAR(tx1.lines[1].credit_amount, 150.0, 0.01);

    TEST_END("Beancount basic import");
}

void test_beancount_account_types() {
    TEST("Beancount account type detection");

    BeancountConnector connector;

    std::string content = R"(
2024-01-01 open Assets:Bank:Checking USD
2024-01-01 open Liabilities:CreditCard USD
2024-01-01 open Equity:OpeningBalances USD
2024-01-01 open Income:Salary USD
2024-01-01 open Expenses:Rent USD
)";

    auto result = connector.import_from_string(content);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)5);
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[1].type, "Liability");
    ASSERT_EQ(result.accounts[2].type, "Equity");
    ASSERT_EQ(result.accounts[3].type, "Revenue");
    ASSERT_EQ(result.accounts[4].type, "Expense");

    TEST_END("Beancount account type detection");
}

void test_beancount_comments_and_empty_lines() {
    TEST("Beancount handles comments and empty lines");

    BeancountConnector connector;

    std::string content = R"(
; This is a comment
# This is also a comment

2024-01-01 open Assets:Bank USD

; Another comment

2024-01-15 * "Test transaction"
  Assets:Bank  100.00 USD
  Expenses:Test  -100.00 USD

)";

    auto result = connector.import_from_string(content);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)1);
    ASSERT_EQ(result.transactions_imported, (uint64_t)1);

    TEST_END("Beancount handles comments and empty lines");
}

void test_beancount_supported_extensions() {
    TEST("Beancount supported extensions");

    BeancountConnector connector;
    auto exts = connector.get_supported_extensions();
    ASSERT_TRUE(exts.size() >= 2);

    TEST_END("Beancount supported extensions");
}

void test_beancount_multiple_transactions() {
    TEST("Beancount multiple transactions");

    BeancountConnector connector;

    std::string content = R"(
2024-01-01 open Assets:Bank USD
2024-01-01 open Expenses:Rent USD
2024-01-01 open Expenses:Food USD
2024-01-01 open Income:Salary USD

2024-01-15 * "Rent payment"
  Expenses:Rent  2500.00 USD
  Assets:Bank  -2500.00 USD

2024-01-20 * "Grocery shopping"
  Expenses:Food  85.50 USD
  Assets:Bank  -85.50 USD

2024-01-31 * "Salary received"
  Assets:Bank  8000.00 USD
  Income:Salary  -8000.00 USD
)";

    auto result = connector.import_from_string(content);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)3);
    ASSERT_NEAR(result.transactions[0].lines[0].debit_amount, 2500.0, 0.01);
    ASSERT_NEAR(result.transactions[1].lines[0].debit_amount, 85.50, 0.01);
    ASSERT_NEAR(result.transactions[2].lines[0].debit_amount, 8000.0, 0.01);

    TEST_END("Beancount multiple transactions");
}

// ============================================================================
// DataNormalizer Tests
// ============================================================================

void test_normalizer_explicit_mapping() {
    TEST("DataNormalizer explicit account mapping");

    DataNormalizer normalizer;
    normalizer.add_account_mapping("EXT-1000", "10001", "Asset");
    normalizer.add_account_mapping("EXT-5100", "50001", "Expense");

    ASSERT_TRUE(normalizer.has_mapping("EXT-1000"));
    ASSERT_TRUE(normalizer.has_mapping("EXT-5100"));
    ASSERT_FALSE(normalizer.has_mapping("EXT-9999"));

    ASSERT_EQ(normalizer.normalize_account_code("EXT-1000"), "10001");
    ASSERT_EQ(normalizer.normalize_account_code("EXT-5100"), "50001");
    ASSERT_EQ(normalizer.normalize_account_code("UNMAPPED"), "UNMAPPED"); // Returns as-is
    ASSERT_EQ(normalizer.get_mapping_count(), (size_t)2);

    TEST_END("DataNormalizer explicit account mapping");
}

void test_normalizer_auto_map() {
    TEST("DataNormalizer auto-mapping from account names");

    DataNormalizer normalizer;

    std::vector<ImportedAccount> accounts;

    ImportedAccount a1;
    a1.external_code = "ext-bank";
    a1.name = "Bank Checking Account";
    accounts.push_back(a1);

    ImportedAccount a2;
    a2.external_code = "ext-rent";
    a2.name = "Rent Expense";
    accounts.push_back(a2);

    ImportedAccount a3;
    a3.external_code = "ext-sales";
    a3.name = "Sales Revenue";
    accounts.push_back(a3);

    ImportedAccount a4;
    a4.external_code = "ext-loan";
    a4.name = "Bank Loan Payable";
    accounts.push_back(a4);

    normalizer.auto_map_accounts(accounts);

    ASSERT_EQ(normalizer.get_mapping_count(), (size_t)4);
    ASSERT_TRUE(normalizer.has_mapping("ext-bank"));
    ASSERT_TRUE(normalizer.has_mapping("ext-rent"));
    ASSERT_TRUE(normalizer.has_mapping("ext-sales"));
    ASSERT_TRUE(normalizer.has_mapping("ext-loan"));

    // Verify type-based code generation
    auto mappings = normalizer.get_all_mappings();
    for (const auto & m : mappings) {
        ASSERT_TRUE(m.is_auto_mapped);
        if (m.external_code == "ext-bank") {
            ASSERT_TRUE(m.ggnucash_code[0] == '1'); // Asset prefix
        } else if (m.external_code == "ext-rent") {
            ASSERT_TRUE(m.ggnucash_code[0] == '5'); // Expense prefix
        } else if (m.external_code == "ext-sales") {
            ASSERT_TRUE(m.ggnucash_code[0] == '4'); // Revenue prefix
        } else if (m.external_code == "ext-loan") {
            ASSERT_TRUE(m.ggnucash_code[0] == '2'); // Liability prefix
        }
    }

    TEST_END("DataNormalizer auto-mapping from account names");
}

void test_normalizer_transaction_conversion() {
    TEST("DataNormalizer transaction conversion");

    DataNormalizer normalizer;
    normalizer.add_account_mapping("Assets:Bank", "10001", "Asset");
    normalizer.add_account_mapping("Expenses:Office", "50001", "Expense");

    ImportedTransaction imported;
    imported.external_id = "IMP-001";
    imported.date = "2024-01-15";
    imported.description = "Office supplies";

    ImportedTransaction::Line line1;
    line1.account_code = "Expenses:Office";
    line1.debit_amount = 150.0;
    imported.lines.push_back(line1);

    ImportedTransaction::Line line2;
    line2.account_code = "Assets:Bank";
    line2.credit_amount = 150.0;
    imported.lines.push_back(line2);

    Transaction tx = normalizer.normalize_transaction(imported);

    ASSERT_EQ(tx.id, "IMP-001");
    ASSERT_EQ(tx.description, "Office supplies");
    ASSERT_EQ(tx.timestamp, "2024-01-15");
    ASSERT_EQ(tx.entries.size(), (size_t)2);
    ASSERT_EQ(tx.entries[0].account_code, "50001");
    ASSERT_EQ(tx.entries[1].account_code, "10001");
    ASSERT_NEAR(tx.entries[0].debit_amount, 150.0, 0.01);
    ASSERT_NEAR(tx.entries[1].credit_amount, 150.0, 0.01);
    ASSERT_TRUE(tx.is_balanced());

    TEST_END("DataNormalizer transaction conversion");
}

void test_normalizer_batch_conversion() {
    TEST("DataNormalizer batch transaction conversion with hash chain");

    DataNormalizer normalizer;
    normalizer.add_account_mapping("A", "10001");
    normalizer.add_account_mapping("B", "20001");

    std::vector<ImportedTransaction> imported;
    for (int i = 0; i < 3; i++) {
        ImportedTransaction tx;
        tx.external_id = "TX-" + std::to_string(i);
        tx.description = "Transaction " + std::to_string(i);
        ImportedTransaction::Line line;
        line.account_code = "A";
        line.debit_amount = 100.0 * (i + 1);
        tx.lines.push_back(line);
        imported.push_back(tx);
    }

    auto transactions = normalizer.normalize_transactions(imported);

    ASSERT_EQ(transactions.size(), (size_t)3);
    // First transaction should have empty prev_hash
    ASSERT_TRUE(transactions[0].prev_hash.empty());
    // Second should link to first
    ASSERT_EQ(transactions[1].prev_hash, transactions[0].hash);
    // Third should link to second
    ASSERT_EQ(transactions[2].prev_hash, transactions[1].hash);

    TEST_END("DataNormalizer batch transaction conversion with hash chain");
}

void test_normalizer_unmapped_codes() {
    TEST("DataNormalizer unmapped code detection");

    DataNormalizer normalizer;
    normalizer.add_account_mapping("A", "10001");

    std::vector<ImportedTransaction> transactions;
    ImportedTransaction tx;
    ImportedTransaction::Line line1;
    line1.account_code = "A";
    tx.lines.push_back(line1);
    ImportedTransaction::Line line2;
    line2.account_code = "B";
    tx.lines.push_back(line2);
    ImportedTransaction::Line line3;
    line3.account_code = "C";
    tx.lines.push_back(line3);
    transactions.push_back(tx);

    auto unmapped = normalizer.get_unmapped_codes(transactions);

    ASSERT_EQ(unmapped.size(), (size_t)2);
    // Should contain B and C but not A
    bool has_b = false, has_c = false;
    for (const auto & code : unmapped) {
        if (code == "B") has_b = true;
        if (code == "C") has_c = true;
    }
    ASSERT_TRUE(has_b);
    ASSERT_TRUE(has_c);

    TEST_END("DataNormalizer unmapped code detection");
}

void test_normalizer_clear_mappings() {
    TEST("DataNormalizer clear mappings");

    DataNormalizer normalizer;
    normalizer.add_account_mapping("A", "10001");
    normalizer.add_account_mapping("B", "20001");
    ASSERT_EQ(normalizer.get_mapping_count(), (size_t)2);

    normalizer.clear_mappings();
    ASSERT_EQ(normalizer.get_mapping_count(), (size_t)0);
    ASSERT_FALSE(normalizer.has_mapping("A"));

    TEST_END("DataNormalizer clear mappings");
}

// ============================================================================
// ConnectorFactory Tests
// ============================================================================

void test_factory_type_detection() {
    TEST("ConnectorFactory file type detection");

    ASSERT_EQ(ConnectorFactory::detect_type("data.csv"), ConnectorType::CSV);
    ASSERT_EQ(ConnectorFactory::detect_type("data.tsv"), ConnectorType::CSV);
    ASSERT_EQ(ConnectorFactory::detect_type("data.gnucash"), ConnectorType::GNUCASH_XML);
    ASSERT_EQ(ConnectorFactory::detect_type("data.xml"), ConnectorType::GNUCASH_XML);
    ASSERT_EQ(ConnectorFactory::detect_type("data.beancount"), ConnectorType::BEANCOUNT);
    ASSERT_EQ(ConnectorFactory::detect_type("data.bean"), ConnectorType::BEANCOUNT);
    ASSERT_EQ(ConnectorFactory::detect_type("data.ledger"), ConnectorType::HLEDGER);
    ASSERT_EQ(ConnectorFactory::detect_type("data.unknown"), ConnectorType::CUSTOM);

    TEST_END("ConnectorFactory file type detection");
}

void test_factory_create_connector() {
    TEST("ConnectorFactory creates correct connector types");

    auto csv = ConnectorFactory::create(ConnectorType::CSV);
    ASSERT_TRUE(csv != nullptr);
    ASSERT_EQ(csv->get_type(), ConnectorType::CSV);

    auto gnucash = ConnectorFactory::create(ConnectorType::GNUCASH_XML);
    ASSERT_TRUE(gnucash != nullptr);
    ASSERT_EQ(gnucash->get_type(), ConnectorType::GNUCASH_XML);

    auto beancount = ConnectorFactory::create(ConnectorType::BEANCOUNT);
    ASSERT_TRUE(beancount != nullptr);
    ASSERT_EQ(beancount->get_type(), ConnectorType::BEANCOUNT);

    auto custom = ConnectorFactory::create(ConnectorType::CUSTOM);
    ASSERT_TRUE(custom == nullptr); // Not implemented

    TEST_END("ConnectorFactory creates correct connector types");
}

void test_factory_create_for_file() {
    TEST("ConnectorFactory creates connector from file path");

    auto csv = ConnectorFactory::create_for_file("transactions.csv");
    ASSERT_TRUE(csv != nullptr);
    ASSERT_EQ(csv->get_type_name(), "CSV");

    auto gnucash = ConnectorFactory::create_for_file("my_accounts.gnucash");
    ASSERT_TRUE(gnucash != nullptr);
    ASSERT_EQ(gnucash->get_type_name(), "GnuCash XML");

    auto beancount = ConnectorFactory::create_for_file("ledger.beancount");
    ASSERT_TRUE(beancount != nullptr);
    ASSERT_EQ(beancount->get_type_name(), "Beancount");

    TEST_END("ConnectorFactory creates connector from file path");
}

// ============================================================================
// Import Result Tests
// ============================================================================

void test_import_result_summary() {
    TEST("ImportResult summary and JSON generation");

    ImportResult result;
    result.success = true;
    result.connector_type = "CSV";
    result.source_path = "/data/test.csv";
    result.total_records_read = 100;
    result.records_imported = 95;
    result.records_skipped = 3;
    result.records_failed = 2;
    result.accounts_imported = 0;
    result.transactions_imported = 95;
    result.duration = std::chrono::milliseconds(42);
    result.errors.push_back("Row 5: Invalid amount");
    result.warnings.push_back("No currency specified");

    auto summary = result.to_summary();
    ASSERT_TRUE(summary.find("SUCCESS") != std::string::npos);
    ASSERT_TRUE(summary.find("95") != std::string::npos);

    auto json = result.to_json();
    ASSERT_TRUE(json.find("\"success\": true") != std::string::npos);
    ASSERT_TRUE(json.find("\"records_imported\": 95") != std::string::npos);

    TEST_END("ImportResult summary and JSON generation");
}

void test_imported_transaction_balance_check() {
    TEST("ImportedTransaction balance check");

    ImportedTransaction tx;
    ImportedTransaction::Line line1;
    line1.debit_amount = 100.0;
    tx.lines.push_back(line1);

    ImportedTransaction::Line line2;
    line2.credit_amount = 100.0;
    tx.lines.push_back(line2);

    ASSERT_TRUE(tx.is_balanced());

    // Unbalanced
    ImportedTransaction tx2;
    ImportedTransaction::Line line3;
    line3.debit_amount = 100.0;
    tx2.lines.push_back(line3);

    ImportedTransaction::Line line4;
    line4.credit_amount = 50.0;
    tx2.lines.push_back(line4);

    ASSERT_FALSE(tx2.is_balanced());

    TEST_END("ImportedTransaction balance check");
}

// ============================================================================
// Connector Type String Tests
// ============================================================================

void test_connector_type_strings() {
    TEST("Connector type string conversion");

    ASSERT_EQ(connector_type_to_string(ConnectorType::CSV), "CSV");
    ASSERT_EQ(connector_type_to_string(ConnectorType::GNUCASH_XML), "GNUCASH_XML");
    ASSERT_EQ(connector_type_to_string(ConnectorType::BEANCOUNT), "BEANCOUNT");
    ASSERT_EQ(connector_type_to_string(ConnectorType::XERO_API), "XERO_API");
    ASSERT_EQ(connector_type_to_string(ConnectorType::ERPNEXT_API), "ERPNEXT_API");

    TEST_END("Connector type string conversion");
}

// ============================================================================
// JSON Parser Tests
// ============================================================================

void test_json_parser_basic() {
    TEST("JsonParser parses objects, arrays, scalars");

    JsonParser parser;
    JsonValue root;
    std::string error;

    ASSERT_TRUE(parser.parse(
        R"({"a": 1, "b": "text", "c": [1, 2.5, -3], "d": {"e": true, "f": null}})",
        root, error));
    ASSERT_TRUE(root.is_object());
    ASSERT_TRUE(root.has("a"));
    ASSERT_NEAR(root.get("a").as_number(), 1.0, 0.0001);
    ASSERT_EQ(root.get("b").as_string(), "text");
    ASSERT_TRUE(root.get("c").is_array());
    ASSERT_EQ(root.get("c").size(), (size_t)3);
    ASSERT_NEAR(root.get("c").at(1).as_number(), 2.5, 0.0001);
    ASSERT_NEAR(root.get("c").at(2).as_number(), -3.0, 0.0001);
    ASSERT_TRUE(root.get("d").get("e").as_bool());
    ASSERT_TRUE(root.get("d").get("f").is_null());

    TEST_END("JsonParser parses objects, arrays, scalars");
}

void test_json_parser_escapes_and_unicode() {
    TEST("JsonParser handles escapes and unicode");

    JsonParser parser;
    JsonValue root;
    std::string error;

    ASSERT_TRUE(parser.parse(R"({"s": "a\"b\\c\ndAé"})", root, error));
    std::string expected = "a\"b\\c\ndA\xc3\xa9";
    ASSERT_EQ(root.get("s").as_string(), expected);

    // Missing keys return null values
    ASSERT_TRUE(root.get("nonexistent").is_null());
    ASSERT_EQ(root.get("nonexistent").as_string(), "");

    TEST_END("JsonParser handles escapes and unicode");
}

void test_json_parser_errors() {
    TEST("JsonParser rejects malformed JSON gracefully");

    JsonParser parser;
    JsonValue root;
    std::string error;

    ASSERT_FALSE(parser.parse("{invalid", root, error));
    ASSERT_FALSE(error.empty());
    ASSERT_FALSE(parser.parse("[1, 2", root, error));
    ASSERT_FALSE(parser.parse("{\"a\": 1} trailing", root, error));
    ASSERT_FALSE(parser.parse("", root, error));
    ASSERT_FALSE(parser.parse("{\"a\": tru}", root, error));

    TEST_END("JsonParser rejects malformed JSON gracefully");
}

// ============================================================================
// Xero Connector Tests
// ============================================================================

void test_xero_accounts_import() {
    TEST("Xero connector imports chart of accounts");

    XeroConnector connector;
    std::string payload = R"({
        "Accounts": [
            {
                "AccountID": "bd0e9484-bfd8-4a40-93b0-c4c5f7a30a01",
                "Code": "090",
                "Name": "Business Bank Account",
                "Type": "BANK",
                "CurrencyCode": "USD",
                "Description": "Main operating account"
            },
            {
                "AccountID": "7dd4b1f4-1111-4a40-93b0-c4c5f7a30a02",
                "Code": "400",
                "Name": "Sales Revenue",
                "Type": "SALES"
            },
            {
                "AccountID": "8ee5c2g5-2222-4a40-93b0-c4c5f7a30a03",
                "Code": "800",
                "Name": "Interest Expense",
                "Type": "EXPENSE"
            }
        ]
    })";

    auto result = connector.import_from_string(payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)3);
    ASSERT_EQ(result.accounts[0].external_id, "bd0e9484-bfd8-4a40-93b0-c4c5f7a30a01");
    ASSERT_EQ(result.accounts[0].external_code, "090");
    ASSERT_EQ(result.accounts[0].name, "Business Bank Account");
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[1].type, "Revenue");
    ASSERT_EQ(result.accounts[2].type, "Expense");
    ASSERT_EQ(result.accounts[0].currency, "USD");
    ASSERT_EQ(result.accounts[1].currency, "USD"); // default

    TEST_END("Xero connector imports chart of accounts");
}

void test_xero_bank_transactions_import() {
    TEST("Xero connector imports bank transactions");

    XeroConnector connector;
    std::string payload = R"({
        "BankTransactions": [
            {
                "BankTransactionID": "a1b2c3d4-0001-4a40-93b0-c4c5f7a30a10",
                "Type": "RECEIVE",
                "Date": "2024-01-15T00:00:00",
                "Reference": "INV-0042",
                "CurrencyCode": "USD",
                "Total": 1500.00,
                "BankAccount": {"Code": "090", "AccountID": "bd0e9484"},
                "LineItems": [
                    {
                        "Description": "Consulting services",
                        "AccountCode": "400",
                        "LineAmount": 1500.00,
                        "Quantity": 1,
                        "UnitAmount": 1500.00
                    }
                ]
            },
            {
                "BankTransactionID": "a1b2c3d4-0002-4a40-93b0-c4c5f7a30a11",
                "Type": "SPEND",
                "Date": "/Date(1705968000000+0000)/",
                "Reference": "OFFICE-SUPPLY",
                "Total": 250.50,
                "BankAccount": {"Code": "090"},
                "LineItems": [
                    {
                        "AccountCode": "610",
                        "LineAmount": 250.50
                    }
                ]
            }
        ]
    })";

    auto result = connector.import_from_string(payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);
    ASSERT_EQ(result.transactions.size(), (size_t)2);

    const auto & tx1 = result.transactions[0];
    ASSERT_EQ(tx1.external_id, "a1b2c3d4-0001-4a40-93b0-c4c5f7a30a10");
    ASSERT_EQ(tx1.date, "2024-01-15");
    ASSERT_EQ(tx1.reference, "INV-0042");
    ASSERT_EQ(tx1.source_system, "Xero");
    ASSERT_EQ(tx1.lines.size(), (size_t)2);
    ASSERT_EQ(tx1.lines[0].account_code, "400");
    ASSERT_NEAR(tx1.lines[0].debit_amount, 1500.0, 0.01);
    ASSERT_EQ(tx1.lines[1].account_code, "090");
    ASSERT_NEAR(tx1.lines[1].debit_amount, 1500.0, 0.01); // RECEIVE -> bank debit

    const auto & tx2 = result.transactions[1];
    ASSERT_EQ(tx2.date, "2024-01-23"); // 1705968000000 ms epoch
    ASSERT_NEAR(tx2.lines[0].debit_amount, 250.50, 0.01);
    ASSERT_NEAR(tx2.lines[1].credit_amount, 250.50, 0.01); // SPEND -> bank credit
    ASSERT_TRUE(tx2.is_balanced());

    TEST_END("Xero connector imports bank transactions");
}

void test_xero_malformed_and_empty() {
    TEST("Xero connector handles malformed and empty payloads");

    XeroConnector connector;

    auto bad = connector.import_from_string("this is not json");
    ASSERT_FALSE(bad.success);
    ASSERT_FALSE(bad.errors.empty());

    auto empty = connector.import_from_string("{}");
    ASSERT_TRUE(empty.success);
    ASSERT_EQ(empty.accounts_imported, (uint64_t)0);
    ASSERT_EQ(empty.transactions_imported, (uint64_t)0);
    ASSERT_FALSE(empty.warnings.empty());

    TEST_END("Xero connector handles malformed and empty payloads");
}

void test_xero_oauth2_request_builder() {
    TEST("Xero OAuth2 request builder constructs valid request");

    auto request = XeroConnector::build_oauth2_request(
        "/api.xro/2.0/Accounts", "test-token-123", "tenant-abc");

    ASSERT_TRUE(request.find("GET /api.xro/2.0/Accounts HTTP/1.1") != std::string::npos);
    ASSERT_TRUE(request.find("Host: api.xero.com") != std::string::npos);
    ASSERT_TRUE(request.find("Authorization: ****** test-token-123") != std::string::npos);
    ASSERT_TRUE(request.find("xero-tenant-id: tenant-abc") != std::string::npos);
    ASSERT_TRUE(request.find("Accept: application/json") != std::string::npos);

    // Without tenant id the header is omitted
    auto request2 = XeroConnector::build_oauth2_request("/api.xro/2.0/Contacts", "tok");
    ASSERT_TRUE(request2.find("xero-tenant-id") == std::string::npos);

    TEST_END("Xero OAuth2 request builder constructs valid request");
}

// ============================================================================
// ERPNext Connector Tests
// ============================================================================

void test_erpnext_accounts_import() {
    TEST("ERPNext connector imports chart of accounts");

    ErpNextConnector connector;
    std::string payload = R"({
        "data": [
            {
                "name": "Main Cash - ACME",
                "account_name": "Main Cash",
                "account_type": "Cash",
                "parent_account": "Cash and Bank Accounts - ACME",
                "account_number": "1110",
                "account_currency": "USD"
            },
            {
                "name": "Sales - ACME",
                "account_name": "Sales",
                "account_type": "Income Account",
                "parent_account": "Income - ACME"
            },
            {
                "name": "Creditors - ACME",
                "account_name": "Creditors",
                "account_type": "Payable",
                "parent_account": "Liabilities - ACME"
            }
        ]
    })";

    auto result = connector.import_from_string(payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)3);
    ASSERT_EQ(result.accounts[0].external_id, "Main Cash - ACME");
    ASSERT_EQ(result.accounts[0].external_code, "1110");
    ASSERT_EQ(result.accounts[0].name, "Main Cash");
    ASSERT_EQ(result.accounts[0].type, "Asset");
    ASSERT_EQ(result.accounts[0].parent_id, "Cash and Bank Accounts - ACME");
    ASSERT_EQ(result.accounts[1].type, "Revenue");
    ASSERT_EQ(result.accounts[2].type, "Liability");

    TEST_END("ERPNext connector imports chart of accounts");
}

void test_erpnext_journal_entries_import() {
    TEST("ERPNext connector imports journal entries");

    ErpNextConnector connector;
    std::string payload = R"({
        "data": [
            {
                "name": "JV-2024-00015",
                "posting_date": "2024-01-20",
                "user_remark": "Monthly office rent",
                "total_debit": 2500.00,
                "total_credit": 2500.00,
                "company_currency": "USD",
                "accounts": [
                    {"account": "Rent Expense - ACME", "debit": 2500.00, "credit": 0.0},
                    {"account": "Main Cash - ACME", "debit": 0.0, "credit": 2500.00}
                ]
            },
            {
                "name": "JV-2024-00016",
                "posting_date": "2024-01-22",
                "user_remark": "Client invoice payment",
                "accounts": [
                    {"account": "Main Cash - ACME", "debit": 5000.00, "credit": 0.0},
                    {"account": "Debtors - ACME", "debit": 0.0, "credit": 5000.00}
                ]
            }
        ]
    })";

    auto result = connector.import_from_string(payload);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);

    const auto & tx1 = result.transactions[0];
    ASSERT_EQ(tx1.external_id, "JV-2024-00015");
    ASSERT_EQ(tx1.date, "2024-01-20");
    ASSERT_EQ(tx1.description, "Monthly office rent");
    ASSERT_EQ(tx1.source_system, "ERPNext");
    ASSERT_EQ(tx1.currency, "USD");
    ASSERT_EQ(tx1.lines.size(), (size_t)2);
    ASSERT_EQ(tx1.lines[0].account_code, "Rent Expense - ACME");
    ASSERT_NEAR(tx1.lines[0].debit_amount, 2500.0, 0.01);
    ASSERT_NEAR(tx1.lines[1].credit_amount, 2500.0, 0.01);
    ASSERT_TRUE(tx1.is_balanced());
    ASSERT_TRUE(tx1.metadata.find("total_debit") != tx1.metadata.end());

    const auto & tx2 = result.transactions[1];
    ASSERT_EQ(tx2.external_id, "JV-2024-00016");
    ASSERT_TRUE(tx2.is_balanced());

    TEST_END("ERPNext connector imports journal entries");
}

void test_erpnext_malformed_and_mixed() {
    TEST("ERPNext connector handles malformed and mixed payloads");

    ErpNextConnector connector;

    auto bad = connector.import_from_string("{not valid json");
    ASSERT_FALSE(bad.success);
    ASSERT_FALSE(bad.errors.empty());

    // Mixed accounts + journal entries in one payload
    std::string mixed = R"({
        "data": [
            {"name": "Sales - ACME", "account_name": "Sales", "account_type": "Income Account"},
            {
                "name": "JV-00001",
                "posting_date": "2024-02-01",
                "accounts": [
                    {"account": "Cash", "debit": 100.0, "credit": 0.0},
                    {"account": "Sales", "debit": 0.0, "credit": 100.0}
                ]
            },
            {"some_other_doctype": true}
        ]
    })";
    auto result = connector.import_from_string(mixed);
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)1);
    ASSERT_EQ(result.transactions_imported, (uint64_t)1);
    ASSERT_EQ(result.records_skipped, (uint64_t)1);

    TEST_END("ERPNext connector handles malformed and mixed payloads");
}

void test_erpnext_api_request_builder() {
    TEST("ERPNext API request builder constructs token-auth request");

    auto request = ErpNextConnector::build_api_request(
        "/api/resource/Account", "api-key-1", "api-secret-2", "erp.example.com");

    ASSERT_TRUE(request.find("GET /api/resource/Account HTTP/1.1") != std::string::npos);
    ASSERT_TRUE(request.find("Host: erp.example.com") != std::string::npos);
    ASSERT_TRUE(request.find("Authorization: token api-key-1:api-secret-2") != std::string::npos);

    TEST_END("ERPNext API request builder constructs token-auth request");
}

// ============================================================================
// GnuCash SQLite Connector Tests
// ============================================================================

// Minimal in-memory SQLite file builder (test fixture). Writes valid SQLite
// format-3 pages: page 1 holds sqlite_master, subsequent pages hold one table
// each as leaf-only b-trees.
class SqliteTestFileBuilder {
public:
    explicit SqliteTestFileBuilder(uint32_t page_size = 4096) : page_size_(page_size) {}

    using Record = std::vector<std::string>; // "" = SQL NULL, "i:<n>" = integer

    // Register a table with CREATE TABLE sql and its rows
    void add_table(const std::string & create_sql, const std::vector<Record> & rows) {
        tables_.push_back({create_sql, rows});
    }

    std::string build() {
        // Page assignment: page 1 = sqlite_master, pages 2..N = one per table
        std::vector<std::vector<uint8_t>> pages;
        pages.push_back(std::vector<uint8_t>(page_size_, 0));
        for (size_t i = 0; i < tables_.size(); i++) {
            pages.push_back(make_table_leaf_page(tables_[i].rows, false));
        }

        // Build sqlite_master rows now that root pages are known
        std::vector<Record> master_rows;
        for (size_t i = 0; i < tables_.size(); i++) {
            std::string name = extract_table_name(tables_[i].create_sql);
            master_rows.push_back({"table", name, name,
                                   "i:" + std::to_string(i + 2),
                                   tables_[i].create_sql});
        }
        pages[0] = make_table_leaf_page(master_rows, true);
        write_header(pages[0], static_cast<uint32_t>(pages.size()));

        std::string out;
        for (const auto & page : pages) {
            out.append(reinterpret_cast<const char *>(page.data()), page.size());
        }
        return out;
    }

private:
    struct TableDef {
        std::string         create_sql;
        std::vector<Record> rows;
    };

    uint32_t              page_size_;
    std::vector<TableDef> tables_;

    static void put_u16(std::vector<uint8_t> & buf, size_t off, uint16_t v) {
        buf[off] = static_cast<uint8_t>((v >> 8) & 0xFF);
        buf[off + 1] = static_cast<uint8_t>(v & 0xFF);
    }

    static void put_u32(std::vector<uint8_t> & buf, size_t off, uint32_t v) {
        for (int i = 3; i >= 0; i--) {
            buf[off + static_cast<size_t>(3 - i)] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
        }
    }

    static void append_varint(std::vector<uint8_t> & out, uint64_t value) {
        uint8_t tmp[9];
        int n = 0;
        tmp[n++] = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        while (value > 0) {
            tmp[n++] = static_cast<uint8_t>(value & 0x7F);
            value >>= 7;
        }
        for (int i = n - 1; i > 0; i--) {
            out.push_back(static_cast<uint8_t>(tmp[i] | 0x80));
        }
        out.push_back(tmp[0]);
    }

    // Serial type + encoded body for one field
    static void encode_field(const std::string & field,
                             uint64_t & serial_type,
                             std::vector<uint8_t> & body) {
        if (field.empty()) {
            serial_type = 0; // NULL
            return;
        }
        if (field.size() > 2 && field[0] == 'i' && field[1] == ':') {
            int64_t v = std::stoll(field.substr(2));
            uint64_t uv = static_cast<uint64_t>(v);
            int bytes;
            if (v >= -128 && v <= 127)                        { serial_type = 1; bytes = 1; }
            else if (v >= -32768 && v <= 32767)               { serial_type = 2; bytes = 2; }
            else if (v >= -8388608 && v <= 8388607)           { serial_type = 3; bytes = 3; }
            else if (v >= -2147483648LL && v <= 2147483647LL) { serial_type = 4; bytes = 4; }
            else                                              { serial_type = 6; bytes = 8; }
            for (int i = bytes - 1; i >= 0; i--) {
                body.push_back(static_cast<uint8_t>((uv >> (8 * i)) & 0xFF));
            }
            return;
        }
        // Text
        serial_type = 13 + 2 * static_cast<uint64_t>(field.size());
        body.insert(body.end(), field.begin(), field.end());
    }

    static std::vector<uint8_t> make_record(const Record & record) {
        std::vector<uint8_t> serials;
        std::vector<uint8_t> body;
        for (const auto & field : record) {
            uint64_t serial = 0;
            encode_field(field, serial, body);
            append_varint(serials, serial);
        }
        // Header size includes the size varint itself; single-byte size suffices
        // for test records (header < 128 bytes)
        uint64_t header_size = static_cast<uint64_t>(serials.size()) + 1;
        std::vector<uint8_t> out;
        append_varint(out, header_size);
        out.insert(out.end(), serials.begin(), serials.end());
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    // Build a leaf table b-tree page. When is_page1 is true the b-tree header
    // starts at offset 100 (after the database header written separately).
    std::vector<uint8_t> make_table_leaf_page(const std::vector<Record> & rows,
                                              bool is_page1) {
        std::vector<uint8_t> page(page_size_, 0);
        size_t hdr = is_page1 ? 100 : 0;
        page[hdr + 0] = 0x0D;
        page[hdr + 1] = 0;
        page[hdr + 2] = 0; // first freeblock
        put_u16(page, hdr + 3, static_cast<uint16_t>(rows.size()));

        size_t cell_ptr_array = hdr + 8;
        size_t cursor = page_size_;
        for (size_t i = 0; i < rows.size(); i++) {
            auto record = make_record(rows[i]);
            std::vector<uint8_t> cell;
            append_varint(cell, static_cast<uint64_t>(record.size()));
            append_varint(cell, static_cast<uint64_t>(i + 1)); // rowid
            cell.insert(cell.end(), record.begin(), record.end());

            cursor -= cell.size();
            std::copy(cell.begin(), cell.end(), page.begin() + cursor);
            put_u16(page, cell_ptr_array + 2 * i, static_cast<uint16_t>(cursor));
        }
        put_u16(page, hdr + 5,
                static_cast<uint16_t>(rows.empty() ? page_size_ : cursor));
        page[hdr + 7] = 0; // fragmented free bytes
        return page;
    }

    static std::string extract_table_name(const std::string & create_sql) {
        // "CREATE TABLE <name> (...)" - <name> may be quoted
        size_t pos = create_sql.find("TABLE");
        if (pos == std::string::npos) return "";
        pos += 5;
        while (pos < create_sql.size() && create_sql[pos] == ' ') pos++;
        if (pos >= create_sql.size()) return "";
        size_t end = pos;
        if (create_sql[pos] == '"' || create_sql[pos] == '[' || create_sql[pos] == '`') {
            char open = create_sql[pos];
            char close = open == '[' ? ']' : open;
            end = create_sql.find(close, pos + 1);
            if (end == std::string::npos) return "";
            return create_sql.substr(pos + 1, end - pos - 1);
        }
        while (end < create_sql.size() &&
               create_sql[end] != ' ' && create_sql[end] != '(') {
            end++;
        }
        return create_sql.substr(pos, end - pos);
    }

    void write_header(std::vector<uint8_t> & page1, uint32_t db_pages) {
        static const char magic[16] = {'S','Q','L','i','t','e',' ','f','o','r','m','a','t',' ','3','\0'};
        std::memcpy(page1.data(), magic, 16);
        put_u16(page1, 16, static_cast<uint16_t>(page_size_));
        page1[18] = 1; // file format write version (legacy)
        page1[19] = 1; // file format read version (legacy)
        page1[20] = 0; // reserved space per page
        page1[21] = 64; // max payload fraction
        page1[22] = 32; // min payload fraction
        page1[23] = 32; // leaf payload fraction
        put_u32(page1, 24, 1); // file change counter
        put_u32(page1, 28, db_pages);
        put_u32(page1, 96, 3040001); // SQLITE_VERSION_NUMBER
    }
};

// Builds the sample GnuCash SQLite database used by the tests below
static std::string build_sample_gnucash_sqlite() {
    SqliteTestFileBuilder builder;
    builder.add_table(
        "CREATE TABLE commodities (guid TEXT, namespace TEXT, mnemonic TEXT)",
        {
            {"comm-guid-usd-0000000000000000001", "CURRENCY", "USD"},
            {"comm-guid-eur-0000000000000000002", "CURRENCY", "EUR"},
        });
    builder.add_table(
        "CREATE TABLE accounts (guid TEXT, name TEXT, account_type TEXT, "
        "parent_guid TEXT, code TEXT, description TEXT)",
        {
            {"acc-guid-bank-0000000000000000001", "Checking Account", "BANK",
             "", "1000", "Main checking"},
            {"acc-guid-sale-0000000000000000002", "Sales Income", "INCOME",
             "", "4000", "Revenue from sales"},
            {"acc-guid-rent-0000000000000000003", "Rent Expense", "EXPENSE",
             "", "5100", ""},
            {"acc-guid-loan-0000000000000000004", "Bank Loan", "LIABILITY",
             "", "2000", ""},
        });
    builder.add_table(
        "CREATE TABLE transactions (guid TEXT, currency_guid TEXT, num TEXT, "
        "post_date TEXT, description TEXT)",
        {
            {"tx-guid-00001-000000000000000000001", "comm-guid-usd-0000000000000000001",
             "INV-100", "2024-01-15 10:30:00", "Client payment"},
            {"tx-guid-00002-000000000000000000002", "comm-guid-eur-0000000000000000002",
             "RENT-JAN", "2024-02-01 09:00:00", "January rent"},
        });
    builder.add_table(
        "CREATE TABLE splits (guid TEXT, tx_guid TEXT, account_guid TEXT, memo TEXT, "
        "reconciled_state TEXT, value_num TEXT, value_denom TEXT)",
        {
            {"split-guid-1a", "tx-guid-00001-000000000000000000001",
             "acc-guid-bank-0000000000000000001", "Deposit", "y",
             "i:150000", "i:100"},
            {"split-guid-1b", "tx-guid-00001-000000000000000000001",
             "acc-guid-sale-0000000000000000002", "Sale", "n",
             "i:-150000", "i:100"},
            {"split-guid-2a", "tx-guid-00002-000000000000000000002",
             "acc-guid-rent-0000000000000000003", "", "c",
             "i:250000", "i:100"},
            {"split-guid-2b", "tx-guid-00002-000000000000000000002",
             "acc-guid-bank-0000000000000000001", "", "n",
             "i:-250000", "i:100"},
        });
    return builder.build();
}

void test_sqlite_reader_accounts() {
    TEST("GnuCash SQLite connector imports accounts");

    std::string db = build_sample_gnucash_sqlite();
    ASSERT_TRUE(db.size() >= 4096);

    GnuCashSqliteConnector connector;
    auto result = connector.import_from_bytes(db);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.accounts_imported, (uint64_t)4);

    const auto & bank = result.accounts[0];
    ASSERT_EQ(bank.external_id, "acc-guid-bank-0000000000000000001");
    ASSERT_EQ(bank.name, "Checking Account");
    ASSERT_EQ(bank.external_code, "1000");
    ASSERT_EQ(bank.type, "Asset");
    ASSERT_EQ(bank.description, "Main checking");

    ASSERT_EQ(result.accounts[1].type, "Revenue");
    ASSERT_EQ(result.accounts[2].type, "Expense");
    ASSERT_EQ(result.accounts[3].type, "Liability");

    TEST_END("GnuCash SQLite connector imports accounts");
}

void test_sqlite_reader_transactions() {
    TEST("GnuCash SQLite connector imports transactions with splits");

    std::string db = build_sample_gnucash_sqlite();

    GnuCashSqliteConnector connector;
    auto result = connector.import_from_bytes(db);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.transactions_imported, (uint64_t)2);

    const auto & tx1 = result.transactions[0];
    ASSERT_EQ(tx1.external_id, "tx-guid-00001-000000000000000000001");
    ASSERT_EQ(tx1.description, "Client payment");
    ASSERT_EQ(tx1.reference, "INV-100");
    ASSERT_EQ(tx1.date, "2024-01-15");
    ASSERT_EQ(tx1.currency, "USD");
    ASSERT_EQ(tx1.source_system, "GnuCash SQLite");
    ASSERT_EQ(tx1.lines.size(), (size_t)2);
    ASSERT_EQ(tx1.lines[0].account_code, "1000");
    ASSERT_NEAR(tx1.lines[0].debit_amount, 1500.0, 0.01);
    ASSERT_EQ(tx1.lines[0].memo, "Deposit");
    ASSERT_EQ(tx1.lines[0].reconciled, "y");
    ASSERT_EQ(tx1.lines[1].account_code, "4000");
    ASSERT_NEAR(tx1.lines[1].credit_amount, 1500.0, 0.01);
    ASSERT_TRUE(tx1.is_balanced());

    const auto & tx2 = result.transactions[1];
    ASSERT_EQ(tx2.date, "2024-02-01");
    ASSERT_EQ(tx2.currency, "EUR");
    ASSERT_TRUE(tx2.is_balanced());

    TEST_END("GnuCash SQLite connector imports transactions with splits");
}

void test_sqlite_reader_malformed_input() {
    TEST("GnuCash SQLite connector rejects malformed input safely");

    GnuCashSqliteConnector connector;

    // Empty input
    auto r1 = connector.import_from_bytes("");
    ASSERT_FALSE(r1.success);
    ASSERT_FALSE(r1.errors.empty());

    // Bad magic
    std::string bad_magic(4096, 'x');
    auto r2 = connector.import_from_bytes(bad_magic);
    ASSERT_FALSE(r2.success);

    // Truncated header
    std::string truncated(50, '\0');
    auto r3 = connector.import_from_bytes(truncated);
    ASSERT_FALSE(r3.success);

    // Valid magic but nonsense page size field
    std::string bad_page_size(4096, '\0');
    std::memcpy(&bad_page_size[0], "SQLite format 3", 15);
    bad_page_size[15] = '\0';
    bad_page_size[16] = 0x00;
    bad_page_size[17] = 0x03; // page size 3 - invalid (not power of two)
    auto r4 = connector.import_from_bytes(bad_page_size);
    ASSERT_FALSE(r4.success);

    // Valid header, but sqlite_master page is garbage
    std::string garbage_master(4096, '\0');
    std::memcpy(&garbage_master[0], "SQLite format 3", 15);
    garbage_master[15] = '\0';
    garbage_master[16] = 0x10; // page size 4096
    garbage_master[17] = 0x00;
    garbage_master[18] = 1;
    garbage_master[19] = 1;
    garbage_master[100] = 0xFF; // invalid page type
    auto r5 = connector.import_from_bytes(garbage_master);
    ASSERT_FALSE(r5.success);

    TEST_END("GnuCash SQLite connector rejects malformed input safely");
}

void test_sqlite_reader_missing_tables() {
    TEST("GnuCash SQLite connector fails cleanly when accounts table is absent");

    SqliteTestFileBuilder builder;
    builder.add_table(
        "CREATE TABLE something_else (guid TEXT, name TEXT)",
        {{"g1", "n1"}});
    std::string db = builder.build();

    GnuCashSqliteConnector connector;
    auto result = connector.import_from_bytes(db);
    ASSERT_FALSE(result.success);
    ASSERT_FALSE(result.errors.empty());

    TEST_END("GnuCash SQLite connector fails cleanly when accounts table is absent");
}

void test_factory_phase_a2_connectors() {
    TEST("ConnectorFactory creates Phase A.2 connector types");

    auto sqlite = ConnectorFactory::create(ConnectorType::GNUCASH_SQLITE);
    ASSERT_TRUE(sqlite != nullptr);
    ASSERT_EQ(sqlite->get_type(), ConnectorType::GNUCASH_SQLITE);
    ASSERT_EQ(sqlite->get_type_name(), "GnuCash SQLite");

    auto xero = ConnectorFactory::create(ConnectorType::XERO_API);
    ASSERT_TRUE(xero != nullptr);
    ASSERT_EQ(xero->get_type(), ConnectorType::XERO_API);

    auto erpnext = ConnectorFactory::create(ConnectorType::ERPNEXT_API);
    ASSERT_TRUE(erpnext != nullptr);
    ASSERT_EQ(erpnext->get_type(), ConnectorType::ERPNEXT_API);

    // Extension detection for file-based SQLite
    ASSERT_EQ(ConnectorFactory::detect_type("books.sqlite"), ConnectorType::GNUCASH_SQLITE);
    ASSERT_EQ(ConnectorFactory::detect_type("books.db"), ConnectorType::GNUCASH_SQLITE);
    ASSERT_EQ(ConnectorFactory::detect_type("books.sqlite3"), ConnectorType::GNUCASH_SQLITE);
    ASSERT_EQ(ConnectorFactory::detect_type("BOOKS.SQLITE"), ConnectorType::GNUCASH_SQLITE);

    // API connectors are not auto-detected from extensions
    ASSERT_EQ(ConnectorFactory::detect_type("xero.json"), ConnectorType::CUSTOM);

    auto for_file = ConnectorFactory::create_for_file("company.sqlite");
    ASSERT_TRUE(for_file != nullptr);
    ASSERT_EQ(for_file->get_type(), ConnectorType::GNUCASH_SQLITE);

    TEST_END("ConnectorFactory creates Phase A.2 connector types");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Financial Data Connector Tests (Phase A.2)" << std::endl;
    std::cout << "============================================" << std::endl;

    std::cout << "\n--- CSV Connector Tests ---" << std::endl;
    test_csv_basic_import();
    test_csv_amount_column();
    test_csv_with_account_column();
    test_csv_default_account();
    test_csv_quoted_fields();
    test_csv_skip_rows();
    test_csv_invalid_amounts();
    test_csv_empty_content();
    test_csv_tsv_delimiter();
    test_csv_reference_column();
    test_csv_supported_extensions();

    std::cout << "\n--- GnuCash XML Connector Tests ---" << std::endl;
    test_gnucash_xml_basic();
    test_gnucash_xml_account_types();
    test_gnucash_xml_multiple_transactions();
    test_gnucash_xml_supported_extensions();

    std::cout << "\n--- Beancount Connector Tests ---" << std::endl;
    test_beancount_basic_import();
    test_beancount_account_types();
    test_beancount_comments_and_empty_lines();
    test_beancount_supported_extensions();
    test_beancount_multiple_transactions();

    std::cout << "\n--- DataNormalizer Tests ---" << std::endl;
    test_normalizer_explicit_mapping();
    test_normalizer_auto_map();
    test_normalizer_transaction_conversion();
    test_normalizer_batch_conversion();
    test_normalizer_unmapped_codes();
    test_normalizer_clear_mappings();

    std::cout << "\n--- ConnectorFactory Tests ---" << std::endl;
    test_factory_type_detection();
    test_factory_create_connector();
    test_factory_create_for_file();
    test_factory_phase_a2_connectors();

    std::cout << "\n--- JSON Parser Tests ---" << std::endl;
    test_json_parser_basic();
    test_json_parser_escapes_and_unicode();
    test_json_parser_errors();

    std::cout << "\n--- Xero Connector Tests ---" << std::endl;
    test_xero_accounts_import();
    test_xero_bank_transactions_import();
    test_xero_malformed_and_empty();
    test_xero_oauth2_request_builder();

    std::cout << "\n--- ERPNext Connector Tests ---" << std::endl;
    test_erpnext_accounts_import();
    test_erpnext_journal_entries_import();
    test_erpnext_malformed_and_mixed();
    test_erpnext_api_request_builder();

    std::cout << "\n--- GnuCash SQLite Connector Tests ---" << std::endl;
    test_sqlite_reader_accounts();
    test_sqlite_reader_transactions();
    test_sqlite_reader_malformed_input();
    test_sqlite_reader_missing_tables();

    std::cout << "\n--- Import Result Tests ---" << std::endl;
    test_import_result_summary();
    test_imported_transaction_balance_check();
    test_connector_type_strings();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
