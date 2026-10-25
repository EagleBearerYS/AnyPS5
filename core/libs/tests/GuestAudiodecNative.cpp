#include "prx/libSceAudiodec.native/include/Api.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
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
    struct Export {
        int (APS5_VABI* function)(void);
        const char* name;
    };
    const Export exports[] = {
        {sceAudiodecClearContextEx, "sceAudiodecClearContextEx"},
        {sceAudiodecCreateDecoderEx, "sceAudiodecCreateDecoderEx"},
        {sceAudiodecDecode2, "sceAudiodecDecode2"},
        {sceAudiodecDecode2Ex, "sceAudiodecDecode2Ex"},
        {sceAudiodecDecode2WithPriority, "sceAudiodecDecode2WithPriority"},
        {sceAudiodecDecode2WithPriorityEx, "sceAudiodecDecode2WithPriorityEx"},
        {sceAudiodecDecodeEx, "sceAudiodecDecodeEx"},
        {sceAudiodecDecodeWithPriority, "sceAudiodecDecodeWithPriority"},
        {sceAudiodecDecodeWithPriorityEx, "sceAudiodecDecodeWithPriorityEx"},
        {sceAudiodecDeleteDecoderEx, "sceAudiodecDeleteDecoderEx"},
        {sceAudiodecGetContext, "sceAudiodecGetContext"},
        {sceAudiodecInitialize, "sceAudiodecInitialize"},
        {sceAudiodecRegisterCodec, "sceAudiodecRegisterCodec"},
        {sceAudiodecTerminate, "sceAudiodecTerminate"},
    };
    for (const auto& entry : exports) {
        if (!ThrowsNotImplemented(entry.function, entry.name)) {
            std::fprintf(stderr, "%s did not throw its not-implemented error\n", entry.name);
            return 1;
        }
    }
    std::puts("libSceAudiodec.native pending exports throw");
    return 0;
}
