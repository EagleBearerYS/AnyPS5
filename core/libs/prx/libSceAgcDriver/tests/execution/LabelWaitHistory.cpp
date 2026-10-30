#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Link = 0x21061300u;

struct Labels {
    alignas(256) volatile std::uint32_t label = 0;
    alignas(256) volatile std::uint32_t gate = 0;
    alignas(256) volatile std::uint32_t done = 0;
    alignas(256) volatile std::uint32_t spare = 0;
    alignas(256) volatile std::uint32_t spareDone = 0;
    alignas(256) volatile std::uint32_t producerGate = 0;
    alignas(256) volatile std::uint32_t crossLabel = 0;
    alignas(256) volatile std::uint32_t crossDone = 0;
    alignas(256) volatile std::uint32_t tailLabel = 0;
    alignas(256) volatile std::uint32_t tailDone = 0;
    alignas(256) volatile std::uint32_t watchedDone = 0;
};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "label wait history: cannot allocate the guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    Labels& Data() { return *reinterpret_cast<Labels*>(block); }

private:
    std::uint8_t* block = nullptr;
};

std::uint32_t Low(const volatile void* pointer) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

std::uint32_t High(const volatile void* pointer) {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)) >> 32u);
}

void ReleaseLabel(std::vector<std::uint32_t>& words, volatile std::uint32_t* target, std::uint32_t value) {
    words.insert(words.end(), {0xc0064900u, 0x514u, (1u << 29u) | (2u << 24u), Low(target), High(target), value, 0u, 0u});
}

void WriteData(std::vector<std::uint32_t>& words, volatile std::uint32_t* target, std::uint32_t value) {
    words.insert(words.end(), {0xc0033700u, 0x00100200u, Low(target), High(target), value});
}

void WaitEqual(std::vector<std::uint32_t>& words, volatile std::uint32_t* target, std::uint32_t value) {
    words.insert(words.end(), {0xc0053c00u, 0x13u, Low(target), High(target), value, 0xffffffffu, 0x19u});
}

void Until(const volatile std::uint32_t& word, std::uint32_t value, std::chrono::milliseconds limit, const std::string& what) {
    const auto start = std::chrono::steady_clock::now();
    while (word != value) {
        Require(std::chrono::steady_clock::now() - start < limit, what);
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
}

void Produce(Labels& labels) {
    std::vector<std::uint32_t> words;
    ReleaseLabel(words, &labels.label, 1u);
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "label wait history: the producer's submission failed");
}

void SubmitGatedWait(Labels& labels) {
    std::vector<std::uint32_t> words;
    WaitEqual(words, &labels.gate, 1u);
    WaitEqual(words, &labels.label, 1u);
    WriteData(words, &labels.done, 1u);
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitAcb(0x20, &packet) == 0, "label wait history: the waiter's submission failed");
}

void Reset(Labels& labels) {
    labels.label = 0u;
    labels.gate = 0u;
    labels.done = 0u;
}

void StoredAfterSubmission(Labels& labels) {
    Reset(labels);
    SubmitGatedWait(labels);
    Produce(labels);
    Until(labels.label, 1u, std::chrono::seconds(10), "label wait history: the producer's label never reached memory");
    labels.label = Link;
    labels.gate = 1u;
    Until(labels.done, 1u, std::chrono::milliseconds(800), "label wait history: a wait whose label was stored after the wait's submission and overwritten before the queue reached it did not end from the store history");
    AgcDriverWaitIdle_nid_postfix();
    std::cout << "label wait history: a label stored after the wait's submission satisfies the wait after an overwrite\n";
}

