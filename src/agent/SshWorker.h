#pragma once

#include <Arduino.h>

#include <atomic>
#include <string>

#include "core/Settings.h"
#include "netignite/Protocol.h"

struct ssh_key_struct;
struct ssh_session_struct;

/**
 * Runs SSH_CHECK and SSH_POWER with libssh, one at a time, each in a task of its own (libssh
 * needs a large stack, which is only allocated while a login runs). The main loop submits jobs
 * and polls for results.
 *
 * The agent logs in with two Ed25519 keys of its own, one for shutdown and one for reboot. They
 * are created on first boot and kept in NVS; only the public keys leave the device (HELLO
 * `ssh_keys`). The device allows each key with a forced command, so a key can do exactly its
 * action. There are no passwords and no credentials from the server.
 */
class SshWorker {
   public:
	using Action = netignite::protocol::SshRequest::Action;

	/** SSH logins (one per connection) per minute, at most. SSH_CHECK takes two. */
	static constexpr size_t MAX_LOGINS_PER_MINUTE = 10;

	struct Done {
		uint32_t requestId;
		uint32_t generation;
		bool power;
		/** SSH_POWER succeeded / SSH_CHECK ran. Otherwise `error` says why. */
		bool ok;
		char error[200];

		// SSH_CHECK
		bool online;
		/** Whether the device accepted the shutdown key / the reboot key. */
		bool shutdownAccepted;
		bool rebootAccepted;
		char hostKey[64];
		/**
		 * `host_key_mismatch` or a short message; empty otherwise. Also set while offline when the
		 * host name doesn't resolve.
		 */
		char checkError[96];
	};

	enum class Submit { Started, Busy, TooManyLogins, OutOfMemory };

	/** Loads the agent's keys from the settings, or creates and stores new ones. */
	void begin(Settings& settings);
	bool ready() const { return keys_[0] != nullptr && keys_[1] != nullptr; }
	/** `ssh-ed25519 <base64>` of an action's key, for HELLO. */
	const std::string& publicKey(Action action) const { return publicKeys_[index(action)]; }
	/** `SHA256:...` of an action's key, for the serial console. */
	const std::string& fingerprint(Action action) const { return fingerprints_[index(action)]; }

	Submit submit(uint32_t requestId, uint32_t generation, bool power, const netignite::protocol::SshRequest& request);
	bool poll(Done& out);

   private:
	struct Job {
		SshWorker* worker;
		uint32_t requestId;
		uint32_t generation;
		bool power;
		netignite::protocol::SshRequest request;
	};
	/**
	 * How a login ended. NoAddress and Unreachable: no TCP connection (the host name doesn't
	 * resolve / the port didn't take one), so the device counts as offline. Failed: the device
	 * answered, but the SSH handshake or the login didn't work for another reason than the key.
	 */
	enum class Login { NoAddress, Unreachable, HostKeyMismatch, Failed, Refused, Accepted };

	QueueHandle_t results_ = nullptr;
	std::atomic<bool> busy_{false};
	ssh_key_struct* keys_[2] = {nullptr, nullptr};
	std::string publicKeys_[2];
	std::string fingerprints_[2];
	/** When the logins of the last minute started (millis), oldest first. */
	uint32_t loginTimes_[MAX_LOGINS_PER_MINUTE] = {};
	size_t loginCount_ = 0;

	static size_t index(Action action) { return action == Action::Shutdown ? 0 : 1; }
	/** Whether `count` more logins fit into the last minute's limit (doesn't take them). */
	bool loginsAvailable(size_t count);
	/** Counts `count` logins that start now; only once their job really started. */
	void recordLogins(size_t count);
	void setupKeys(Settings& settings);
	/** Frees both keys: SSH is off (ready() is false) from then on. */
	void freeKeys();

	static void run(void* job);
	static void setupTask(void* arg);
	void check(const Job& job, Done& done);
	void power(const Job& job, Done& done);
	/**
	 * Connects, checks the host key and authenticates with `key`, all before `deadline` (millis);
	 * `session` stays connected.
	 */
	Login login(ssh_session_struct* session, const netignite::protocol::SshRequest& request, const std::string& expectedHostKey,
	            ssh_key_struct* key, uint32_t deadline, std::string& hostKey, std::string& error);
};
