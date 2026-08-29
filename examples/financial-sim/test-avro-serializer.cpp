#include "avro-serializer.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace ggnucash::avro;
using ggnucash::connector::ImportedAccount;
using ggnucash::connector::ImportedTransaction;

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
// Helpers
// ============================================================================

static int64_t decode_long_full(const std::vector<uint8_t> & bytes) {
    int64_t value = 0;
    size_t read = 0;
    bool ok = AvroSerializer::decode_long(bytes.data(), bytes.size(), value, read);
    if (!ok || read != bytes.size()) {
        throw std::runtime_error("decode_long did not consume the full buffer");
    }
    return value;
}

static void assert_account_eq(const ImportedAccount & a, const ImportedAccount & b) {
    ASSERT_EQ(a.external_id, b.external_id);
    ASSERT_EQ(a.external_code, b.external_code);
    ASSERT_EQ(a.name, b.name);
    ASSERT_EQ(a.type, b.type);
    ASSERT_EQ(a.parent_id, b.parent_id);
    ASSERT_EQ(a.currency, b.currency);
    ASSERT_EQ(a.description, b.description);
    ASSERT_EQ(a.metadata, b.metadata);
}

static void assert_line_eq(const ImportedTransaction::Line & a,
                           const ImportedTransaction::Line & b) {
    ASSERT_EQ(a.account_code, b.account_code);
    ASSERT_EQ(a.debit_amount, b.debit_amount);   // exact: binary doubles round-trip
    ASSERT_EQ(a.credit_amount, b.credit_amount);
    ASSERT_EQ(a.memo, b.memo);
    ASSERT_EQ(a.reconciled, b.reconciled);
}

static void assert_transaction_eq(const ImportedTransaction & a,
                                  const ImportedTransaction & b) {
    ASSERT_EQ(a.external_id, b.external_id);
    ASSERT_EQ(a.date, b.date);
    ASSERT_EQ(a.description, b.description);
    ASSERT_EQ(a.reference, b.reference);
    ASSERT_EQ(a.source_system, b.source_system);
    ASSERT_EQ(a.currency, b.currency);
    ASSERT_EQ(a.lines.size(), b.lines.size());
    for (size_t i = 0; i < a.lines.size(); i++) {
        assert_line_eq(a.lines[i], b.lines[i]);
    }
    ASSERT_EQ(a.metadata, b.metadata);
}

// ============================================================================
// Primitive Tests
// ============================================================================

void test_zigzag_edge_cases() {
    TEST("Avro long zig-zag varint edge cases");

    // 0 -> zigzag 0 -> single byte 0x00
    auto b0 = AvroSerializer::encode_long(0);
    ASSERT_EQ(b0.size(), (size_t)1);
    ASSERT_EQ(b0[0], (uint8_t)0x00);
    ASSERT_EQ(decode_long_full(b0), (int64_t)0);

    // -1 -> zigzag 1 -> 0x01
    auto bn1 = AvroSerializer::encode_long(-1);
    ASSERT_EQ(bn1.size(), (size_t)1);
    ASSERT_EQ(bn1[0], (uint8_t)0x01);
    ASSERT_EQ(decode_long_full(bn1), (int64_t)-1);

    // 1 -> zigzag 2 -> 0x02
    auto b1 = AvroSerializer::encode_long(1);
    ASSERT_EQ(b1.size(), (size_t)1);
    ASSERT_EQ(b1[0], (uint8_t)0x02);
    ASSERT_EQ(decode_long_full(b1), (int64_t)1);

    // 2 -> 0x04, -2 -> 0x03
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(2)), (int64_t)2);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(-2)), (int64_t)-2);

    // INT64_MAX -> zigzag 0xFFFFFFFFFFFFFFFE (63 significant bits) -> 10 bytes
    auto bmax = AvroSerializer::encode_long(std::numeric_limits<int64_t>::max());
    ASSERT_EQ(bmax.size(), (size_t)10);
    ASSERT_EQ(decode_long_full(bmax), std::numeric_limits<int64_t>::max());

    // INT64_MIN -> zigzag 0xFFFFFFFFFFFFFFFF -> 10 bytes
    auto bmin = AvroSerializer::encode_long(std::numeric_limits<int64_t>::min());
    ASSERT_EQ(bmin.size(), (size_t)10);
    ASSERT_EQ(decode_long_full(bmin), std::numeric_limits<int64_t>::min());

    // Powers of two boundaries
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(63)), (int64_t)63);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(64)), (int64_t)64);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(-64)), (int64_t)-64);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(-65)), (int64_t)-65);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(1000000)), (int64_t)1000000);
    ASSERT_EQ(decode_long_full(AvroSerializer::encode_long(-123456789)), (int64_t)-123456789);

    TEST_END("Avro long zig-zag varint edge cases");
}

