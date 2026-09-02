#include "unity.h"

#include <cstdio>
#include <cstring>

#include "support/RequestDiag.h"

using namespace Support;

void setUp() {
    clearRequests();
}
void tearDown() {}

namespace {
    void push(const char *url, int code, uint32_t at) {
        recordRequest("POST", url, code, 32, 1, 50000, 40000, at);
    }
}

void test_empty_to_start() {
    TEST_ASSERT_EQUAL_UINT32(0, requestCount());
    TEST_ASSERT_EQUAL_UINT32(0, totalRequests());
}

void test_records_in_order() {
    push("/a", 200, 100);
    push("/b", 400, 200);
    push("/c", 501, 300);
    TEST_ASSERT_EQUAL_UINT32(3, requestCount());
    TEST_ASSERT_EQUAL_STRING("/a", requestAt(0).url);
    TEST_ASSERT_EQUAL_STRING("/b", requestAt(1).url);
    TEST_ASSERT_EQUAL_STRING("/c", requestAt(2).url);
    TEST_ASSERT_EQUAL_INT(501, requestAt(2).code);
}

void test_wraps_and_keeps_the_newest() {
    // The failure being hunted is intermittent, so the buffer must hold the
    // most recent requests and drop the oldest — the reverse would make it
    // useless exactly when it fills up.
    for (uint32_t i = 0; i < REQUEST_DIAG_CAPACITY + 5; ++i) {
        char url[16];
        snprintf(url, sizeof(url), "/r%u", static_cast<unsigned>(i));
        push(url, 200, i * 10);
    }
    TEST_ASSERT_EQUAL_UINT32(REQUEST_DIAG_CAPACITY, requestCount());
    TEST_ASSERT_EQUAL_UINT32(REQUEST_DIAG_CAPACITY + 5, totalRequests());

    // Oldest held is request #5; newest is the last one pushed.
    TEST_ASSERT_EQUAL_STRING("/r5", requestAt(0).url);
    char expected[16];
    snprintf(expected, sizeof(expected), "/r%u",
             static_cast<unsigned>(REQUEST_DIAG_CAPACITY + 4));
    TEST_ASSERT_EQUAL_STRING(expected, requestAt(REQUEST_DIAG_CAPACITY - 1).url);
}

void test_timestamps_are_monotonic_after_wrap() {
    for (uint32_t i = 0; i < REQUEST_DIAG_CAPACITY * 2; ++i) {
        push("/x", 200, i * 10);
    }
    for (size_t i = 1; i < requestCount(); ++i) {
        TEST_ASSERT_TRUE(requestAt(i).atMs > requestAt(i - 1).atMs);
    }
}

// --- stage marks ---

void test_stages_accumulate_then_clear() {
    markStage(StageBodyEntered);
    markStage(StageCsrfPassed);
    TEST_ASSERT_EQUAL_UINT16(StageBodyEntered | StageCsrfPassed, pendingStages());
    push("/a", 200, 10);
    TEST_ASSERT_EQUAL_UINT16(StageBodyEntered | StageCsrfPassed, requestAt(0).stages);
    // Consumed, so the next request cannot inherit them — otherwise a handler
    // that marks nothing would appear to have run the previous one's code.
    TEST_ASSERT_EQUAL_UINT16(StageNone, pendingStages());
    push("/b", 200, 20);
    TEST_ASSERT_EQUAL_UINT16(StageNone, requestAt(1).stages);
}

void test_the_signature_being_hunted() {
    // A body handler that started and then stopped before responding: the
    // record shows 501 with body|csrf set but no "sent".
    markStage(StageBodyEntered);
    markStage(StageCsrfPassed);
    push("/api/actuator/timing", 501, 100);
    const RequestRecord &r = requestAt(0);
    TEST_ASSERT_EQUAL_INT(501, r.code);
    TEST_ASSERT_TRUE((r.stages & StageBodyEntered) != 0);
    TEST_ASSERT_TRUE((r.stages & StageCsrfPassed) != 0);
    TEST_ASSERT_TRUE((r.stages & StageResponded) == 0);
}

void test_describe_stages() {
    char buf[64];
    describeStages(StageNone, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("none", buf);

    describeStages(StageBodyEntered | StageCsrfPassed, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("body|csrf", buf);

    describeStages(StageBodyEntered | StageCsrfPassed | StageJsonParsed | StageValidated |
                       StageResponded,
                   buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("body|csrf|json|valid|sent", buf);
}

void test_describe_stages_truncates_safely() {
    char buf[8];
    describeStages(StageBodyEntered | StageCsrfPassed | StageJsonParsed, buf, sizeof(buf));
    TEST_ASSERT_TRUE(std::strlen(buf) < sizeof(buf));
}

void test_long_url_is_truncated_not_overflowed() {
    char longUrl[200];
    std::memset(longUrl, 'x', sizeof(longUrl) - 1);
    longUrl[sizeof(longUrl) - 1] = '\0';
    recordRequest("POST", longUrl, 200, 0, 0, 0, 0, 1);
    const RequestRecord &r = requestAt(0);
    TEST_ASSERT_TRUE(std::strlen(r.url) < sizeof(r.url));
}

void test_null_fields_are_safe() {
    recordRequest(nullptr, nullptr, 200, 0, 0, 0, 0, 1);
    TEST_ASSERT_EQUAL_STRING("", requestAt(0).url);
    TEST_ASSERT_EQUAL_STRING("", requestAt(0).method);
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_to_start);
    RUN_TEST(test_records_in_order);
    RUN_TEST(test_wraps_and_keeps_the_newest);
    RUN_TEST(test_timestamps_are_monotonic_after_wrap);
    RUN_TEST(test_stages_accumulate_then_clear);
    RUN_TEST(test_the_signature_being_hunted);
    RUN_TEST(test_describe_stages);
    RUN_TEST(test_describe_stages_truncates_safely);
    RUN_TEST(test_long_url_is_truncated_not_overflowed);
    RUN_TEST(test_null_fields_are_safe);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
