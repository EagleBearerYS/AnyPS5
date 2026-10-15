#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP

#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include <cstddef>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

bool LoopGuardTripped();

class BdaArena {
public:
    struct Slice {
        std::shared_ptr<Buffer> slab;
        VkDeviceSize offset = 0;
        std::size_t bytes = 0;
        std::byte* data = nullptr;
    };
    static Slice Take(const Context& context, std::size_t bytes);

private:
    static constexpr std::size_t SlabBytes = std::size_t{8} << 20u;
    std::mutex mutex;
    std::vector<std::unique_ptr<Buffer>> free;
    std::shared_ptr<Buffer> current;
    std::size_t used = 0;
};

class BdaResources {
public:
    explicit BdaResources(const Context& context);
    BdaResources(const Context& context, const GuestBufferMemory& memory, std::span<const ShaderRecompiler::BdaAbi::Range> overrides = {});
    VkDescriptorBufferInfo Table() const;
    VkDescriptorBufferInfo Fault() const;
    void CheckFault() const;
    struct ImageTableFaults {
        std::uint32_t stage = 0;
        std::uint64_t shader = 0;
        std::vector<ShaderRecompiler::ImageTableEntryPoison> poison;
        std::vector<std::string> messages;
    };
    void SetImageTables(std::vector<ImageTableFaults> tables, std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges);

private:
    void markWrittenPages() const;
    std::shared_ptr<const BdaPageTable> pages;
    std::vector<ShaderRecompiler::BdaAbi::Range> shadowRanges;
    std::vector<std::shared_ptr<ShadowSlab>> shadowSlabs;
    std::vector<ShaderRecompiler::BdaAbi::Range> ownRanges;
    std::vector<ShaderRecompiler::BdaAbi::Grant> writeGrants;
    BdaArena::Slice table;
    BdaArena::Slice fault;
    const DeviceImageTable* deviceImages = nullptr;
    std::vector<ImageTableFaults> imageTables;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> imageTableRanges;
    std::vector<std::vector<std::byte>> imageTableBytes;
};

}

#endif