void HeldAtSubmission(Labels& labels) {
    Reset(labels);
    Produce(labels);
    Until(labels.label, 1u, std::chrono::seconds(10), "label wait history: the producer's label never reached memory");
    AgcDriverWaitIdle_nid_postfix();
    SubmitGatedWait(labels);
    labels.label = Link;
    labels.gate = 1u;
    const auto start = std::chrono::steady_clock::now();
    while (labels.done == 0u && std::chrono::steady_clock::now() - start < std::chrono::milliseconds(800)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool ended = labels.done != 0u;
    if (!ended) {
        labels.label = 1u;
        Until(labels.done, 1u, std::chrono::seconds(10), "label wait history: the wait did not end after its label was stored again");
    }
    AgcDriverWaitIdle_nid_postfix();
    Require(ended, "label wait history: a wait whose label held its value at the wait's submission, and was overwritten before the queue reached it, did not end from the value at submission");
    std::cout << "label wait history: a label that held its value at the wait's submission satisfies the wait after an overwrite\n";
}


#ifdef _WIN32
int Duplicate(int descriptor) { return _dup(descriptor); }
int Redirect(int from, int to) { return _dup2(from, to); }
int Close(int descriptor) { return _close(descriptor); }
int Descriptor(std::FILE* file) { return _fileno(file); }
#else
int Duplicate(int descriptor) { return dup(descriptor); }
int Redirect(int from, int to) { return dup2(from, to); }
int Close(int descriptor) { return close(descriptor); }
int Descriptor(std::FILE* file) { return fileno(file); }
#endif

class ErrorCapture {
public:
    ErrorCapture() : path(std::filesystem::temp_directory_path() / ("anyps5_label_wait_timeout_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".log")) {
        std::fflush(stderr);
        saved = Duplicate(Descriptor(stderr));
        file = std::fopen(path.string().c_str(), "w+");
        Require(saved >= 0 && file != nullptr && Redirect(Descriptor(file), Descriptor(stderr)) >= 0, "label wait timeout reason: cannot capture stderr");
    }

    ~ErrorCapture() { Restore(); }

    ErrorCapture(const ErrorCapture&) = delete;
    ErrorCapture& operator=(const ErrorCapture&) = delete;

    std::string Text() const {
        std::fflush(stderr);
        std::ifstream input(path);
        std::stringstream text;
        text << input.rdbuf();
        return text.str();
    }

    std::string Restore() {
        if (file == nullptr) return {};
        auto text = Text();
        Redirect(saved, Descriptor(stderr));
        Close(saved);
        std::fclose(file);
        file = nullptr;
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        return text;
    }

private:
    std::filesystem::path path;
    int saved = -1;
    std::FILE* file = nullptr;
};

std::string Hex(std::uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
    return text;
}

std::string Hex(const volatile std::uint32_t* pointer) {
    return Hex(reinterpret_cast<std::uintptr_t>(pointer));
}

void SetWaitTimeout() {
#ifdef _WIN32
    _putenv_s("APS5_GPU_WAIT_TIMEOUT_MS", "100");
#else
    setenv("APS5_GPU_WAIT_TIMEOUT_MS", "100", 1);
#endif
}

void SubmitAcb(std::vector<std::uint32_t>& words, std::uint32_t queue = 0x20) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitAcb(queue, &packet) == 0, "label wait timeout reason: a queue 0x" + Hex(queue) + " submission failed");
}

std::string Line(const ErrorCapture& capture, const volatile std::uint32_t* label, const std::string& marker) {
    const auto prefix = "[gpu]   label 0x" + Hex(reinterpret_cast<std::uintptr_t>(label)) + " " + marker;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        const auto text = capture.Text();
        const auto at = text.find(prefix);
        const auto end = at == std::string::npos ? std::string::npos : text.find('\n', at);
        if (end != std::string::npos) return text.substr(at, end - at);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {};
}

std::string Reason(const ErrorCapture& capture, const volatile std::uint32_t* label) {
    return Line(capture, label, "wait submitted");
}

