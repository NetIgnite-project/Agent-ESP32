#include "net/Probes.h"

#include <esp_netif.h>
#include <lwip/inet.h>
#include <lwip/ip_addr.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <ping/ping_sock.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "Config.h"
#include "core/Log.h"
#include "netignite/Mac.h"

using netignite::protocol::StatusRequest;

namespace probes {

namespace {

int staNetifIndex() {
	esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	return netif ? esp_netif_get_netif_impl_index(netif) : 0;
}

bool isLinkLocal(const ip_addr_t& address) {
	return IP_IS_V6_VAL(address) && ip6_addr_islinklocal(ip_2_ip6(&address));
}

/** Link-local IPv6 addresses only make sense on the Wi-Fi interface. */
void scopeToWifi(ip_addr_t& address) {
	if (isLinkLocal(address)) ip6_addr_set_zone(ip_2_ip6(&address), static_cast<u8_t>(staNetifIndex()));
}

String formatAddress(const ip_addr_t& address) {
	char text[IPADDR_STRLEN_MAX];
	ipaddr_ntoa_r(&address, text, sizeof(text));
	return text;
}

bool resolve(const StatusRequest& request, std::vector<ip_addr_t>& out, String& error) {
	if (request.target == StatusRequest::Target::LinkLocal) {
		const netignite::IPv6Bytes bytes = netignite::linkLocalFromMac(request.mac);
		ip_addr_t address = {};
		IP_SET_TYPE_VAL(address, IPADDR_TYPE_V6);
		std::memcpy(ip_2_ip6(&address)->addr, bytes.data(), bytes.size());
		scopeToWifi(address);
		out.push_back(address);
		return true;
	}

	if (request.target == StatusRequest::Target::Ip) {
		ip_addr_t address = {};
		if (!ipaddr_aton(request.address.c_str(), &address)) {
			error = "Invalid IP address";
			return false;
		}
		scopeToWifi(address);
		out.push_back(address);
		return true;
	}

	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	addrinfo* results = nullptr;
	if (getaddrinfo(request.address.c_str(), nullptr, &hints, &results) != 0 || !results) {
		error = "Host name doesn't resolve";
		return false;
	}

	for (addrinfo* entry = results; entry && out.size() < 2; entry = entry->ai_next) {
		ip_addr_t address = {};
		if (entry->ai_family == AF_INET) {
			const auto* sin = reinterpret_cast<const sockaddr_in*>(entry->ai_addr);
			IP_SET_TYPE_VAL(address, IPADDR_TYPE_V4);
			inet_addr_to_ip4addr(ip_2_ip4(&address), &sin->sin_addr);
		} else if (entry->ai_family == AF_INET6) {
			const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(entry->ai_addr);
			IP_SET_TYPE_VAL(address, IPADDR_TYPE_V6);
			inet6_addr_to_ip6addr(ip_2_ip6(&address), &sin6->sin6_addr);
			scopeToWifi(address);
		} else {
			continue;
		}
		out.push_back(address);
	}
	freeaddrinfo(results);

	if (out.empty()) {
		error = "Host name doesn't resolve";
		return false;
	}
	return true;
}

// --- ICMP ---------------------------------------------------------------------------------------

/**
 * Shared between the caller and the ping session's callbacks. The ping task may finish after the
 * caller gave up waiting, so whoever finishes last frees it.
 */
struct PingContext {
	SemaphoreHandle_t done = xSemaphoreCreateBinary();
	std::atomic<bool> replied{false};
	/** 0 = running, 1 = ended, 2 = abandoned by the caller */
	std::atomic<int> state{0};
	esp_ping_handle_t session = nullptr;

