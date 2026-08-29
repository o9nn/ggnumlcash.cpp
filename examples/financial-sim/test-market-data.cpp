#include "market-data.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace ggnucash::marketdata;

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

#define ASSERT_NEAR(a, b, tol) do { double _va = (a), _vb = (b), _vt = (tol); \
    if (std::fabs(_va - _vb) > _vt) { \
        throw std::runtime_error(std::string("Assertion failed: |") + #a + " - " + #b + \
            "| = " + std::to_string(std::fabs(_va - _vb)) + " > " + std::to_string(_vt) + \
            " at line " + std::to_string(__LINE__)); \
    } } while(0)

namespace {

Tick make_tick(const std::string & symbol, double bid, double ask, double last,
               uint64_t volume, int64_t ts_ns) {
    Tick t;
    t.symbol             = symbol;
    t.bid                = bid;
    t.ask                = ask;
    t.last               = last;
    t.volume             = volume;
    t.exchange           = Exchange::SIMULATED;
    t.feed_timestamp_ns  = ts_ns;
    return t;
}

} // anonymous namespace

// ============================================================================
// Timestamping Tests
// ============================================================================

void test_timestamp_monotonic_ordering() {
    TEST("Monotonic timestamps are non-decreasing");
    const int64_t a = TimestampSource::now_monotonic_ns();
    const int64_t b = TimestampSource::now_monotonic_ns();
    ASSERT_TRUE(b >= a);
    ASSERT_TRUE(a > 0);
    TEST_END("Monotonic timestamps are non-decreasing");
}

void test_timestamp_wall_clock_epoch() {
    TEST("Wall clock is near epoch time");
    const int64_t wall = TimestampSource::now_wall_ns();
    // After 2020-01-01 and before 2100-01-01 in ns.
    ASSERT_TRUE(wall > 1577836800000000000LL);
    ASSERT_TRUE(wall < 4102444800000000000LL);
    TEST_END("Wall clock is near epoch time");
}

void test_timestamper_marks_and_latency() {
    TEST("Timestamper stamps receipt and computes latency");
    HardwareTimestamper ts;
    Tick tick = make_tick("AAPL", 100.0, 100.05, 100.02, 500,
                          TimestampSource::now_wall_ns());
    ts.stamp(tick);
    ASSERT_TRUE(tick.receive_timestamp_ns > 0);
    ASSERT_TRUE(tick.receive_timestamp_ns >= tick.feed_timestamp_ns);
    const int64_t latency = HardwareTimestamper::latency_ns(tick);
    ASSERT_TRUE(latency >= 0);
    ASSERT_TRUE(latency < 60000000000LL);   // under a minute in tests
    TEST_END("Timestamper stamps receipt and computes latency");
}

void test_timestamper_latency_edge_cases() {
    TEST("Latency edge cases return zero");
    ASSERT_EQ(HardwareTimestamper::latency_ns(0, 100), 0);
    ASSERT_EQ(HardwareTimestamper::latency_ns(100, 0), 0);
    ASSERT_EQ(HardwareTimestamper::latency_ns(200, 100), 0);   // inverted
    ASSERT_EQ(HardwareTimestamper::latency_ns(100, 250), 150);
    TEST_END("Latency edge cases return zero");
}

// ============================================================================
// FIX Protocol Tests
// ============================================================================

void test_fix_encode_parse_roundtrip() {
    TEST("FIX encode/parse round-trip");
    FixMessage msg;
    msg.begin_string = "FIX.4.4";
    msg.set(35, "D");
    msg.set_int(34, 42);
    msg.set(49, "SENDER");
    msg.set(56, "TARGET");
    msg.set(55, "AAPL");
    msg.set_int(38, 1000);
    msg.set_double(44, 187.55, 2);

    const std::string wire = FixMessage::encode(msg);
    ASSERT_TRUE(wire.find("8=FIX.4.4\x01") == 0);
    ASSERT_TRUE(wire.find("35=D") != std::string::npos);

    FixMessage parsed;
    size_t consumed = 0;
    ASSERT_TRUE(FixMessage::parse(wire.data(), wire.size(), parsed, consumed));
    ASSERT_EQ(consumed, wire.size());
    ASSERT_EQ(parsed.begin_string, std::string("FIX.4.4"));
    ASSERT_EQ(parsed.msg_type(), std::string("D"));
    ASSERT_EQ(parsed.get_int(34), 42);
    ASSERT_EQ(parsed.get(49), std::string("SENDER"));
    ASSERT_EQ(parsed.get(55), std::string("AAPL"));
    ASSERT_EQ(parsed.get_int(38), 1000);
    ASSERT_NEAR(parsed.get_double(44), 187.55, 1e-9);
    TEST_END("FIX encode/parse round-trip");
}

void test_fix_checksum_validation() {
    TEST("FIX checksum computation and validation");
    FixMessage msg;
    msg.set(35, "0");
    msg.set_int(34, 1);
    const std::string wire = FixMessage::encode(msg);
    ASSERT_TRUE(FixMessage::validate_checksum(wire.data(), wire.size()));
    ASSERT_EQ(wire.substr(wire.size() - 7, 3), std::string("10="));
    TEST_END("FIX checksum computation and validation");
}

void test_fix_checksum_failure_detected() {
    TEST("FIX checksum failure is detected");
    FixMessage msg;
    msg.set(35, "0");
    msg.set_int(34, 7);
    msg.set(49, "SENDER");
    std::string wire = FixMessage::encode(msg);

    // Corrupt one body byte without touching the checksum.
    const size_t pos = wire.find("SENDER");
    ASSERT_TRUE(pos != std::string::npos);
    wire[pos] = (wire[pos] == 'X') ? 'Y' : 'X';

    ASSERT_FALSE(FixMessage::validate_checksum(wire.data(), wire.size()));
    FixMessage parsed;
    size_t consumed = 0;
    ASSERT_FALSE(FixMessage::parse(wire.data(), wire.size(), parsed, consumed));
    ASSERT_TRUE(consumed > 0);   // malformed message is consumed for resync
    TEST_END("FIX checksum failure is detected");
}

