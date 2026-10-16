#include "GuestTestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <optional>
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

std::vector<std::uint32_t> Fill(std::initializer_list<std::uint32_t> store, bool branch = false, bool memoryDescriptor = false) {
    std::vector<std::uint32_t> code{0xd7460004u, 0x04010c08u, 0xf4280302u, 0xfa000000u};
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

std::optional<ShaderRecompiler::LinearStore> Recognized(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::array<std::uint32_t, 4>& target, const std::array<std::uint32_t, 4>& constants, std::uint32_t threadsY = 1u) {
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
    return ShaderRecompiler::Recompile(request).linearStore;
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
        std::puts("linear store tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
