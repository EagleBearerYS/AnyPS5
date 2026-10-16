#include "GuestTestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Store = 0xe01c2000u;
constexpr std::uint32_t StoreDword = 0xe0702000u;
constexpr std::uint32_t StoreDwordX4 = 0xe0782000u;
constexpr std::uint32_t StoreFormatX = 0xe0102000u;
constexpr std::uint32_t AtomicAdd = 0xe0c82000u;
constexpr std::uint32_t StoreOperands = 0x80000004u;
constexpr std::uint32_t Raw = 0x0004bfacu;
constexpr std::array<std::uint32_t, 2> ShiftAddIndex{0xd7460004u, 0x04010c08u};
constexpr std::array<std::uint32_t, 2> Mad24Index{0xd5430004u, 0x04018008u};
constexpr std::array<std::uint32_t, 2> MadSigned24Index{0xd5420004u, 0x04018008u};
constexpr std::array<std::uint32_t, 2> Mad24WrongSizeIndex{0xd5430004u, 0x04017e08u};

std::vector<std::uint32_t> Fill(std::initializer_list<std::uint32_t> store, bool branch = false, bool memoryDescriptor = false, const std::array<std::uint32_t, 2>& index = ShiftAddIndex) {
    std::vector<std::uint32_t> code{index[0], index[1], 0xf4280302u, 0xfa000000u};
    if (memoryDescriptor) code.insert(code.end(), {0xf4080002u, 0xfa000000u});
    code.insert(code.end(), {0xbf8cc07fu, 0x7e00020cu, 0x7e02020du, 0x7e04020eu, 0x7e06020fu});
    if (branch) code.push_back(0xbf880000u | static_cast<std::uint32_t>(store.size()));
    code.insert(code.end(), store);
    code.push_back(0xbf810000u);
    return code;
}

std::array<std::uint32_t, 4> Descriptor(const void* data, std::uint32_t stride, std::uint32_t records, std::uint32_t word3, bool swizzle = false) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | stride << 16u | (swizzle ? 1u << 31u : 0u), records, word3};
}

bool StagedStoreBranch(std::span<const std::uint32_t> words) {
    std::map<std::uint32_t, std::string> names;
    std::set<std::uint32_t> zero64;
    std::set<std::uint32_t> staged;
    std::set<std::uint32_t> physical;
    std::map<std::uint32_t, std::uint32_t> wide;
    std::uint32_t merge = 0;
    std::uint32_t regionEnd = 0;
    std::size_t stores = 0;
    bool writesThroughTable = false;
    bool found = false;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> 16u;
        const auto op = words[cursor] & 0xffffu;
        if (count == 0u || cursor + count > words.size()) return false;
        const auto* operands = words.data() + cursor + 1u;
        if (op == 5u) names[operands[0]] = std::string(reinterpret_cast<const char*>(operands + 1), (count - 2u) * 4u).c_str();
        if (op == 21u && operands[1] == 64u) wide[operands[0]] = 64u;
        if (op == 43u && wide.contains(operands[0]) && count == 5u && operands[2] == 0u && operands[3] == 0u) zero64.insert(operands[1]);
        if (op == 171u && (zero64.contains(operands[2]) || zero64.contains(operands[3]))) staged.insert(operands[1]);
        if (op == 247u) merge = operands[0];
        if (op == 248u && regionEnd != 0u && operands[0] == regionEnd) {
            found = found || (stores != 0u && !writesThroughTable);
            regionEnd = 0u;
        }
        if (op == 250u && regionEnd == 0u && staged.contains(operands[0])) {
            regionEnd = merge;
            stores = 0u;
            writesThroughTable = false;
        }
        if (op == 120u) physical.insert(operands[1]);
        if (regionEnd != 0u && op == 62u && physical.contains(operands[0])) ++stores;
        if (regionEnd != 0u && op == 57u) {
            const auto name = names[operands[2]];
            writesThroughTable = writesThroughTable || name.starts_with("write_bda") || name.starts_with("get_bda_write_pointer") || name.starts_with("note_bda_write");
        }
        cursor += count;
    }
    return found;
}

