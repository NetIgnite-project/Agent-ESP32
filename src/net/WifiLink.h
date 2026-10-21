#pragma once

#include <Arduino.h>

#include <vector>

#include "core/Settings.h"

/**
 * Station-mode Wi-Fi: DHCP or a static IPv4 configuration, IPv6 (link-local and SLAAC/DHCPv6),
 * automatic reconnects. Credentials come from Settings, never from compiled-in values.
 */
class WifiLink {
   public:
	explicit WifiLink(Settings& settings) : settings_(settings) {}

	void begin();
	void loop();

	bool connected() const;
	/** True once the current connection has an IPv4 address. */
	bool hasIp() const;

	/**
	 * Connects to a new network and stores it only if the connection succeeds within
	 * `config::WIFI_TRY_CONNECT_MS`. Otherwise reconnects to the previous network.
	 */
	bool tryNetwork(const String& ssid, const String& password);

	/** Applies changed IP settings (static/DHCP) by reconnecting. */
	void reconnect();

	struct Network {
		String ssid;
		int rssi;
		bool secured;
	};
	/** Scans for networks (blocking, a few seconds); at most 20, strongest first, no duplicates. */
	std::vector<Network> scan();

	String ipv4() const;
	String linkLocalIpv6() const;
	String globalIpv6() const;
	int rssi() const;
	String ssid() const;

   private:
	Settings& settings_;
	bool wasConnected_ = false;
	uint32_t disconnectedSince_ = 0;
	uint32_t lastAttempt_ = 0;

	void applyIpConfig();
	void connect(const String& ssid, const String& password);
};
