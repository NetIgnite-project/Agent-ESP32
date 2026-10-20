#pragma once

#include <Arduino.h>

#include "netignite/ServerUrl.h"

/**
 * Enrollment: the agent generates its own secret (hardware RNG) and exchanges a single-use
 * enrollment token for a credential at `POST /api/v1/agents/enroll`. The secret is only sent
 * once, over this request; the server keeps a hash of it.
 */
namespace enrollment {

struct Result {
	enum class Status {
		Ok,
		/** The server rejected the token (unknown, used or expired): a new one is needed. */
		Rejected,
		/** Network or server error: try again later. */
		Failed,
	};
	Status status = Status::Failed;
	String message;
	uint32_t agentId = 0;
	String tokenId;
	String secret;
};

Result enroll(const netignite::ServerUrl& server, const String& enrollmentToken);

}  // namespace enrollment
