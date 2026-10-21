#pragma once

#include <Arduino.h>

#include "core/Settings.h"

/**
 * After an update the new firmware is on probation (ESP-IDF app rollback): it marks itself valid
 * once it has reached the server. If it crashes, or doesn't reach the server within
 * `config::OTA_CONFIRM_TIMEOUT_MS`, the bootloader starts the previous firmware again.
 */
namespace rollback {

/** Call early in setup(), after Settings::load(). */
void begin(Settings& settings);

/** True while the running firmware is on probation. */
bool pending();

/** Marks the running firmware as good. Call once the agent is connected to the server. */
void confirm(Settings& settings);

/** Rolls back when the probation time is over. */
void loop();

/** Version of an update that failed and was rolled back (reported once per boot), or empty. */
const String& failedUpdate();

/** Remembers an installed update, so the next boot can tell whether it started. */
void recordInstalled(Settings& settings, const String& version);

}  // namespace rollback
