#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "unity.h"

// Native test for the "largest_free_block in /api/status" contract —
// see spec `memory-management` → "Heap fragmentation is observable" and
// `system-architecture` → "Memory budget".
//
// The status-handler code path is built under #ifdef ARDUINO, so the
// heap_caps_get_largest_free_block call is only executed on the device.
// On the native build, the field is intentionally absent from the
// /api/status JSON. The contract is verified on the device target by
// the `pio run -e adafruit_qtpy_esp32s2` build; this native test
// statically checks the source file so a regression that drops the
// field is caught even when running the host-side test suite.

void setUp() {}
void tearDown() {}

// Read the StatusRoutes source file as a string. We do this at runtime
// (not via a build-system include) so the test does not depend on the
// Arduino-only headers and works on any native platform.
static std::string readStatusRoutesSource() {
    // The test runs from the project root, so the path is relative.
    // PIO's test runner also sets the cwd to the project root.
    std::ifstream f("src/routes/StatusRoutes.cpp");
    if (!f.is_open()) {
        return std::string{};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void test_status_routes_emits_largest_free_block_field() {
    const std::string src = readStatusRoutesSource();
    TEST_ASSERT_FALSE_MESSAGE(src.empty(),
        "could not open src/routes/StatusRoutes.cpp from project root");

    // The contract is that the field is present in BOTH /api/status and
    // /api/about (the latter is the new home for the field in the web UI;
    // see change `largest-free-block-as-measurement` for the spec delta).
    // The value source is heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    // and the call is gated on #ifdef ARDUINO so the native build still
    // compiles.
    TEST_ASSERT_TRUE_MESSAGE(src.find("\"largest_free_block\"") != std::string::npos,
        "StatusRoutes.cpp must emit a 'largest_free_block' JSON key");

    // The doc["largest_free_block"] assignment must appear at least twice
    // — once in the /api/status handler (memory-management spec) and once
    // in the /api/about handler (http-api delta in this change). Use a
    // second substring check on the exact assignment form to avoid false
    // positives from the unit comment in the change description.
    const std::string assignment = "doc[\"largest_free_block\"]";
    size_t pos = 0;
    uint32_t count = 0;
    while ((pos = src.find(assignment, pos)) != std::string::npos) {
        count++;
        pos += assignment.size();
    }
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32_MESSAGE(2, count,
        "StatusRoutes.cpp must emit 'largest_free_block' in BOTH /api/status and /api/about");

    TEST_ASSERT_TRUE_MESSAGE(src.find("heap_caps_get_largest_free_block") != std::string::npos,
        "StatusRoutes.cpp must source the value from heap_caps_get_largest_free_block");
    TEST_ASSERT_TRUE_MESSAGE(src.find("MALLOC_CAP_8BIT") != std::string::npos,
        "StatusRoutes.cpp must query the 8-bit-capable heap region");
    TEST_ASSERT_TRUE_MESSAGE(src.find("#ifdef ARDUINO") != std::string::npos,
        "StatusRoutes.cpp must guard the heap_caps call with #ifdef ARDUINO so native builds compile");
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_status_routes_emits_largest_free_block_field);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
