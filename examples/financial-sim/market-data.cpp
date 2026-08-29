#include "market-data.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>

// ============================================================================
// Market Data Integration - implementation (Phase 1, Issue #003)
// ============================================================================

namespace ggnucash {
namespace marketdata {

namespace {
const char FIX_SOH = '\x01';

// Format an int64 as a zero-padded decimal string of at least `width` digits.
std::string pad_int(int64_t value, int width) {
    std::string s = std::to_string(value);
    if (static_cast<int>(s.size()) < width) {
        s.insert(0, static_cast<size_t>(width) - s.size(), '0');
    }
    return s;
}

// UTC timestamp in FIX format YYYYMMDD-HH:MM:SS.mmm from ns since epoch.
std::string fix_utc_timestamp(int64_t epoch_ns) {
    int64_t epoch_ms  = epoch_ns / 1000000;
    time_t  secs      = static_cast<time_t>(epoch_ms / 1000);
    int     millis    = static_cast<int>(epoch_ms % 1000);
    if (millis < 0) { millis += 1000; secs -= 1; }

    struct tm tm_utc;
#if defined(_WIN32)
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d:%02d:%02d.%03d",
                  tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                  tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec, millis);
    return std::string(buf);
}

// ============================================================================
// Varint helpers (unsigned LEB128 + zigzag) for the binary store format
// ============================================================================

void write_uvarint(std::string & out, uint64_t value) {
    while (value >= 0x80) {
        out.push_back(static_cast<char>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<char>(value));
}

void write_svarint(std::string & out, int64_t value) {
    const uint64_t zz = (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
    write_uvarint(out, zz);
}

struct VarintReader {
    const uint8_t * data;
    size_t          len;
    size_t          pos;
    bool            ok;

    VarintReader(const uint8_t * d, size_t l) : data(d), len(l), pos(0), ok(true) {}

    uint64_t read_uvarint() {
        uint64_t result = 0;
        int      shift  = 0;
        while (ok) {
            if (pos >= len || shift > 63) { ok = false; return 0; }
            const uint8_t byte = data[pos++];
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) { break; }
            shift += 7;
        }
        return result;
    }

    int64_t read_svarint() {
        const uint64_t zz = read_uvarint();
        return static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1);
    }
};

// Quantize a price to 1e-8 fixed point.
int64_t price_to_fixed(double price) {
    return static_cast<int64_t>(std::llround(price * 1e8));
}

double fixed_to_price(int64_t fixed) {
    return static_cast<double>(fixed) / 1e8;
}

} // anonymous namespace

// ============================================================================
// Core Types
// ============================================================================

std::string exchange_to_string(Exchange exchange) {
    switch (exchange) {
        case Exchange::NYSE:       return "NYSE";
        case Exchange::NASDAQ:     return "NASDAQ";
        case Exchange::LSE:        return "LSE";
        case Exchange::JSE:        return "JSE";
        case Exchange::XETRA:      return "XETRA";
        case Exchange::CME:        return "CME";
        case Exchange::CRYPTO_SIM: return "CRYPTO_SIM";
        case Exchange::SIMULATED:  return "SIMULATED";
        case Exchange::UNKNOWN:    return "UNKNOWN";
        default:                   return "UNKNOWN";
    }
}

Exchange exchange_from_string(const std::string & name) {
    if (name == "NYSE")       { return Exchange::NYSE; }
    if (name == "NASDAQ")     { return Exchange::NASDAQ; }
    if (name == "LSE")        { return Exchange::LSE; }
    if (name == "JSE")        { return Exchange::JSE; }
    if (name == "XETRA")      { return Exchange::XETRA; }
    if (name == "CME")        { return Exchange::CME; }
    if (name == "CRYPTO_SIM") { return Exchange::CRYPTO_SIM; }
    if (name == "SIMULATED")  { return Exchange::SIMULATED; }
    return Exchange::UNKNOWN;
}

MarketDataMessage MarketDataMessage::from_tick(const Tick & t) {
    MarketDataMessage m;
    m.kind                 = MarketDataKind::TICK;
    m.tick                 = t;
    m.receive_timestamp_ns = t.receive_timestamp_ns;
    return m;
}

MarketDataMessage MarketDataMessage::from_bar(const OHLCV & b) {
    MarketDataMessage m;
    m.kind                 = MarketDataKind::OHLCV_BAR;
    m.bar                  = b;
    m.receive_timestamp_ns = b.receive_timestamp_ns;
    return m;
}

MarketDataMessage MarketDataMessage::from_book(const OrderBookSnapshot & s) {
    MarketDataMessage m;
    m.kind                 = MarketDataKind::ORDER_BOOK;
    m.book                 = s;
    m.receive_timestamp_ns = s.receive_timestamp_ns;
    return m;
}

MarketDataMessage MarketDataMessage::heartbeat(int64_t ts_ns) {
    MarketDataMessage m;
    m.kind                 = MarketDataKind::HEARTBEAT;
    m.status_text          = "HEARTBEAT";
    m.receive_timestamp_ns = ts_ns;
    return m;
}

// ============================================================================
// Hardware Timestamping
// ============================================================================

int64_t TimestampSource::now_monotonic_ns() {
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

int64_t TimestampSource::now_wall_ns() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
}

HardwareTimestamper::HardwareTimestamper() {}

int64_t HardwareTimestamper::stamp_arrival() const {
    return TimestampSource::now_wall_ns();
}

void HardwareTimestamper::stamp(Tick & tick) const {
    tick.receive_timestamp_ns = stamp_arrival();
}

void HardwareTimestamper::stamp(OHLCV & bar) const {
    bar.receive_timestamp_ns = stamp_arrival();
}

void HardwareTimestamper::stamp(OrderBookSnapshot & book) const {
    book.receive_timestamp_ns = stamp_arrival();
}

void HardwareTimestamper::stamp(MarketDataMessage & msg) const {
    msg.receive_timestamp_ns = stamp_arrival();
    switch (msg.kind) {
        case MarketDataKind::TICK:       msg.tick.receive_timestamp_ns = msg.receive_timestamp_ns; break;
        case MarketDataKind::OHLCV_BAR:  msg.bar.receive_timestamp_ns  = msg.receive_timestamp_ns; break;
        case MarketDataKind::ORDER_BOOK: msg.book.receive_timestamp_ns = msg.receive_timestamp_ns; break;
        default: break;
    }
}

int64_t HardwareTimestamper::latency_ns(int64_t feed_timestamp_ns, int64_t receive_timestamp_ns) {
    if (feed_timestamp_ns <= 0 || receive_timestamp_ns <= 0) {
        return 0;
    }
    if (receive_timestamp_ns < feed_timestamp_ns) {
        return 0;
    }
    return receive_timestamp_ns - feed_timestamp_ns;
}

int64_t HardwareTimestamper::latency_ns(const Tick & tick) {
    return latency_ns(tick.feed_timestamp_ns, tick.receive_timestamp_ns);
}

// ============================================================================
// FIX Protocol Parser & Encoder
// ============================================================================

FixMessage::FixMessage() : begin_string("FIX.4.2") {}

bool FixMessage::has(int tag) const {
    for (const auto & f : fields) {
        if (f.tag == tag) { return true; }
    }
    return false;
}

std::string FixMessage::get(int tag, const std::string & fallback) const {
    for (const auto & f : fields) {
        if (f.tag == tag) { return f.value; }
    }
    return fallback;
}

int64_t FixMessage::get_int(int tag, int64_t fallback) const {
    const std::string v = get(tag, "");
    if (v.empty()) { return fallback; }
    try {
        return std::stoll(v);
    } catch (...) {
        return fallback;
    }
}

double FixMessage::get_double(int tag, double fallback) const {
    const std::string v = get(tag, "");
    if (v.empty()) { return fallback; }
    try {
        return std::stod(v);
    } catch (...) {
        return fallback;
    }
}

char FixMessage::get_char(int tag, char fallback) const {
    const std::string v = get(tag, "");
    return v.empty() ? fallback : v[0];
}

std::vector<std::string> FixMessage::get_all(int tag) const {
    std::vector<std::string> out;
    for (const auto & f : fields) {
        if (f.tag == tag) { out.push_back(f.value); }
    }
    return out;
}

std::string FixMessage::msg_type() const {
    return get(35, "");
}

void FixMessage::set(int tag, const std::string & value) {
    for (auto & f : fields) {
        if (f.tag == tag) { f.value = value; return; }
    }
    fields.push_back({tag, value});
}

void FixMessage::add(int tag, const std::string & value) {
    fields.push_back({tag, value});
}

void FixMessage::set_int(int tag, int64_t value) {
    set(tag, std::to_string(value));
}

void FixMessage::set_double(int tag, double value, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, value);
    set(tag, buf);
}