ShaderRecompiler::RecompileResult Compiled(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::array<std::uint32_t, 4>& target, const std::array<std::uint32_t, 4>& constants, std::uint32_t threadsY = 1u) {
    alignas(256) static std::array<std::uint32_t, 64> program{};
    std::copy(code.begin(), code.end(), program.begin());
    const std::span<const std::uint32_t> words(program.data(), code.size());
    std::array<std::uint32_t, 8> userData{};
    std::copy(target.begin(), target.end(), userData.begin());
    std::copy(constants.begin(), constants.end(), userData.begin() + 4);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(program.data()), std::as_bytes(words)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{64u, threadsY, 1u}, 0u, {true, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(program.data()), words, 0, {}},
        {64, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

std::optional<ShaderRecompiler::LinearStore> Recognized(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::array<std::uint32_t, 4>& target, const std::array<std::uint32_t, 4>& constants, std::uint32_t threadsY = 1u) {
    const auto result = Compiled(device, code, target, constants, threadsY);
    const bool reserved = result.linearStoreBaseDword != ShaderRecompiler::NoLinearStoreBaseDword;
    Require(reserved || !result.linearStore.has_value(), "linear stores: a program reserved no staging base but has a linear store");
    if (!reserved) return result.linearStore;
    Require(result.linearStoreBaseDword + 2u <= result.shaderDataDwords, "linear stores: the staging base lies outside the shader data");
    Require(StagedStoreBranch(result.spirv.Words()), "linear stores: a program with a staging base has no branch that stores to it without the page table");
    return result.linearStore;
}

bool Reserved(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::array<std::uint32_t, 4>& target, const std::array<std::uint32_t, 4>& constants, std::uint32_t threadsY = 1u) {
    return Compiled(device, code, target, constants, threadsY).linearStoreBaseDword != ShaderRecompiler::NoLinearStoreBaseDword;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        TestGuest::Array<std::uint32_t, 4096> target;
        TestGuest::Array<std::uint32_t, 4> constants;
        TestGuest::Array<std::uint32_t, 8> table;
        const auto cb = Descriptor(constants.data(), 16u, 1u, 0x0004dfacu);
        const auto address = reinterpret_cast<std::uintptr_t>(target.data());
        const auto expect = [&](const char* name, const std::optional<ShaderRecompiler::LinearStore>& found, std::uint32_t stride, std::uint32_t records, std::uint32_t elementBytes) {
            Require(found.has_value(), std::string("linear stores: ") + name + " was not recognized");
            Require(found->base == address && found->stride == stride && found->records == records && found->elementBytes == elementBytes, std::string("linear stores: ") + name + " was recognized as base 0x" + std::to_string(found->base) + ", stride " + std::to_string(found->stride) + ", records " + std::to_string(found->records) + ", " + std::to_string(found->elementBytes) + " bytes per element");
        };
        const auto refuse = [&](const char* name, const std::optional<ShaderRecompiler::LinearStore>& found) {
            Require(!found.has_value(), std::string("linear stores: ") + name + " was recognized as a linear store");
        };
        const auto fill = Fill({Store, StoreOperands});
        expect("a four-component format fill through a user-data descriptor", Recognized(*device, fill, Descriptor(target.data(), 16u, 768u, Raw), cb), 16u, 768u, 16u);
        expect("a fill whose descriptor format has two components", Recognized(*device, fill, Descriptor(target.data(), 8u, 1024u, 0x00040facu), cb), 8u, 1024u, 8u);
        expect("a dwordx4 fill", Recognized(*device, Fill({StoreDwordX4, StoreOperands}), Descriptor(target.data(), 16u, 768u, Raw), cb), 16u, 768u, 16u);
        expect("a dword fill", Recognized(*device, Fill({StoreDword, StoreOperands}), Descriptor(target.data(), 4u, 4096u, Raw), cb), 4u, 4096u, 4u);
        refuse("a store_format_x through a four-component descriptor format", Recognized(*device, Fill({StoreFormatX, StoreOperands}), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("a store indexed by the thread id alone", Recognized(*device, Fill({Store, 0x80000000u}), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("a store with an instruction offset", Recognized(*device, Fill({Store | 0x10u, StoreOperands}), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("a store skipped by a branch", Recognized(*device, Fill({Store, StoreOperands}, true), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("two stores", Recognized(*device, Fill({Store, StoreOperands, Store | 0x10u, StoreOperands}), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("an atomic add", Recognized(*device, Fill({AtomicAdd, StoreOperands}), Descriptor(target.data(), 4u, 4096u, Raw), cb));
        refuse("a store through a descriptor read from memory", Recognized(*device, Fill({Store, StoreOperands}, false, true), Descriptor(table.data(), 16u, 2u, Raw), cb));
        refuse("a swizzled descriptor", Recognized(*device, fill, Descriptor(target.data(), 16u, 768u, Raw, true), cb));
        refuse("a descriptor that adds the thread id", Recognized(*device, fill, Descriptor(target.data(), 16u, 768u, Raw | 1u << 23u), cb));
        refuse("a two-dimensional workgroup", Recognized(*device, fill, Descriptor(target.data(), 16u, 768u, Raw), cb, 2u));
        Require(Reserved(*device, fill, Descriptor(target.data(), 16u, 768u, Raw), cb), "linear stores: a linear fill reserved no staging base");
        Require(Reserved(*device, fill, Descriptor(target.data(), 16u, 768u, Raw, true), cb), "linear stores: a linear fill through a swizzled descriptor reserved no staging base for the dispatches that may stage it");
        Require(!Reserved(*device, Fill({Store, StoreOperands, Store | 0x10u, StoreOperands}), Descriptor(target.data(), 16u, 768u, Raw), cb), "linear stores: a program with two stores reserved a staging base");
        Require(!Reserved(*device, Fill({AtomicAdd, StoreOperands}), Descriptor(target.data(), 4u, 4096u, Raw), cb), "linear stores: an atomic add reserved a staging base");
        Require(!Reserved(*device, Fill({Store, StoreOperands}, true), Descriptor(target.data(), 16u, 768u, Raw), cb), "linear stores: a store skipped by a branch reserved a staging base");
        const auto mad24 = Fill({Store, StoreOperands}, false, false, Mad24Index);
        expect("a fill indexed by v_mad_u32_u24 of the group id", Recognized(*device, mad24, Descriptor(target.data(), 16u, 768u, Raw), cb), 16u, 768u, 16u);
        expect("a v_mad_u32_u24 fill whose records exceed the threads of 2^24 groups", Recognized(*device, mad24, Descriptor(target.data(), 16u, 0xffffffffu, Raw), cb), 16u, 1u << 30u, 16u);
        expect("a v_lshl_add_u32 fill whose records exceed the threads of 2^24 groups", Recognized(*device, fill, Descriptor(target.data(), 16u, 0xffffffffu, Raw), cb), 16u, 0xffffffffu, 16u);
        refuse("a fill indexed by v_mad_i32_i24 of the group id", Recognized(*device, Fill({Store, StoreOperands}, false, false, MadSigned24Index), Descriptor(target.data(), 16u, 768u, Raw), cb));
        refuse("a v_mad_u32_u24 fill whose group stride is not the workgroup size", Recognized(*device, Fill({Store, StoreOperands}, false, false, Mad24WrongSizeIndex), Descriptor(target.data(), 16u, 768u, Raw), cb));
        std::puts("linear store tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
