#include "prx/libSceAvPlayer.native/include/Api.hpp"
#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstdio>

int main() {
    struct Export {
        int (APS5_VABI* function)(void);
        const char* name;
    };
    const Export exports[] = {
        {sceAvPlayerPrintf, "sceAvPlayerPrintf"},
        {sceAvPlayerVprintf, "sceAvPlayerVprintf"},
    };
    for (const auto& entry : exports) {
        if (entry.function == nullptr) {
            std::fprintf(stderr, "%s is not exported\n", entry.name);
            return 1;
        }
    }
    std::puts("libSceAvPlayer.native pending exports are exported");
    return 0;
}