void TimeoutReasons(Labels& labels) {
    Reset(labels);
    labels.spare = 0u;
    labels.spareDone = 0u;
    Produce(labels);
    Until(labels.label, 1u, std::chrono::seconds(10), "label wait timeout reason: the producer's label never reached memory");
    AgcDriverWaitIdle_nid_postfix();
    ErrorCapture capture;
    std::vector<std::uint32_t> writer;
    WaitEqual(writer, &labels.gate, 1u);
    ReleaseLabel(writer, &labels.label, 2u);
    SubmitAcb(writer);
    std::vector<std::uint32_t> waiter;
    WaitEqual(waiter, &labels.label, 1u);
    WriteData(waiter, &labels.done, 1u);
    SubmitAcb(waiter);
    labels.gate = 1u;
    const auto unfinished = Reason(capture, &labels.label);
    labels.label = 1u;
    Until(labels.done, 1u, std::chrono::seconds(10), "label wait timeout reason: the wait did not end after its label was stored again");
    std::vector<std::uint32_t> spare;
    WaitEqual(spare, &labels.spare, 1u);
    WriteData(spare, &labels.spareDone, 1u);
    SubmitAcb(spare);
    const auto checked = Reason(capture, &labels.spare);
    labels.spare = 1u;
    Until(labels.spareDone, 1u, std::chrono::seconds(10), "label wait timeout reason: the spare wait did not end after its label was stored");
    AgcDriverWaitIdle_nid_postfix();
    const auto log = capture.Restore();
    Require(!unfinished.empty(), "label wait timeout reason: the timed-out wait printed no reason line; stderr was:\n" + log);
    std::smatch match;
    const std::regex unfinishedLine(R"(wait submitted at stamp (\d+): not checked at submission \(queue 0x20 serial (\d+) writes it\); stores newest first: stamp (\d+) value 0x2 queue 0x20 opcode 0x49, stamp (\d+) value 0x1 queue 0x0 opcode 0x49(, .*)?; now (\d+)$)");
    Require(std::regex_search(unfinished, match, unfinishedLine), "label wait timeout reason: the line does not name the submission stamp, the queue's own unfinished writer and the stored values: " + unfinished);
    const auto submitted = std::stoull(match[1].str());
    const auto writerSerial = std::stoull(match[2].str());
    const auto after = std::stoull(match[3].str());
    const auto before = std::stoull(match[4].str());
    const auto now = std::stoull(match[6].str());
    Require(writerSerial != 0u && before < submitted && submitted < after && after <= now, "label wait timeout reason: the stamps are out of order (store of 1 before the submission, store of 2 after it, then now): " + unfinished);
    Require(!checked.empty(), "label wait timeout reason: the timed-out spare wait printed no reason line; stderr was:\n" + log);
    const std::regex checkedLine(R"(wait submitted at stamp \d+: checked at submission: value 0x0; stores newest first: none recorded; now \d+$)");
    Require(std::regex_search(checked, match, checkedLine), "label wait timeout reason: the line does not name the value checked at submission and the empty store history: " + checked);
    std::cout << unfinished << '\n' << checked << '\n';
    std::cout << "label wait timeout reason: a timed-out wait names its submission stamp, the check at submission and the label's stores\n";
}

class WatchedBlock {
public:
    WatchedBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, BlockBytes));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        void* raw = mmap(nullptr, BlockBytes * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        Require(raw != MAP_FAILED, "label wait timeout reason: cannot map the watched block");
        const auto begin = reinterpret_cast<std::uintptr_t>(raw);
        const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
        if (aligned != begin) munmap(raw, aligned - begin);
        if (aligned + BlockBytes != begin + BlockBytes * 2) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + BlockBytes - aligned);
        block = reinterpret_cast<std::uint8_t*>(aligned);
        GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
#endif
        Require(block != nullptr, "label wait timeout reason: cannot allocate the watched block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~WatchedBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
#else
        munmap(block, BlockBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
#endif
    }

    WatchedBlock(const WatchedBlock&) = delete;
    WatchedBlock& operator=(const WatchedBlock&) = delete;

    volatile std::uint32_t* Label() { return reinterpret_cast<volatile std::uint32_t*>(block + 256); }

private:
    std::uint8_t* block = nullptr;
};

std::uint64_t BlockOf(const volatile std::uint32_t* label) {
    return reinterpret_cast<std::uintptr_t>(label) & ~std::uint64_t{0xffff};
}

struct Moments {
    std::uint64_t copiedStamp = 0;
    std::uint64_t checkedStamp = 0;
    std::uint64_t nowStamp = 0;
    double copiedMs = 0;
    double checkedMs = 0;
    double nowMs = 0;
    double validMs = 0;
};

