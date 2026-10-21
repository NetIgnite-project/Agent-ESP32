#pragma once

#include <cstdint>

#ifndef NETIGNITE_FW_VERSION
#define NETIGNITE_FW_VERSION "2.0.0-dev"
#endif

namespace config {

constexpr const char* FIRMWARE_NAME = "NetIgnite Agent";
constexpr const char* FIRMWARE_VERSION = NETIGNITE_FW_VERSION;

/**
 * NVS namespace and keys (max. 15 characters each). The web installer writes `server`, `enroll`
 * and `name` into an NVS partition image (the keys scripts/collect_release.py lists in
 * release.json), so these names are part of the provisioning format: change them only together
 * with the server (server/lib/firmware/provisioning.ts).
 */
namespace keys {
constexpr const char* NAMESPACE = "netignite";
constexpr const char* SERVER_URL = "server";
constexpr const char* ENROLL_TOKEN = "enroll";
constexpr const char* AGENT_ID = "agent_id";
constexpr const char* TOKEN_ID = "token_id";
constexpr const char* SECRET = "secret";
constexpr const char* WIFI_SSID = "wifi_ssid";
constexpr const char* WIFI_PASSWORD = "wifi_pass";
constexpr const char* DEVICE_NAME = "name";
constexpr const char* IP_MODE = "ip_mode";
constexpr const char* STATIC_IP = "ip";
constexpr const char* GATEWAY = "gateway";
constexpr const char* NETMASK = "netmask";
constexpr const char* DNS1 = "dns1";
constexpr const char* DNS2 = "dns2";
constexpr const char* LOG_LEVEL = "log_level";
/** Version and partition of an installed update until it confirms itself (ota/Rollback.h). */
constexpr const char* OTA_VERSION = "ota_ver";
constexpr const char* OTA_PARTITION = "ota_part";
/** The agent's SSH private keys (OpenSSH format), created on first boot (agent/SshWorker.h). */
constexpr const char* SSH_KEY_SHUTDOWN = "ssh_shutdown";
constexpr const char* SSH_KEY_REBOOT = "ssh_reboot";
}  // namespace keys

constexpr uint32_t SERIAL_BAUD = 115200;

/** Wi-Fi: reconnect when the link has been down this long (the driver also retries on its own). */
constexpr uint32_t WIFI_RECONNECT_AFTER_MS = 30000;
/** Wi-Fi: how long a new network (Improv, console) may take to connect before reverting. */
constexpr uint32_t WIFI_TRY_CONNECT_MS = 20000;

/** Server: reconnect backoff, doubled after each failed attempt. */
constexpr uint32_t RECONNECT_MIN_MS = 5000;
constexpr uint32_t RECONNECT_MAX_MS = 60000;
/** The server sends a PING every 30 s; without any frame for this long the link is dead. */
constexpr uint32_t SERVER_SILENCE_TIMEOUT_MS = 100000;

/** Enrollment: retry interval after network errors. */
constexpr uint32_t ENROLL_RETRY_MS = 30000;

/** Status checks must finish before the server's 6 s request timeout. */
constexpr uint32_t STATUS_BUDGET_MS = 4500;
constexpr uint32_t PING_TIMEOUT_MS = 1000;
constexpr uint32_t TCP_TIMEOUT_MS = 1500;

/**
 * OTA: a new firmware goes back to the previous one unless it reaches the server within this
 * time. Short in emulator builds, which can't reach a server, so the test sees the rollback.
 */
#ifdef NETIGNITE_EMULATOR
constexpr uint32_t OTA_CONFIRM_TIMEOUT_MS = 20000;
#else
constexpr uint32_t OTA_CONFIRM_TIMEOUT_MS = 5 * 60 * 1000;
#endif
/** OTA: the download needs a second TLS session; below this the server connection is closed first. */
constexpr uint32_t OTA_MIN_FREE_BLOCK = 50000;

/**
 * CONFIG_SET: a new server URL or IP setting is only saved once the agent reaches the server with
 * it; otherwise the agent restarts with the previous settings after this time.
 */
constexpr uint32_t CONFIG_TRIAL_MS = 120000;

/** Console: log lines are sent to the server in batches of at most this size, this often. */
constexpr size_t LOG_EVENT_MAX_BYTES = 6000;
constexpr uint32_t LOG_EVENT_INTERVAL_MS = 300;

/** Wake-on-LAN: rounds of packets per request, and the pause between them. */
constexpr int WOL_ROUNDS = 3;
constexpr uint32_t WOL_ROUND_DELAY_MS = 100;

}  // namespace config