void FixMessage::set_char(int tag, char value) {
    set(tag, std::string(1, value));
}

void FixMessage::clear() {
    fields.clear();
}

std::string FixMessage::encode(const FixMessage & msg) {
    std::string body;
    for (const auto & f : msg.fields) {
        body += std::to_string(f.tag);
        body += '=';
        body += f.value;
        body += FIX_SOH;
    }

    std::string head = "8=" + msg.begin_string + FIX_SOH + "9=" + std::to_string(body.size()) + FIX_SOH;
    std::string full = head + body;
    full += "10=" + compute_checksum(full.data(), full.size()) + FIX_SOH;
    return full;
}

std::string FixMessage::compute_checksum(const char * data, size_t len) {
    uint64_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += static_cast<uint8_t>(data[i]);
    }
    return pad_int(static_cast<int64_t>(sum % 256), 3);
}

bool FixMessage::parse(const char * data, size_t len, FixMessage & out, size_t & consumed) {
    consumed = 0;
    if (data == nullptr || len == 0) { return false; }

    // Locate "10=ddd<SOH>" trailer to know the full message extent.
    size_t checksum_pos = std::string::npos;
    if (len >= 7) {
        for (size_t i = 0; i + 6 < len; i++) {
            if (data[i] == FIX_SOH &&
                data[i + 1] == '1' && data[i + 2] == '0' && data[i + 3] == '=' &&
                data[i + 4] >= '0' && data[i + 4] <= '9' &&
                data[i + 5] >= '0' && data[i + 5] <= '9' &&
                data[i + 6] >= '0' && data[i + 6] <= '9' &&
                i + 7 < len && data[i + 7] == FIX_SOH) {
                checksum_pos = i;
                break;
            }
        }
    }
    if (checksum_pos == std::string::npos) {
        // No complete trailer yet: partial message (or garbage without SOH).
        const char * any_soh = static_cast<const char *>(std::memchr(data, FIX_SOH, len));
        if (any_soh == nullptr) {
            consumed = len;   // no delimiter at all: unrecoverable garbage
        }
        return false;
    }
    const size_t total_len = checksum_pos + 8;   // up to and including final SOH

    if (!validate_checksum(data, total_len)) {
        consumed = total_len;
        return false;
    }

    // Begin string: must start with "8=".
    if (total_len < 3 || data[0] != '8' || data[1] != '=') {
        consumed = total_len;
        return false;
    }
    const char * first_soh_p = static_cast<const char *>(std::memchr(data, FIX_SOH, total_len));
    if (first_soh_p == nullptr) {
        consumed = total_len;
        return false;
    }
    const size_t first_soh = static_cast<size_t>(first_soh_p - data);
    FixMessage msg;
    msg.begin_string.assign(data + 2, first_soh - 2);

    // BodyLength: tag 9 must immediately follow.
    size_t cursor = first_soh + 1;
    if (cursor + 1 >= total_len || data[cursor] != '9' || data[cursor + 1] != '=') {
        consumed = total_len;
        return false;
    }
    size_t eq9      = cursor + 1;
    const char * soh9_p = static_cast<const char *>(std::memchr(data + cursor, FIX_SOH, total_len - cursor));
    if (soh9_p == nullptr) {
        consumed = total_len;
        return false;
    }
    size_t soh9 = static_cast<size_t>(soh9_p - data);
    int64_t body_length = -1;
    try {
        body_length = std::stoll(std::string(data + eq9 + 1, soh9 - eq9 - 1));
    } catch (...) {
        consumed = total_len;
        return false;
    }
    const size_t body_start = soh9 + 1;
    if (body_length < 0 || body_start + static_cast<size_t>(body_length) != checksum_pos + 1) {
        consumed = total_len;
        return false;
    }

    // Body fields.
    cursor = body_start;
    while (cursor < checksum_pos + 1) {
        const char * soh_p = static_cast<const char *>(
            std::memchr(data + cursor, FIX_SOH, checksum_pos + 1 - cursor));
        if (soh_p == nullptr) { break; }
        const size_t end = static_cast<size_t>(soh_p - data);
        const char * eq_p = static_cast<const char *>(std::memchr(data + cursor, '=', end - cursor));
        if (eq_p == nullptr) {
            consumed = total_len;
            return false;
        }
        const size_t eq = static_cast<size_t>(eq_p - data);
        int tag = 0;
        for (size_t i = cursor; i < eq; i++) {
            if (data[i] < '0' || data[i] > '9') {
                consumed = total_len;
                return false;
            }
            tag = tag * 10 + (data[i] - '0');
        }
        msg.fields.push_back({tag, std::string(data + eq + 1, end - eq - 1)});
        cursor = end + 1;
    }

    out      = std::move(msg);
    consumed = total_len;
    return true;
}

