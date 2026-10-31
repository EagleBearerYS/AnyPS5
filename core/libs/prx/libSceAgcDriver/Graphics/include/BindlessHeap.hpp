#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BINDLESSHEAP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_BINDLESSHEAP_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "RuntimeAbi.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;
class Recorder;
class Sampler;
class StorageTexture;
class Texture;

class BindlessHeap {
public:
    explicit BindlessHeap(const Context& context);
    ~BindlessHeap();
    BindlessHeap(const BindlessHeap&) = delete;
    BindlessHeap& operator=(const BindlessHeap&) = delete;

    static BindlessHeap& For(const Context& context);
    static BindlessHeap* Existing(const Context& context);
    static void Release(VkDevice device);
    static bool Fits(const VkPhysicalDeviceDescriptorHeapPropertiesEXT& properties);
    static bool RingInResourceHeap(const VkPhysicalDeviceDescriptorHeapPropertiesEXT& properties, VkDeviceSize uniformAlignment);

    bool DescriptorHeapMode() const { return resourceHeap != nullptr; }
    VkDeviceSize ImageHeapStride() const { return imageHeapStride; }
    VkDeviceSize SamplerHeapStride() const { return samplerHeapStride; }
    void BindDescriptorHeaps(VkCommandBuffer commands) const;
    bool DescriptorHeapMappings(std::span<const std::uint32_t> spirv, std::vector<VkDescriptorSetAndBindingMappingEXT>& mappings,
                                std::uint32_t workgroupAddressOffset = ~0u, bool rootGds = false, bool rootFault = false,
                                bool pixelLds = false, bool meshWindow = false) const;
    void WritePixelLdsBuffer(VkDeviceAddress address, VkDeviceSize bytes);
    void WriteMeshWindowBuffer(VkDeviceAddress address, VkDeviceSize bytes);
    VkDeviceAddress RingAddress() const;
    VkBuffer RingBuffer() const;
    VkDeviceSize RingOffset() const;

    std::optional<std::uint32_t> TextureSlot(std::uint32_t kind, const std::shared_ptr<Texture>& texture, VkImageView view,
                                             VkImageLayout layout);
    std::optional<std::uint32_t> StorageSlot(std::uint32_t kind, const void* owner, VkImageView view);
    std::optional<std::uint32_t> StorageSlot(std::uint32_t kind, const std::shared_ptr<StorageTexture>& image, VkImageView view);
    std::optional<std::uint32_t> ViewSlot(std::uint32_t kind, const void* owner, VkImageView view);
    void ReleaseImageSlots(const void* owner);
    void ReleaseSamplerSlot(const Sampler* sampler);
    std::uint32_t SlotGeneration(std::uint32_t slot) const;
    const Texture* SlotTexture(std::uint32_t kind, std::uint32_t slot) const;
    bool SlotHoldsStorage(std::uint32_t kind, std::uint32_t slot, const void* owner, VkImageView view) const;
    std::optional<std::uint32_t> SamplerSlot(const std::shared_ptr<Sampler>& sampler);
    std::uint32_t SamplerGeneration(std::uint32_t slot) const;
    std::uint64_t SlotSerial() const { return slotSerial.load(std::memory_order_acquire); }
    bool GenerationsCurrent(std::span<const std::vector<std::uint32_t>> slots, std::span<const std::uint32_t> slotGenerations,
                            std::span<const std::uint32_t> samplerSlots, std::span<const std::uint32_t> samplerGenerations) const;
    std::uint32_t AbsentSlot(std::uint32_t kind, VkImageView view);
    static bool NearlyFull();
    static std::uint32_t ImageSlotCapacity();

    struct DataSpan {
        std::uint32_t offset = 0;
        std::span<std::uint32_t> words;
    };
    DataSpan Allocate(std::size_t words);
    struct SpanMark {
        std::uint32_t offset = 0;
        std::uint32_t words = 0;
        std::uint64_t entry = 0;
    };
    DataSpan Allocate(std::size_t words, SpanMark& mark);
    bool Held(const SpanMark& mark);

