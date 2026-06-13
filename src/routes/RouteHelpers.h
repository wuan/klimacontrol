#ifndef KLIMACONTROL_ROUTEHELPERS_H
#define KLIMACONTROL_ROUTEHELPERS_H

#ifdef ARDUINO
#include <ESPAsyncWebServer.h>

// Content type constants
inline const char* CONTENT_TYPE_HTML = "text/html";
inline const char* CONTENT_TYPE_CSS = "text/css";
inline const char* CONTENT_TYPE_SVG = "image/svg+xml";
inline const char* CONTENT_TYPE_JSON = "application/json";

// JSON Key constants
inline const char* JSON_KEY_SUCCESS = "success";
inline const char* JSON_KEY_VALUE = "value";
inline const char* JSON_KEY_NAME = "name";

// Common JSON Responses
inline const char* JSON_RESPONSE_SUCCESS = "{\"success\":true}";
inline const char* JSON_RESPONSE_ERROR_INVALID_JSON = R"({"success":false,"error":"Invalid JSON"})";
inline const char* JSON_RESPONSE_ERROR_CSRF =
    R"({"success":false,"error":"Missing or invalid X-Requested-With header"})";

// CSRF protection for state-changing endpoints.
//
// The device has no authentication, so any host on the local network can reach
// its API. The realistic threat is cross-site request forgery: a malicious page
// the user happens to open auto-submits a request to the device's IP. Such
// forged requests come from a plain <form> (which cannot set custom headers) or
// from fetch()/XHR (where setting a custom header triggers a CORS preflight the
// device never answers, so the browser blocks the request before it arrives).
// Requiring this custom header therefore rejects forged requests while leaving
// the device's own same-origin UI - which sets the header explicitly - working.
inline const char* CSRF_HEADER = "X-Requested-With";
inline const char* CSRF_HEADER_VALUE = "KlimaControl";

// Returns true when the request carries the required CSRF header. Otherwise
// sends a 403 response and returns false; the caller must stop processing.
inline bool verifyCsrfHeader(AsyncWebServerRequest *request) {
    const AsyncWebHeader *header = request->getHeader(CSRF_HEADER);
    if (header == nullptr || header->value() != CSRF_HEADER_VALUE) {
        request->send(403, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_CSRF);
        return false;
    }
    return true;
}

inline void sendGzippedResponse(AsyncWebServerRequest *request, const char *contentType, const uint8_t *data, size_t len) {
    AsyncWebServerResponse *response = request->beginResponse(200, contentType, data, len);
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "max-age=86400");
    request->send(response);
}

#endif

#endif //KLIMACONTROL_ROUTEHELPERS_H
