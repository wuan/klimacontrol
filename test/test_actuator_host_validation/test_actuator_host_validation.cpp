#include "unity.h"
#include "support/HostValidation.h"

#include <cstring>

void setUp() {}
void tearDown() {}

// --- Empty host is permitted (means "clear the assignment") ---

void test_empty_host_is_valid() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost(""));
}

void test_null_host_is_invalid() {
    // A null pointer is rejected defensively; the route handler passes ""
    // for a missing field.
    TEST_ASSERT_FALSE(Support::isValidActuatorHost(nullptr));
}

// --- Typical IPv4 literals are accepted ---

void test_ipv4_literal_typical() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("192.168.1.1"));
}

void test_ipv4_literal_private() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("10.0.0.1"));
}

void test_ipv4_literal_loopback() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("127.0.0.1"));
}

void test_ipv4_literal_zero() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("0.0.0.0"));
}

void test_ipv4_literal_high_octets() {
    // The validator does not enforce dotted-quad structure; a malformed
    // literal will fail to resolve as IPv4 anyway and round-trip as a
    // harmless hostname. The SSRF concern is met by rejecting URL
    // metacharacters, not by over-validating address shape.
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("999.999.999.999"));
}

void test_ipv4_literal_five_octets() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("1.2.3.4.5"));
}

// --- Typical hostnames are accepted ---

void test_hostname_short() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("a"));
}

void test_hostname_mdns_style() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("shellypro4pm-aabbccddeeff.local"));
}

void test_hostname_with_dashes() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("klima-01.lan"));
}

void test_hostname_with_underscore() {
    // mDNS service instances use underscores; ordinary hostnames do not,
    // but rejecting them costs nothing for the actuator use case.
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("my_device"));
}

void test_hostname_mixed_case() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("Heizung-1"));
}

void test_hostname_uppercase_letters() {
    TEST_ASSERT_TRUE(Support::isValidActuatorHost("KLIMA.local"));
}

// --- Length boundary ---

void test_host_252_chars_valid() {
    char host[253];
    std::memset(host, 'a', 252);
    host[252] = '\0';
    TEST_ASSERT_TRUE(Support::isValidActuatorHost(host));
}

void test_host_253_chars_valid() {
    char host[254];
    std::memset(host, 'a', 253);
    host[253] = '\0';
    TEST_ASSERT_TRUE(Support::isValidActuatorHost(host));
}

void test_host_254_chars_rejected() {
    char host[255];
    std::memset(host, 'a', 254);
    host[254] = '\0';
    TEST_ASSERT_FALSE(Support::isValidActuatorHost(host));
}

void test_host_500_chars_rejected() {
    char host[501];
    std::memset(host, 'a', 500);
    host[500] = '\0';
    TEST_ASSERT_FALSE(Support::isValidActuatorHost(host));
}

// --- URL metacharacters are rejected (SSRF payload matrix) ---

void test_slash_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42/admin"));
}

void test_slash_at_start_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("/etc/passwd"));
}

void test_question_mark_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42?x=y"));
}

void test_hash_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42#frag"));
}

void test_at_sign_rejected_userinfo_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42@evil.example.com"));
}

void test_colon_rejected_port_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42:80"));
}

void test_backslash_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("host\\path"));
}

void test_whitespace_space_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42 "));
}

void test_whitespace_tab_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42\t"));
}

void test_whitespace_newline_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42\n"));
}

void test_whitespace_carriage_return_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42\r"));
}

void test_leading_space_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost(" host"));
}

// --- IPv6 literals are rejected (the device is IPv4-only) ---

void test_ipv6_full_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("fe80::1"));
}

void test_ipv6_loopback_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("::1"));
}

void test_ipv6_bracketed_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("[fe80::1]"));
}

// --- Non-printable bytes and other punctuation ---

void test_exclamation_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("host!"));
}

void test_dollar_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("host$"));
}

void test_asterisk_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("host*"));
}

void test_paren_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("host(0)"));
}

