#include "agent/AgentClient.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>

#include "Config.h"
#include "core/CaBundle.h"
#include "core/DeviceInfo.h"
#include "core/Log.h"
#include "net/WakeSender.h"
#include "netignite/Protocol.h"

namespace protocol = netignite::protocol;

void AgentClient::begin() {
	worker_.begin();
	ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { onEvent(type, payload, length); });
}

void AgentClient::start(const netignite::ServerUrl& server, const String& tokenId, const String& secret) {
	stop();

	const std::string path = server.path("/ws/agent");
	authHeader_ = std::string("Authorization: Bearer netignite_agent_") + tokenId.c_str() + ":" + secret.c_str();

	// An empty subprotocol: the server doesn't negotiate one.
	if (server.secure) {
		ws_.beginSslWithBundle(server.host.c_str(), server.port, path.c_str(), caBundle::data(), caBundle::size(), "");
	} else {
		ws_.begin(server.host.c_str(), server.port, path.c_str(), "");
	}
	ws_.setExtraHeaders(authHeader_.c_str());

	reconnectInterval_ = config::RECONNECT_MIN_MS;
	ws_.setReconnectInterval(reconnectInterval_);

	running_ = true;
	Log::info("Connecting to %s%s", server.origin().c_str(), path.c_str());
}

void AgentClient::stop() {
	if (!running_) return;
	running_ = false;
	ws_.disconnect();
	connected_ = false;
	helloSent_ = false;
	generation_++;
}

AgentClient::State AgentClient::state() const {
	if (!running_) return State::Stopped;
	return connected_ && helloSent_ ? State::Online : State::Connecting;
}

void AgentClient::loop() {
	if (!running_) return;
	ws_.loop();

	StatusWorker::Done done;
	while (worker_.poll(done)) {
		if (done.generation != generation_ || !connected_) continue;
		if (done.error[0] != '\0') {
			send(protocol::encodeError(done.requestId, done.error));
		} else {
			JsonDocument payload;
			payload["online"] = done.online;
			send(protocol::encodeResponse(done.requestId, payload.as<JsonObjectConst>()));
		}
	}

	// The server pings every 30 s; silence means the connection is dead even if TCP doesn't know.
	if (connected_ && millis() - lastFrameAt_ > config::SERVER_SILENCE_TIMEOUT_MS) {
		Log::warn("No message from the server for %lu s, reconnecting",
		          static_cast<unsigned long>(config::SERVER_SILENCE_TIMEOUT_MS / 1000));
		ws_.disconnect();
	}
}

void AgentClient::onEvent(WStype_t type, uint8_t* payload, size_t length) {
	switch (type) {
		case WStype_CONNECTED:
			connected_ = true;
			generation_++;
			lastFrameAt_ = millis();
			reconnectInterval_ = config::RECONNECT_MIN_MS;
			ws_.setReconnectInterval(reconnectInterval_);
			sendHello();
			break;

		case WStype_DISCONNECTED:
			if (connected_) {
				Log::warn("Disconnected from the server");
			} else {
				Log::warn("Couldn't connect to the server, retrying in %lu s",
				          static_cast<unsigned long>(reconnectInterval_ / 1000));
				// Exponential backoff while the server is unreachable or rejects the credential.
				reconnectInterval_ = min<uint32_t>(reconnectInterval_ * 2, config::RECONNECT_MAX_MS);
				ws_.setReconnectInterval(reconnectInterval_);
			}
			connected_ = false;
			helloSent_ = false;
			generation_++;
			break;

		case WStype_TEXT:
			lastFrameAt_ = millis();
			handleText(std::string_view(reinterpret_cast<const char*>(payload), length));
			break;

		case WStype_PING:
		case WStype_PONG:
			lastFrameAt_ = millis();
			break;

		case WStype_ERROR:
			Log::warn("WebSocket error: %.*s", static_cast<int>(length), payload ? reinterpret_cast<const char*>(payload) : "");
			break;

		default:
			break;
	}
}

void AgentClient::sendHello() {
	JsonDocument hello;
	hello["protocol"] = protocol::VERSION;
	hello["kind"] = protocol::AGENT_KIND;
	hello["version"] = config::FIRMWARE_VERSION;

	JsonArray capabilities = hello["capabilities"].to<JsonArray>();
	for (const char* capability : protocol::CAPABILITIES) capabilities.add(capability);

	JsonObject info = hello["info"].to<JsonObject>();
	deviceInfo::fill(info, settings_, wifi_);

	send(protocol::encodeEvent("HELLO", hello.as<JsonObjectConst>()));
	helloSent_ = true;
	Log::info("Connected to the server as agent %lu", static_cast<unsigned long>(settings_.agentId()));
}

void AgentClient::send(const std::string& frame) {
	if (!connected_) return;
	ws_.sendTXT(frame.c_str(), frame.size());
}

void AgentClient::handleText(std::string_view text) {
	JsonDocument doc;
	protocol::Frame frame;
	std::string error;

	if (!protocol::parseFrame(text, doc, frame, error)) {
		Log::warn("Ignored an invalid frame from the server: %s", error.c_str());
		return;
	}
	if (frame.type != protocol::FrameType::Request) return;

	Log::debug("Request %lu: %s", static_cast<unsigned long>(frame.id), frame.cmd.c_str());

	if (frame.cmd == "PING") {
		send(protocol::encodeEmptyResponse(frame.id));
		return;
	}

	if (frame.cmd == "WAKE") {
		protocol::WakeRequest wake;
		if (!protocol::parseWakeRequest(frame.payload, wake, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}
		String sendError;
		if (wake::send(wake.mac, wake.port, sendError)) {
			send(protocol::encodeEmptyResponse(frame.id));
		} else {
			send(protocol::encodeError(frame.id, sendError.c_str()));
		}
		return;
	}

	if (frame.cmd == "STATUS") {
		protocol::StatusRequest status;
		if (!protocol::parseStatusRequest(frame.payload, status, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}
		if (!worker_.submit(frame.id, generation_, status)) {
			send(protocol::encodeError(frame.id, "Too many status checks at once"));
		}
		return;
	}

	send(protocol::encodeError(frame.id, "Unknown command " + frame.cmd));
}
