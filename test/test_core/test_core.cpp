// Host-side unit tests for lib/netignite_core: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "netignite/CommandLine.h"
#include "netignite/Hex.h"
#include "netignite/Improv.h"
#include "netignite/LogRing.h"
#include "netignite/Mac.h"
#include "netignite/MagicPacket.h"
#include "netignite/Protocol.h"
#include "netignite/SerialDemux.h"
#include "netignite/ServerUrl.h"
#include "netignite/Validation.h"

using namespace netignite;

void setUp() {}
void tearDown() {}

// --- MAC addresses -----------------------------------------------------------------------------

void test_parse_mac_formats() {
	MacAddress mac{};
	const MacAddress expected = {0xaa, 0xbb, 0xcc, 0x01, 0x02, 0xff};

	TEST_ASSERT_TRUE(parseMac("AA:BB:CC:01:02:FF", mac));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), mac.data(), 6);
	TEST_ASSERT_TRUE(parseMac("aa-bb-cc-01-02-ff", mac));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), mac.data(), 6);
	TEST_ASSERT_TRUE(parseMac("aabbcc0102FF", mac));
	TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), mac.data(), 6);
}

void test_parse_mac_rejects_invalid() {
	MacAddress mac{};
	TEST_ASSERT_FALSE(parseMac("", mac));
	TEST_ASSERT_FALSE(parseMac("AA:BB:CC:DD:EE", mac));
	TEST_ASSERT_FALSE(parseMac("AA:BB:CC:DD:EE:GG", mac));
	TEST_ASSERT_FALSE(parseMac("AA:BB-CC:DD:EE:FF", mac));
	TEST_ASSERT_FALSE(parseMac("AABB.CCDD.EEFF", mac));
	TEST_ASSERT_FALSE(parseMac("AA:BB:CC:DD:EE:FF:", mac));
}

void test_format_mac() {
	TEST_ASSERT_EQUAL_STRING("0A:1B:2C:3D:4E:5F", formatMac({0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f}).c_str());
}

void test_link_local_from_mac() {
	// 52:74:f2:b1:a8:7f -> fe80::5074:f2ff:feb1:a87f (RFC 4291 modified EUI-64)
	const IPv6Bytes address = linkLocalFromMac({0x52, 0x74, 0xf2, 0xb1, 0xa8, 0x7f});
	TEST_ASSERT_EQUAL_STRING("fe80::5074:f2ff:feb1:a87f", formatIPv6(address).c_str());
}

void test_format_ipv6_compression() {
	IPv6Bytes loopback{};
	loopback[15] = 1;
	TEST_ASSERT_EQUAL_STRING("::1", formatIPv6(loopback).c_str());

	const IPv6Bytes address = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1};
	TEST_ASSERT_EQUAL_STRING("2001:db8::1:0:0:1", formatIPv6(address).c_str());
}

// --- Server URLs -------------------------------------------------------------------------------

void test_server_url_https_default() {
	auto url = parseServerUrl("https://app.netignite.dev");
	TEST_ASSERT_TRUE(url.has_value());
	TEST_ASSERT_TRUE(url->secure);
	TEST_ASSERT_EQUAL_STRING("app.netignite.dev", url->host.c_str());
	TEST_ASSERT_EQUAL_UINT16(443, url->port);
	TEST_ASSERT_EQUAL_STRING("https://app.netignite.dev/api/v1/agents/enroll",
	                         url->url("/api/v1/agents/enroll").c_str());
	TEST_ASSERT_EQUAL_STRING("/ws/agent", url->path("/ws/agent").c_str());
}

void test_server_url_http_port_and_path() {
	auto url = parseServerUrl("  HTTP://192.168.1.10:18232/netignite/  ");
	TEST_ASSERT_TRUE(url.has_value());
	TEST_ASSERT_FALSE(url->secure);
	TEST_ASSERT_EQUAL_STRING("192.168.1.10", url->host.c_str());
	TEST_ASSERT_EQUAL_UINT16(18232, url->port);
	TEST_ASSERT_EQUAL_STRING("/netignite/ws/agent", url->path("/ws/agent").c_str());
	TEST_ASSERT_EQUAL_STRING("http://192.168.1.10:18232", url->origin().c_str());
}

void test_server_url_without_scheme_and_ipv6() {
	auto url = parseServerUrl("netignite.example.com");
	TEST_ASSERT_TRUE(url.has_value());
	TEST_ASSERT_TRUE(url->secure);

	auto v6 = parseServerUrl("http://[fd00::10]:18232");
	TEST_ASSERT_TRUE(v6.has_value());
	TEST_ASSERT_EQUAL_STRING("fd00::10", v6->host.c_str());
	TEST_ASSERT_EQUAL_UINT16(18232, v6->port);
	TEST_ASSERT_EQUAL_STRING("http://[fd00::10]:18232", v6->origin().c_str());
}

