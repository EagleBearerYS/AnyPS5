#include "SceTypes.hpp"
#include <cstdint>
#include <cstdlib>

extern "C" {
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI sceKernelCreateEventFlag(KernelEventFlag* ef, const char* name, uint32_t attr, uint64_t init_pattern, const void* param);
int APS5_VABI sceKernelDeleteEventFlag(KernelEventFlag ef);
int APS5_VABI sceKernelSetEventFlag(KernelEventFlag ef, uint64_t bit_pattern);
int APS5_VABI sceKernelClearEventFlag(KernelEventFlag ef, uint64_t bit_pattern);
int APS5_VABI sceKernelPollEventFlag(KernelEventFlag ef, uint64_t bit_pattern, uint32_t wait_mode, uint64_t* result_pat);
int APS5_VABI sceKernelWaitEventFlag(KernelEventFlag ef, uint64_t bit_pattern, uint32_t wait_mode, uint64_t* result_pat, KernelUseconds* timeout);
}

static constexpr int SCE_OK = 0;
static constexpr int SCE_KERNEL_ERROR_EPERM = static_cast<int>(0x80020001);
static constexpr uint32_t EVF_ATTR_SINGLE = 0x10;
static constexpr uint32_t EVF_WAITMODE_OR = 0x02;

static void Require(bool value) { if (!value) std::abort(); }

struct Waiter {
    KernelEventFlag ef = nullptr;
    uint32_t mode = EVF_WAITMODE_OR;
    int result = -1;
    uint64_t pattern = 0;
};

static void* APS5_VABI Wait(void* arg) {
    auto& waiter = *static_cast<Waiter*>(arg);
    KernelUseconds timeout = 1000000;
    waiter.result = sceKernelWaitEventFlag(waiter.ef, 0x1, waiter.mode, &waiter.pattern, &timeout);
    return nullptr;
}

static void PulseReleasesWaiter() {
    KernelEventFlag ef = nullptr;
    Require(sceKernelCreateEventFlag(&ef, "pulse", EVF_ATTR_SINGLE, 0, nullptr) == SCE_OK);
    Waiter waiter{ef, EVF_WAITMODE_OR};
    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, nullptr, Wait, &waiter, nullptr) == SCE_OK);
    while (sceKernelPollEventFlag(ef, 0x1, EVF_WAITMODE_OR, nullptr) != SCE_KERNEL_ERROR_EPERM) {
    }
    Require(sceKernelSetEventFlag(ef, 0x1) == SCE_OK);
    Require(sceKernelClearEventFlag(ef, 0) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);
    Require(waiter.result == SCE_OK);
    Require(waiter.pattern == 0x1);
    Require(sceKernelDeleteEventFlag(ef) == SCE_OK);
}

int main() {
    for (int i = 0; i < 100; ++i) {
        PulseReleasesWaiter();
    }
    return 0;
}
