#ifndef KLIMACONTROL_OTA_HTTP_HTTPCLIENT_H
#define KLIMACONTROL_OTA_HTTP_HTTPCLIENT_H

#include <esp_http_client.h>

namespace OTA::Http {

// esp_http_client response (RX) buffer size. Must be large enough to hold a
// whole HTTP response header block in a SINGLE esp_tls_conn_read(): GitHub's
// github.com 302 release-download redirect carries a ~3.6 KB
// Content-Security-Policy header (total header block ~5 KB) with a 0-byte
// body, sent as one small TLS record. mbedTLS decrypts the full record into
// its internal buffer on the first read; if our buffer is smaller than the
// record, the leftover plaintext stays buffered inside mbedTLS and is
// invisible to the socket poll() that esp_http_client's next read performs,
// so esp_http_client_fetch_headers() never reaches on_headers_complete and
// get_status_code() keeps its -1 init value ("No HTTP response"). 8 KB holds
// the current ~5 KB block with headroom for CSP growth.
//
// Applied to the check request too, not just the download: api.github.com
// happens to stream a body across many TLS records today (so the 512-byte
// default survives), but that is a property of GitHub's current framing,
// not something we should depend on.
constexpr int kHttpRxBuffer = 8192;

// RAII wrapper around esp_http_client. Owns the handle for the lifetime of
// the wrapper, runs the redirect-following loop, and exposes the raw handle
// for callers that need to stream the response body through their own reader
// (e.g. HttpReader for deserializeJson).
class HttpClient {
public:
    explicit HttpClient(const esp_http_client_config_t &config);
    ~HttpClient();

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    explicit operator bool() const { return handle != nullptr; }

    // Open connection, following redirects (up to maxRedirects hops). Returns
    // the HTTP status code of the final response, or -1 on connection failure
    // or a redirect refused by the scheme classifier.
    int openWithRedirects(int maxRedirects = 5);

    // Underlying handle, used by HttpReader to stream the response body.
    esp_http_client_handle_t raw() const { return handle; }

private:
    esp_http_client_handle_t handle = nullptr;
};

} // namespace OTA::Http

#endif // KLIMACONTROL_OTA_HTTP_HTTPCLIENT_H
