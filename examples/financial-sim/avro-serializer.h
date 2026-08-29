#pragma once

#include "financial-data-connector.h"
#include <cstdint>
#include <string>
#include <vector>

// ============================================================================
// Apache Avro Serializer - Phase A.2 cross-language data exchange
//
// Implements the Avro 1.11 primitive encodings needed to exchange
// ImportedAccount / ImportedTransaction records with other systems
// (the cogflu Service Provider Interface pattern):
//
//   - Binary encoding: zig-zag varint longs, length-prefixed bytes/strings,
//     8-byte little-endian doubles, count-prefixed arrays and maps
//   - JSON encoding:  standard Avro JSON (strings quoted, longs/doubles as
//     JSON numbers, maps as objects, arrays as JSON arrays)
//
// Schemas for both record types are provided as Avro .avsc JSON strings
// (avro_schema_account() / avro_schema_transaction()). The binary form is
// "single datum" encoding - no Object Container File framing, no schema
// fingerprint prefix - so payloads must be exchanged together with the schema.
//
// All decode paths are bounds-checked: malformed or truncated input fails
// gracefully with false / an empty optional, never an out-of-bounds read.
// ============================================================================

namespace ggnucash {
namespace avro {

// ============================================================================
// Schemas (.avsc JSON) for the exchange record types
// ============================================================================

// Avro schema for connector::ImportedAccount
const char * avro_schema_account();

// Avro schema for connector::ImportedTransaction (nested Line record)
const char * avro_schema_transaction();

// ============================================================================
// AvroSerializer - encode/decode of imported records
// ============================================================================

class AvroSerializer {
public:
    AvroSerializer();

    // ---- Primitive encoding (Avro binary) ----

    // Zig-zag varint long: (n << 1) ^ (n >> 63), then 7-bit little-endian groups
    static std::vector<uint8_t> encode_long(int64_t value);
    static bool                 decode_long(const uint8_t * data, size_t size,
                                            int64_t & value, size_t & bytes_read);

    // Length-prefixed UTF-8 string (long byte count + raw bytes)
    static std::vector<uint8_t> encode_string(const std::string & value);
    static bool                 decode_string(const uint8_t * data, size_t size,
                                              std::string & value, size_t & bytes_read);

    // 8-byte little-endian IEEE-754 double
    static std::vector<uint8_t> encode_double(double value);
    static bool                 decode_double(const uint8_t * data, size_t size,
                                              double & value, size_t & bytes_read);

    // Length-prefixed raw bytes
    static std::vector<uint8_t> encode_bytes(const uint8_t * data, size_t size);
    static bool                 decode_bytes(const uint8_t * data, size_t size,
                                             std::vector<uint8_t> & value, size_t & bytes_read);

    // ---- Record encoding (Avro binary, field order per schema) ----

    static std::vector<uint8_t> encode_account(const connector::ImportedAccount & account);
    static bool                 decode_account(const uint8_t * data, size_t size,
                                               connector::ImportedAccount & account,
                                               size_t & bytes_read);

    static std::vector<uint8_t> encode_transaction(const connector::ImportedTransaction & tx);
    static bool                 decode_transaction(const uint8_t * data, size_t size,
                                                   connector::ImportedTransaction & tx,
                                                   size_t & bytes_read);

    // Convenience wrappers over the byte-buffer forms
    static std::vector<uint8_t> encode_accounts(
        const std::vector<connector::ImportedAccount> & accounts);
    static std::vector<uint8_t> encode_transactions(
        const std::vector<connector::ImportedTransaction> & transactions);

    // ---- JSON encoding (Avro JSON encoding rules) ----

    static std::string account_to_json(const connector::ImportedAccount & account);
    static std::string transaction_to_json(const connector::ImportedTransaction & tx);

    static bool account_from_json(const std::string & json,
                                  connector::ImportedAccount & account);
    static bool transaction_from_json(const std::string & json,
                                      connector::ImportedTransaction & tx);

private:
    // Encode a string->string map (block form: count, pairs, terminator)
    static void append_map(std::vector<uint8_t> & out,
                           const std::map<std::string, std::string> & map);
    static bool decode_map(const uint8_t * data, size_t size,
                           std::map<std::string, std::string> & map,
                           size_t & bytes_read);

    static void append_bytes(std::vector<uint8_t> & out, const uint8_t * data, size_t size);

    static std::string json_escape(const std::string & value);
};

} // namespace avro
} // namespace ggnucash
