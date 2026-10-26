#include "prx/libSceAudiodec.native/include/Api.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdio>

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
        if (entry.function == nullptr) {
            std::fprintf(stderr, "%s is not exported\n", entry.name);
            return 1;
        }
    }
    std::puts("libSceAudiodec.native pending exports are exported");
    return 0;
}
