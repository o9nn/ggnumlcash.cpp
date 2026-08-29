#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ============================================================================
// Market Data Integration - Phase 1, Issue #003
//
// Standalone, dependency-free market data infrastructure for the GGNuCash
// platform. This module deliberately performs NO socket I/O and links NO
// external libraries (no libcurl, no OpenSSL, no Boost, no WebSocket libs).
// All network-facing concerns are split into pure parsing / framing /
// serialization layers so the host application owns the transport while this
// module owns everything after the byte boundary:
//
//   - Core market data types (Tick, OHLCV, OrderBookSnapshot, variant message)
//   - Hardware timestamping (steady + wall clocks, feed-to-receipt latency)
//   - FIX 4.2/4.4 protocol parser & encoder (tag=value, SOH-delimited),
//     checksum / body-length validation, market-data message helpers, and a
//     minimal session state machine (sequence numbers, logon / heartbeat)
//   - WebSocket frame codec (RFC 6455) and HTTP/1.1 request/response framing
//     sufficient to drive REST market-data polls
//   - Normalization layer mapping venue-specific records to canonical form
//   - Historical time-series store with compact binary persistence
//     (delta + run-length encoding), point/range queries, and replay
//   - Thread-safe latest-value cache and an in-process pub/sub bus
//
// Everything is C++17 and fully testable offline.
// ============================================================================

namespace ggnucash {
namespace marketdata {

// ============================================================================
// Core Types
// ============================================================================

enum class Exchange {
    UNKNOWN,
    NYSE,
    NASDAQ,
    LSE,
    JSE,
    XETRA,
    CME,
    CRYPTO_SIM,
    SIMULATED
};

std::string exchange_to_string(Exchange exchange);
Exchange    exchange_from_string(const std::string & name);

// A single best-bid/offer + last-trade market data update.
struct Tick {
    std::string symbol;
    double      bid;
    double      ask;
    double      last;
    uint64_t    volume;
    Exchange    exchange;
    int64_t     feed_timestamp_ns;      // timestamp applied by the feed/exchange (ns since epoch)
    int64_t     receive_timestamp_ns;   // local hardware receipt timestamp (ns since epoch)

    Tick()
        : bid(0.0), ask(0.0), last(0.0), volume(0), exchange(Exchange::UNKNOWN),
          feed_timestamp_ns(0), receive_timestamp_ns(0) {}
};

// An aggregated OHLCV bar.
struct OHLCV {
    std::string symbol;
    double      open;
    double      high;
    double      low;
    double      close;
    uint64_t    volume;
    int64_t     bar_start_ns;           // bar open time (ns since epoch)
    int64_t     bar_end_ns;             // bar close time (ns since epoch)
    Exchange    exchange;
    int64_t     receive_timestamp_ns;   // local hardware receipt timestamp (ns since epoch)

    OHLCV()
        : open(0.0), high(0.0), low(0.0), close(0.0), volume(0), bar_start_ns(0), bar_end_ns(0),
          exchange(Exchange::UNKNOWN), receive_timestamp_ns(0) {}
};

struct OrderBookLevel {
    double   price;
    double   quantity;
    uint32_t order_count;

    OrderBookLevel() : price(0.0), quantity(0.0), order_count(0) {}
    OrderBookLevel(double p, double q, uint32_t n) : price(p), quantity(q), order_count(n) {}
};

// A full depth snapshot (bids sorted best-first, asks sorted best-first).
struct OrderBookSnapshot {
    std::string                 symbol;
    std::vector<OrderBookLevel> bids;
    std::vector<OrderBookLevel> asks;
    Exchange                    exchange;
    int64_t                     feed_timestamp_ns;      // exchange timestamp (ns since epoch)
    int64_t                     receive_timestamp_ns;   // local receipt timestamp (ns since epoch)

