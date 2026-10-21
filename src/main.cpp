// NetIgnite ESP32 agent, firmware v2.
//
// Flow: Wi-Fi (from Improv or the console) → time sync (for HTTPS) → enrollment with a
// single-use token (from the web installer's NVS image or the console) → WebSocket to the
// server, which then sends WAKE / STATUS / PING requests, OTA updates and console commands.

#include <Arduino.h>

#include <optional>

#include "Config.h"
#include "agent/AgentClient.h"
#include "agent/Enrollment.h"
#include "core/DeviceInfo.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "net/TimeSync.h"
#include "net/WifiLink.h"
#include "netignite/ServerUrl.h"
#include "ota/Rollback.h"
#include "serial/SerialConsole.h"

namespace {

Settings settings;
WifiLink wifi(settings);
TimeSync timeSync;
AgentClient agent(settings, wifi);

/** What the main loop is waiting for; logged once per change. */
enum class Phase { NoWifi, NoServer, WaitingForNetwork, WaitingForTime, Enrolling, NotEnrolled, Running };
Phase phase = Phase::NoWifi;
bool phaseLogged = false;

std::optional<netignite::ServerUrl> server;
uint32_t nextEnrollAttempt = 0;

/** A restart requested by the server or an installed update, after the answer went out. */
struct PendingRestart {
	bool active = false;
	bool factoryReset = false;
	uint32_t at = 0;
} pendingRestart;

/**
 * A CONFIG_SET that changed the server URL or the IP settings: applied for this boot only, and
 * saved once the agent reaches the server with it. Otherwise the agent restarts with the saved
 * settings when `deadline` passes.
 */
struct ConfigTrial {
	bool active = false;
	bool applyPending = false;
	bool serverChanged = false;
	bool ipChanged = false;
	uint32_t applyAt = 0;
	uint32_t deadline = 0;
} trial;

void reloadServer() {
	server = netignite::parseServerUrl(settings.serverUrl().c_str());
	if (settings.serverUrl().length() > 0 && !server) {
		Log::error("The saved server URL \"%s\" is invalid", settings.serverUrl().c_str());
	}
}

void onConfigChanged() {
	agent.stop();
	reloadServer();
	nextEnrollAttempt = 0;
	phaseLogged = false;
}

void scheduleRestart(bool factoryReset, uint32_t delayMs) {
	pendingRestart = {true, factoryReset, millis() + delayMs};
}

bool applyConfig(const netignite::protocol::ConfigRequest& request, std::string& error) {
	if ((request.serverUrl || request.ip) && trial.active) {
		error = "A previous server or network change is still being tried";
		return false;
	}

	if (request.name) {
		settings.setDeviceName(request.name->c_str());
		Log::info("Name changed to %s (host name after a restart)", request.name->c_str());
	}
	if (request.logLevel) {
		LogLevel level;
		Log::parseLevel(request.logLevel->c_str(), level);
		settings.setLogLevel(level);
		Log::setLevel(level);
		Log::info("Log level: %s", Log::levelName(level));
	}

	const bool serverChanged = request.serverUrl && settings.serverUrl() != request.serverUrl->c_str();
	if (serverChanged) settings.setServerUrlUnsaved(request.serverUrl->c_str());

	if (request.ip) {
		Settings::StaticIp ip;
		ip.enabled = request.ip->isStatic;
		ip.ip = request.ip->address.c_str();
		ip.gateway = request.ip->gateway.c_str();
		ip.netmask = request.ip->netmask.c_str();
		ip.dns1 = request.ip->dns1.c_str();
		ip.dns2 = request.ip->dns2.c_str();
		settings.setStaticIpUnsaved(ip);
	}

	if (serverChanged || request.ip) {
		// Applied a moment later, so the answer still goes out over the current connection.
		trial = {true, true, serverChanged, request.ip.has_value(), millis() + 500,
		         millis() + config::CONFIG_TRIAL_MS};
		Log::info("Trying the new %s; it is kept once the agent reaches the server (within %lu s)",
		          serverChanged && request.ip ? "server and network settings"
		          : serverChanged             ? "server address"
		                                      : "network settings",
		          static_cast<unsigned long>(config::CONFIG_TRIAL_MS / 1000));
	}
	return true;
}

/** The agent reached the server: the running firmware and any tried settings work. */
void onServerConfirmed() {
	rollback::confirm(settings);

	if (trial.active && !trial.applyPending) {
		if (trial.serverChanged) settings.setServerUrl(settings.serverUrl());
		if (trial.ipChanged) settings.setStaticIp(settings.staticIp());
		trial = {};
		Log::info("New settings work and are saved");
	}
}

void loopTrialAndRestart() {
	if (trial.active && trial.applyPending && static_cast<int32_t>(millis() - trial.applyAt) >= 0) {
		trial.applyPending = false;
		if (trial.ipChanged) wifi.reconnect();
		onConfigChanged();
	}
	if (trial.active && !trial.applyPending && static_cast<int32_t>(millis() - trial.deadline) >= 0) {
		Log::warn("The agent didn't reach the server with the new settings; restarting with the previous ones");
		scheduleRestart(false, 200);
		trial = {};
	}

	if (pendingRestart.active && static_cast<int32_t>(millis() - pendingRestart.at) >= 0) {
		if (pendingRestart.factoryReset) {
			settings.factoryReset();
			Log::info("All settings erased");
		}
		Log::info("Restarting");
		delay(100);
		ESP.restart();
	}
}

SerialConsole console(settings, wifi, agent, onConfigChanged);

void setPhase(Phase next) {
	if (next == phase && phaseLogged) return;
	phase = next;
	phaseLogged = true;

	switch (phase) {
		case Phase::NoWifi:
			Log::info("Waiting for Wi-Fi settings (web installer or \"wifi\" command)");
			break;
		case Phase::NoServer:
			Log::info("Not connected to a server yet. Type: setup <server-url> <enrollment-token>");
			break;
		case Phase::WaitingForNetwork:
			Log::info("Waiting for the network");
			break;
		case Phase::WaitingForTime:
			Log::info("Waiting for the time (needed to check the server's certificate)");
			break;
		case Phase::Enrolling:
			break;
		case Phase::NotEnrolled:
			Log::warn("This agent has no credential. Create an enrollment token in the dashboard, then type: enroll <token>");
			break;
		case Phase::Running:
			break;
	}
}

void runEnrollment() {
	if (millis() < nextEnrollAttempt) return;

	const enrollment::Result result = enrollment::enroll(*server, settings.enrollToken());
	switch (result.status) {
		case enrollment::Result::Status::Ok:
			settings.setCredentials(result.agentId, result.tokenId, result.secret);
			Log::info("Enrolled as agent %lu", static_cast<unsigned long>(result.agentId));
			break;
		case enrollment::Result::Status::Rejected:
			// Retrying can't help: the token is used, expired or for another agent kind.
			settings.setEnrollToken("");
			Log::error("Enrollment rejected: %s. Create a new token in the dashboard.", result.message.c_str());
			phaseLogged = false;
			break;
		case enrollment::Result::Status::Failed:
			nextEnrollAttempt = millis() + config::ENROLL_RETRY_MS;
			Log::warn("Enrollment failed: %s. Retrying in %lu s.", result.message.c_str(),
			          static_cast<unsigned long>(config::ENROLL_RETRY_MS / 1000));
			break;
	}
}

void runAgent() {
	if (!settings.hasWifi()) return setPhase(Phase::NoWifi);
	if (!server) return setPhase(Phase::NoServer);

	const bool online = wifi.connected() && wifi.hasIp();
	if (!online) {
		// The WebSocket client keeps retrying on its own once it has been started.
		if (!agent.running()) setPhase(Phase::WaitingForNetwork);
		return;
	}
	timeSync.start();
	if (server->secure && !timeSync.synced()) return setPhase(Phase::WaitingForTime);

	if (settings.enrollToken().length() > 0) {
		agent.stop();
		setPhase(Phase::Enrolling);
		runEnrollment();
		return;
	}
	if (!settings.hasCredentials()) return setPhase(Phase::NotEnrolled);

	setPhase(Phase::Running);
	if (!agent.running()) agent.start(*server, settings.tokenId(), settings.secret());
}

}  // namespace

void setup() {
	Log::begin(config::SERIAL_BAUD);
	settings.load();
	Log::setLevel(settings.logLevel());

	Log::info("%s %s on %s", config::FIRMWARE_NAME, config::FIRMWARE_VERSION, deviceInfo::chip().c_str());
	Log::info("Device %s (%s), partition %s, reset reason: %s", settings.deviceName().c_str(), deviceInfo::mac().c_str(),
	          deviceInfo::runningPartition(), deviceInfo::resetReason());
	rollback::begin(settings);

	reloadServer();
	wifi.begin();
	agent.begin({
		.applyConfig = applyConfig,
		.restart = [](bool factoryReset) { scheduleRestart(factoryReset, 500); },
		.serverConfirmed = onServerConfirmed,
		.updateInstalled =
			[](const std::string& version) {
				rollback::recordInstalled(settings, version.c_str());
				Log::info("Restarting into firmware %s", version.c_str());
				scheduleRestart(false, 1000);
			},
	});
}

void loop() {
	console.loop();
	wifi.loop();
	runAgent();
	agent.loop();
	rollback::loop();
	loopTrialAndRestart();
	delay(2);
}