void test_zigzag_known_vectors() {
    TEST("Avro long known encoding vectors from the spec");

    // From the Avro 1.11 specification examples: 0->00, -1->01, 1->02, -2->03, 2->04
    ASSERT_EQ(AvroSerializer::encode_long(0),  std::vector<uint8_t>({0x00}));
    ASSERT_EQ(AvroSerializer::encode_long(-1), std::vector<uint8_t>({0x01}));
    ASSERT_EQ(AvroSerializer::encode_long(1),  std::vector<uint8_t>({0x02}));
    ASSERT_EQ(AvroSerializer::encode_long(-2), std::vector<uint8_t>({0x03}));
    ASSERT_EQ(AvroSerializer::encode_long(2),  std::vector<uint8_t>({0x04}));
    // 64 -> zigzag 128 -> two bytes 0x80 0x01
    ASSERT_EQ(AvroSerializer::encode_long(64), std::vector<uint8_t>({0x80, 0x01}));

    TEST_END("Avro long known encoding vectors from the spec");
}

void test_string_round_trip() {
    TEST("Avro string encode/decode round trip");

    const std::vector<std::string> samples = {
        "",
        "a",
        "hello world",
        "unicode: caf\u00e9 \u00fc\u00df \u4e2d\u6587",
        std::string(300, 'x'), // longer than one varint length byte
    };

    for (const auto & s : samples) {
        auto encoded = AvroSerializer::encode_string(s);
        std::string decoded;
        size_t read = 0;
        ASSERT_TRUE(AvroSerializer::decode_string(encoded.data(), encoded.size(),
                                                  decoded, read));
        ASSERT_EQ(decoded, s);
        ASSERT_EQ(read, encoded.size());
    }

    TEST_END("Avro string encode/decode round trip");
}

void test_double_round_trip() {
    TEST("Avro double encode/decode round trip");

    const std::vector<double> samples = {
        0.0, 1.0, -1.0, 3.141592653589793, -273.15, 1e308, 1e-308,
        std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(),
        100.25, -9999999.99,
    };

    for (double d : samples) {
        auto encoded = AvroSerializer::encode_double(d);
        ASSERT_EQ(encoded.size(), (size_t)8);
        double decoded = 0.0;
        size_t read = 0;
        ASSERT_TRUE(AvroSerializer::decode_double(encoded.data(), encoded.size(),
                                                  decoded, read));
        ASSERT_EQ(decoded, d); // bit-exact round trip
        ASSERT_EQ(read, (size_t)8);
    }

    // IEEE-754 little-endian spot check: 1.0 = 0x3FF0000000000000
    auto one = AvroSerializer::encode_double(1.0);
    ASSERT_EQ(one[6], (uint8_t)0xF0);
    ASSERT_EQ(one[7], (uint8_t)0x3F);
    ASSERT_EQ(one[0], (uint8_t)0x00);

    TEST_END("Avro double encode/decode round trip");
}

void test_truncated_inputs_fail() {
    TEST("Avro decoders reject truncated input safely");

    // Truncated varint (continuation bit set, then nothing)
    std::vector<uint8_t> trunc_varint = {0x80};
    int64_t lv = 0;
    size_t read = 0;
    ASSERT_FALSE(AvroSerializer::decode_long(trunc_varint.data(),
                                             trunc_varint.size(), lv, read));

    // Truncated string: declared length 10, only 2 bytes present
    std::vector<uint8_t> trunc_string = {0x14, 'a', 'b'}; // 0x14 = zigzag(10)
    std::string sv;
    ASSERT_FALSE(AvroSerializer::decode_string(trunc_string.data(),
                                               trunc_string.size(), sv, read));

    // Negative length
    std::vector<uint8_t> neg_len = {0x01}; // zigzag(1) = -1
    ASSERT_FALSE(AvroSerializer::decode_string(neg_len.data(), neg_len.size(), sv, read));

    // Truncated double
    std::vector<uint8_t> trunc_double = {0x00, 0x11, 0x22};
    double dv = 0.0;
    ASSERT_FALSE(AvroSerializer::decode_double(trunc_double.data(),
                                               trunc_double.size(), dv, read));

    // Truncated record
    auto good = AvroSerializer::encode_long(5);
    ImportedAccount account;
    ASSERT_FALSE(AvroSerializer::decode_account(good.data(), good.size(), account, read));

    // Null buffer
    ASSERT_FALSE(AvroSerializer::decode_long(nullptr, 10, lv, read));
    ASSERT_FALSE(AvroSerializer::decode_double(nullptr, 8, dv, read));

    TEST_END("Avro decoders reject truncated input safely");
}

