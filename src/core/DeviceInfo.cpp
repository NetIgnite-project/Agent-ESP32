#include "core/DeviceInfo.h"

#include <esp_idf_version.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "Config.h"

namespace deviceInfo {

const char* resetReason() {
	switch (esp_reset_reason()) {
		case ESP_RST_POWERON:
			return "power-on";
		case ESP_RST_SW:
			return "restart";
		case ESP_RST_PANIC:
			return "crash";
		case ESP_RST_INT_WDT:
		case ESP_RST_TASK_WDT:
		case ESP_RST_WDT:
			return "watchdog";
		case ESP_RST_BROWNOUT:
			return "brownout";
		case ESP_RST_DEEPSLEEP:
			return "deep sleep";
		case ESP_RST_EXT:
			return "reset pin";
		default:
			return "unknown";
	}
}

String chip() {
	return String(ESP.getChipModel()) + " rev " + (ESP.getChipRevision() / 100) + "." + (ESP.getChipRevision() % 100);
}

String mac() {
	uint8_t bytes[6] = {};
	esp_read_mac(bytes, ESP_MAC_WIFI_STA);
	char text[18];
	snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
	         bytes[5]);
	return text;
}

const char* runningPartition() {
	const esp_partition_t* partition = esp_ota_get_running_partition();
	return partition ? partition->label : "?";
}

void fill(JsonObject info, const Settings& settings, const WifiLink& wifi) {
	info["name"] = settings.deviceName();
	info["mac"] = mac();
	info["ipv4"] = wifi.ipv4();
	const String linkLocal = wifi.linkLocalIpv6();
	if (linkLocal != "::") info["ipv6_link_local"] = linkLocal;
	const String global = wifi.globalIpv6();
	if (global != "::") info["ipv6_global"] = global;
	info["wifi"] = wifi.ssid();
	info["rssi_dbm"] = wifi.rssi();
	info["ip_mode"] = settings.staticIp().enabled ? "static" : "dhcp";
	info["chip"] = chip();
	info["partition"] = runningPartition();
	info["reset_reason"] = resetReason();
	info["uptime_s"] = static_cast<uint32_t>(millis() / 1000);
	info["free_heap"] = ESP.getFreeHeap();
	info["min_free_heap"] = ESP.getMinFreeHeap();
	info["esp_idf"] = esp_get_idf_version();
}

}  // namespace deviceInfo
