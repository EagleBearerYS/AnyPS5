#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BindlessHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "Optimization/ResourceMaterializer.hpp"
#include "IntermediateRepresentation/IrProgram.hpp"
#include "DeviceImageTableAbi.hpp"
#include "ImageTableAbi.hpp"
#include "RuntimeAbi.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace AgcDriver::Graphics {

namespace {

namespace Abi = ShaderRecompiler::DeviceImageTableAbi;
namespace Runtime = ShaderRecompiler::RuntimeAbi;
using Reason = ShaderRecompiler::ImageTableAbi::PoisonReason;

constexpr std::uint32_t RequiredSets = Runtime::DeviceTableSet + 1u;
constexpr std::uint32_t HeapImageKinds = Runtime::FirstStorageImageBinding - Runtime::FirstImageBinding;
constexpr VkDeviceSize UpdateBytes = 65536u;
constexpr VkDeviceSize TableBytes = static_cast<VkDeviceSize>(DeviceImageTable::TableWords) * sizeof(std::uint32_t);
constexpr VkDeviceSize RingBytes = static_cast<VkDeviceSize>(DeviceImageTable::RingWords) * sizeof(std::uint32_t);
constexpr VkDeviceSize BucketsOffset = static_cast<VkDeviceSize>(Abi::HeaderWords) * sizeof(std::uint32_t);
constexpr std::uint32_t BlockShift = 16u;

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("AGC device image table: ") + operation + ": Vulkan result " + std::to_string(result));
}

bool profiled() {
    static const bool profile = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return profile;
}

std::string hexWords(std::span<const std::uint32_t> words) {
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < words.size(); ++index) text << (index == 0 ? "" : " ") << std::setw(8) << words[index];
    return text.str();
}

struct Tables {
    std::mutex mutex;
    std::unordered_map<VkDevice, std::unique_ptr<DeviceImageTable>> tables;
};

Tables& tables() {
    static auto& registry = *new Tables();
    return registry;
}

struct PacketTotals {
    std::array<std::atomic<std::uint64_t>, 4> packets{};
    std::array<std::atomic<std::uint64_t>, 4> nanoseconds{};
};

PacketTotals& packetTotals() {
    static PacketTotals totals;
    return totals;
}

std::array<std::uint64_t, 8> packetSnapshot() {
    auto& totals = packetTotals();
    std::array<std::uint64_t, 8> snapshot{};
    for (std::size_t index = 0; index < 4u; ++index) {
        snapshot[index] = totals.packets[index].load(std::memory_order_relaxed);
        snapshot[4u + index] = totals.nanoseconds[index].load(std::memory_order_relaxed);
    }
    return snapshot;
}

std::string hexText(std::uint64_t value) {
    std::ostringstream text;
    text << std::hex << value;
    return text.str();
}

bool pointSampled(const ShaderRecompiler::ImageResource& mode) {
    return mode.numericClass == ShaderRecompiler::IrTextureNumericClass::Uint || mode.numericClass == ShaderRecompiler::IrTextureNumericClass::Sint || mode.conversionFormat != ShaderRecompiler::IrBufferFormat::Invalid || mode.depthBits;
}

bool multisampled(const ShaderRecompiler::ImageResource& mode) {
    return mode.dimension == ShaderRecompiler::RdnaImageDimension::Dim2DMsaa || mode.dimension == ShaderRecompiler::RdnaImageDimension::Dim2DMsaaArray;
}

ShaderRecompiler::DescriptorImageShape shapeOf(const ShaderRecompiler::ImageResource& mode) {
    using Shape = ShaderRecompiler::DescriptorImageShape;
    using Dimension = ShaderRecompiler::RdnaImageDimension;
    switch (mode.dimension) {
        case Dimension::Dim1D: return Shape::Image1D;
        case Dimension::Dim1DArray: return Shape::Image1DArray;
        case Dimension::Dim2D: return Shape::Image2D;
        case Dimension::Dim2DArray: return Shape::Image2DArray;
        case Dimension::Dim3D: return Shape::Image3D;
        default: break;
    }
    throw std::runtime_error("AGC device image table: an image mode of dimension " + std::to_string(static_cast<std::uint32_t>(mode.dimension)) + " has no sampled descriptor image shape");
}

}

bool GpuPathEnabled() {
    static const bool enabled = std::getenv("APS5_GPU_PATH") != nullptr;
    return enabled;
}

std::optional<std::string> DeviceImagePathRefusal(const VkPhysicalDeviceLimits& limits, const DeviceImagePathFeatures& features) {
    if (limits.maxBoundDescriptorSets < RequiredSets) return "the device binds " + std::to_string(limits.maxBoundDescriptorSets) + " descriptor sets, and the image heap and the device image table need sets " + std::to_string(Runtime::DeviceHeapSet) + " and " + std::to_string(Runtime::DeviceTableSet);
    if (!features.heapFeatures) return "the device lacks descriptorBindingSampledImageUpdateAfterBind, descriptorBindingPartiallyBound or descriptorBindingUpdateUnusedWhilePending, which the image heap needs";
    if (!features.nonUniformIndexing) return "the device lacks shaderSampledImageArrayNonUniformIndexing, which sampling the image heap at a slot read from the table needs";
    const auto heapImages = static_cast<std::uint64_t>(HeapImageKinds) * Runtime::GpuImageHeapSlots;
    if (features.sampledImages < heapImages) return "the device binds " + std::to_string(features.sampledImages) + " update-after-bind sampled images per stage, and the image heap holds " + std::to_string(heapImages);
    if (features.samplers < Runtime::GpuSamplerHeapSlots) return "the device binds " + std::to_string(features.samplers) + " update-after-bind samplers per stage, and the image heap holds " + std::to_string(Runtime::GpuSamplerHeapSlots);
    if (limits.maxStorageBufferRange < TableBytes) return "the device's storage buffer range of " + std::to_string(limits.maxStorageBufferRange) + " bytes is smaller than the device image table's " + std::to_string(TableBytes);
    return std::nullopt;
}

