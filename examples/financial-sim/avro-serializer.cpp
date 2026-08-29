#include "avro-serializer.h"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace ggnucash {
namespace avro {

using connector::ImportedAccount;
using connector::ImportedTransaction;

// ============================================================================
// Avro Schemas (.avsc)
// ============================================================================

const char * avro_schema_account() {
    return R"avsc({
  "type": "record",
  "name": "ImportedAccount",
  "namespace": "ggnucash.connector",
  "doc": "Normalized account record exchanged between GGNuCash and external systems",
  "fields": [
    {"name": "external_id",   "type": "string"},
    {"name": "external_code", "type": "string"},
    {"name": "name",          "type": "string"},
    {"name": "type",          "type": "string"},
    {"name": "parent_id",     "type": "string"},
    {"name": "currency",      "type": "string"},
    {"name": "description",   "type": "string"},
    {"name": "metadata",      "type": {"type": "map", "values": "string"}}
  ]
})avsc";
}

const char * avro_schema_transaction() {
    return R"avsc({
  "type": "record",
  "name": "ImportedTransaction",
  "namespace": "ggnucash.connector",
  "doc": "Normalized transaction record exchanged between GGNuCash and external systems",
  "fields": [
    {"name": "external_id",   "type": "string"},
    {"name": "date",          "type": "string"},
    {"name": "description",   "type": "string"},
    {"name": "reference",     "type": "string"},
    {"name": "source_system", "type": "string"},
    {"name": "currency",      "type": "string"},
    {"name": "lines", "type": {"type": "array", "items": {
      "type": "record",
      "name": "Line",
      "fields": [
        {"name": "account_code",  "type": "string"},
        {"name": "debit_amount",  "type": "double"},
        {"name": "credit_amount", "type": "double"},
        {"name": "memo",          "type": "string"},
        {"name": "reconciled",    "type": "string"}
      ]
    }}},
    {"name": "metadata", "type": {"type": "map", "values": "string"}}
  ]
})avsc";
}

// ============================================================================
// Primitive Encoding
// ============================================================================

AvroSerializer::AvroSerializer() {}

std::vector<uint8_t> AvroSerializer::encode_long(int64_t value) {
    // Zig-zag transform: maps 0->0, -1->1, 1->2, -2->3, ...
    uint64_t zigzag = (static_cast<uint64_t>(value) << 1) ^
                      static_cast<uint64_t>(value >> 63);
    std::vector<uint8_t> out;
    out.reserve(10);
    while ((zigzag & ~static_cast<uint64_t>(0x7F)) != 0) {
        out.push_back(static_cast<uint8_t>((zigzag & 0x7F) | 0x80));
        zigzag >>= 7;
    }
    out.push_back(static_cast<uint8_t>(zigzag));
    return out;
}

bool AvroSerializer::decode_long(const uint8_t * data, size_t size,
                                  int64_t & value, size_t & bytes_read) {
    value = 0;
    bytes_read = 0;
    if (data == nullptr) return false;

    uint64_t zigzag = 0;
    int shift = 0;
    for (size_t i = 0; i < size && i < 10; i++) {
        uint8_t byte = data[i];
        if (shift < 64) {
            // For the 10th byte (shift 63) only a single payload bit is valid;
            // extra bits are ignored rather than overflowing
            zigzag |= static_cast<uint64_t>(byte & 0x7F) << shift;
        }
        bytes_read = i + 1;
        if ((byte & 0x80) == 0) {
            value = static_cast<int64_t>((zigzag >> 1) ^
                  (~(zigzag & 1) + 1));
            return true;
        }
        shift += 7;
    }
    return false; // Truncated or overlong varint
}

std::vector<uint8_t> AvroSerializer::encode_string(const std::string & value) {
    std::vector<uint8_t> out = encode_long(static_cast<int64_t>(value.size()));
    append_bytes(out, reinterpret_cast<const uint8_t *>(value.data()), value.size());
    return out;
}

bool AvroSerializer::decode_string(const uint8_t * data, size_t size,
                                    std::string & value, size_t & bytes_read) {
    value.clear();
    bytes_read = 0;
    int64_t length = 0;
    size_t used = 0;
    if (!decode_long(data, size, length, used)) return false;
    if (length < 0) return false;
    if (static_cast<uint64_t>(length) > size - used) return false;
    value.assign(reinterpret_cast<const char *>(data + used),
                 static_cast<size_t>(length));
    bytes_read = used + static_cast<size_t>(length);
    return true;
}

std::vector<uint8_t> AvroSerializer::encode_double(double value) {
    std::vector<uint8_t> out(8);
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 0; i < 8; i++) {
        out[i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);
    }
    return out;
}

