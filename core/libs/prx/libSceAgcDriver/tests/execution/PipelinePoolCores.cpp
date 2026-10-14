#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

}

int main() {
    try {
        const auto tasks = std::min(std::max(2u, std::thread::hardware_concurrency()), 8u);
        std::mutex mutex;
        std::condition_variable changed;
        unsigned arrived = 0;
        unsigned met = 0;
        unsigned finished = 0;
        for (unsigned task = 0; task < tasks; ++task) {
            AgcDriver::DriverDetail::SubmitLibraryPrebuild([&] {
                std::unique_lock lock(mutex);
                ++arrived;
                changed.notify_all();
                if (changed.wait_for(lock, std::chrono::seconds(2), [&] { return arrived == tasks; })) ++met;
                ++finished;
                changed.notify_all();
            });
        }
        std::unique_lock lock(mutex);
        Require(changed.wait_for(lock, std::chrono::seconds(10), [&] { return finished == tasks; }), "the pipeline pool did not finish " + std::to_string(tasks) + " tasks");
        Require(met == tasks, std::to_string(tasks) + " pipeline pool tasks submitted together did not all run at the same time (" + std::to_string(met) + " met)");
        lock.unlock();
        AgcDriver::DriverDetail::CloseRegistrationPreparation();
        std::puts("pipeline pool tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
