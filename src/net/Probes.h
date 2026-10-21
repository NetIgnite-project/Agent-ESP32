#pragma once

#include <Arduino.h>

#include "netignite/Protocol.h"

/**
 * Status checks for STATUS requests. Blocking; run them from the status worker task, not the
 * main loop. A device counts as online if any requested probe answers:
 *
 * - ICMP echo (IPv4 or IPv6; link-local addresses are sent on the Wi-Fi interface)
 * - TCP connect to a port: an accepted *or refused* connection both mean the host is up
 *
 * Host names are resolved to IPv4 and IPv6 addresses (A/AAAA).
 */
namespace probes {

struct Result {
	bool online = false;
	/** Set when the check couldn't run (bad address, name doesn't resolve, no network). */
	String error;
};

Result check(const netignite::protocol::StatusRequest& request);

}  // namespace probes
