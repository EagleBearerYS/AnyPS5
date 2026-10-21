#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BDARESOURCES_HPP

#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

bool LoopGuardTripped();

struct BdaFaultChunk;

class BdaFaultRecord {
public:
    BdaFaultRecord(const Context& context, bool reportOnly);
    ~BdaFaultRecord();
    BdaFaultRecord(const BdaFaultRecord&) = delete;
    BdaFaultRecord& operator=(const BdaFaultRecord&) = delete;
    VkDescriptorBufferInfo Descriptor() const;
    std::span<std::byte> Bytes() const;
    void Park();
    void Unpark();

private:
    VkDevice device = VK_NULL_HANDLE;
    std::shared_ptr<BdaFaultChunk> chunk;
    VkDeviceSize offset = 0;
    bool parked = false;
};

class BdaResources {
public:
    explicit BdaResources(const Context& context, bool reportOnly = false);
    BdaResources(const Context& context, const GuestBufferMemory& memory, bool reportOnly = false);
    ~BdaResources();
    BdaResources(const BdaResources&) = delete;
    BdaResources& operator=(const BdaResources&) = delete;
    VkDescriptorBufferInfo Table() const;
    VkDescriptorBufferInfo Fault() const;
    void CheckFault() const;
    std::unique_ptr<BdaFaultRecord> FaultRecord(const Context& context) const;
    void CheckFault(BdaFaultRecord& record) const;
    static bool SharedFaultSlots();
    void NoteUse(std::uint64_t serial);
    bool TakeFaultSlot(const Context& context);
    bool HoldsFaultSlot() const { return faultChunk != nullptr; }
    static void NoteFinished(VkDevice device, std::uint64_t serial);
    struct FaultSlotCounts {
        std::uint64_t chunks = 0;
        std::uint64_t slots = 0;
        std::uint64_t held = 0;
        std::uint64_t reclaimed = 0;
        std::uint64_t retaken = 0;
        std::uint64_t records = 0;
        std::uint64_t recordBytes = 0;
        std::uint64_t parked = 0;
        std::uint64_t bytes = 0;
    };
    static FaultSlotCounts FaultSlots(VkDevice device);
    struct ImageTableFaults {
        std::uint32_t stage = 0;
        std::uint64_t shader = 0;
        std::vector<ShaderRecompiler::ImageTableEntryPoison> poison;
        std::vector<std::string> messages;
    };
    void SetImageTables(std::vector<ImageTableFaults> tables, std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges);
    struct SrtGuardFaults {
        std::uint32_t stage = 0;
        std::vector<std::uint32_t> flattenedSrt;
    };
    void SetSrtGuards(std::vector<SrtGuardFaults> guards);
    // APS5_PROFILE_DRAW: page tables served from the per-device cache and built anew (cumulative),
    // and how many recent tables the cache holds right now.
    struct TableCacheStats {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::size_t held = 0;
        // Hits served by the cached address space's serial alone (no hash, no compare; see
        // GuestBufferMemory::CachedAddressTable), counted in `hits` too.
        std::uint64_t spaceTables = 0;
        // Why the most recently used table did not serve a build ([bda-table] first-differing entry
        // class): its owners were gone (expired weak reference), or its word hash differed, split by
        // whether the first differing range begins below 0x10000000 (a low mapping: the exe image,
        // the system heaps) or in the guest heap above; `sameHash` is a hash match whose ranges
        // still differed, `empty` a lookup with no entry held.
        std::uint64_t firstExpired = 0;
        std::uint64_t firstDiffersLow = 0;
        std::uint64_t firstDiffersHeap = 0;
        std::uint64_t firstSameHash = 0;
        std::uint64_t firstEmpty = 0;
    };
    static TableCacheStats TableCacheCounters();

private:
    void checkFault(std::span<std::byte> faults, bool onlyReport) const;
    void markWrittenChunks(std::uint32_t* words) const;
    void markWritableRanges() const;
    // The page table is read-only to the shader, so consecutive builds mapping the same ranges to the
    // same device addresses share one buffer while one of them is alive (see the table cache in
    // BdaResources.cpp, which refers to it weakly).
    void markWrittenPages(std::span<std::byte> faults) const;
    void checkSrtGuard(std::span<const std::byte> faults) const;
    std::span<std::byte> faultBytes() const;
    std::shared_ptr<Buffer> table;
    VkDevice faultDevice = VK_NULL_HANDLE;
    std::shared_ptr<BdaFaultChunk> faultChunk;
    VkDeviceSize faultOffset = 0;
    bool reportOnly = false;
    std::uint64_t lastUse = 0;
    std::size_t tableBytes = 0;
    std::vector<ImageTableFaults> imageTables;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> imageTableRanges;
    std::vector<SrtGuardFaults> srtGuards;
};

}

#endif
