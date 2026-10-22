#include "SceTypes.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
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

namespace {

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

void TestSuspend() {
    Case("sceLncUtilBlockAppSuspend", [] {
        return sceLncUtilBlockAppSuspend("saving") == SYSTEM_SERVICE_OK && sceLncUtilBlockAppSuspend("saving") == SYSTEM_SERVICE_OK;
    });
    Case("sceLncUtilUnblockAppSuspend", [] {
        return sceLncUtilUnblockAppSuspend("saving") == SYSTEM_SERVICE_OK && sceLncUtilUnblockAppSuspend("saving") == SYSTEM_SERVICE_OK &&
            Throws<std::logic_error>([] { return sceLncUtilUnblockAppSuspend("saving"); });
    });
    Case("sceLncUtilBlockAppSuspend null reason", [] {
        return Throws<std::invalid_argument>([] { return sceLncUtilBlockAppSuspend(nullptr); }) &&
            Throws<std::invalid_argument>([] { return sceLncUtilUnblockAppSuspend(nullptr); });
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
    return call(&active) == SYSTEM_SERVICE_OK && !active && call(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER;
}

void TestSessions() {
    Case("sceShellCoreUtilGetRemotePlayStatus", [] { return ReportsInactive(sceShellCoreUtilGetRemotePlayStatus); });
    Case("sceSystemServiceGetRemoteplayStatus", [] { return ReportsInactive(sceSystemServiceGetRemoteplayStatus); });
    Case("sceShellCoreUtilGetSharePlayStatus", [] { return ReportsInactive(sceShellCoreUtilGetSharePlayStatus); });
    Case("sceShellCoreUtilGetGameLiveStreamingStatus", [] { return ReportsInactive(sceShellCoreUtilGetGameLiveStreamingStatus); });
    Case("sceShellCoreUtilIsGameLiveStreamingOnAir", [] { return !sceShellCoreUtilIsGameLiveStreamingOnAir(); });
    Case("sceSystemServiceIsGameLiveStreamingOnAir", [] {
        return !sceSystemServiceIsGameLiveStreamingOnAir() && sceSystemServiceIsGameLiveStreamingOnAir() == sceShellCoreUtilIsGameLiveStreamingOnAir();
    });
}

template<typename TCall>
bool ReportsNotAdjusted(TCall call) {
    const auto user = static_cast<std::uint32_t>(USER_SERVICE_INITIAL_USER_ID);
    std::uint8_t adjusted = 0xff;
    if (call(user, &adjusted) != SYSTEM_SERVICE_OK || adjusted != 0) return false;
    if (call(user, nullptr) != SYSTEM_SERVICE_ERROR_PARAMETER) return false;
    for (const auto other : {0u, user + 1, static_cast<std::uint32_t>(USER_SERVICE_USER_ID_INVALID)}) {
        adjusted = 0xff;
        if (!Throws<std::invalid_argument>([&] { return call(other, &adjusted); }) || adjusted != 0xff) return false;
    }
    return true;
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
    Case("sceSystemServiceIsEyeToEyeDistanceAdjusted", [] { return ReportsNotAdjusted(sceSystemServiceIsEyeToEyeDistanceAdjusted); });
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "suspend") TestSuspend();
    else if (mode == "sessions") TestSessions();
    else if (mode == "shell") TestShell();
    else return 2;
    if (failures == 0) std::printf("all cases passed\n");
    return failures == 0 ? 0 : 1;
}
