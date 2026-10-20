#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "netignite/Mac.h"

/**
 * NetIgnite agent protocol v2 (the server side is `server/lib/agents/protocol.ts`).
 *
 * One WebSocket to `<server>/ws/agent`, authenticated with
 * `Authorization: Bearer netignite_agent_<token_id>:<secret>`. JSON text frames:
 *
 *   {"t":"req","id":7,"cmd":"WAKE","p":{...}}       server → agent
 *   {"t":"res","id":7,"ok":true,"p":{...}}           agent → server, same id
 *   {"t":"res","id":7,"ok":false,"err":"..."}
 *   {"t":"evt","cmd":"HELLO","p":{...}}              agent → server, first frame
 */
namespace netignite::protocol {

constexpr int VERSION = 2;
constexpr const char* AGENT_KIND = "esp32";

/** Capabilities this firmware announces in HELLO. */
constexpr const char* CAPABILITIES[] = {"wake", "status.link_local", "status.ip", "status.host", "status.tcp"};

enum class FrameType { Invalid, Request, Response, Event };

struct Frame {
	FrameType type = FrameType::Invalid;
	uint32_t id = 0;
	std::string cmd;
	/** Points into the JsonDocument passed to parseFrame. */
	JsonObjectConst payload;
};

/** Parses a frame from the server. Returns false (and an error text) for invalid frames. */
bool parseFrame(std::string_view text, JsonDocument& doc, Frame& out, std::string& error);

std::string encodeResponse(uint32_t id, JsonObjectConst payload);
std::string encodeEmptyResponse(uint32_t id);
std::string encodeError(uint32_t id, std::string_view error);
std::string encodeEvent(std::string_view cmd, JsonObjectConst payload);

struct WakeRequest {
	MacAddress mac{};
	uint16_t port = 9;
};

bool parseWakeRequest(JsonObjectConst payload, WakeRequest& out, std::string& error);

struct StatusRequest {
	enum class Target { LinkLocal, Ip, Host };
	Target target = Target::Ip;
	MacAddress mac{};
	/** IP address (zone suffix like `%eth0` removed) or host name. */
	std::string address;
	bool icmp = true;
	/** 0 = no TCP probe. */
	uint16_t tcpPort = 0;
};

bool parseStatusRequest(JsonObjectConst payload, StatusRequest& out, std::string& error);

}  // namespace netignite::protocol
