#include "netignite/LogRing.h"

namespace netignite {

void LogRing::push(std::string_view line) {
	if (line.size() > MAX_LINE) line = line.substr(0, MAX_LINE);
	lines_.emplace_back(line);
	bytes_ += line.size();

	while (bytes_ > capacity_ && !lines_.empty()) {
		bytes_ -= lines_.front().size();
		lines_.pop_front();
		firstSeq_++;
	}
}

LogRing::ReadResult LogRing::read(uint32_t from, size_t maxBytes, std::vector<std::string>& out) const {
	ReadResult result;
	// Sequence numbers wrap after 2^32 lines; compare by distance.
	uint32_t start = from;
	if (static_cast<int32_t>(from - firstSeq_) < 0) {
		result.dropped = firstSeq_ - from;
		start = firstSeq_;
	}
	if (static_cast<int32_t>(start - nextSeq()) > 0) start = nextSeq();

	size_t used = 0;
	uint32_t seq = start;
	for (size_t i = start - firstSeq_; i < lines_.size(); i++, seq++) {
		const std::string& line = lines_[i];
		if (used > 0 && used + line.size() > maxBytes) break;
		out.push_back(line);
		used += line.size();
	}
	result.next = seq;
	return result;
}

}  // namespace netignite
