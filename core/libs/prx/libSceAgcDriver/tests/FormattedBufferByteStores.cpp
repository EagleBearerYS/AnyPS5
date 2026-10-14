#include "BdaAbi.hpp"
#include "Recompiler.hpp"
#include <array>
#include <cstdio>
#include <exception>
#include <span>
#include <spirv/unified1/spirv.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ShaderRecompiler;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

SpirvTarget Target(bool byteAccess, bool ballot = false) {
    static constexpr std::array<std::uint32_t, 3> withBytes{
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess,
    };
    static constexpr std::array<std::uint32_t, 2> withoutBytes{spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses};
    static constexpr std::array<std::uint32_t, 5> withBallot{
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess, spv::CapabilityGroupNonUniform, spv::CapabilityGroupNonUniformBallot,
    };
    static constexpr std::array<std::string_view, 4> extensions{
        "SPV_EXT_descriptor_indexing", "SPV_KHR_8bit_storage", "SPV_KHR_float_controls", "SPV_KHR_physical_storage_buffer",
    };
    const auto capabilities = ballot ? std::span<const std::uint32_t>(withBallot) : byteAccess ? std::span<const std::uint32_t>(withBytes) : std::span<const std::uint32_t>(withoutBytes);
    return {0x00401000u, 0x00010300u, 32, BdaAbi::Version, capabilities, extensions, false,
            {1024, 1024, 64}, 1024, 32768, {}, {}};
}

struct Counts {
    std::size_t compareExchanges = 0;
    std::size_t byteStores = 0;
    std::size_t byteTypes = 0;
    std::size_t elections = 0;
    std::size_t broadcasts = 0;
    bool byteCapability = false;
    bool byteBuffers = false;
};

Counts Count(std::span<const std::uint32_t> words) {
    Check(words.size() >= 5 && words[0] == spv::MagicNumber, "formatted buffer byte stores: missing SPIR-V module");
    Counts counts;
    std::uint32_t byteType = 0;
    std::vector<std::uint32_t> bytePointers;
    for (std::size_t offset = 5; offset < words.size();) {
        const auto count = words[offset] >> spv::WordCountShift;
        Check(count != 0 && count <= words.size() - offset, "formatted buffer byte stores: malformed SPIR-V instruction");
        const auto opcode = words[offset] & spv::OpCodeMask;
        if (opcode == spv::OpAtomicCompareExchange) ++counts.compareExchanges;
        if (opcode == spv::OpGroupNonUniformElect) ++counts.elections;
        if (opcode == spv::OpGroupNonUniformBroadcastFirst) ++counts.broadcasts;
        if (opcode == spv::OpCapability && words[offset + 1] == spv::CapabilityStorageBuffer8BitAccess) counts.byteCapability = true;
        if (opcode == spv::OpTypeInt && words[offset + 2] == 8u) {
            ++counts.byteTypes;
            byteType = words[offset + 1];
        }
        if (opcode == spv::OpTypePointer && words[offset + 2] == spv::StorageClassPhysicalStorageBuffer && words[offset + 3] == byteType && byteType != 0) {
            bytePointers.push_back(words[offset + 1]);
            counts.byteBuffers = true;
        }
        offset += count;
    }
    std::vector<std::uint32_t> byteChains;
    for (std::size_t offset = 5; offset < words.size();) {
        const auto count = words[offset] >> spv::WordCountShift;
        const auto opcode = words[offset] & spv::OpCodeMask;
        if (opcode == spv::OpAccessChain) {
            for (const auto pointer : bytePointers) {
                if (words[offset + 1] == pointer) byteChains.push_back(words[offset + 2]);
            }
        }
        if (opcode == spv::OpStore) {
            for (const auto chain : byteChains) {
                if (words[offset + 1] == chain) ++counts.byteStores;
            }
        }
        offset += count;
    }
    return counts;
}

Counts Compile(bool byteAccess, bool vertex = false, bool coherent = false) {
    std::vector<std::uint32_t> code{0xe0000000u, 0x80020a00u, 0xbf8c3f70u, 0xe0100000u | (coherent ? 0x4000u : 0u), 0x80020a00u};
    if (vertex) code.insert(code.end(), {0xf80008cfu, 0x03020100u});
    code.push_back(0xbf810000u);
    const std::span<const std::uint32_t> words(code);
    const std::array<MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(words)}}};
    const std::array<std::uint32_t, 12> userData{0u, 0u, 0u, 0u, 0x10000000u, 0u, 0x1000u, 0x31016facu, 0x20000000u, 0u, 0x1000u, 0x31038facu};
    RecompileRequest request{};
    request.shader = {vertex ? ShaderStage::Vertex : ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), words, 0, {}};
    request.target = Target(byteAccess, coherent);
    request.layout = {0, 0, 0, 128};
    request.context.waveSize = 32;
    request.context.userDataBaseRegister = 0;
    request.context.userData = userData;
    if (vertex) request.context.vertex = ShaderVertexStageInfo{};
    else request.context.compute = ShaderComputeStageInfo{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.context.memory = memory;
    request.useCache = false;
    return Count(Recompile(request).spirv.Words());
}

}

int main() {
    try {
        const auto bytes = Compile(true);
        Check(bytes.compareExchanges == 0, "formatted buffer byte stores: a store still patches its dword with " + std::to_string(bytes.compareExchanges) + " compare-exchange loops");
        Check(bytes.byteBuffers && bytes.byteCapability && bytes.byteTypes == 1, "formatted buffer byte stores: the buffers are not addressed as bytes under StorageBuffer8BitAccess");
        Check(bytes.byteStores >= 4, "formatted buffer byte stores: the 8-bit components are stored with " + std::to_string(bytes.byteStores) + " byte stores, expected at least 4");
        const auto dwords = Compile(false);
        Check(dwords.compareExchanges > 0 && !dwords.byteBuffers && !dwords.byteCapability, "formatted buffer byte stores: without StorageBuffer8BitAccess the stores do not fall back to the dword patch");
        const auto coherent = Compile(true, false, true);
        Check(coherent.compareExchanges > 0 && coherent.byteStores == 0, "formatted buffer byte stores: a glc store does not merge its bytes into the dword atomically");
        Check(coherent.elections > 0 && coherent.broadcasts >= 2 * coherent.elections, "formatted buffer byte stores: the lanes of a glc store that target the same bytes of one dword each run their own merge (" + std::to_string(coherent.elections) + " elections, " + std::to_string(coherent.broadcasts) + " broadcasts of the first lane's target)");
        std::printf("formatted buffer byte stores tests passed (%zu byte stores, %zu compare-exchange loops without the capability, %zu merges with one writer per target)\n", bytes.byteStores, dwords.compareExchanges, coherent.elections);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
