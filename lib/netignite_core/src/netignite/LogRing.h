#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace netignite {

/**
 * The most recent log lines, up to a byte budget, for the dashboard's console. Every line gets a
 * sequence number, so a reader can continue where it stopped and learn how many lines it missed.
 * Not thread-safe; the caller locks.
 */
class LogRing {
   public:
	/** Longer lines are cut. */
	static constexpr size_t MAX_LINE = 240;

	explicit LogRing(size_t capacityBytes) : capacity_(capacityBytes) {}

	void push(std::string_view line);

	/** Sequence number the next pushed line gets. */
	uint32_t nextSeq() const { return firstSeq_ + static_cast<uint32_t>(lines_.size()); }

	struct ReadResult {
		/** Where to continue reading. */
		uint32_t next = 0;
		/** Lines between `from` and the oldest stored line that were already dropped. */
		uint32_t dropped = 0;
	};

	/** Appends lines from sequence `from` on to `out`, at most `maxBytes` of text. */
	ReadResult read(uint32_t from, size_t maxBytes, std::vector<std::string>& out) const;

	size_t bytes() const { return bytes_; }

   private:
	size_t capacity_;
	size_t bytes_ = 0;
	uint32_t firstSeq_ = 0;
	std::deque<std::string> lines_;
};

}  // namespace netignite
