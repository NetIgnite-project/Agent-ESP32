#include "netignite/Protocol.h"

#include <algorithm>

#include "netignite/Hex.h"
#include "netignite/ServerUrl.h"
#include "netignite/Validation.h"

namespace netignite::protocol {

namespace {

bool readPort(JsonVariantConst value, uint16_t& out, bool allowZero) {
	if (!value.is<long long>()) return false;
	const long long port = value.as<long long>();
	if (port < (allowZero ? 0 : 1) || port > 65535) return false;
	out = static_cast<uint16_t>(port);
	return true;
}

/** An absolute server path without `//`, `..`, query or fragment. */
bool safePath(std::string_view path) {
	if (path.empty() || path.size() > 200 || path[0] != '/' || path.find("//") != std::string_view::npos ||
	    path.find("..") != std::string_view::npos) {
		return false;
	}
	for (char c : path) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
		                c == '.' || c == '_' || c == '-' || c == '~' || c == '+';
		if (!ok) return false;
	}
	return true;
}

bool safeVersion(std::string_view version) {
	if (version.empty() || version.size() > 64) return false;
	for (char c : version) {
		const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
		                c == '-' || c == '+';
		if (!ok) return false;
	}
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

bool parseOtaRequest(JsonObjectConst payload, OtaRequest& out, std::string& error) {
	out = OtaRequest{};

	const char* version = payload["version"];
	if (!version || !safeVersion(version)) {
		error = "Invalid version";
		return false;
	}
	out.version = version;

	const char* path = payload["path"];
	if (!path || !safePath(path)) {
		error = "Invalid download path";
		return false;
	}
	out.path = path;

	JsonVariantConst size = payload["size"];
	if (!size.is<long long>() || size.as<long long>() <= 0 || size.as<long long>() > MAX_FIRMWARE_SIZE) {
		error = "Invalid size (the image must fit into an OTA slot)";
		return false;
	}
	out.size = static_cast<uint32_t>(size.as<long long>());

	const char* sha256 = payload["sha256"];
	std::vector<uint8_t> digest;
	if (!sha256 || !fromHex(sha256, digest) || digest.size() != out.sha256.size()) {
		error = "Invalid SHA-256";
		return false;
	}
	std::copy(digest.begin(), digest.end(), out.sha256.begin());

	JsonVariantConst signature = payload["signature"];
	if (!signature.isNull()) {
		const char* text = signature.as<const char*>();
		// DER-encoded ECDSA P-256 signatures are 70–72 bytes.
		if (!text || !fromHex(text, out.signature) || out.signature.size() < 8 || out.signature.size() > 80) {
			error = "Invalid signature";
			return false;
		}
	}
	return true;
}

bool parseConfigRequest(JsonObjectConst payload, ConfigRequest& out, std::string& error) {
	out = ConfigRequest{};

	JsonVariantConst server = payload["server_url"];
	if (!server.isNull()) {
		const char* text = server.as<const char*>();
		const auto parsed = text ? parseServerUrl(text) : std::nullopt;
		if (!parsed) {
			error = "Invalid server URL";
			return false;
		}
		out.serverUrl = parsed->url("");
	}

	JsonVariantConst name = payload["name"];
	if (!name.isNull()) {
		const char* text = name.as<const char*>();
		if (!text || !isValidHostname(text)) {
			error = "Invalid name (letters, digits and -, max. 63)";
			return false;
		}
		out.name = text;
	}

	JsonVariantConst level = payload["log_level"];
	if (!level.isNull()) {
		const char* text = level.as<const char*>();
		const std::string_view l = text ? text : "";
		if (l != "error" && l != "warn" && l != "info" && l != "debug") {
			error = "Invalid log level";
			return false;
		}
		out.logLevel = std::string(l);
	}

	if (!payload["ip"].isNull()) {
		JsonObjectConst ip = payload["ip"];
		const char* mode = ip["mode"];
		ConfigRequest::Ip config;
		if (mode && std::string_view(mode) == "dhcp") {
			out.ip = config;
		} else if (mode && std::string_view(mode) == "static") {
			config.isStatic = true;
			const char* const fields[] = {"address", "gateway", "netmask", "dns1", "dns2"};
			std::string* const targets[] = {&config.address, &config.gateway, &config.netmask, &config.dns1,
			                                &config.dns2};
			for (size_t i = 0; i < 5; i++) {
				const char* value = ip[fields[i]];
				if (!value || !*value) {
					// address, gateway and netmask are required.
					if (i < 3) {
						error = std::string("Missing ") + fields[i];
						return false;
					}
					continue;
				}
				if (!isValidIpv4(value)) {
					error = std::string("Invalid ") + fields[i];
					return false;
				}
				*targets[i] = value;
			}
			out.ip = config;
		} else {
			error = "Invalid IP mode (dhcp or static)";
			return false;
		}
	}

	if (!out.serverUrl && !out.name && !out.logLevel && !out.ip) {
		error = "Nothing to change";
		return false;
	}
	return true;
}

namespace {

bool usernameChar(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
	       c == '@' || c == '\\' || c == '-';
}

bool base64Char(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '/';
}

}  // namespace

bool parseSshRequest(JsonObjectConst payload, bool power, SshRequest& out, std::string& error) {
	out = SshRequest{};

	const char* host = payload["host"];
	const std::string_view h = host ? host : "";
	bool hostOk = !h.empty() && h.size() <= 253;
	for (char c : h) hostOk = hostOk && static_cast<unsigned char>(c) > ' ' && c != '/' && c != '@';
	if (!hostOk) {
		error = "Invalid host";
		return false;
	}
	out.host = std::string(h);

	if (!readPort(payload["port"], out.port, false)) {
		error = "Invalid port";
		return false;
	}

	const char* username = payload["username"];
	const std::string_view u = username ? username : "";
	bool userOk = !u.empty() && u.size() <= 64;
	for (char c : u) userOk = userOk && usernameChar(c);
	if (!userOk) {
		error = "Invalid username";
		return false;
	}
	out.username = std::string(u);

	JsonVariantConst hostKey = payload["host_key"];
	if (!hostKey.isNull()) {
		const char* text = hostKey.as<const char*>();
		const std::string_view k = text ? text : "";
		bool keyOk = k.size() == 50 && k.substr(0, 7) == "SHA256:";
		for (size_t i = 7; keyOk && i < k.size(); i++) keyOk = base64Char(k[i]);
		if (!keyOk) {
			error = "Invalid host key fingerprint";
			return false;
		}
		out.hostKey = std::string(k);
	}

	if (!power) return true;

	if (out.hostKey.empty()) {
		error = "A trusted host key is required";
		return false;
	}
	const char* action = payload["action"];
	if (action && std::string_view(action) == "shutdown") {
		out.action = SshRequest::Action::Shutdown;
	} else if (action && std::string_view(action) == "reboot") {
		out.action = SshRequest::Action::Reboot;
	} else {
		error = "Unknown action";
		return false;
	}
	const char* os = payload["os"];
	if (os && std::string_view(os) == "windows") {
		out.windows = true;
	} else if (!os || std::string_view(os) != "posix") {
		error = "Unknown operating system";
		return false;
	}
	JsonVariantConst sudo = payload["sudo"];
	if (!sudo.is<bool>()) {
		error = "sudo must be true or false";
		return false;
	}
	out.sudo = sudo.as<bool>();
	return true;
}

std::string sshCommand(const SshRequest& request) {
	const bool reboot = request.action == SshRequest::Action::Reboot;
	if (request.windows) return reboot ? "shutdown /r /t 0" : "shutdown /s /t 0";
	return std::string(request.sudo ? "sudo -n " : "") + (reboot ? "shutdown -r now" : "shutdown -h now");
}

std::string sshFingerprint(const uint8_t sha256[32]) {
	return "SHA256:" + toBase64(sha256, 32, false);
}

bool parseLogSubscribe(JsonObjectConst payload, bool& enabled, std::string& error) {
	JsonVariantConst value = payload["enabled"];
	if (!value.is<bool>()) {
		error = "enabled must be true or false";
		return false;
	}
	enabled = value.as<bool>();
	return true;
}

}  // namespace netignite::protocol
