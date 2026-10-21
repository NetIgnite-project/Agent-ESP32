#include "agent/SshWorker.h"

#include <libssh/libssh.h>
#include <libssh_esp32.h>

#include <string>

#include "core/Log.h"

namespace protocol = netignite::protocol;

namespace {

constexpr uint32_t TASK_STACK = 40 * 1024;
constexpr long CONNECT_TIMEOUT_S = 5;
/** SSH_EXEC: how long to wait for the command's exit status (shutdown usually drops the link). */
constexpr uint32_t EXEC_WAIT_MS = 5000;

/** Overwrites a secret before its memory is freed. */
void wipe(std::string& secret) {
	volatile char* data = secret.data();
	for (size_t i = 0; i < secret.size(); i++) data[i] = 0;
	secret.clear();
}

/** Closes the session on every path out of a login. */
struct Session {
	ssh_session session = ssh_new();
	~Session() {
		if (!session) return;
		if (ssh_is_connected(session)) ssh_disconnect(session);
		ssh_free(session);
	}
};

}  // namespace

void SshWorker::begin() {
	results_ = xQueueCreate(2, sizeof(Done));
	libssh_begin();
}

bool SshWorker::submit(uint32_t requestId, uint32_t generation, bool exec, const protocol::SshRequest& request) {
	bool expected = false;
	if (!busy_.compare_exchange_strong(expected, true)) return false;

	auto* job = new Job{this, requestId, generation, exec, request};
	if (xTaskCreate(run, "ssh", TASK_STACK, job, 1, nullptr) != pdPASS) {
		wipe(job->request.secret);
		delete job;
		busy_ = false;
		return false;
	}
	return true;
}

bool SshWorker::poll(Done& out) {
	return results_ && xQueueReceive(results_, &out, 0) == pdTRUE;
}

void SshWorker::run(void* arg) {
	auto* job = static_cast<Job*>(arg);

	Done done = {};
	done.requestId = job->requestId;
	done.generation = job->generation;
	done.exec = job->exec;
	login(*job, done);

	SshWorker* worker = job->worker;
	wipe(job->request.secret);
	delete job;

	xQueueSend(worker->results_, &done, portMAX_DELAY);
	worker->busy_ = false;
	vTaskDelete(nullptr);
}

void SshWorker::login(const Job& job, Done& done) {
	const protocol::SshRequest& request = job.request;
	auto fail = [&done](const char* message) {
		done.ok = false;
		strlcpy(done.error, message, sizeof(done.error));
	};

	Log::info("SSH %s: %s@%s:%u", job.exec ? "command" : "check", request.username.c_str(), request.host.c_str(),
	          request.port);

	Session ssh;
	if (!ssh.session) return fail("Not enough memory for SSH");

	int port = request.port;
	long timeout = CONNECT_TIMEOUT_S;
	bool processConfig = false;
	ssh_options_set(ssh.session, SSH_OPTIONS_HOST, request.host.c_str());
	ssh_options_set(ssh.session, SSH_OPTIONS_PORT, &port);
	ssh_options_set(ssh.session, SSH_OPTIONS_USER, request.username.c_str());
	ssh_options_set(ssh.session, SSH_OPTIONS_TIMEOUT, &timeout);
	ssh_options_set(ssh.session, SSH_OPTIONS_PROCESS_CONFIG, &processConfig);

	if (ssh_connect(ssh.session) != SSH_OK) {
		if (!job.exec) {
			// The SSH port didn't accept a connection: the device is offline (for this check).
			done.ok = true;
			done.online = false;
			return;
		}
		return fail((std::string("Can't connect: ") + ssh_get_error(ssh.session)).c_str());
	}
	done.online = true;

	// The host key, before any credential is sent.
	ssh_key serverKey = nullptr;
	unsigned char* hash = nullptr;
	size_t hashLength = 0;
	if (ssh_get_server_publickey(ssh.session, &serverKey) != SSH_OK ||
	    ssh_get_publickey_hash(serverKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLength) != SSH_OK || hashLength != 32) {
		ssh_key_free(serverKey);
		ssh_clean_pubkey_hash(&hash);
		return fail("Can't read the device's host key");
	}
	const std::string hostKey = protocol::sshFingerprint(hash);
	ssh_clean_pubkey_hash(&hash);
	ssh_key_free(serverKey);
	strlcpy(done.hostKey, hostKey.c_str(), sizeof(done.hostKey));

	if (!request.hostKey.empty() && hostKey != request.hostKey) {
		Log::warn("SSH: %s presented another host key (%s)", request.host.c_str(), hostKey.c_str());
		if (job.exec) return fail("host key changed");
		done.ok = true;
		strlcpy(done.checkError, "host_key_mismatch", sizeof(done.checkError));
		return;
	}

	int auth = SSH_AUTH_ERROR;
	if (request.auth == protocol::SshRequest::Auth::Password) {
		auth = ssh_userauth_password(ssh.session, nullptr, request.secret.c_str());
	} else {
		ssh_key key = nullptr;
		if (ssh_pki_import_privkey_base64(request.secret.c_str(), nullptr, nullptr, nullptr, &key) != SSH_OK) {
			return fail("The private key can't be read");
		}
		auth = ssh_userauth_publickey(ssh.session, nullptr, key);
		ssh_key_free(key);
	}
	if (auth != SSH_AUTH_SUCCESS) {
		if (job.exec) return fail("authentication failed");
		done.ok = true;
		strlcpy(done.checkError, "auth_failed", sizeof(done.checkError));
		return;
	}
	done.authenticated = true;

	if (!job.exec) {
		// Log in only: never open a session, so forced commands in authorized_keys don't run.
		done.ok = true;
		return;
	}

	ssh_channel channel = ssh_channel_new(ssh.session);
	if (!channel || ssh_channel_open_session(channel) != SSH_OK) {
		if (channel) ssh_channel_free(channel);
		return fail("The device refused a session");
	}
	const std::string command = protocol::sshCommand(request);
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
		return fail(message.c_str());
	}
	Log::info("SSH: ran \"%s\" on %s", command.c_str(), request.host.c_str());
	done.ok = true;
}