    static constexpr std::uint32_t SamplerKind = static_cast<std::uint32_t>(ShaderRecompiler::RuntimeAbi::Binding::Samplers);
    void Submitted(Recorder& recorder, std::uint64_t serial);

    struct Counts {
        std::uint64_t textures = 0, samplers = 0, full = 0, ringWraps = 0, encodes = 0, encodedCopies = 0;
        std::uint64_t slotsFreed = 0, slotsReused = 0, chunkWaits = 0;
        std::uint64_t spansWritten = 0, spansKept = 0, spansExpired = 0, keptChunksSkipped = 0;
    };
    Counts Statistics() const;

    static constexpr std::size_t RingBytes = 64u << 20u;
    static constexpr std::size_t RingChunkBytes = 2u << 20u;

private:
    Context context;
    std::unique_ptr<Buffer> ring;
    std::byte* ringData = nullptr;
    std::size_t ringBytes = RingBytes;
    VkDeviceSize ringHeapOffset = 0;
    bool ringInHeap = false;
    std::unique_ptr<Buffer> resourceHeap;
    std::unique_ptr<Buffer> samplerHeap;
    VkDeviceSize imageHeapStride = 0;
    VkDeviceSize samplerHeapStride = 0;
    VkDeviceSize resourceHeapUsed = 0;
    VkDeviceSize pixelLdsHeapOffset = 0;
    VkDeviceSize meshWindowHeapOffset = 0;
    VkDeviceSize samplerHeapUsed = 0;
    PFN_vkWriteResourceDescriptorsEXT writeResourceDescriptors = nullptr;
    PFN_vkWriteSamplerDescriptorsEXT writeSamplerDescriptors = nullptr;
    std::vector<std::byte> encodeScratch;
    void writeImageSlot(std::uint32_t kind, std::uint32_t slot, VkImageView view, VkImageLayout layout);
    void writeSamplerSlot(std::uint32_t slot, VkSampler sampler);
    void writeBufferDescriptor(VkDeviceSize offset, VkDeviceAddress address, VkDeviceSize bytes, const char* what);
    struct Chunk {
        Recorder* recorder = nullptr;
        std::uint64_t serial = 0;
        bool open = false;
        std::uint64_t entries = 0;
    };
    std::vector<Chunk> ringChunks;
    std::size_t ringChunk = ~std::size_t{0};
    void enterChunk(std::vector<Chunk>& chunks, std::size_t index, std::size_t& current, const char* what);
    std::size_t ringCursor = 0;
    std::uint32_t ringAlignment = 256;
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> slotSerial{1};
    struct TextureKey {
        std::uint32_t kind;
        const void* texture;
        VkImageView view;
        VkImageLayout layout;
        bool operator==(const TextureKey&) const = default;
    };
    struct TextureKeyHash {
        std::size_t operator()(const TextureKey& key) const {
            const auto texture = std::hash<const void*>{}(key.texture);
            const auto view = std::hash<const void*>{}(reinterpret_cast<const void*>(key.view));
            return texture ^ view ^ (static_cast<std::size_t>(key.kind) << 48u) ^ (static_cast<std::size_t>(key.layout) << 40u);
        }
    };
    std::unordered_map<TextureKey, std::uint32_t, TextureKeyHash> textureSlots;
    std::unordered_map<const void*, std::vector<std::uint32_t>> ownerSlots;
    std::uint32_t nextTextureSlot = 0;
    struct SlotOwner {
        const Texture* texture = nullptr;
        TextureKey key{};
        std::uint32_t generation = 0;
    };
    std::vector<SlotOwner> owners;
    std::vector<std::uint32_t> freeSlots;
    std::uint32_t slotCap = 0;
    std::optional<std::uint32_t> ownedSlot(std::uint32_t kind, const void* owner, VkImageView view);
    std::optional<std::uint32_t> takeSlotLocked();
    SlotOwner& claimSlotLocked(const TextureKey& key, std::uint32_t slot);
    void freeSlotLocked(std::uint32_t slot);
    const SlotOwner* ownerLocked(std::uint32_t slot) const;
    std::unordered_map<const Sampler*, std::uint32_t> samplerSlots;
    std::unordered_set<std::uint32_t> absentSlots;
    std::vector<const Sampler*> heldSamplers;
    std::vector<std::uint32_t> samplerGenerations;
    std::vector<std::uint32_t> freeSamplerSlots;
    bool refused = false;
    void noteRefusedLocked(const char* what, std::uint32_t margin);
    void noteTakenLocked();
    void noteSamplerTakenLocked();
    void noteReleasedLocked();
    std::uint32_t imageMargin() const;
    std::size_t unusedLocked() const;
    bool roomLocked() const;
    Counts counts;
};

