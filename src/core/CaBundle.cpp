#include "core/CaBundle.h"

// Symbols created by board_build.embed_files (binary, not NUL-terminated).
extern const uint8_t ca_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");
extern const uint8_t ca_bundle_end[] asm("_binary_data_cert_x509_crt_bundle_bin_end");

namespace caBundle {

const uint8_t* data() {
	return ca_bundle_start;
}

size_t size() {
	return static_cast<size_t>(ca_bundle_end - ca_bundle_start);
}

}  // namespace caBundle