void NoteDeviceImagePacket(bool dispatch, bool deviceImages, double milliseconds) {
    auto& totals = packetTotals();
    const auto index = (dispatch ? 2u : 0u) + (deviceImages ? 1u : 0u);
    totals.packets[index].fetch_add(1u, std::memory_order_relaxed);
    totals.nanoseconds[index].fetch_add(static_cast<std::uint64_t>(std::max(milliseconds, 0.0) * 1e6), std::memory_order_relaxed);
}

VkDescriptorSetLayout DeviceImageHeap::Layout(const Context& context) {
    return BindlessHeap::For(context).HeapLayout();
}

VkDescriptorSet DeviceImageHeap::Set(const Context& context) {
    return BindlessHeap::For(context).HeapSet();
}

std::optional<std::uint32_t> DeviceImageHeap::Slot(const Context& context, std::uint32_t kind, const std::shared_ptr<Texture>& texture, VkImageView view, VkImageLayout layout, bool replacing) {
    auto& heap = BindlessHeap::For(context);
    const auto keepFree = std::max(heap.TextureSlotMargin(), GpuHeapEnabled() ? heap.TextureSlotCap() - heap.TextureSlotCap() / 2u : 0u);
    return heap.TextureSlot(kind, texture, view, layout, replacing && keepFree != 0u ? keepFree - 1u : keepFree);
}

DeviceImageTable::DeviceImageTable(const Context& context, InterfaceLookup lookup) : DeviceImageTable(context, std::move(lookup), Budget{}) {}

DeviceImageTable::DeviceImageTable(const Context& context, InterfaceLookup lookup, Budget budget) : context(context), lookup(std::move(lookup)), keys(std::min(DeviceImageKeys::InitialBuckets, budget.maxBuckets), budget.maxBuckets), budget(budget) {
    const auto destroyLayout = context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    const auto destroyPool = context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
    try {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {Runtime::DeviceTableBucketsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, ImageHeapStages(context), nullptr},
            {Runtime::DeviceTableRingBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, ImageHeapStages(context), nullptr},
        }};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &layoutInfo, nullptr, &layout), "vkCreateDescriptorSetLayout");
        const VkDescriptorSetLayoutCreateInfo emptyInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &emptyInfo, nullptr, &emptyLayout), "vkCreateDescriptorSetLayout (empty)");
        const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1}}};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();
        check(context.Function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        check(context.Function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device, &allocate, &set), "vkAllocateDescriptorSets");
        table = std::make_unique<Buffer>(context, static_cast<std::size_t>(TableBytes), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        const auto alignment = std::max<VkDeviceSize>(context.limits.minStorageBufferOffsetAlignment, 16u);
        chunkStride = (RingBytes + alignment - 1u) / alignment * alignment;
        ring = std::make_unique<Buffer>(context, static_cast<std::size_t>(chunkStride * RingChunks), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memset(ring->Bytes().data(), 0, ring->Bytes().size());
        chunks.resize(RingChunks);
        const VkDescriptorBufferInfo tableInfo{table->Handle(), 0, TableBytes};
        const VkDescriptorBufferInfo ringInfo{ring->Handle(), 0, RingBytes};
        const std::array<VkWriteDescriptorSet, 2> writes{{
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, Runtime::DeviceTableBucketsBinding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &tableInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, Runtime::DeviceTableRingBinding, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, nullptr, &ringInfo, nullptr},
        }};
        context.Function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
    } catch (...) {
        if (pool != VK_NULL_HANDLE) destroyPool(context.device, pool, nullptr);
        if (emptyLayout != VK_NULL_HANDLE) destroyLayout(context.device, emptyLayout, nullptr);
        if (layout != VK_NULL_HANDLE) destroyLayout(context.device, layout, nullptr);
        throw;
    }
}

