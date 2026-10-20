#include "serial/SerialConsole.h"

#include <WiFi.h>

#include "Config.h"
#include "core/DeviceInfo.h"
#include "core/Log.h"
#include "netignite/CommandLine.h"
#include "netignite/ServerUrl.h"

namespace improv = netignite::improv;

namespace {

bool validHostname(const std::string& name) {
	if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') return false;
	for (char c : name) {
		if (!isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
	}
	return true;
}

bool validIp(const std::string& text) {
	IPAddress address;
	return address.fromString(text.c_str());
}

}  // namespace

void SerialConsole::loop() {
	while (Serial.available() > 0) {
		const int byte = Serial.read();
		if (byte < 0) break;

		const auto event = demux_.feed(static_cast<uint8_t>(byte));
		switch (event.type) {
			case netignite::SerialDemux::Event::Type::Line:
				handleLine(event.line);
				break;
			case netignite::SerialDemux::Event::Type::ImprovPacket:
				handleImprov(event.improvType, event.improvData);
				break;
			case netignite::SerialDemux::Event::Type::ImprovBadChecksum:
				sendImprov(improv::buildErrorPacket(improv::Error::InvalidRpc));
				break;
			case netignite::SerialDemux::Event::Type::None:
				break;
		}
	}
}

// --- Improv -------------------------------------------------------------------------------------

void SerialConsole::sendImprov(const std::vector<uint8_t>& packet) {
	Log::writeRaw(packet.data(), packet.size());
}

improv::State SerialConsole::improvState() const {
	return wifi_.connected() && wifi_.hasIp() ? improv::State::Provisioned : improv::State::Authorized;
}

std::string SerialConsole::dashboardUrl() const {
	auto server = netignite::parseServerUrl(settings_.serverUrl().c_str());
	if (!server) return "";
	if (settings_.agentId() != 0) return server->url("/dashboard/agents/" + std::to_string(settings_.agentId()));
	return server->url("/dashboard/agents");
}

void SerialConsole::handleImprov(uint8_t type, const std::vector<uint8_t>& data) {
	if (type != static_cast<uint8_t>(improv::PacketType::Rpc)) return;

	improv::RpcCommand command;
	if (!improv::parseRpc(data, command)) {
		sendImprov(improv::buildErrorPacket(improv::Error::InvalidRpc));
		return;
	}
	sendImprov(improv::buildErrorPacket(improv::Error::None));

	switch (command.command) {
		case improv::Command::GetCurrentState: {
			const improv::State state = improvState();
			sendImprov(improv::buildStatePacket(state));
			if (state == improv::State::Provisioned) {
				const std::string url = dashboardUrl();
				sendImprov(improv::buildRpcResult(command.command, url.empty() ? std::vector<std::string>{}
				                                                                : std::vector<std::string>{url}));
			}
			break;
		}

		case improv::Command::GetDeviceInfo:
			sendImprov(improv::buildRpcResult(command.command, {config::FIRMWARE_NAME, config::FIRMWARE_VERSION, "ESP32",
			                                                    settings_.deviceName().c_str()}));
			break;

		case improv::Command::WifiSettings: {
			Log::info("Improv: Wi-Fi settings for \"%s\" received", command.ssid.c_str());
			sendImprov(improv::buildStatePacket(improv::State::Provisioning));
			if (wifi_.tryNetwork(command.ssid.c_str(), command.password.c_str())) {
				sendImprov(improv::buildStatePacket(improv::State::Provisioned));
				const std::string url = dashboardUrl();
				sendImprov(improv::buildRpcResult(command.command, url.empty() ? std::vector<std::string>{}
				                                                                : std::vector<std::string>{url}));
			} else {
				sendImprov(improv::buildErrorPacket(improv::Error::UnableToConnect));
				sendImprov(improv::buildStatePacket(improvState()));
			}
			break;
		}

		case improv::Command::GetWifiNetworks: {
			for (const WifiLink::Network& network : wifi_.scan()) {
				sendImprov(improv::buildRpcResult(
					command.command, {network.ssid.c_str(), std::to_string(network.rssi), network.secured ? "YES" : "NO"}));
			}
			// An empty result ends the list.
			sendImprov(improv::buildRpcResult(command.command, {}));
			break;
		}

		case improv::Command::Hostname:
		case improv::Command::DeviceName: {
			// The device name doubles as the network host name; setting it applies after a restart.
			// Set: command | length | name length | name
			if (data.size() > 2) {
				if (static_cast<size_t>(data[2]) + 3 != data.size()) {
					sendImprov(improv::buildErrorPacket(improv::Error::InvalidRpc));
					break;
				}
				const std::string name(data.begin() + 3, data.end());
				if (!validHostname(name)) {
					sendImprov(improv::buildErrorPacket(improv::Error::BadHostname));
					break;
				}
				settings_.setDeviceName(name.c_str());
			}
			sendImprov(improv::buildRpcResult(command.command, {settings_.deviceName().c_str()}));
			break;
		}

		case improv::Command::GetNetworkState: {
			const bool online = improvState() == improv::State::Provisioned;
			// 0x01 online, 0x02 supports Wi-Fi
			std::vector<std::string> result = {std::to_string((online ? 0x01 : 0x00) | 0x02)};
			const std::string url = dashboardUrl();
			if (online && !url.empty()) result.push_back(url);
			sendImprov(improv::buildRpcResult(command.command, result));
			break;
		}

		default:
			sendImprov(improv::buildErrorPacket(improv::Error::UnknownRpc));
			break;
	}
}

// --- Console ------------------------------------------------------------------------------------

void SerialConsole::handleLine(const std::string& line) {
	std::vector<std::string> args;
	if (!netignite::splitCommandLine(line, args)) {
		Log::print("Unterminated quote. Type \"help\" for the commands.");
		return;
	}
	if (args.empty()) return;

	const std::string& command = args[0];
	if (command == "help" || command == "?") {
		printHelp();
	} else if (command == "status") {
		printStatus();
	} else if (command == "wifi") {
		cmdWifi(args);
	} else if (command == "server") {
		cmdServer(args);
	} else if (command == "enroll") {
		cmdEnroll(args);
	} else if (command == "setup") {
		cmdSetup(args);
	} else if (command == "ip") {
		cmdIp(args);
	} else if (command == "name") {
		cmdName(args);
	} else if (command == "log") {
		cmdLog(args);
	} else if (command == "reboot" || command == "restart") {
		Log::print("Restarting...");
		delay(200);
		ESP.restart();
	} else if (command == "factory-reset") {
		cmdFactoryReset(args);
	} else {
		Log::print("Unknown command \"%s\". Type \"help\" for the commands.", command.c_str());
	}
}

void SerialConsole::printHelp() {
	// One line per print: Log::print has a fixed-size buffer.
	static const char* const LINES[] = {
		"NetIgnite agent commands:",
		"  status                                   show the agent's state",
		"  setup <server-url> <enrollment-token>    connect this agent to a NetIgnite server",
		"  wifi <ssid> [password]                   connect to a Wi-Fi network (quote names with spaces)",
		"  server <url>                             change the server address",
		"  enroll <enrollment-token>                set the agent up again with a new token",
		"  ip dhcp                                  get the IP address via DHCP (default)",
		"  ip static <ip> <gateway> <netmask> [dns1] [dns2]",
		"  name <device-name>                       network host name (letters, digits, -)",
		"  log <error|warn|info|debug>              log level",
		"  reboot                                   restart the agent",
		"  factory-reset confirm                    erase all settings and credentials",
	};
	for (const char* line : LINES) Log::print("%s", line);
}

void SerialConsole::printStatus() {
	const char* agentState = "not configured";
	if (settings_.enrollToken().length() > 0) {
		agentState = "waiting to enroll";
	} else if (settings_.hasCredentials()) {
		switch (agent_.state()) {
			case AgentClient::State::Online:
				agentState = "online";
				break;
			case AgentClient::State::Connecting:
				agentState = "connecting";
				break;
			case AgentClient::State::Stopped:
				agentState = "waiting for the network";
				break;
		}
	}

	Log::print("%s %s", config::FIRMWARE_NAME, config::FIRMWARE_VERSION);
	Log::print("  device:    %s (%s, %s)", settings_.deviceName().c_str(), deviceInfo::mac().c_str(),
	           deviceInfo::chip().c_str());
	if (wifi_.connected() && wifi_.hasIp()) {
		Log::print("  wifi:      %s, %d dBm", wifi_.ssid().c_str(), wifi_.rssi());
		Log::print("  ipv4:      %s (%s)", wifi_.ipv4().c_str(), settings_.staticIp().enabled ? "static" : "DHCP");
		Log::print("  ipv6:      %s %s", wifi_.linkLocalIpv6().c_str(), wifi_.globalIpv6().c_str());
	} else {
		Log::print("  wifi:      %s", settings_.hasWifi() ? "not connected" : "not set up");
	}
	Log::print("  server:    %s", settings_.serverUrl().length() > 0 ? settings_.serverUrl().c_str() : "not set");
	if (settings_.agentId() != 0) Log::print("  agent id:  %lu", static_cast<unsigned long>(settings_.agentId()));
	Log::print("  agent:     %s", agentState);
	Log::print("  partition: %s, reset reason: %s, free heap: %lu", deviceInfo::runningPartition(),
	           deviceInfo::resetReason(), static_cast<unsigned long>(ESP.getFreeHeap()));
}

void SerialConsole::cmdWifi(const std::vector<std::string>& args) {
	if (args.size() < 2 || args.size() > 3 || args[1].empty() || args[1].size() > 32) {
		Log::print("Usage: wifi <ssid> [password]");
		return;
	}
	const std::string password = args.size() == 3 ? args[2] : "";
	if (password.size() > 64) {
		Log::print("The password is too long (max. 64 characters).");
		return;
	}
	Log::print("Connecting to \"%s\"...", args[1].c_str());
	if (wifi_.tryNetwork(args[1].c_str(), password.c_str())) {
		Log::print("Connected and saved.");
	} else {
		Log::print("Couldn't connect; the previous network is kept.");
	}
}

bool SerialConsole::applyServer(const std::string& url) {
	auto server = netignite::parseServerUrl(url);
	if (!server) {
		Log::print("Invalid server URL. Example: https://app.netignite.dev");
		return false;
	}
	settings_.setServerUrl(server->url("").c_str());
	return true;
}

bool SerialConsole::applyEnrollToken(const std::string& token) {
	if (token.rfind("netignite_enroll_", 0) != 0 || token.size() > 200) {
		Log::print("That isn't an enrollment token. Create one on the agent's page in the dashboard.");
		return false;
	}
	settings_.setEnrollToken(token.c_str());
	return true;
}

void SerialConsole::cmdServer(const std::vector<std::string>& args) {
	if (args.size() != 2) {
		Log::print("Usage: server <url>");
		return;
	}
	if (!applyServer(args[1])) return;
	Log::print("Server set to %s.", settings_.serverUrl().c_str());
	onConfigChanged_();
}

void SerialConsole::cmdEnroll(const std::vector<std::string>& args) {
	if (args.size() != 2) {
		Log::print("Usage: enroll <enrollment-token>");
		return;
	}
	if (!applyEnrollToken(args[1])) return;
	Log::print("Enrollment token saved; the agent enrolls as soon as it is online.");
	onConfigChanged_();
}

void SerialConsole::cmdSetup(const std::vector<std::string>& args) {
	if (args.size() != 3) {
		Log::print("Usage: setup <server-url> <enrollment-token>");
		return;
	}
	if (!applyServer(args[1]) || !applyEnrollToken(args[2])) return;
	Log::print("Saved. The agent enrolls with %s as soon as it is online.", settings_.serverUrl().c_str());
	onConfigChanged_();
}

void SerialConsole::cmdIp(const std::vector<std::string>& args) {
	if (args.size() == 2 && args[1] == "dhcp") {
		settings_.setStaticIp({});
		Log::print("Using DHCP. Reconnecting...");
		wifi_.reconnect();
		return;
	}
	if (args.size() >= 5 && args.size() <= 7 && args[1] == "static") {
		for (size_t i = 2; i < args.size(); i++) {
			if (!validIp(args[i])) {
				Log::print("\"%s\" isn't an IPv4 address.", args[i].c_str());
				return;
			}
		}
		Settings::StaticIp ip;
		ip.enabled = true;
		ip.ip = args[2].c_str();
		ip.gateway = args[3].c_str();
		ip.netmask = args[4].c_str();
		if (args.size() > 5) ip.dns1 = args[5].c_str();
		if (args.size() > 6) ip.dns2 = args[6].c_str();
		settings_.setStaticIp(ip);
		Log::print("Static IP %s saved. Reconnecting...", ip.ip.c_str());
		wifi_.reconnect();
		return;
	}
	Log::print("Usage: ip dhcp | ip static <ip> <gateway> <netmask> [dns1] [dns2]");
}

void SerialConsole::cmdName(const std::vector<std::string>& args) {
	if (args.size() != 2 || !validHostname(args[1])) {
		Log::print("Usage: name <device-name> (letters, digits and -, max. 63)");
		return;
	}
	settings_.setDeviceName(args[1].c_str());
	Log::print("Name saved; it is used as the host name after a restart.");
}

void SerialConsole::cmdLog(const std::vector<std::string>& args) {
	LogLevel level;
	if (args.size() != 2 || !Log::parseLevel(args[1].c_str(), level)) {
		Log::print("Usage: log <error|warn|info|debug>");
		return;
	}
	settings_.setLogLevel(level);
	Log::setLevel(level);
	Log::print("Log level: %s", Log::levelName(level));
}

void SerialConsole::cmdFactoryReset(const std::vector<std::string>& args) {
	if (args.size() != 2 || args[1] != "confirm") {
		Log::print("This erases Wi-Fi, server and agent credentials. Type \"factory-reset confirm\".");
		return;
	}
	settings_.factoryReset();
	Log::print("All settings erased. Restarting...");
	delay(200);
	ESP.restart();
}
