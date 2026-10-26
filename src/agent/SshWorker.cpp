#include "agent/SshWorker.h"

#include <esp_heap_caps.h>
#include <libssh/libssh.h>
#include <libssh_esp32.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#include "core/Log.h"

namespace protocol = netignite::protocol;

namespace {

constexpr uint32_t TASK_STACK = 40 * 1024;
/** Creating and exporting the keys on first boot needs more than the main loop's stack. */
constexpr uint32_t KEY_TASK_STACK = 24 * 1024;
/**
 * A job only starts with this much free heap in one block beyond its task's stack: copying the job's
 * strings can't report a failed allocation, it would abort the firmware.
 */
constexpr uint32_t JOB_HEAP_MARGIN = 4 * 1024;
/** Connecting, up to and including the key exchange. */
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
/** SSH_CHECK: one login, connect to answer; two of them fit the server's 25 s. */
constexpr uint32_t LOGIN_BUDGET_MS = 10000;
/** SSH_POWER: the login, the command and the wait for its exit status; the server waits 15 s. */
constexpr uint32_t POWER_BUDGET_MS = 12000;
/** SSH_POWER: how long to wait for the command's exit status (shutdown usually drops the link). */
constexpr uint32_t EXEC_WAIT_MS = 5000;
/** The exit status can follow the end of the output; this long at least for it to arrive. */
constexpr uint32_t EXIT_STATUS_WAIT_MS = 500;
/** How much of the command's output goes into a failed SSH_POWER's answer. */
constexpr size_t OUTPUT_BYTES = 200;
constexpr uint32_t MINUTE_MS = 60000;
/** The keys' names for log lines, in index order. */
constexpr const char* KEY_NAMES[2] = {"shutdown", "reboot"};

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

/** A stored private key, or null if it isn't a usable Ed25519 key. */
ssh_key importKey(const String& text) {
	ssh_key key = nullptr;
	if (text.length() == 0 || ssh_pki_import_privkey_base64(text.c_str(), nullptr, nullptr, nullptr, &key) != SSH_OK) {
		return nullptr;
	}
	if (ssh_key_type(key) == SSH_KEYTYPE_ED25519) return key;
	ssh_key_free(key);
	return nullptr;
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

/**
 * `text` into a fixed field of Done: one line of valid UTF-8, cut without splitting a character.
 * The device's error output can be in any encoding, and the server drops a connection that sends
 * invalid UTF-8.
 */
template <size_t N>
void setText(char (&field)[N], std::string_view text) {
	strlcpy(field, protocol::oneLine(text, N - 1).c_str(), N);
}

/** Milliseconds until `deadline` (millis), 0 once it has passed. */
uint32_t remaining(uint32_t deadline) {
	const int32_t left = static_cast<int32_t>(deadline - millis());
	return left > 0 ? static_cast<uint32_t>(left) : 0;
}

/**
 * The longest libssh's next blocking step (connect, login, a channel request) may take. At least
 * 1 ms: with 0, libssh waits forever.
 */
void setWait(ssh_session session, uint32_t ms) {
	ms = std::max<uint32_t>(ms, 1);
	long seconds = ms / 1000;
	long micros = (ms % 1000) * 1000;
	ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &seconds);
	ssh_options_set(session, SSH_OPTIONS_TIMEOUT_USEC, &micros);
}

/** Whether libssh's connect error says the host name doesn't resolve (connect.c). */
bool unresolved(const char* error) {
	static constexpr char PREFIX[] = "Failed to resolve hostname";
	return error && std::strncmp(error, PREFIX, sizeof(PREFIX) - 1) == 0;
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
	char* exported[2] = {nullptr, nullptr};
	bool ok = true;
	for (size_t i = 0; i < 2 && ok; i++) {
		keys_[i] = importKey(*stored[i]);
		if (keys_[i]) continue;
		// Missing on first boot. A stored key that can't be used is replaced on its own, so the
		// other key's line on the devices keeps working.
		if (stored[i]->length() > 0) {
			Log::warn("SSH: the stored %s key can't be used; creating a new one (update its line on the devices)",
			          KEY_NAMES[i]);
		}
		ok = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &keys_[i]) == SSH_OK &&
		     ssh_pki_export_privkey_base64(keys_[i], nullptr, nullptr, nullptr, &exported[i]) == SSH_OK;
	}