void test_server_url_rejects_invalid() {
	TEST_ASSERT_FALSE(parseServerUrl("").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("ftp://example.com").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://example.com:").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://example.com:0").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://example.com:70000").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://user@example.com").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://example.com/?x=1").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("https://exa mple.com").has_value());
	TEST_ASSERT_FALSE(parseServerUrl("http://[::1]:").has_value());
}

// --- Wake-on-LAN --------------------------------------------------------------------------------

void test_magic_packet() {
	const MacAddress mac = {1, 2, 3, 4, 5, 6};
	const auto packet = buildMagicPacket(mac);
	for (int i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX8(0xff, packet[i]);
	for (int repeat = 0; repeat < 16; repeat++) {
		TEST_ASSERT_EQUAL_UINT8_ARRAY(mac.data(), packet.data() + 6 + repeat * 6, 6);
	}
}

void test_directed_broadcast() {
	// 192.168.1.32/24 -> 192.168.1.255
	TEST_ASSERT_EQUAL_HEX32(0xc0a801ff, directedBroadcast(0xc0a80120, 0xffffff00));
	// 10.1.2.3/16 -> 10.1.255.255
	TEST_ASSERT_EQUAL_HEX32(0x0a01ffff, directedBroadcast(0x0a010203, 0xffff0000));
}

// --- Improv -------------------------------------------------------------------------------------

uint8_t checksum(const std::vector<uint8_t>& bytes, size_t count) {
	uint8_t sum = 0;
	for (size_t i = 0; i < count; i++) sum = static_cast<uint8_t>(sum + bytes[i]);
	return sum;
}

void test_improv_state_packet() {
	const auto packet = improv::buildStatePacket(improv::State::Authorized);
	const std::vector<uint8_t> expected = {'I', 'M', 'P', 'R', 'O', 'V', 1, 0x01, 1, 0x02};
	TEST_ASSERT_EQUAL_size_t(expected.size() + 2, packet.size());
	TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), packet.data(), expected.size());
	TEST_ASSERT_EQUAL_HEX8(checksum(packet, expected.size()), packet[expected.size()]);
	TEST_ASSERT_EQUAL_HEX8('\n', packet.back());
}

void test_improv_rpc_result() {
	const auto packet =
		improv::buildRpcResult(improv::Command::GetDeviceInfo, {"NetIgnite", "2.0.0", "ESP32", "Office"});
	// data = cmd, total length, then length-prefixed strings
	TEST_ASSERT_EQUAL_HEX8(0x04, packet[7]);
	const size_t dataLength = packet[8];
	TEST_ASSERT_EQUAL_size_t(2 + 4 + 9 + 5 + 5 + 6, dataLength);
	TEST_ASSERT_EQUAL_HEX8(0x03, packet[9]);
	TEST_ASSERT_EQUAL_size_t(dataLength - 2, packet[10]);
	TEST_ASSERT_EQUAL_size_t(9, packet[11]);
	TEST_ASSERT_EQUAL_MEMORY("NetIgnite", packet.data() + 12, 9);
	TEST_ASSERT_EQUAL_HEX8(checksum(packet, 9 + dataLength), packet[9 + dataLength]);
}

void test_improv_parse_wifi_settings() {
	// Example from the spec: SSID "MyWirelessAP", password "mysecurepassword"
	std::vector<uint8_t> data = {0x01, 0x1e, 0x0c};
	for (char c : std::string("MyWirelessAP")) data.push_back(static_cast<uint8_t>(c));
	data.push_back(0x10);
	for (char c : std::string("mysecurepassword")) data.push_back(static_cast<uint8_t>(c));

	improv::RpcCommand command;
	TEST_ASSERT_TRUE(improv::parseRpc(data, command));
	TEST_ASSERT_EQUAL(static_cast<int>(improv::Command::WifiSettings), static_cast<int>(command.command));
	TEST_ASSERT_EQUAL_STRING("MyWirelessAP", command.ssid.c_str());
	TEST_ASSERT_EQUAL_STRING("mysecurepassword", command.password.c_str());
}

void test_improv_parse_rejects_bad_lengths() {
	improv::RpcCommand command;
	TEST_ASSERT_FALSE(improv::parseRpc({0x01}, command));
	TEST_ASSERT_FALSE(improv::parseRpc({0x01, 0x05, 0x01, 'a'}, command));
	// SSID length runs past the end
	TEST_ASSERT_FALSE(improv::parseRpc({0x01, 0x03, 0x09, 'a', 'b'}, command));
	// open network: empty password is fine, empty SSID isn't
	TEST_ASSERT_TRUE(improv::parseRpc({0x01, 0x03, 0x01, 'a', 0x00}, command));
	TEST_ASSERT_EQUAL_STRING("", command.password.c_str());
	TEST_ASSERT_FALSE(improv::parseRpc({0x01, 0x02, 0x00, 0x00}, command));
	// other commands carry no payload
	TEST_ASSERT_TRUE(improv::parseRpc({0x03, 0x00}, command));
	TEST_ASSERT_EQUAL(static_cast<int>(improv::Command::GetDeviceInfo), static_cast<int>(command.command));
}

