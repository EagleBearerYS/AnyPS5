#include "prx/libSceAgcDriver/include/AgcDriver.hpp"
#include "SceTypes.hpp"

#include <cstdio>

int main() {
    if (sceAgcDriverGetCoredumpAddress() != nullptr) return 1;
    if (sceAgcDriverSetTargetRingForDiag() != static_cast<int>(0x8A6D0001)) return 2;
    auto* volatile linked = &sceAgcDriverSetFlip;
    if (linked == nullptr) return 3;
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
        if (entry.function == nullptr) {
            std::fprintf(stderr, "%s is not exported\n", entry.name);
            return 5;
        }
    }
    return 0;
}