    OrderBookSnapshot() : exchange(Exchange::UNKNOWN), feed_timestamp_ns(0), receive_timestamp_ns(0) {}
};

enum class MarketDataKind {
    TICK,
    OHLCV_BAR,
    ORDER_BOOK,
    HEARTBEAT,
    STATUS
};

// Variant-ish envelope: exactly one payload is meaningful, selected by `kind`.
struct MarketDataMessage {
    MarketDataKind    kind;
    Tick              tick;             // valid when kind == TICK
    OHLCV             bar;              // valid when kind == OHLCV_BAR
    OrderBookSnapshot book;             // valid when kind == ORDER_BOOK
    std::string       status_text;      // valid when kind == HEARTBEAT / STATUS
    int64_t           receive_timestamp_ns;

    MarketDataMessage() : kind(MarketDataKind::STATUS), receive_timestamp_ns(0) {}

    static MarketDataMessage from_tick(const Tick & t);
    static MarketDataMessage from_bar(const OHLCV & b);
    static MarketDataMessage from_book(const OrderBookSnapshot & s);
    static MarketDataMessage heartbeat(int64_t ts_ns);
};

// ============================================================================
// Hardware Timestamping
// ============================================================================

// Nanosecond timestamp sources. `now_monotonic_ns` uses a steady clock and is
// suitable for latency measurement; `now_wall_ns` uses the system clock and is
// suitable for epoch-aligned feed timestamps.
class TimestampSource {
public:
    static int64_t now_monotonic_ns();
    static int64_t now_wall_ns();
};

// Captures arrival timestamps and computes feed-to-receipt latency. In a real
// deployment the receive stamp would be taken by the NIC/driver on packet
// arrival; here the host calls `stamp_arrival()` as early as possible.
class HardwareTimestamper {
public:
    HardwareTimestamper();

    // Capture a wall-clock arrival timestamp in nanoseconds.
    int64_t stamp_arrival() const;

    // Stamp `receive_timestamp_ns` on the message payload in place.
    void stamp(Tick & tick) const;
    void stamp(OHLCV & bar) const;
    void stamp(OrderBookSnapshot & book) const;
    void stamp(MarketDataMessage & msg) const;

    // Latency between the exchange/feed timestamp and local receipt.
    // Returns 0 when either timestamp is unset or ordering is inverted.
    static int64_t latency_ns(int64_t feed_timestamp_ns, int64_t receive_timestamp_ns);

    static int64_t latency_ns(const Tick & tick);
};

// ============================================================================
// FIX Protocol (4.2 / 4.4) Parser & Encoder
// ============================================================================

// SOH (\x01) field delimiter used on the wire; `=` separates tag and value.
//
// Wire layout:
//   8=<BeginString> SOH 9=<BodyLength> SOH <body fields> 10=<Checksum> SOH
// BodyLength counts bytes from the first byte after the SOH terminating tag 9
// up to and including the SOH terminating the final body field (before 10).
// Checksum is the sum of every byte of the message up to and including the
// SOH immediately preceding "10=", modulo 256, zero-padded to 3 digits.

struct FixField {
    int         tag;
    std::string value;
};

class FixMessage {
public:
    FixMessage();

    // Begin string, e.g. "FIX.4.2" / "FIX.4.4" (tag 8).
    std::string begin_string;

    // Ordered field storage (body fields only; 8/9/10 handled by the codec).
    std::vector<FixField> fields;

    // Field access -----------------------------------------------------------
    bool                     has(int tag) const;
    std::string              get(int tag, const std::string & fallback = "") const;
    int64_t                  get_int(int tag, int64_t fallback = 0) const;
    double                   get_double(int tag, double fallback = 0.0) const;
    char                     get_char(int tag, char fallback = '\0') const;
    std::vector<std::string> get_all(int tag) const;

    // Message type (tag 35) convenience.
    std::string msg_type() const;

    // Mutation ---------------------------------------------------------------
    void set(int tag, const std::string & value);   // replaces first or appends
    void add(int tag, const std::string & value);   // always appends (groups)
    void set_int(int tag, int64_t value);
    void set_double(int tag, double value, int precision = 6);
    void set_char(int tag, char value);
    void clear();

    // Codec ------------------------------------------------------------------

    // Encode a complete wire message (adds 8=, 9=, 10= around the body).
    static std::string encode(const FixMessage & msg);