// ============================================================================
// Record Round-Trip Tests
// ============================================================================

void test_account_round_trip() {
    TEST("Avro ImportedAccount binary round trip");

    ImportedAccount account;
    account.external_id = "bd0e9484-bfd8-4a40-93b0-c4c5f7a30a01";
    account.external_code = "090";
    account.name = "Business Bank Account";
    account.type = "Asset";
    account.parent_id = "parent-guid-1234";
    account.currency = "USD";
    account.description = "Main operating account";
    account.metadata["xero_type"] = "BANK";
    account.metadata["source"] = "api";

    auto encoded = AvroSerializer::encode_account(account);

    ImportedAccount decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_account(encoded.data(), encoded.size(),
                                               decoded, read));
    ASSERT_EQ(read, encoded.size());
    assert_account_eq(account, decoded);

    TEST_END("Avro ImportedAccount binary round trip");
}

void test_account_empty_fields_round_trip() {
    TEST("Avro ImportedAccount with empty fields and empty metadata");

    ImportedAccount account;
    // All string fields left empty, metadata empty, default currency "USD"
    account.external_id = "only-id";
    account.metadata.clear();

    auto encoded = AvroSerializer::encode_account(account);

    ImportedAccount decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_account(encoded.data(), encoded.size(),
                                               decoded, read));
    ASSERT_EQ(read, encoded.size());
    assert_account_eq(account, decoded);
    ASSERT_TRUE(decoded.metadata.empty());

    TEST_END("Avro ImportedAccount with empty fields and empty metadata");
}

void test_account_unicode_round_trip() {
    TEST("Avro ImportedAccount unicode round trip");

    ImportedAccount account;
    account.external_id = "acc-\u4e2d\u6587";
    account.name = "Compte R\u00e9sultat \u00c9tranger";
    account.type = "Expense";
    account.description = "Beschl\u00fcsselung f\u00fcr \u00dcberweisungen";
    account.metadata["notiz"] = "Gr\u00fc\u00dfe aus M\u00fcnchen \u2014 \u20ac100";

    auto encoded = AvroSerializer::encode_account(account);

    ImportedAccount decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_account(encoded.data(), encoded.size(),
                                               decoded, read));
    assert_account_eq(account, decoded);

    TEST_END("Avro ImportedAccount unicode round trip");
}

void test_transaction_round_trip_multi_line() {
    TEST("Avro ImportedTransaction binary round trip with multiple lines");

    ImportedTransaction tx;
    tx.external_id = "JV-2024-00099";
    tx.date = "2024-03-31";
    tx.description = "Quarterly VAT settlement";
    tx.reference = "VAT-Q1-2024";
    tx.source_system = "ERPNext";
    tx.currency = "EUR";
    tx.metadata["total_debit"] = "11500.00";
    tx.metadata["total_credit"] = "11500.00";

    ImportedTransaction::Line l1;
    l1.account_code = "2200";
    l1.debit_amount = 11500.00;
    l1.memo = "VAT output tax";
    l1.reconciled = "y";
    tx.lines.push_back(l1);

    ImportedTransaction::Line l2;
    l2.account_code = "1300";
    l2.credit_amount = 10000.00;
    l2.memo = "Net liability";
    tx.lines.push_back(l2);

    ImportedTransaction::Line l3;
    l3.account_code = "1310";
    l3.credit_amount = 1500.00;
    l3.memo = "Penalty accrual";
    l3.reconciled = "c";
    tx.lines.push_back(l3);

    auto encoded = AvroSerializer::encode_transaction(tx);

    ImportedTransaction decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_transaction(encoded.data(), encoded.size(),
                                                   decoded, read));
    ASSERT_EQ(read, encoded.size());
    assert_transaction_eq(tx, decoded);
    ASSERT_TRUE(decoded.is_balanced());

    TEST_END("Avro ImportedTransaction binary round trip with multiple lines");
}

