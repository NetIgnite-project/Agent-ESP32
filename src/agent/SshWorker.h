#pragma once

#include <Arduino.h>

#include <atomic>

#include "netignite/Protocol.h"

/**
 * Runs SSH_CHECK and SSH_EXEC with libssh, one at a time, each in a task of its own (libssh needs
 * a large stack, which is only allocated while a login runs). The main loop submits jobs and polls
 * for results. Passwords and keys are wiped from memory after each job.
 */
class SshWorker {
   public:
	struct Done {
		uint32_t requestId;
		uint32_t generation;
		bool exec;
		/** SSH_EXEC succeeded / SSH_CHECK ran. Otherwise `error` says why. */
		bool ok;
		char error[200];

		// SSH_CHECK
		bool online;
		bool authenticated;
		char hostKey[64];
		/** `host_key_mismatch`, `auth_failed` or a short message; empty on success. */
		char checkError[96];
	};

	void begin();
	/** False if a login is already running or the task can't start. */
	bool submit(uint32_t requestId, uint32_t generation, bool exec, const netignite::protocol::SshRequest& request);
	bool poll(Done& out);

   private:
	struct Job {
		SshWorker* worker;
		uint32_t requestId;
		uint32_t generation;
		bool exec;
		netignite::protocol::SshRequest request;
	};

	QueueHandle_t results_ = nullptr;
	std::atomic<bool> busy_{false};

	static void run(void* job);
	static void login(const Job& job, Done& done);
};
