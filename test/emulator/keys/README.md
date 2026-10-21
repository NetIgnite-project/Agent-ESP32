# Test keys for the emulator test

**Not secret, only for `esp32dev-qemu`.** The emulator firmware has `emulator-test.pub.pem`
compiled in (`custom_signing_key` in platformio.ini), so `improv-test.ts` can check that it
installs updates signed with `emulator-test.key.pem` and rejects unsigned ones and ones signed
with `emulator-other.key.pem`.

`scripts/signing_key.py` refuses to build any other environment with a key from `test/`. Release
firmware uses `keys/firmware-signing.pub.pem` (see `scripts/generate_signing_key.sh`).