DeviceImageTable::~DeviceImageTable() {
    const auto destroyLayout = context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    if (pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
    if (emptyLayout != VK_NULL_HANDLE) destroyLayout(context.device, emptyLayout, nullptr);
    if (layout != VK_NULL_HANDLE) destroyLayout(context.device, layout, nullptr);
}

DeviceImageTable& DeviceImageTable::Create(const Context& context, InterfaceLookup lookup) {
    auto& registry = tables();
    std::lock_guard lock(registry.mutex);
    auto& table = registry.tables[context.device];
    if (table != nullptr) throw std::runtime_error("AGC device image table: the device already has one");
    table = std::make_unique<DeviceImageTable>(context, std::move(lookup));
    return *table;
}

bool DeviceImageTable::ExpectsKeys() {
    static const bool expects = std::getenv("APS5_NO_DEVICE_IMAGE_EXPECT") == nullptr;
    return expects;
}

DeviceImageTable* DeviceImageTable::Existing(const Context& context) {
    auto& registry = tables();
    std::lock_guard lock(registry.mutex);
    const auto found = registry.tables.find(context.device);
    return found != registry.tables.end() ? found->second.get() : nullptr;
}

void DeviceImageTable::Release(VkDevice device) {
    std::unique_ptr<DeviceImageTable> released;
    auto& registry = tables();
    std::lock_guard lock(registry.mutex);
    const auto found = registry.tables.find(device);
    if (found == registry.tables.end()) return;
    released = std::move(found->second);
    registry.tables.erase(found);
}

void DeviceImageTable::Prepare(const Context& build, Recorder& recorder, std::span<const ShaderRecompiler::PipelineIdentity> programs) {
    std::lock_guard lock(mutex);
    ++totals.prepares;
    resolve(build, recorder);
    refresh(build, recorder);
    evict(build, recorder);
    upload(recorder);
    auto& chunk = chunks[chunkFor(recorder, true)];
    chunk.programs.insert(programs.begin(), programs.end());
    report();
}

void DeviceImageTable::Expect(const Context& build, Recorder& recorder, std::span<const ShaderRecompiler::DeviceImageTableAbi::Expectation> images) {
    if (!ExpectsKeys()) return;
    std::lock_guard lock(mutex);
    for (const auto& image : images) {
        ++totals.expected;
        if (keys.Find(image.key).has_value()) continue;
        Entry entry;
        const auto value = lookupKey(build, image.key, entry);
        if (!value.has_value()) continue;
        if (!insert(image.key, *value)) {
            if (entry.texture != nullptr) recorder.Keep(std::move(entry.texture));
            continue;
        }
        if (entry.texture == nullptr) continue;
        hold(image.key, std::move(entry));
        ++totals.inserted;
        ++totals.expectedInserted;
    }
}

void DeviceImageTable::SetInterfaceLookup(InterfaceLookup interfaces) {
    std::lock_guard lock(mutex);
    lookup = std::move(interfaces);
}

void DeviceImageTable::NoteProgram(const ShaderRecompiler::RecompileResult& result, std::uint64_t codeAddress) {
    if (!result.deviceImageTables) return;
    const auto identity = result.Identity();
    std::lock_guard lock(mutex);
    auto& addresses = identityPrograms[identity];
    addresses.insert(codeAddress);
    if (failedPrograms.contains(identity)) fallbackPrograms.insert(codeAddress);
}

bool DeviceImageTable::ServesProgram(std::uint64_t codeAddress) const {
    std::lock_guard lock(mutex);
    return !fallbackPrograms.contains(codeAddress);
}

bool DeviceImageTable::ServesIdentity(const ShaderRecompiler::PipelineIdentity& identity) const {
    std::lock_guard lock(mutex);
    return !failedPrograms.contains(identity);
}

void DeviceImageTable::fallBack(const std::set<ShaderRecompiler::PipelineIdentity>& programs, const std::string& reason) {
    for (const auto& identity : programs) {
        if (!failedPrograms.insert(identity).second) continue;
        ++totals.fallbacks;
        lastFallback = reason;
        std::string addresses;
        if (const auto found = identityPrograms.find(identity); found != identityPrograms.end()) {
            for (const auto address : found->second) {
                fallbackPrograms.insert(address);
                addresses += (addresses.empty() ? "0x" : ", 0x") + hexText(address);
            }
        }
        std::fprintf(stderr, "[device-images] the program %s (identity %016llx%016llx) takes the CPU table path from its next recompile: %s\n", addresses.empty() ? "of no noted address" : addresses.c_str(), static_cast<unsigned long long>(identity.high), static_cast<unsigned long long>(identity.low), reason.c_str());
    }
}

void DeviceImageTable::Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, Recorder& recorder) {
    std::lock_guard lock(mutex);
    const auto chunk = chunkFor(recorder, false);
    const auto heap = DeviceImageHeap::Set(context);
    const auto offset = static_cast<std::uint32_t>(static_cast<VkDeviceSize>(chunk) * chunkStride);
    const auto bind = context.Resolved(&DeviceFunctions::cmdBindDescriptorSets, "vkCmdBindDescriptorSets");
    bind(commands, bindPoint, pipelineLayout, Runtime::DeviceHeapSet, 1, &heap, 0, nullptr);
    bind(commands, bindPoint, pipelineLayout, Runtime::DeviceTableSet, 1, &set, 1, &offset);
}

std::span<std::uint32_t> DeviceImageTable::RingChunk(Recorder& recorder) {
    std::lock_guard lock(mutex);
    const auto chunk = chunkFor(recorder, true);
    auto bytes = ring->Bytes().subspan(static_cast<std::size_t>(chunk * chunkStride), static_cast<std::size_t>(RingBytes));
    return {reinterpret_cast<std::uint32_t*>(bytes.data()), RingWords};
}

VkBuffer DeviceImageTable::TableBuffer() const {
    return table->Handle();
}

std::optional<DeviceImageValue> DeviceImageTable::Find(const Key& key) const {
    std::lock_guard lock(mutex);
    return keys.Find(key);
}

