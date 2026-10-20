#pragma once

#include <cstddef>
#include <cstdint>

/**
 * The trusted root certificates (data/cert/x509_crt_bundle.bin, generated at build time from
 * Mozilla's list plus data/cert/custom/), embedded into the firmware.
 */
namespace caBundle {

const uint8_t* data();
size_t size();

}  // namespace caBundle