// --- Serial demultiplexer ----------------------------------------------------------------------

std::vector<SerialDemux::Event> feedAll(SerialDemux& demux, const std::vector<uint8_t>& bytes) {
	std::vector<SerialDemux::Event> events;
	for (uint8_t byte : bytes) {
		auto event = demux.feed(byte);
		if (event.type != SerialDemux::Event::Type::None) events.push_back(event);
	}
	return events;
}

std::vector<uint8_t> bytesOf(const std::string& text) {
	return std::vector<uint8_t>(text.begin(), text.end());
}

void test_demux_text_lines() {
	SerialDemux demux;
	auto events = feedAll(demux, bytesOf("status\r\n  wifi \"My Net\" pw  \n\n"));
	TEST_ASSERT_EQUAL_size_t(2, events.size());
	TEST_ASSERT_EQUAL_STRING("status", events[0].line.c_str());
	TEST_ASSERT_EQUAL_STRING("wifi \"My Net\" pw", events[1].line.c_str());
}

void test_demux_improv_packet_with_newline_in_data() {
	// An RPC packet whose payload contains 0x0A must not be split at it.
	const std::vector<uint8_t> rpc = {0x01, 0x05, 0x01, '\n', 0x02, 'p', 'w'};
	auto packet = improv::buildPacket(improv::PacketType::Rpc, rpc);

	SerialDemux demux;
	std::vector<uint8_t> input = bytesOf("IMPROVE is not a packet\n");
	input.insert(input.end(), packet.begin(), packet.end());
	auto line = bytesOf("help\n");
	input.insert(input.end(), line.begin(), line.end());

	auto events = feedAll(demux, input);
	TEST_ASSERT_EQUAL_size_t(3, events.size());
	TEST_ASSERT_EQUAL(static_cast<int>(SerialDemux::Event::Type::Line), static_cast<int>(events[0].type));
	TEST_ASSERT_EQUAL(static_cast<int>(SerialDemux::Event::Type::ImprovPacket), static_cast<int>(events[1].type));
	TEST_ASSERT_EQUAL_HEX8(0x03, events[1].improvType);
	TEST_ASSERT_EQUAL_size_t(rpc.size(), events[1].improvData.size());
	TEST_ASSERT_EQUAL_UINT8_ARRAY(rpc.data(), events[1].improvData.data(), rpc.size());
	TEST_ASSERT_EQUAL_STRING("help", events[2].line.c_str());

	improv::RpcCommand command;
	TEST_ASSERT_TRUE(improv::parseRpc(events[1].improvData, command));
	TEST_ASSERT_EQUAL_STRING("\n", command.ssid.c_str());
}

void test_demux_bad_checksum() {
	auto packet = improv::buildPacket(improv::PacketType::Rpc, {0x02, 0x00});
	packet[packet.size() - 2] ^= 0xff;
	SerialDemux demux;
	auto events = feedAll(demux, packet);
	TEST_ASSERT_EQUAL_size_t(1, events.size());
	TEST_ASSERT_EQUAL(static_cast<int>(SerialDemux::Event::Type::ImprovBadChecksum),
	                  static_cast<int>(events[0].type));
}

void test_demux_overlong_line_is_dropped() {
	SerialDemux demux;
	std::string longLine(SerialDemux::MAX_LINE + 10, 'x');
	auto events = feedAll(demux, bytesOf(longLine + "\nreboot\n"));
	TEST_ASSERT_EQUAL_size_t(1, events.size());
	TEST_ASSERT_EQUAL_STRING("reboot", events[0].line.c_str());
}

// --- Protocol ----------------------------------------------------------------------------------

void test_parse_request_frame() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	TEST_ASSERT_TRUE(protocol::parseFrame(
		R"({"t":"req","id":4294967295,"cmd":"WAKE","p":{"mac":"AA:BB:CC:DD:EE:FF","port":9}})", doc, frame,
		error));
	TEST_ASSERT_EQUAL(static_cast<int>(protocol::FrameType::Request), static_cast<int>(frame.type));
	TEST_ASSERT_EQUAL_UINT32(4294967295u, frame.id);
	TEST_ASSERT_EQUAL_STRING("WAKE", frame.cmd.c_str());

	protocol::WakeRequest wake;
	TEST_ASSERT_TRUE(protocol::parseWakeRequest(frame.payload, wake, error));
	TEST_ASSERT_EQUAL_UINT16(9, wake.port);
	TEST_ASSERT_EQUAL_HEX8(0xff, wake.mac[5]);
}

