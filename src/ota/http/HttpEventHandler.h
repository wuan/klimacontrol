#ifndef KLIMACONTROL_OTA_HTTP_HTTPEVENTHANDLER_H
#define KLIMACONTROL_OTA_HTTP_HTTPEVENTHANDLER_H

// The header body is guarded by ARDUINO because esp_http_client_event_t is a
// typedef'd anonymous struct in the IDF SDK and cannot be forward-declared
// safely without dragging in <esp_http_client.h>. The function is only ever
// called from ARDUINO TUs (OTAUpdater.cpp, HttpClient.cpp), so the gating
// loses nothing.
#ifdef ARDUINO

#include <esp_http_client.h>

namespace OTA::Http {

    // Scheme prefix of the most recently seen Location header. captureRedirectLocation
    // writes it via strlcpy (truncation intentional — only the prefix is needed to
    // classify the redirect target). Reset to the empty string by HttpClient at
    // the top of each openWithRedirects() iteration.
    //
    // A shared buffer is sufficient because the Activity claim serializes all OTA
    // HTTP: the check and the update are mutually exclusive, so only one client is
    // ever open. Defined exactly once in HttpEventHandler.cpp; the extern here is
    // the only declaration site so a second definition would be a link error.
    extern char g_redirectLocation[32];

    // esp_http_client event handler that copies the value of the Location response
    // header into g_redirectLocation. Wired up via esp_http_client_config_t's
    // event_handler field from OTAUpdater's checkForUpdate / performUpdate.
    esp_err_t captureRedirectLocation(esp_http_client_event_t* evt);

    // Accessor used by HttpClient::openWithRedirects() between hops. Returns a
    // null-terminated C string (possibly empty after a reset, possibly truncated
    // after a long Location header).
    const char* capturedRedirectPrefix();

} // namespace OTA::Http

#endif // ARDUINO

#endif // KLIMACONTROL_OTA_HTTP_HTTPEVENTHANDLER_H
