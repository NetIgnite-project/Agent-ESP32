#include "net/WifiLink.h"

#include <WiFi.h>

#include "Config.h"
#include "core/Log.h"

namespace {

bool parseIp(const String& text, IPAddress& out) {
	return text.length() > 0 && out.fromString(text);
}

}  // namespace

// The emulator build (env:esp32dev-qemu) has no Wi-Fi radio: touching the driver would abort.
#ifdef NETIGNITE_EMULATOR
constexpr bool WIFI_AVAILABLE = false;
#else
constexpr bool WIFI_AVAILABLE = true;
#endif

void WifiLink::begin() {
	if (!WIFI_AVAILABLE) {
		Log::warn("Emulator build: Wi-Fi is disabled");
		return;
	}
	WiFi.persistent(false);  // credentials live in our own NVS namespace
	WiFi.setHostname(settings_.deviceName().c_str());
	WiFi.mode(WIFI_STA);
	WiFi.setSleep(false);  // lower latency for status checks and wake commands
	WiFi.enableIPv6(true);
	WiFi.setAutoReconnect(true);

	if (!settings_.hasWifi()) {
		Log::warn("Wi-Fi isn't set up yet. Use the web installer, or type: wifi \"<ssid>\" \"<password>\"");
		return;
	}
	applyIpConfig();
	connect(settings_.wifiSsid(), settings_.wifiPassword());
}

void WifiLink::applyIpConfig() {
	const Settings::StaticIp& config = settings_.staticIp();
	if (!config.enabled) {
		WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP
		return;
	}

	IPAddress ip, gateway, netmask, dns1, dns2;
	if (!parseIp(config.ip, ip) || !parseIp(config.gateway, gateway) || !parseIp(config.netmask, netmask)) {
		Log::error("Static IP settings are invalid, using DHCP");
		WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
		return;
	}
	if (!parseIp(config.dns1, dns1)) dns1 = gateway;
	if (!parseIp(config.dns2, dns2)) dns2 = IPAddress(0, 0, 0, 0);

	if (!WiFi.config(ip, gateway, netmask, dns1, dns2)) {
		Log::error("Couldn't apply the static IP configuration");
	}
}

void WifiLink::connect(const String& ssid, const String& password) {
	Log::info("Connecting to Wi-Fi \"%s\"", ssid.c_str());
	lastAttempt_ = millis();
	WiFi.begin(ssid.c_str(), password.length() > 0 ? password.c_str() : nullptr);
}

void WifiLink::loop() {
	if (!WIFI_AVAILABLE) return;
	const bool isConnected = connected() && hasIp();

	if (isConnected && !wasConnected_) {
		Log::info("Wi-Fi connected: %s, IPv4 %s, RSSI %d dBm", ssid().c_str(), ipv4().c_str(), rssi());
		disconnectedSince_ = 0;
	} else if (!isConnected && wasConnected_) {
		Log::warn("Wi-Fi connection lost");
		disconnectedSince_ = millis();
	}
	wasConnected_ = isConnected;

	if (isConnected || !settings_.hasWifi()) return;

	if (disconnectedSince_ == 0) disconnectedSince_ = millis();
	// The driver reconnects on its own; restart the attempt if it stays down.
	if (millis() - disconnectedSince_ > config::WIFI_RECONNECT_AFTER_MS &&
	    millis() - lastAttempt_ > config::WIFI_RECONNECT_AFTER_MS) {
		WiFi.disconnect();
		connect(settings_.wifiSsid(), settings_.wifiPassword());
	}
}

bool WifiLink::tryNetwork(const String& ssid, const String& password) {
	if (!WIFI_AVAILABLE) {
		Log::warn("Emulator build: can't connect to \"%s\"", ssid.c_str());
		return false;
	}
	const String previousSsid = settings_.wifiSsid();
	const String previousPassword = settings_.wifiPassword();

	WiFi.disconnect();
	delay(100);
	applyIpConfig();
	connect(ssid, password);

	const uint32_t start = millis();
	while (millis() - start < config::WIFI_TRY_CONNECT_MS) {
		if (connected() && hasIp()) {
			settings_.setWifi(ssid, password);
			Log::info("Wi-Fi \"%s\" saved", ssid.c_str());
			return true;
		}
		delay(100);
	}

	Log::warn("Couldn't connect to \"%s\"", ssid.c_str());
	WiFi.disconnect();
	if (previousSsid.length() > 0) connect(previousSsid, previousPassword);
	return false;
}

void WifiLink::reconnect() {
	if (!WIFI_AVAILABLE) return;
	WiFi.disconnect();
	delay(100);
	applyIpConfig();
	if (settings_.hasWifi()) connect(settings_.wifiSsid(), settings_.wifiPassword());
}

bool WifiLink::connected() const {
	return WIFI_AVAILABLE && WiFi.status() == WL_CONNECTED;
}

bool WifiLink::hasIp() const {
	return WIFI_AVAILABLE && WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

std::vector<WifiLink::Network> WifiLink::scan() {
	std::vector<Network> networks;
	if (!WIFI_AVAILABLE) return networks;

	const int count = WiFi.scanNetworks();
	for (int i = 0; i < count && networks.size() < 20; i++) {
		const String ssid = WiFi.SSID(i);
		if (ssid.length() == 0) continue;
		bool duplicate = false;
		for (const Network& network : networks) duplicate = duplicate || network.ssid == ssid;
		// Results are sorted by signal strength, so the first entry of an SSID is its strongest.
		if (!duplicate) networks.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
	}
	WiFi.scanDelete();
	return networks;
}

String WifiLink::ipv4() const {
	return WiFi.localIP().toString();
}

String WifiLink::linkLocalIpv6() const {
	return WiFi.linkLocalIPv6().toString();
}

String WifiLink::globalIpv6() const {
	return WiFi.globalIPv6().toString();
}

int WifiLink::rssi() const {
	return WIFI_AVAILABLE ? WiFi.RSSI() : 0;
}

String WifiLink::ssid() const {
	return WiFi.SSID();
}