void test_parse_frame_rejects_invalid() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	TEST_ASSERT_FALSE(protocol::parseFrame("not json", doc, frame, error));
	TEST_ASSERT_FALSE(protocol::parseFrame("[1,2]", doc, frame, error));
	TEST_ASSERT_FALSE(protocol::parseFrame(R"({"t":"req","cmd":"PING"})", doc, frame, error));
	TEST_ASSERT_FALSE(protocol::parseFrame(R"({"t":"req","id":-1,"cmd":"PING"})", doc, frame, error));
	TEST_ASSERT_FALSE(protocol::parseFrame(R"({"t":"req","id":1})", doc, frame, error));
	TEST_ASSERT_FALSE(protocol::parseFrame(R"({"t":"xyz","id":1,"cmd":"PING"})", doc, frame, error));
}

void test_parse_wake_rejects_invalid() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::WakeRequest wake;
	protocol::parseFrame(R"({"t":"req","id":1,"cmd":"WAKE","p":{"mac":"nope","port":9}})", doc, frame, error);
	TEST_ASSERT_FALSE(protocol::parseWakeRequest(frame.payload, wake, error));
	protocol::parseFrame(R"({"t":"req","id":1,"cmd":"WAKE","p":{"mac":"AA:BB:CC:DD:EE:FF","port":0}})", doc,
	                     frame, error);
	TEST_ASSERT_FALSE(protocol::parseWakeRequest(frame.payload, wake, error));
	protocol::parseFrame(R"({"t":"req","id":1,"cmd":"WAKE","p":{"mac":"AA:BB:CC:DD:EE:FF","port":"9"}})", doc,
	                     frame, error);
	TEST_ASSERT_FALSE(protocol::parseWakeRequest(frame.payload, wake, error));
}

void test_parse_status_requests() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::StatusRequest status;

	protocol::parseFrame(
		R"({"t":"req","id":2,"cmd":"STATUS","p":{"target":{"type":"link_local","mac":"52:74:F2:B1:A8:7F"},"icmp":true}})",
		doc, frame, error);
	TEST_ASSERT_TRUE(protocol::parseStatusRequest(frame.payload, status, error));
	TEST_ASSERT_EQUAL(static_cast<int>(protocol::StatusRequest::Target::LinkLocal), static_cast<int>(status.target));
	TEST_ASSERT_EQUAL_UINT16(0, status.tcpPort);

	protocol::parseFrame(
		R"({"t":"req","id":3,"cmd":"STATUS","p":{"target":{"type":"ip","address":"fe80::1%eth0"},"icmp":true,"tcp_port":445}})",
		doc, frame, error);
	TEST_ASSERT_TRUE(protocol::parseStatusRequest(frame.payload, status, error));
	TEST_ASSERT_EQUAL_STRING("fe80::1", status.address.c_str());
	TEST_ASSERT_EQUAL_UINT16(445, status.tcpPort);

	protocol::parseFrame(
		R"({"t":"req","id":4,"cmd":"STATUS","p":{"target":{"type":"host","host":"nas.local"},"icmp":false,"tcp_port":22}})",
		doc, frame, error);
	TEST_ASSERT_TRUE(protocol::parseStatusRequest(frame.payload, status, error));
	TEST_ASSERT_EQUAL(static_cast<int>(protocol::StatusRequest::Target::Host), static_cast<int>(status.target));
	TEST_ASSERT_FALSE(status.icmp);

	protocol::parseFrame(
		R"({"t":"req","id":5,"cmd":"STATUS","p":{"target":{"type":"ip","address":"10.0.0.1"},"icmp":false}})", doc,
		frame, error);
	TEST_ASSERT_FALSE(protocol::parseStatusRequest(frame.payload, status, error));
	protocol::parseFrame(R"({"t":"req","id":6,"cmd":"STATUS","p":{"target":{"type":"carrier-pigeon"}}})", doc,
	                     frame, error);
	TEST_ASSERT_FALSE(protocol::parseStatusRequest(frame.payload, status, error));
}

void test_encode_frames() {
	JsonDocument payload;
	payload["online"] = true;
	TEST_ASSERT_EQUAL_STRING(R"({"t":"res","id":7,"ok":true,"p":{"online":true}})",
	                         protocol::encodeResponse(7, payload.as<JsonObjectConst>()).c_str());
	TEST_ASSERT_EQUAL_STRING(R"({"t":"res","id":8,"ok":true,"p":{}})", protocol::encodeEmptyResponse(8).c_str());
	TEST_ASSERT_EQUAL_STRING(R"({"t":"res","id":9,"ok":false,"err":"Unknown command"})",
	                         protocol::encodeError(9, "Unknown command").c_str());

	JsonDocument hello;
	hello["protocol"] = protocol::VERSION;
	hello["kind"] = protocol::AGENT_KIND;
	TEST_ASSERT_EQUAL_STRING(R"({"t":"evt","cmd":"HELLO","p":{"protocol":2,"kind":"esp32"}})",
	                         protocol::encodeEvent("HELLO", hello.as<JsonObjectConst>()).c_str());
}