void test_fix_partial_message_waits() {
    TEST("FIX partial message is not consumed");
    FixMessage msg;
    msg.set(35, "0");
    const std::string wire = FixMessage::encode(msg);
    const std::string partial = wire.substr(0, wire.size() - 5);

    FixMessage parsed;
    size_t consumed = 99;
    ASSERT_FALSE(FixMessage::parse(partial.data(), partial.size(), parsed, consumed));
    ASSERT_EQ(consumed, 0);
    TEST_END("FIX partial message is not consumed");
}

void test_fix_body_length_enforced() {
    TEST("FIX body length mismatch rejected");
    FixMessage msg;
    msg.set(35, "0");
    msg.set_int(34, 1);
    msg.set(49, "AB");
    std::string wire = FixMessage::encode(msg);

    // Widen the declared body length and recompute checksum so only the
    // length check can fail.
    const size_t pos9 = wire.find("9=");
    ASSERT_TRUE(pos9 != std::string::npos);
    const size_t soh9 = wire.find('\x01', pos9);
    const int declared = std::stoi(wire.substr(pos9 + 2, soh9 - pos9 - 2));
    wire.replace(pos9 + 2, soh9 - pos9 - 2, std::to_string(declared + 1));
    const size_t trailer = wire.find("\x01" "10=");
    ASSERT_TRUE(trailer != std::string::npos);
    const std::string cksum = FixMessage::compute_checksum(wire.data(), trailer + 1);
    wire.replace(trailer + 4, 3, cksum);

    FixMessage parsed;
    size_t consumed = 0;
    ASSERT_FALSE(FixMessage::parse(wire.data(), wire.size(), parsed, consumed));
    ASSERT_TRUE(consumed > 0);
    TEST_END("FIX body length mismatch rejected");
}

void test_fix_market_data_snapshot() {
    TEST("FIX MarketDataSnapshot build and parse (35=W)");
    FixMessage msg = FixMessage::build_market_data_snapshot(
        "MSFT", 299.90, 300, 300.10, 250, 300.00, 100);
    ASSERT_EQ(msg.msg_type(), std::string("W"));
    ASSERT_EQ(msg.get_int(268), 3);

    const std::string wire = FixMessage::encode(msg);
    FixMessage parsed;
    size_t consumed = 0;
    ASSERT_TRUE(FixMessage::parse(wire.data(), wire.size(), parsed, consumed));

    std::string symbol;
    std::vector<FixMessage::MdEntry> entries;
    ASSERT_TRUE(parsed.parse_market_data_entries(symbol, entries));
    ASSERT_EQ(symbol, std::string("MSFT"));
    ASSERT_EQ(entries.size(), static_cast<size_t>(3));
    ASSERT_EQ(entries[0].entry_type, '0');
    ASSERT_NEAR(entries[0].price, 299.90, 1e-9);
    ASSERT_NEAR(entries[0].quantity, 300.0, 1e-9);
    ASSERT_EQ(entries[1].entry_type, '1');
    ASSERT_NEAR(entries[1].price, 300.10, 1e-9);
    ASSERT_EQ(entries[2].entry_type, '2');
    ASSERT_NEAR(entries[2].price, 300.00, 1e-9);
    TEST_END("FIX MarketDataSnapshot build and parse (35=W)");
}

void test_fix_incremental_refresh_parse() {
    TEST("FIX IncrementalRefresh parse (35=X)");
    FixMessage msg;
    msg.set(35, "X");
    msg.set(55, "EURUSD");
    msg.set_int(268, 2);
    msg.add(269, "0");  msg.add(270, "1.08450"); msg.add(271, "1000000");
    msg.add(269, "1");  msg.add(270, "1.08460"); msg.add(271, "2000000");

    std::string symbol;
    std::vector<FixMessage::MdEntry> entries;
    ASSERT_TRUE(msg.parse_market_data_entries(symbol, entries));
    ASSERT_EQ(symbol, std::string("EURUSD"));
    ASSERT_EQ(entries.size(), static_cast<size_t>(2));
    ASSERT_EQ(entries[0].entry_type, '0');
    ASSERT_NEAR(entries[0].price, 1.0845, 1e-9);
    ASSERT_NEAR(entries[1].quantity, 2000000.0, 1e-6);
    TEST_END("FIX IncrementalRefresh parse (35=X)");
}

void test_fix_parse_rejects_non_market_data_entries() {
    TEST("FIX entry parser rejects non-market-data messages");
    FixMessage msg;
    msg.set(35, "D");
    std::string symbol;
    std::vector<FixMessage::MdEntry> entries;
    ASSERT_FALSE(msg.parse_market_data_entries(symbol, entries));
    TEST_END("FIX entry parser rejects non-market-data messages");
}

void test_fix_session_sequences() {
    TEST("FixSession sequence numbers and logon/heartbeat");
    FixSession session("GGNUCASH", "VENUE", "FIX.4.2");

    FixMessage logon = session.build_logon(30);
    ASSERT_EQ(logon.msg_type(), std::string("A"));
    ASSERT_EQ(logon.get_int(34), 1);
    ASSERT_EQ(logon.get_int(108), 30);
    ASSERT_EQ(logon.get(49), std::string("GGNUCASH"));
    ASSERT_EQ(logon.get(56), std::string("VENUE"));
    ASSERT_TRUE(logon.has(52));

    FixMessage hb = session.build_heartbeat("REQ-1");
    ASSERT_EQ(hb.msg_type(), std::string("0"));
    ASSERT_EQ(hb.get_int(34), 2);
    ASSERT_EQ(hb.get(112), std::string("REQ-1"));

    FixMessage logout = session.build_logout();
    ASSERT_EQ(logout.msg_type(), std::string("5"));
    ASSERT_EQ(logout.get_int(34), 3);
    ASSERT_EQ(session.outgoing_seq(), 4);

    // Logon message must encode/parse cleanly through the wire codec.
    const std::string wire = FixMessage::encode(logon);
    FixMessage parsed;
    size_t consumed = 0;
    ASSERT_TRUE(FixMessage::parse(wire.data(), wire.size(), parsed, consumed));
    ASSERT_EQ(parsed.get_int(108), 30);
    TEST_END("FixSession sequence numbers and logon/heartbeat");
}

