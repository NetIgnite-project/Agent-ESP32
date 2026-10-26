#include "agent/AgentClient.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "Config.h"
#include "core/CaBundle.h"
#include "core/DeviceInfo.h"
#include "core/Log.h"
#include "net/WakeSender.h"
#include "ota/OtaWriter.h"
#include "ota/Rollback.h"

namespace protocol = netignite::protocol;

void AgentClient::begin(Hooks hooks) {
	hooks_ = std::move(hooks);
	worker_.begin();
	ssh_.begin(settings_);
	ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { onEvent(type, payload, length); });
}

void AgentClient::start(const netignite::ServerUrl& server, const String& tokenId, const String& secret) {
	stop();

	server_ = server;
	const std::string path = server.path("/ws/agent");
	authorization_ = std::string("Bearer netignite_agent_") + tokenId.c_str() + ":" + secret.c_str();
	authHeader_ = "Authorization: " + authorization_;

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
	logSubscribed_ = false;
	generation_++;
}

AgentClient::State AgentClient::state() const {
	if (!running_) return State::Stopped;
	return connected_ && helloSent_ ? State::Online : State::Connecting;
}

void AgentClient::loop() {
	if (!running_) return;

	// While an update downloads with the connection paused, don't reconnect.
	if (otaPausedConnection_ && ota_.busy()) {
		if (connected_) ws_.disconnect();
	} else {
		otaPausedConnection_ = false;
		ws_.loop();
	}

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

	SshWorker::Done ssh;
	while (ssh_.poll(ssh)) {
		if (ssh.generation != generation_ || !connected_) continue;
		if (!ssh.ok) {
			send(protocol::encodeError(ssh.requestId, ssh.error));
		} else if (ssh.power) {
			send(protocol::encodeEmptyResponse(ssh.requestId));
		} else {
			JsonDocument payload;
			payload["online"] = ssh.online;
			if (ssh.online) {
				if (ssh.hostKey[0] != '\0') payload["host_key"] = ssh.hostKey;
				payload["shutdown"] = ssh.shutdownAccepted;
				payload["reboot"] = ssh.rebootAccepted;
			}
			// Offline too, when the host name doesn't resolve.
			if (ssh.checkError[0] != '\0') payload["error"] = ssh.checkError;
			send(protocol::encodeResponse(ssh.requestId, payload.as<JsonObjectConst>()));
		}
	}

	reportOta();
	if (logSubscribed_ && connected_ && millis() - lastLogSentAt_ >= config::LOG_EVENT_INTERVAL_MS) sendLogs();

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
			confirmed_ = false;
			generation_++;
			lastFrameAt_ = millis();
			reconnectInterval_ = config::RECONNECT_MIN_MS;
			ws_.setReconnectInterval(reconnectInterval_);
			sendHello();
			break;

		case WStype_DISCONNECTED:
			if (connected_) {
				if (!otaPausedConnection_) Log::warn("Disconnected from the server");
			} else {
				Log::warn("Couldn't connect to the server, retrying in %lu s",
				          static_cast<unsigned long>(reconnectInterval_ / 1000));
				// Exponential backoff while the server is unreachable or rejects the credential.
				reconnectInterval_ = min<uint32_t>(reconnectInterval_ * 2, config::RECONNECT_MAX_MS);
				ws_.setReconnectInterval(reconnectInterval_);
			}
			connected_ = false;
			helloSent_ = false;
			logSubscribed_ = false;
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

void AgentClient::fillInfo(JsonObject info) {
	deviceInfo::fill(info, settings_, wifi_);
	info["signed_updates"] = OtaWriter::requiresSignature();
	if (rollback::pending()) info["ota_probation"] = true;
	if (rollback::failedUpdate().length() > 0) info["ota_failed_version"] = rollback::failedUpdate();
}

void AgentClient::sendHello() {
	JsonDocument hello;
	hello["protocol"] = protocol::VERSION;
	hello["kind"] = protocol::AGENT_KIND;
	hello["version"] = config::FIRMWARE_VERSION;

	JsonArray capabilities = hello["capabilities"].to<JsonArray>();
	for (const char* capability : protocol::CAPABILITIES) capabilities.add(capability);
	if (sshAvailable()) {
		capabilities.add(protocol::SSH_CAPABILITY);
		JsonObject keys = hello["ssh_keys"].to<JsonObject>();
		keys["shutdown"] = ssh_.publicKey(SshWorker::Action::Shutdown);
		keys["reboot"] = ssh_.publicKey(SshWorker::Action::Reboot);
	}

	fillInfo(hello["info"].to<JsonObject>());

	send(protocol::encodeEvent("HELLO", hello.as<JsonObjectConst>()));
	helloSent_ = true;
	Log::info("Connected to the server as agent %lu", static_cast<unsigned long>(settings_.agentId()));
}

void AgentClient::send(const std::string& frame) {
	if (!connected_) return;
	ws_.sendTXT(frame.c_str(), frame.size());
}

void AgentClient::sendLogs() {
	lastLogSentAt_ = millis();
	std::vector<std::string> lines;
	const auto result = Log::read(logCursor_, config::LOG_EVENT_MAX_BYTES, lines);
	logCursor_ = result.next;
	if (lines.empty() && result.dropped == 0) return;

	JsonDocument payload;
	JsonArray array = payload["lines"].to<JsonArray>();
	// Lines can hold other machines' text (a Wi-Fi name, a device's SSH output) in any encoding.
	for (const std::string& line : lines) array.add(protocol::validUtf8(line));
	if (result.dropped > 0) payload["dropped"] = result.dropped;
	send(protocol::encodeEvent("LOG", payload.as<JsonObjectConst>()));
}

void AgentClient::reportOta() {
	const OtaUpdater::Progress progress = ota_.progress();
	if (progress.state == OtaUpdater::State::Idle) return;

	const bool finished = progress.state != OtaUpdater::State::Downloading;
	const bool changed = progress.revision != otaReportedRevision_;
	if (changed && connected_ && helloSent_ && (finished || millis() - otaReportedAt_ >= 1000)) {
		otaReportedRevision_ = progress.revision;
		otaReportedAt_ = millis();

		JsonDocument payload;
		payload["version"] = progress.version;
		payload["state"] = OtaUpdater::stateName(progress.state);
		payload["percent"] = progress.percent;
		if (!progress.error.empty()) payload["error"] = progress.error;
		send(protocol::encodeEvent("OTA_PROGRESS", payload.as<JsonObjectConst>()));
	}

	if (progress.state == OtaUpdater::State::Installed && !otaInstalledHandled_) {
		otaInstalledHandled_ = true;
		if (hooks_.updateInstalled) hooks_.updateInstalled(progress.version);
	}
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

	// The server only sends requests after accepting the credential and HELLO.
	if (!confirmed_) {
		confirmed_ = true;
		if (hooks_.serverConfirmed) hooks_.serverConfirmed();
	}

	Log::debug("Request %lu: %s", static_cast<unsigned long>(frame.id), frame.cmd.c_str());
	handleRequest(frame);
}

void AgentClient::handleRequest(const protocol::Frame& frame) {
	std::string error;
	const std::string& cmd = frame.cmd;

	if (cmd == "PING") {
		send(protocol::encodeEmptyResponse(frame.id));
		return;
	}

	if (cmd == "WAKE") {
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

	if (cmd == "STATUS") {
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

	if (cmd == "SSH_CHECK" || cmd == "SSH_POWER") {
		if (!server_.secure) {
			send(protocol::encodeError(frame.id, "SSH needs an https:// server address (the agent connects without TLS)"));
			return;
		}
		if (!ssh_.ready()) {
			send(protocol::encodeError(frame.id, "This agent has no SSH keys"));
			return;
		}
		const bool power = cmd == "SSH_POWER";
		protocol::SshRequest request;
		if (!protocol::parseSshRequest(frame.payload, power, request, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}
		switch (ssh_.submit(frame.id, generation_, power, request)) {
			case SshWorker::Submit::Started:
				break;
			case SshWorker::Submit::Busy:
				send(protocol::encodeError(frame.id, "Another SSH login is running"));
				break;
			case SshWorker::Submit::TooManyLogins:
				send(protocol::encodeError(frame.id, "Too many SSH logins (at most 10 a minute); try again shortly"));
				break;
			case SshWorker::Submit::OutOfMemory:
				send(protocol::encodeError(frame.id, "Not enough memory for an SSH login right now; try again shortly"));
				break;
		}
		return;
	}

	if (cmd == "GET_INFO") {
		JsonDocument payload;
		payload["version"] = config::FIRMWARE_VERSION;
		fillInfo(payload["info"].to<JsonObject>());
		send(protocol::encodeResponse(frame.id, payload.as<JsonObjectConst>()));
		return;
	}

	if (cmd == "GET_LOGS" || cmd == "LOG_SUBSCRIBE") {
		bool enabled = true;
		if (cmd == "LOG_SUBSCRIBE" && !protocol::parseLogSubscribe(frame.payload, enabled, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}

		JsonDocument payload;
		if (enabled) {
			// Everything stored so far; a subscription continues from there with LOG events.
			std::vector<std::string> lines;
			const auto result = Log::read(0, Log::RING_BYTES, lines);
			JsonArray array = payload["lines"].to<JsonArray>();
			for (const std::string& line : lines) array.add(protocol::validUtf8(line));
			payload["level"] = Log::levelName(Log::level());
			if (cmd == "LOG_SUBSCRIBE") logCursor_ = result.next;
		}
		if (cmd == "LOG_SUBSCRIBE") logSubscribed_ = enabled;
		send(protocol::encodeResponse(frame.id, payload.as<JsonObjectConst>()));
		return;
	}

	if (cmd == "CONFIG_SET") {
		protocol::ConfigRequest request;
		if (!protocol::parseConfigRequest(frame.payload, request, error) ||
		    !(hooks_.applyConfig && hooks_.applyConfig(request, error))) {
			send(protocol::encodeError(frame.id, error.empty() ? "Can't change the configuration" : error));
			return;
		}
		send(protocol::encodeEmptyResponse(frame.id));
		return;
	}

	if (cmd == "REBOOT" || cmd == "FACTORY_RESET") {
		send(protocol::encodeEmptyResponse(frame.id));
		Log::info("%s requested by the server", cmd == "REBOOT" ? "Restart" : "Factory reset");
		if (hooks_.restart) hooks_.restart(cmd == "FACTORY_RESET");
		return;
	}

	if (cmd == "OTA_BEGIN") {
		protocol::OtaRequest request;
		if (!protocol::parseOtaRequest(frame.payload, request, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}
		if (rollback::pending()) {
			send(protocol::encodeError(frame.id, "The current firmware isn't confirmed yet; try again shortly"));
			return;
		}

		// Two TLS sessions at once (WebSocket + download) need memory; without it, pause the connection.
		const bool pause = server_.secure && ESP.getMaxAllocHeap() < config::OTA_MIN_FREE_BLOCK;
		if (!ota_.start(server_, authorization_, request, error)) {
			send(protocol::encodeError(frame.id, error));
			return;
		}
		otaInstalledHandled_ = false;
		send(protocol::encodeEmptyResponse(frame.id));
		if (pause) {
			Log::info("Pausing the server connection during the download (largest free block: %lu bytes)",
			          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
			otaPausedConnection_ = true;
		}
		return;
	}

	send(protocol::encodeError(frame.id, "Unknown command " + cmd));
}