void test_transaction_negative_amounts_round_trip() {
    TEST("Avro ImportedTransaction with negative amounts");

    ImportedTransaction tx;
    tx.external_id = "NEG-001";
    tx.date = "2024-04-01";
    tx.description = "Reversal entry";
    tx.source_system = "GnuCash SQLite";

    ImportedTransaction::Line l1;
    l1.account_code = "1000";
    l1.debit_amount = -250.75; // stored verbatim, not normalized
    tx.lines.push_back(l1);

    ImportedTransaction::Line l2;
    l2.account_code = "4000";
    l2.credit_amount = -250.75;
    tx.lines.push_back(l2);

    auto encoded = AvroSerializer::encode_transaction(tx);

    ImportedTransaction decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_transaction(encoded.data(), encoded.size(),
                                                   decoded, read));
    assert_transaction_eq(tx, decoded);
    ASSERT_NEAR(decoded.lines[0].debit_amount, -250.75, 0.0001);
    ASSERT_NEAR(decoded.lines[1].credit_amount, -250.75, 0.0001);

    TEST_END("Avro ImportedTransaction with negative amounts");
}

void test_transaction_no_lines_round_trip() {
    TEST("Avro ImportedTransaction with no lines and empty metadata");

    ImportedTransaction tx;
    tx.external_id = "EMPTY-001";
    tx.date = "2024-01-01";
    tx.metadata.clear();

    auto encoded = AvroSerializer::encode_transaction(tx);

    ImportedTransaction decoded;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_transaction(encoded.data(), encoded.size(),
                                                   decoded, read));
    ASSERT_EQ(read, encoded.size());
    assert_transaction_eq(tx, decoded);
    ASSERT_TRUE(decoded.lines.empty());

    TEST_END("Avro ImportedTransaction with no lines and empty metadata");
}

void test_transaction_sequence_round_trip() {
    TEST("Avro transaction array encode round trip");

    std::vector<ImportedTransaction> txs;
    for (int i = 0; i < 3; i++) {
        ImportedTransaction tx;
        tx.external_id = "TX-" + std::to_string(i);
        tx.date = "2024-05-0" + std::to_string(i + 1);
        tx.description = "Transaction number " + std::to_string(i);
        ImportedTransaction::Line line;
        line.account_code = "ACC" + std::to_string(i);
        line.debit_amount = 100.0 * (i + 1);
        tx.lines.push_back(line);
        txs.push_back(tx);
    }

    auto encoded = AvroSerializer::encode_transactions(txs);
    ASSERT_FALSE(encoded.empty());

    // Walk the array manually: block count, then records, then terminator
    size_t offset = 0;
    int64_t count = 0;
    size_t read = 0;
    ASSERT_TRUE(AvroSerializer::decode_long(encoded.data() + offset,
                                            encoded.size() - offset, count, read));
    offset += read;
    ASSERT_EQ(count, (int64_t)3);

    for (int64_t i = 0; i < count; i++) {
        ImportedTransaction decoded;
        ASSERT_TRUE(AvroSerializer::decode_transaction(encoded.data() + offset,
                                                       encoded.size() - offset,
                                                       decoded, read));
        offset += read;
        assert_transaction_eq(txs[static_cast<size_t>(i)], decoded);
    }

    int64_t terminator = -1;
    ASSERT_TRUE(AvroSerializer::decode_long(encoded.data() + offset,
                                            encoded.size() - offset,
                                            terminator, read));
    offset += read;
    ASSERT_EQ(terminator, (int64_t)0);
    ASSERT_EQ(offset, encoded.size());

    TEST_END("Avro transaction array encode round trip");
}

// ============================================================================
// JSON Encoding Tests
// ============================================================================

void test_account_json_round_trip() {
    TEST("Avro account JSON encoding round trip");

    ImportedAccount account;
    account.external_id = "json-acc-1";
    account.external_code = "1050";
    account.name = "Savings \"High Yield\"";
    account.type = "Asset";
    account.parent_id = "";
    account.currency = "ZAR";
    account.description = "Line1\nLine2 with\ttabs";
    account.metadata["region"] = "ZA";
    account.metadata["note"] = "back\\slash";

    std::string json = AvroSerializer::account_to_json(account);

    ImportedAccount decoded;
    ASSERT_TRUE(AvroSerializer::account_from_json(json, decoded));
    assert_account_eq(account, decoded);

    TEST_END("Avro account JSON encoding round trip");
}

