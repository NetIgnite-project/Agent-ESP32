#include "core/Log.h"

#include <cstdarg>

LogLevel Log::level_ = LogLevel::Info;
SemaphoreHandle_t Log::mutex_ = nullptr;

namespace {

struct Lock {
	explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) {
		if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
	}
	~Lock() {
		if (mutex_) xSemaphoreGive(mutex_);
	}
	SemaphoreHandle_t mutex_;
};

}  // namespace

void Log::begin(uint32_t baud) {
	mutex_ = xSemaphoreCreateMutex();
	Serial.begin(baud);
	// Start on a fresh line after the ROM bootloader's output.
	Serial.print("\n");
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

	Lock lock(mutex_);
	Serial.printf("[%8lu] %s %s\n", static_cast<unsigned long>(millis() / 1000), prefix, message);
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