// --- Console command lines ---------------------------------------------------------------------

void test_split_command_line() {
	std::vector<std::string> args;
	TEST_ASSERT_TRUE(splitCommandLine(R"(wifi "My Home Network" "pa\"ss word")", args));
	TEST_ASSERT_EQUAL_size_t(3, args.size());
	TEST_ASSERT_EQUAL_STRING("wifi", args[0].c_str());
	TEST_ASSERT_EQUAL_STRING("My Home Network", args[1].c_str());
	TEST_ASSERT_EQUAL_STRING("pa\"ss word", args[2].c_str());

	TEST_ASSERT_TRUE(splitCommandLine("  ip   static 192.168.1.5\t192.168.1.1 255.255.255.0 ", args));
	TEST_ASSERT_EQUAL_size_t(5, args.size());
	TEST_ASSERT_EQUAL_STRING("255.255.255.0", args[4].c_str());

	// An empty quoted argument (open network) is kept.
	TEST_ASSERT_TRUE(splitCommandLine(R"(wifi Guest "")", args));
	TEST_ASSERT_EQUAL_size_t(3, args.size());
	TEST_ASSERT_EQUAL_STRING("", args[2].c_str());

	TEST_ASSERT_FALSE(splitCommandLine(R"(wifi "unterminated)", args));
}

void test_hex() {
	const uint8_t bytes[] = {0x00, 0x0f, 0xa5, 0xff};
	TEST_ASSERT_EQUAL_STRING("000fa5ff", toHex(bytes, sizeof(bytes)).c_str());

	std::vector<uint8_t> decoded;
	TEST_ASSERT_TRUE(fromHex("000FA5ff", decoded));
	TEST_ASSERT_EQUAL_size_t(4, decoded.size());
	TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes, decoded.data(), 4);
	TEST_ASSERT_TRUE(fromHex("", decoded));
	TEST_ASSERT_EQUAL_size_t(0, decoded.size());
	TEST_ASSERT_FALSE(fromHex("abc", decoded));
	TEST_ASSERT_FALSE(fromHex("zz", decoded));
	TEST_ASSERT_EQUAL_size_t(0, decoded.size());
}

// --- Validation --------------------------------------------------------------------------------

void test_validation() {
	TEST_ASSERT_TRUE(isValidHostname("Garage-ESP32"));
	TEST_ASSERT_TRUE(isValidHostname("a"));
	TEST_ASSERT_FALSE(isValidHostname(""));
	TEST_ASSERT_FALSE(isValidHostname("-a"));
	TEST_ASSERT_FALSE(isValidHostname("a-"));
	TEST_ASSERT_FALSE(isValidHostname("Garage ESP32"));
	TEST_ASSERT_FALSE(isValidHostname(std::string(64, 'a')));

	TEST_ASSERT_TRUE(isValidIpv4("192.168.1.5"));
	TEST_ASSERT_TRUE(isValidIpv4("0.0.0.0"));
	TEST_ASSERT_TRUE(isValidIpv4("255.255.255.255"));
	TEST_ASSERT_FALSE(isValidIpv4("256.1.1.1"));
	TEST_ASSERT_FALSE(isValidIpv4("1.2.3"));
	TEST_ASSERT_FALSE(isValidIpv4("1.2.3.4.5"));
	TEST_ASSERT_FALSE(isValidIpv4("01.2.3.4"));
	TEST_ASSERT_FALSE(isValidIpv4("1..3.4"));
	TEST_ASSERT_FALSE(isValidIpv4("1.2.3.4."));
	TEST_ASSERT_FALSE(isValidIpv4("a.b.c.d"));
	TEST_ASSERT_FALSE(isValidIpv4(""));
}

// --- Log ring ----------------------------------------------------------------------------------

void test_log_ring_reads_from_a_cursor() {
	LogRing ring(1000);
	ring.push("one");
	ring.push("two");
	ring.push("three");
	TEST_ASSERT_EQUAL_UINT32(3, ring.nextSeq());

	std::vector<std::string> lines;
	auto result = ring.read(1, 1000, lines);
	TEST_ASSERT_EQUAL_size_t(2, lines.size());
	TEST_ASSERT_EQUAL_STRING("two", lines[0].c_str());
	TEST_ASSERT_EQUAL_UINT32(3, result.next);
	TEST_ASSERT_EQUAL_UINT32(0, result.dropped);

	// Nothing new: empty read, cursor stays.
	lines.clear();
	result = ring.read(3, 1000, lines);
	TEST_ASSERT_EQUAL_size_t(0, lines.size());
	TEST_ASSERT_EQUAL_UINT32(3, result.next);
}

void test_log_ring_drops_old_lines_and_reports_them() {
	LogRing ring(10);
	ring.push("aaaa");  // seq 0
	ring.push("bbbb");  // seq 1
	ring.push("cccc");  // seq 2: drops seq 0
	TEST_ASSERT_EQUAL_size_t(8, ring.bytes());

	std::vector<std::string> lines;
	const auto result = ring.read(0, 1000, lines);
	TEST_ASSERT_EQUAL_UINT32(1, result.dropped);
	TEST_ASSERT_EQUAL_size_t(2, lines.size());
	TEST_ASSERT_EQUAL_STRING("bbbb", lines[0].c_str());
	TEST_ASSERT_EQUAL_UINT32(3, result.next);
}

void test_log_ring_limits_bytes_and_line_length() {
	LogRing ring(10000);
	ring.push(std::string(500, 'x'));
	TEST_ASSERT_EQUAL_size_t(LogRing::MAX_LINE, ring.bytes());
	ring.push("abc");
	ring.push("def");

	std::vector<std::string> lines;
	auto result = ring.read(0, 245, lines);
	// The first line is always returned, even if it alone exceeds the budget.
	TEST_ASSERT_EQUAL_size_t(2, lines.size());
	TEST_ASSERT_EQUAL_UINT32(2, result.next);
	lines.clear();
	result = ring.read(result.next, 245, lines);
	TEST_ASSERT_EQUAL_size_t(1, lines.size());
	TEST_ASSERT_EQUAL_STRING("def", lines[0].c_str());

	// A cursor from the future (after a reboot) reads from the end.
	lines.clear();
	result = ring.read(1000, 245, lines);
	TEST_ASSERT_EQUAL_size_t(0, lines.size());
	TEST_ASSERT_EQUAL_UINT32(3, result.next);
}

// --- OTA, config and log requests --------------------------------------------------------------

static const char* const SHA =
	"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void test_parse_ota_request() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::OtaRequest ota;

	std::string text = std::string(R"({"t":"req","id":1,"cmd":"OTA_BEGIN","p":{"version":"2.1.0-beta.1",)") +
	                   R"("path":"/api/v1/firmware/2.1.0-beta.1/files/firmware.bin","size":1260048,"sha256":")" + SHA +
	                   R"("}})";
	TEST_ASSERT_TRUE(protocol::parseFrame(text, doc, frame, error));
	TEST_ASSERT_TRUE(protocol::parseOtaRequest(frame.payload, ota, error));
	TEST_ASSERT_EQUAL_STRING("2.1.0-beta.1", ota.version.c_str());
	TEST_ASSERT_EQUAL_STRING("/api/v1/firmware/2.1.0-beta.1/files/firmware.bin", ota.path.c_str());
	TEST_ASSERT_EQUAL_UINT32(1260048, ota.size);
	TEST_ASSERT_EQUAL_HEX8(0x01, ota.sha256[0]);
	TEST_ASSERT_EQUAL_HEX8(0xef, ota.sha256[31]);
	TEST_ASSERT_EQUAL_size_t(0, ota.signature.size());
}

