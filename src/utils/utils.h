#ifndef UTILS_H
#define UTILS_H

#include "utils/logger.h"
#include "utils/inputValidation.h"
#include <WiFi.h>

class Utils {
  public:

    // Starts NTP and waits up to `timeoutMs` for the first sync. Returns whether the clock is set.
    // SNTP keeps retrying in the background, so a timeout (e.g. no network at boot) only delays TLS
    // connections until the time is known instead of blocking the boot forever.
    static bool setClock(unsigned long timeoutMs) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");

        Logger::info(F("Waiting for NTP time sync: "));
        unsigned long start = millis();
        time_t nowSecs = time(nullptr);
        while(nowSecs < 8 * 3600 * 2) {
            if (millis() - start >= timeoutMs) {
                Logger::println("");
                Logger::warnln(F("NTP time sync timed out. Continuing; the time is set once the network is available."));
                return false;
            }
            delay(500);
            Logger::print(F("."));
            yield();
            nowSecs = time(nullptr);
        }

        Logger::println("");
        struct tm timeinfo;
        gmtime_r(&nowSecs, &timeinfo);
        Logger::info(F("Current UTC time: "));
        Logger::print(asctime(&timeinfo));
        return true;
    }


    static String utf8ToHexStr(const char* str) {
        String hexStr = "";

        for (int i = 0; str[i] != '\0'; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", str[i]);
            hexStr += hex;
        }

        return hexStr;
    }

    // Returns the EUI-64 IPv6 link-local address for a MAC address, or an empty string if the MAC
    // address is invalid.
    static String macToLinkLocalIPv6(const char* macStr) {

        if (!macStr) return String();

        // Convert MAC string with colons or dashes to byte array
        uint8_t mac[6];
        if (InputValidation::macAddressColon(macStr)) {
            sscanf(macStr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        } else if (InputValidation::macAddressDashed(macStr)) {
            sscanf(macStr, "%hhx-%hhx-%hhx-%hhx-%hhx-%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
        } else {
            return String();
        }

        char buffer[40];

        uint8_t eui64[8];
        eui64[0] = mac[0] ^ 0x02;
        eui64[1] = mac[1];
        eui64[2] = mac[2];
        eui64[3] = 0xFF;
        eui64[4] = 0xFE;
        eui64[5] = mac[3];
        eui64[6] = mac[4];
        eui64[7] = mac[5];

        snprintf(buffer, sizeof(buffer),
            "fe80:0000:0000:0000:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            eui64[0], eui64[1],  // first 16 bits
            eui64[2], eui64[3],  // second 16 bits
            eui64[4], eui64[5],  // third 16 bits
            eui64[6], eui64[7]   // fourth 16 bits
        );

        // Returned by value: the caller previously had to free a strdup'd buffer and never did.
        return String(buffer);
    }
};

#endif // UTILS_H