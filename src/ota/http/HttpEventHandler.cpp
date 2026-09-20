#include "HttpEventHandler.h"

#ifdef ARDUINO

#include <esp_http_client.h>
#include <cstring>

namespace OTA::Http {

char g_redirectLocation[32];

esp_err_t captureRedirectLocation(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        evt->header_key != nullptr && evt->header_value != nullptr &&
        strcasecmp(evt->header_key, "Location") == 0) {
        // Only the scheme prefix matters; truncation is intentional.
        strlcpy(g_redirectLocation, evt->header_value, sizeof(g_redirectLocation));
    }
    return ESP_OK;
}

const char *capturedRedirectPrefix() {
    return g_redirectLocation;
}

} // namespace OTA::Http

#endif // ARDUINO
