#pragma once

#include <Arduino.h>

#include "netignite/Protocol.h"

/**
 * Runs STATUS checks (blocking ping/TCP/DNS) in their own FreeRTOS task, one at a time. The main
 * loop submits jobs and polls for results, so the WebSocket keeps being served meanwhile.
 */
class StatusWorker {
   public:
	struct Done {
		uint32_t requestId;
		/** Connection the request came from; results for older connections are dropped. */
		uint32_t generation;
		bool online;
		char error[96];
	};

	void begin();
	/** False if too many checks are queued. */
	bool submit(uint32_t requestId, uint32_t generation, const netignite::protocol::StatusRequest& request);
	bool poll(Done& out);

   private:
	struct Job {
		uint32_t requestId;
		uint32_t generation;
		netignite::protocol::StatusRequest request;
	};

	QueueHandle_t jobs_ = nullptr;
	QueueHandle_t results_ = nullptr;

	static void run(void* self);
};
