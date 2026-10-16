#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP

#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
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

class BdaResources;

struct BatchFaultUser {
    const BdaResources* bda = nullptr;
    std::uint64_t program = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> reads;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> writes;
};

class BatchFaultRecord {
public:
    static constexpr std::uint32_t Capacity = ShaderRecompiler::BdaAbi::WrittenPageSlots / 2;
    BatchFaultRecord(const Context& context, std::uint32_t stream, std::uint64_t batch);
    VkDescriptorBufferInfo Descriptor() const;
    std::uint32_t Stream() const { return stream; }
    std::uint64_t Batch() const { return batch; }
    std::uint64_t Id() const { return id; }
    bool Admit(std::uint32_t chunks);
    void Check(std::span<const BatchFaultUser> users) const;

private:
    BdaArena::Slice slice;
    std::uint32_t stream = 0;
    std::uint64_t batch = 0;
    std::uint32_t chunks = 0;
    std::uint64_t id = 0;
};

class BdaResources {
public:
    explicit BdaResources(const Context& context, std::shared_ptr<BatchFaultRecord> shared = nullptr);
    BdaResources(const Context& context, const GuestBufferMemory& memory, std::span<const ShaderRecompiler::BdaAbi::Range> overrides = {}, std::shared_ptr<BatchFaultRecord> shared = nullptr);
    VkDescriptorBufferInfo Table() const;
    VkDescriptorBufferInfo Fault() const;
    void CheckFault() const;
    const std::shared_ptr<BatchFaultRecord>& SharedRecord() const { return shared; }
    std::uint64_t StoreRecord() const { return shared != nullptr ? shared->Id() : ownRecord; }
    void UseRecord(const Context& context, std::shared_ptr<BatchFaultRecord> record);
    struct ImageTableFaults {
        std::uint32_t stage = 0;
        std::uint64_t shader = 0;
        std::vector<ShaderRecompiler::ImageTableEntryPoison> poison;
        std::vector<std::string> messages;
    };
    void SetImageTables(std::vector<ImageTableFaults> tables, std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges);
    void NoteExactWrites(std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges) { exactWrites = std::move(ranges); }

private:
    friend class BatchFaultRecord;
    static void markWrittenPages(std::uint32_t* words, std::span<const BdaResources* const> users);
    static void applyWrittenPages(std::uint32_t* words, std::span<const BdaResources* const> users);
    static void markWrittenRange(std::uint64_t begin, std::size_t bytes, std::span<const BdaResources* const> users);
    static void markExactWrites(std::span<const BdaResources* const> users);
    static bool shadowed(std::span<const BdaResources* const> users);
    static bool published(const ShaderRecompiler::BdaAbi::Fault& report);
    bool owns(const ShaderRecompiler::BdaAbi::Fault& report) const;
    [[noreturn]] void throwFault(const ShaderRecompiler::BdaAbi::Fault& report) const;
    std::shared_ptr<const BdaPageTable> pages;
    std::vector<ShaderRecompiler::BdaAbi::Range> shadowRanges;
    std::vector<std::shared_ptr<ShadowSlab>> shadowSlabs;
    std::vector<ShaderRecompiler::BdaAbi::Range> ownRanges;
    std::vector<ShaderRecompiler::BdaAbi::Grant> writeGrants;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> exactWrites;
    BdaArena::Slice table;
    BdaArena::Slice fault;
    std::shared_ptr<BatchFaultRecord> shared;
    std::uint64_t ownRecord = 0;
    const DeviceImageTable* deviceImages = nullptr;
    std::vector<ImageTableFaults> imageTables;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> imageTableRanges;
    std::vector<std::vector<std::byte>> imageTableBytes;
};

}

#endif