std::vector<std::uint32_t> DeviceImageTable::KeyWords() const {
    std::lock_guard lock(mutex);
    const auto words = keys.Words();
    return {words.begin(), words.end()};
}

std::string DeviceImageTable::LastFault() const {
    std::lock_guard lock(mutex);
    return lastFault;
}

std::string DeviceImageTable::LastFallback() const {
    std::lock_guard lock(mutex);
    return lastFallback;
}

std::uint32_t DeviceImageTable::OverflowStreak(const ShaderRecompiler::PipelineIdentity& identity) const {
    std::lock_guard lock(mutex);
    const auto found = overflowStreaks.find(identity);
    return found != overflowStreaks.end() ? found->second : 0u;
}

std::vector<std::pair<VkImage, bool>> DeviceImageTable::StorageImages() const {
    std::lock_guard lock(mutex);
    if (!storageImages.has_value()) {
        std::set<VkImage> images;
        for (const auto& [key, entry] : entries) {
            if (entry.texture != nullptr && entry.texture->StorageSource() != nullptr) images.insert(entry.texture->StorageSource()->Image());
        }
        storageImages.emplace();
        for (const auto image : images) storageImages->emplace_back(image, false);
    }
    return *storageImages;
}

std::shared_ptr<Texture> DeviceImageTable::EntryTexture(const Key& key) const {
    std::lock_guard lock(mutex);
    const auto found = entries.find(key);
    return found != entries.end() ? found->second.texture : nullptr;
}

void DeviceImageTable::resolve(const Context& build, Recorder& recorder) {
    if (pending.empty()) return;
    ++resolveGeneration;
    std::vector<Key> order;
    std::map<Key, std::set<ShaderRecompiler::PipelineIdentity>> missed;
    const std::set<ShaderRecompiler::PipelineIdentity>* resolved = nullptr;
    for (const auto& [miss, programs] : std::exchange(pending, {})) {
        const auto [found, inserted] = missed.try_emplace(miss.key);
        if (inserted) order.push_back(miss.key);
        if (programs == nullptr) continue;
        found->second.insert(programs->begin(), programs->end());
        if (programs.get() == resolved) continue;
        resolved = programs.get();
        for (const auto& identity : *programs) overflowFrom.try_emplace(identity, resolveGeneration);
    }
    for (const auto& key : order) {
        const auto& programs = missed.at(key);
        if (keys.Find(key).has_value()) continue;
        ++totals.distinct;
        Entry entry;
        std::string failure;
        const auto value = lookupKey(build, key, entry, &failure);
        if (!value.has_value()) {
            if (!failure.empty()) fallBack(programs, failure);
            continue;
        }
        if (!insert(key, *value)) {
            if (entry.texture != nullptr) recorder.Keep(std::move(entry.texture));
            fallBack(programs, "the key table is full at " + std::to_string(keys.Entries()) + " entries in " + std::to_string(keys.Buckets()) + " buckets, even after its budget evicted");
            continue;
        }
        if (entry.texture == nullptr) continue;
        hold(key, std::move(entry));
        ++totals.inserted;
    }
}

std::optional<DeviceImageValue> DeviceImageTable::lookupKey(const Context& build, const Key& key, Entry& entry, std::string* failure, bool replacing) {
    const auto id = static_cast<std::uint64_t>(key[Abi::InterfaceWord]) | (static_cast<std::uint64_t>(key[Abi::InterfaceWord + 1u]) << 32u);
    const auto interface = lookup ? lookup(id) : nullptr;
    if (interface == nullptr) throw std::runtime_error("AGC device image table: a missed key names the image interface 0x" + hexText(id) + ", which no compiled program registered");
    ShaderRecompiler::DescriptorValue descriptor;
    descriptor.dwordCount = Abi::DescriptorWords;
    descriptor.availableDwordMask = 0xffu;
    std::copy_n(key.begin(), Abi::DescriptorWords, descriptor.dwords.begin());
    const auto match = ShaderRecompiler::ResourceMaterializer::TryRuntimeImageMode(interface->image, descriptor, interface->modes);
    if (!match.mode.has_value()) return reject(key, match.reason, match.detail);
    if (*match.mode >= interface->modes.size()) throw std::runtime_error("AGC device image table: the runtime image mode check chose a mode the interface does not have");
    const auto& mode = interface->modes[*match.mode];
    if (multisampled(mode)) return reject(key, Reason::Multisampled, "an image table entry is multisampled");
    const auto samplerClass = key[Abi::SamplerClassWord];
    if (pointSampled(mode) && (samplerClass & Abi::SamplerReduction) != 0u) return reject(key, Reason::Reduction, "a min or max reduction sampler that filters between texels or mip levels samples an image that needs point filtering (sint, converted or depth-bits format), which is not implemented");
    const auto kind = static_cast<std::uint32_t>(ShaderRecompiler::DescriptorBindingForImage(mode));
    if (kind < Runtime::FirstImageBinding || kind >= Runtime::FirstStorageImageBinding) return reject(key, Reason::UnsupportedOperation, "the device image table serves sampled images only");
    DeviceTableTexture texture;
    std::uint64_t stamp = 0;
    try {
        const DeviceTableView view{shapeOf(mode), mode.depthCompare, mode.conversionFormat != ShaderRecompiler::IrBufferFormat::Invalid || mode.depthBits};
        texture = LookupDeviceTableTexture(build, std::span<const std::uint32_t, Abi::DescriptorWords>(key.data(), Abi::DescriptorWords), view, samplerClass);
        stamp = GuestMemory::TrackerGeneration();
    } catch (const std::runtime_error& error) {
        return reject(key, Reason::DriverRejected, error.what());
    }
    if (!texture.unservable.empty()) {
        unserved(key, texture.unservable);
        if (failure != nullptr) *failure = texture.unservable;
        return std::nullopt;
    }
    const auto slot = DeviceImageHeap::Slot(build, kind, texture.texture, texture.view, texture.layout, replacing);
    if (!slot.has_value()) {
        heapFull(key, kind);
        if (failure != nullptr) *failure = "the image heap has no free slot of kind " + std::to_string(kind);
        return std::nullopt;
    }
    entry = {std::move(texture.texture), kind, texture.address, texture.guestBytes, texture.generation, stamp, 0u, 0u};
    return DeviceImageValue{*slot, *match.mode, 0u, 0u};
}

