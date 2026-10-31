#ifndef KLIMACONTROL_SUPPORT_IPFORMAT_H
#define KLIMACONTROL_SUPPORT_IPFORMAT_H

#ifdef ARDUINO
#include <Arduino.h>
#include <cstddef>
#include <cstdio>

/**
 * Format an IPv4 address as "a.b.c.d" into `buf` — byte-identical to
 * `IPAddress::toString()`, but without the temporary `String` that
 * `toString()` allocates on every call (16 B buffer covers the worst case
 * "255.255.255.255" plus NUL).
 */
inline void formatIpv4(const IPAddress& ip, char* buf, size_t len) {
    snprintf(buf, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
#endif // ARDUINO

#endif // KLIMACONTROL_SUPPORT_IPFORMAT_H