bool AvroSerializer::decode_double(const uint8_t * data, size_t size,
                                    double & value, size_t & bytes_read) {
    if (data == nullptr || size < 8) return false;
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
        bits |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    std::memcpy(&value, &bits, sizeof(value));
    bytes_read = 8;
    return true;
}

std::vector<uint8_t> AvroSerializer::encode_bytes(const uint8_t * data, size_t size) {
    std::vector<uint8_t> out = encode_long(static_cast<int64_t>(size));
    append_bytes(out, data, size);
    return out;
}

bool AvroSerializer::decode_bytes(const uint8_t * data, size_t size,
                                   std::vector<uint8_t> & value, size_t & bytes_read) {
    value.clear();
    bytes_read = 0;
    int64_t length = 0;
    size_t used = 0;
    if (!decode_long(data, size, length, used)) return false;
    if (length < 0) return false;
    if (static_cast<uint64_t>(length) > size - used) return false;
    value.assign(data + used, data + used + static_cast<size_t>(length));
    bytes_read = used + static_cast<size_t>(length);
    return true;
}

void AvroSerializer::append_bytes(std::vector<uint8_t> & out,
                                   const uint8_t * data, size_t size) {
    if (data == nullptr || size == 0) return;
    out.insert(out.end(), data, data + size);
}

// ============================================================================
// Map Encoding (block form: long count, key/value pairs, zero terminator)
// ============================================================================

void AvroSerializer::append_map(std::vector<uint8_t> & out,
                                 const std::map<std::string, std::string> & map) {
    if (map.empty()) {
        auto terminator = encode_long(0);
        out.insert(out.end(), terminator.begin(), terminator.end());
        return;
    }
    auto count = encode_long(static_cast<int64_t>(map.size()));
    out.insert(out.end(), count.begin(), count.end());
    for (const auto & kv : map) {
        auto key = encode_string(kv.first);
        out.insert(out.end(), key.begin(), key.end());
        auto val = encode_string(kv.second);
        out.insert(out.end(), val.begin(), val.end());
    }
    auto terminator = encode_long(0);
    out.insert(out.end(), terminator.begin(), terminator.end());
}

bool AvroSerializer::decode_map(const uint8_t * data, size_t size,
                                 std::map<std::string, std::string> & map,
                                 size_t & bytes_read) {
    map.clear();
    bytes_read = 0;
    size_t offset = 0;

    while (true) {
        int64_t block_count = 0;
        size_t used = 0;
        if (!decode_long(data + offset, size - offset, block_count, used)) return false;
        offset += used;
        if (block_count == 0) break;
        if (block_count < 0) {
            // Negative block count: followed by a block byte-size long
            block_count = -block_count;
            int64_t block_size = 0;
            if (!decode_long(data + offset, size - offset, block_size, used)) return false;
            offset += used;
        }
        for (int64_t i = 0; i < block_count; i++) {
            std::string key, val;
            if (!decode_string(data + offset, size - offset, key, used)) return false;
            offset += used;
            if (!decode_string(data + offset, size - offset, val, used)) return false;
            offset += used;
            map[key] = val;
        }
    }

    bytes_read = offset;
    return true;
}

// ============================================================================
// Record Encoding - ImportedAccount
// ============================================================================

std::vector<uint8_t> AvroSerializer::encode_account(const ImportedAccount & account) {
    std::vector<uint8_t> out;
    auto append_str = [&](const std::string & s) {
        auto encoded = encode_string(s);
        out.insert(out.end(), encoded.begin(), encoded.end());
    };
    append_str(account.external_id);
    append_str(account.external_code);
    append_str(account.name);
    append_str(account.type);
    append_str(account.parent_id);
    append_str(account.currency);
    append_str(account.description);
    append_map(out, account.metadata);
    return out;
}

bool AvroSerializer::decode_account(const uint8_t * data, size_t size,
                                     ImportedAccount & account, size_t & bytes_read) {
    bytes_read = 0;
    size_t offset = 0, used = 0;
    auto read_str = [&](std::string & s) -> bool {
        if (!decode_string(data + offset, size - offset, s, used)) return false;
        offset += used;
        return true;
    };
    if (!read_str(account.external_id))   return false;
    if (!read_str(account.external_code)) return false;
    if (!read_str(account.name))          return false;
    if (!read_str(account.type))          return false;
    if (!read_str(account.parent_id))     return false;
    if (!read_str(account.currency))      return false;
    if (!read_str(account.description))   return false;
    if (!decode_map(data + offset, size - offset, account.metadata, used)) return false;
    offset += used;
    bytes_read = offset;
    return true;
}

// ============================================================================
// Record Encoding - ImportedTransaction
// ============================================================================