bool FixMessage::validate_checksum(const char * data, size_t len) {
    if (len < 8) { return false; }
    // Trailer must be "10=ddd<SOH>" at the very end.
    if (data[len - 1] != FIX_SOH || data[len - 7] != '1' || data[len - 6] != '0' ||
        data[len - 5] != '=') {
        return false;
    }
    const std::string expected(data + len - 4, 3);
    return compute_checksum(data, len - 7) == expected;
}

FixMessage FixMessage::build_market_data_snapshot(const std::string & symbol,
                                                  double bid,
                                                  double bid_qty,
                                                  double ask,
                                                  double ask_qty,
                                                  double last,
                                                  double last_qty) {
    FixMessage msg;
    msg.set(35, "W");
    msg.set(55, symbol);

    int entry_count = 0;
    std::vector<std::pair<char, std::pair<double, double>>> entries;
    if (bid_qty > 0.0 || bid > 0.0)  { entries.push_back({'0', {bid, bid_qty}}); }
    if (ask_qty > 0.0 || ask > 0.0)  { entries.push_back({'1', {ask, ask_qty}}); }
    if (last_qty > 0.0 || last > 0.0) { entries.push_back({'2', {last, last_qty}}); }
    entry_count = static_cast<int>(entries.size());

    char buf[64];
    msg.set_int(268, entry_count);
    for (const auto & e : entries) {
        msg.add(269, std::string(1, e.first));
        std::snprintf(buf, sizeof(buf), "%.6f", e.second.first);
        msg.add(270, buf);
        std::snprintf(buf, sizeof(buf), "%.2f", e.second.second);
        msg.add(271, buf);
    }
    return msg;
}

bool FixMessage::parse_market_data_entries(std::string & symbol_out,
                                           std::vector<MdEntry> & entries_out) const {
    const std::string type = msg_type();
    if (type != "W" && type != "X") {
        return false;
    }
    symbol_out = get(55, "");
    entries_out.clear();

    const int64_t count = get_int(268, -1);
    if (count < 0) {
        return false;
    }

    // Repeating group: entries start after tag 268, in 269/270/271 triples.
    bool in_group = false;
    MdEntry current;
    bool have_type = false;
    for (const auto & f : fields) {
        if (f.tag == 268) { in_group = true; continue; }
        if (!in_group) { continue; }
        if (f.tag == 269) {
            if (have_type) { entries_out.push_back(current); }
            current = MdEntry{};
            try {
                current.entry_type = f.value.empty() ? '\0' : f.value[0];
            } catch (...) {
                current.entry_type = '\0';
            }
            have_type = true;
        } else if (f.tag == 270 && have_type) {
            try { current.price = std::stod(f.value); } catch (...) { current.price = 0.0; }
        } else if (f.tag == 271 && have_type) {
            try { current.quantity = std::stod(f.value); } catch (...) { current.quantity = 0.0; }
        }
    }
    if (have_type) { entries_out.push_back(current); }
    return static_cast<int64_t>(entries_out.size()) == count;
}

// ============================================================================
// FIX Session State
// ============================================================================