DeviceImageValue DeviceImageTable::reject(const Key& key, Reason reason, const std::string& message) {
    if (rejections.try_emplace(key, Rejection{reason, message}).second) {
        ++totals.rejected;
        const auto name = ShaderRecompiler::ImageTableAbi::PoisonReasonName(reason);
        std::fprintf(stderr, "[device-images] the descriptor (words %s) is rejected: %.*s: %s\n", hexWords(std::span<const std::uint32_t>(key).first(Abi::DescriptorWords)).c_str(), static_cast<int>(name.size()), name.data(), message.c_str());
    }
    return {0u, 0u, 1u + static_cast<std::uint32_t>(reason), 0u};
}

void DeviceImageTable::unserved(const Key& key, const std::string& why) {
    ++totals.unserved;
    if (unservedKeys.insert(key).second) std::fprintf(stderr, "[device-images] the descriptor (words %s) stays unresolved: %s\n", hexWords(std::span<const std::uint32_t>(key).first(Abi::DescriptorWords)).c_str(), why.c_str());
}

void DeviceImageTable::heapFull(const Key& key, std::uint32_t kind) {
    ++totals.heapFull;
    if (fullKinds.insert(kind).second) std::fprintf(stderr, "[device-images] the image heap has no free slot of kind %u for the descriptor (words %s); it stays unresolved\n", kind, hexWords(std::span<const std::uint32_t>(key).first(Abi::DescriptorWords)).c_str());
}

bool DeviceImageTable::insert(const Key& key, const DeviceImageValue& value) {
    if (keys.Insert(key, value) != DeviceImageKeys::Insertion::Full) return true;
    ++totals.tableFull;
    if (!tableFullReported) std::fprintf(stderr, "[device-images] the key table is full at %u entries in %u buckets; new keys stay unresolved\n", keys.Entries(), keys.Buckets());
    tableFullReported = true;
    return false;
}

void DeviceImageTable::hold(const Key& key, Entry entry) {
    const std::pair use{entry.kind, static_cast<const void*>(entry.texture.get())};
    if (textureUses[use]++ == 0u) {
        auto& live = liveTextures[entry.kind];
        totals.peakTextures[entry.kind] = std::max(totals.peakTextures[entry.kind], ++live);
    }
    entry.used = ++useSerial;
    entry.usedPresent = Recorder::Presents();
    const auto first = entry.address >> BlockShift;
    const auto last = (entry.address + std::max<std::uint64_t>(entry.guestBytes, 1u) - 1u) >> BlockShift;
    for (auto block = first; block <= last; ++block) blocks[block].keys.insert(key);
    entries[key] = std::move(entry);
    storageImages.reset();
}

void DeviceImageTable::release(const Key& key, Recorder& recorder) {
    const auto found = entries.find(key);
    if (found == entries.end()) return;
    auto& entry = found->second;
    const auto first = entry.address >> BlockShift;
    const auto last = (entry.address + std::max<std::uint64_t>(entry.guestBytes, 1u) - 1u) >> BlockShift;
    for (auto block = first; block <= last; ++block) {
        const auto held = blocks.find(block);
        if (held == blocks.end()) continue;
        held->second.keys.erase(key);
        if (held->second.keys.empty()) blocks.erase(held);
    }
    const std::pair use{entry.kind, static_cast<const void*>(entry.texture.get())};
    if (const auto counted = textureUses.find(use); counted != textureUses.end() && --counted->second == 0u) {
        textureUses.erase(counted);
        --liveTextures[entry.kind];
    }
    if (entry.texture != nullptr) recorder.Keep(std::move(entry.texture));
    entries.erase(found);
    storageImages.reset();
}

