#include "SceTypes.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include "prx/libkernel/KernelErrors.hpp"
#include "prx/libSceUserService/UserService.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

extern "C" int APS5_VABI sceLncUtilBlockAppSuspend(const char* reason);
extern "C" int APS5_VABI sceLncUtilUnblockAppSuspend(const char* reason);
extern "C" int APS5_VABI sceLncUtilEnableSuspendNotification();
extern "C" int APS5_VABI sceLncUtilDisableSuspendNotification();
extern "C" int APS5_VABI sceSystemServiceEnableSuspendNotification();
extern "C" int APS5_VABI sceSystemServiceDisableSuspendNotification();
extern "C" int APS5_VABI sceShellCoreUtilGetRemotePlayStatus(bool* active);
extern "C" int APS5_VABI sceSystemServiceGetRemoteplayStatus(bool* active);
extern "C" int APS5_VABI sceShellCoreUtilGetSharePlayStatus(bool* active);
extern "C" int APS5_VABI sceShellCoreUtilGetGameLiveStreamingStatus(bool* active);
extern "C" bool APS5_VABI sceShellCoreUtilIsGameLiveStreamingOnAir();
extern "C" bool APS5_VABI sceSystemServiceIsGameLiveStreamingOnAir();
extern "C" bool APS5_VABI sceLncUtilIsShellUiFgAndGameBgCpuMode();
extern "C" bool APS5_VABI sceSystemServiceIsShellUiFgAndGameBgCpuMode();
extern "C" bool APS5_VABI sceShellCoreUtilIsImposeScreenOverlaid();
extern "C" bool APS5_VABI sceShellCoreUtilIsScreenSaverOn();
extern "C" bool APS5_VABI sceSystemServiceIsScreenSaverOn();
extern "C" bool APS5_VABI sceSystemStateMgrIsShellUIShutdownInProgress();
extern "C" int APS5_VABI sceShellCoreUtilGetSocialScreenStatus(bool* active);
extern "C" int APS5_VABI sceShellCoreUtilIsEyeToEyeDistanceAdjusted(std::uint32_t userId, std::uint8_t* adjusted);
extern "C" int APS5_VABI sceSystemServiceIsEyeToEyeDistanceAdjusted(std::uint32_t userId, std::uint8_t* adjusted);
extern "C" bool APS5_VABI sceSystemStateMgrIsBdDriveReady();
extern "C" bool APS5_VABI sceShellCoreUtilIsUsbMassStorageMounted(std::uint32_t deviceIndex);
extern "C" bool APS5_VABI sceShellCoreUtilIsExternalStorageAppMoveInProgress();
extern "C" int APS5_VABI sceLncUtilIsPs2Emu(bool* emulated);