FixSession::FixSession(std::string sender_comp_id, std::string target_comp_id,
                       std::string begin_string)
    : sender_(std::move(sender_comp_id)),
      target_(std::move(target_comp_id)),
      begin_(std::move(begin_string)),
      outgoing_seq_(1),
      incoming_seq_(1) {}

FixMessage FixSession::build_admin(const std::string & msg_type) {
    FixMessage msg;
    msg.begin_string = begin_;
    msg.set(35, msg_type);
    prepare_outgoing(msg);
    return msg;
}

void FixSession::prepare_outgoing(FixMessage & msg) {
    msg.set_int(34, outgoing_seq_++);
    msg.set(49, sender_);
    msg.set(56, target_);
    msg.set(52, fix_utc_timestamp(TimestampSource::now_wall_ns()));
}

FixMessage FixSession::build_logon(int heartbeat_interval_sec) {
    FixMessage msg = build_admin("A");
    msg.set_int(98, 0);                       // EncryptMethod = none
    msg.set_int(108, heartbeat_interval_sec); // HeartBtInt
    return msg;
}

FixMessage FixSession::build_heartbeat(const std::string & test_req_id) {
    FixMessage msg = build_admin("0");
    if (!test_req_id.empty()) {
        msg.set(112, test_req_id);
    }
    return msg;
}

FixMessage FixSession::build_test_request(const std::string & test_req_id) {
    FixMessage msg = build_admin("1");
    msg.set(112, test_req_id);
    return msg;
}

FixMessage FixSession::build_logout() {
    return build_admin("5");
}

bool FixSession::validate_incoming_seq(int64_t seq) {
    if (seq == incoming_seq_) {
        incoming_seq_++;
        return true;
    }
    return false;
}

int64_t FixSession::outgoing_seq() const          { return outgoing_seq_; }
int64_t FixSession::expected_incoming_seq() const { return incoming_seq_; }

void FixSession::reset() {
    outgoing_seq_ = 1;
    incoming_seq_ = 1;
}

const std::string & FixSession::sender_comp_id() const { return sender_; }
const std::string & FixSession::target_comp_id() const { return target_; }

// ============================================================================
// WebSocket Framing (RFC 6455)
// ============================================================================

std::vector<uint8_t> WebSocketFramer::encode(const WsFrame & frame, bool mask) {
    std::vector<uint8_t> out;
    const uint8_t op = static_cast<uint8_t>(frame.opcode) & 0x0F;
    out.push_back(static_cast<uint8_t>((frame.fin ? 0x80 : 0x00) | op));

    const size_t   payload_len = frame.payload.size();
    const uint8_t  mask_bit    = mask ? 0x80 : 0x00;
    if (payload_len <= 125) {
        out.push_back(static_cast<uint8_t>(mask_bit | payload_len));
    } else if (payload_len <= 0xFFFF) {
        out.push_back(static_cast<uint8_t>(mask_bit | 126));
        out.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(payload_len & 0xFF));
    } else {
        out.push_back(static_cast<uint8_t>(mask_bit | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<uint8_t>((static_cast<uint64_t>(payload_len) >> shift) & 0xFF));
        }
    }

    if (mask) {
        // Deterministic masking key (hosts should supply entropy at transport
        // level; framing correctness does not depend on key randomness).
        const uint8_t key[4] = {0x37, 0xFA, 0x21, 0x3D};
        out.insert(out.end(), key, key + 4);
        for (size_t i = 0; i < payload_len; i++) {
            out.push_back(static_cast<uint8_t>(frame.payload[i] ^ key[i % 4]));
        }
    } else {
        out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    }
    return out;
}

bool WebSocketFramer::decode(const uint8_t * data, size_t len, WsFrame & out, size_t & consumed) {
    consumed = 0;
    if (data == nullptr || len < 2) { return false; }

    const uint8_t b0 = data[0];
    const uint8_t b1 = data[1];
    const bool    fin     = (b0 & 0x80) != 0;
    const uint8_t opcode  = b0 & 0x0F;
    const bool    masked  = (b1 & 0x80) != 0;
    uint64_t      pay_len = b1 & 0x7F;
    size_t        cursor  = 2;

    if (pay_len == 126) {
        if (len < cursor + 2) { return false; }
        pay_len = (static_cast<uint64_t>(data[cursor]) << 8) | data[cursor + 1];
        cursor += 2;
    } else if (pay_len == 127) {
        if (len < cursor + 8) { return false; }
        pay_len = 0;
        for (int i = 0; i < 8; i++) {
            pay_len = (pay_len << 8) | data[cursor + i];
        }
        cursor += 8;
    }

    uint8_t mask_key[4] = {0, 0, 0, 0};
    if (masked) {
        if (len < cursor + 4) { return false; }
        std::memcpy(mask_key, data + cursor, 4);
        cursor += 4;
    }

    if (pay_len > len - cursor) { return false; }   // partial payload

    out.fin    = fin;
    out.opcode = static_cast<WsOpcode>(opcode);
    out.masked = masked;
    out.payload.resize(static_cast<size_t>(pay_len));
    for (size_t i = 0; i < static_cast<size_t>(pay_len); i++) {
        out.payload[i] = masked ? static_cast<uint8_t>(data[cursor + i] ^ mask_key[i % 4])
                                : data[cursor + i];
    }
    consumed = cursor + static_cast<size_t>(pay_len);
    return true;
}

void WebSocketFramer::feed(const uint8_t * data, size_t len) {
    buffer_.insert(buffer_.end(), data, data + len);
}