    // Parse one complete wire message from `data`/`len`. Returns true on
    // success and sets `consumed` to the number of bytes used. Returns false
    // (consumed == 0) when the buffer holds only a partial message, and false
    // with `consumed` > 0 when the message is malformed / checksum-invalid
    // (caller should skip `consumed` bytes and resync).
    static bool parse(const char * data, size_t len, FixMessage & out, size_t & consumed);

    // Recompute the 3-digit checksum over a raw message prefix.
    static std::string compute_checksum(const char * data, size_t len);

    // Validate the checksum of a complete raw wire message.
    static bool validate_checksum(const char * data, size_t len);

    // Market-data helpers ----------------------------------------------------

    // Build a MarketDataSnapshot (35=W) carrying one symbol and MD entries.
    static FixMessage build_market_data_snapshot(const std::string & symbol,
                                                 double bid,
                                                 double bid_qty,
                                                 double ask,
                                                 double ask_qty,
                                                 double last,
                                                 double last_qty);

    // Parse 35=W / 35=X messages into MD entries. Entry type tag 269:
    // '0' = bid, '1' = offer, '2' = trade. Returns false for other types.
    struct MdEntry {
        char   entry_type;  // 269
        double price;       // 270
        double quantity;    // 271
    };
    bool parse_market_data_entries(std::string & symbol_out, std::vector<MdEntry> & entries_out) const;
};

// Minimal FIX session state: tracks sequence numbers and stamps outbound
// administrative messages. Performs NO socket I/O - the host transmits the
// encoded bytes and feeds inbound bytes to FixMessage::parse.
class FixSession {
public:
    FixSession(std::string sender_comp_id, std::string target_comp_id,
               std::string begin_string = "FIX.4.2");

    // Administrative message construction (stamps 34/49/52/56, bumps seq).
    FixMessage build_logon(int heartbeat_interval_sec = 30);
    FixMessage build_heartbeat(const std::string & test_req_id = "");
    FixMessage build_test_request(const std::string & test_req_id);
    FixMessage build_logout();

    // Stamp sequence/comp/time fields onto an application message.
    void prepare_outgoing(FixMessage & msg);

    // Incoming sequence-number validation. Returns true when `seq` equals the
    // expected number (and advances it); returns false on gap / out-of-order.
    bool validate_incoming_seq(int64_t seq);

    int64_t outgoing_seq() const;
    int64_t expected_incoming_seq() const;
    void    reset();

    const std::string & sender_comp_id() const;
    const std::string & target_comp_id() const;

private:
    std::string sender_;
    std::string target_;
    std::string begin_;
    int64_t     outgoing_seq_;
    int64_t     incoming_seq_;

    FixMessage build_admin(const std::string & msg_type);
};

// ============================================================================
// WebSocket Framing (RFC 6455) - pure byte-buffer codec, no sockets
// ============================================================================

enum class WsOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT         = 0x1,
    BINARY       = 0x2,
    CLOSE        = 0x8,
    PING         = 0x9,
    PONG         = 0xA
};

struct WsFrame {
    bool                 fin;
    WsOpcode             opcode;
    bool                 masked;
    std::vector<uint8_t> payload;

    WsFrame() : fin(true), opcode(WsOpcode::BINARY), masked(false) {}
};

class WebSocketFramer {
public:
    // Encode one frame. `mask` should be true for client-to-server traffic.
    static std::vector<uint8_t> encode(const WsFrame & frame, bool mask);

    // Streaming decoder: feed raw bytes, pop complete frames one at a time.
    // Returns false from next_frame() when the buffer holds a partial frame
    // or when a protocol violation is detected (use had_error() to tell).
    void feed(const uint8_t * data, size_t len);
    bool next_frame(WsFrame & out);
    bool had_error() const;
    void reset();

    // Convenience single-shot decode for complete frames.
    static bool decode(const uint8_t * data, size_t len, WsFrame & out, size_t & consumed);

private:
    std::vector<uint8_t> buffer_;
    bool                 error_;
};

// ============================================================================
// HTTP/1.1 Framing (request builder + response parser) - no sockets
// ============================================================================

class HttpRequestBuilder {
public:
    HttpRequestBuilder();

