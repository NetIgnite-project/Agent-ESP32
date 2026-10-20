#pragma once

#include <Arduino.h>

#include <cstdint>

enum class LogLevel : uint8_t {
	Error = 0,
	Warn = 1,
	Info = 2,
	Debug = 3,
};

/**
 * Logging to the serial port. Thread-safe, and every message ends with a newline: the web
 * installer only recognizes Improv packets at the start of a line.
 */
class Log {
   public:
	static void begin(uint32_t baud);

	static void setLevel(LogLevel level) { level_ = level; }
	static LogLevel level() { return level_; }
	static const char* levelName(LogLevel level);
	/** Parses `error`, `warn`, `info` or `debug`. */
	static bool parseLevel(const String& text, LogLevel& out);

	static void error(const char* format, ...) __attribute__((format(printf, 1, 2)));
	static void warn(const char* format, ...) __attribute__((format(printf, 1, 2)));
	static void info(const char* format, ...) __attribute__((format(printf, 1, 2)));
	static void debug(const char* format, ...) __attribute__((format(printf, 1, 2)));

	/** Unprefixed console output (command answers), regardless of the log level. */
	static void print(const char* format, ...) __attribute__((format(printf, 1, 2)));

	/** Writes raw bytes (Improv packets) without interleaving with log lines. */
	static void writeRaw(const uint8_t* data, size_t length);

   private:
	static LogLevel level_;
	static SemaphoreHandle_t mutex_;

	static void write(LogLevel level, const char* prefix, const char* format, va_list args);
};