	if (!ok) {
		Log::error("SSH: couldn't create the agent's keys; SSH is off");
	} else if (exported[0] || exported[1]) {
		// Keys that aren't stored would change with the next boot, and the lines on the devices
		// would stop working: SSH stays off instead.
		ok = settings.setSshKeys(exported[0] ? String(exported[0]) : *stored[0],
		                         exported[1] ? String(exported[1]) : *stored[1]);
		if (ok) {
			Log::info("SSH: created the agent's keys");
		} else {
			Log::error("SSH: couldn't store the agent's keys in flash; SSH is off");
		}
	}
	for (char* text : exported) freeSecret(text);
	if (!ok) {
		freeKeys();
		return;
	}

	for (size_t i = 0; i < 2; i++) {
		publicKeys_[i] = publicKeyLine(keys_[i]);
		fingerprints_[i] = fingerprintOf(keys_[i]);
	}
	if (publicKeys_[0].empty() || publicKeys_[1].empty()) {
		freeKeys();
		Log::error("SSH: can't read the agent's keys; SSH is off");
	}
}

void SshWorker::freeKeys() {
	for (auto& key : keys_) {
		if (key) ssh_key_free(key);
		key = nullptr;
	}
}

bool SshWorker::loginsAvailable(size_t count) {
	const uint32_t now = millis();
	size_t kept = 0;
	for (size_t i = 0; i < loginCount_; i++) {
		if (now - loginTimes_[i] < MINUTE_MS) loginTimes_[kept++] = loginTimes_[i];
	}
	loginCount_ = kept;
	return loginCount_ + count <= MAX_LOGINS_PER_MINUTE;
}

void SshWorker::recordLogins(size_t count) {
	const uint32_t now = millis();
	for (size_t i = 0; i < count && loginCount_ < MAX_LOGINS_PER_MINUTE; i++) loginTimes_[loginCount_++] = now;
}