bool GpuHeapEnabled();
void SetDescriptorHeapDevice(bool available);
bool DescriptorHeapEnabled();
bool HeapComputeEnabled();
bool DescriptorHeapMappable(std::span<const std::uint32_t> spirv, bool workgroupAddress, bool gdsAddress, bool pixelLds = false, bool meshWindow = false);
void NoteHeapComputeStage();
std::uint64_t HeapComputeStages();
void NoteImageTableBuild();
std::uint64_t ImageTableBuilds();
void NoteDataAddressPush();
void NoteEmptyResourceSet();
std::uint64_t EmptyResourceSets();
std::uint64_t DataAddressPushes();
enum class SetBindSite : std::uint8_t { Draw, Dispatch, Detiler, ColorTransfer, Presentation, Count };
enum class HeapFallback : std::uint8_t { DrawUnmapped, DrawMixedRetry, DrawRefusedPair, DrawMixedReused, DispatchUnmapped, Count };
enum class HeapStageReason : std::uint8_t {
    Heap,
    Disabled,
    PairRefused,
    FragmentOffHeap,
    StageKind,
    NoPlan,
    PlanStage,
    UnresolvedImageSource,
    KnownIneligible,
    Count
};
void NoteHeapStageReason(HeapStageReason reason);
void NoteHeapRecord(bool compute, bool heap);
void NoteHeapFallback(HeapFallback reason);
std::uint64_t HeapFallbacks(HeapFallback reason);
void NoteCpuBufferCopy(std::uint64_t bytes);
enum class HeapCost : std::uint8_t {
    StagePrepare,
    HeapStageCall,
    StageFinish,
    WalkMemo,
    Walk,
    Materialize,
    TemplateFind,
    TemplateBuild,
    Slots,
    StageData,
    BufferAddresses,
    MemoInsert,
    Guards,
    Count
};
bool HeapCostTimed();

void NoteHeapWalkMemoBytes(std::int64_t delta);
void NoteHeapCost(HeapCost cost, std::uint64_t nanoseconds);
class HeapCostScope {
public:
    explicit HeapCostScope(HeapCost cost) :
        cost(cost), start(HeapCostTimed() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~HeapCostScope() { Stop(); }
    HeapCostScope(const HeapCostScope&) = delete;
    HeapCostScope& operator=(const HeapCostScope&) = delete;
    void Stop() {
        if (start == std::chrono::steady_clock::time_point{}) return;
        NoteHeapCost(cost, static_cast<std::uint64_t>(
                               std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count()));
        start = {};
    }

private:
    HeapCost cost;
    std::chrono::steady_clock::time_point start;
};
void NoteCommandBufferBegun(VkCommandBuffer commands);
void CmdPushConstants(const Context& context, VkCommandBuffer commands, VkPipelineLayout layout, VkShaderStageFlags stages,
                      std::uint32_t offset, std::uint32_t size, const void* values);
void CmdBindDescriptorSets(const Context& context, VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
                           std::uint32_t firstSet, std::uint32_t setCount, const VkDescriptorSet* sets, SetBindSite site);
std::uint64_t DescriptorHeapBinds();

}

#endif
