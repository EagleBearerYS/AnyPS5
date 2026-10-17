#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BindlessHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "Optimization/ResourceMaterializer.hpp"
#include "IntermediateRepresentation/IrProgram.hpp"
#include "DeviceImageTableAbi.hpp"
#include "ImageTableAbi.hpp"
#include "RuntimeAbi.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace AgcDriver::Graphics {

namespace {

namespace Abi = ShaderRecompiler::DeviceImageTableAbi;
namespace Runtime = ShaderRecompiler::RuntimeAbi;
using Reason = ShaderRecompiler::ImageTableAbi::PoisonReason;

constexpr std::uint32_t RequiredSets = Runtime::DeviceTableSet + 1u;
constexpr std::uint32_t HeapImageKinds = Runtime::FirstStorageImageBinding - Runtime::FirstImageBinding;
constexpr VkShaderStageFlags TableStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
constexpr VkDeviceSize UpdateBytes = 65536u;
constexpr VkDeviceSize TableBytes = static_cast<VkDeviceSize>(DeviceImageTable::TableWords) * sizeof(std::uint32_t);
constexpr VkDeviceSize RingBytes = static_cast<VkDeviceSize>(DeviceImageTable::RingWords) * sizeof(std::uint32_t);
constexpr VkDeviceSize BucketsOffset = static_cast<VkDeviceSize>(Abi::HeaderWords) * sizeof(std::uint32_t);

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

DeviceImageTable::DeviceImageTable(const Context& context, InterfaceLookup lookup) : context(context), lookup(std::move(lookup)) {
    const auto destroyLayout = context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    const auto destroyPool = context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool");
    try {
        const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
            {Runtime::DeviceTableBucketsBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, TableStages, nullptr},
            {Runtime::DeviceTableRingBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, TableStages, nullptr},
        }};
        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &layoutInfo, nullptr, &layout), "vkCreateDescriptorSetLayout");
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
        if (layout != VK_NULL_HANDLE) destroyLayout(context.device, layout, nullptr);
        throw;
    }
}

DeviceImageTable::~DeviceImageTable() {
    if (pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
    if (layout != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, layout, nullptr);
}

void DeviceImageTable::Prepare(const Context& build, Recorder& recorder) {
    std::lock_guard lock(mutex);
    resolve(build, recorder);
    upload(recorder);
    static_cast<void>(chunkFor(recorder, true));
    report();
}

void DeviceImageTable::Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, Recorder& recorder) {
    std::lock_guard lock(mutex);
    const auto chunk = chunkFor(recorder, false);
    const std::array<VkDescriptorSet, 2> sets{BindlessHeap::For(context).HeapSet(), set};
    const auto offset = static_cast<std::uint32_t>(static_cast<VkDeviceSize>(chunk) * chunkStride);
    context.Resolved(&DeviceFunctions::cmdBindDescriptorSets, "vkCmdBindDescriptorSets")(commands, bindPoint, pipelineLayout, Runtime::DeviceHeapSet, static_cast<std::uint32_t>(sets.size()), sets.data(), 1, &offset);
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

void DeviceImageTable::resolve(const Context& build, Recorder& recorder) {
    if (pending.empty()) return;
    const auto misses = DistinctDeviceImageMisses(std::exchange(pending, {}));
    for (const auto& miss : misses) {
        if (keys.Find(miss.key).has_value()) continue;
        ++totals.distinct;
        Entry entry;
        const auto value = lookupKey(build, miss.key, entry);
        if (!value.has_value()) continue;
        if (!insert(miss.key, *value)) {
            if (entry.texture != nullptr) recorder.Keep(std::move(entry.texture));
            continue;
        }
        if (entry.texture == nullptr) continue;
        hold(miss.key, std::move(entry));
        ++totals.inserted;
    }
}

std::optional<DeviceImageValue> DeviceImageTable::lookupKey(const Context& build, const Key& key, Entry& entry) {
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
    try {
        const DeviceTableView view{shapeOf(mode), mode.depthCompare, mode.conversionFormat != ShaderRecompiler::IrBufferFormat::Invalid || mode.depthBits};
        texture = LookupDeviceTableTexture(build, std::span<const std::uint32_t, Abi::DescriptorWords>(key.data(), Abi::DescriptorWords), view, samplerClass);
    } catch (const std::runtime_error& error) {
        return reject(key, Reason::DriverRejected, error.what());
    }
    if (!texture.unservable.empty()) {
        unserved(key, texture.unservable);
        return std::nullopt;
    }
    const auto slot = BindlessHeap::For(build).TextureSlot(kind, texture.texture, texture.view, texture.layout);
    if (!slot.has_value()) {
        heapFull(key, kind);
        return std::nullopt;
    }
    entry = {std::move(texture.texture), kind, texture.address, texture.guestBytes};
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
    entries[key] = std::move(entry);
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
    *found = {recorder.OpenSerial(), true};
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
    chunks[index].busy = false;
    totals.drained += decoded.misses.size();
    totals.dropped += decoded.dropped;
    std::optional<std::string> fault;
    for (const auto& miss : decoded.misses) {
        if ((miss.pc & Abi::FaultFlag) == 0u) {
            pending.push_back(miss);
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
        return;
    }
    if (now - lastReport < 10'000'000'000ll) return;
    std::string peaks;
    for (std::uint32_t kind = Runtime::FirstImageBinding; kind < totals.peakTextures.size(); ++kind) {
        if (totals.peakTextures[kind] != 0u) peaks += (peaks.empty() ? "" : " ") + std::to_string(kind) + ":" + std::to_string(totals.peakTextures[kind]);
    }
    const auto delta = [&](std::uint64_t Counts::* field) { return static_cast<unsigned long long>(totals.*field - reported.*field); };
    std::fprintf(stderr, "[device-images] (10 s): %llu records drained, %llu distinct misses, %llu dropped, %llu inserted, %llu faults, %llu rejected, %llu unserved, %llu heap full, %u live entries in %u buckets, %llu uploads (%llu bytes), peak textures by kind %s\n",
        delta(&Counts::drained), delta(&Counts::distinct), delta(&Counts::dropped), delta(&Counts::inserted), delta(&Counts::faults), delta(&Counts::rejected), delta(&Counts::unserved), delta(&Counts::heapFull), keys.Entries(), keys.Buckets(), delta(&Counts::uploads), delta(&Counts::uploadBytes), peaks.empty() ? "none" : peaks.c_str());
    reported = totals;
    lastReport = now;
}

}
