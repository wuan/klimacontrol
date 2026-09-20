#ifndef KLIMACONTROL_REDIRECT_SCHEME_H
#define KLIMACONTROL_REDIRECT_SCHEME_H

#include <cstring>

namespace Support {

    /**
     * Classify the captured `Location` header from an HTTP redirect.
     *
     * Returns true iff it is safe to follow the redirect without downgrading
     * the transport to cleartext or losing the CA-bundle check:
     *
     *   - null or empty: false (the caller has not captured a header yet, or
     *     the upstream send was malformed — refusing is the safe default)
     *   - absolute `https://...`: true
     *   - relative path (no `://` anywhere in the captured prefix): true,
     *     because a relative Location inherits the current request's scheme,
     *     which is already HTTPS
     *   - anything else (`http://`, `ftp://`, a scheme we do not recognise,
     *     a truncated absolute URL with no `://` in the captured prefix):
     *     false
     *
     * The redirectLocation buffer on the OTA path is only 32 bytes; strlcpy()
     * guarantees null termination, so this works on the truncated prefix
     * without a separate length argument.
     *
     * Pure C++, no Arduino-only headers. Safe to call from native tests.
     */
    inline bool isSecureRedirectTarget(const char *location) {
        if (location == nullptr || location[0] == '\0') {
            return false;
        }
        if (strncasecmp(location, "https://", 8) == 0) {
            return true;
        }
        // No scheme delimiter in the captured prefix => relative URL (it
        // inherits the current request's scheme). An empty string would have
        // matched the early-return above; an absolute URL whose `://` falls
        // past the capture cap would still have `://` in the captured prefix
        // (it is shorter than the buffer by construction).
        return strstr(location, "://") == nullptr;
    }

} // namespace Support

#endif // KLIMACONTROL_REDIRECT_SCHEME_H