bool WebSocketFramer::next_frame(WsFrame & out) {
    size_t consumed = 0;
    if (!decode(buffer_.data(), buffer_.size(), out, consumed)) {
        // A header exists but claims an absurd length -> protocol error.
        if (buffer_.size() >= 2) {
            const uint64_t len7 = buffer_[1] & 0x7F;
            if (len7 == 127 && buffer_.size() >= 10) {
                uint64_t pay_len = 0;
                for (int i = 0; i < 8; i++) { pay_len = (pay_len << 8) | buffer_[2 + i]; }
                if (pay_len > (uint64_t{1} << 40)) { error_ = true; }
            }
        }
        return false;
    }
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(consumed));
    return true;
}

bool WebSocketFramer::had_error() const { return error_; }

void WebSocketFramer::reset() {
    buffer_.clear();
    error_ = false;
}

// ============================================================================
// HTTP/1.1 Framing
// ============================================================================

HttpRequestBuilder::HttpRequestBuilder()
    : method_("GET"), path_("/"), has_host_(false), has_content_length_(false) {}

HttpRequestBuilder & HttpRequestBuilder::set_method(const std::string & method) {
    method_ = method;
    return *this;
}

HttpRequestBuilder & HttpRequestBuilder::set_path(const std::string & path) {
    path_ = path;
    return *this;
}

HttpRequestBuilder & HttpRequestBuilder::set_host(const std::string & host) {
    has_host_ = true;
    return add_header("Host", host);
}

HttpRequestBuilder & HttpRequestBuilder::add_header(const std::string & name,
                                                    const std::string & value) {
    if (name == "Content-Length") { has_content_length_ = true; }
    headers_.push_back({name, value});
    return *this;
}

HttpRequestBuilder & HttpRequestBuilder::set_body(const std::string & body) {
    body_ = body;
    return *this;
}

std::string HttpRequestBuilder::build() const {
    std::string out = method_ + " " + path_ + " HTTP/1.1\r\n";
    for (const auto & h : headers_) {
        out += h.first + ": " + h.second + "\r\n";
    }
    if (!body_.empty() && !has_content_length_) {
        out += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
    }
    out += "Connection: close\r\n";
    out += "\r\n";
    out += body_;
    return out;
}

std::string HttpResponse::header(const std::string & name) const {
    for (const auto & h : headers) {
        if (h.first.size() == name.size()) {
            bool match = true;
            for (size_t i = 0; i < name.size(); i++) {
                char a = h.first[i];
                char b = name[i];
                if (a >= 'A' && a <= 'Z') { a = static_cast<char>(a - 'A' + 'a'); }
                if (b >= 'A' && b <= 'Z') { b = static_cast<char>(b - 'A' + 'a'); }
                if (a != b) { match = false; break; }
            }
            if (match) { return h.second; }
        }
    }
    return "";
}

HttpResponseParser::HttpResponseParser() : complete_(false) {}

bool HttpResponseParser::feed(const char * data, size_t len) {
    buffer_.append(data, len);
    if (!complete_) { try_parse(); }
    return complete_;
}

bool HttpResponseParser::complete() const { return complete_; }

const HttpResponse & HttpResponseParser::response() const { return response_; }

void HttpResponseParser::reset() {
    buffer_.clear();
    complete_  = false;
    response_  = HttpResponse();
}

bool HttpResponseParser::try_parse() {
    const size_t header_end = buffer_.find("\r\n\r\n");
    if (header_end == std::string::npos) { return false; }

    // Status line: "HTTP/1.1 200 OK"
    const size_t line_end = buffer_.find("\r\n");
    if (line_end == std::string::npos || line_end > header_end) { return false; }
    const std::string status_line = buffer_.substr(0, line_end);
    const size_t sp1 = status_line.find(' ');
    if (sp1 == std::string::npos) { return false; }
    size_t sp2 = status_line.find(' ', sp1 + 1);
    try {
        response_.status_code = std::stoi(status_line.substr(sp1 + 1,
            sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1));
    } catch (...) {
        return false;
    }
    response_.reason = sp2 == std::string::npos ? "" : status_line.substr(sp2 + 1);

    // Headers.
    response_.headers.clear();
    size_t cursor = line_end + 2;
    while (cursor < header_end) {
        const size_t eol = buffer_.find("\r\n", cursor);
        if (eol == std::string::npos || eol > header_end) { break; }
        const std::string line = buffer_.substr(cursor, eol - cursor);
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string value = line.substr(colon + 1);
            const size_t first = value.find_first_not_of(" \t");
            const size_t last  = value.find_last_not_of(" \t");
            value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
            response_.headers.push_back({line.substr(0, colon), value});
        }
        cursor = eol + 2;
    }

    // Body framed by Content-Length (when present).
    const std::string cl = response_.header("Content-Length");
    const size_t body_start = header_end + 4;
    if (cl.empty()) {
        response_.body.clear();
        complete_ = true;
        return true;
    }
    size_t body_len = 0;
    try {
        body_len = static_cast<size_t>(std::stoull(cl));
    } catch (...) {
        return false;
    }
    if (buffer_.size() < body_start + body_len) { return false; }
    response_.body = buffer_.substr(body_start, body_len);
    complete_ = true;
    return true;
}

// ============================================================================
// Normalization Layer
// ============================================================================

void MarketDataNormalizer::register_symbol_mapping(const std::string & venue_symbol,
                                                   const std::string & canonical) {
    symbol_map_[venue_symbol] = canonical;
}

void MarketDataNormalizer::register_exchange_mapping(const std::string & venue, Exchange exchange) {
    exchange_map_[venue] = exchange;
}

