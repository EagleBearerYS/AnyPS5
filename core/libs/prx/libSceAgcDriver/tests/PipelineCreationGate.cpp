#include "prx/libSceAgcDriver/Execution/include/PipelineCreation.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using AgcDriver::CreationAhead;
using AgcDriver::CreationGate;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void Bounded() {
    const std::vector<std::size_t> words{600000, 500000, 300000, 2000000, 100000, 900000, 50000, 1200000, 400000, 700000};
    std::atomic<bool> exceeded{false};
    std::atomic<std::size_t> overlapped{0};
    std::vector<std::thread> threads;
    for (const auto size : words) {
        threads.emplace_back([&, size] {
            const CreationAhead ahead;
            const CreationGate gate(size);
            const auto inFlight = CreationGate::InFlight();
            const auto creations = CreationGate::Creations();
            if (creations > 1u && inFlight > CreationGate::WordsInFlight) exceeded = true;
            if (creations > 1u) ++overlapped;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        });
    }
    for (auto& thread : threads) thread.join();
    Require(!exceeded, "creations together over the words in flight ran at the same time");
    Require(overlapped > 0u, "creations that fit together did not run at the same time");
    Require(CreationGate::InFlight() == 0u && CreationGate::Creations() == 0u, "the gate kept words of finished creations");
}

void InOrder() {
    std::mutex mutex;
    std::vector<char> admitted;
    const auto note = [&](char name) {
        std::lock_guard lock(mutex);
        admitted.push_back(name);
    };
    std::atomic<bool> release{false};
    std::thread first([&] {
        const CreationAhead ahead;
        const CreationGate gate(600000);
        note('a');
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (CreationGate::Creations() == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread large([&] {
        const CreationAhead ahead;
        const CreationGate gate(2000000);
        note('b');
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::thread small([&] {
        const CreationAhead ahead;
        const CreationGate gate(100000);
        note('c');
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    release = true;
    first.join();
    large.join();
    small.join();
    Require(admitted == std::vector<char>{'a', 'b', 'c'}, "a small creation passed a large one that waited before it");
}

void FramesFirst() {
    std::atomic<bool> release{false};
    std::atomic<bool> waiterAdmitted{false};
    std::thread large([&] {
        const CreationAhead ahead;
        const CreationGate gate(1500000);
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (CreationGate::Creations() == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread waiter([&] {
        const CreationAhead ahead;
        const CreationGate gate(100000);
        waiterAdmitted = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::atomic<bool> frameAdmitted{false};
    std::atomic<std::size_t> frameInFlight{0};
    std::thread inFrame([&] {
        const CreationGate gate(50000);
        frameInFlight = CreationGate::InFlight();
        frameAdmitted = true;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!frameAdmitted && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool admittedAtOnce = frameAdmitted;
    const bool waiterWaited = !waiterAdmitted;
    release = true;
    large.join();
    waiter.join();
    inFrame.join();
    Require(admittedAtOnce && frameInFlight == 1550000u, "a creation inside a frame waited for creations ahead of frames");
    Require(waiterWaited, "a creation ahead of frames passed a running one larger than the words in flight");
    std::atomic<bool> frameDone{false};
    std::atomic<bool> aheadAdmittedEarly{false};
    std::thread frame([&] {
        const CreationGate gate(900000);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        frameDone = true;
    });
    while (CreationGate::Creations() == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread ahead([&] {
        const CreationAhead marked;
        const CreationGate gate(200000);
        if (!frameDone) aheadAdmittedEarly = true;
    });
    frame.join();
    ahead.join();
    Require(!aheadAdmittedEarly, "a creation ahead of frames did not count the words of a creation inside a frame");
}

void LargeOnOneThread() {
    using AgcDriver::CreationThread;
    const auto words = CreationThread::Words();
    Require(words == CreationThread::RetainedBudgetBytes / (std::max(2u, std::thread::hardware_concurrency()) * CreationThread::RetainedBytesPerWord), "the large-module words are not the retained budget shared by the pool's threads");
    const auto caller = std::this_thread::get_id();
    const auto where = [](std::size_t size) {
        return CreationThread::Run(size, [] { return std::this_thread::get_id(); });
    };
    Require(where(words) == caller, "a large creation inside a frame left its caller's thread");
    const CreationAhead ahead;
    Require(where(words - 1u) == caller, "a creation ahead below the large-module words left its caller's thread");
    const auto dedicated = where(words);
    Require(dedicated != caller && !CreationThread::Current(), "a large creation ahead ran on its caller's thread");
    std::vector<std::thread::id> seen(4);
    std::vector<std::thread> threads;
    for (std::size_t index = 0; index < seen.size(); ++index) {
        threads.emplace_back([&, index] {
            const CreationAhead marked;
            seen[index] = CreationThread::Run(words * (index + 1u), [] {
                Require(CreationAhead::Active(), "a large creation lost its origin ahead of frames on the creation thread");
                return std::this_thread::get_id();
            });
        });
    }
    for (auto& thread : threads) thread.join();
    for (const auto& id : seen) Require(id == dedicated, "large creations from several threads ran on more than one thread");
    bool thrown = false;
    try {
        static_cast<void>(CreationThread::Run(words, []() -> int { throw std::runtime_error("creation failed"); }));
    } catch (const std::runtime_error& error) {
        thrown = std::string(error.what()) == "creation failed";
    }
    Require(thrown, "a large creation's failure did not reach its caller");
}

}

int main() {
    try {
        Bounded();
        InOrder();
        FramesFirst();
        LargeOnOneThread();
        std::puts("pipeline creation gate tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