    HttpRequestBuilder & set_method(const std::string & method);   // GET, POST, ...
    HttpRequestBuilder & set_path(const std::string & path);       // /v1/ticks?sym=X
    HttpRequestBuilder & set_host(const std::string & host);       // adds Host header
    HttpRequestBuilder & add_header(const std::string & name, const std::string & value);
    HttpRequestBuilder & set_body(const std::string & body);       // sets Content-Length

    std::string build() const;

private:
    std::string                              method_;
    std::string                              path_;
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string                              body_;
    bool                                     has_host_;
    bool                                     has_content_length_;
};

struct HttpResponse {
    int                                         status_code;
    std::string                                 reason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string                                 body;

    HttpResponse() : status_code(0) {}

    std::string header(const std::string & name) const;   // case-insensitive
};

class HttpResponseParser {
public:
    // Feed raw response bytes; returns true once a complete response is held.
    // Bodies are framed by Content-Length; when no Content-Length header is
    // present the response is header-only (body empty) and completes at the
    // end of the header block.
    bool feed(const char * data, size_t len);
    bool complete() const;
    const HttpResponse & response() const;
    void reset();

private:
    std::string  buffer_;
    bool         complete_;
    HttpResponse response_;

    bool try_parse();
};

// ============================================================================
// Normalization Layer
// ============================================================================

// Maps venue-specific feed records onto canonical platform types: canonical
// symbol naming, venue -> Exchange enum, and price scaling (e.g. pence ->
// pounds, satoshi -> BTC).
class MarketDataNormalizer {
public:
    // Symbol mapping: venue-qualified symbol ("NASDAQ:AAPL" or "AAPL.OQ")
    // -> canonical symbol ("AAPL"). Unmapped symbols pass through unchanged.
    void register_symbol_mapping(const std::string & venue_symbol, const std::string & canonical);

    // Venue name -> Exchange enum ("NASDAQ" -> Exchange::NASDAQ).
    void register_exchange_mapping(const std::string & venue, Exchange exchange);

    // Price scale for a canonical symbol: incoming prices are multiplied by
    // `scale` (default 1.0). E.g. scale 0.01 converts pence quotes to pounds.
    void register_price_scale(const std::string & canonical_symbol, double scale);

    std::string canonical_symbol(const std::string & venue_symbol) const;
    Exchange    exchange_for(const std::string & venue) const;
    double      price_scale_for(const std::string & canonical_symbol) const;

    // Canonicalize a venue tick record. Applies symbol mapping, exchange
    // mapping, and price scaling; volume passes through unchanged.
    Tick normalize_tick(const std::string & venue,
                        const std::string & venue_symbol,
                        double bid,
                        double ask,
                        double last,
                        uint64_t volume,
                        int64_t feed_timestamp_ns) const;

    // Canonicalize a venue OHLCV bar record.
    OHLCV normalize_bar(const std::string & venue,
                        const std::string & venue_symbol,
                        double open,
                        double high,
                        double low,
                        double close,
                        uint64_t volume,
                        int64_t bar_start_ns,
                        int64_t bar_end_ns) const;

private:
    std::unordered_map<std::string, std::string> symbol_map_;
    std::unordered_map<std::string, Exchange>    exchange_map_;
    std::unordered_map<std::string, double>      price_scale_map_;
};

// ============================================================================
// Historical Data Store (in-memory index + compact binary file persistence)
// ============================================================================
//
// Binary file format ("GGMD" v1) - little-endian throughout:
//
//   Header:
//     char[8]    magic = "GGMD0001"
//     uint32     format_version (= 1)
//     uint32     symbol_count
//
//   Per symbol block:
//     uint16     symbol_length
//     char[]     symbol bytes (no NUL)
//     uint64     record_count
//     Records (delta + run-length encoded, in ascending timestamp order):
//       Record 0 (absolute anchor):
//         uvarint   feed_timestamp_ns
//         svarint   bid_fixed    (price * 1e8, zigzag varint)
//         svarint   ask_fixed
//         svarint   last_fixed
//         uvarint   volume
//       Record i > 0:
//         uint8     control
//           0x00: explicit record ->
//                   svarint ts_delta    (feed_ts - prev_feed_ts)
//                   svarint bid_delta   (fixed-point deltas vs previous record)
//                   svarint ask_delta
//                   svarint last_delta
//                   svarint volume_delta
//           0x01: run-length record -> uvarint count
//                   repeats the previous record `count` times, advancing the
//                   timestamp by the previous ts_delta each step (compact
//                   encoding for regular-interval / unchanged-price series)
//
//   uvarint = unsigned LEB128; svarint = zigzag-encoded signed LEB128.
//   Prices are quantized to 1e-8 fixed point so encoding is exact and
//   deterministic across save/load.

class HistoricalDataStore {
public:
    HistoricalDataStore();

