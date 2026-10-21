#pragma once

#include <Arduino.h>

#include <string>

#include "netignite/Protocol.h"
#include "netignite/ServerUrl.h"
#include "ota/OtaWriter.h"

/**
 * Runs an OTA update in its own task: downloads the image from the agent's server (with the agent
 * credential) into an OtaWriter. The main loop polls progress() and reports it to the server.
 */
class OtaUpdater {
   public:
	enum class State : uint8_t { Idle, Downloading, Installed, Failed };

	struct Progress {
		State state = State::Idle;
		uint8_t percent = 0;
		/** Changes whenever state or percent change. */
		uint32_t revision = 0;
		std::string version;
		std::string error;
	};

	static const char* stateName(State state);

	/** Starts the download task. False (with an error) if an update is running or can't start. */
	bool start(const netignite::ServerUrl& server, const std::string& authorization,
	           const netignite::protocol::OtaRequest& request, std::string& error);

	bool busy() const;
	Progress progress() const;

   private:
	netignite::ServerUrl server_;
	std::string authorization_;
	netignite::protocol::OtaRequest request_;
	OtaWriter writer_;

	mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
	Progress progress_;
	TaskHandle_t task_ = nullptr;

	static void taskMain(void* self);
	void run();
	void setProgress(State state, uint8_t percent, const char* error = nullptr);
};