std::vector<uint8_t> AvroSerializer::encode_transaction(const ImportedTransaction & tx) {
    std::vector<uint8_t> out;
    auto append_str = [&](const std::string & s) {
        auto encoded = encode_string(s);
        out.insert(out.end(), encoded.begin(), encoded.end());
    };
    auto append_dbl = [&](double d) {
        auto encoded = encode_double(d);
        out.insert(out.end(), encoded.begin(), encoded.end());
    };

    append_str(tx.external_id);
    append_str(tx.date);
    append_str(tx.description);
    append_str(tx.reference);
    append_str(tx.source_system);
    append_str(tx.currency);

    // Lines array: block count, items, zero terminator
    if (!tx.lines.empty()) {
        auto count = encode_long(static_cast<int64_t>(tx.lines.size()));
        out.insert(out.end(), count.begin(), count.end());
        for (const auto & line : tx.lines) {
            append_str(line.account_code);
            append_dbl(line.debit_amount);
            append_dbl(line.credit_amount);
            append_str(line.memo);
            append_str(line.reconciled);
        }
    }
    auto terminator = encode_long(0);
    out.insert(out.end(), terminator.begin(), terminator.end());

    append_map(out, tx.metadata);
    return out;
}

bool AvroSerializer::decode_transaction(const uint8_t * data, size_t size,
                                         ImportedTransaction & tx, size_t & bytes_read) {
    bytes_read = 0;
    size_t offset = 0, used = 0;
    auto read_str = [&](std::string & s) -> bool {
        if (!decode_string(data + offset, size - offset, s, used)) return false;
        offset += used;
        return true;
    };

    if (!read_str(tx.external_id))   return false;
    if (!read_str(tx.date))          return false;
    if (!read_str(tx.description))   return false;
    if (!read_str(tx.reference))     return false;
    if (!read_str(tx.source_system)) return false;
    if (!read_str(tx.currency))      return false;

    tx.lines.clear();
    while (true) {
        int64_t block_count = 0;
        if (!decode_long(data + offset, size - offset, block_count, used)) return false;
        offset += used;
        if (block_count == 0) break;
        if (block_count < 0) {
            block_count = -block_count;
            int64_t block_size = 0;
            if (!decode_long(data + offset, size - offset, block_size, used)) return false;
            offset += used;
        }
        for (int64_t i = 0; i < block_count; i++) {
            ImportedTransaction::Line line;
            if (!read_str(line.account_code)) return false;
            if (!decode_double(data + offset, size - offset, line.debit_amount, used)) {
                return false;
            }
            offset += used;
            if (!decode_double(data + offset, size - offset, line.credit_amount, used)) {
                return false;
            }
            offset += used;
            if (!read_str(line.memo))       return false;
            if (!read_str(line.reconciled)) return false;
            tx.lines.push_back(line);
        }
    }

    if (!decode_map(data + offset, size - offset, tx.metadata, used)) return false;
    offset += used;
    bytes_read = offset;
    return true;
}

std::vector<uint8_t> AvroSerializer::encode_accounts(
    const std::vector<ImportedAccount> & accounts) {
    // Sequence encoded as an Avro array of ImportedAccount records
    std::vector<uint8_t> out;
    if (!accounts.empty()) {
        auto count = encode_long(static_cast<int64_t>(accounts.size()));
        out.insert(out.end(), count.begin(), count.end());
        for (const auto & account : accounts) {
            auto encoded = encode_account(account);
            out.insert(out.end(), encoded.begin(), encoded.end());
        }
    }
    auto terminator = encode_long(0);
    out.insert(out.end(), terminator.begin(), terminator.end());
    return out;
}

std::vector<uint8_t> AvroSerializer::encode_transactions(
    const std::vector<ImportedTransaction> & transactions) {
    std::vector<uint8_t> out;
    if (!transactions.empty()) {
        auto count = encode_long(static_cast<int64_t>(transactions.size()));
        out.insert(out.end(), count.begin(), count.end());
        for (const auto & tx : transactions) {
            auto encoded = encode_transaction(tx);
            out.insert(out.end(), encoded.begin(), encoded.end());
        }
    }
    auto terminator = encode_long(0);
    out.insert(out.end(), terminator.begin(), terminator.end());
    return out;
}

// ============================================================================
// JSON Encoding
// ============================================================================