    // Append a tick. Ticks for a symbol must arrive in non-decreasing
    // feed_timestamp_ns order; out-of-order appends return false.
    bool append(const Tick & tick);

    // Latest tick at or before `timestamp_ns`. Returns false when none.
    bool point_query(const std::string & symbol, int64_t timestamp_ns, Tick & out) const;

    // All ticks with start_ns <= feed_timestamp_ns <= end_ns, ascending.
    std::vector<Tick> range_query(const std::string & symbol, int64_t start_ns, int64_t end_ns) const;

    std::vector<std::string> symbols() const;
    size_t                 tick_count(const std::string & symbol) const;
    size_t                 total_ticks() const;
    void                   clear();

    // Persistence ------------------------------------------------------------
    bool save(const std::string & path) const;
    bool load(const std::string & path);

    // Replay -----------------------------------------------------------------
    // Stream a symbol's ticks back in ascending timestamp order. With
    // `speed_multiplier` > 0 the replay sleeps between ticks to reproduce
    // original timing scaled by the multiplier (2.0 = twice real-time);
    // `speed_multiplier` <= 0 replays as fast as possible. Returns the number
    // of ticks delivered.
    uint64_t replay(const std::string & symbol,
                    const std::function<void(const Tick &)> & callback,
                    double speed_multiplier = 0.0) const;

private:
    std::unordered_map<std::string, std::vector<Tick>> series_;
};

// ============================================================================
// Latest-Value Cache & Pub/Sub Bus
// ============================================================================

// Thread-safe latest-value cache keyed by canonical symbol.
class MarketDataCache {
public:
    void update(const Tick & tick);
    void update(const OHLCV & bar);

    bool get(const std::string & symbol, Tick & out) const;
    bool get_bar(const std::string & symbol, OHLCV & out) const;

    std::unordered_map<std::string, Tick>  snapshot() const;
    std::vector<std::string>               symbols() const;
    size_t                                 size() const;
    void                                   clear();

private:
    mutable std::mutex                     mutex_;
    std::unordered_map<std::string, Tick>  latest_ticks_;
    std::unordered_map<std::string, OHLCV> latest_bars_;
};

// Lightweight in-process publish/subscribe bus with per-subscriber callbacks.
// Subscriptions match an exact symbol or the "*" wildcard.
class MarketDataBus {
public:
    using Callback = std::function<void(const MarketDataMessage &)>;

    // Subscribe to one exact symbol. Returns a subscription id.
    uint64_t subscribe(const std::string & symbol, Callback callback);

    // Subscribe to every message (wildcard "*").
    uint64_t subscribe_all(Callback callback);

    void unsubscribe(uint64_t subscription_id);

    // Fan out to all matching subscribers (exact symbol + wildcards).
    // The symbol is taken from the message payload (tick/bar/book).
    void publish(const MarketDataMessage & message);

    size_t   subscriber_count() const;
    uint64_t messages_published() const;
    uint64_t messages_delivered() const;
    void     reset_metrics();

private:
    struct Subscription {
        uint64_t    id;
        std::string symbol;      // exact symbol or "*"
        Callback    callback;
    };

    std::mutex               mutex_;
    std::vector<Subscription> subscriptions_;
    uint64_t                 next_id_;
    uint64_t                 published_;
    uint64_t                 delivered_;
};

} // namespace marketdata
} // namespace ggnucash