void test_fix_session_incoming_validation() {
    TEST("FixSession incoming sequence validation");
    FixSession session("A", "B");
    ASSERT_TRUE(session.validate_incoming_seq(1));
    ASSERT_TRUE(session.validate_incoming_seq(2));
    ASSERT_FALSE(session.validate_incoming_seq(4));   // gap
    ASSERT_FALSE(session.validate_incoming_seq(2));   // replay
    ASSERT_TRUE(session.validate_incoming_seq(3));
    session.reset();
    ASSERT_TRUE(session.validate_incoming_seq(1));
    TEST_END("FixSession incoming sequence validation");
}

// ============================================================================
// WebSocket Framing Tests
// ============================================================================

void test_ws_text_roundtrip() {
    TEST("WebSocket text frame round-trip");
    WsFrame frame;
    frame.opcode  = WsOpcode::TEXT;
    frame.payload = {'h', 'e', 'l', 'l', 'o'};

    const auto encoded = WebSocketFramer::encode(frame, false);
    ASSERT_EQ(encoded.size(), static_cast<size_t>(7));
    ASSERT_EQ(encoded[0], 0x81);

    WsFrame decoded;
    size_t consumed = 0;
    ASSERT_TRUE(WebSocketFramer::decode(encoded.data(), encoded.size(), decoded, consumed));
    ASSERT_EQ(consumed, encoded.size());
    ASSERT_TRUE(decoded.fin);
    ASSERT_TRUE(decoded.opcode == WsOpcode::TEXT);
    ASSERT_EQ(decoded.payload.size(), static_cast<size_t>(5));
    ASSERT_EQ(std::string(decoded.payload.begin(), decoded.payload.end()), std::string("hello"));
    TEST_END("WebSocket text frame round-trip");
}

void test_ws_masked_frame_roundtrip() {
    TEST("WebSocket masked frame round-trip");
    const std::string text = "{\"symbol\":\"AAPL\",\"bid\":187.5}";
    WsFrame frame;
    frame.opcode  = WsOpcode::TEXT;
    frame.payload.assign(text.begin(), text.end());

    const auto encoded = WebSocketFramer::encode(frame, true);
    ASSERT_TRUE((encoded[1] & 0x80) != 0);   // mask bit set

    WsFrame decoded;
    size_t consumed = 0;
    ASSERT_TRUE(WebSocketFramer::decode(encoded.data(), encoded.size(), decoded, consumed));
    ASSERT_TRUE(decoded.masked);
    ASSERT_EQ(std::string(decoded.payload.begin(), decoded.payload.end()), text);
    TEST_END("WebSocket masked frame round-trip");
}

void test_ws_16bit_length() {
    TEST("WebSocket 16-bit extended length");
    WsFrame frame;
    frame.opcode  = WsOpcode::BINARY;
    frame.payload.assign(1000, 0xAB);

    const auto encoded = WebSocketFramer::encode(frame, false);
    ASSERT_EQ(encoded[1] & 0x7F, 126);
    ASSERT_EQ(encoded.size(), static_cast<size_t>(4 + 1000));

    WsFrame decoded;
    size_t consumed = 0;
    ASSERT_TRUE(WebSocketFramer::decode(encoded.data(), encoded.size(), decoded, consumed));
    ASSERT_EQ(decoded.payload.size(), static_cast<size_t>(1000));
    ASSERT_EQ(decoded.payload[999], 0xAB);
    TEST_END("WebSocket 16-bit extended length");
}

void test_ws_64bit_length() {
    TEST("WebSocket 64-bit extended length");
    WsFrame frame;
    frame.opcode  = WsOpcode::BINARY;
    frame.payload.assign(70000, 0x11);   // > 65535 forces the 64-bit form

    const auto encoded = WebSocketFramer::encode(frame, false);
    ASSERT_EQ(encoded[1] & 0x7F, 127);
    ASSERT_EQ(encoded.size(), static_cast<size_t>(10 + 70000));

    WsFrame decoded;
    size_t consumed = 0;
    ASSERT_TRUE(WebSocketFramer::decode(encoded.data(), encoded.size(), decoded, consumed));
    ASSERT_EQ(consumed, encoded.size());
    ASSERT_EQ(decoded.payload.size(), static_cast<size_t>(70000));
    ASSERT_EQ(decoded.payload[69999], 0x11);
    TEST_END("WebSocket 64-bit extended length");
}

void test_ws_control_frames() {
    TEST("WebSocket ping/pong/close opcodes");
    WsFrame ping;
    ping.opcode = WsOpcode::PING;
    const auto enc_ping = WebSocketFramer::encode(ping, false);
    ASSERT_EQ(enc_ping[0], 0x89);

    WsFrame pong;
    pong.opcode = WsOpcode::PONG;
    const auto enc_pong = WebSocketFramer::encode(pong, false);
    ASSERT_EQ(enc_pong[0], 0x8A);

    WsFrame close_frame;
    close_frame.opcode = WsOpcode::CLOSE;
    const auto enc_close = WebSocketFramer::encode(close_frame, false);
    ASSERT_EQ(enc_close[0], 0x88);

    WsFrame decoded;
    size_t consumed = 0;
    ASSERT_TRUE(WebSocketFramer::decode(enc_ping.data(), enc_ping.size(), decoded, consumed));
    ASSERT_TRUE(decoded.opcode == WsOpcode::PING);
    TEST_END("WebSocket ping/pong/close opcodes");
}

