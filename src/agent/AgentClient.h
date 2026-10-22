#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <functional>
#include <string>
#include <string_view>

#include "agent/SshWorker.h"
#include "agent/StatusWorker.h"
#include "net/WifiLink.h"
#include "netignite/Protocol.h"
#include "netignite/ServerUrl.h"
#include "ota/OtaUpdater.h"

/**
 * The connection to the NetIgnite server: one WebSocket to `/ws/agent` speaking protocol v2.
 * Sends HELLO on connect and answers the server's requests (wake, status checks, OTA updates,
 * logs, configuration, reboot, SSH logins to devices).
 */
class AgentClient {
   public:
	enum class State { Stopped, Connecting, Online };

	/** Device-wide actions, carried out by the main loop. */
	struct Hooks {
		/** Validates and schedules a CONFIG_SET. False (with an error) if it can't be applied. */
		std::function<bool(const netignite::protocol::ConfigRequest&, std::string& error)> applyConfig;
		/** Restart soon (REBOOT); with `factoryReset`, erase all settings first. */
		std::function<void(bool factoryReset)> restart;
		/** The server answered on this connection: the credential, server URL and network work. */
		std::function<void()> serverConfirmed;
		/** An OTA update was installed; restart into it. */
		std::function<void(const std::string& version)> updateInstalled;
	};

	AgentClient(Settings& settings, WifiLink& wifi) : settings_(settings), wifi_(wifi) {}

	void begin(Hooks hooks);
	void start(const netignite::ServerUrl& server, const String& tokenId, const String& secret);
	void stop();
	void loop();

	State state() const;
	bool running() const { return running_; }
	const OtaUpdater& ota() const { return ota_; }
	const SshWorker& ssh() const { return ssh_; }

   private:
	Settings& settings_;
	WifiLink& wifi_;
	Hooks hooks_;
	WebSocketsClient ws_;
	StatusWorker worker_;
	SshWorker ssh_;
	OtaUpdater ota_;
	netignite::ServerUrl server_;

	bool running_ = false;
	bool connected_ = false;
	bool helloSent_ = false;
	bool confirmed_ = false;
	uint32_t generation_ = 0;
	uint32_t reconnectInterval_ = 0;
	uint32_t lastFrameAt_ = 0;
	std::string authHeader_;
	std::string authorization_;

	// Console log stream (LOG_SUBSCRIBE)
	bool logSubscribed_ = false;
	uint32_t logCursor_ = 0;
	uint32_t lastLogSentAt_ = 0;

	// OTA: progress reported to the server; the connection is paused if memory is short.
	uint32_t otaReportedRevision_ = 0;
	uint32_t otaReportedAt_ = 0;
	bool otaPausedConnection_ = false;
	bool otaInstalledHandled_ = false;

	void onEvent(WStype_t type, uint8_t* payload, size_t length);
	void handleText(std::string_view text);
	void handleRequest(const netignite::protocol::Frame& frame);
	void sendHello();
	/** SSH logins to devices: only with the agent's keys and a TLS connection to the server. */
	bool sshAvailable() const { return ssh_.ready() && server_.secure; }
	void send(const std::string& frame);
	void sendLogs();
	void reportOta();
	void fillInfo(JsonObject info);
};
