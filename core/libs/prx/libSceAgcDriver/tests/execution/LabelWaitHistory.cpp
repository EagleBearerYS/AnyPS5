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
#endif
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
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

}

int main() {
    try {
        auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        device.reset();
        GuestBlock block;
        StoredAfterSubmission(block.Data());
        HeldAtSubmission(block.Data());
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
