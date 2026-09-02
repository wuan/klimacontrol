#include "support/RequestDiag.h"

#include <cstdio>
#include <cstring>

namespace Support {

    namespace {
        RequestRecord ring[REQUEST_DIAG_CAPACITY];
        size_t head = 0;   // next slot to write
        size_t held = 0;   // records currently valid, <= capacity
        uint32_t total = 0;
        uint16_t pending = StageNone;

        void copyField(char *dst, size_t dstSize, const char *src) {
            if (src == nullptr) {
                dst[0] = '\0';
                return;
            }
            std::strncpy(dst, src, dstSize - 1);
            dst[dstSize - 1] = '\0';
        }
    }

    void markStage(uint16_t stage) {
        pending |= stage;
    }

    uint16_t pendingStages() {
        return pending;
    }

    void recordRequest(const char *method, const char *url, int code, uint32_t contentLength,
                       uint16_t elapsedMs, uint32_t freeHeap, uint32_t largestBlock, uint32_t atMs,
                       const char *contentType, uint16_t params) {
        RequestRecord &r = ring[head];
        r.atMs = atMs;
        r.stages = pending;
        r.code = static_cast<int16_t>(code);
        r.contentLength = contentLength;
        r.freeHeap = freeHeap;
        r.largestBlock = largestBlock;
        r.elapsedMs = elapsedMs;
        copyField(r.method, sizeof(r.method), method);
        copyField(r.url, sizeof(r.url), url);
        copyField(r.contentType, sizeof(r.contentType), contentType);
        r.params = params;

        head = (head + 1) % REQUEST_DIAG_CAPACITY;
        if (held < REQUEST_DIAG_CAPACITY) {
            ++held;
        }
        ++total;

        // Consumed: the next request starts from nothing, so a handler that
        // marks nothing cannot inherit the previous request's marks.
        pending = StageNone;
    }

    size_t requestCount() {
        return held;
    }

    uint32_t totalRequests() {
        return total;
    }

    const RequestRecord &requestAt(size_t i) {
        // Oldest first. When the ring has wrapped, the oldest lives at head.
        const size_t base = (held == REQUEST_DIAG_CAPACITY) ? head : 0;
        return ring[(base + i) % REQUEST_DIAG_CAPACITY];
    }

    void clearRequests() {
        head = 0;
        held = 0;
        total = 0;
        pending = StageNone;
    }

    void describeStages(uint16_t stages, char *out, size_t outSize) {
        if (out == nullptr || outSize == 0) {
            return;
        }
        out[0] = '\0';
        if (stages == StageNone) {
            std::strncpy(out, "none", outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
        struct Entry {
            uint16_t bit;
            const char *name;
        };
        static const Entry entries[] = {
            {StageBodyEntered, "body"},   {StageCsrfPassed, "csrf"},
            {StageJsonParsed, "json"},    {StageValidated, "valid"},
            {StageResponded, "sent"},
        };
        size_t used = 0;
        for (const Entry &e : entries) {
            if ((stages & e.bit) == 0) {
                continue;
            }
            const size_t len = std::strlen(e.name);
            // +1 for the separator, +1 for the terminator.
            if (used + len + 2 > outSize) {
                break;
            }
            if (used > 0) {
                out[used++] = '|';
            }
            std::memcpy(out + used, e.name, len);
            used += len;
            out[used] = '\0';
        }
    }

}
