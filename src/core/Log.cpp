#include "core/Log.h"

#include <esp_log.h>

#include <cstdarg>

LogLevel Log::level_ = LogLevel::Info;
SemaphoreHandle_t Log::mutex_ = nullptr;

namespace {

netignite::LogRing ring(Log::RING_BYTES);
vprintf_like_t idfOriginalVprintf = nullptr;

/** Recursive: ESP-IDF may log from inside a serial write. */
struct Lock {
	explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
		if (mutex_) xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
	}
	~Lock() {
		if (mutex_) xSemaphoreGiveRecursive(mutex_);
	}
	SemaphoreHandle_t mutex_;
};

/** Removes ANSI color codes and trailing line breaks (ESP-IDF's log format). */
std::string cleanLine(const char* text) {
	std::string line;
	for (const char* p = text; *p; p++) {
		if (*p == '\x1b') {
			while (*p && *p != 'm') p++;
			if (!*p) break;
			continue;
		}
		line += *p;
	}
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
	return line;
}

}  // namespace

void Log::begin(uint32_t baud) {
	mutex_ = xSemaphoreCreateRecursiveMutex();
#ifdef NETIGNITE_EMULATOR
	// The emulator test streams firmware images over the serial port.
	Serial.setRxBufferSize(8192);
#endif
	Serial.begin(baud);
	// Start on a fresh line after the ROM bootloader's output.
	Serial.print("\n");
	// Keep ESP-IDF's messages (Wi-Fi driver, TLS errors) for the dashboard too.
	idfOriginalVprintf = esp_log_set_vprintf(&Log::idfVprintf);
}

const char* Log::levelName(LogLevel level) {
	switch (level) {
		case LogLevel::Error:
			return "error";
		case LogLevel::Warn:
			return "warn";
		case LogLevel::Info:
			return "info";
		case LogLevel::Debug:
			return "debug";
	}
	return "info";
}

bool Log::parseLevel(const String& text, LogLevel& out) {
	for (LogLevel level : {LogLevel::Error, LogLevel::Warn, LogLevel::Info, LogLevel::Debug}) {
		if (text.equalsIgnoreCase(levelName(level))) {
			out = level;
			return true;
		}
	}
	return false;
}

void Log::write(LogLevel level, const char* prefix, const char* format, va_list args) {
	if (level > level_) return;

	char message[256];
	vsnprintf(message, sizeof(message), format, args);

	char line[288];
	snprintf(line, sizeof(line), "[%8lu] %s %s", static_cast<unsigned long>(millis() / 1000), prefix, message);

	Lock lock(mutex_);
	ring.push(line);
	Serial.print(line);
	Serial.print("\n");
}

int Log::idfVprintf(const char* format, va_list args) {
	char message[256];
	va_list copy;
	va_copy(copy, args);
	vsnprintf(message, sizeof(message), format, copy);
	va_end(copy);

	{
		Lock lock(mutex_);
		const std::string line = cleanLine(message);
		if (!line.empty()) ring.push(line);
	}
	return idfOriginalVprintf ? idfOriginalVprintf(format, args) : vprintf(format, args);
}

#define NETIGNITE_LOG_IMPL(name, level, prefix) \
	void Log::name(const char* format, ...) {   \
		va_list args;                           \
		va_start(args, format);                 \
		write(level, prefix, format, args);     \
		va_end(args);                           \
	}

NETIGNITE_LOG_IMPL(error, LogLevel::Error, "E")
NETIGNITE_LOG_IMPL(warn, LogLevel::Warn, "W")
NETIGNITE_LOG_IMPL(info, LogLevel::Info, "I")
NETIGNITE_LOG_IMPL(debug, LogLevel::Debug, "D")

void Log::print(const char* format, ...) {
	char message[512];
	va_list args;
	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	Lock lock(mutex_);
	Serial.print(message);
	Serial.print("\n");
}

void Log::writeRaw(const uint8_t* data, size_t length) {
	Lock lock(mutex_);
	Serial.write(data, length);
	Serial.flush();
}

uint32_t Log::nextSeq() {
	Lock lock(mutex_);
	return ring.nextSeq();
}

netignite::LogRing::ReadResult Log::read(uint32_t from, size_t maxBytes, std::vector<std::string>& out) {
	Lock lock(mutex_);
	return ring.read(from, maxBytes, out);
}