namespace {

constexpr auto LOGGED_IN_USER = static_cast<std::uint32_t>(USER_SERVICE_INITIAL_USER_ID);

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

void TestSuspend() {
    Case("sceLncUtilBlockAppSuspend", [] {
        return sceLncUtilBlockAppSuspend("saving") == SYSTEM_SERVICE_OK && sceLncUtilBlockAppSuspend("saving") == SYSTEM_SERVICE_OK;
    });
    Case("sceLncUtilUnblockAppSuspend", [] {
        return sceLncUtilUnblockAppSuspend("saving") == SYSTEM_SERVICE_OK && sceLncUtilUnblockAppSuspend("saving") == SYSTEM_SERVICE_OK;
    });
    Case("sceLncUtilUnblockAppSuspend with no block left", [] {
        return sceLncUtilUnblockAppSuspend("saving") == SCE_KERNEL_ERROR_EBUSY && sceLncUtilUnblockAppSuspend(nullptr) == SCE_KERNEL_ERROR_EBUSY &&
            sceLncUtilBlockAppSuspend("saving") == SYSTEM_SERVICE_OK && sceLncUtilUnblockAppSuspend("saving") == SYSTEM_SERVICE_OK &&
            sceLncUtilUnblockAppSuspend("saving") == SCE_KERNEL_ERROR_EBUSY;
    });
    Case("sceLncUtilEnableSuspendNotification", [] {
        return sceLncUtilEnableSuspendNotification() == SYSTEM_SERVICE_OK && sceLncUtilEnableSuspendNotification() == SYSTEM_SERVICE_OK;
    });
    Case("sceLncUtilDisableSuspendNotification", [] {
        return sceLncUtilDisableSuspendNotification() == SYSTEM_SERVICE_OK && sceLncUtilDisableSuspendNotification() == SYSTEM_SERVICE_OK;
    });
    Case("sceSystemServiceEnableSuspendNotification", [] {
        return sceSystemServiceEnableSuspendNotification() == sceLncUtilEnableSuspendNotification();
    });
    Case("sceSystemServiceDisableSuspendNotification", [] {
        return sceSystemServiceDisableSuspendNotification() == sceLncUtilDisableSuspendNotification();
    });
}

template<typename TCall>
bool ReportsInactive(TCall call) {
    bool active = true;
    return call(&active) == SYSTEM_SERVICE_OK && !active;
}

template<typename TCall>
bool RejectsNullOutput(TCall call, int error) {
    return call(nullptr) == error;
}

void TestSessions() {
    Case("sceShellCoreUtilGetRemotePlayStatus", [] { return ReportsInactive(sceShellCoreUtilGetRemotePlayStatus); });
    Case("sceShellCoreUtilGetRemotePlayStatus null output", [] {
        return RejectsNullOutput(sceShellCoreUtilGetRemotePlayStatus, SHELL_CORE_UTIL_ERROR_NULL_OUTPUT);
    });
    Case("sceSystemServiceGetRemoteplayStatus", [] { return ReportsInactive(sceSystemServiceGetRemoteplayStatus); });
    Case("sceSystemServiceGetRemoteplayStatus null output", [] {
        return RejectsNullOutput(sceSystemServiceGetRemoteplayStatus, SHELL_CORE_UTIL_ERROR_NULL_OUTPUT);
    });
    Case("sceShellCoreUtilGetSharePlayStatus", [] { return ReportsInactive(sceShellCoreUtilGetSharePlayStatus); });
    Case("sceShellCoreUtilGetSharePlayStatus null output", [] {
        return RejectsNullOutput(sceShellCoreUtilGetSharePlayStatus, SHELL_CORE_UTIL_ERROR_NULL_OUTPUT);
    });
    Case("sceShellCoreUtilGetGameLiveStreamingStatus", [] { return ReportsInactive(sceShellCoreUtilGetGameLiveStreamingStatus); });
    Case("sceShellCoreUtilGetGameLiveStreamingStatus null output", [] {
        return RejectsNullOutput(sceShellCoreUtilGetGameLiveStreamingStatus, SHELL_CORE_UTIL_ERROR_NULL_OUTPUT);
    });
    Case("sceShellCoreUtilIsGameLiveStreamingOnAir", [] { return !sceShellCoreUtilIsGameLiveStreamingOnAir(); });
    Case("sceSystemServiceIsGameLiveStreamingOnAir", [] {
        return !sceSystemServiceIsGameLiveStreamingOnAir() && sceSystemServiceIsGameLiveStreamingOnAir() == sceShellCoreUtilIsGameLiveStreamingOnAir();
    });
}

template<typename TCall>
bool ReportsNotAdjusted(TCall call) {
    const auto user = LOGGED_IN_USER;
    std::uint8_t adjusted = 0xff;
    return call(user, &adjusted) == SYSTEM_SERVICE_OK && adjusted == 0;
}

void TestShell() {
    Case("sceLncUtilIsShellUiFgAndGameBgCpuMode", [] { return !sceLncUtilIsShellUiFgAndGameBgCpuMode(); });
    Case("sceSystemServiceIsShellUiFgAndGameBgCpuMode", [] {
        return !sceSystemServiceIsShellUiFgAndGameBgCpuMode() && sceSystemServiceIsShellUiFgAndGameBgCpuMode() == sceLncUtilIsShellUiFgAndGameBgCpuMode();
    });
    Case("sceShellCoreUtilIsImposeScreenOverlaid", [] { return !sceShellCoreUtilIsImposeScreenOverlaid(); });
    Case("sceShellCoreUtilIsScreenSaverOn", [] { return !sceShellCoreUtilIsScreenSaverOn(); });
    Case("sceSystemServiceIsScreenSaverOn", [] {
        return !sceSystemServiceIsScreenSaverOn() && sceSystemServiceIsScreenSaverOn() == sceShellCoreUtilIsScreenSaverOn();
    });
    Case("sceSystemStateMgrIsShellUIShutdownInProgress", [] { return !sceSystemStateMgrIsShellUIShutdownInProgress(); });
    Case("sceShellCoreUtilGetSocialScreenStatus", [] { return ReportsInactive(sceShellCoreUtilGetSocialScreenStatus); });
    Case("sceShellCoreUtilIsEyeToEyeDistanceAdjusted", [] { return ReportsNotAdjusted(sceShellCoreUtilIsEyeToEyeDistanceAdjusted); });
    Case("sceSystemServiceIsEyeToEyeDistanceAdjusted", [] {
        return ReportsNotAdjusted(sceSystemServiceIsEyeToEyeDistanceAdjusted) &&
            sceSystemServiceIsEyeToEyeDistanceAdjusted(LOGGED_IN_USER, nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER;
    });
}

void TestDevices() {
    Case("sceSystemStateMgrIsBdDriveReady", [] { return !sceSystemStateMgrIsBdDriveReady(); });
    Case("sceShellCoreUtilIsUsbMassStorageMounted", [] {
        for (std::uint32_t device = 0; device < 8; ++device) {
            if (sceShellCoreUtilIsUsbMassStorageMounted(device)) return false;
        }
        return true;
    });
    Case("sceShellCoreUtilIsExternalStorageAppMoveInProgress", [] { return !sceShellCoreUtilIsExternalStorageAppMoveInProgress(); });
    Case("sceLncUtilIsPs2Emu", [] { return ReportsInactive(sceLncUtilIsPs2Emu); });
    Case("sceLncUtilIsPs2Emu null output", [] { return RejectsNullOutput(sceLncUtilIsPs2Emu, LNC_UTIL_ERROR_NULL_OUTPUT); });
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "suspend") TestSuspend();
    else if (mode == "sessions") TestSessions();
    else if (mode == "shell") TestShell();
    else if (mode == "devices") TestDevices();
    else return 2;
    if (failures == 0) std::printf("all cases passed\n");
    return failures == 0 ? 0 : 1;
}