void test_ws_streaming_decoder_partial() {
    TEST("WebSocket streaming decoder handles split feeds");
    WsFrame frame;
    frame.opcode  = WsOpcode::TEXT;
    const std::string text = "streaming-market-data-payload";
    frame.payload.assign(text.begin(), text.end());
    const auto encoded = WebSocketFramer::encode(frame, true);

    WebSocketFramer framer;
    framer.feed(encoded.data(), 3);                 // partial header
    WsFrame out;
    ASSERT_FALSE(framer.next_frame(out));           // incomplete -> wait
    ASSERT_FALSE(framer.had_error());
    framer.feed(encoded.data() + 3, encoded.size() - 3);
    ASSERT_TRUE(framer.next_frame(out));
    ASSERT_EQ(std::string(out.payload.begin(), out.payload.end()), text);
    ASSERT_FALSE(framer.next_frame(out));           // drained
    TEST_END("WebSocket streaming decoder handles split feeds");
}

void test_ws_streaming_decoder_back_to_back() {
    TEST("WebSocket streaming decoder handles back-to-back frames");
    WsFrame a; a.opcode = WsOpcode::TEXT;   a.payload = {'1'};
    WsFrame b; b.opcode = WsOpcode::BINARY; b.payload = {0x01, 0x02, 0x03};
    auto ea = WebSocketFramer::encode(a, false);
    auto eb = WebSocketFramer::encode(b, false);
    ea.insert(ea.end(), eb.begin(), eb.end());

    WebSocketFramer framer;
    framer.feed(ea.data(), ea.size());
    WsFrame out;
    ASSERT_TRUE(framer.next_frame(out));
    ASSERT_TRUE(out.opcode == WsOpcode::TEXT);
    ASSERT_TRUE(framer.next_frame(out));
    ASSERT_TRUE(out.opcode == WsOpcode::BINARY);
    ASSERT_EQ(out.payload.size(), static_cast<size_t>(3));
    ASSERT_FALSE(framer.next_frame(out));
    TEST_END("WebSocket streaming decoder handles back-to-back frames");
}

// ============================================================================
// HTTP Framing Tests
// ============================================================================

void test_http_request_builder() {
    TEST("HTTP request builder");
    const std::string req = HttpRequestBuilder()
        .set_method("GET")
        .set_path("/v1/ticks?symbol=AAPL")
        .set_host("api.example.com")
        .add_header("Accept", "application/json")
        .build();

    ASSERT_TRUE(req.find("GET /v1/ticks?symbol=AAPL HTTP/1.1\r\n") == 0);
    ASSERT_TRUE(req.find("Host: api.example.com\r\n") != std::string::npos);
    ASSERT_TRUE(req.find("Accept: application/json\r\n") != std::string::npos);
    ASSERT_TRUE(req.find("\r\n\r\n") != std::string::npos);
    TEST_END("HTTP request builder");
}

void test_http_request_builder_body() {
    TEST("HTTP request builder sets Content-Length for body");
    const std::string req = HttpRequestBuilder()
        .set_method("POST")
        .set_path("/v1/subscribe")
        .set_host("feed.example.com")
        .set_body("{\"symbols\":[\"AAPL\"]}")
        .build();
    ASSERT_TRUE(req.find("POST /v1/subscribe HTTP/1.1\r\n") == 0);
    ASSERT_TRUE(req.find("Content-Length: 21\r\n") != std::string::npos);
    ASSERT_TRUE(req.find("\r\n\r\n{\"symbols\":[\"AAPL\"]}") != std::string::npos);
    TEST_END("HTTP request builder sets Content-Length for body");
}

void test_http_response_parse() {
    TEST("HTTP response parse with body");
    const std::string body = "{\"bid\":187.50,\"ask\":187.55}";
    const std::string raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "\r\n" + body;

    HttpResponseParser parser;
    ASSERT_TRUE(parser.feed(raw.data(), raw.size()));
    ASSERT_TRUE(parser.complete());
    ASSERT_EQ(parser.response().status_code, 200);
    ASSERT_EQ(parser.response().reason, std::string("OK"));
    ASSERT_EQ(parser.response().header("content-type"), std::string("application/json"));
    ASSERT_EQ(parser.response().body, body);
    TEST_END("HTTP response parse with body");
}

void test_http_response_parse_split_feed() {
    TEST("HTTP response parse across split feeds");
    const std::string body = "0123456789";
    const std::string raw =
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Length: 10\r\n"
        "\r\n" + body;

    HttpResponseParser parser;
    ASSERT_FALSE(parser.feed(raw.data(), 20));
    ASSERT_TRUE(parser.feed(raw.data() + 20, raw.size() - 20));
    ASSERT_EQ(parser.response().status_code, 404);
    ASSERT_EQ(parser.response().reason, std::string("Not Found"));
    ASSERT_EQ(parser.response().body, body);
    TEST_END("HTTP response parse across split feeds");
}

void test_http_response_header_only() {
    TEST("HTTP response without Content-Length completes at headers");
    const std::string raw = "HTTP/1.1 204 No Content\r\nServer: sim\r\n\r\n";
    HttpResponseParser parser;
    ASSERT_TRUE(parser.feed(raw.data(), raw.size()));
    ASSERT_EQ(parser.response().status_code, 204);
    ASSERT_EQ(parser.response().header("Server"), std::string("sim"));
    ASSERT_TRUE(parser.response().body.empty());
    TEST_END("HTTP response without Content-Length completes at headers");
}

// ============================================================================
// Normalizer Tests
// ============================================================================

