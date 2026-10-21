#include "ota/Rollback.h"

#include <esp_ota_ops.h>

#include "Config.h"
#include "core/Log.h"

/**
 * Arduino's core marks a new firmware valid on its first boot unless this returns true; the agent
 * confirms it itself once it has reached the server (rollback::confirm).
 */
extern "C" bool verifyRollbackLater() {
	return true;
}

namespace rollback {

namespace {

bool pending_ = false;
uint32_t deadline_ = 0;
String failedUpdate_;

}  // namespace

void begin(Settings& settings) {
	const esp_partition_t* running = esp_ota_get_running_partition();
	esp_ota_img_states_t state;
	pending_ = running && esp_ota_get_state_partition(running, &state) == ESP_OK &&
	           state == ESP_OTA_IMG_PENDING_VERIFY;
	deadline_ = millis() + config::OTA_CONFIRM_TIMEOUT_MS;

	const String target = settings.otaVersion();
	if (target.length() > 0) {
		if (running && settings.otaPartition() == running->label) {
			Log::info("Updated to firmware %s", config::FIRMWARE_VERSION);
			if (!pending_) settings.clearOtaMarker();
		} else {
			// The bootloader went back to this partition: the update crashed or never confirmed.
			failedUpdate_ = target;
			settings.clearOtaMarker();
			Log::warn("The update to %s failed; running %s again", target.c_str(), config::FIRMWARE_VERSION);
		}
	}

	if (pending_) {
		Log::info("New firmware on probation: it stays installed once it reaches the server (within %lu s)",
		          static_cast<unsigned long>(config::OTA_CONFIRM_TIMEOUT_MS / 1000));
	}
}

bool pending() {
	return pending_;
}

void confirm(Settings& settings) {
	if (!pending_) return;
	if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
		Log::error("Couldn't confirm the new firmware");
		return;
	}
	pending_ = false;
	settings.clearOtaMarker();
	Log::info("Firmware %s confirmed", config::FIRMWARE_VERSION);
}

void loop() {
	if (!pending_ || static_cast<int32_t>(millis() - deadline_) < 0) return;

	Log::error("The new firmware didn't reach the server in time; going back to the previous firmware");
	delay(200);
	esp_ota_mark_app_invalid_rollback_and_reboot();
	// Only returns if there is no previous firmware to go back to.
	pending_ = false;
	Log::error("No previous firmware to go back to; keeping this one");
}

const String& failedUpdate() {
	return failedUpdate_;
}

void recordInstalled(Settings& settings, const String& version) {
	const esp_partition_t* next = esp_ota_get_boot_partition();
	settings.setOtaMarker(version, next ? next->label : "");
}

}  // namespace rollback
