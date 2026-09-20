#include "TlsAllocator.h"

#ifdef ARDUINO

#include <esp_heap_caps.h>

extern "C" void* esp_mbedtls_mem_calloc(size_t n, size_t size) {
    const size_t total = n * size;
    const uint32_t preferred = total > OTA::Http::kEspMbedtlsCallocThreshold ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    const uint32_t fallback = total > OTA::Http::kEspMbedtlsCallocThreshold ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;

    void* ptr = heap_caps_calloc(n, size, preferred | MALLOC_CAP_8BIT);
    if (ptr == nullptr) {
        ptr = heap_caps_calloc(n, size, fallback | MALLOC_CAP_8BIT);
    }
    return ptr;
}

extern "C" void esp_mbedtls_mem_free(void* ptr) {
    heap_caps_free(ptr);
}

#endif // ARDUINO
