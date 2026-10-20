#include "netignite/Improv.h"

namespace netignite::improv {

std::vector<uint8_t> buildPacket(PacketType type, const std::vector<uint8_t>& data) {
	std::vector<uint8_t> packet = {'I', 'M', 'P', 'R', 'O', 'V', VERSION, static_cast<uint8_t>(type),
	                               static_cast<uint8_t>(data.size())};
	packet.insert(packet.end(), data.begin(), data.end());

	uint8_t checksum = 0;
	for (uint8_t byte : packet) checksum = static_cast<uint8_t>(checksum + byte);
	packet.push_back(checksum);
	packet.push_back('\n');
	return packet;
}

std::vector<uint8_t> buildStatePacket(State state) {
	return buildPacket(PacketType::CurrentState, {static_cast<uint8_t>(state)});
}

std::vector<uint8_t> buildErrorPacket(Error error) {
	return buildPacket(PacketType::ErrorState, {static_cast<uint8_t>(error)});
}

std::vector<uint8_t> buildRpcResult(Command command, const std::vector<std::string>& strings) {
	std::vector<uint8_t> data = {static_cast<uint8_t>(command), 0};
	for (const std::string& text : strings) {
		const size_t length = text.size() > 255 ? 255 : text.size();
		// The whole result must fit the packet's one-byte length.
		if (data.size() + 1 + length > 255) break;
		data.push_back(static_cast<uint8_t>(length));
		data.insert(data.end(), text.begin(), text.begin() + static_cast<long>(length));
	}
	data[1] = static_cast<uint8_t>(data.size() - 2);
	return buildPacket(PacketType::RpcResult, data);
}

bool parseRpc(const std::vector<uint8_t>& data, RpcCommand& out) {
	if (data.size() < 2) return false;

	const uint8_t length = data[1];
	if (static_cast<size_t>(length) + 2 != data.size()) return false;

	out = RpcCommand{static_cast<Command>(data[0]), {}, {}};
	if (out.command != Command::WifiSettings) return true;

	// payload: ssid length | ssid | password length | password
	size_t pos = 2;
	if (pos >= data.size()) return false;
	const size_t ssidLength = data[pos++];
	// The SSID and the password's length byte must fit.
	if (pos + ssidLength + 1 > data.size()) return false;
	out.ssid.assign(data.begin() + static_cast<long>(pos), data.begin() + static_cast<long>(pos + ssidLength));
	pos += ssidLength;

	const size_t passwordLength = data[pos++];
	if (pos + passwordLength != data.size()) return false;
	out.password.assign(data.begin() + static_cast<long>(pos), data.end());

	return !out.ssid.empty();
}

}  // namespace netignite::improv
