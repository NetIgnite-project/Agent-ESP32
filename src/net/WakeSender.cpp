#include "net/WakeSender.h"

#include <WiFi.h>
#include <lwip/sockets.h>

#include "Config.h"
#include "core/Log.h"
#include "netignite/MagicPacket.h"

namespace wake {

namespace {

uint32_t toHostOrder(const IPAddress& address) {
	return (static_cast<uint32_t>(address[0]) << 24) | (static_cast<uint32_t>(address[1]) << 16) |
	       (static_cast<uint32_t>(address[2]) << 8) | static_cast<uint32_t>(address[3]);
}

}  // namespace

bool send(const netignite::MacAddress& mac, uint16_t port, String& error) {
	const IPAddress local = WiFi.localIP();
	if (WiFi.status() != WL_CONNECTED || local == IPAddress(0, 0, 0, 0)) {
		error = "The agent has no network connection";
		return false;
	}

	const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		error = "Couldn't open a UDP socket";
		return false;
	}
	const int enable = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));

	const auto packet = netignite::buildMagicPacket(mac);
	const uint32_t targets[] = {
		netignite::directedBroadcast(toHostOrder(local), toHostOrder(WiFi.subnetMask())),
		0xffffffff,
	};

	int sent = 0;
	for (int round = 0; round < config::WOL_ROUNDS; round++) {
		if (round > 0) delay(config::WOL_ROUND_DELAY_MS);
		for (uint32_t target : targets) {
			sockaddr_in destination = {};
			destination.sin_family = AF_INET;
			destination.sin_port = htons(port);
			destination.sin_addr.s_addr = htonl(target);
			if (sendto(sock, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&destination),
			           sizeof(destination)) == static_cast<int>(packet.size())) {
				sent++;
			}
		}
	}
	close(sock);

	if (sent == 0) {
		error = "Sending the magic packet failed";
		return false;
	}
	Log::info("Wake-on-LAN sent to %s (port %u, %d packets)", netignite::formatMac(mac).c_str(), port, sent);
	return true;
}

}  // namespace wake
