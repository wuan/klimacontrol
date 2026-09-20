#include <Arduino.h>
#include "HttpClient.h"

#ifdef ARDUINO

#include "HttpEventHandler.h"
#include "RedirectScheme.h"
#include "Log.h"

namespace OTA::Http {

static constexpr const char *const TAG = "ota";

HttpClient::HttpClient(const esp_http_client_config_t &config)
    : handle(esp_http_client_init(&config)) {}

HttpClient::~HttpClient() {
    if (handle) {
        esp_http_client_close(handle);
        esp_http_client_cleanup(handle);
    }
}

int HttpClient::openWithRedirects(int maxRedirects) {
    for (int i = 0; i < maxRedirects; i++) {
        g_redirectLocation[0] = '\0';
        esp_err_t err = esp_http_client_open(handle, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
            return -1;
        }

        esp_http_client_fetch_headers(handle);
        int status = esp_http_client_get_status_code(handle);

        if (status <= 0) {
            // open() succeeded (TLS connected) but no valid response line was
            // parsed — typically the server dropped the connection after we
            // sent a malformed/truncated request (e.g. TX buffer too small for
            // a long redirect URL). status_code keeps its -1 init value.
            ESP_LOGE(TAG, "No HTTP response (status %d) — connection dropped", status);
            return -1;
        }

        if (status == 301 || status == 302 || status == 307 || status == 308) {
            // The caller's host allowlist only covers the first hop, so
            // enforce the transport on every subsequent one: a
            // "Location: http://..." would otherwise be followed in
            // cleartext, silently dropping both confidentiality and the
            // CA-bundle check for the hop that actually carries the
            // firmware image.
            if (!isSecureRedirectPrefix(capturedRedirectPrefix())) {
                ESP_LOGE(TAG, "Redirect refused: target is not HTTPS");
                return -1;
            }
            // Snapshot the URL we just fetched so the post-redirect log line
            // can name both endpoints. esp_http_client_get_url() takes a
            // caller buffer; we capture before set_redirection() because
            // that call rewrites the handle's URL to the redirect target.
            char currentHost[256] = "?";
            esp_http_client_get_url(handle, currentHost, sizeof(currentHost));
            esp_http_client_close(handle);
            if (esp_http_client_set_redirection(handle) != ESP_OK) {
                ESP_LOGE(TAG, "Redirect failed: no Location header");
                return -1;
            }
            // Diagnostic only: when a hop later fails to connect, this
            // names the URL we were navigating to instead of leaving the
            // log to describe "Redirect refused: target is not HTTPS" with
            // no host. Compiled out under CORE_DEBUG_LEVEL=0.
            ESP_LOGD(TAG, "OTA hop %d: %s -> %s", i, currentHost, capturedRedirectPrefix());
            ESP_LOGI(TAG, "Following redirect (%d)...", status);
            continue;
        }
        return status;
    }
    ESP_LOGE(TAG, "Too many redirects");
    return -1;
}

} // namespace OTA::Http

#endif // ARDUINO