void test_normalizer_symbol_mapping() {
    TEST("Normalizer maps venue symbols to canonical");
    MarketDataNormalizer norm;
    norm.register_symbol_mapping("AAPL.OQ", "AAPL");
    norm.register_exchange_mapping("NASDAQ", Exchange::NASDAQ);

    Tick tick = norm.normalize_tick("NASDAQ", "AAPL.OQ", 187.50, 187.55, 187.52, 1000, 42);
    ASSERT_EQ(tick.symbol, std::string("AAPL"));
    ASSERT_TRUE(tick.exchange == Exchange::NASDAQ);
    ASSERT_NEAR(tick.bid, 187.50, 1e-12);
    ASSERT_EQ(tick.volume, static_cast<uint64_t>(1000));
    ASSERT_EQ(tick.feed_timestamp_ns, 42);

    // Unmapped symbols pass through unchanged.
    Tick raw = norm.normalize_tick("NYSE", "IBM", 1.0, 1.1, 1.05, 10, 1);
    ASSERT_EQ(raw.symbol, std::string("IBM"));
    ASSERT_TRUE(raw.exchange == Exchange::UNKNOWN);
    TEST_END("Normalizer maps venue symbols to canonical");
}

void test_normalizer_price_scaling() {
    TEST("Normalizer applies price scaling");
    MarketDataNormalizer norm;
    norm.register_symbol_mapping("VOD.L", "VOD");
    norm.register_exchange_mapping("LSE", Exchange::LSE);
    norm.register_price_scale("VOD", 0.01);   // pence -> pounds

    Tick tick = norm.normalize_tick("LSE", "VOD.L", 7050.0, 7060.0, 7055.0, 500, 7);
    ASSERT_NEAR(tick.bid, 70.50, 1e-12);
    ASSERT_NEAR(tick.ask, 70.60, 1e-12);
    ASSERT_NEAR(tick.last, 70.55, 1e-12);
    TEST_END("Normalizer applies price scaling");
}

void test_normalizer_bar() {
    TEST("Normalizer canonicalizes OHLCV bars");
    MarketDataNormalizer norm;
    norm.register_exchange_mapping("JSE", Exchange::JSE);
    OHLCV bar = norm.normalize_bar("JSE", "SOL", 300.0, 310.0, 295.0, 305.0,
                                   123456, 1000, 2000);
    ASSERT_EQ(bar.symbol, std::string("SOL"));
    ASSERT_TRUE(bar.exchange == Exchange::JSE);
    ASSERT_NEAR(bar.high, 310.0, 1e-12);
    ASSERT_EQ(bar.bar_start_ns, 1000);
    ASSERT_EQ(bar.bar_end_ns, 2000);
    TEST_END("Normalizer canonicalizes OHLCV bars");
}

void test_exchange_enum_strings() {
    TEST("Exchange enum string round-trip");
    ASSERT_EQ(exchange_to_string(Exchange::NASDAQ), std::string("NASDAQ"));
    ASSERT_TRUE(exchange_from_string("JSE") == Exchange::JSE);
    ASSERT_TRUE(exchange_from_string("nope") == Exchange::UNKNOWN);
    TEST_END("Exchange enum string round-trip");
}

// ============================================================================
// Historical Data Store Tests
// ============================================================================

void test_store_append_and_ordering() {
    TEST("Store appends in order and rejects out-of-order");
    HistoricalDataStore store;
    ASSERT_TRUE(store.append(make_tick("AAPL", 100.0, 100.1, 100.05, 100, 1000)));
    ASSERT_TRUE(store.append(make_tick("AAPL", 100.1, 100.2, 100.15, 200, 2000)));
    ASSERT_TRUE(store.append(make_tick("AAPL", 100.2, 100.3, 100.25, 300, 2000))); // equal ts ok
    ASSERT_FALSE(store.append(make_tick("AAPL", 99.0, 99.1, 99.05, 50, 1500)));    // out of order
    ASSERT_EQ(store.tick_count("AAPL"), static_cast<size_t>(3));
    ASSERT_EQ(store.total_ticks(), static_cast<size_t>(3));
    TEST_END("Store appends in order and rejects out-of-order");
}

void test_store_point_and_range_query() {
    TEST("Store point and range queries");
    HistoricalDataStore store;
    for (int i = 1; i <= 10; i++) {
        ASSERT_TRUE(store.append(make_tick("MSFT", 100.0 + i, 100.1 + i, 100.05 + i,
                                           static_cast<uint64_t>(i * 100), i * 1000)));
    }

    Tick out;
    ASSERT_TRUE(store.point_query("MSFT", 5500, out));
    ASSERT_EQ(out.feed_timestamp_ns, 5000);
    ASSERT_NEAR(out.last, 105.05, 1e-9);
    ASSERT_TRUE(store.point_query("MSFT", 10000, out));
    ASSERT_EQ(out.feed_timestamp_ns, 10000);
    ASSERT_FALSE(store.point_query("MSFT", 500, out));       // before first
    ASSERT_FALSE(store.point_query("NOPE", 5000, out));      // unknown symbol

    const auto range = store.range_query("MSFT", 3000, 6000);
    ASSERT_EQ(range.size(), static_cast<size_t>(4));
    ASSERT_EQ(range.front().feed_timestamp_ns, 3000);
    ASSERT_EQ(range.back().feed_timestamp_ns, 6000);
    for (size_t i = 1; i < range.size(); i++) {
        ASSERT_TRUE(range[i].feed_timestamp_ns >= range[i - 1].feed_timestamp_ns);
    }

    ASSERT_EQ(store.symbols().size(), static_cast<size_t>(1));
    ASSERT_EQ(store.symbols()[0], std::string("MSFT"));
    TEST_END("Store point and range queries");
}

