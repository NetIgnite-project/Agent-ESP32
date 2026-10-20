#pragma once

#include <Arduino.h>

#include <functional>
#include <string>
#include <vector>

#include "agent/AgentClient.h"
#include "core/Settings.h"
#include "net/WifiLink.h"
#include "netignite/Improv.h"
#include "netignite/SerialDemux.h"

/**
 * The serial port: Improv Wi-Fi for the web installer, and a small text console for setting the
 * agent up by hand (see `help`). Secrets are never printed.
 */
class SerialConsole {
   public:
	SerialConsole(Settings& settings, WifiLink& wifi, AgentClient& agent, std::function<void()> onConfigChanged)
		: settings_(settings), wifi_(wifi), agent_(agent), onConfigChanged_(std::move(onConfigChanged)) {}

	void loop();

   private:
	Settings& settings_;
	WifiLink& wifi_;
	AgentClient& agent_;
	std::function<void()> onConfigChanged_;
	netignite::SerialDemux demux_;

	// Improv
	void handleImprov(uint8_t type, const std::vector<uint8_t>& data);
	void sendImprov(const std::vector<uint8_t>& packet);
	netignite::improv::State improvState() const;
	/** Where the web installer's "Visit device" button leads: the agent's dashboard page. */
	std::string dashboardUrl() const;

	// Console
	void handleLine(const std::string& line);
	void printHelp();
	void printStatus();
	void cmdWifi(const std::vector<std::string>& args);
	void cmdServer(const std::vector<std::string>& args);
	void cmdEnroll(const std::vector<std::string>& args);
	void cmdSetup(const std::vector<std::string>& args);
	void cmdIp(const std::vector<std::string>& args);
	void cmdName(const std::vector<std::string>& args);
	void cmdLog(const std::vector<std::string>& args);
	void cmdFactoryReset(const std::vector<std::string>& args);

	bool applyServer(const std::string& url);
	bool applyEnrollToken(const std::string& token);
};