void MarketDataNormalizer::register_price_scale(const std::string & canonical_symbol, double scale) {
    price_scale_map_[canonical_symbol] = scale;
}

std::string MarketDataNormalizer::canonical_symbol(const std::string & venue_symbol) const {
    const auto it = symbol_map_.find(venue_symbol);
    return it == symbol_map_.end() ? venue_symbol : it->second;
}

Exchange MarketDataNormalizer::exchange_for(const std::string & venue) const {
    const auto it = exchange_map_.find(venue);
    return it == exchange_map_.end() ? Exchange::UNKNOWN : it->second;
}

double MarketDataNormalizer::price_scale_for(const std::string & canonical_symbol) const {
    const auto it = price_scale_map_.find(canonical_symbol);
    return it == price_scale_map_.end() ? 1.0 : it->second;
}

Tick MarketDataNormalizer::normalize_tick(const std::string & venue,
                                          const std::string & venue_symbol,
                                          double bid,
                                          double ask,
                                          double last,
                                          uint64_t volume,
                                          int64_t feed_timestamp_ns) const {
    Tick tick;
    tick.symbol            = canonical_symbol(venue_symbol);
    tick.exchange          = exchange_for(venue);
    const double scale     = price_scale_for(tick.symbol);
    tick.bid               = bid * scale;
    tick.ask               = ask * scale;
    tick.last              = last * scale;
    tick.volume            = volume;
    tick.feed_timestamp_ns = feed_timestamp_ns;
    return tick;
}

OHLCV MarketDataNormalizer::normalize_bar(const std::string & venue,
                                          const std::string & venue_symbol,
                                          double open,
                                          double high,
                                          double low,
                                          double close,
                                          uint64_t volume,
                                          int64_t bar_start_ns,
                                          int64_t bar_end_ns) const {
    OHLCV bar;
    bar.symbol            = canonical_symbol(venue_symbol);
    bar.exchange          = exchange_for(venue);
    const double scale    = price_scale_for(bar.symbol);
    bar.open              = open * scale;
    bar.high              = high * scale;
    bar.low               = low * scale;
    bar.close             = close * scale;
    bar.volume            = volume;
    bar.bar_start_ns      = bar_start_ns;
    bar.bar_end_ns        = bar_end_ns;
    return bar;
}

// ============================================================================
// Historical Data Store
// ============================================================================

HistoricalDataStore::HistoricalDataStore() {}

bool HistoricalDataStore::append(const Tick & tick) {
    auto & series = series_[tick.symbol];
    if (!series.empty() && tick.feed_timestamp_ns < series.back().feed_timestamp_ns) {
        return false;
    }
    series.push_back(tick);
    return true;
}

bool HistoricalDataStore::point_query(const std::string & symbol, int64_t timestamp_ns,
                                      Tick & out) const {
    const auto it = series_.find(symbol);
    if (it == series_.end() || it->second.empty()) { return false; }
    const auto & series = it->second;

    // First element with feed_timestamp_ns > timestamp_ns, then step back.
    const auto ub = std::upper_bound(series.begin(), series.end(), timestamp_ns,
        [](int64_t ts, const Tick & t) { return ts < t.feed_timestamp_ns; });
    if (ub == series.begin()) { return false; }
    out = *(ub - 1);
    return true;
}

std::vector<Tick> HistoricalDataStore::range_query(const std::string & symbol,
                                                   int64_t start_ns,
                                                   int64_t end_ns) const {
    std::vector<Tick> out;
    const auto it = series_.find(symbol);
    if (it == series_.end()) { return out; }
    const auto & series = it->second;

    const auto lo = std::lower_bound(series.begin(), series.end(), start_ns,
        [](const Tick & t, int64_t ts) { return t.feed_timestamp_ns < ts; });
    for (auto cur = lo; cur != series.end() && cur->feed_timestamp_ns <= end_ns; ++cur) {
        out.push_back(*cur);
    }
    return out;
}

std::vector<std::string> HistoricalDataStore::symbols() const {
    std::vector<std::string> out;
    out.reserve(series_.size());
    for (const auto & kv : series_) { out.push_back(kv.first); }
    std::sort(out.begin(), out.end());
    return out;
}

size_t HistoricalDataStore::tick_count(const std::string & symbol) const {
    const auto it = series_.find(symbol);
    return it == series_.end() ? 0 : it->second.size();
}

size_t HistoricalDataStore::total_ticks() const {
    size_t total = 0;
    for (const auto & kv : series_) { total += kv.second.size(); }
    return total;
}

void HistoricalDataStore::clear() {
    series_.clear();
}

