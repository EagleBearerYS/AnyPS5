#include "SceTypes.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <intrin.h>
#else
#include <time.h>
#endif

extern "C" {
int APS5_VABI clock_gettime_nid_postfix(int clockId, KernelTimespec* tp);
int APS5_VABI scePthreadCondInit(PthreadCond* cond, const PthreadCondattr* attr, const char* name);
int APS5_VABI scePthreadCondDestroy(PthreadCond* cond);
int APS5_VABI scePthreadCondTimedwait(PthreadCond* cond, PthreadMutex* mutex, KernelUseconds usec);
int APS5_VABI scePthreadMutexInit(PthreadMutex* mutex, const PthreadMutexattr* attr, const char* name);
int APS5_VABI scePthreadMutexDestroy(PthreadMutex* mutex);
int APS5_VABI scePthreadMutexLock(PthreadMutex* mutex);
int APS5_VABI scePthreadMutexTimedlock(PthreadMutex* mutex, KernelUseconds usec);
int APS5_VABI scePthreadMutexUnlock(PthreadMutex* mutex);
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
}

static constexpr int GUEST_CLOCK_MONOTONIC = 4;
static constexpr int SCE_KERNEL_ERROR_ETIMEDOUT = static_cast<int>(0x8002003C);
static constexpr std::int64_t NANOS_PER_SECOND = 1000000000LL;
static constexpr std::int64_t NANOS_PER_MICROSECOND = 1000LL;
static constexpr KernelUseconds TIMEOUT_MICROS = 250;
static constexpr int TIMEOUTS = 1000;
static constexpr double MAX_CPU_SHARE = 0.25;
static constexpr std::int64_t MAX_MEDIAN_LATE_NANOS = 2000000;

static void Require(bool value) { if (!value) std::abort(); }

static std::int64_t MonotonicNanos() {
    KernelTimespec time{-1, -1};
    Require(clock_gettime_nid_postfix(GUEST_CLOCK_MONOTONIC, &time) == 0);
    return time.tv_sec * NANOS_PER_SECOND + time.tv_nsec;
}

#ifdef _WIN32
static double BusyTicks() {
    ULONG64 cycles = 0;
    Require(QueryThreadCycleTime(GetCurrentThread(), &cycles) != FALSE);
    return static_cast<double>(cycles);
}

static double ElapsedTicks() {
    return static_cast<double>(__rdtsc());
}
#else
static double HostNanos(clockid_t id) {
    timespec time{};
    Require(clock_gettime(id, &time) == 0);
    return static_cast<double>(time.tv_sec) * 1e9 + static_cast<double>(time.tv_nsec);
}

static double BusyTicks() {
    return HostNanos(CLOCK_THREAD_CPUTIME_ID);
}

static double ElapsedTicks() {
    return HostNanos(CLOCK_MONOTONIC);
}
#endif

template <class Wait>
static void TimeoutsLeaveTheCpu(const char* name, Wait wait) {
    std::vector<std::int64_t> late;
    late.reserve(TIMEOUTS);
    const double busyStart = BusyTicks();
    const double elapsedStart = ElapsedTicks();
    for (int i = 0; i < TIMEOUTS; ++i) {
        const std::int64_t start = MonotonicNanos();
        Require(wait() == SCE_KERNEL_ERROR_ETIMEDOUT);
        late.push_back(MonotonicNanos() - start - static_cast<std::int64_t>(TIMEOUT_MICROS) * NANOS_PER_MICROSECOND);
    }
    const double share = (BusyTicks() - busyStart) / (ElapsedTicks() - elapsedStart);
    std::nth_element(late.begin(), late.begin() + TIMEOUTS / 2, late.end());
    const std::int64_t medianLate = late[TIMEOUTS / 2];
    std::printf("%s(%u us) timing out: %.1f%% of a core, median %lld us late\n", name, TIMEOUT_MICROS, share * 100.0, static_cast<long long>(medianLate / NANOS_PER_MICROSECOND));
    std::fflush(stdout);
    Require(share < MAX_CPU_SHARE);
    Require(medianLate < MAX_MEDIAN_LATE_NANOS);
}

static void CondTimeoutsLeaveTheCpu() {
    PthreadCond cond = nullptr;
    PthreadMutex mutex = nullptr;
    Require(scePthreadCondInit(&cond, nullptr, "timed wait cpu") == 0);
    Require(scePthreadMutexInit(&mutex, nullptr, "timed wait cpu") == 0);
    Require(scePthreadMutexLock(&mutex) == 0);
    TimeoutsLeaveTheCpu("scePthreadCondTimedwait", [&] { return scePthreadCondTimedwait(&cond, &mutex, TIMEOUT_MICROS); });
    Require(scePthreadMutexUnlock(&mutex) == 0);
    Require(scePthreadMutexDestroy(&mutex) == 0);
    Require(scePthreadCondDestroy(&cond) == 0);
}

static PthreadMutex heldMutex = nullptr;
static std::atomic<bool> mutexHeld{false};
static std::atomic<bool> releaseMutex{false};

static void* APS5_VABI HoldMutex(void*) {
    Require(scePthreadMutexLock(&heldMutex) == 0);
    mutexHeld.store(true);
    while (!releaseMutex.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Require(scePthreadMutexUnlock(&heldMutex) == 0);
    return nullptr;
}

static void MutexTimeoutsLeaveTheCpu() {
    Require(scePthreadMutexInit(&heldMutex, nullptr, "timed wait cpu") == 0);
    Pthread holder = nullptr;
    Require(scePthreadCreate(&holder, nullptr, HoldMutex, nullptr, "timed wait cpu holder") == 0);
    while (!mutexHeld.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    TimeoutsLeaveTheCpu("scePthreadMutexTimedlock", [] { return scePthreadMutexTimedlock(&heldMutex, TIMEOUT_MICROS); });
    releaseMutex.store(true);
    Require(scePthreadJoin(holder, nullptr) == 0);
    Require(scePthreadMutexDestroy(&heldMutex) == 0);
}

int main() {
    CondTimeoutsLeaveTheCpu();
    MutexTimeoutsLeaveTheCpu();
}
