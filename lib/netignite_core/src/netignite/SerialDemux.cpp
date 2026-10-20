#include "netignite/SerialDemux.h"

namespace netignite {

namespace {
constexpr char HEADER[] = "IMPROV";
constexpr size_t HEADER_LENGTH = 6;
// header + version + type + length
constexpr size_t PREFIX_LENGTH = HEADER_LENGTH + 3;
}  // namespace

SerialDemux::Event SerialDemux::finishLine() {
	Event event;
	std::string line(buffer_.begin(), buffer_.end());
	buffer_.clear();
	mode_ = Mode::LineStart;

	while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
	size_t start = 0;
	while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) start++;
	line.erase(0, start);

	if (line.empty()) return event;
	event.type = Event::Type::Line;
	event.line = std::move(line);
	return event;
}

SerialDemux::Event SerialDemux::feed(uint8_t byte) {
	switch (mode_) {
		case Mode::LineStart:
			if (byte == '\n' || byte == '\r') {
				buffer_.clear();
				return {};
			}
			buffer_.push_back(byte);
			if (byte != static_cast<uint8_t>(HEADER[buffer_.size() - 1])) {
				mode_ = Mode::Text;
			} else if (buffer_.size() == HEADER_LENGTH) {
				mode_ = Mode::Improv;
			}
			return {};

		case Mode::Text:
			if (byte == '\n' || byte == '\r') return finishLine();
			if (buffer_.size() >= MAX_LINE) {
				buffer_.clear();
				mode_ = Mode::Discard;
				return {};
			}
			// Backspace / delete, for people typing in a terminal.
			if (byte == 0x08 || byte == 0x7f) {
				if (!buffer_.empty()) buffer_.pop_back();
				return {};
			}
			buffer_.push_back(byte);
			return {};

		case Mode::Discard:
			if (byte == '\n' || byte == '\r') mode_ = Mode::LineStart;
			return {};

		case Mode::Improv: {
			buffer_.push_back(byte);
			// "IMPROV" followed by anything but version 1 and a known type is just text.
			if (buffer_.size() == HEADER_LENGTH + 1 && byte != 1) {
				mode_ = Mode::Text;
				return {};
			}
			if (buffer_.size() == HEADER_LENGTH + 2 && (byte < 0x01 || byte > 0x04)) {
				mode_ = Mode::Text;
				return {};
			}
			if (buffer_.size() < PREFIX_LENGTH) return {};

			const size_t dataLength = buffer_[PREFIX_LENGTH - 1];
			const size_t total = PREFIX_LENGTH + dataLength + 1;
			if (buffer_.size() < total) return {};

			Event event;
			uint8_t checksum = 0;
			for (size_t i = 0; i + 1 < total; i++) checksum = static_cast<uint8_t>(checksum + buffer_[i]);

			if (checksum != buffer_[total - 1]) {
				event.type = Event::Type::ImprovBadChecksum;
			} else {
				event.type = Event::Type::ImprovPacket;
				event.improvType = buffer_[HEADER_LENGTH + 1];
				event.improvData.assign(buffer_.begin() + PREFIX_LENGTH, buffer_.begin() + static_cast<long>(total - 1));
			}
			buffer_.clear();
			mode_ = Mode::LineStart;
			return event;
		}
	}
	return {};
}

}  // namespace netignite