SshWorker::Submit SshWorker::submit(uint32_t requestId, uint32_t generation, bool power,
                                    const protocol::SshRequest& request) {
	// Only the main loop submits; the task only clears `busy_`.
	if (busy_) return Submit::Busy;
	const size_t logins = power ? 1 : 2;
	if (!loginsAvailable(logins)) return Submit::TooManyLogins;

	// The task's stack is by far the largest allocation; with room for it and more, copying the
	// job can't run out of heap.
	if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < TASK_STACK + JOB_HEAP_MARGIN) {
		return Submit::OutOfMemory;
	}
	auto* job = new (std::nothrow) Job{this, requestId, generation, power, request};
	if (!job) return Submit::OutOfMemory;

	busy_ = true;
	if (xTaskCreate(run, "ssh", TASK_STACK, job, 1, nullptr) != pdPASS) {
		delete job;
		busy_ = false;
		return Submit::OutOfMemory;
	}
	// Counted only now: a job that couldn't start made no login.
	recordLogins(logins);
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
                                  const std::string& expectedHostKey, ssh_key key, uint32_t deadline,
                                  std::string& hostKey, std::string& error) {
	int port = request.port;
	bool processConfig = false;
	// All or nothing: on failure libssh keeps no host, and connecting would fail anyway.
	if (ssh_options_set(session, SSH_OPTIONS_HOST, request.host.c_str()) != SSH_OK) {
		error = "Invalid host name " + request.host;
		return Login::NoAddress;
	}
	ssh_options_set(session, SSH_OPTIONS_PORT, &port);
	// Not checked: libssh keeps the name even when it reports a syntax error, which Windows names
	// such as `DOMAIN\user` rely on.
	ssh_options_set(session, SSH_OPTIONS_USER, request.username.c_str());
	ssh_options_set(session, SSH_OPTIONS_PROCESS_CONFIG, &processConfig);
	setWait(session, std::min(remaining(deadline), CONNECT_TIMEOUT_MS));

	if (ssh_connect(session) != SSH_OK) {
		const char* message = ssh_get_error(session);
		// After the device's SSH banner, the device is up: the handshake failed (no common
		// algorithm, a timeout). Before it, there was no TCP connection, or no address.
		if (ssh_get_serverbanner(session)) {
			error = std::string("SSH handshake failed: ") + message;
			return Login::Failed;
		}
		if (unresolved(message)) {
			error = "Host name " + request.host + " doesn't resolve";
			return Login::NoAddress;
		}
		error = "Couldn't connect to " + request.host + " port " + std::to_string(request.port);
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

	if (remaining(deadline) == 0) {
		error = "SSH login timed out";
		return Login::Failed;
	}
	setWait(session, remaining(deadline));
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
			setText(done.error, "Not enough memory for SSH");
			return;
		}
		std::string hostKey, error;
		const Login result =
			login(ssh.session, request, expected, keys_[index(action)], millis() + LOGIN_BUDGET_MS, hostKey, error);
		// The first login's host key: another one on the second login is reported as a mismatch.
		if (!hostKey.empty() && done.hostKey[0] == '\0') strlcpy(done.hostKey, hostKey.c_str(), sizeof(done.hostKey));
		done.ok = true;

		switch (result) {
			case Login::NoAddress:
			case Login::Unreachable:
				// No TCP connection: the device is offline (for this check). Only a host name that
				// doesn't resolve says why, like the Linux agent.
				if (action == Action::Reboot) {
					setText(done.checkError, "The device stopped answering");
				} else if (result == Login::NoAddress) {
					setText(done.checkError, error);
				}
				return;
			case Login::HostKeyMismatch:
				done.online = true;
				setText(done.checkError, "host_key_mismatch");
				return;
			case Login::Failed:
				done.online = true;
				setText(done.checkError, error);
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
	auto fail = [&done](std::string_view message) {
		done.ok = false;
		setText(done.error, message);
	};
	const uint32_t deadline = millis() + POWER_BUDGET_MS;

	const std::string command = protocol::sshCommand(request);
	Log::info("SSH: running \"%s\" on %s@%s:%u", command.c_str(), request.username.c_str(), request.host.c_str(),
	          request.port);

	Session ssh;
	if (!ssh.session) return fail("Not enough memory for SSH");

	std::string hostKey, error;
	switch (login(ssh.session, request, request.hostKey, keys_[index(request.action)], deadline, hostKey, error)) {
		case Login::NoAddress:
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

	constexpr const char* TOO_SLOW = "The device took too long to answer";
	if (remaining(deadline) == 0) return fail(TOO_SLOW);
	setWait(ssh.session, remaining(deadline));
	ssh_channel channel = ssh_channel_new(ssh.session);
	if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
		if (channel) ssh_channel_free(channel);
		return fail("The device refused a session");
	}
	if (remaining(deadline) == 0) {
		ssh_channel_free(channel);
		return fail(TOO_SLOW);
	}
	setWait(ssh.session, remaining(deadline));
	if (ssh_channel_request_exec(channel, command.c_str()) != SSH_OK) {
		ssh_channel_free(channel);
		return fail("The device refused the command");
	}

	// Wait for the exit status. Shutdown often drops the connection first: that counts as success.
	// Windows prints its errors to stdout, so both streams are kept for a failure's message.
	std::string stderrText, stdoutText;
	char buffer[128];
	bool eof = false;
	const uint32_t start = millis();
	while (millis() - start < EXEC_WAIT_MS && remaining(deadline) > 0) {
		const int err = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 1, 200);
		if (err > 0 && stderrText.size() < OUTPUT_BYTES) {
			stderrText.append(buffer, std::min<size_t>(err, OUTPUT_BYTES - stderrText.size()));
		}
		const int out = ssh_channel_read_timeout(channel, buffer, sizeof(buffer), 0, 50);
		if (out > 0 && stdoutText.size() < OUTPUT_BYTES) {
			stdoutText.append(buffer, std::min<size_t>(out, OUTPUT_BYTES - stdoutText.size()));
		}
		if (err == SSH_ERROR || out == SSH_ERROR || !ssh_is_connected(ssh.session)) break;
		if (ssh_channel_is_eof(channel)) {
			eof = true;
			break;
		}
	}

	uint32_t exitCode = 0;
	setWait(ssh.session, std::max(remaining(deadline), EXIT_STATUS_WAIT_MS));
	const bool exited = eof && ssh_channel_get_exit_state(channel, &exitCode, nullptr, nullptr) == SSH_OK;
	ssh_channel_free(channel);

	if (exited && exitCode != 0) {
		// One line of valid UTF-8: the output can be in the device's code page (and it goes into
		// the log, which the server reads too).
		std::string output = protocol::oneLine(stderrText, OUTPUT_BYTES);
		if (output.empty()) output = protocol::oneLine(stdoutText, OUTPUT_BYTES);
		const std::string message =
			"Exit code " + std::to_string(exitCode) + (output.empty() ? std::string() : ": " + output);
		Log::warn("SSH: %s failed: %s", command.c_str(), message.c_str());
		return fail(message);
	}
	Log::info("SSH: ran \"%s\" on %s", command.c_str(), request.host.c_str());
	done.ok = true;
}
