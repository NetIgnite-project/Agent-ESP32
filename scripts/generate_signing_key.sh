#!/usr/bin/env sh
# Creates the ECDSA P-256 key pair for signing firmware releases.
#
#   sh scripts/generate_signing_key.sh
#
# - firmware-signing-key.pem (private): store it as the repository secret FIRMWARE_SIGNING_KEY
#   (Settings -> Secrets and variables -> Actions), then delete the file. Never commit it.
# - keys/firmware-signing.pub.pem (public): commit it. OTA updates only accept firmware signed with
#   the matching private key.
set -eu
mkdir -p keys
openssl ecparam -name prime256v1 -genkey -noout -out firmware-signing-key.pem
openssl ec -in firmware-signing-key.pem -pubout -out keys/firmware-signing.pub.pem
echo "Private key: firmware-signing-key.pem  (add as secret FIRMWARE_SIGNING_KEY, then delete)"
echo "Public key:  keys/firmware-signing.pub.pem  (commit)"
