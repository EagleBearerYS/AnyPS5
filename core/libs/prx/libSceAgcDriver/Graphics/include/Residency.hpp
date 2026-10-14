#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENCY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENCY_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AgcDriver::Graphics {

constexpr std::uint32_t ResidencyPageShift = 16;
constexpr std::uint64_t ResidencyPageBytes = std::uint64_t{1} << ResidencyPageShift;
constexpr std::uint32_t ResidencySubpageShift = 12;
constexpr std::uint32_t ResidencySubpageBytes = 1u << ResidencySubpageShift;
constexpr std::uint32_t ResidencySubpages = 1u << (ResidencyPageShift - ResidencySubpageShift);
constexpr std::size_t ResidencyStreams = 16;
constexpr std::uint64_t ResidencyUnchecked = ~std::uint64_t{0};

using ResidencyToken = std::array<std::uint64_t, ResidencyStreams>;
using ResidencyMask = std::uint16_t;
static_assert(sizeof(ResidencyMask) * 8 == ResidencySubpages);

constexpr ResidencyMask ResidencyWholePage = static_cast<ResidencyMask>(~ResidencyMask{0});

ResidencyToken MergeTokens(const ResidencyToken& first, const ResidencyToken& second);
bool TokenEmpty(const ResidencyToken& token);
ResidencyMask SubpageMask(std::uint64_t page, std::uint64_t address, std::uint64_t bytes);

enum class ResidencyAccess : std::uint8_t { Read, Write };
struct ResidencyImport {
    bool whole = false;
    bool cpuWritable = false;
    bool gpuWritable = false;
    bool aliased = false;
    std::uint64_t section = 0;
    std::uint64_t offset = 0;
};

struct ResidencyRange {
    std::uint64_t address = 0;
    std::uint64_t bytes = 0;
};

struct ResidencyPolicy {
    std::uint32_t chunkSlots = 1024;
    std::uint64_t budgetBytes = 0;
    std::uint32_t hotFaults = 2;
    std::uint32_t hotEpochs = 2;
};

struct MirrorSlot {
    std::uint32_t chunk = 0;
    std::uint32_t slot = 0;
    bool operator==(const MirrorSlot&) const = default;
};

struct ResidencyCopy {
    std::uint64_t page = 0;
    MirrorSlot mirror;
    std::uint32_t offset = 0;
    std::uint32_t bytes = 0;
};

class IResidencyDevice {
public:
    virtual ~IResidencyDevice() = default;
    virtual bool Reached(const ResidencyToken& token) = 0;
    virtual void Wait(const ResidencyToken& token) = 0;
    virtual ResidencyToken Newest() = 0;
    virtual ResidencyToken Current() = 0;
    virtual ResidencyImport Import(std::uint64_t page) = 0;
    virtual std::vector<ResidencyRange> Aliases(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual std::uint64_t Generation() = 0;
    virtual void ChangedPages(std::uint64_t firstPage, std::span<const std::uint64_t> generations, std::span<std::uint8_t> changed) = 0;
    virtual std::uint64_t AllocateChunk(std::uint32_t chunk, std::uint32_t slots) = 0;
    virtual void ReleaseChunk(std::uint32_t chunk) = 0;
    virtual ResidencyToken CopyIn(std::span<const ResidencyCopy> copies) = 0;
    virtual ResidencyToken WriteBack(std::span<const ResidencyCopy> copies) = 0;
    virtual bool Guard(std::uint64_t page) = 0;
    virtual void Unguard(std::span<const std::uint64_t> pages) = 0;
};

struct ResidencyCounts {
    std::uint64_t admitted = 0;
    std::uint64_t deferred = 0;
    std::uint64_t inadmissible = 0;
    std::uint64_t evicted = 0;
    std::uint64_t demoted = 0;
    std::uint64_t dropped = 0;
    std::uint64_t refreshed = 0;
    std::uint64_t faults = 0;
    std::uint64_t hot = 0;
    std::uint64_t cooled = 0;
    std::uint64_t unguardable = 0;
    std::uint64_t aliasSkipped = 0;
    std::uint64_t conflicts = 0;
    std::uint64_t waits = 0;
    std::uint64_t copiedInBytes = 0;
    std::uint64_t writtenBackBytes = 0;
    std::uint64_t chunksAllocated = 0;
    std::uint64_t chunksReleased = 0;
    std::uint32_t chunks = 0;
    std::uint32_t resident = 0;
    std::uint32_t dirty = 0;
    std::uint32_t retiring = 0;
    std::uint32_t free = 0;
};

class ResidencyManager {
public:
    ResidencyManager(IResidencyDevice& device, const ResidencyPolicy& policy);
    ResidencyManager(const ResidencyManager&) = delete;
    ResidencyManager& operator=(const ResidencyManager&) = delete;
    ~ResidencyManager();

    void SetBudget(std::uint64_t bytes);
    bool Advance();
    void Apply();
    void Prepare();
    void Use(std::uint64_t address, std::uint64_t bytes, ResidencyAccess access);
    void Pin(std::uint64_t address, std::uint64_t bytes, ResidencyAccess access, const ResidencyToken& token);
    void Demote(std::uint64_t address, std::uint64_t bytes);
    void DemoteWritable();
    void DropRange(std::uint64_t address, std::uint64_t bytes);
    void NoteImportWritten(std::uint64_t address, std::uint64_t bytes);
    void QueueCpuWritten(std::span<const std::uint64_t> pages);
    void NoteGpuWritten(std::uint64_t address, std::uint64_t bytes);
    void FlushForCpuRead(std::uint64_t address, std::uint64_t bytes);
    void Retire();