bool HistoricalDataStore::save(const std::string & path) const {
    std::string buf;
    buf.append("GGMD0001", 8);

    // Version + symbol count (little-endian u32).
    for (int i = 0; i < 4; i++) { buf.push_back(static_cast<char>((1u >> (8 * i)) & 0xFF)); }
    const uint32_t symbol_count = static_cast<uint32_t>(series_.size());
    for (int i = 0; i < 4; i++) { buf.push_back(static_cast<char>((symbol_count >> (8 * i)) & 0xFF)); }

    const auto ordered = symbols();
    for (const auto & symbol : ordered) {
        const auto & series = series_.at(symbol);
        const uint16_t sym_len = static_cast<uint16_t>(symbol.size());
        buf.push_back(static_cast<char>(sym_len & 0xFF));
        buf.push_back(static_cast<char>((sym_len >> 8) & 0xFF));
        buf.append(symbol);

        // Record count placeholder: actual count is known, write it directly.
        write_uvarint(buf, static_cast<uint64_t>(series.size()));

        if (series.empty()) { continue; }

        // Anchor record (absolute values).
        const Tick & first = series.front();
        write_uvarint(buf, static_cast<uint64_t>(first.feed_timestamp_ns));
        write_svarint(buf, price_to_fixed(first.bid));
        write_svarint(buf, price_to_fixed(first.ask));
        write_svarint(buf, price_to_fixed(first.last));
        write_uvarint(buf, first.volume);

        int64_t prev_ts    = first.feed_timestamp_ns;
        int64_t prev_bid   = price_to_fixed(first.bid);
        int64_t prev_ask   = price_to_fixed(first.ask);
        int64_t prev_last  = price_to_fixed(first.last);
        int64_t prev_vol   = static_cast<int64_t>(first.volume);
        int64_t prev_delta = 0;

        size_t i = 1;
        while (i < series.size()) {
            const Tick & t = series[i];
            const int64_t ts_d   = t.feed_timestamp_ns - prev_ts;
            const int64_t bid_d  = price_to_fixed(t.bid) - prev_bid;
            const int64_t ask_d  = price_to_fixed(t.ask) - prev_ask;
            const int64_t last_d = price_to_fixed(t.last) - prev_last;
            const int64_t vol_d  = static_cast<int64_t>(t.volume) - prev_vol;

            // Count a run of records identical in price/volume advancing by
            // the same timestamp delta as the previous step.
            if (bid_d == 0 && ask_d == 0 && last_d == 0 && vol_d == 0 &&
                i > 1 && ts_d == prev_delta) {
                uint64_t run = 1;
                while (i + run < series.size()) {
                    const Tick & n = series[i + run];
                    if (price_to_fixed(n.bid) != prev_bid ||
                        price_to_fixed(n.ask) != prev_ask ||
                        price_to_fixed(n.last) != prev_last ||
                        static_cast<int64_t>(n.volume) != prev_vol ||
                        n.feed_timestamp_ns - prev_ts != static_cast<int64_t>((run + 1) * ts_d)) {
                        break;
                    }
                    run++;
                }
                buf.push_back(static_cast<char>(0x01));
                write_uvarint(buf, run);
                prev_ts    += static_cast<int64_t>(run) * ts_d;
                prev_delta  = ts_d;
                i += run;
                continue;
            }

            buf.push_back(static_cast<char>(0x00));
            write_svarint(buf, ts_d);
            write_svarint(buf, bid_d);
            write_svarint(buf, ask_d);
            write_svarint(buf, last_d);
            write_svarint(buf, vol_d);

            prev_ts    = t.feed_timestamp_ns;
            prev_bid   = price_to_fixed(t.bid);
            prev_ask   = price_to_fixed(t.ask);
            prev_last  = price_to_fixed(t.last);
            prev_vol   = static_cast<int64_t>(t.volume);
            prev_delta = ts_d;
            i++;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) { return false; }
    file.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return file.good();
}

bool HistoricalDataStore::load(const std::string & path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) { return false; }
    const std::streamsize size = file.tellg();
    if (size < 0) { return false; }
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0 && !file.read(reinterpret_cast<char *>(bytes.data()), size)) { return false; }

    if (bytes.size() < 16 || std::memcmp(bytes.data(), "GGMD0001", 8) != 0) { return false; }

    size_t cursor = 8;
    uint32_t version = 0;
    for (int i = 0; i < 4; i++) { version |= static_cast<uint32_t>(bytes[cursor++]) << (8 * i); }
    if (version != 1) { return false; }
    uint32_t symbol_count = 0;
    for (int i = 0; i < 4; i++) { symbol_count |= static_cast<uint32_t>(bytes[cursor++]) << (8 * i); }

    std::unordered_map<std::string, std::vector<Tick>> loaded;
    for (uint32_t s = 0; s < symbol_count; s++) {
        if (cursor + 2 > bytes.size()) { return false; }
        const uint16_t sym_len = static_cast<uint16_t>(bytes[cursor]) |
                                 static_cast<uint16_t>(bytes[cursor + 1] << 8);
        cursor += 2;
        if (cursor + sym_len > bytes.size()) { return false; }
        const std::string symbol(reinterpret_cast<const char *>(bytes.data() + cursor), sym_len);
        cursor += sym_len;

        VarintReader reader(bytes.data() + cursor, bytes.size() - cursor);
        const uint64_t record_count = reader.read_uvarint();
        std::vector<Tick> series;
        series.reserve(static_cast<size_t>(record_count));

        if (record_count > 0) {
            Tick first;
            first.symbol             = symbol;
            first.feed_timestamp_ns  = static_cast<int64_t>(reader.read_uvarint());
            first.bid                = fixed_to_price(reader.read_svarint());
            first.ask                = fixed_to_price(reader.read_svarint());
            first.last               = fixed_to_price(reader.read_svarint());
            first.volume             = reader.read_uvarint();
            if (!reader.ok) { return false; }
            series.push_back(first);

            int64_t prev_ts    = first.feed_timestamp_ns;
            int64_t prev_bid   = price_to_fixed(first.bid);
            int64_t prev_ask   = price_to_fixed(first.ask);
            int64_t prev_last  = price_to_fixed(first.last);
            int64_t prev_vol   = static_cast<int64_t>(first.volume);

            uint64_t produced = 1;
            while (produced < record_count) {
                if (reader.pos >= reader.len) { return false; }
                const uint8_t control = reader.data[reader.pos++];
                if (control == 0x00) {
                    const int64_t ts_d   = reader.read_svarint();
                    const int64_t bid_d  = reader.read_svarint();
                    const int64_t ask_d  = reader.read_svarint();
                    const int64_t last_d = reader.read_svarint();
                    const int64_t vol_d  = reader.read_svarint();
                    if (!reader.ok) { return false; }
                    prev_ts   += ts_d;
                    prev_bid  += bid_d;
                    prev_ask  += ask_d;
                    prev_last += last_d;
                    prev_vol  += vol_d;
                    Tick t;
                    t.symbol             = symbol;
                    t.feed_timestamp_ns  = prev_ts;
                    t.bid                = fixed_to_price(prev_bid);
                    t.ask                = fixed_to_price(prev_ask);
                    t.last               = fixed_to_price(prev_last);
                    t.volume             = static_cast<uint64_t>(prev_vol);
                    series.push_back(t);
                    produced++;
                } else if (control == 0x01) {
                    const uint64_t run = reader.read_uvarint();
                    if (!reader.ok || run == 0 || produced + run > record_count) { return false; }
                    if (series.size() < 2) { return false; }   // run needs a delta context
                    const int64_t ts_d = series.back().feed_timestamp_ns -
                                         series[series.size() - 2].feed_timestamp_ns;
                    for (uint64_t r = 0; r < run; r++) {
                        prev_ts += ts_d;
                        Tick t;
                        t.symbol             = symbol;
                        t.feed_timestamp_ns  = prev_ts;
                        t.bid                = fixed_to_price(prev_bid);
                        t.ask                = fixed_to_price(prev_ask);
                        t.last               = fixed_to_price(prev_last);
                        t.volume             = static_cast<uint64_t>(prev_vol);
                        series.push_back(t);
                    }
                    produced += run;
                } else {
                    return false;
                }
            }
        }
        if (!reader.ok) { return false; }
        cursor += reader.pos;
        loaded[symbol] = std::move(series);
    }

    series_ = std::move(loaded);
    return true;
}

