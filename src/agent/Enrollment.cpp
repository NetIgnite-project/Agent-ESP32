#include "agent/Enrollment.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <esp_random.h>

#include <memory>

#include "core/CaBundle.h"
#include "core/Log.h"
#include "netignite/Hex.h"
#include "netignite/Protocol.h"

namespace enrollment {

namespace {

String generateSecret() {
	uint8_t bytes[32];
	esp_fill_random(bytes, sizeof(bytes));
	return netignite::toHex(bytes, sizeof(bytes)).c_str();
}

}  // namespace

Result enroll(const netignite::ServerUrl& server, const String& enrollmentToken) {
	Result result;
	const String secret = generateSecret();
	const std::string url = server.url("/api/v1/agents/enroll");

	std::unique_ptr<NetworkClient> client;
	if (server.secure) {
		auto* secure = new NetworkClientSecure();
		secure->setCACertBundle(caBundle::data(), caBundle::size());
		client.reset(secure);
	} else {
		client.reset(new NetworkClient());
	}

	HTTPClient http;
	http.setTimeout(15000);
	http.setReuse(false);
	if (!http.begin(*client, url.c_str())) {
		result.message = "Invalid server URL";
		return result;
	}
	http.addHeader("Content-Type", "application/json");

	JsonDocument body;
	body["enrollment_token"] = enrollmentToken;
	body["secret"] = secret;
	// The server refuses a token of a Linux agent (409) without using it up.
	body["kind"] = netignite::protocol::AGENT_KIND;
	String payload;
	serializeJson(body, payload);

	Log::info("Enrolling with %s", server.origin().c_str());
	const int status = http.POST(payload);
	const String response = status > 0 ? http.getString() : String();
	http.end();

	if (status <= 0) {
		result.message = String("Request failed: ") + HTTPClient::errorToString(status);
		return result;
	}

	JsonDocument answer;
	const bool parsed = !deserializeJson(answer, response);
	const char* message = parsed ? answer["message"].as<const char*>() : nullptr;

	if (status == 401 || status == 400 || status == 409) {
		result.status = Result::Status::Rejected;
		result.message = message ? message : "Enrollment token rejected";
		return result;
	}
	if (status != 200 || !parsed || !answer["success"].as<bool>()) {
		result.message = String("Server answered ") + status + (message ? String(": ") + message : String());
		return result;
	}

	const char* kind = answer["data"]["kind"];
	const char* tokenId = answer["data"]["token_id"];
	const uint32_t agentId = answer["data"]["agent_id"] | 0u;
	if (!tokenId || !*tokenId || agentId == 0) {
		result.message = "Unexpected answer from the server";
		return result;
	}
	if (!kind || String(kind) != netignite::protocol::AGENT_KIND) {
		// The token belongs to a Linux agent; the credential is useless here.
		result.status = Result::Status::Rejected;
		result.message = "This enrollment token is for a different kind of agent";
		return result;
	}

	result.status = Result::Status::Ok;
	result.agentId = agentId;
	result.tokenId = tokenId;
	result.secret = secret;
	return result;
}

}  // namespace enrollment
