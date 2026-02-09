#include "financial-data-connector.h"

#include <cassert>
#include <cmath>
#include <cstdio>
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