std::string AvroSerializer::json_escape(const std::string & value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::stringstream ss;
                    ss << "\\u" << std::hex << std::setfill('0') << std::setw(4)
                       << static_cast<int>(static_cast<unsigned char>(c));
                    out += ss.str();
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

std::string AvroSerializer::account_to_json(const ImportedAccount & account) {
    std::stringstream ss;
    auto field = [&ss](const char * name, const std::string & value, bool last = false) {
        ss << "  \"" << name << "\": \"" << json_escape(value) << "\""
           << (last ? "" : ",\n");
    };

    ss << "{\n";
    field("external_id", account.external_id);
    field("external_code", account.external_code);
    field("name", account.name);
    field("type", account.type);
    field("parent_id", account.parent_id);
    field("currency", account.currency);
    field("description", account.description);

    ss << "  \"metadata\": {";
    size_t i = 0;
    for (const auto & kv : account.metadata) {
        ss << (i == 0 ? "" : ",") << "\n    \"" << json_escape(kv.first)
           << "\": \"" << json_escape(kv.second) << "\"";
        i++;
    }
    ss << (account.metadata.empty() ? "}" : "\n  }") << "\n";
    ss << "}";
    return ss.str();
}

std::string AvroSerializer::transaction_to_json(const ImportedTransaction & tx) {
    std::stringstream ss;
    auto field = [&ss](const char * name, const std::string & value) {
        ss << "  \"" << name << "\": \"" << json_escape(value) << "\",\n";
    };

    ss << "{\n";
    field("external_id", tx.external_id);
    field("date", tx.date);
    field("description", tx.description);
    field("reference", tx.reference);
    field("source_system", tx.source_system);
    field("currency", tx.currency);

    ss << "  \"lines\": [";
    for (size_t i = 0; i < tx.lines.size(); i++) {
        const auto & line = tx.lines[i];
        ss << (i == 0 ? "\n" : ",\n");
        ss << "    {\"account_code\": \"" << json_escape(line.account_code)
           << "\", \"debit_amount\": " << std::setprecision(17) << line.debit_amount
           << ", \"credit_amount\": " << std::setprecision(17) << line.credit_amount
           << ", \"memo\": \"" << json_escape(line.memo)
           << "\", \"reconciled\": \"" << json_escape(line.reconciled) << "\"}";
    }
    ss << (tx.lines.empty() ? "]," : "\n  ],") << "\n";

    ss << "  \"metadata\": {";
    size_t i = 0;
    for (const auto & kv : tx.metadata) {
        ss << (i == 0 ? "" : ",") << "\n    \"" << json_escape(kv.first)
           << "\": \"" << json_escape(kv.second) << "\"";
        i++;
    }
    ss << (tx.metadata.empty() ? "}" : "\n  }") << "\n";
    ss << "}";
    return ss.str();
}

bool AvroSerializer::account_from_json(const std::string & json,
                                        ImportedAccount & account) {
    connector::JsonValue root;
    std::string error;
    connector::JsonParser parser;
    if (!parser.parse(json, root, error) || !root.is_object()) {
        return false;
    }
    account.external_id   = root.get("external_id").as_string();
    account.external_code = root.get("external_code").as_string();
    account.name          = root.get("name").as_string();
    account.type          = root.get("type").as_string();
    account.parent_id     = root.get("parent_id").as_string();
    account.currency      = root.get("currency").as_string();
    account.description   = root.get("description").as_string();
    account.metadata.clear();
    const connector::JsonValue & metadata = root.get("metadata");
    if (metadata.is_object()) {
        for (const auto & kv : metadata.object_value) {
            account.metadata[kv.first] = kv.second.as_string();
        }
    }
    return true;
}

bool AvroSerializer::transaction_from_json(const std::string & json,
                                            ImportedTransaction & tx) {
    connector::JsonValue root;
    std::string error;
    connector::JsonParser parser;
    if (!parser.parse(json, root, error) || !root.is_object()) {
        return false;
    }
    tx.external_id   = root.get("external_id").as_string();
    tx.date          = root.get("date").as_string();
    tx.description   = root.get("description").as_string();
    tx.reference     = root.get("reference").as_string();
    tx.source_system = root.get("source_system").as_string();
    tx.currency      = root.get("currency").as_string();

    tx.lines.clear();
    const connector::JsonValue & lines = root.get("lines");
    if (lines.is_array()) {
        for (size_t i = 0; i < lines.size(); i++) {
            const connector::JsonValue & obj = lines.at(i);
            ImportedTransaction::Line line;
            line.account_code  = obj.get("account_code").as_string();
            line.debit_amount  = obj.get("debit_amount").as_number();
            line.credit_amount = obj.get("credit_amount").as_number();
            line.memo          = obj.get("memo").as_string();
            line.reconciled    = obj.get("reconciled").as_string();
            tx.lines.push_back(line);
        }
    }

    tx.metadata.clear();
    const connector::JsonValue & metadata = root.get("metadata");
    if (metadata.is_object()) {
        for (const auto & kv : metadata.object_value) {
            tx.metadata[kv.first] = kv.second.as_string();
        }
    }
    return true;
}

} // namespace avro
} // namespace ggnucash