void DeviceImageTable::refresh(const Context& build, Recorder& recorder) {
    if (entries.empty()) return;
    const ValidationState state{GuestMemory::ThreadCollectEpoch(), GuestMemory::UnwatchSerial(), DepthSurfacesSerial(), StorageTexture::PendingSerial(), GuestMemory::OwnWriteSerial()};
    if (!validated.has_value()) {
        validated = state;
        return;
    }
    if (state.epoch != 0u && *validated == state) return;
    const bool everything = validated->unwatch != state.unwatch || validated->depthSurfaces != state.depthSurfaces;
    const bool pending = validated->pending != state.pending;
    const bool epochMoved = state.epoch == 0u || validated->epoch != state.epoch;
    validated = state;
    ++totals.scans;
    const auto scanned = std::exchange(scannedStamp, GuestMemory::TrackerGeneration());
    std::set<Key> stale;
    std::vector<std::uint64_t> generations;
    std::vector<std::uint8_t> changed;
    std::vector<std::uint8_t> cpu;
    for (auto run = blocks.begin(); run != blocks.end();) {
        auto end = std::next(run);
        while (end != blocks.end() && end->first == std::prev(end)->first + 1u) ++end;
        const auto begin = run->first << BlockShift;
        const auto count = static_cast<std::size_t>(std::prev(end)->first - run->first + 1u);
        const auto bytes = static_cast<std::size_t>(count) << BlockShift;
        static_cast<void>(GuestMemory::CollectWrites(begin, bytes));
        if (!everything && !epochMoved && !pending && scanned != 0u && GuestMemory::NewestStamp(begin, bytes) <= scanned) {
            run = end;
            continue;
        }
        generations.clear();
        for (auto block = run; block != end; ++block) generations.push_back(everything ? 0u : block->second.generation);
        changed.assign(count, GuestMemory::BlockWritten);
        cpu.assign(count, 1u);
        static_cast<void>(GuestMemory::ChangedBlocks(begin, bytes, generations, changed, cpu));
        StorageTexture::PendingQuery runQuery{begin, begin + bytes, nullptr, nullptr, false};
        if (pending) static_cast<void>(StorageTexture::ScanPending(std::span(&runQuery, 1)));
        const auto stamp = GuestMemory::TrackerGeneration();
        std::size_t index = 0;
        for (auto block = run; block != end; ++block, ++index) {
            if (changed[index] != GuestMemory::BlockUnchanged || runQuery.overlaps) {
                for (const auto& key : block->second.keys) {
                    if (stale.contains(key)) continue;
                    const auto& entry = entries.at(key);
                    const auto range = static_cast<std::size_t>(entry.guestBytes);
                    const auto* image = entry.texture != nullptr ? entry.texture->StorageSource() : nullptr;
                    bool fresh = false;
                    if (everything || (image != nullptr && !image->Cached())) fresh = false;
                    else if (image != nullptr && entry.generation != 0u) fresh = cpu[index] == 0u || GuestMemory::UnchangedSinceCollected(entry.address, range, entry.generation);
                    else if (entry.generation != 0u) fresh = GuestMemory::UnchangedSince(entry.address, range, entry.generation);
                    else fresh = !epochMoved && GuestMemory::NewestStamp(entry.address, range) <= entry.stamp;
                    if (fresh && runQuery.overlaps) {
                        StorageTexture::PendingQuery query{entry.address, entry.address + entry.guestBytes, image, nullptr, false};
                        static_cast<void>(StorageTexture::ScanPending(std::span(&query, 1)));
                        fresh = !query.overlaps;
                    }
                    if (!fresh) stale.insert(key);
                }
            }
            block->second.generation = stamp;
        }
        run = end;
    }
    for (const auto& key : stale) {
        ++totals.checked;
        auto& held = entries.at(key);
        Entry fresh;
        const auto value = lookupKey(build, key, fresh, nullptr, held.texture != nullptr);
        const auto previous = keys.Find(key);
        if (!value.has_value() || fresh.texture == nullptr) {
            ++totals.refreshed;
            release(key, recorder);
            if (value.has_value()) static_cast<void>(insert(key, *value));
            else static_cast<void>(keys.Retain([&](const Key& kept) { return kept != key; }));
            continue;
        }
        const bool replaced = fresh.texture != held.texture;
        if (!previous.has_value() || *previous != *value) static_cast<void>(insert(key, *value));
        if (!replaced) {
            held.generation = fresh.generation;
            held.stamp = fresh.stamp;
            held.used = ++useSerial;
            held.usedPresent = Recorder::Presents();
            continue;
        }
        ++totals.refreshed;
        release(key, recorder);
        hold(key, std::move(fresh));
    }
}

void DeviceImageTable::evict(const Context& build, Recorder& recorder) {
    const auto presents = Recorder::Presents();
    const bool overEntries = entries.size() > budget.entries;
    if (!overEntries && presents < evictionCheck + budget.checkPresents) return;
    evictionCheck = presents;
    std::uint64_t bytes = 0;
    for (const auto& [key, entry] : entries) bytes += entry.guestBytes;
    const auto byteBudget = budget.textureBytes != 0u ? budget.textureBytes : SampledTextureCacheBudget(build) / 2u;
    const bool overBytes = bytes > byteBudget;
    if (!overEntries && !overBytes) return;
    std::vector<std::pair<std::uint64_t, Key>> order;
    order.reserve(entries.size());
    for (const auto& [key, entry] : entries) order.emplace_back(entry.used, key);
    std::ranges::sort(order, [](const auto& left, const auto& right) { return left.first < right.first; });
    const auto entryTarget = static_cast<std::size_t>(budget.entries) / 4u * 3u;
    const auto byteTarget = byteBudget / 4u * 3u;
    std::set<Key> dropped;
    for (const auto& [used, key] : order) {
        const auto& entry = entries.at(key);
        const bool count = overEntries && entries.size() - dropped.size() > entryTarget;
        const bool idle = entry.usedPresent + budget.idlePresents <= presents;
        const bool memory = overBytes && bytes > byteTarget && idle;
        if (!count && !memory) {
            if (!overBytes || bytes <= byteTarget) break;
            continue;
        }
        bytes -= std::min(bytes, entry.guestBytes);
        dropped.insert(key);
    }
    if (dropped.empty()) return;
    for (const auto& key : dropped) release(key, recorder);
    totals.evicted += keys.Retain([&](const Key& key) { return !dropped.contains(key); });
}

