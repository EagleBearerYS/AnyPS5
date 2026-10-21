#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libc/include/CpuTopology.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <future>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

int failures = 0;

void Require(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

struct ThreadView {
    int priority = 0;
    std::uint64_t mask = 0;
};

ThreadView CurrentThreadView() {
    ThreadView view;
#ifdef _WIN32
    view.priority = GetThreadPriority(GetCurrentThread());
#elif defined(__linux__)
    errno = 0;
    view.priority = getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
    cpu_set_t set;
    CPU_ZERO(&set);
    if (pthread_getaffinity_np(pthread_self(), sizeof(set), &set) == 0) {
        for (unsigned cpu = 0; cpu < 64; ++cpu)
            if (CPU_ISSET(cpu, &set)) view.mask |= std::uint64_t{1} << cpu;
    }
#endif
    return view;
}

ThreadView ViewFromPool(const std::function<void(std::function<void()>)>& submit) {
    std::promise<ThreadView> seen;
    auto future = seen.get_future();
    submit([&seen] { seen.set_value(CurrentThreadView()); });
    return future.get();
}

void CheckPoolWorker(const char* pool, const ThreadView& submitter, const ThreadView& worker) {
    std::fprintf(stderr, "%s worker: priority %d (submitter %d), host mask 0x%llx (submitter 0x%llx)\n", pool, worker.priority, submitter.priority, static_cast<unsigned long long>(worker.mask), static_cast<unsigned long long>(submitter.mask));
#ifdef _WIN32
    Require(worker.priority == THREAD_PRIORITY_BELOW_NORMAL, std::string("the ") + pool + " worker does not run below normal priority");
#elif defined(__linux__)
    Require(worker.priority == std::min(submitter.priority + 5, 19), std::string("the ") + pool + " worker's nice value is " + std::to_string(worker.priority) + ", not five steps above its submitter's " + std::to_string(submitter.priority));
    const auto& placement = CpuTopology::Placement();
    const std::uint64_t expected = placement.reserved != 0 && placement.background != 0 ? placement.background : submitter.mask;
    Require(worker.mask == expected, std::string("the ") + pool + " worker runs on host mask " + Hex(worker.mask) + " instead of " + Hex(expected));
#endif
}

std::uint64_t Core(unsigned first, unsigned second) {
    return (std::uint64_t{1} << first) | (std::uint64_t{1} << second);
}

void CheckBackgroundCores() {
    std::vector<std::uint64_t> sixteen;
    for (unsigned core = 0; core < 16; ++core) sixteen.push_back(Core(core, core + 16));
    const auto wide = CpuTopology::PlaceGuestCpus(0xffffffffull, sixteen);
    Require(wide.reserved == 0x3fffull, "16 cores with 2 threads reserve the first thread of 14 cores for the guest CPUs");
    Require(wide.background == 0xc000c000ull, "16 cores with 2 threads leave the 2 cores without a guest CPU to the compile workers");

    std::vector<std::uint64_t> eight;
    for (unsigned core = 0; core < 8; ++core) eight.push_back(Core(core, core + 8));
    const auto narrow = CpuTopology::PlaceGuestCpus(0xffffull, eight);
    Require(narrow.reserved == 0x3fffull, "8 cores with 2 threads reserve 14 logical CPUs for the guest CPUs");
    Require(narrow.background == 0xc000ull, "8 cores with 2 threads leave the compile workers the 2 unreserved logical CPUs");

    std::vector<std::uint64_t> four;
    for (unsigned core = 0; core < 4; ++core) four.push_back(std::uint64_t{1} << core);
    const auto few = CpuTopology::PlaceGuestCpus(0xfull, four);
    Require(few.reserved == 0, "4 CPUs reserve nothing");
}

}

int main() {
    CheckBackgroundCores();
    const auto submitter = CurrentThreadView();
    CheckPoolWorker("registration preparation", submitter, ViewFromPool([](std::function<void()> task) { AgcDriver::DriverDetail::SubmitRegistrationPreparation(std::move(task)); }));
    CheckPoolWorker("library prebuild", submitter, ViewFromPool([](std::function<void()> task) { AgcDriver::DriverDetail::SubmitLibraryPrebuild(std::move(task)); }));
    if (failures != 0) {
        std::fprintf(stderr, "%d compile pool placement checks failed\n", failures);
        return 1;
    }
    std::printf("compile pool placement tests passed\n");
    return 0;
}
