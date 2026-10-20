// NetIgnite ESP32 agent, firmware v2.
//
// Flow: Wi-Fi (from Improv or the console) → time sync (for HTTPS) → enrollment with a
// single-use token (from the web installer's NVS image or the console) → WebSocket to the
// server, which then sends WAKE / STATUS / PING requests.

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

	reloadServer();
	wifi.begin();
	agent.begin();
}

void loop() {
	console.loop();
	wifi.loop();
	runAgent();
	agent.loop();
	delay(2);
}