	~PingContext() { vSemaphoreDelete(done); }
};

void onPingSuccess(esp_ping_handle_t, void* arg) {
	static_cast<PingContext*>(arg)->replied = true;
}

void onPingEnd(esp_ping_handle_t, void* arg) {
	auto* context = static_cast<PingContext*>(arg);
	if (context->state.exchange(1) == 2) {
		// The caller stopped waiting: clean up here.
		esp_ping_delete_session(context->session);
		delete context;
		return;
	}
	xSemaphoreGive(context->done);
}

bool ping(const ip_addr_t& address, uint32_t timeoutMs) {
	auto* context = new PingContext();

	esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
	config.target_addr = address;
	config.count = 1;
	config.timeout_ms = timeoutMs;
	if (isLinkLocal(address)) config.interface = static_cast<uint32_t>(staNetifIndex());

	esp_ping_callbacks_t callbacks = {};
	callbacks.cb_args = context;
	callbacks.on_ping_success = onPingSuccess;
	callbacks.on_ping_end = onPingEnd;

	if (esp_ping_new_session(&config, &callbacks, &context->session) != ESP_OK) {
		delete context;
		return false;
	}
	if (esp_ping_start(context->session) != ESP_OK) {
		esp_ping_delete_session(context->session);
		delete context;
		return false;
	}

	const bool ended = xSemaphoreTake(context->done, pdMS_TO_TICKS(timeoutMs + 1500)) == pdTRUE;
	const bool replied = context->replied;

	if (ended || context->state.exchange(2) == 1) {
		// Ended normally (or just now): the session is ours to delete.
		if (!ended) xSemaphoreTake(context->done, portMAX_DELAY);
		esp_ping_delete_session(context->session);
		delete context;
	}
	return replied;
}

// --- TCP ----------------------------------------------------------------------------------------

bool tcpConnect(const ip_addr_t& address, uint16_t port, uint32_t timeoutMs) {
	sockaddr_storage storage = {};
	socklen_t length = 0;
	int family = AF_INET;

	if (IP_IS_V6_VAL(address)) {
		family = AF_INET6;
		auto* sin6 = reinterpret_cast<sockaddr_in6*>(&storage);
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = htons(port);
		inet6_addr_from_ip6addr(&sin6->sin6_addr, ip_2_ip6(&address));
		sin6->sin6_scope_id = ip6_addr_zone(ip_2_ip6(&address));
		length = sizeof(sockaddr_in6);
	} else {
		auto* sin = reinterpret_cast<sockaddr_in*>(&storage);
		sin->sin_family = AF_INET;
		sin->sin_port = htons(port);
		inet_addr_from_ip4addr(&sin->sin_addr, ip_2_ip4(&address));
		length = sizeof(sockaddr_in);
	}

	const int sock = socket(family, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) return false;
	fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

	bool up = false;
	if (connect(sock, reinterpret_cast<sockaddr*>(&storage), length) == 0) {
		up = true;
	} else if (errno == EINPROGRESS) {
		fd_set writable;
		FD_ZERO(&writable);
		FD_SET(sock, &writable);
		timeval timeout = {static_cast<long>(timeoutMs / 1000), static_cast<long>((timeoutMs % 1000) * 1000)};
		if (select(sock + 1, nullptr, &writable, nullptr, &timeout) > 0) {
			int error = 0;
			socklen_t errorLength = sizeof(error);
			getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &errorLength);
			// A refused (reset) connection still proves the host is up.
			up = error == 0 || error == ECONNREFUSED || error == ECONNRESET;
		}
	} else {
		up = errno == ECONNREFUSED || errno == ECONNRESET;
	}

	close(sock);
	return up;
}

}  // namespace

Result check(const StatusRequest& request) {
	Result result;
	std::vector<ip_addr_t> addresses;
	if (!resolve(request, addresses, result.error)) return result;

	const uint32_t start = millis();
	auto remaining = [&]() -> int32_t {
		return static_cast<int32_t>(config::STATUS_BUDGET_MS) - static_cast<int32_t>(millis() - start);
	};

	for (const ip_addr_t& address : addresses) {
		if (request.icmp) {
			// Two echoes: the first one often only triggers ARP/NDP resolution.
			for (int attempt = 0; attempt < 2 && remaining() > static_cast<int32_t>(config::PING_TIMEOUT_MS); attempt++) {
				if (ping(address, config::PING_TIMEOUT_MS)) {
					Log::debug("Status: %s answers ping", formatAddress(address).c_str());
					result.online = true;
					return result;
				}
			}
		}
		if (request.tcpPort != 0 && remaining() > 200) {
			const uint32_t timeout = min<uint32_t>(config::TCP_TIMEOUT_MS, static_cast<uint32_t>(remaining()));
			if (tcpConnect(address, request.tcpPort, timeout)) {
				Log::debug("Status: %s answers on TCP port %u", formatAddress(address).c_str(), request.tcpPort);
				result.online = true;
				return result;
			}
		}
		if (remaining() <= 200) break;
	}

	Log::debug("Status: %s doesn't answer", formatAddress(addresses.front()).c_str());
	return result;
}

}  // namespace probes
