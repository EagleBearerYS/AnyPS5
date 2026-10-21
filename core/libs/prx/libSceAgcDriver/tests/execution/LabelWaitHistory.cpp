#include "VulkanTestDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void SetWaitTimeout() {
#ifdef _WIN32
    _putenv_s("APS5_GPU_WAIT_TIMEOUT_MS", "100");
#else
    setenv("APS5_GPU_WAIT_TIMEOUT_MS", "100", 1);
#endif
}

void SubmitAcb(std::vector<std::uint32_t>& words) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitAcb(0x20, &packet) == 0, "label wait timeout reason: a queue 0x20 submission failed");
}

std::string Reason(const ErrorCapture& capture, const volatile std::uint32_t* label) {
    char prefix[64];
    std::snprintf(prefix, sizeof(prefix), "[gpu]   label 0x%llx wait submitted", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(label)));
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
}

int main(int argc, char** argv) {
    const bool reasons = argc == 2 && std::strcmp(argv[1], "--timeout-reason") == 0;
    if (reasons) SetWaitTimeout();
    try {
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
        GuestBlock block;
        if (reasons) {
            TimeoutReasons(block.Data());
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
