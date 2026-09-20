#ifndef KLIMACONTROL_OTA_HTTP_HTTPREADER_H
#define KLIMACONTROL_OTA_HTTP_HTTPREADER_H

#include <esp_http_client.h>
#include <cstddef>

namespace OTA::Http {

    // Streaming reader for ArduinoJson. Reads directly from esp_http_client so the
    // GitHub releases JSON does not need to land in RAM whole — deserializeJson
    // pulls bytes through read()/readBytes() until the document is complete.
    struct HttpReader {
        esp_http_client_handle_t client;

        int read();
        size_t readBytes(char* buffer, size_t length);
    };

} // namespace OTA::Http

#endif // KLIMACONTROL_OTA_HTTP_HTTPREADER_H