void test_transaction_json_round_trip() {
    TEST("Avro transaction JSON encoding round trip");

    ImportedTransaction tx;
    tx.external_id = "json-tx-1";
    tx.date = "2024-06-15";
    tx.description = "JSON \"quoted\" description";
    tx.reference = "REF-42";
    tx.source_system = "Xero";
    tx.currency = "GBP";
    tx.metadata["k"] = "v";

    ImportedTransaction::Line line;
    line.account_code = "700";
    line.debit_amount = 42.5;
    line.credit_amount = 0.0;
    line.memo = "memo \u00e9";
    tx.lines.push_back(line);

    std::string json = AvroSerializer::transaction_to_json(tx);

    ImportedTransaction decoded;
    ASSERT_TRUE(AvroSerializer::transaction_from_json(json, decoded));
    ASSERT_EQ(decoded.external_id, tx.external_id);
    ASSERT_EQ(decoded.description, tx.description);
    ASSERT_EQ(decoded.reference, tx.reference);
    ASSERT_EQ(decoded.lines.size(), (size_t)1);
    ASSERT_EQ(decoded.lines[0].account_code, "700");
    ASSERT_NEAR(decoded.lines[0].debit_amount, 42.5, 0.0001);
    ASSERT_EQ(decoded.lines[0].memo, line.memo);
    ASSERT_EQ(decoded.metadata, tx.metadata);

    TEST_END("Avro transaction JSON encoding round trip");
}

void test_json_from_malformed_fails() {
    TEST("Avro JSON decoders reject malformed input");

    ImportedAccount account;
    ASSERT_FALSE(AvroSerializer::account_from_json("{not json", account));
    ASSERT_FALSE(AvroSerializer::account_from_json("[1,2,3]", account));

    ImportedTransaction tx;
    ASSERT_FALSE(AvroSerializer::transaction_from_json("", tx));
    ASSERT_FALSE(AvroSerializer::transaction_from_json("42", tx));

    TEST_END("Avro JSON decoders reject malformed input");
}

// ============================================================================
// Schema Tests
// ============================================================================

void test_schemas_present_and_parseable() {
    TEST("Avro schemas are present and valid JSON");

    std::string account_schema = avro_schema_account();
    std::string tx_schema = avro_schema_transaction();

    ASSERT_TRUE(account_schema.find("\"ImportedAccount\"") != std::string::npos);
    ASSERT_TRUE(account_schema.find("\"record\"") != std::string::npos);
    ASSERT_TRUE(account_schema.find("\"metadata\"") != std::string::npos);
    ASSERT_TRUE(tx_schema.find("\"ImportedTransaction\"") != std::string::npos);
    ASSERT_TRUE(tx_schema.find("\"Line\"") != std::string::npos);
    ASSERT_TRUE(tx_schema.find("\"debit_amount\"") != std::string::npos);

    // Both schemas must parse as valid JSON with the connector's own parser
    ggnucash::connector::JsonParser parser;
    ggnucash::connector::JsonValue root;
    std::string error;
    ASSERT_TRUE(parser.parse(account_schema, root, error));
    ASSERT_EQ(root.get("name").as_string(), "ImportedAccount");
    ASSERT_TRUE(parser.parse(tx_schema, root, error));
    ASSERT_EQ(root.get("name").as_string(), "ImportedTransaction");

    TEST_END("Avro schemas are present and valid JSON");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "============================================" << std::endl;
    std::cout << "  Avro Serializer Tests (Phase A.2)" << std::endl;
    std::cout << "============================================" << std::endl;

    std::cout << "\n--- Primitive Encoding Tests ---" << std::endl;
    test_zigzag_edge_cases();
    test_zigzag_known_vectors();
    test_string_round_trip();
    test_double_round_trip();
    test_truncated_inputs_fail();

    std::cout << "\n--- Record Round-Trip Tests ---" << std::endl;
    test_account_round_trip();
    test_account_empty_fields_round_trip();
    test_account_unicode_round_trip();
    test_transaction_round_trip_multi_line();
    test_transaction_negative_amounts_round_trip();
    test_transaction_no_lines_round_trip();
    test_transaction_sequence_round_trip();

    std::cout << "\n--- JSON Encoding Tests ---" << std::endl;
    test_account_json_round_trip();
    test_transaction_json_round_trip();
    test_json_from_malformed_fails();

    std::cout << "\n--- Schema Tests ---" << std::endl;
    test_schemas_present_and_parseable();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
