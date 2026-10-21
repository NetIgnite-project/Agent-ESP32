#include "agent/SshWorker.h"

#include <libssh/libssh.h>
#include <libssh_esp32.h>

#include <string>

#include "core/Log.h"

namespace protocol = netignite::protocol;

namespace {

constexpr uint32_t TASK_STACK = 40 * 1024;
/** Creating and exporting the keys on first boot needs more than the main loop's stack. */
constexpr uint32_t KEY_TASK_STACK = 24 * 1024;
constexpr long CONNECT_TIMEOUT_S = 5;
/** SSH_POWER: how long to wait for the command's exit status (shutdown usually drops the link). */
constexpr uint32_t EXEC_WAIT_MS = 5000;
constexpr uint32_t MINUTE_MS = 60000;

/** Closes the session on every path out of a login. */
struct Session {
	ssh_session session = ssh_new();
	~Session() {
		if (!session) return;
		if (ssh_is_connected(session)) ssh_disconnect(session);
		ssh_free(session);
	}
};

struct KeySetup {
	SshWorker* worker;
	Settings* settings;
	SemaphoreHandle_t done;
};

/** Overwrites an exported private key before its memory is freed. */
void freeSecret(char* text) {
	if (!text) return;
	for (volatile char* p = text; *p; p++) *p = 0;
	ssh_string_free_char(text);
}

/** `ssh-ed25519 <base64>` of a key's public half. */
std::string publicKeyLine(ssh_key key) {
	char* base64 = nullptr;
	if (ssh_pki_export_pubkey_base64(key, &base64) != SSH_OK || !base64) return {};
	std::string line = std::string(ssh_key_type_to_char(ssh_key_type(key))) + " " + base64;
	ssh_string_free_char(base64);
	return line;
}

std::string fingerprintOf(ssh_key key) {
	unsigned char* hash = nullptr;
	size_t length = 0;
	if (ssh_get_publickey_hash(key, SSH_PUBLICKEY_HASH_SHA256, &hash, &length) != SSH_OK || length != 32) {
		ssh_clean_pubkey_hash(&hash);
		return {};
	}
	std::string fingerprint = protocol::sshFingerprint(hash);
	ssh_clean_pubkey_hash(&hash);
	return fingerprint;
}

}  // namespace

void SshWorker::begin(Settings& settings) {
	results_ = xQueueCreate(2, sizeof(Done));
	libssh_begin();

	KeySetup setup{this, &settings, xSemaphoreCreateBinary()};
	if (!setup.done || xTaskCreate(setupTask, "sshkeys", KEY_TASK_STACK, &setup, 1, nullptr) != pdPASS) {
		Log::error("SSH: not enough memory to set up the keys; SSH is off");
	} else {
		xSemaphoreTake(setup.done, portMAX_DELAY);
	}
	if (setup.done) vSemaphoreDelete(setup.done);
}

void SshWorker::setupTask(void* arg) {
	auto* setup = static_cast<KeySetup*>(arg);
	setup->worker->setupKeys(*setup->settings);
	xSemaphoreGive(setup->done);
	vTaskDelete(nullptr);
}

void SshWorker::setupKeys(Settings& settings) {
	const String* stored[2] = {&settings.sshKeyShutdown(), &settings.sshKeyReboot()};
	bool loaded = true;
	for (size_t i = 0; i < 2; i++) {
		loaded = loaded && stored[i]->length() > 0 &&
		         ssh_pki_import_privkey_base64(stored[i]->c_str(), nullptr, nullptr, nullptr, &keys_[i]) == SSH_OK &&
		         ssh_key_type(keys_[i]) == SSH_KEYTYPE_ED25519;
	}

	if (!loaded) {
		for (auto& key : keys_) {
			if (key) ssh_key_free(key);
			key = nullptr;
		}
		char* exported[2] = {nullptr, nullptr};
		bool created = true;
		for (size_t i = 0; i < 2 && created; i++) {
			created = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &keys_[i]) == SSH_OK &&
			          ssh_pki_export_privkey_base64(keys_[i], nullptr, nullptr, nullptr, &exported[i]) == SSH_OK;
		}
		if (created) {
			settings.setSshKeys(exported[0], exported[1]);
			Log::info("SSH: created the agent's keys");
		}
		for (char* text : exported) freeSecret(text);
		if (!created) {
			for (auto& key : keys_) {
				if (key) ssh_key_free(key);
				key = nullptr;
			}
			Log::error("SSH: couldn't create the agent's keys; SSH is off");
			return;
		}
	}

	for (size_t i = 0; i < 2; i++) {
		publicKeys_[i] = publicKeyLine(keys_[i]);
		fingerprints_[i] = fingerprintOf(keys_[i]);
	}
	if (publicKeys_[0].empty() || publicKeys_[1].empty()) {
		for (auto& key : keys_) {
			ssh_key_free(key);
			key = nullptr;
		}
		Log::error("SSH: can't read the agent's keys; SSH is off");
	}
}

