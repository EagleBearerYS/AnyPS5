#include "prx/libSceAgcDriver/Execution/include/HeapWatch.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#include <psapi.h>
#endif

namespace AgcDriver {

namespace {

constexpr std::uint64_t PageBytes = 4096;
constexpr double Gibibyte = 1024.0 * 1024.0 * 1024.0;

std::string grouped(std::uint64_t value) {
    auto digits = std::to_string(value);
    for (auto at = digits.size(); at > 3; at -= 3) digits.insert(at - 3, ",");
    return digits;
}

template <class... Values>
std::string formatted(const char* pattern, Values... values) {
    const auto size = std::snprintf(nullptr, 0, pattern, values...);
    if (size < 0) throw std::runtime_error("cannot format a heap line");
    std::string text(static_cast<std::size_t>(size), '\0');
    std::snprintf(text.data(), text.size() + 1, pattern, values...);
    return text;
}

double gibibytes(double bytes) {
    return bytes / Gibibyte;
}

double zeroPages(std::uint64_t pages, std::uint64_t sampled, std::uint64_t zero) {
    return sampled == 0 ? 0.0 : static_cast<double>(zero) * static_cast<double>(pages) / static_cast<double>(sampled);
}

std::string privateChange(std::uint64_t before, std::uint64_t after) {
    return formatted("private %.2f -> %.2f GiB (%+.2f GiB)", gibibytes(static_cast<double>(before)), gibibytes(static_cast<double>(after)), gibibytes(static_cast<double>(after) - static_cast<double>(before)));
}

#ifdef _WIN32
constexpr std::size_t WatchSamplesPerKind = 2048;
std::atomic<std::uint64_t> previousBusyPages{0};
std::atomic<std::uint64_t> previousFreePages{0};
std::jthread heapWatchThread;

int pageIsZero(std::uintptr_t page) {
    std::array<std::uint64_t, PageBytes / sizeof(std::uint64_t)> words;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(page), words.data(), PageBytes, &read) || read != PageBytes) return -1;
    return std::all_of(words.begin(), words.end(), [](std::uint64_t word) { return word == 0; }) ? 1 : 0;
}

void samplePages(std::uintptr_t address, std::uint64_t bytes, std::uint64_t stride, std::uint64_t& pages, std::uint64_t& sampled, std::uint64_t& zero) {
    const auto begin = (address + PageBytes - 1) & ~(PageBytes - 1);
    const auto end = (address + bytes) & ~(PageBytes - 1);
    if (end <= begin) return;
    const auto count = (end - begin) / PageBytes;
    for (auto index = (pages + stride - 1) / stride * stride; index < pages + count; index += stride) {
        const auto result = pageIsZero(begin + (index - pages) * PageBytes);
        if (result < 0) continue;
        ++sampled;
        zero += static_cast<std::uint64_t>(result);
    }
    pages += count;
}

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

std::uint64_t switchSeconds(const char* name) {
    const char* text = std::getenv(name);
    if (text == nullptr) return 0;
    char* end = nullptr;
    const auto value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || value == 0) {
        ProfilePrint_nid_no_patch("[heap] %s=%s is not a whole number of seconds above zero; it is ignored\n", name, text);
        return 0;
    }
    return value;
}
#endif

}

double HeapReport::BusyZeroPages() const {
    return zeroPages(busyPages, busySampled, busyZeroSampled);
}

double HeapReport::FreeZeroPages() const {
    return zeroPages(freePages, freeSampled, freeZeroSampled);
}

std::string FormatHeapReport(const HeapReport& report) {
    if (!report.walked) return formatted("[heap] the walk of the process heap stopped early (error %lu)", report.walkError);
    return "[heap] busy " + formatted("%.2f", gibibytes(static_cast<double>(report.busyBytes))) + " GiB in " + grouped(report.busyBlocks) + " blocks (zero pages ~" + formatted("%.2f", gibibytes(report.BusyZeroPages() * PageBytes)) + " GiB), free " + formatted("%.2f", gibibytes(static_cast<double>(report.freeBytes))) + " GiB in " + grouped(report.freeBlocks) + " blocks (zero pages ~" + formatted("%.2f", gibibytes(report.FreeZeroPages() * PageBytes)) + " GiB), uncommitted " + formatted("%.2f", gibibytes(static_cast<double>(report.uncommittedBytes))) + " GiB, " + grouped(report.regions) + " regions; private " + formatted("%.1f", gibibytes(static_cast<double>(report.privateBytes))) + " GiB; walk " + formatted("%.0f", report.walkMilliseconds) + " ms; zero-page samples 1 in " + grouped(report.busyStride) + " busy pages (" + grouped(report.busySampled) + "), 1 in " + grouped(report.freeStride) + " free pages (" + grouped(report.freeSampled) + ")";
}

std::string FormatHeapTrim(const HeapTrim& trim) {
    const auto optimize = trim.optimized ? "HeapOptimizeResources ok, " + privateChange(trim.privateBefore, trim.privateAfterOptimize) : formatted("HeapOptimizeResources failed (error %lu), ", trim.error) + privateChange(trim.privateBefore, trim.privateAfterOptimize);
    return "[heap-trim] " + optimize + formatted(" in %.0f ms; _heapmin %d, ", trim.optimizeMilliseconds, trim.heapminResult) + privateChange(trim.privateAfterOptimize, trim.privateAfterHeapmin) + formatted(" in %.0f ms", trim.heapminMilliseconds);
}