void test_comma_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a,b"));
}

void test_semicolon_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a;b"));
}

void test_quote_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a'b"));
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a\"b"));
}

void test_angle_bracket_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a<b"));
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a>b"));
}

void test_non_printable_below_0x20_rejected() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a\x01""b"));
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a\x1f""b"));
}

void test_del_byte_rejected() {
    // 0x7F is DEL, not in any printable range.
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a\x7f""b"));
}

void test_high_byte_rejected() {
    // UTF-8 multi-byte sequences: a single 0xC3 byte is not a valid UTF-8
    // lead-and-trail and is certainly not in the DNS-name character class.
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("a\xc3\x9f""b"));
}

// --- The full SSRF attack matrix from the design ---

void test_ssrf_userinfo_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42@evil.example.com"));
}

void test_ssrf_path_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42/admin"));
}

void test_ssrf_query_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42?x=y"));
}

void test_ssrf_port_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42:80"));
}

void test_ssrf_trailing_space_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42 "));
}

void test_ssrf_newline_injection_attack() {
    TEST_ASSERT_FALSE(Support::isValidActuatorHost("192.168.1.42\n"));
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_host_is_valid);
    RUN_TEST(test_null_host_is_invalid);
    RUN_TEST(test_ipv4_literal_typical);
    RUN_TEST(test_ipv4_literal_private);
    RUN_TEST(test_ipv4_literal_loopback);
    RUN_TEST(test_ipv4_literal_zero);
    RUN_TEST(test_ipv4_literal_high_octets);
    RUN_TEST(test_ipv4_literal_five_octets);
    RUN_TEST(test_hostname_short);
    RUN_TEST(test_hostname_mdns_style);
    RUN_TEST(test_hostname_with_dashes);
    RUN_TEST(test_hostname_with_underscore);
    RUN_TEST(test_hostname_mixed_case);
    RUN_TEST(test_hostname_uppercase_letters);
    RUN_TEST(test_host_252_chars_valid);
    RUN_TEST(test_host_253_chars_valid);
    RUN_TEST(test_host_254_chars_rejected);
    RUN_TEST(test_host_500_chars_rejected);
    RUN_TEST(test_slash_rejected);
    RUN_TEST(test_slash_at_start_rejected);
    RUN_TEST(test_question_mark_rejected);
    RUN_TEST(test_hash_rejected);
    RUN_TEST(test_at_sign_rejected_userinfo_attack);
    RUN_TEST(test_colon_rejected_port_attack);
    RUN_TEST(test_backslash_rejected);
    RUN_TEST(test_whitespace_space_rejected);
    RUN_TEST(test_whitespace_tab_rejected);
    RUN_TEST(test_whitespace_newline_rejected);
    RUN_TEST(test_whitespace_carriage_return_rejected);
    RUN_TEST(test_leading_space_rejected);
    RUN_TEST(test_ipv6_full_rejected);
    RUN_TEST(test_ipv6_loopback_rejected);
    RUN_TEST(test_ipv6_bracketed_rejected);
    RUN_TEST(test_exclamation_rejected);
    RUN_TEST(test_dollar_rejected);
    RUN_TEST(test_asterisk_rejected);
    RUN_TEST(test_paren_rejected);
    RUN_TEST(test_comma_rejected);
    RUN_TEST(test_semicolon_rejected);
    RUN_TEST(test_quote_rejected);
    RUN_TEST(test_angle_bracket_rejected);
    RUN_TEST(test_non_printable_below_0x20_rejected);
    RUN_TEST(test_del_byte_rejected);
    RUN_TEST(test_high_byte_rejected);
    RUN_TEST(test_ssrf_userinfo_attack);
    RUN_TEST(test_ssrf_path_attack);
    RUN_TEST(test_ssrf_query_attack);
    RUN_TEST(test_ssrf_port_attack);
    RUN_TEST(test_ssrf_trailing_space_attack);
    RUN_TEST(test_ssrf_newline_injection_attack);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