void test_parse_ota_request_rejects_invalid() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::OtaRequest ota;

	const std::string valid = std::string(R"("version":"2.1.0","size":1000,"sha256":")") + SHA + "\"";
	auto parse = [&](const std::string& payload) {
		protocol::parseFrame(R"({"t":"req","id":1,"cmd":"OTA_BEGIN","p":{)" + payload + "}}", doc, frame, error);
		return protocol::parseOtaRequest(frame.payload, ota, error);
	};

	TEST_ASSERT_TRUE(parse(valid + R"(,"path":"/api/v1/firmware/2.1.0/files/firmware.bin")"));
	TEST_ASSERT_EQUAL_size_t(0, ota.signature.size());
	TEST_ASSERT_TRUE(parse(valid + R"(,"path":"/f.bin","signature":")" + std::string(142, 'a') + "\""));
	TEST_ASSERT_EQUAL_size_t(71, ota.signature.size());

	TEST_ASSERT_FALSE(parse(valid + R"(,"path":"https://evil.example/f.bin")"));
	TEST_ASSERT_FALSE(parse(valid + R"(,"path":"//evil.example/f.bin")"));
	TEST_ASSERT_FALSE(parse(valid + R"(,"path":"/api/../../f.bin")"));
	TEST_ASSERT_FALSE(parse(valid + R"(,"path":"/f.bin?x=1")"));
	TEST_ASSERT_FALSE(parse(valid + R"(,"path":"/f.bin","signature":"xyz")"));
	TEST_ASSERT_FALSE(parse(std::string(R"("version":"2.1.0","size":2000000,"sha256":")") + SHA + R"(","path":"/f")"));
	TEST_ASSERT_FALSE(parse(std::string(R"("version":"2.1.0","size":0,"sha256":")") + SHA + R"(","path":"/f")"));
	TEST_ASSERT_FALSE(parse(R"("version":"2.1.0","size":10,"sha256":"abcd","path":"/f")"));
	TEST_ASSERT_FALSE(parse(std::string(R"("version":"2 1","size":10,"sha256":")") + SHA + R"(","path":"/f")"));
}

void test_parse_config_request() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::ConfigRequest config;
	auto parse = [&](const std::string& payload) {
		protocol::parseFrame(R"({"t":"req","id":1,"cmd":"CONFIG_SET","p":)" + payload + "}", doc, frame, error);
		return protocol::parseConfigRequest(frame.payload, config, error);
	};

	TEST_ASSERT_TRUE(parse(R"({"server_url":"https://netignite.example/","name":"Garage-ESP32","log_level":"debug"})"));
	TEST_ASSERT_EQUAL_STRING("https://netignite.example", config.serverUrl->c_str());
	TEST_ASSERT_EQUAL_STRING("Garage-ESP32", config.name->c_str());
	TEST_ASSERT_EQUAL_STRING("debug", config.logLevel->c_str());
	TEST_ASSERT_FALSE(config.ip.has_value());

	TEST_ASSERT_TRUE(parse(R"({"ip":{"mode":"static","address":"192.168.1.50","gateway":"192.168.1.1","netmask":"255.255.255.0","dns1":"1.1.1.1"}})"));
	TEST_ASSERT_TRUE(config.ip->isStatic);
	TEST_ASSERT_EQUAL_STRING("192.168.1.50", config.ip->address.c_str());
	TEST_ASSERT_EQUAL_STRING("1.1.1.1", config.ip->dns1.c_str());
	TEST_ASSERT_EQUAL_STRING("", config.ip->dns2.c_str());

	TEST_ASSERT_TRUE(parse(R"({"ip":{"mode":"dhcp"}})"));
	TEST_ASSERT_FALSE(config.ip->isStatic);

	TEST_ASSERT_FALSE(parse(R"({})"));
	TEST_ASSERT_FALSE(parse(R"({"server_url":"ftp://x"})"));
	TEST_ASSERT_FALSE(parse(R"({"name":"Garage ESP32"})"));
	TEST_ASSERT_FALSE(parse(R"({"log_level":"verbose"})"));
	TEST_ASSERT_FALSE(parse(R"({"ip":{"mode":"static","address":"192.168.1.50","gateway":"192.168.1.1"}})"));
	TEST_ASSERT_FALSE(parse(R"({"ip":{"mode":"static","address":"192.168.1.500","gateway":"192.168.1.1","netmask":"255.255.255.0"}})"));
	TEST_ASSERT_FALSE(parse(R"({"ip":{"mode":"magic"}})"));
}

void test_parse_log_subscribe() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	bool enabled = false;
	protocol::parseFrame(R"({"t":"req","id":1,"cmd":"LOG_SUBSCRIBE","p":{"enabled":true}})", doc, frame, error);
	TEST_ASSERT_TRUE(protocol::parseLogSubscribe(frame.payload, enabled, error));
	TEST_ASSERT_TRUE(enabled);
	protocol::parseFrame(R"({"t":"req","id":1,"cmd":"LOG_SUBSCRIBE","p":{"enabled":"yes"}})", doc, frame, error);
	TEST_ASSERT_FALSE(protocol::parseLogSubscribe(frame.payload, enabled, error));
}

// --- SSH ---------------------------------------------------------------------------------------

void test_base64() {
	const auto b64 = [](const char* text, bool pad) {
		return toBase64(reinterpret_cast<const uint8_t*>(text), strlen(text), pad);
	};
	TEST_ASSERT_EQUAL_STRING("", b64("", true).c_str());
	TEST_ASSERT_EQUAL_STRING("Zg==", b64("f", true).c_str());
	TEST_ASSERT_EQUAL_STRING("Zm8=", b64("fo", true).c_str());
	TEST_ASSERT_EQUAL_STRING("Zm9v", b64("foo", true).c_str());
	TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", b64("foobar", true).c_str());
	TEST_ASSERT_EQUAL_STRING("Zg", b64("f", false).c_str());
	TEST_ASSERT_EQUAL_STRING("Zm8", b64("fo", false).c_str());

	const uint8_t zeros[32] = {};
	TEST_ASSERT_EQUAL_STRING(("SHA256:" + std::string(43, 'A')).c_str(), protocol::sshFingerprint(zeros).c_str());
}

void test_parse_ssh_requests() {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;
	protocol::SshRequest ssh;
	const std::string key = "SHA256:" + std::string(43, 'b');
	auto parse = [&](const std::string& payload, bool power) {
		protocol::parseFrame(R"({"t":"req","id":1,"cmd":"SSH","p":)" + payload + "}", doc, frame, error);
		return protocol::parseSshRequest(frame.payload, power, ssh, error);
	};

	// No credentials: the agent logs in with its own keys. Extra fields (old `auth`) are ignored.
	TEST_ASSERT_TRUE(parse(R"({"host":"192.168.1.20","port":22,"username":"netignite","host_key":null})", false));
	TEST_ASSERT_EQUAL_STRING("192.168.1.20", ssh.host.c_str());
	TEST_ASSERT_EQUAL_STRING("netignite", ssh.username.c_str());
	TEST_ASSERT_TRUE(ssh.hostKey.empty());
	TEST_ASSERT_TRUE(parse(R"({"host":"h","port":22,"username":"a","auth":{"type":"password","password":"pw"}})", false));

	const std::string power = R"({"host":"nas.local","port":2222,"username":"DOMAIN\\admin","host_key":")" + key +
	                          R"(","action":"reboot","os":"posix","sudo":true})";
	TEST_ASSERT_TRUE(parse(power, true));
	TEST_ASSERT_EQUAL_UINT16(2222, ssh.port);
	TEST_ASSERT_EQUAL_STRING("DOMAIN\\admin", ssh.username.c_str());
	TEST_ASSERT_EQUAL_STRING(key.c_str(), ssh.hostKey.c_str());
	TEST_ASSERT_EQUAL(static_cast<int>(protocol::SshRequest::Action::Reboot), static_cast<int>(ssh.action));
	TEST_ASSERT_EQUAL_STRING("sudo -n shutdown -r now", protocol::sshCommand(ssh).c_str());

	ssh.sudo = false;
	ssh.action = protocol::SshRequest::Action::Shutdown;
	TEST_ASSERT_EQUAL_STRING("shutdown -h now", protocol::sshCommand(ssh).c_str());
	ssh.windows = true;
	TEST_ASSERT_EQUAL_STRING("shutdown /s /t 0", protocol::sshCommand(ssh).c_str());
	ssh.action = protocol::SshRequest::Action::Reboot;
	TEST_ASSERT_EQUAL_STRING("shutdown /r /t 0", protocol::sshCommand(ssh).c_str());

	// SSH_POWER needs a trusted host key, a known action and OS.
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a","host_key":null,"action":"reboot","os":"posix","sudo":false})", true));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a","host_key":")" + key + R"(","action":"rm -rf /","os":"posix","sudo":false})", true));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a","host_key":")" + key + R"(","action":"reboot","os":"amiga","sudo":false})", true));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a","host_key":")" + key + R"(","action":"reboot","os":"posix"})", true));

	TEST_ASSERT_FALSE(parse(R"({"host":"","port":22,"username":"a"})", false));
	TEST_ASSERT_FALSE(parse(R"({"host":"a b","port":22,"username":"a"})", false));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":0,"username":"a"})", false));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a b"})", false));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":""})", false));
	TEST_ASSERT_FALSE(parse(R"({"host":"h","port":22,"username":"a","host_key":"MD5:aa"})", false));
}

