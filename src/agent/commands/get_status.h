#ifndef GET_STATUS_H
#define GET_STATUS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "utils/inputValidation.h"
#include "utils/utils.h"
#include "utils/icmp.h"

JsonDocument get_status(JsonDocument& input) {

    JsonDocument response;

    response["status"] = "ERROR";
    response["online"] = "unknown";

    if (!input["address"].is<const char *>()) {
        response["message"] = "Missing or invalid payload";
        return response;
    }

    if (InputValidation::ipv4Address(input["address"])) {

        IPAddress ipAddress;
        if (!ipAddress.fromString(static_cast<const char *>(input["address"]))) {
            response["message"] = "Invalid IPv4 address format";
            return response;
        }

        response["online"] = ICMP::ping(ipAddress, 1) ? "online" : "offline";

    } else if (InputValidation::macAddress(input["address"])) {

        String linkLocal = Utils::macToLinkLocalIPv6(input["address"]);
        IPv6Address linkLocalAddress;
        if (linkLocal.length() == 0 || !linkLocalAddress.fromString(linkLocal.c_str())) {
            response["message"] = "Invalid MAC address format";
            return response;
        }

        response["online"] = ICMP::ping(linkLocalAddress, 1) ? "online" : "offline";

    } else {
        response["message"] = "Invalid address format: must be a valid IPv4 address or a MAC address";
        return response;
    }

    response["status"] = "OK";
    response["message"] = "Status retrieved successfully";
    return response;
}

#endif // GET_STATUS_H