void test_store_save_load_roundtrip() {
    TEST("Store binary save/load round-trip");
    HistoricalDataStore store;
    for (int i = 0; i < 50; i++) {
        Tick t = make_tick("AAPL", 180.0 + 0.01 * i, 180.05 + 0.01 * i,
                           180.025 + 0.01 * i, static_cast<uint64_t>(1000 + i),
                           1700000000000000000LL + i * 1000000LL);
        ASSERT_TRUE(store.append(t));
    }
    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(store.append(make_tick("MSFT", 300.0, 300.1, 300.05,
                                           500, 1700000000000000000LL + i * 5000000LL)));
    }

    const std::string path = "/tmp/i003_store_roundtrip.ggmd";
    std::remove(path.c_str());
    ASSERT_TRUE(store.save(path));

    HistoricalDataStore loaded;
    ASSERT_TRUE(loaded.load(path));
    ASSERT_EQ(loaded.total_ticks(), store.total_ticks());
    ASSERT_EQ(loaded.tick_count("AAPL"), static_cast<size_t>(50));
    ASSERT_EQ(loaded.tick_count("MSFT"), static_cast<size_t>(10));

    const auto original = store.range_query("AAPL", 0, INT64_MAX);
    const auto restored = loaded.range_query("AAPL", 0, INT64_MAX);
    ASSERT_EQ(original.size(), restored.size());
    for (size_t i = 0; i < original.size(); i++) {
        ASSERT_EQ(original[i].feed_timestamp_ns, restored[i].feed_timestamp_ns);
        ASSERT_NEAR(original[i].bid, restored[i].bid, 1e-9);
        ASSERT_NEAR(original[i].ask, restored[i].ask, 1e-9);
        ASSERT_NEAR(original[i].last, restored[i].last, 1e-9);
        ASSERT_EQ(original[i].volume, restored[i].volume);
    }
    std::remove(path.c_str());
    TEST_END("Store binary save/load round-trip");
}

void test_store_run_length_encoding() {
    TEST("Store run-length encoding round-trip (regular unchanged series)");
    HistoricalDataStore store;
    const int64_t base_ts = 1700000000000000000LL;
    for (int i = 0; i < 100; i++) {
        // Constant prices at regular 1ms intervals -> runs after first delta.
        ASSERT_TRUE(store.append(make_tick("EURUSD", 1.0845, 1.0846, 1.08455,
                                           1000000, base_ts + i * 1000000LL)));
    }
    const std::string path = "/tmp/i003_store_rle.ggmd";
    std::remove(path.c_str());
    ASSERT_TRUE(store.save(path));

    HistoricalDataStore loaded;
    ASSERT_TRUE(loaded.load(path));
    ASSERT_EQ(loaded.tick_count("EURUSD"), static_cast<size_t>(100));
    const auto series = loaded.range_query("EURUSD", 0, INT64_MAX);
    ASSERT_EQ(series.size(), static_cast<size_t>(100));
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(series[i].feed_timestamp_ns, base_ts + i * 1000000LL);
        ASSERT_NEAR(series[i].bid, 1.0845, 1e-9);
        ASSERT_NEAR(series[i].ask, 1.0846, 1e-9);
        ASSERT_EQ(series[i].volume, static_cast<uint64_t>(1000000));
    }
    std::remove(path.c_str());
    TEST_END("Store run-length encoding round-trip (regular unchanged series)");
}

void test_store_load_rejects_bad_file() {
    TEST("Store load rejects bad magic and truncated files");
    const std::string path = "/tmp/i003_store_bad.ggmd";
    {
        FILE * f = std::fopen(path.c_str(), "wb");
        ASSERT_TRUE(f != nullptr);
        const char junk[] = "NOTGGMD-at-all";
        std::fwrite(junk, 1, sizeof(junk) - 1, f);
        std::fclose(f);
    }
    HistoricalDataStore store;
    ASSERT_FALSE(store.load(path));
    ASSERT_FALSE(store.load("/tmp/i003_store_missing_file.ggmd"));
    std::remove(path.c_str());
    TEST_END("Store load rejects bad magic and truncated files");
}

void test_store_replay_ordering() {
    TEST("Store replay delivers ticks in timestamp order");
    HistoricalDataStore store;
    for (int i = 0; i < 20; i++) {
        ASSERT_TRUE(store.append(make_tick("AAPL", 100.0 + i, 100.1 + i, 100.05 + i,
                                           100, 1000000LL * (i + 1))));
    }
    std::vector<int64_t> seen;
    const uint64_t delivered = store.replay("AAPL",
        [&seen](const Tick & t) { seen.push_back(t.feed_timestamp_ns); });
    ASSERT_EQ(delivered, static_cast<uint64_t>(20));
    ASSERT_EQ(seen.size(), static_cast<size_t>(20));
    for (size_t i = 1; i < seen.size(); i++) {
        ASSERT_TRUE(seen[i] > seen[i - 1]);
    }
    ASSERT_EQ(seen.front(), 1000000LL);
    ASSERT_EQ(seen.back(), 20000000LL);
    ASSERT_EQ(store.replay("NOPE", [](const Tick &) {}), static_cast<uint64_t>(0));
    TEST_END("Store replay delivers ticks in timestamp order");
}

void test_store_replay_speed_multiplier() {
    TEST("Store replay honours speed multiplier timing");
    HistoricalDataStore store;
    const int64_t base = 1700000000000000000LL;
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(store.append(make_tick("X", 1.0, 1.1, 1.05, 1,
                                           base + i * 20000000LL)));   // 20ms gaps
    }
    // 20x speed: 4 gaps x 20ms / 20 = 4ms expected, well under 1s.
    const int64_t start = TimestampSource::now_monotonic_ns();
    const uint64_t delivered = store.replay("X", [](const Tick &) {}, 20.0);
    const int64_t elapsed = TimestampSource::now_monotonic_ns() - start;
    ASSERT_EQ(delivered, static_cast<uint64_t>(5));
    ASSERT_TRUE(elapsed >= 3000000LL);          // at least ~3ms of sleeping
    ASSERT_TRUE(elapsed < 1000000000LL);        // far less than real-time
    TEST_END("Store replay honours speed multiplier timing");
}

// ============================================================================
// Cache Tests
// ============================================================================

