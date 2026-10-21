#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace netignite {

/**
 * Splits the serial input into Improv packets and text lines (the local console), which share
 * the UART. A packet is recognized when a line starts with "IMPROV"; it is read by its length,
 * because its data may contain newline bytes.
 */
class SerialDemux {
   public:
	struct Event {
		enum class Type { None, Line, ImprovPacket, ImprovBadChecksum };
		Type type = Type::None;
		/** Line, without the line ending. */
		std::string line;
		/** Improv packet type and data (without header and checksum). */
		uint8_t improvType = 0;
		std::vector<uint8_t> improvData;
	};

	static constexpr size_t MAX_LINE = 512;

	Event feed(uint8_t byte);

   private:
	enum class Mode { LineStart, Text, Improv, Discard };

	Mode mode_ = Mode::LineStart;
	std::vector<uint8_t> buffer_;

	Event finishLine();
};

}  // namespace netignite