void DeviceImageTable::upload(Recorder& recorder) {
    const bool header = uploadedBuckets != keys.Buckets();
    const bool rebuilt = keys.Rebuilt();
    const auto changed = keys.TakeChanged();
    if (!header && !rebuilt && changed.empty()) return;
    const auto commands = recorder.Commands();
    RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const auto updateBuffer = context.Resolved(&DeviceFunctions::cmdUpdateBuffer, "vkCmdUpdateBuffer");
    const auto words = keys.Words();
    std::uint64_t bytes = 0;
    if (rebuilt && keys.Entries() == 0u) {
        context.Resolved(&DeviceFunctions::cmdFillBuffer, "vkCmdFillBuffer")(commands, table->Handle(), BucketsOffset, words.size_bytes(), 0u);
    } else if (rebuilt) {
        const auto size = words.size_bytes();
        auto staging = std::make_shared<Buffer>(context, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging->Bytes().data(), words.data(), size);
        const VkBufferCopy region{0, BucketsOffset, size};
        context.Resolved(&DeviceFunctions::cmdCopyBuffer, "vkCmdCopyBuffer")(commands, staging->Handle(), table->Handle(), 1, &region);
        recorder.Keep(std::move(staging), size);
        bytes += size;
    } else {
        for (std::size_t first = 0; first < changed.size();) {
            auto last = first;
            while (last + 1u < changed.size() && changed[last + 1u] == changed[last] + 1u && static_cast<VkDeviceSize>(last + 2u - first) * Abi::BucketBytes <= UpdateBytes) ++last;
            const auto size = static_cast<VkDeviceSize>(last + 1u - first) * Abi::BucketBytes;
            updateBuffer(commands, table->Handle(), BucketsOffset + static_cast<VkDeviceSize>(changed[first]) * Abi::BucketBytes, size, words.data() + static_cast<std::size_t>(changed[first]) * Abi::BucketWords);
            bytes += size;
            first = last + 1u;
        }
    }
    if (header) {
        std::array<std::uint32_t, Abi::HeaderWords> fields{};
        fields[Abi::HeaderVersion] = Abi::Version;
        fields[Abi::HeaderMask] = keys.Buckets() - 1u;
        fields[Abi::HeaderRingCapacity] = RingCapacity;
        updateBuffer(commands, table->Handle(), 0, sizeof(fields), fields.data());
        uploadedBuckets = keys.Buckets();
        bytes += sizeof(fields);
    }
    RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    ++totals.uploads;
    totals.uploadBytes += bytes;
}

std::uint32_t DeviceImageTable::chunkFor(Recorder& recorder, bool mayWait) {
    const auto serial = recorder.OpenSerial();
    const auto held = std::find_if(chunks.begin(), chunks.end(), [&](const Chunk& chunk) { return chunk.busy && chunk.serial == serial; });
    if (held != chunks.end()) return static_cast<std::uint32_t>(held - chunks.begin());
    const auto free = [&] { return std::find_if(chunks.begin(), chunks.end(), [](const Chunk& chunk) { return !chunk.busy; }); };
    auto found = free();
    if (found == chunks.end()) {
        ++totals.chunkWaits;
        static_cast<void>(recorder.Reap());
        found = free();
    }
    if (found == chunks.end() && mayWait) {
        recorder.Sync();
        found = free();
    }
    if (found == chunks.end()) throw std::runtime_error("AGC device image table: every miss ring chunk belongs to a batch still in flight, and a recording draw or dispatch cannot wait for one");
    const auto index = static_cast<std::uint32_t>(found - chunks.begin());
    *found = {recorder.OpenSerial(), resolveGeneration, true};
    std::memset(ring->Bytes().data() + static_cast<std::size_t>(index * chunkStride), 0, Abi::RingRecordsWord * sizeof(std::uint32_t));
    recorder.OnComplete([token = std::weak_ptr<bool>(alive), this, index] {
        if (token.lock() != nullptr) drain(index);
    });
    return index;
}