void test_cache_set_get() {
    TEST("Cache latest-value set/get");
    MarketDataCache cache;
    Tick out;
    ASSERT_FALSE(cache.get("AAPL", out));

    cache.update(make_tick("AAPL", 187.50, 187.55, 187.52, 100, 1000));
    cache.update(make_tick("MSFT", 300.00, 300.10, 300.05, 200, 1000));
    ASSERT_TRUE(cache.get("AAPL", out));
    ASSERT_NEAR(out.bid, 187.50, 1e-12);

    // Update overwrites the latest value.
    cache.update(make_tick("AAPL", 188.00, 188.05, 188.02, 150, 2000));
    ASSERT_TRUE(cache.get("AAPL", out));
    ASSERT_NEAR(out.bid, 188.00, 1e-12);
    ASSERT_EQ(out.feed_timestamp_ns, 2000);
    ASSERT_EQ(cache.size(), static_cast<size_t>(2));

    OHLCV bar;
    bar.symbol = "AAPL";
    bar.open = 187.0; bar.high = 188.5; bar.low = 186.5; bar.close = 188.0;
    cache.update(bar);
    OHLCV bar_out;
    ASSERT_TRUE(cache.get_bar("AAPL", bar_out));
    ASSERT_NEAR(bar_out.close, 188.0, 1e-12);
    TEST_END("Cache latest-value set/get");
}

void test_cache_snapshot() {
    TEST("Cache snapshot returns all symbols");
    MarketDataCache cache;
    cache.update(make_tick("AAPL", 1, 2, 1.5, 1, 1));
    cache.update(make_tick("MSFT", 3, 4, 3.5, 2, 2));
    cache.update(make_tick("VOD", 5, 6, 5.5, 3, 3));

    const auto snap = cache.snapshot();
    ASSERT_EQ(snap.size(), static_cast<size_t>(3));
    ASSERT_TRUE(snap.count("AAPL") == 1);
    ASSERT_TRUE(snap.count("MSFT") == 1);
    ASSERT_TRUE(snap.count("VOD") == 1);
    ASSERT_NEAR(snap.at("MSFT").last, 3.5, 1e-12);

    const auto syms = cache.symbols();
    ASSERT_EQ(syms.size(), static_cast<size_t>(3));
    ASSERT_EQ(syms[0], std::string("AAPL"));   // sorted

    cache.clear();
    ASSERT_EQ(cache.size(), static_cast<size_t>(0));
    TEST_END("Cache snapshot returns all symbols");
}

void test_cache_thread_safety() {
    TEST("Cache concurrent writers/readers");
    MarketDataCache cache;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < 500; i++) {
                cache.update(make_tick("SYM" + std::to_string(t),
                                       1.0 * i, 2.0 * i, 1.5 * i,
                                       static_cast<uint64_t>(i), i));
                Tick out;
                cache.get("SYM" + std::to_string(t), out);
            }
        });
    }
    for (auto & th : threads) { th.join(); }
    ASSERT_EQ(cache.size(), static_cast<size_t>(4));
    Tick out;
    ASSERT_TRUE(cache.get("SYM2", out));
    ASSERT_EQ(out.feed_timestamp_ns, 499);
    TEST_END("Cache concurrent writers/readers");
}

// ============================================================================
// Pub/Sub Bus Tests
// ============================================================================

void test_bus_subscribe_publish() {
    TEST("Bus subscribe/publish delivers to matching symbol");
    MarketDataBus bus;
    int aapl_hits = 0;
    int msft_hits = 0;
    bus.subscribe("AAPL", [&aapl_hits](const MarketDataMessage & m) {
        if (m.kind == MarketDataKind::TICK) { aapl_hits++; }
    });
    bus.subscribe("MSFT", [&msft_hits](const MarketDataMessage &) { msft_hits++; });

    bus.publish(MarketDataMessage::from_tick(make_tick("AAPL", 1, 2, 1.5, 1, 1)));
    bus.publish(MarketDataMessage::from_tick(make_tick("MSFT", 3, 4, 3.5, 1, 1)));
    bus.publish(MarketDataMessage::from_tick(make_tick("AAPL", 5, 6, 5.5, 1, 2)));

    ASSERT_EQ(aapl_hits, 2);
    ASSERT_EQ(msft_hits, 1);
    ASSERT_EQ(bus.messages_published(), static_cast<uint64_t>(3));
    ASSERT_EQ(bus.messages_delivered(), static_cast<uint64_t>(3));
    ASSERT_EQ(bus.subscriber_count(), static_cast<size_t>(2));
    TEST_END("Bus subscribe/publish delivers to matching symbol");
}

void test_bus_wildcard_fanout() {
    TEST("Bus wildcard subscriber receives everything");
    MarketDataBus bus;
    int all_hits = 0;
    int one_hits = 0;
    bus.subscribe_all([&all_hits](const MarketDataMessage &) { all_hits++; });
    bus.subscribe("AAPL", [&one_hits](const MarketDataMessage &) { one_hits++; });

    bus.publish(MarketDataMessage::from_tick(make_tick("AAPL", 1, 2, 1.5, 1, 1)));
    bus.publish(MarketDataMessage::from_tick(make_tick("TSLA", 1, 2, 1.5, 1, 1)));
    bus.publish(MarketDataMessage::heartbeat(123));

    ASSERT_EQ(all_hits, 3);      // wildcard sees ticks + heartbeat
    ASSERT_EQ(one_hits, 1);
    ASSERT_EQ(bus.messages_delivered(), static_cast<uint64_t>(4));
    TEST_END("Bus wildcard subscriber receives everything");
}

void test_bus_unsubscribe() {
    TEST("Bus unsubscribe stops delivery");
    MarketDataBus bus;
    int hits = 0;
    const uint64_t id = bus.subscribe("AAPL",
        [&hits](const MarketDataMessage &) { hits++; });

    bus.publish(MarketDataMessage::from_tick(make_tick("AAPL", 1, 2, 1.5, 1, 1)));
    ASSERT_EQ(hits, 1);
    ASSERT_EQ(bus.subscriber_count(), static_cast<size_t>(1));

    bus.unsubscribe(id);
    ASSERT_EQ(bus.subscriber_count(), static_cast<size_t>(0));
    bus.publish(MarketDataMessage::from_tick(make_tick("AAPL", 3, 4, 3.5, 1, 2)));
    ASSERT_EQ(hits, 1);                       // no further delivery
    ASSERT_EQ(bus.messages_published(), static_cast<uint64_t>(2));
    ASSERT_EQ(bus.messages_delivered(), static_cast<uint64_t>(1));

    bus.reset_metrics();
    ASSERT_EQ(bus.messages_published(), static_cast<uint64_t>(0));
    ASSERT_EQ(bus.messages_delivered(), static_cast<uint64_t>(0));
    TEST_END("Bus unsubscribe stops delivery");
}

