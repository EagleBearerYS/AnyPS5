#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_HEAPWATCH_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_HEAPWATCH_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace AgcDriver {

struct HeapReport {
    bool walked = false;
    unsigned long walkError = 0;
    std::uint64_t busyBytes = 0;
    std::uint64_t busyBlocks = 0;
    std::uint64_t busyPages = 0;
    std::uint64_t busySampled = 0;
    std::uint64_t busyZeroSampled = 0;
    std::uint64_t busyStride = 1;
    std::uint64_t freeBytes = 0;
    std::uint64_t freeBlocks = 0;
    std::uint64_t freePages = 0;
    std::uint64_t freeSampled = 0;
    std::uint64_t freeZeroSampled = 0;
    std::uint64_t freeStride = 1;
    std::uint64_t uncommittedBytes = 0;
    std::uint64_t regions = 0;
    std::uint64_t privateBytes = 0;
    double walkMilliseconds = 0.0;

    double BusyZeroPages() const;
    double FreeZeroPages() const;
};

struct HeapTrim {
    bool optimized = false;
    unsigned long error = 0;
    std::uint64_t privateBefore = 0;
    std::uint64_t privateAfterOptimize = 0;
    double optimizeMilliseconds = 0.0;
    int heapminResult = 0;
    std::uint64_t privateAfterHeapmin = 0;
    double heapminMilliseconds = 0.0;
};

HeapReport ReportProcessHeap(std::size_t samplesPerKind);
HeapTrim TrimProcessHeap();
std::uint64_t ProcessPrivateBytes();
std::string FormatHeapReport(const HeapReport& report);
std::string FormatHeapTrim(const HeapTrim& trim);
void StartHeapWatch();
void StopHeapWatch();

}

#endif
