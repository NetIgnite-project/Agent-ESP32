#pragma once

#include <Arduino.h>

#include "core/Log.h"

/** Persistent configuration in NVS (namespace `netignite`, see `config::keys`). */
class Settings {
   public:
	struct StaticIp {
		bool enabled = false;
		String ip, gateway, netmask, dns1, dns2;
	};

	/** Loads all values; call once in setup(). */
	void load();

	const String& serverUrl() const { return serverUrl_; }
	const String& enrollToken() const { return enrollToken_; }
	uint32_t agentId() const { return agentId_; }
	const String& tokenId() const { return tokenId_; }
	const String& secret() const { return secret_; }
	const String& wifiSsid() const { return wifiSsid_; }
	const String& wifiPassword() const { return wifiPassword_; }
	const String& deviceName() const { return deviceName_; }
	const StaticIp& staticIp() const { return staticIp_; }
	LogLevel logLevel() const { return logLevel_; }

	bool hasWifi() const { return wifiSsid_.length() > 0; }
	bool hasCredentials() const { return tokenId_.length() > 0 && secret_.length() > 0; }

	void setServerUrl(const String& url);
	void setEnrollToken(const String& token);
	/** Stores the credential from a successful enrollment and removes the enrollment token. */
	void setCredentials(uint32_t agentId, const String& tokenId, const String& secret);
	void clearCredentials();
	void setWifi(const String& ssid, const String& password);
	void setDeviceName(const String& name);
	void setStaticIp(const StaticIp& config);
	void setLogLevel(LogLevel level);

	/**
	 * Changes the server URL or IP settings for this boot only (CONFIG_SET trial); a restart
	 * returns to the saved values. Save them with setServerUrl() / setStaticIp().
	 */
	void setServerUrlUnsaved(const String& url) { serverUrl_ = url; }
	void setStaticIpUnsaved(const StaticIp& config) { staticIp_ = config.enabled ? config : StaticIp{}; }

	/** The agent's SSH private keys for shutdown and reboot (OpenSSH format), or empty. */
	const String& sshKeyShutdown() const { return sshKeyShutdown_; }
	const String& sshKeyReboot() const { return sshKeyReboot_; }
	void setSshKeys(const String& shutdown, const String& reboot);

	/** An installed update that hasn't confirmed itself yet (see ota/Rollback.h). */
	String otaVersion() const { return otaVersion_; }
	String otaPartition() const { return otaPartition_; }
	void setOtaMarker(const String& version, const String& partition);
	void clearOtaMarker();

	/** Erases everything (credentials, Wi-Fi, server). */
	void factoryReset();

   private:
	String serverUrl_, enrollToken_, tokenId_, secret_, wifiSsid_, wifiPassword_, deviceName_;
	String otaVersion_, otaPartition_, sshKeyShutdown_, sshKeyReboot_;
	uint32_t agentId_ = 0;
	StaticIp staticIp_;
	LogLevel logLevel_ = LogLevel::Info;

	static String defaultDeviceName();
};
