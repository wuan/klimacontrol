#ifndef KLIMACONTROL_SUPPORT_REQUESTDIAG_H
#define KLIMACONTROL_SUPPORT_REQUESTDIAG_H

#include <cstddef>
#include <cstdint>

// An in-RAM record of recent HTTP requests, readable over a GET.
//
// Exists because of a specific blind spot: body-carrying POSTs intermittently
// return 501, and the failing case has never been observed while a serial
// monitor was attached. Reading the serial log requires attaching one, so the
// only channel that could explain the fault may also perturb it. GET requests
// are unaffected by the fault, so a ring buffer read over HTTP can see what
// serial cannot.
//
// Written and read from the AsyncTCP task, which handles requests sequentially
// on this single-core part, so no locking is needed. That assumption is the
// reason this is a diagnostic and not general-purpose infrastructure.
namespace Support {

    /**
     * How far a handler got before the framework took over. Set by the handler
     * itself; absent bits mean it never reached that point.
     *
     * The interesting reading is a request that logs a 501 with only
     * `BodyEntered` set — the body callback ran and then stopped somewhere
     * before it could respond.
     */
    enum RequestStage : uint16_t {
        StageNone = 0,
        StageBodyEntered = 1u << 0,  // the onBody callback was invoked
        StageCsrfPassed = 1u << 1,   // CSRF header accepted
        StageJsonParsed = 1u << 2,   // deserializeJson returned success
        StageValidated = 1u << 3,    // input passed range checks
        StageResponded = 1u << 4,    // the handler called send()
    };

    struct RequestRecord {
        uint32_t atMs = 0;
        uint16_t stages = StageNone;
        int16_t code = 0;          // as seen by the middleware; -1 = no response yet
        uint32_t contentLength = 0;
        uint32_t freeHeap = 0;
        uint32_t largestBlock = 0;
        uint16_t elapsedMs = 0;
        uint16_t params = 0;      // POST params the framework parsed from the body
        char method[8] = "";
        char url[40] = "";
        char contentType[32] = "";
    };

    constexpr size_t REQUEST_DIAG_CAPACITY = 24;

    /**
     * Accumulating marks for the request currently being handled.
     *
     * Requests are processed one at a time on the AsyncTCP task, so a single
     * pending value is sufficient; record() consumes and clears it. A handler
     * that never marks anything simply reports StageNone.
     */
    void markStage(uint16_t stage);
    uint16_t pendingStages();

    /** Append a record and clear the pending marks. */
    void recordRequest(const char *method, const char *url, int code, uint32_t contentLength,
                       uint16_t elapsedMs, uint32_t freeHeap, uint32_t largestBlock, uint32_t atMs,
                       const char *contentType = nullptr, uint16_t params = 0);

    size_t requestCount();                     // records held, up to capacity
    uint32_t totalRequests();                  // seen since boot, including evicted
    const RequestRecord &requestAt(size_t i);  // 0 = oldest held
    void clearRequests();

    /** Comma-free textual form of the stage bits, for the API. */
    void describeStages(uint16_t stages, char *out, size_t outSize);

}

#endif // KLIMACONTROL_SUPPORT_REQUESTDIAG_H
