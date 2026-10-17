#include "Recompiler.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("SPIR-V residency: " + message);
}

void SetEnvironment(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::vector<std::uint32_t> Module(std::uint32_t seed) {
    std::vector<std::uint32_t> words{0x07230203u, 0x00010300u, 0u, 64u, 0u};
    for (std::uint32_t index = 0; index < 4096; ++index) words.push_back(seed * 0x9e3779b9u + index);
    return words;
}

void ReleasedWordsReloadOnDemand() {
    using ShaderRecompiler::SharedSpirv;
    const auto words = Module(1);
    const auto before = ShaderRecompiler::HeldSpirv();
    int reloads = 0;
    {
        const auto spirv = SharedSpirv::Releasable(SharedSpirv(words), [&] {
            ++reloads;
            return SharedSpirv(words);
        });
        Require(spirv.IsReleasable() && spirv.IsResident(), "a module with a reload source is not releasable and resident");
        const auto hash = spirv.ContentHash();
        auto held = ShaderRecompiler::HeldSpirv();
        Require(held.releasable == before.releasable + 1 && held.resident == before.resident + 1 && held.heapWords == before.heapWords + words.size(), "the counters missed a new releasable module");
        spirv.Release();
        held = ShaderRecompiler::HeldSpirv();
        Require(!spirv.IsResident() && held.resident == before.resident && held.heapWords == before.heapWords && held.releases == before.releases + 1, "a release kept the words or missed the counters");
        Require(spirv.size() == words.size() && spirv[1] == words[1] && spirv.ContentHash() == hash && reloads == 0, "the size, header or hash of a released module needed a reload");
        {
            const auto lease = spirv.Hold();
            Require(reloads == 1 && std::ranges::equal(lease.Words(), words), "a lease did not reload the released words");
            spirv.Release();
            Require(spirv.IsResident(), "a release dropped words a lease still reads");
        }
        held = ShaderRecompiler::HeldSpirv();
        Require(held.reloads == before.reloads + 1 && held.resident == before.resident + 1, "the counters missed a reload");
        spirv.Release();
        Require(!spirv.IsResident(), "a release after the lease ended kept the words");
        Require(spirv.Copy() == words && reloads == 2, "a copy did not reload the released words");
        spirv.Release();
        Require(!spirv.IsResident(), "a copy kept the words resident");
        const auto copy = spirv;
        Require(std::ranges::equal(copy.Words(), words) && reloads == 3, "an unleased read did not reload the released words");
        spirv.Release();
        held = ShaderRecompiler::HeldSpirv();
        Require(spirv.IsResident() && held.pinned == before.pinned + 1, "an unleased read did not pin the module");
    }
    const auto after = ShaderRecompiler::HeldSpirv();
    Require(after.holders == before.holders && after.heapWords == before.heapWords && after.releasable == before.releasable && after.resident == before.resident && after.pinned == before.pinned, "the counters kept a destroyed module");
}

void ChangedWordsAreRefused() {
    using ShaderRecompiler::SharedSpirv;
    const auto words = Module(2);
    auto changed = words;
    changed.back() ^= 1u;
    const auto spirv = SharedSpirv::Releasable(SharedSpirv(words), [&] { return SharedSpirv(changed); });
    spirv.Release();
    bool refused = false;
    try {
        static_cast<void>(spirv.Hold());
    } catch (const std::runtime_error&) {
        refused = true;
    }
    Require(refused && !spirv.IsResident(), "a reload that returned different words was accepted");
    const auto shorter = SharedSpirv::Releasable(SharedSpirv(words), [&] { return SharedSpirv(std::vector<std::uint32_t>(words.begin(), words.end() - 1)); });
    shorter.Release();
    refused = false;
    try {
        static_cast<void>(shorter.Copy());
    } catch (const std::runtime_error&) {
        refused = true;
    }
    Require(refused, "a reload that returned fewer words was accepted");
}

void SwitchOffKeepsWords() {
    using ShaderRecompiler::SharedSpirv;
    const auto words = Module(3);
    int reloads = 0;
    const auto spirv = SharedSpirv::Releasable(SharedSpirv(words), [&] {
        ++reloads;
        return SharedSpirv(words);
    });
    spirv.Release();
    Require(!spirv.IsReleasable() && spirv.IsResident() && std::ranges::equal(spirv.Words(), words) && reloads == 0, "a module was released with APS5_RELEASE_SPIRV unset");
}

}

int main(int argc, char** argv) {
    try {
        const bool off = argc == 2 && std::string(argv[1]) == "--off";
        SetEnvironment("APS5_RELEASE_SPIRV", off ? "0" : "1");
        Require(ShaderRecompiler::SpirvReleaseEnabled() != off, "the switch does not follow APS5_RELEASE_SPIRV");
        if (off) {
            SwitchOffKeepsWords();
        } else {
            ReleasedWordsReloadOnDemand();
            ChangedWordsAreRefused();
        }
        std::puts(off ? "SPIR-V residency: released nothing with the switch off" : "SPIR-V residency: released words reload on demand, leases and unleased reads keep them");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