uint64_t HistoricalDataStore::replay(const std::string & symbol,
                                     const std::function<void(const Tick &)> & callback,
                                     double speed_multiplier) const {
    const auto it = series_.find(symbol);
    if (it == series_.end() || !callback) { return 0; }
    const auto & series = it->second;

    uint64_t delivered = 0;
    int64_t  prev_ts   = 0;
    for (const auto & tick : series) {
        if (speed_multiplier > 0.0 && delivered > 0) {
            const int64_t gap = tick.feed_timestamp_ns - prev_ts;
            if (gap > 0) {
                const auto scaled = std::chrono::nanoseconds(
                    static_cast<int64_t>(static_cast<double>(gap) / speed_multiplier));
                std::this_thread::sleep_for(scaled);
            }
        }
        prev_ts = tick.feed_timestamp_ns;
        callback(tick);
        delivered++;
    }
    return delivered;
}

// ============================================================================
// Latest-Value Cache
// ============================================================================

void MarketDataCache::update(const Tick & tick) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ticks_[tick.symbol] = tick;
}

void MarketDataCache::update(const OHLCV & bar) {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_bars_[bar.symbol] = bar;
}

bool MarketDataCache::get(const std::string & symbol, Tick & out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = latest_ticks_.find(symbol);
    if (it == latest_ticks_.end()) { return false; }
    out = it->second;
    return true;
}

bool MarketDataCache::get_bar(const std::string & symbol, OHLCV & out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = latest_bars_.find(symbol);
    if (it == latest_bars_.end()) { return false; }
    out = it->second;
    return true;
}

std::unordered_map<std::string, Tick> MarketDataCache::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_ticks_;
}

std::vector<std::string> MarketDataCache::symbols() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(latest_ticks_.size());
    for (const auto & kv : latest_ticks_) { out.push_back(kv.first); }
    std::sort(out.begin(), out.end());
    return out;
}

size_t MarketDataCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return latest_ticks_.size();
}

void MarketDataCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_ticks_.clear();
    latest_bars_.clear();
}

// ============================================================================
// Pub/Sub Bus
// ============================================================================

uint64_t MarketDataBus::subscribe(const std::string & symbol, Callback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t id = next_id_++;
    subscriptions_.push_back({id, symbol, std::move(callback)});
    return id;
}

uint64_t MarketDataBus::subscribe_all(Callback callback) {
    return subscribe("*", std::move(callback));
}

void MarketDataBus::unsubscribe(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [subscription_id](const Subscription & s) { return s.id == subscription_id; }),
        subscriptions_.end());
}

void MarketDataBus::publish(const MarketDataMessage & message) {
    std::string symbol;
    switch (message.kind) {
        case MarketDataKind::TICK:       symbol = message.tick.symbol; break;
        case MarketDataKind::OHLCV_BAR:  symbol = message.bar.symbol; break;
        case MarketDataKind::ORDER_BOOK: symbol = message.book.symbol; break;
        default: break;
    }

    // Copy matching callbacks under lock, invoke outside the critical path of
    // metric accounting (still under lock here to keep delivery counts exact
    // and callbacks serialized for subscribers).
    std::lock_guard<std::mutex> lock(mutex_);
    published_++;
    for (const auto & sub : subscriptions_) {
        if (sub.symbol == "*" || sub.symbol == symbol) {
            sub.callback(message);
            delivered_++;
        }
    }
}

size_t MarketDataBus::subscriber_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.size();
}

uint64_t MarketDataBus::messages_published() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

uint64_t MarketDataBus::messages_delivered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return delivered_;
}

void MarketDataBus::reset_metrics() {
    std::lock_guard<std::mutex> lock(mutex_);
    published_ = 0;
    delivered_ = 0;
}

} // namespace marketdata
} // namespace ggnucash
