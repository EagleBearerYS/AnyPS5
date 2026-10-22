#include "SceTypes.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include <cstdio>
#include <exception>
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

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    if (mode == "suspend") TestSuspend();
    else if (mode == "sessions") TestSessions();
    else return 2;
    if (failures == 0) std::printf("all cases passed\n");
    return failures == 0 ? 0 : 1;
}
