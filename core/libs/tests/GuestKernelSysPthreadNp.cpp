#include "SceTypes.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>

struct GuestBarrier;
struct GuestBarrierattr;

extern "C" {
int APS5_VABI pthread_mutex_init_nid_postfix(PthreadMutex* mutex, const PthreadMutexattr* attr);
int APS5_VABI pthread_mutex_lock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_unlock_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_destroy_nid_postfix(PthreadMutex* mutex);
int APS5_VABI pthread_mutex_setname_np_nid_postfix(PthreadMutex* mutex, const char* name);
int APS5_VABI pthread_cond_init_nid_postfix(PthreadCond* cond, const PthreadCondattr* attr);
int APS5_VABI pthread_cond_signal_nid_postfix(PthreadCond* cond);
int APS5_VABI pthread_cond_destroy_nid_postfix(PthreadCond* cond);
int APS5_VABI pthread_cond_setname_np_nid_postfix(PthreadCond* cond, const char* name);
int APS5_VABI pthread_rwlock_init_nid_postfix(PthreadRwlock* rwlock, const PthreadRwlockattr* attr);
int APS5_VABI pthread_rwlock_wrlock_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_unlock_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_destroy_nid_postfix(PthreadRwlock* rwlock);
int APS5_VABI pthread_rwlock_setname_np_nid_postfix(PthreadRwlock* rwlock, const char* name);
int APS5_VABI pthread_rwlockattr_init_nid_postfix(PthreadRwlockattr* attr);
int APS5_VABI pthread_rwlockattr_destroy_nid_postfix(PthreadRwlockattr* attr);
int APS5_VABI pthread_rwlockattr_settype_np_nid_postfix(PthreadRwlockattr* attr, int type);
int APS5_VABI scePthreadRwlockattrSettype(PthreadRwlockattr* attr, int type);
int APS5_VABI pthread_barrier_init_nid_postfix(GuestBarrier** barrier, GuestBarrierattr* const* attr, unsigned count);
int APS5_VABI pthread_barrier_wait_nid_postfix(GuestBarrier** barrier);
int APS5_VABI pthread_barrier_destroy_nid_postfix(GuestBarrier** barrier);
int APS5_VABI pthread_barrier_setname_np_nid_postfix(GuestBarrier** barrier, const char* name);
int APS5_VABI sem_init_nid_postfix(void* sem, int pshared, unsigned int value);
int APS5_VABI sem_trywait_nid_postfix(void* sem);
int APS5_VABI sem_destroy_nid_postfix(void* sem);
int APS5_VABI sem_setname_nid_postfix(void* sem, const char* name);
}

namespace {

constexpr int BARRIER_SERIAL_THREAD = -1;

int failures = 0;

template<typename TCall>
void Case(const char* name, TCall call) {
    try {
        if (call()) return;
        std::printf("%s: wrong answer\n", name);
    } catch (const std::exception& error) {
        std::printf("%s: %s\n", name, error.what());
    }
    ++failures;
}

template<typename TException, typename TCall>
bool Throws(TCall call) {
    try {
        static_cast<void>(call());
    } catch (const TException&) {
        return true;
    }
    return false;
}

template<typename TObject, typename TSetname>
bool AcceptsNames(TObject* object, TSetname setname) {
    const std::string overlong(300, 'n');
    return setname(object, "worker") == 0 && setname(object, "") == 0 && setname(object, overlong.c_str()) == 0 &&
        Throws<std::invalid_argument>([&] { return setname(object, nullptr); }) &&
        Throws<std::invalid_argument>([&] { return setname(nullptr, "worker"); });
}

void TestNames() {
    Case("pthread_mutex_setname_np", [] {
        PthreadMutex mutex = nullptr;
        if (pthread_mutex_init_nid_postfix(&mutex, nullptr) != 0) return false;
        const bool named = AcceptsNames(&mutex, pthread_mutex_setname_np_nid_postfix);
        const bool usable = pthread_mutex_lock_nid_postfix(&mutex) == 0 && pthread_mutex_unlock_nid_postfix(&mutex) == 0;
        return named && usable && pthread_mutex_destroy_nid_postfix(&mutex) == 0;
    });
    Case("pthread_cond_setname_np", [] {
        PthreadCond cond = nullptr;
        if (pthread_cond_init_nid_postfix(&cond, nullptr) != 0) return false;
        const bool named = AcceptsNames(&cond, pthread_cond_setname_np_nid_postfix);
        return named && pthread_cond_signal_nid_postfix(&cond) == 0 && pthread_cond_destroy_nid_postfix(&cond) == 0;
    });
    Case("pthread_rwlock_setname_np", [] {
        PthreadRwlock rwlock = nullptr;
        if (pthread_rwlock_init_nid_postfix(&rwlock, nullptr) != 0) return false;
        const bool named = AcceptsNames(&rwlock, pthread_rwlock_setname_np_nid_postfix);
        const bool usable = pthread_rwlock_wrlock_nid_postfix(&rwlock) == 0 && pthread_rwlock_unlock_nid_postfix(&rwlock) == 0;
        return named && usable && pthread_rwlock_destroy_nid_postfix(&rwlock) == 0;
    });
    Case("pthread_barrier_setname_np", [] {
        GuestBarrier* barrier = nullptr;
        if (pthread_barrier_init_nid_postfix(&barrier, nullptr, 1) != 0) return false;
        const bool named = AcceptsNames(&barrier, pthread_barrier_setname_np_nid_postfix);
        return named && pthread_barrier_wait_nid_postfix(&barrier) == BARRIER_SERIAL_THREAD && pthread_barrier_destroy_nid_postfix(&barrier) == 0;
    });
    Case("sem_setname", [] {
        std::uint64_t sem = 0;
        if (sem_init_nid_postfix(&sem, 0, 1) != 0) return false;
        const bool named = AcceptsNames(&sem, sem_setname_nid_postfix);
        return named && sem_trywait_nid_postfix(&sem) == 0 && sem_destroy_nid_postfix(&sem) == 0;
    });
}

void TestRwlockattr() {
    Case("pthread_rwlockattr_settype_np", [] {
        PthreadRwlockattr attr = nullptr;
        if (pthread_rwlockattr_init_nid_postfix(&attr) != 0) return false;
        for (const int type : {0, 1, 2}) {
            if (pthread_rwlockattr_settype_np_nid_postfix(&attr, type) != 0 || scePthreadRwlockattrSettype(&attr, type) != 0) return false;
            PthreadRwlock rwlock = nullptr;
            if (pthread_rwlock_init_nid_postfix(&rwlock, &attr) != 0) return false;
            const bool usable = pthread_rwlock_wrlock_nid_postfix(&rwlock) == 0 && pthread_rwlock_unlock_nid_postfix(&rwlock) == 0;
            if (!usable || pthread_rwlock_destroy_nid_postfix(&rwlock) != 0) return false;
        }
        if (pthread_rwlockattr_destroy_nid_postfix(&attr) != 0) return false;
        return Throws<std::runtime_error>([&] { return pthread_rwlockattr_settype_np_nid_postfix(&attr, 1); }) &&
            Throws<std::runtime_error>([] { return pthread_rwlockattr_settype_np_nid_postfix(nullptr, 1); });
    });
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "names") TestNames();
    else if (mode == "rwlockattr") TestRwlockattr();
    else return 2;
    if (failures == 0) std::printf("all cases passed\n");
    return failures == 0 ? 0 : 1;
}
