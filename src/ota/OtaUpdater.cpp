#include "ota/OtaUpdater.h"

#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

#include <memory>

#include "core/CaBundle.h"
#include "core/Log.h"

namespace {

constexpr uint32_t TASK_STACK = 12 * 1024;
constexpr uint32_t READ_TIMEOUT_MS = 30000;
constexpr size_t CHUNK = 4096;

}  // namespace

const char* OtaUpdater::stateName(State state) {
	switch (state) {
		case State::Idle:
			return "idle";
		case State::Downloading:
			return "downloading";
		case State::Installed:
			return "installed";
		case State::Failed:
			return "failed";
	}
	return "idle";
}

bool OtaUpdater::start(const netignite::ServerUrl& server, const std::string& authorization,
                       const netignite::protocol::OtaRequest& request, std::string& error) {
	if (busy()) {
		error = "An update is already running";
		return false;
	}
	if (OtaWriter::requiresSignature() && request.signature.empty()) {
		error = "This firmware only installs signed updates, and the update has no signature";
		return false;
	}

	server_ = server;
	authorization_ = authorization;
	request_ = request;
	{
		portENTER_CRITICAL(&lock_);
		progress_.version = request.version;
		progress_.error.clear();
		portEXIT_CRITICAL(&lock_);
	}
	setProgress(State::Downloading, 0);

	if (xTaskCreate(&OtaUpdater::taskMain, "ota", TASK_STACK, this, 1, &task_) != pdPASS) {
		task_ = nullptr;
		setProgress(State::Failed, 0, "Not enough memory to start the update");
		error = "Not enough memory to start the update";
		return false;
	}
	return true;
}

bool OtaUpdater::busy() const {
	portENTER_CRITICAL(&lock_);
	const bool running = progress_.state == State::Downloading;
	portEXIT_CRITICAL(&lock_);
	return running;
}

OtaUpdater::Progress OtaUpdater::progress() const {
	portENTER_CRITICAL(&lock_);
	Progress copy = progress_;
	portEXIT_CRITICAL(&lock_);
	return copy;
}

void OtaUpdater::setProgress(State state, uint8_t percent, const char* error) {
	portENTER_CRITICAL(&lock_);
	if (progress_.state != state || progress_.percent != percent) progress_.revision++;
	progress_.state = state;
	progress_.percent = percent;
	if (error) progress_.error = error;
	portEXIT_CRITICAL(&lock_);
}

void OtaUpdater::taskMain(void* self) {
	static_cast<OtaUpdater*>(self)->run();
	static_cast<OtaUpdater*>(self)->task_ = nullptr;
	vTaskDelete(nullptr);
}

void OtaUpdater::run() {
	const std::string url = server_.url(request_.path);
	Log::info("Downloading firmware %s (%lu bytes)", request_.version.c_str(),
	          static_cast<unsigned long>(request_.size));

	std::unique_ptr<NetworkClient> client;
	if (server_.secure) {
		auto* secure = new NetworkClientSecure();
		secure->setCACertBundle(caBundle::data(), caBundle::size());
		client.reset(secure);
	} else {
		client.reset(new NetworkClient());
	}

	auto fail = [this](const String& message) {
		writer_.abort();
		Log::error("Update failed: %s", message.c_str());
		setProgress(State::Failed, 0, message.c_str());
	};

	HTTPClient http;
	http.setTimeout(READ_TIMEOUT_MS);
	http.setReuse(false);
	if (!http.begin(*client, url.c_str())) {
		fail("Invalid download URL");
		return;
	}
	http.addHeader("Authorization", authorization_.c_str());

	const int status = http.GET();
	if (status != HTTP_CODE_OK) {
		fail(status > 0 ? String("The server answered ") + status : String("Download failed: ") +
		                                                                 HTTPClient::errorToString(status));
		http.end();
		return;
	}
	if (http.getSize() != static_cast<int>(request_.size)) {
		fail("The server sent a file of the wrong size");
		http.end();
		return;
	}

	String error;
	if (!writer_.begin(request_, error)) {
		fail(error);
		http.end();
		return;
	}

	NetworkClient* stream = http.getStreamPtr();
	std::unique_ptr<uint8_t[]> buffer(new uint8_t[CHUNK]);
	uint32_t lastData = millis();
	uint8_t percent = 0;

	while (writer_.written() < request_.size) {
		const size_t available = stream->available();
		if (available == 0) {
			if (!stream->connected() || millis() - lastData > READ_TIMEOUT_MS) {
				fail("The download stopped");
				http.end();
				return;
			}
			delay(5);
			continue;
		}

		const size_t wanted = std::min<size_t>({available, CHUNK, request_.size - writer_.written()});
		const int read = stream->read(buffer.get(), wanted);
		if (read <= 0) continue;
		lastData = millis();

		if (!writer_.write(buffer.get(), static_cast<size_t>(read), error)) {
			fail(error);
			http.end();
			return;
		}
		// A fast download could keep this task busy for seconds; let the idle task (task watchdog) run.
		delay(1);

		const uint8_t now = static_cast<uint8_t>(static_cast<uint64_t>(writer_.written()) * 100 / request_.size);
		if (now != percent) {
			percent = now;
			setProgress(State::Downloading, percent);
		}
	}
	http.end();

	if (!writer_.finish(error)) {
		fail(error);
		return;
	}
	Log::info("Firmware %s installed", request_.version.c_str());
	setProgress(State::Installed, 100);
}
