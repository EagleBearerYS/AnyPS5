#include "prx/libSceAgcDriver/include/AgcDriver.hpp"
#include "SceTypes.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

static bool ThrowsNotImplemented(int (APS5_VABI* function)(void), const char* name) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        return std::string(error.what()) == std::string(name) + " not implemented";
    }
    return false;
}

int main() {
    if (sceAgcDriverGetCoredumpAddress() != nullptr) return 1;
    if (sceAgcDriverSetTargetRingForDiag() != static_cast<int>(0x8A6D0001)) return 2;
    struct Export {
        int (APS5_VABI* function)(void);
        const char* name;
    };
    const Export exports[] = {
        {sceAgcDriverAcquireRazorACQ, "sceAgcDriverAcquireRazorACQ"},
        {sceAgcDriverCreateQueue, "sceAgcDriverCreateQueue"},
        {sceAgcDriverCwsrResumeAcq, "sceAgcDriverCwsrResumeAcq"},
        {sceAgcDriverCwsrSuspendAcq, "sceAgcDriverCwsrSuspendAcq"},
        {sceAgcDriverDebugHardwareStatus, "sceAgcDriverDebugHardwareStatus"},
        {sceAgcDriverDestroyQueue, "sceAgcDriverDestroyQueue"},
        {sceAgcDriverGetPaDebugInterfaceVersion, "sceAgcDriverGetPaDebugInterfaceVersion"},
        {sceAgcDriverGetRegShadowInfo, "sceAgcDriverGetRegShadowInfo"},
        {sceAgcDriverGetRegShadowInfoAgr, "sceAgcDriverGetRegShadowInfoAgr"},
        {sceAgcDriverGetReservedDmemForAgc, "sceAgcDriverGetReservedDmemForAgc"},
        {sceAgcDriverGetWorkloadStreamInfo, "sceAgcDriverGetWorkloadStreamInfo"},
        {sceAgcDriverIDHSSubmit, "sceAgcDriverIDHSSubmit"},
        {sceAgcDriverModuleRegistration, "sceAgcDriverModuleRegistration"},
        {sceAgcDriverNotifyDefaultStates, "sceAgcDriverNotifyDefaultStates"},
        {sceAgcDriverPassInfoDownward, "sceAgcDriverPassInfoDownward"},
        {sceAgcDriverPatchClearState, "sceAgcDriverPatchClearState"},
        {sceAgcDriverSetFlip, "sceAgcDriverSetFlip"},
        {sceAgcDriverSetHsOffchipParamDirect, "sceAgcDriverSetHsOffchipParamDirect"},
        {sceAgcDriverSetWorkloadComplete, "sceAgcDriverSetWorkloadComplete"},
        {sceAgcDriverSetWorkloadsActive, "sceAgcDriverSetWorkloadsActive"},
        {sceAgcDriverSetupAsyncGraphics, "sceAgcDriverSetupAsyncGraphics"},
        {sceAgcDriverSetupRegisterShadow, "sceAgcDriverSetupRegisterShadow"},
        {sceAgcDriverSubmitMultiCommandBuffersDirect, "sceAgcDriverSubmitMultiCommandBuffersDirect"},
        {sceAgcDriverSubmitToHDRScopesACQ, "sceAgcDriverSubmitToHDRScopesACQ"},
        {sceAgcDriverSubmitToRazorACQ, "sceAgcDriverSubmitToRazorACQ"},
        {sceAgcDriverSuspendPointSubmit, "sceAgcDriverSuspendPointSubmit"},
        {sceAgcDriverSuspendPointSubmitDirect, "sceAgcDriverSuspendPointSubmitDirect"},
        {sceAgcDriverSysEnableSubmitDone45Exception, "sceAgcDriverSysEnableSubmitDone45Exception"},
        {sceAgcDriverSysGetClientNumber, "sceAgcDriverSysGetClientNumber"},
        {sceAgcDriverSysIsGameClosed, "sceAgcDriverSysIsGameClosed"},
        {sceAgcDriverTmpInitIdhs, "sceAgcDriverTmpInitIdhs"},
        {sceAgcDriverUserDataWritePacket, "sceAgcDriverUserDataWritePacket"},
        {sceAgcDriverUserDataWritePopMarker, "sceAgcDriverUserDataWritePopMarker"},
        {sceAgcDriverUserDataWritePushMarker, "sceAgcDriverUserDataWritePushMarker"},
        {sceAgcDriverUserDataWriteSetMarker, "sceAgcDriverUserDataWriteSetMarker"},
    };
    for (const auto& entry : exports) {
        if (!ThrowsNotImplemented(entry.function, entry.name)) {
            std::fprintf(stderr, "%s did not throw its not-implemented error\n", entry.name);
            return 3;
        }
    }
    return 0;
}
