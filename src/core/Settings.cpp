#include "core/Settings.h"

#include <Preferences.h>
#include <esp_mac.h>

#include "Config.h"

namespace k = config::keys;

namespace {

/** Opens the namespace for one operation; closes it again in the destructor. */
class Store {
   public:
	explicit Store(bool readOnly) { ok_ = prefs_.begin(k::NAMESPACE, readOnly); }
	~Store() {
		if (ok_) prefs_.end();
	}
	Preferences& operator*() { return prefs_; }
	Preferences* operator->() { return &prefs_; }
	bool ok() const { return ok_; }

   private:
	Preferences prefs_;
	bool ok_ = false;
};

String readString(Preferences& prefs, const char* key) {
	return prefs.isKey(key) ? prefs.getString(key, "") : String();
}

void writeString(Preferences& prefs, const char* key, const String& value) {
	if (value.length() == 0) {
		if (prefs.isKey(key)) prefs.remove(key);
	} else {
		prefs.putString(key, value);
	}
}

}  // namespace

String Settings::defaultDeviceName() {
	uint8_t mac[6] = {};
	esp_read_mac(mac, ESP_MAC_WIFI_STA);
	char name[32];
	snprintf(name, sizeof(name), "NetIgnite-%02X%02X%02X", mac[3], mac[4], mac[5]);
	return name;
}

void Settings::load() {
	{
		// Creates the namespace on first boot, so the read-only open below succeeds.
		Store create(false);
	}
	Store store(true);
	if (!store.ok()) {
		Log::error("Settings: can't open NVS namespace '%s'", k::NAMESPACE);
		deviceName_ = defaultDeviceName();
		return;
	}

	serverUrl_ = readString(*store, k::SERVER_URL);
	enrollToken_ = readString(*store, k::ENROLL_TOKEN);
	agentId_ = store->getUInt(k::AGENT_ID, 0);
	tokenId_ = readString(*store, k::TOKEN_ID);
	secret_ = readString(*store, k::SECRET);
	wifiSsid_ = readString(*store, k::WIFI_SSID);
	wifiPassword_ = readString(*store, k::WIFI_PASSWORD);
	deviceName_ = readString(*store, k::DEVICE_NAME);
	if (deviceName_.length() == 0) deviceName_ = defaultDeviceName();

	staticIp_.enabled = store->getUChar(k::IP_MODE, 0) == 1;
	staticIp_.ip = readString(*store, k::STATIC_IP);
	staticIp_.gateway = readString(*store, k::GATEWAY);
	staticIp_.netmask = readString(*store, k::NETMASK);
	staticIp_.dns1 = readString(*store, k::DNS1);
	staticIp_.dns2 = readString(*store, k::DNS2);

	const uint8_t level = store->getUChar(k::LOG_LEVEL, static_cast<uint8_t>(LogLevel::Info));
	logLevel_ = level <= static_cast<uint8_t>(LogLevel::Debug) ? static_cast<LogLevel>(level) : LogLevel::Info;
}

void Settings::setServerUrl(const String& url) {
	Store store(false);
	writeString(*store, k::SERVER_URL, url);
	serverUrl_ = url;
}

void Settings::setEnrollToken(const String& token) {
	Store store(false);
	writeString(*store, k::ENROLL_TOKEN, token);
	enrollToken_ = token;
}

void Settings::setCredentials(uint32_t agentId, const String& tokenId, const String& secret) {
	Store store(false);
	store->putUInt(k::AGENT_ID, agentId);
	writeString(*store, k::TOKEN_ID, tokenId);
	writeString(*store, k::SECRET, secret);
	writeString(*store, k::ENROLL_TOKEN, "");
	agentId_ = agentId;
	tokenId_ = tokenId;
	secret_ = secret;
	enrollToken_ = "";
}

void Settings::clearCredentials() {
	Store store(false);
	if (store->isKey(k::AGENT_ID)) store->remove(k::AGENT_ID);
	writeString(*store, k::TOKEN_ID, "");
	writeString(*store, k::SECRET, "");
	agentId_ = 0;
	tokenId_ = "";
	secret_ = "";
}

void Settings::setWifi(const String& ssid, const String& password) {
	Store store(false);
	writeString(*store, k::WIFI_SSID, ssid);
	writeString(*store, k::WIFI_PASSWORD, password);
	wifiSsid_ = ssid;
	wifiPassword_ = password;
}

void Settings::setDeviceName(const String& name) {
	Store store(false);
	writeString(*store, k::DEVICE_NAME, name);
	deviceName_ = name.length() > 0 ? name : defaultDeviceName();
}

void Settings::setStaticIp(const StaticIp& config) {
	Store store(false);
	store->putUChar(k::IP_MODE, config.enabled ? 1 : 0);
	writeString(*store, k::STATIC_IP, config.enabled ? config.ip : String());
	writeString(*store, k::GATEWAY, config.enabled ? config.gateway : String());
	writeString(*store, k::NETMASK, config.enabled ? config.netmask : String());
	writeString(*store, k::DNS1, config.enabled ? config.dns1 : String());
	writeString(*store, k::DNS2, config.enabled ? config.dns2 : String());
	staticIp_ = config.enabled ? config : StaticIp{};
}

void Settings::setLogLevel(LogLevel level) {
	Store store(false);
	store->putUChar(k::LOG_LEVEL, static_cast<uint8_t>(level));
	logLevel_ = level;
}

void Settings::factoryReset() {
	Store store(false);
	store->clear();
	*this = Settings();
	deviceName_ = defaultDeviceName();
}
