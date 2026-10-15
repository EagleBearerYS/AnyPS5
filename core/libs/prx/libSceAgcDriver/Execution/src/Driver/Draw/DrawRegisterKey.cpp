#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawRegisterKey.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

namespace AgcDriver::DriverDetail {

namespace {

bool runtimeRegister(std::uint32_t offset) {
    return offset - 0x00cu < 32u || offset - 0x08cu < 32u || offset - 0x10cu < 32u || offset == 0x082u || offset == 0x083u || offset == 0x102u || offset == 0x103u;
}

const auto& registerMasks() {
    static const auto masks = [] {
        std::array<std::vector<std::uint64_t>, 3> bits;
        for (const auto& range : Graphics::DrawKeyRegisters) {
            auto& bank = bits[static_cast<std::size_t>(range.bank)];
            bank.resize(std::max(bank.size(), (static_cast<std::size_t>(range.first) + range.count + 63) / 64));
            for (auto offset = range.first; offset < range.first + range.count; ++offset) {
                if (range.bank == Graphics::RegisterBank::Shader && runtimeRegister(offset)) continue;
                bank[offset / 64] |= std::uint64_t{1} << (offset % 64);
            }
        }
        std::array<std::shared_ptr<const Registers::Mask>, 3> result;
        for (std::size_t i = 0; i < bits.size(); ++i) result[i] = std::make_shared<const Registers::Mask>(std::move(bits[i]));
        return result;
    }();
    return masks;
}

}

std::uint64_t RegisterShapeKey(const QueueState& queue, bool incremental) {
    const auto& masks = registerMasks();
    const auto fingerprint = [&](const Registers& registers, Graphics::RegisterBank bank) {
        const auto& mask = masks[static_cast<std::size_t>(bank)];
        return incremental ? registers.Fingerprint(mask) : registers.RecomputeFingerprint(*mask);
    };
    auto shape = (0xcbf29ce484222325ull ^ fingerprint(queue.context, Graphics::RegisterBank::Context)) * 0x100000001b3ull;
    shape = (shape ^ fingerprint(queue.shader, Graphics::RegisterBank::Shader)) * 0x100000001b3ull;
    return (shape ^ fingerprint(queue.userConfig, Graphics::RegisterBank::UserConfig)) * 0x100000001b3ull;
}

std::uint64_t RegisterStateKey(const QueueState& queue, bool allUserWords, bool incremental) {
    auto exact = RegisterShapeKey(queue, incremental);
    const auto words = [&](std::uint32_t base, std::uint32_t count) {
        for (auto it = queue.shader.lower_bound(base); it != queue.shader.end() && it->first < base + count; ++it) {
            exact = (exact ^ it->first) * 0x100000001b3ull;
            exact = (exact ^ it->second) * 0x100000001b3ull;
        }
    };
    for (const auto base : {0x00cu, 0x08cu, 0x10cu}) {
        const auto resources = queue.shader.find(base - 1);
        const auto count = resources == queue.shader.end() ? 0u : ((resources->second >> 1u) & 0x1fu) | (((resources->second >> 27u) & 1u) << 5u);
        words(base, allUserWords ? 32u : std::min(count, 32u));
    }
    words(0x082u, 2);
    words(0x102u, 2);
    return exact;
}

}
