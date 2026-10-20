#include "netignite/Protocol.h"

namespace netignite::protocol {

namespace {

bool readPort(JsonVariantConst value, uint16_t& out, bool allowZero) {
	if (!value.is<long long>()) return false;
	const long long port = value.as<long long>();
	if (port < (allowZero ? 0 : 1) || port > 65535) return false;
	out = static_cast<uint16_t>(port);
	return true;
}

std::string serialize(const JsonDocument& doc) {
	std::string out;
	serializeJson(doc, out);
	return out;
}

}  // namespace

bool parseFrame(std::string_view text, JsonDocument& doc, Frame& out, std::string& error) {
	out = Frame{};

	const DeserializationError result = deserializeJson(doc, text.data(), text.size());
	if (result) {
		error = std::string("invalid JSON: ") + result.c_str();
		return false;
	}

	JsonObjectConst frame = doc.as<JsonObjectConst>();
	if (frame.isNull()) {
		error = "frame is not an object";
		return false;
	}

	const char* type = frame["t"];
	if (!type) {
		error = "missing frame type";
		return false;
	}

	const std::string_view t(type);
	if (t == "req") {
		out.type = FrameType::Request;
	} else if (t == "res") {
		out.type = FrameType::Response;
	} else if (t == "evt") {
		out.type = FrameType::Event;
	} else {
		error = "unknown frame type";
		return false;
	}

	if (out.type != FrameType::Event) {
		JsonVariantConst id = frame["id"];
		if (!id.is<long long>() || id.as<long long>() < 0 || id.as<long long>() > 0xffffffffLL) {
			error = "missing or invalid id";
			return false;
		}
		out.id = static_cast<uint32_t>(id.as<long long>());
	}

	if (out.type != FrameType::Response) {
		const char* cmd = frame["cmd"];
		if (!cmd || !*cmd) {
			error = "missing cmd";
			return false;
		}
		out.cmd = cmd;
	}

	out.payload = frame["p"].as<JsonObjectConst>();
	return true;
}

std::string encodeResponse(uint32_t id, JsonObjectConst payload) {
	JsonDocument doc;
	doc["t"] = "res";
	doc["id"] = id;
	doc["ok"] = true;
	JsonObject p = doc["p"].to<JsonObject>();
	if (!payload.isNull()) p.set(payload);
	return serialize(doc);
}

std::string encodeEmptyResponse(uint32_t id) {
	return encodeResponse(id, JsonObjectConst());
}

std::string encodeError(uint32_t id, std::string_view error) {
	JsonDocument doc;
	doc["t"] = "res";
	doc["id"] = id;
	doc["ok"] = false;
	doc["err"] = std::string(error.substr(0, 1024));
	return serialize(doc);
}

std::string encodeEvent(std::string_view cmd, JsonObjectConst payload) {
	JsonDocument doc;
	doc["t"] = "evt";
	doc["cmd"] = std::string(cmd);
	JsonObject p = doc["p"].to<JsonObject>();
	if (!payload.isNull()) p.set(payload);
	return serialize(doc);
}

bool parseWakeRequest(JsonObjectConst payload, WakeRequest& out, std::string& error) {
	const char* mac = payload["mac"];
	if (!mac || !parseMac(mac, out.mac)) {
		error = "Invalid MAC address";
		return false;
	}
	if (!readPort(payload["port"], out.port, false)) {
		error = "Invalid port";
		return false;
	}
	return true;
}

bool parseStatusRequest(JsonObjectConst payload, StatusRequest& out, std::string& error) {
	out = StatusRequest{};

	JsonObjectConst target = payload["target"];
	const char* type = target["type"];
	if (!type) {
		error = "Missing status target";
		return false;
	}

	const std::string_view t(type);
	if (t == "link_local") {
		out.target = StatusRequest::Target::LinkLocal;
		const char* mac = target["mac"];
		if (!mac || !parseMac(mac, out.mac)) {
			error = "Invalid MAC address";
			return false;
		}
	} else if (t == "ip" || t == "host") {
		out.target = t == "ip" ? StatusRequest::Target::Ip : StatusRequest::Target::Host;
		const char* address = target[t == "ip" ? "address" : "host"];
		if (!address || !*address || std::string_view(address).size() > 253) {
			error = "Invalid address";
			return false;
		}
		out.address = address;
		// The agent uses its own interface for link-local addresses.
		const size_t zone = out.address.find('%');
		if (zone != std::string::npos) out.address.erase(zone);
	} else {
		error = "Unknown status target";
		return false;
	}

	JsonVariantConst icmp = payload["icmp"];
	out.icmp = icmp.is<bool>() ? icmp.as<bool>() : true;

	JsonVariantConst tcpPort = payload["tcp_port"];
	if (!tcpPort.isNull() && !readPort(tcpPort, out.tcpPort, false)) {
		error = "Invalid TCP port";
		return false;
	}

	if (!out.icmp && out.tcpPort == 0) {
		error = "No probe requested";
		return false;
	}
	return true;
}

}  // namespace netignite::protocol
