#ifndef KLIMACONTROL_OTA_HTTP_TLSALLOCATOR_H
#define KLIMACONTROL_OTA_HTTP_TLSALLOCATOR_H

#include <cstddef>
#include <esp_err.h>

namespace OTA::Http {

// Allocations larger than this go to PSRAM first; smaller allocations stay in
// internal SRAM. Mirrors CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096 so that
// esp_mbedtls_mem_calloc behaves the same way as plain malloc() on this board:
// small TLS metadata stays internal (fast, no PSRAM bus contention on the WiFi
// path), and the 16 KB mbedTLS record buffers per direction land in PSRAM
// instead of swallowing the ~24 KB of internal SRAM that WiFi/lwIP needs to
// keep running during the OTA download.
constexpr size_t kEspMbedtlsCallocThreshold = 4096;

} // namespace OTA::Http

// Override the pre-compiled SDK's mbedTLS allocator. The SDK version uses
// MALLOC_CAP_INTERNAL only, which fails on ESP32-S2 once internal SRAM is
// fragmented. Resolved at link time by the IDF SDK: we don't call these from
// C++ — the SDK calls them from inside mbedTLS.
extern "C" void *esp_mbedtls_mem_calloc(size_t n, size_t size);
extern "C" void esp_mbedtls_mem_free(void *ptr);

// The IDF esp_crt_bundle_attach uses the CA bundle embedded in the firmware
// binary. Declared here because Arduino's WiFiClientSecure wrapper shadows
// the IDF header that normally exposes it; TlsAllocator.cpp provides the
// matching definition.
extern "C" esp_err_t esp_crt_bundle_attach(void *conf);

#endif // KLIMACONTROL_OTA_HTTP_TLSALLOCATOR_H