    std::optional<MirrorSlot> Slot(std::uint64_t page);
    std::optional<std::uint64_t> Translate(std::uint64_t page);
    ResidencyMask Dirty(std::uint64_t page);
    bool AnyResident(std::uint64_t address, std::uint64_t bytes);
    std::vector<ResidencyRange> ResidentRanges();
    std::vector<std::uint64_t> TakeChanges();
    std::vector<std::uint32_t> Chunks() const;
    std::uint64_t ChunkAddress(std::uint32_t chunk) const;
    std::uint32_t ChunkSlots() const { return chunkSlots; }
    std::uint64_t Serial() const { return serial; }
    std::uint64_t Epoch() const { return epoch.load(std::memory_order_relaxed); }
    bool Hot(std::uint64_t page) const { return hot.contains(page); }
    ResidencyCounts Counts() const;

private:
    static constexpr std::uint32_t None = ~std::uint32_t{0};

    struct Page {
        std::uint64_t index = 0;
        std::uint64_t generation = 0;
        MirrorSlot slot;
        std::uint32_t older = None;
        std::uint32_t newer = None;
        std::uint64_t faultEpoch = 0;
        std::uint64_t faultHistory = 0;
        ResidencyMask dirty = 0;
        bool gpuWritable = false;
        bool guarded = false;
        ResidencyToken pin{};
    };

    struct Cooling {
        std::uint64_t generation = 0;
        std::uint64_t epoch = 0;
    };

    struct Chunk {
        std::uint64_t address = 0;
        std::vector<std::uint64_t> freeBits;
        std::uint32_t freeSlots = 0;
        bool live = false;
        bool draining = false;
    };

    struct Retiring {
        MirrorSlot slot;
        ResidencyToken token{};
    };

    struct Fill {
        std::uint32_t id = 0;
        ResidencyMask mask = 0;
    };

    struct WriteBacks {
        std::vector<ResidencyCopy> copies;
        std::vector<Retiring> slots;
        std::vector<std::uint64_t> pages;
        std::vector<std::uint64_t> unguard;
    };

    bool settled(const ResidencyToken& token);
    void applyDrops();
    void applyQueued();
    std::vector<std::uint32_t> residentIn(std::uint64_t address, std::uint64_t bytes) const;
    std::vector<Fill> withAliases(std::uint64_t address, std::uint64_t bytes);
    std::optional<std::uint32_t> find(std::uint64_t page) const;
    void unlink(std::uint32_t id);
    void linkNewest(std::uint32_t id);
    std::uint32_t newPage(std::uint64_t page, MirrorSlot slot, const ResidencyImport& import, bool guarded);
    void releasePage(std::uint32_t id);
    std::optional<MirrorSlot> takeFree();
    void freeSlot(MirrorSlot slot);
    void retireReached();
    std::optional<std::uint32_t> allocateChunk();
    void releaseChunk(std::uint32_t chunk);
    std::optional<std::uint32_t> victim(std::uint64_t protectFirst, std::uint64_t protectLast);
    void admit(std::uint64_t first, std::uint64_t last);
    void coolHot(std::uint64_t first, std::span<const std::uint8_t> written);
    std::optional<MirrorSlot> acquireSlot(std::uint64_t protectFirst, std::uint64_t protectLast, WriteBacks& writeBacks);
    void leave(std::uint32_t id, WriteBacks& writeBacks);
    void forget(std::uint32_t id);
    void recordWriteBacks(WriteBacks& writeBacks);
    void appendRuns(std::uint64_t page, MirrorSlot slot, ResidencyMask mask, std::vector<ResidencyCopy>& copies) const;
    void copyIn(std::span<const Fill> fills);
    void refresh(std::span<const Fill> fills);
    void waitWriters(std::span<const Fill> fills);
    void applyFaults(std::vector<std::uint64_t> written);
    void evictDraining();
    void demote(std::span<const std::uint32_t> ids);
    void changed(std::uint64_t page);
    void waitFor(const ResidencyToken& token);

    IResidencyDevice& device;
    const std::uint32_t chunkSlots;
    const std::uint32_t hotFaults;
    const std::uint32_t hotEpochs;
    std::uint32_t maxChunks = 0;
    std::uint32_t liveChunks = 0;
    std::vector<Chunk> chunks;
    std::vector<Page> pages;
    std::vector<std::uint32_t> freePages;
    std::unordered_map<std::uint64_t, std::uint32_t> resident;
    std::uint32_t oldest = None;
    std::uint32_t newest = None;
    std::deque<Retiring> retiring;
    std::unordered_map<std::uint64_t, ResidencyToken> writers;
    std::unordered_map<std::uint64_t, ResidencyToken> writingBack;
    std::unordered_set<std::uint64_t> changes;
    std::unordered_map<std::uint64_t, Cooling> hot;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> canonical;
    std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>> aliasKeys;
    std::atomic<std::uint64_t> epoch{1};
    std::atomic<bool> queued{false};
    std::uint64_t serial = 0;
    std::uint64_t rangesSerial = ~std::uint64_t{0};
    std::vector<ResidencyRange> ranges;
    std::uint32_t dirtyPages = 0;
    ResidencyCounts counts;
    mutable std::mutex residentMutex;
    std::mutex queueMutex;
    std::vector<ResidencyRange> dropQueue;
    std::vector<std::uint64_t> cpuQueue;
};

}

#endif
