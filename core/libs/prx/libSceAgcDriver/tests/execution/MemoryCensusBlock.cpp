#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using AgcDriver::Graphics::Require;
using namespace ShaderRecompiler;

struct alignas(4096) GuestData {
    std::array<std::array<std::uint32_t, 8>, 4> heap{};
    std::array<std::uint32_t, 12> material{};
    std::array<std::uint32_t, 16> srt{};
    std::array<std::uint32_t, 64> output{};
};

alignas(256) std::array<std::array<std::uint32_t, 64>, 2> textures{};
std::array<GuestData, 8> guest;
constexpr std::array<std::uint32_t, 23> code{0xbf068002u, 0xbf850013u, 0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x93109010u, 0xf4200406u, 0x20000004u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u, 0xbf800000u};

std::array<std::uint32_t, 4> Buffer(const void* pointer, std::uint32_t stride, std::uint32_t records) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u) | (stride << 16u), records, stride == 0u ? 0x31016facu : 0x01016facu};
}

void Dispatches(AgcDriver::VulkanDevice& device) {
    textures[0].fill(0x40000000u);
    textures[1].fill(0x40400000u);
    const auto threads = std::min(32u, device.Target().subgroupSize);
    for (std::uint32_t iteration = 0u; iteration < guest.size(); ++iteration) {
        auto& data = guest[iteration];
        const auto table = Buffer(data.heap.data(), 32u, 4u);
        const auto material = Buffer(data.material.data(), 16u, 3u);
        std::copy(table.begin(), table.end(), data.srt.begin());
        std::copy(material.begin(), material.end(), data.srt.begin() + 8u);
        const auto srtAddress = reinterpret_cast<std::uintptr_t>(data.srt.data());
        const std::array<std::uint32_t, 3> userData{static_cast<std::uint32_t>(srtAddress), static_cast<std::uint32_t>(srtAddress >> 32u), 1u};
        const std::array regions{MemoryRegion{reinterpret_cast<std::uintptr_t>(&data), std::as_bytes(std::span(&data, 1u))}};
        for (std::uint32_t texture = 0u; texture < textures.size(); ++texture) {
            const auto address = reinterpret_cast<std::uintptr_t>(textures[texture].data());
            data.heap[texture] = {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u) | ((texture == 0u ? 20u : 22u) << 20u) | (3u << 30u), 7u, 0x90000facu, 0u, 0u, 0u, 0u};
        }
        data.material[1] = iteration % 2u;
        data.material[5] = 0u;
        data.material[9] = 1u;
        data.output.fill(0xdeadbeefu);
        const auto output = Buffer(data.output.data(), 0u, 256u);
        std::copy(output.begin(), output.end(), data.srt.begin() + 12u);
        const ShaderComputeStageInfo compute{{threads, 1u, 1u}, 0u, {false, false, false}, false, 1u};
        RecompileRequest request{{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0u, {}}, {32u, 0u, userData, compute, std::nullopt, std::nullopt, regions}, device.Target(), {0u, 0u, 0u, 128u}};
        device.Dispatch(Recompile(request), 1u, 1u, 1u);
        device.SubmitRecorded(false);
    }
    device.WaitIdle();
    for (std::uint32_t iteration = 0u; iteration < guest.size(); ++iteration) {
        const auto expected = textures[iteration % 2u][0];
        Require(guest[iteration].output[0] == expected, "dispatch " + std::to_string(iteration) + " read " + std::to_string(guest[iteration].output[0]) + ", expected " + std::to_string(expected));
    }
}

std::string_view Line(std::string_view block, std::string_view prefix) {
    for (std::size_t at = 0; at < block.size();) {
        const auto end = std::min(block.find('\n', at), block.size());
        const auto line = block.substr(at, end - at);
        if (line.substr(0, prefix.size()) == prefix) return line;
        at = end + 1;
    }
    throw std::runtime_error("the census block has no line starting " + std::string(prefix) + ":\n" + std::string(block));
}

std::uint64_t After(std::string_view line, std::string_view label) {
    const auto at = line.find(label);
    Require(at != std::string_view::npos, "census line lacks \"" + std::string(label) + "\": " + std::string(line));
    std::uint64_t value = 0;
    bool digits = false;
    for (auto index = at + label.size(); index < line.size() && line[index] >= '0' && line[index] <= '9'; ++index) {
        value = value * 10u + static_cast<std::uint64_t>(line[index] - '0');
        digits = true;
    }
    Require(digits, "no number after \"" + std::string(label) + "\": " + std::string(line));
    return value;
}

void CheckPools(std::string_view block) {
    const auto line = Line(block, "[mem-census] descriptor pools ");
    const auto chain = After(line, "): chain ");
    const auto pools = chain + After(line, "; dedicated ") + After(line, "; other ");
    Require(chain >= 1, "the dispatches opened no chain pool: " + std::string(line));
    Require(pools >= chain, "the pool kinds do not add up: " + std::string(line));
    Require(After(line, "; probe: ") >= 1, "no create-info was probed: " + std::string(line));
    Require(After(line, "sets allocated, ") == 0, "a probe could not create its pool or allocate a live pool's set: " + std::string(line));
    const auto probes = Line(block, "[mem-census] descriptor pool probes ");
    Require(probes.find("chain maxSets ") != std::string_view::npos && probes.find(" est ") != std::string_view::npos, "the chain create-info was not probed: " + std::string(probes));
}

void CheckOwners(std::string_view block) {
    const auto line = Line(block, "[mem-census] driver: ");
    After(line, "; never erased: draw relocations ");
    After(line, "relocation trials ");
    After(line, "draw aliases ");
    After(line, "resource cache ");
    Require(After(line, "; recompiler sources ") >= 1, "the compiled shader has no source key: " + std::string(line));
    const auto sources = ShaderRecompiler::SourceFootprint();
    Require(sources.keys >= 1 && sources.sources >= 1 && sources.plans >= 1 && sources.variants >= 1, "the source table footprint misses the compiled shader");
    Require(sources.codeBytes >= sizeof(code) && sources.planBytes > 0 && sources.tableBytes > 0, "the source table footprint counts no code, plan or table bytes");
    Require(sources.spirvHeapBytes + sources.spirvViewBytes > 0, "the source table footprint counts no SPIR-V");
    if (std::getenv("ANYPS5_NO_SHADER_CACHE") != nullptr) Require(sources.spirvViewBytes == 0 && sources.spirvHeapBytes > 0, "SPIR-V compiled with the shader cache off is not on the heap");
    const auto resources = AgcDriver::Graphics::SharedResourceCache().Measure();
    Require(resources.objectBytes >= resources.entries * sizeof(AgcDriver::Graphics::ShaderResources), "a cached resource object is smaller than its struct");
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Dispatches(*device);
        const auto block = AgcDriver::MemoryCensus::Block();
        if (!AgcDriver::MemoryCensus::Enabled()) {
            Require(block.empty(), "the census printed a block with APS5_MEMORY_CENSUS unset");
            std::cout << "memory census block tests passed (census off)\n";
            return 0;
        }
        std::cout << block;
        CheckPools(block);
        CheckOwners(block);
        Dispatches(*device);
        std::cout << "memory census block tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
