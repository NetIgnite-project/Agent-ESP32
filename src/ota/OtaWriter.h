#pragma once

#include <Arduino.h>
#include <mbedtls/sha256.h>

#include "netignite/Protocol.h"

/**
 * Writes a firmware image into the inactive OTA slot while hashing it. `finish()` only activates
 * the image if its size and SHA-256 match and, when a public key is compiled in
 * (`scripts/signing_key.py`), its ECDSA P-256 signature is valid. Used by OTA downloads and, in
 * emulator builds, by the serial `ota-*` test commands.
 */
class OtaWriter {
   public:
	~OtaWriter() { abort(); }

	/** Whether this firmware only accepts signed images. */
	static bool requiresSignature();

	bool begin(const netignite::protocol::OtaRequest& request, String& error);
	bool write(const uint8_t* data, size_t length, String& error);
	/** Verifies the image and makes it the boot image. */
	bool finish(String& error);
	void abort();

	bool active() const { return active_; }
	uint32_t written() const { return written_; }
	uint32_t size() const { return request_.size; }
	const std::string& version() const { return request_.version; }

   private:
	netignite::protocol::OtaRequest request_;
	mbedtls_sha256_context sha_{};
	bool active_ = false;
	uint32_t written_ = 0;

	bool verifySignature(const uint8_t hash[32], String& error) const;
};
