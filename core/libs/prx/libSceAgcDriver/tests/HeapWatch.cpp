#include "prx/libSceAgcDriver/Execution/include/HeapWatch.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace AgcDriver;

constexpr std::uint64_t Gibibyte = std::uint64_t{1} << 30;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckFormats() {
    HeapReport report;
    report.walked = true;
    report.busyBytes = 4 * Gibibyte;
    report.busyBlocks = 9812334;
    report.busyPages = 1048576;
    report.busySampled = 1024;
    report.busyZeroSampled = 64;
    report.busyStride = 1024;
    report.freeBytes = 3 * Gibibyte / 2;
    report.freeBlocks = 14022;
    report.freePages = 393216;
    report.freeSampled = 512;
    report.freeZeroSampled = 256;
    report.freeStride = 768;
    report.uncommittedBytes = Gibibyte / 2;
    report.regions = 402;
    report.privateBytes = 26 * Gibibyte;
    report.walkMilliseconds = 210.0;
    const std::string expected = "[heap] busy 4.00 GiB in 9,812,334 blocks (zero pages ~0.25 GiB), free 1.50 GiB in 14,022 blocks (zero pages ~0.75 GiB), uncommitted 0.50 GiB, 402 regions; private 26.0 GiB; walk 210 ms; zero-page samples 1 in 1,024 busy pages (1,024), 1 in 768 free pages (512)";
    Require(FormatHeapReport(report) == expected, "the report line is \"" + FormatHeapReport(report) + "\"");
    HeapReport stopped;
    stopped.walkError = 87;
    Require(FormatHeapReport(stopped) == "[heap] the walk of the process heap stopped early (error 87)", "the line of a stopped walk is \"" + FormatHeapReport(stopped) + "\"");
    HeapTrim trim;
    trim.optimized = true;
    trim.privateBefore = 26 * Gibibyte;
    trim.privateAfterOptimize = 49 * Gibibyte / 2;
    trim.optimizeMilliseconds = 35.0;
    trim.privateAfterHeapmin = 49 * Gibibyte / 2;
    trim.heapminMilliseconds = 1.0;
    const std::string trimmed = "[heap-trim] HeapOptimizeResources ok, private 26.00 -> 24.50 GiB (-1.50 GiB) in 35 ms; _heapmin 0, private 24.50 -> 24.50 GiB (+0.00 GiB) in 1 ms";
    Require(FormatHeapTrim(trim) == trimmed, "the trim line is \"" + FormatHeapTrim(trim) + "\"");
    HeapTrim failed;
    failed.error = 87;
    failed.privateBefore = 26 * Gibibyte;
    failed.privateAfterOptimize = 26 * Gibibyte;
    failed.privateAfterHeapmin = 26 * Gibibyte;
    Require(FormatHeapTrim(failed) == "[heap-trim] HeapOptimizeResources failed (error 87), private 26.00 -> 26.00 GiB (+0.00 GiB) in 0 ms; _heapmin 0, private 26.00 -> 26.00 GiB (+0.00 GiB) in 0 ms", "the line of a failed trim is \"" + FormatHeapTrim(failed) + "\"");
    std::cout << "the report and trim lines are formatted as expected\n";
}

#ifdef _WIN32
void CheckTrimAndReport() {
    constexpr std::size_t Total = std::size_t{512} << 20;
    constexpr std::size_t Stride = std::size_t{1} << 20;
    constexpr std::size_t ZeroBytes = std::size_t{256} << 10;
    constexpr auto Exact = std::numeric_limits<std::size_t>::max();
    std::mt19937_64 random(3);
    std::vector<std::pair<void*, std::size_t>> blocks;
    std::size_t allocated = 0;
    while (allocated < Total) {
        const auto size = static_cast<std::size_t>(64 + random() % (480 - 64 + 1)) << 10;
        void* memory = std::malloc(size);
        Require(memory != nullptr, "cannot allocate a test block");
        std::memset(memory, 0xa5, size);
        blocks.emplace_back(memory, size);
        allocated += size;
    }
    void* zero = std::malloc(ZeroBytes);
    Require(zero != nullptr, "cannot allocate the zero block");
    std::memset(zero, 0, ZeroBytes);
    const auto before = ReportProcessHeap(Exact);
    const auto privateAllocated = ProcessPrivateBytes();
    std::size_t freed = 0;
    std::size_t kept = 0;
    std::size_t cursor = 0;
    for (auto& [memory, size] : blocks) {
        const bool keep = cursor / Stride != (cursor + size) / Stride;
        cursor += size;
        if (keep) {
            ++kept;
            continue;
        }
        std::free(memory);
        memory = nullptr;
        freed += size;
    }
    const auto after = ReportProcessHeap(Exact);
    const auto privateFreed = ProcessPrivateBytes();
    const auto trim = TrimProcessHeap();
    std::cout << FormatHeapReport(before) << "\n" << FormatHeapReport(after) << "\n" << FormatHeapTrim(trim) << "\n";
    std::cout << "allocated " << (allocated >> 20) << " MiB in " << blocks.size() << " blocks, kept " << kept << ", freed " << (freed >> 20) << " MiB; private " << (privateAllocated >> 20) << " MiB, " << (privateFreed >> 20) << " MiB after the frees, " << (trim.privateAfterHeapmin >> 20) << " MiB after the trim\n";
    Require(before.walked && after.walked, "a walk of the process heap stopped early");
    Require(trim.optimized, "HeapOptimizeResources failed with error " + std::to_string(trim.error));
    Require(privateAllocated > trim.privateAfterHeapmin && privateAllocated - trim.privateAfterHeapmin >= freed / 2, "the private bytes fell by " + std::to_string((privateAllocated - std::min(privateAllocated, trim.privateAfterHeapmin)) >> 20) + " MiB, less than half of the " + std::to_string(freed >> 20) + " MiB freed");
    Require(before.busyBytes >= allocated && before.busyBytes >= after.busyBytes && (before.busyBytes - after.busyBytes) * 10 >= freed * 9, "the report's busy bytes did not fall by the freed amount");
    const auto availableBefore = before.freeBytes + before.uncommittedBytes;
    const auto availableAfter = after.freeBytes + after.uncommittedBytes;
    Require(availableAfter >= availableBefore && (availableAfter - availableBefore) * 10 >= freed * 9, "the report's free and uncommitted bytes grew by " + std::to_string((availableAfter - std::min(availableAfter, availableBefore)) >> 20) + " MiB, not the " + std::to_string(freed >> 20) + " MiB freed");
    Require(after.busySampled == after.busyPages && after.freeSampled == after.freePages, "an exact report skipped pages");
    Require(after.BusyZeroPages() >= static_cast<double>(ZeroBytes / 4096 - 1) && after.BusyZeroPages() <= static_cast<double>(after.busyPages) && after.FreeZeroPages() <= static_cast<double>(after.freePages), "the report's zero-page counts are not sane");
    for (auto& [memory, size] : blocks) std::free(memory);
    std::free(zero);
}
#endif

}

int main() {
    try {
        CheckFormats();
#ifdef _WIN32
        CheckTrimAndReport();
#endif
    } catch (const std::exception& error) {
        std::cerr << "heap watch test failed: " << error.what() << "\n";
        return 1;
    }
    std::cout << "heap watch tests passed\n";
    return 0;
}