bool SshWorker::takeLogins(size_t count) {
	const uint32_t now = millis();
	size_t kept = 0;
	for (size_t i = 0; i < loginCount_; i++) {
		if (now - loginTimes_[i] < MINUTE_MS) loginTimes_[kept++] = loginTimes_[i];
	}
	loginCount_ = kept;
	if (loginCount_ + count > MAX_LOGINS_PER_MINUTE) return false;
	for (size_t i = 0; i < count; i++) loginTimes_[loginCount_++] = now;
	return true;
}

SshWorker::Submit SshWorker::submit(uint32_t requestId, uint32_t generation, bool power,
                                    const protocol::SshRequest& request) {
	// Only the main loop submits; the task only clears `busy_`.
	if (busy_) return Submit::Busy;
	if (!takeLogins(power ? 1 : 2)) return Submit::TooManyLogins;
	busy_ = true;

	auto* job = new Job{this, requestId, generation, power, request};
	if (xTaskCreate(run, "ssh", TASK_STACK, job, 1, nullptr) != pdPASS) {
		delete job;
		busy_ = false;
		return Submit::Busy;
	}
	return Submit::Started;
}

bool SshWorker::poll(Done& out) {
	return results_ && xQueueReceive(results_, &out, 0) == pdTRUE;
}

void SshWorker::run(void* arg) {
	auto* job = static_cast<Job*>(arg);
	SshWorker* worker = job->worker;

	Done done = {};
	done.requestId = job->requestId;
	done.generation = job->generation;
	done.power = job->power;
	if (job->power) {
		worker->power(*job, done);
	} else {
		worker->check(*job, done);
	}
	delete job;

	xQueueSend(worker->results_, &done, portMAX_DELAY);
	worker->busy_ = false;
	vTaskDelete(nullptr);
}

SshWorker::Login SshWorker::login(ssh_session session, const protocol::SshRequest& request,
                                  const std::string& expectedHostKey, ssh_key key, std::string& hostKey,
                                  std::string& error) {
	int port = request.port;
	long timeout = CONNECT_TIMEOUT_S;
	bool processConfig = false;
	ssh_options_set(session, SSH_OPTIONS_HOST, request.host.c_str());
	ssh_options_set(session, SSH_OPTIONS_PORT, &port);
	ssh_options_set(session, SSH_OPTIONS_USER, request.username.c_str());
	ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
	ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &processConfig);

	if (ssh_connect(session) != SSH_OK) {
		error = std::string("Can't connect: ") + ssh_get_error(session);
		return Login::Unreachable;
	}

	// The host key, before the agent's key is offered.
	ssh_key serverKey = nullptr;
	unsigned char* hash = nullptr;
	size_t hashLength = 0;
	if (ssh_get_server_publickey(session, &serverKey) != SSH_OK ||
	    ssh_get_publickey_hash(serverKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLength) != SSH_OK || hashLength != 32) {
		ssh_key_free(serverKey);
		ssh_clean_pubkey_hash(&hash);
		error = "Can't read the device's host key";
		return Login::Failed;
	}
	hostKey = protocol::sshFingerprint(hash);
	ssh_clean_pubkey_hash(&hash);
	ssh_key_free(serverKey);

	if (!expectedHostKey.empty() && hostKey != expectedHostKey) {
		Log::warn("SSH: %s presented another host key (%s)", request.host.c_str(), hostKey.c_str());
		return Login::HostKeyMismatch;
	}

	const int auth = ssh_userauth_publickey(session, nullptr, key);
	if (auth == SSH_AUTH_SUCCESS) return Login::Accepted;
	if (auth == SSH_AUTH_DENIED || auth == SSH_AUTH_PARTIAL) return Login::Refused;
	error = std::string("Login failed: ") + ssh_get_error(session);
	return Login::Failed;
}