Moments ReadMoments(const std::string& line, bool tail) {
    const std::regex submitted(R"(submitted by the title at stamp (\d+) value 0x0 \(\+([0-9.]+) ms\), checked at stamp (\d+) value 0x0 \(\+([0-9.]+) ms\), kind submit; now stamp (\d+) \(\+([0-9.]+) ms\)$)");
    const std::regex copied(R"(tail copied at stamp (\d+) value 0x0 \(\+([0-9.]+) ms\), checked at stamp (\d+) value 0x0 \(\+([0-9.]+) ms\), kind rewind-tail, valid bit seen \+([0-9.]+) ms; now stamp (\d+) \(\+([0-9.]+) ms\)$)");
    std::smatch match;
    Require(std::regex_search(line, match, tail ? copied : submitted), "label wait timeout reason: the line does not name the wait's copy and check moments: " + line);
    Moments moments;
    moments.copiedStamp = std::stoull(match[1].str());
    moments.copiedMs = std::stod(match[2].str());
    moments.checkedStamp = std::stoull(match[3].str());
    moments.checkedMs = std::stod(match[4].str());
    const std::size_t now = tail ? 6 : 5;
    if (tail) moments.validMs = std::stod(match[5].str());
    moments.nowStamp = std::stoull(match[now].str());
    moments.nowMs = std::stod(match[now + 1].str());
    Require(moments.copiedStamp <= moments.checkedStamp && moments.checkedStamp <= moments.nowStamp && moments.validMs <= moments.copiedMs && moments.copiedMs <= moments.checkedMs && moments.checkedMs + 99.0 <= moments.nowMs, "label wait timeout reason: the moments are out of order (copy, check, then the timeout at least 100 ms later): " + line);
    return moments;
}

std::string WaitingLine(std::uint32_t queue, const std::string& serial, const volatile std::uint32_t* label) {
    return "queue 0x" + Hex(queue) + ": executing serial " + serial + " at dword 0 opcode 0x3c, waiting on 0x" + Hex(label) + " function 3 ref 0x1 mask 0xffffffff; 0 pending$";
}

void CrossQueueProducer(Labels& labels) {
    labels.producerGate = 0u;
    labels.crossLabel = 0u;
    labels.crossDone = 0u;
    ErrorCapture capture;
    std::vector<std::uint32_t> producer;
    WaitEqual(producer, &labels.producerGate, 1u);
    ReleaseLabel(producer, &labels.crossLabel, 1u);
    SubmitAcb(producer, 0x21);
    std::vector<std::uint32_t> waiter;
    WaitEqual(waiter, &labels.crossLabel, 1u);
    WriteData(waiter, &labels.crossDone, 1u);
    SubmitAcb(waiter);
    const auto moments = Line(capture, &labels.crossLabel, "submitted by the title");
    const auto block = Line(capture, &labels.crossLabel, "block 0x");
    const auto writers = Line(capture, &labels.crossLabel, "writers pending or running:");
    const auto producerState = Line(capture, &labels.crossLabel, "queue 0x21:");
    const auto waiterState = Line(capture, &labels.crossLabel, "queue 0x20:");
    labels.producerGate = 1u;
    Until(labels.crossDone, 1u, std::chrono::seconds(10), "label wait timeout reason: the wait did not end after its producer on queue 0x21 was released");
    AgcDriverWaitIdle_nid_postfix();
    const auto log = capture.Restore();
    Require(!moments.empty() && !block.empty() && !writers.empty() && !producerState.empty() && !waiterState.empty(), "label wait timeout reason: the timed-out cross-queue wait printed no moments, block, writers or queue line; stderr was:\n" + log);
    ReadMoments(moments, false);
    std::smatch match;
    const std::regex blockLine(R"(block 0x([0-9a-f]+): (not watched, no write stamps|watched); write generation at the check \d+, tracker generation now \d+)");
    Require(std::regex_search(block, match, blockLine) && std::stoull(match[1].str(), nullptr, 16) == BlockOf(&labels.crossLabel), "label wait timeout reason: the line does not name the label's block and its generations: " + block);
    const std::regex writersLine(R"(writers pending or running: queue 0x21 serial (\d+) \(running\)$)");
    Require(std::regex_search(writers, match, writersLine), "label wait timeout reason: the line does not name the producer running on queue 0x21: " + writers);
    Require(std::regex_search(producerState, std::regex(WaitingLine(0x21, match[1].str(), &labels.producerGate))), "label wait timeout reason: the queue 0x21 line does not show the producer blocked on its own wait: " + producerState);
    Require(std::regex_search(waiterState, std::regex(WaitingLine(0x20, R"(\d+)", &labels.crossLabel))), "label wait timeout reason: the queue 0x20 line does not show the timed-out wait: " + waiterState);
    std::cout << moments << '\n' << block << '\n' << writers << '\n' << producerState << '\n' << waiterState << '\n';
    std::cout << "label wait timeout reason: a timed-out wait names its copy and check moments, its block and its producer blocked on another queue\n";
}