int main(int, char**) {
	UNITY_BEGIN();
	RUN_TEST(test_parse_mac_formats);
	RUN_TEST(test_parse_mac_rejects_invalid);
	RUN_TEST(test_format_mac);
	RUN_TEST(test_link_local_from_mac);
	RUN_TEST(test_format_ipv6_compression);
	RUN_TEST(test_server_url_https_default);
	RUN_TEST(test_server_url_http_port_and_path);
	RUN_TEST(test_server_url_without_scheme_and_ipv6);
	RUN_TEST(test_server_url_rejects_invalid);
	RUN_TEST(test_magic_packet);
	RUN_TEST(test_directed_broadcast);
	RUN_TEST(test_improv_state_packet);
	RUN_TEST(test_improv_rpc_result);
	RUN_TEST(test_improv_parse_wifi_settings);
	RUN_TEST(test_improv_parse_rejects_bad_lengths);
	RUN_TEST(test_demux_text_lines);
	RUN_TEST(test_demux_improv_packet_with_newline_in_data);
	RUN_TEST(test_demux_bad_checksum);
	RUN_TEST(test_demux_overlong_line_is_dropped);
	RUN_TEST(test_parse_request_frame);
	RUN_TEST(test_parse_frame_rejects_invalid);
	RUN_TEST(test_parse_wake_rejects_invalid);
	RUN_TEST(test_parse_status_requests);
	RUN_TEST(test_encode_frames);
	RUN_TEST(test_split_command_line);
	RUN_TEST(test_hex);
	RUN_TEST(test_validation);
	RUN_TEST(test_log_ring_reads_from_a_cursor);
	RUN_TEST(test_log_ring_drops_old_lines_and_reports_them);
	RUN_TEST(test_log_ring_limits_bytes_and_line_length);
	RUN_TEST(test_parse_ota_request);
	RUN_TEST(test_parse_ota_request_rejects_invalid);
	RUN_TEST(test_parse_config_request);
	RUN_TEST(test_parse_log_subscribe);
	RUN_TEST(test_base64);
	RUN_TEST(test_parse_ssh_requests);
	return UNITY_END();
}
