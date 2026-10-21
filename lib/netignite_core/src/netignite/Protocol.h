#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
 *
 * Requests: PING, WAKE, STATUS, OTA_BEGIN, LOG_SUBSCRIBE, GET_LOGS, GET_INFO, CONFIG_SET, REBOOT,
 * FACTORY_RESET, SSH_CHECK, SSH_EXEC. Events: HELLO, LOG, OTA_PROGRESS.
 */
namespace netignite::protocol {

constexpr int VERSION = 2;
constexpr const char* AGENT_KIND = "esp32";

/** Capabilities this firmware announces in HELLO. */
constexpr const char* CAPABILITIES[] = {
	"wake", "status.link_local", "status.ip", "status.host",   "status.tcp",
	"ota",  "logs",              "config",    "device.reboot", "device.reset",
	"ssh",
};

/** Largest firmware image: one OTA slot in partitions.csv. */
constexpr uint32_t MAX_FIRMWARE_SIZE = 0x1E0000;

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

/**
 * OTA_BEGIN: download `path` from the agent's own server (with its agent credential) and install
 * it if size and SHA-256 match, and the signature if the firmware has a public key compiled in.
 */
struct OtaRequest {
	std::string version;
	/** Absolute path on the server, e.g. `/api/v1/firmware/2.1.0/files/firmware.bin`. */
	std::string path;
	uint32_t size = 0;
	std::array<uint8_t, 32> sha256{};
	/** DER-encoded ECDSA P-256 signature of the image; empty for unsigned releases. */
	std::vector<uint8_t> signature;
};

bool parseOtaRequest(JsonObjectConst payload, OtaRequest& out, std::string& error);

/** CONFIG_SET: only the fields present change. */
struct ConfigRequest {
	struct Ip {
		bool isStatic = false;
		std::string address, gateway, netmask, dns1, dns2;
	};
	/** Normalized (`ServerUrl::url("")`). */
	std::optional<std::string> serverUrl;
	std::optional<std::string> name;
	/** `error`, `warn`, `info` or `debug`. */
	std::optional<std::string> logLevel;
	std::optional<Ip> ip;
};

bool parseConfigRequest(JsonObjectConst payload, ConfigRequest& out, std::string& error);

/** LOG_SUBSCRIBE `{ "enabled": bool }`. */
bool parseLogSubscribe(JsonObjectConst payload, bool& enabled, std::string& error);

/**
 * SSH_CHECK (log in, no session) and SSH_EXEC (log in with a trusted host key and run the fixed
 * shutdown or reboot command). The secret is a password or an OpenSSH private key.
 */
struct SshRequest {
	enum class Auth { Password, Key };
	enum class Action { Shutdown, Reboot };

	std::string host;
	uint16_t port = 22;
	std::string username;
	Auth auth = Auth::Password;
	std::string secret;
	/** Trusted host key fingerprint (`SHA256:...`), or empty. Required for SSH_EXEC. */
	std::string hostKey;

	// SSH_EXEC only
	Action action = Action::Shutdown;
	bool windows = false;
	bool sudo = false;
};

bool parseSshRequest(JsonObjectConst payload, bool exec, SshRequest& out, std::string& error);

/** The command SSH_EXEC runs: `[sudo -n ]shutdown -h|-r now`, or `shutdown /s|/r /t 0`. */
std::string sshCommand(const SshRequest& request);

/** OpenSSH's fingerprint of a host key from its SHA-256: `SHA256:` + unpadded base64. */
std::string sshFingerprint(const uint8_t sha256[32]);

}  // namespace netignite::protocol