void test_bus_bar_and_book_messages() {
    TEST("Bus fans out OHLCV and order book messages by symbol");
    MarketDataBus bus;
    int hits = 0;
    bus.subscribe("AAPL", [&hits](const MarketDataMessage &) { hits++; });

    OHLCV bar;
    bar.symbol = "AAPL";
    bus.publish(MarketDataMessage::from_bar(bar));

    OrderBookSnapshot book;
    book.symbol = "AAPL";
    book.bids.push_back(OrderBookLevel(100.0, 10.0, 1));
    book.asks.push_back(OrderBookLevel(100.1, 5.0, 2));
    bus.publish(MarketDataMessage::from_book(book));

    OrderBookSnapshot other;
    other.symbol = "MSFT";
    bus.publish(MarketDataMessage::from_book(other));

    ASSERT_EQ(hits, 2);
    ASSERT_EQ(bus.messages_published(), static_cast<uint64_t>(3));
    TEST_END("Bus fans out OHLCV and order book messages by symbol");
}

// ============================================================================
// Integration Test
// ============================================================================

void test_end_to_end_pipeline() {
    TEST("End-to-end: venue record -> normalize -> stamp -> cache/bus/store");
    MarketDataNormalizer norm;
    norm.register_symbol_mapping("AAPL.OQ", "AAPL");
    norm.register_exchange_mapping("NASDAQ", Exchange::NASDAQ);

    HardwareTimestamper timestamper;
    MarketDataCache     cache;
    MarketDataBus       bus;
    HistoricalDataStore store;

    int delivered = 0;
    bus.subscribe("AAPL", [&delivered](const MarketDataMessage &) { delivered++; });

    const int64_t base = TimestampSource::now_wall_ns();
    for (int i = 0; i < 5; i++) {
        Tick tick = norm.normalize_tick("NASDAQ", "AAPL.OQ",
                                        187.50 + i, 187.55 + i, 187.52 + i,
                                        static_cast<uint64_t>(100 * (i + 1)),
                                        base + i * 1000000LL);
        timestamper.stamp(tick);
        cache.update(tick);
        bus.publish(MarketDataMessage::from_tick(tick));
        ASSERT_TRUE(store.append(tick));
    }

    ASSERT_EQ(delivered, 5);
    Tick latest;
    ASSERT_TRUE(cache.get("AAPL", latest));
    ASSERT_NEAR(latest.bid, 191.50, 1e-9);
    ASSERT_TRUE(latest.receive_timestamp_ns >= latest.feed_timestamp_ns);
    ASSERT_EQ(store.tick_count("AAPL"), static_cast<size_t>(5));
    TEST_END("End-to-end: venue record -> normalize -> stamp -> cache/bus/store");
}

// ============================================================================
// Test Runner
// ============================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  Market Data Integration Test Suite" << std::endl;
    std::cout << "  Issue #003 - ggnucash::marketdata" << std::endl;
    std::cout << "========================================" << std::endl;

    std::cout << "\n--- Timestamping ---" << std::endl;
    test_timestamp_monotonic_ordering();
    test_timestamp_wall_clock_epoch();
    test_timestamper_marks_and_latency();
    test_timestamper_latency_edge_cases();

    std::cout << "\n--- FIX Protocol ---" << std::endl;
    test_fix_encode_parse_roundtrip();
    test_fix_checksum_validation();
    test_fix_checksum_failure_detected();
    test_fix_partial_message_waits();
    test_fix_body_length_enforced();
    test_fix_market_data_snapshot();
    test_fix_incremental_refresh_parse();
    test_fix_parse_rejects_non_market_data_entries();
    test_fix_session_sequences();
    test_fix_session_incoming_validation();

    std::cout << "\n--- WebSocket Framing ---" << std::endl;
    test_ws_text_roundtrip();
    test_ws_masked_frame_roundtrip();
    test_ws_16bit_length();
    test_ws_64bit_length();
    test_ws_control_frames();
    test_ws_streaming_decoder_partial();
    test_ws_streaming_decoder_back_to_back();

    std::cout << "\n--- HTTP Framing ---" << std::endl;
    test_http_request_builder();
    test_http_request_builder_body();
    test_http_response_parse();
    test_http_response_parse_split_feed();
    test_http_response_header_only();

    std::cout << "\n--- Normalizer ---" << std::endl;
    test_normalizer_symbol_mapping();
    test_normalizer_price_scaling();
    test_normalizer_bar();
    test_exchange_enum_strings();

    std::cout << "\n--- Historical Data Store ---" << std::endl;
    test_store_append_and_ordering();
    test_store_point_and_range_query();
    test_store_save_load_roundtrip();
    test_store_run_length_encoding();
    test_store_load_rejects_bad_file();
    test_store_replay_ordering();
    test_store_replay_speed_multiplier();

    std::cout << "\n--- Cache ---" << std::endl;
    test_cache_set_get();
    test_cache_snapshot();
    test_cache_thread_safety();

    std::cout << "\n--- Pub/Sub Bus ---" << std::endl;
    test_bus_subscribe_publish();
    test_bus_wildcard_fanout();
    test_bus_unsubscribe();
    test_bus_bar_and_book_messages();

    std::cout << "\n--- Integration ---" << std::endl;
    test_end_to_end_pipeline();

    std::cout << "\n============================================" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
    std::cout << "  Total:   " << (tests_passed + tests_failed) << " tests" << std::endl;
    std::cout << "============================================" << std::endl;

    return tests_failed == 0 ? 0 : 1;
}
