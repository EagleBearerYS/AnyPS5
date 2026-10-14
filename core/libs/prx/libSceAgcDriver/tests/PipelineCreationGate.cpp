#include "prx/libSceAgcDriver/Execution/include/PipelineCreation.hpp"
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
        const CreationGate gate(600000);
        note('a');
        while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (CreationGate::Creations() == 0u) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread large([&] {
        const CreationGate gate(2000000);
        note('b');
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::thread small([&] {
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

}

int main() {
    try {
        Bounded();
        InOrder();
        std::puts("pipeline creation gate tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
