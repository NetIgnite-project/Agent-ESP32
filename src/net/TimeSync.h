#pragma once

#include <Arduino.h>

/** SNTP. TLS certificate checks need the real time, so HTTPS waits until it is synced. */
class TimeSync {
   public:
	/** Starts SNTP once the network is up. */
	void start();
	bool synced() const;

   private:
	bool started_ = false;
};