void RewindTail(Labels& labels) {
    labels.tailLabel = 0u;
    labels.tailDone = 0u;
    ErrorCapture capture;
    std::vector<std::uint32_t> words{0xc0005900u, 0u};
    WaitEqual(words, &labels.tailLabel, 1u);
    WriteData(words, &labels.tailDone, 1u);
    SubmitAcb(words);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::atomic_ref<std::uint32_t>(words[1]).store(0x80000000u, std::memory_order_release);
    AgcDriver::GuestMemory::MarkWritten(reinterpret_cast<std::uintptr_t>(&words[1]), 4);
    const auto moments = Line(capture, &labels.tailLabel, "tail copied");
    const auto waiterState = Line(capture, &labels.tailLabel, "queue 0x20:");
    labels.tailLabel = 1u;
    Until(labels.tailDone, 1u, std::chrono::seconds(10), "label wait timeout reason: the REWIND tail's wait did not end after its label was stored");
    AgcDriverWaitIdle_nid_postfix();
    const auto log = capture.Restore();
    Require(!moments.empty() && !waiterState.empty(), "label wait timeout reason: the timed-out REWIND tail wait printed no moments or queue line; stderr was:\n" + log);
    ReadMoments(moments, true);
    Require(std::regex_search(waiterState, std::regex(WaitingLine(0x20, R"(\d+)", &labels.tailLabel))), "label wait timeout reason: the queue 0x20 line does not show the REWIND tail's wait: " + waiterState);
    std::cout << moments << '\n' << waiterState << '\n';
    std::cout << "label wait timeout reason: a timed-out wait in a REWIND tail names the tail copy and when the valid bit was seen\n";
}

void WatchedStore(Labels& labels, WatchedBlock* watched) {
    if (watched == nullptr) {
        std::cout << "label wait timeout reason: guest writes are not watched here, the watched block line is not checked\n";
        return;
    }
    auto* label = watched->Label();
    labels.watchedDone = 0u;
    ErrorCapture capture;
    std::vector<std::uint32_t> waiter;
    WaitEqual(waiter, label, 1u);
    WriteData(waiter, &labels.watchedDone, 1u);
    SubmitAcb(waiter);
    *label = 7u;
    const auto block = Line(capture, label, "block 0x");
    *label = 1u;
    Until(labels.watchedDone, 1u, std::chrono::seconds(10), "label wait timeout reason: the wait on the watched label did not end after its label was stored");
    AgcDriverWaitIdle_nid_postfix();
    const auto log = capture.Restore();
    Require(!block.empty(), "label wait timeout reason: the timed-out wait on a watched label printed no block line; stderr was:\n" + log);
    std::smatch match;
    const std::regex blockLine(R"(block 0x([0-9a-f]+): watched; write generation at the check (\d+), tracker generation now \d+; stamp \d+ cpu stamp \d+, after a collect stamp (\d+) cpu stamp (\d+); stamped after the check yes, a CPU store collected after the check yes$)");
    Require(std::regex_search(block, match, blockLine) && std::stoull(match[1].str(), nullptr, 16) == BlockOf(label), "label wait timeout reason: the line does not name the CPU store collected after the check: " + block);
    Require(std::stoull(match[4].str()) > std::stoull(match[2].str()) && std::stoull(match[3].str()) >= std::stoull(match[4].str()), "label wait timeout reason: the block stamps do not follow the write generation at the check: " + block);
    std::cout << block << '\n';
    std::cout << "label wait timeout reason: a timed-out wait on a watched label names the CPU store collected after its check\n";
}
}

int main(int argc, char** argv) {
    const bool reasons = argc == 2 && std::strcmp(argv[1], "--timeout-reason") == 0;
    if (reasons) SetWaitTimeout();
    try {
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
        GuestBlock block;
        std::optional<WatchedBlock> watched;
        if (reasons && AgcDriver::GuestMemory::WriteWatched()) watched.emplace();
        if (reasons) {
            TimeoutReasons(block.Data());
            CrossQueueProducer(block.Data());
            RewindTail(block.Data());
            WatchedStore(block.Data(), watched ? &*watched : nullptr);
        } else {
            StoredAfterSubmission(block.Data());
            HeldAtSubmission(block.Data());
        }
        AgcDriverShutdown_nid_postfix();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& shutdownError) {
            std::cerr << shutdownError.what() << '\n';
        }
        return 1;
    }
}
