#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Improv Wi-Fi Serial (https://www.improv-wifi.com/serial/), as used by ESP Web Tools to send
 * Wi-Fi credentials right after flashing.
 *
 * Packet: "IMPROV" | version (1) | type | length | data[length] | checksum | '\n'
 * The checksum is the low byte of the sum of all bytes before it, starting at 'I'.
 */
namespace netignite::improv {

constexpr uint8_t VERSION = 1;

enum class PacketType : uint8_t {
	CurrentState = 0x01,
	ErrorState = 0x02,
	Rpc = 0x03,
	RpcResult = 0x04,
};

enum class State : uint8_t {
	Stopped = 0x00,
	AwaitingAuthorization = 0x01,
	Authorized = 0x02,
	Provisioning = 0x03,
	Provisioned = 0x04,
};

enum class Error : uint8_t {
	None = 0x00,
	InvalidRpc = 0x01,
	UnknownRpc = 0x02,
	UnableToConnect = 0x03,
	NotAuthorized = 0x04,
	BadHostname = 0x05,
	Unknown = 0xff,
};

enum class Command : uint8_t {
	WifiSettings = 0x01,
	GetCurrentState = 0x02,
	GetDeviceInfo = 0x03,
	GetWifiNetworks = 0x04,
	Hostname = 0x05,
	DeviceName = 0x06,
	GetNetworkState = 0x07,
};

struct RpcCommand {
	Command command;
	/** Only for WifiSettings. */
	std::string ssid;
	std::string password;
};

/** A complete serial packet including header, checksum and trailing newline. */
std::vector<uint8_t> buildPacket(PacketType type, const std::vector<uint8_t>& data);

std::vector<uint8_t> buildStatePacket(State state);
std::vector<uint8_t> buildErrorPacket(Error error);
/** RPC result: the strings, each prefixed with its length (truncated to 255 bytes). */
std::vector<uint8_t> buildRpcResult(Command command, const std::vector<std::string>& strings);

/**
 * Parses the data of an RPC packet: command | length | payload[length]. Returns false for a
 * malformed packet (lengths that don't add up).
 */
bool parseRpc(const std::vector<uint8_t>& data, RpcCommand& out);

}  // namespace netignite::improv
