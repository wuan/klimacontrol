#include "HttpReader.h"

#ifdef ARDUINO

namespace OTA::Http {

int HttpReader::read() {
    char c;
    int r = esp_http_client_read(client, &c, 1);
    return r == 1 ? static_cast<unsigned char>(c) : -1;
}

size_t HttpReader::readBytes(char *buffer, size_t length) {
    int r = esp_http_client_read(client, buffer, length);
    return r > 0 ? static_cast<size_t>(r) : 0;
}

} // namespace OTA::Http

#endif // ARDUINO