#ifdef _WIN32
std::uint64_t ProcessPrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) throw std::runtime_error(formatted("cannot read the process's private bytes (error %lu)", GetLastError()));
    return counters.PrivateUsage;
}

HeapReport ReportProcessHeap(std::size_t samplesPerKind) {
    if (samplesPerKind == 0) throw std::invalid_argument("a heap report needs at least one sample per kind");
    HeapReport report;
    report.privateBytes = ProcessPrivateBytes();
    const auto strideFor = [&](std::uint64_t previous) {
        const auto pages = previous != 0 ? previous : report.privateBytes / PageBytes;
        return std::max<std::uint64_t>(1, pages / samplesPerKind);
    };
    report.busyStride = strideFor(previousBusyPages.load(std::memory_order_relaxed));
    report.freeStride = strideFor(previousFreePages.load(std::memory_order_relaxed));
    const HANDLE heap = GetProcessHeap();
    const auto start = std::chrono::steady_clock::now();
    if (!HeapLock(heap)) {
        report.walkError = GetLastError();
        return report;
    }
    PROCESS_HEAP_ENTRY entry{};
    while (HeapWalk(heap, &entry)) {
        if ((entry.wFlags & PROCESS_HEAP_REGION) != 0) {
            ++report.regions;
            continue;
        }
        if ((entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) != 0) {
            report.uncommittedBytes += entry.cbData;
            continue;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(entry.lpData);
        if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
            report.busyBytes += entry.cbData;
            ++report.busyBlocks;
            samplePages(address, entry.cbData, report.busyStride, report.busyPages, report.busySampled, report.busyZeroSampled);
        } else {
            report.freeBytes += entry.cbData;
            ++report.freeBlocks;
            samplePages(address, entry.cbData, report.freeStride, report.freePages, report.freeSampled, report.freeZeroSampled);
        }
    }
    report.walkError = GetLastError();
    HeapUnlock(heap);
    report.walkMilliseconds = millisecondsSince(start);
    report.walked = report.walkError == ERROR_NO_MORE_ITEMS;
    if (report.walked) {
        report.walkError = 0;
        previousBusyPages.store(report.busyPages, std::memory_order_relaxed);
        previousFreePages.store(report.freePages, std::memory_order_relaxed);
    }
    return report;
}

HeapTrim TrimProcessHeap() {
    HeapTrim trim;
    trim.privateBefore = ProcessPrivateBytes();
    HEAP_OPTIMIZE_RESOURCES_INFORMATION information{HEAP_OPTIMIZE_RESOURCES_CURRENT_VERSION, 0};
    auto start = std::chrono::steady_clock::now();
    trim.optimized = HeapSetInformation(GetProcessHeap(), HeapOptimizeResources, &information, sizeof(information)) != FALSE;
    if (!trim.optimized) trim.error = GetLastError();
    trim.optimizeMilliseconds = millisecondsSince(start);
    trim.privateAfterOptimize = ProcessPrivateBytes();
    start = std::chrono::steady_clock::now();
    trim.heapminResult = _heapmin();
    trim.heapminMilliseconds = millisecondsSince(start);
    trim.privateAfterHeapmin = ProcessPrivateBytes();
    return trim;
}

void StartHeapWatch() {
    const auto report = switchSeconds("APS5_HEAP_REPORT");
    const auto trim = switchSeconds("APS5_HEAP_TRIM");
    if (report == 0 && trim == 0) return;
    heapWatchThread = std::jthread([report, trim](std::stop_token token) {
        using Clock = std::chrono::steady_clock;
        std::mutex mutex;
        std::condition_variable_any wake;
        auto nextReport = Clock::now() + std::chrono::seconds(report);
        auto nextTrim = Clock::now() + std::chrono::seconds(trim);
        try {
            while (!token.stop_requested()) {
                const auto due = report == 0 ? nextTrim : trim == 0 ? nextReport : std::min(nextReport, nextTrim);
                {
                    std::unique_lock lock(mutex);
                    static_cast<void>(wake.wait_until(lock, token, due, [] { return false; }));
                }
                if (token.stop_requested()) break;
                const auto now = Clock::now();
                if (report != 0 && now >= nextReport) {
                    ProfilePrint_nid_no_patch("%s\n", FormatHeapReport(ReportProcessHeap(WatchSamplesPerKind)).c_str());
                    nextReport = now + std::chrono::seconds(report);
                }
                if (trim != 0 && now >= nextTrim) {
                    ProfilePrint_nid_no_patch("%s\n", FormatHeapTrim(TrimProcessHeap()).c_str());
                    nextTrim = now + std::chrono::seconds(trim);
                }
            }
        } catch (const std::exception& error) {
            ProfilePrint_nid_no_patch("[heap] the heap watch stopped: %s\n", error.what());
        }
    });
}

void StopHeapWatch() {
    heapWatchThread.request_stop();
    if (heapWatchThread.joinable()) heapWatchThread.join();
}
#else
std::uint64_t ProcessPrivateBytes() {
    throw std::logic_error("the process's private bytes are read on Windows only");
}

HeapReport ReportProcessHeap(std::size_t) {
    throw std::logic_error("the process heap report is Windows-only");
}

HeapTrim TrimProcessHeap() {
    throw std::logic_error("the process heap trim is Windows-only");
}

void StartHeapWatch() {}

void StopHeapWatch() {}
#endif

}
