#include "net/TimeSync.h"

#include <ctime>

#include "core/Log.h"

void TimeSync::start() {
	if (started_) return;
	started_ = true;
	configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
	Log::info("Time sync started");
}

bool TimeSync::synced() const {
	// Any time after 2024-01-01 means SNTP has answered.
	return time(nullptr) > 1704067200;
}
