#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>

#include <string>
#include <string_view>

#include "agent/StatusWorker.h"
#include "net/WifiLink.h"
#include "netignite/ServerUrl.h"

/**
 * The connection to the NetIgnite server: one WebSocket to `/ws/agent` speaking protocol v2.
 * Sends HELLO on connect and answers PING, WAKE and STATUS requests.
 */
class AgentClient {
   public:
	enum class State { Stopped, Connecting, Online };

	AgentClient(Settings& settings, WifiLink& wifi) : settings_(settings), wifi_(wifi) {}

	void begin();
	void start(const netignite::ServerUrl& server, const String& tokenId, const String& secret);
	void stop();
	void loop();

	State state() const;
	bool running() const { return running_; }

   private:
	Settings& settings_;
	WifiLink& wifi_;
	WebSocketsClient ws_;
	StatusWorker worker_;

	bool running_ = false;
	bool connected_ = false;
	bool helloSent_ = false;
	uint32_t generation_ = 0;
	uint32_t reconnectInterval_ = 0;
	uint32_t lastFrameAt_ = 0;
	std::string authHeader_;

	void onEvent(WStype_t type, uint8_t* payload, size_t length);
	void handleText(std::string_view text);
	void sendHello();
	void send(const std::string& frame);
};