void SshWorker::check(const Job& job, Done& done) {
	const protocol::SshRequest& request = job.request;
	Log::info("SSH check: %s@%s:%u", request.username.c_str(), request.host.c_str(), request.port);

	// The second login must meet the same device as the first.
	std::string expected = request.hostKey;
	for (const Action action : {Action::Shutdown, Action::Reboot}) {
		Session ssh;
		if (!ssh.session) {
			done.ok = false;
			strlcpy(done.error, "Not enough memory for SSH", sizeof(done.error));
			return;
		}
		std::string hostKey, error;
		const Login result = login(ssh.session, request, expected, keys_[index(action)], hostKey, error);
		if (!hostKey.empty()) strlcpy(done.hostKey, hostKey.c_str(), sizeof(done.hostKey));
		done.ok = true;

		switch (result) {
			case Login::Unreachable:
				// The SSH port didn't take a connection: the device is offline (for this check).
				if (action == Action::Reboot) strlcpy(done.checkError, "The device stopped answering", sizeof(done.checkError));
				return;
			case Login::HostKeyMismatch:
				done.online = true;
				strlcpy(done.checkError, "host_key_mismatch", sizeof(done.checkError));
				return;
			case Login::Failed:
				done.online = true;
				strlcpy(done.checkError, error.c_str(), sizeof(done.checkError));
				return;
			case Login::Refused:
				break;
			case Login::Accepted:
				// Logged in only: no session, so the key's forced command doesn't run.
				(action == Action::Shutdown ? done.shutdownAccepted : done.rebootAccepted) = true;
				break;
		}
		done.online = true;
		if (expected.empty()) expected = hostKey;
	}
}

void SshWorker::power(const Job& job, Done& done) {
	const protocol::SshRequest& request = job.request;
	auto fail = [&done](const std::string& message) {
		done.ok = false;
		strlcpy(done.error, message.c_str(), sizeof(done.error));
	};

	const std::string command = protocol::sshCommand(request);
	Log::info("SSH: running \"%s\" on %s@%s:%u", command.c_str(), request.username.c_str(), request.host.c_str(),
	          request.port);

	Session ssh;
	if (!ssh.session) return fail("Not enough memory for SSH");

	std::string hostKey, error;
	switch (login(ssh.session, request, request.hostKey, keys_[index(request.action)], hostKey, error)) {
		case Login::Unreachable:
		case Login::Failed:
			return fail(error);
		case Login::HostKeyMismatch:
			return fail("host key changed");
		case Login::Refused:
			return fail(std::string("the device didn't accept the agent's ") +
			            (request.action == Action::Shutdown ? "shutdown" : "reboot") + " key");
		case Login::Accepted:
			break;
	}

	ssh_channel channel = ssh_channel_new(ssh.session);
	if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
		if (channel) ssh_channel_free(channel);
		return fail("The device refused a session");
	}
	if (ssh_channel_request_exec(channel, command.c_str()) != SSH_OK) {
		ssh_channel_free(channel);
		return fail("The device refused the command");
	}

	// Wait for the exit status. Shutdown often drops the connection first: that counts as success.
	std::string stderrText;
	char buffer[128];
	bool eof = false;
	const uint32_t start = millis();
	while (millis() - start < EXEC_WAIT_MS) {
		const int err = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 1, 200);
		if (err > 0 && stderrText.size() < 200) stderrText.append(buffer, std::min<size_t>(err, 200 - stderrText.size()));
		const int out = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 50);
		if (err == SSH_ERROR || out == SSH_ERROR || !ssh_is_connected(ssh.session)) break;
		if (ssh_channel_is_eof(channel)) {
			eof = true;
			break;
		}
	}

	uint32_t exitCode = 0;
	const bool exited = eof && ssh_channel_get_exit_state(channel, &exitCode, nullptr, nullptr) == SSH_OK;
	ssh_channel_free(channel);

	if (exited && exitCode != 0) {
		while (!stderrText.empty() && (stderrText.back() == '\n' || stderrText.back() == '\r')) stderrText.pop_back();
		const std::string message =
			"Exit code " + std::to_string(exitCode) + (stderrText.empty() ? std::string() : ": " + stderrText);
		Log::warn("SSH: %s failed: %s", command.c_str(), message.c_str());
		return fail(message);
	}
	Log::info("SSH: ran \"%s\" on %s", command.c_str(), request.host.c_str());
	done.ok = true;
}
