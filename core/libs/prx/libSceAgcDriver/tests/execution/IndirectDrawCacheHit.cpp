#include "IndirectDrawCacheFixture.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

using namespace IndirectDrawCacheFixture;

void RepeatedRecordsHitTheDrawCache() {
    const auto first = Reference();
    const auto second = Submit(PushedOne, {{3, 1, 0, 0}});
    const auto third = Submit(PushedOne, {{3, 1, 0, 0}});
    const auto inserts = third.after.inserts - first.before.inserts;
    const auto hits = third.after.hits - first.before.hits;
    Require(inserts == 1 && hits == 1 && third.Hits() == 1, "three CPU-path indirect draws of the same records and registers inserted " + std::to_string(inserts) + " draw entries and made " + std::to_string(hits) + " draw-cache hits (" + std::to_string(third.Hits()) + " by the third), expected the second to insert and the third to hit");
    RequireSamePixels(second, first, "the second draw of the same records");
    RequireSamePixels(third, first, "the third draw of the same records, a draw-cache hit");
}

}

int main() {
    try {
        if (std::getenv("APS5_CACHE_CPU_INDIRECT") == nullptr) {
#ifdef _WIN32
            _putenv_s("APS5_CACHE_CPU_INDIRECT", "1");
#else
            setenv("APS5_CACHE_CPU_INDIRECT", "1", 1);
#endif
        }
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        RegisterShaders();
        RepeatedRecordsHitTheDrawCache();
        AgcDriverShutdown_nid_postfix();
        std::puts("a CPU-path indirect draw repeated with the same records hit the draw cache and drew the same pixels");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