void DeviceImageTable::drain(std::uint32_t index) {
    std::lock_guard lock(mutex);
    const auto bytes = ring->Bytes().subspan(static_cast<std::size_t>(index * chunkStride), static_cast<std::size_t>(RingBytes));
    const auto decoded = DecodeDeviceImageRing(std::span<const std::uint32_t>(reinterpret_cast<const std::uint32_t*>(bytes.data()), RingWords), RingCapacity);
    auto& chunk = chunks[index];
    chunk.busy = false;
    const auto taken = chunk.generation;
    const auto programs = std::make_shared<const std::set<ShaderRecompiler::PipelineIdentity>>(std::move(chunk.programs));
    chunk.programs.clear();
    totals.drained += decoded.misses.size();
    totals.dropped += decoded.dropped;
    std::set<ShaderRecompiler::PipelineIdentity> overflowing;
    for (const auto& identity : *programs) {
        const auto from = overflowFrom.find(identity);
        if (from == overflowFrom.end() || taken < from->second) continue;
        if (decoded.dropped == 0u) {
            overflowStreaks.erase(identity);
            continue;
        }
        from->second = resolveGeneration + 1u;
        if (++overflowStreaks[identity] >= OverflowBatches) overflowing.insert(identity);
    }
    if (!overflowing.empty()) fallBack(overflowing, "its misses overflowed the ring of " + std::to_string(RingCapacity) + " records in " + std::to_string(OverflowBatches) + " batches in a row");
    std::optional<std::string> fault;
    for (const auto& miss : decoded.misses) {
        if ((miss.pc & Abi::FaultFlag) == 0u) {
            pending.push_back({miss, programs});
            continue;
        }
        ++totals.faults;
        if (!fault.has_value()) fault = faultText(miss.key, miss.pc & ~Abi::FaultFlag);
    }
    if (!fault.has_value()) return;
    lastFault = *fault;
    throw std::runtime_error(*fault);
}

std::string DeviceImageTable::faultText(const Key& key, std::uint32_t pc) const {
    std::ostringstream message;
    message << "image table: pc 0x" << std::hex << pc << std::dec << " selected the descriptor (words " << hexWords(std::span<const std::uint32_t>(key).first(Abi::DescriptorWords)) << ")";
    if (const auto found = rejections.find(key); found != rejections.end()) {
        message << ": " << ShaderRecompiler::ImageTableAbi::PoisonReasonName(found->second.reason);
        if (!found->second.message.empty()) message << ": " << found->second.message;
    } else if (const auto value = keys.Find(key); value.has_value() && value->fault != 0u && value->fault - 1u < static_cast<std::uint32_t>(Reason::Count)) {
        message << ": " << ShaderRecompiler::ImageTableAbi::PoisonReasonName(static_cast<Reason>(value->fault - 1u));
    } else {
        message << ": the device image table holds no rejection for it";
    }
    return message.str();
}

std::vector<DeviceImageTable::Counts> DeviceImageTable::DeviceTotals() {
    auto& registry = tables();
    std::lock_guard lock(registry.mutex);
    std::vector<Counts> totals;
    for (const auto& [device, table] : registry.tables) {
        if (table != nullptr) totals.push_back(table->Totals());
    }
    return totals;
}

DeviceImageTable::Counts DeviceImageTable::Totals() const {
    std::lock_guard lock(mutex);
    auto counts = totals;
    counts.entries = keys.Entries();
    counts.buckets = keys.Buckets();
    return counts;
}

void DeviceImageTable::report() {
    if (!profiled()) return;
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (lastReport == 0) {
        lastReport = now;
        reported = totals;
        reportedPackets = packetSnapshot();
        return;
    }
    if (now - lastReport < 10'000'000'000ll) return;
    std::string peaks;
    for (std::uint32_t kind = Runtime::FirstImageBinding; kind < totals.peakTextures.size(); ++kind) {
        if (totals.peakTextures[kind] != 0u) peaks += (peaks.empty() ? "" : " ") + std::to_string(kind) + ":" + std::to_string(totals.peakTextures[kind]);
    }
    const auto delta = [&](std::uint64_t Counts::* field) { return static_cast<unsigned long long>(totals.*field - reported.*field); };
    const auto packets = packetSnapshot();
    const auto count = [&](std::size_t index) { return static_cast<unsigned long long>(packets[index] - reportedPackets[index]); };
    const auto perPacket = [&](std::size_t index) { return count(index) != 0u ? static_cast<double>(packets[4u + index] - reportedPackets[4u + index]) / 1000.0 / static_cast<double>(count(index)) : 0.0; };
    std::fprintf(stderr, "[device-images] (10 s): %llu keys expected (%llu inserted before their batch), %llu records drained, %llu distinct misses, %llu dropped, %llu inserted, %llu faults, %llu rejected, %llu unserved, %llu heap full, %llu refreshed (%llu checked, %llu scans), %llu evicted, %u live entries in %u buckets, %llu uploads (%llu bytes), peak textures by kind %s; draws on the device table %llu of %llu (driver CPU %.1f us per packet, %.1f us for the others), dispatches %llu of %llu (%.1f us, %.1f us for the others)\n",
        delta(&Counts::expected), delta(&Counts::expectedInserted), delta(&Counts::drained), delta(&Counts::distinct), delta(&Counts::dropped), delta(&Counts::inserted), delta(&Counts::faults), delta(&Counts::rejected), delta(&Counts::unserved), delta(&Counts::heapFull), delta(&Counts::refreshed), delta(&Counts::checked), delta(&Counts::scans), delta(&Counts::evicted), keys.Entries(), keys.Buckets(), delta(&Counts::uploads), delta(&Counts::uploadBytes), peaks.empty() ? "none" : peaks.c_str(), count(1), count(0) + count(1), perPacket(1), perPacket(0), count(3), count(2) + count(3), perPacket(3), perPacket(2));
    reported = totals;
    reportedPackets = packets;
    lastReport = now;
}

}
