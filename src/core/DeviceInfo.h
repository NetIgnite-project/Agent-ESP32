#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "core/Settings.h"
#include "net/WifiLink.h"

namespace deviceInfo {

/** Diagnostics for the HELLO message (shown on the agent's page in the dashboard). */
void fill(JsonObject info, const Settings& settings, const WifiLink& wifi);

const char* resetReason();
/** `ESP32-D0WD-V3 rev 3.1` */
String chip();
/** `AA:BB:CC:DD:EE:FF` of the Wi-Fi station interface. */
String mac();
/** Label of the running app partition (`app0`/`app1`). */
const char* runningPartition();

}  // namespace deviceInfo
