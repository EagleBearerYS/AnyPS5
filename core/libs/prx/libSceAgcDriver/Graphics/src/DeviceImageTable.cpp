#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "Optimization/DescriptorBindingBuilder.hpp"
#include "Optimization/ResourceMaterializer.hpp"
#include "DeviceImageTableAbi.hpp"
#include "ImageTableAbi.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <utility>

namespace AgcDriver::Graphics {

namespace Abi = ShaderRecompiler::DeviceImageTableAbi;

namespace {

constexpr VkBufferUsageFlags DeviceUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr std::size_t RingWords = Abi::RingRecordsWord + static_cast<std::size_t>(DeviceImageTable::RingCapacity) * Abi::RecordWords;
constexpr std::size_t UpdateBufferBytes = 65536u;

bool Profiled() {
    static const bool profile = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return profile;
}

std::string HexWords(std::span<const std::uint32_t> words) {
    std::string text;
    for (const auto word : words) {
        char hex[12];
        std::snprintf(hex, sizeof(hex), text.empty() ? "%08x" : " %08x", word);
        text += hex;
    }
    return text;
}

bool PointSampled(const ShaderRecompiler::ImageResource& mode) {
    return mode.numericClass == ShaderRecompiler::IrTextureNumericClass::Uint || mode.numericClass == ShaderRecompiler::IrTextureNumericClass::Sint || mode.conversionFormat != ShaderRecompiler::IrBufferFormat::Invalid || mode.depthBits;
}

bool Multisampled(const ShaderRecompiler::ImageResource& mode) {
    return mode.dimension == ShaderRecompiler::RdnaImageDimension::Dim2DMsaa || mode.dimension == ShaderRecompiler::RdnaImageDimension::Dim2DMsaaArray;
}

}

bool UsesDeviceImageTable(const ShaderRecompiler::RecompileResult& program) {
    namespace Map = ShaderRecompiler::ImageTableAbi;
    return std::any_of(program.bindings.begin(), program.bindings.end(), [](const ShaderRecompiler::DescriptorBinding& binding) {
        return binding.role == ShaderRecompiler::DescriptorRole::ImageTableMap && binding.guestDescriptor.size() > Map::HeaderSamplerClasses && binding.guestDescriptor[Map::HeaderSamplerClasses] != 0u;
    });
}

DeviceImageTable::DeviceImageTable(const Context& context) : context(context) {
    Require(context.bufferDeviceAddress, "the device image table needs buffer device addresses");
    header = std::make_shared<Buffer>(context, Abi::HeaderWords * sizeof(std::uint32_t), DeviceUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

DeviceImageTable::~DeviceImageTable() = default;

std::uint64_t DeviceImageTable::HeaderAddress() const {
    return header->DeviceAddress();
}

void DeviceImageTable::BeforeDispatch(const Context& build, Recorder& recorder) {
    std::lock_guard lock(mutex);
    resolve(build, recorder);
    refresh(build, recorder);
    record(recorder);
    report();
}

void DeviceImageTable::resolve(const Context& build, Recorder& recorder) {
    if (pending.empty()) return;
    const auto misses = DistinctDeviceImageMisses(pending);
    pending.clear();
    for (const auto& miss : misses) {
        if (keys.Find(miss.key).has_value()) continue;
        ++totals.distinct;
        Entry entry;
        const auto value = lookup(build, miss.key, miss.pc, entry);
        insert(miss.key, value, recorder);
        if (entry.texture == nullptr) continue;
        entries[miss.key] = std::move(entry);
        ++totals.inserted;
    }
}

DeviceImageValue DeviceImageTable::lookup(const Context& build, const DeviceImageKeys::Key& key, std::uint32_t pc, Entry& entry) {
    using Reason = ShaderRecompiler::ImageTableAbi::PoisonReason;
    const auto id = static_cast<std::uint64_t>(key[Abi::InterfaceWord]) | (static_cast<std::uint64_t>(key[Abi::InterfaceWord + 1u]) << 32u);
    const auto interface = ShaderRecompiler::ResourceMaterializer::FindDeviceImageInterface(id);
    Require(interface != nullptr, "a device image table miss names an image interface no compiled program registered (" + HexWords(std::span<const std::uint32_t>(key).subspan(Abi::InterfaceWord, 2u)) + ")");
    ShaderRecompiler::DescriptorValue descriptor;
    descriptor.dwordCount = Abi::DescriptorWords;
    std::copy_n(key.begin(), Abi::DescriptorWords, descriptor.dwords.begin());
    const auto rejected = [&](Reason reason, const std::string& message) {
        return DeviceImageValue{0u, 0u, reject(key, pc, static_cast<std::uint32_t>(reason), message) + 1u};
    };
    const auto match = ShaderRecompiler::ResourceMaterializer::TryRuntimeImageMode(interface->image, descriptor, interface->modes);
    if (!match.mode.has_value()) return rejected(match.reason, match.detail);
    const auto& mode = interface->modes.at(*match.mode);
    if (Multisampled(mode)) return rejected(Reason::Multisampled, "an image table entry is multisampled");
    if (PointSampled(mode) && (key[Abi::SamplerClassWord] & Abi::SamplerReduction) != 0u) return rejected(Reason::Reduction, "a min or max reduction sampler that filters between texels or mip levels samples an image that needs point filtering (sint, converted or depth-bits format), which is not implemented");
    try {
        const DeviceTableView view{ShaderRecompiler::ImageShapeForResource(mode), mode.depthCompare, mode.conversionFormat != ShaderRecompiler::IrBufferFormat::Invalid || mode.depthBits, ShaderRecompiler::ResourceMaterializer::BindsDecodedCopy(mode, descriptor)};
        auto texture = LookupDeviceTableTexture(build, std::span<const std::uint32_t, 8>(key.data(), Abi::DescriptorWords), view, key[Abi::SamplerClassWord]);
        entry = {std::move(texture.texture), texture.address, texture.guestBytes, texture.generation, texture.snapshot};
        return {texture.slot, *match.mode, 0u};
    } catch (const std::runtime_error& error) {
        return rejected(Reason::DriverRejected, error.what());
    }
}

std::uint32_t DeviceImageTable::reject(const DeviceImageKeys::Key& key, std::uint32_t pc, std::uint32_t reason, const std::string& message) {
    const auto [found, inserted] = rejectionIndex.try_emplace({key, reason, message}, static_cast<std::uint32_t>(rejections.size()));
    if (!inserted) return found->second;
    Require(rejections.size() < Abi::FaultFlag, "the device image table holds more rejections than a fault record can name");
    Rejection rejection;
    std::copy_n(key.begin(), Abi::DescriptorWords, rejection.words.begin());
    rejection.reason = reason;
    rejection.pc = pc;
    rejection.message = message;
    rejections.push_back(std::move(rejection));
    ++totals.rejected;
    const auto name = ShaderRecompiler::ImageTableAbi::PoisonReasonName(static_cast<ShaderRecompiler::ImageTableAbi::PoisonReason>(reason));
    std::fprintf(stderr, "[device-images] the descriptor (words %s) first sampled at pc 0x%x is rejected: %.*s: %s\n", HexWords(std::span<const std::uint32_t>(key).first(Abi::DescriptorWords)).c_str(), pc, static_cast<int>(name.size()), name.data(), message.c_str());
    return found->second;
}

void DeviceImageTable::insert(const DeviceImageKeys::Key& key, const DeviceImageValue& value, Recorder& recorder) {
    if (keys.Insert(key, value) != DeviceImageKeys::Insertion::Full) return;
    ++totals.clears;
    std::fprintf(stderr, "[device-images] the table is full at %u entries in %u buckets: every entry is dropped and resolved again on its next use\n", keys.Entries(), keys.Buckets());
    for (auto& [held, entry] : entries) retire(entry.texture, recorder);
    entries.clear();
    keys.Clear();
    Require(keys.Insert(key, value) != DeviceImageKeys::Insertion::Full, "the device image table refused a key while empty");
}

void DeviceImageTable::retire(const std::shared_ptr<Texture>& texture, Recorder& recorder) {
    if (texture != nullptr) recorder.Keep(texture);
}

void DeviceImageTable::refresh(const Context& build, Recorder& recorder) {
    if (entries.empty()) return;
    const ValidationState state{GuestMemory::ThreadCollectEpoch(), GuestMemory::UnwatchSerial(), DepthSurfacesSerial(), StorageTexture::PendingSerial()};
    if (state.epoch != 0u && validated == state) return;
    const bool everything = !validated.has_value() || validated->unwatch != state.unwatch || validated->depthSurfaces != state.depthSurfaces;
    const bool pendingChanged = !validated.has_value() || validated->pending != state.pending;
    validated = state;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> spans;
    spans.reserve(entries.size());
    for (const auto& [key, entry] : entries) spans.emplace_back(entry.address, entry.address + entry.guestBytes);
    std::ranges::sort(spans);
    std::size_t merged = 0;
    for (const auto& span : spans) {
        if (merged != 0u && span.first <= spans[merged - 1u].second) spans[merged - 1u].second = std::max(spans[merged - 1u].second, span.second);
        else spans[merged++] = span;
    }
    spans.resize(merged);
    for (const auto& [begin, end] : spans) static_cast<void>(GuestMemory::CollectWrites(begin, static_cast<std::size_t>(end - begin)));
    for (auto it = entries.begin(); it != entries.end();) {
        auto& [key, entry] = *it;
        bool stale = everything || !GuestMemory::UnchangedSince(entry.address, static_cast<std::size_t>(entry.guestBytes), entry.generation);
        if (!stale && pendingChanged) {
            const std::array<std::pair<std::uint64_t, std::uint64_t>, 1> range{{{entry.address, entry.address + entry.guestBytes}}};
            stale = !entry.snapshot || StorageTexture::AnyPendingOverlaps(range);
        }
        if (!stale) {
            ++it;
            continue;
        }
        ++totals.refreshed;
        Entry fresh;
        const auto value = lookup(build, key, 0u, fresh);
        Require(keys.Insert(key, value) == DeviceImageKeys::Insertion::Updated, "a refreshed device image entry was not in the table");
        if (fresh.texture != entry.texture) {
            retire(entry.texture, recorder);
            ++totals.replaced;
        }
        if (fresh.texture == nullptr) {
            it = entries.erase(it);
            continue;
        }
        entry = std::move(fresh);
        ++it;
    }
}

void DeviceImageTable::record(Recorder& recorder) {
    const auto serial = recorder.Submissions() + 1u;
    const bool assign = ring == nullptr || ringBatch != serial;
    const bool grown = table == nullptr || keys.Buckets() != tableBuckets;
    const bool rebuilt = grown || keys.Rebuilt();
    const auto changed = keys.TakeChanged();
    if (!assign && !rebuilt && changed.empty()) return;
    const auto commands = recorder.Commands();
    RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const auto words = keys.Words();
    const auto bytes = words.size() * sizeof(std::uint32_t);
    if (grown) {
        if (table != nullptr) recorder.Keep(table);
        table = std::make_shared<Buffer>(context, bytes, DeviceUsage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        tableBuckets = keys.Buckets();
    }
    if (rebuilt) {
        auto staging = std::make_shared<Buffer>(context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        std::memcpy(staging->Bytes().data(), words.data(), bytes);
        const VkBufferCopy region{0, 0, bytes};
        context.Resolved(&DeviceFunctions::cmdCopyBuffer, "vkCmdCopyBuffer")(commands, staging->Handle(), table->Handle(), 1, &region);
        recorder.Keep(std::move(staging));
    } else {
        for (std::size_t first = 0; first < changed.size();) {
            auto last = first;
            while (last + 1u < changed.size() && changed[last + 1u] == changed[last] + 1u && (last + 2u - first) * Abi::BucketBytes <= UpdateBufferBytes) ++last;
            const auto offset = static_cast<VkDeviceSize>(changed[first]) * Abi::BucketBytes;
            context.Resolved(&DeviceFunctions::cmdUpdateBuffer, "vkCmdUpdateBuffer")(commands, table->Handle(), offset, static_cast<VkDeviceSize>(last + 1u - first) * Abi::BucketBytes, words.data() + static_cast<std::size_t>(changed[first]) * Abi::BucketWords);
            first = last + 1u;
        }
    }
    if (assign || grown) {
        if (assign) {
            if (freeRings.empty()) freeRings.push_back(std::make_shared<Buffer>(context, RingWords * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
            ring = std::move(freeRings.back());
            freeRings.pop_back();
            std::memset(ring->Bytes().data(), 0, Abi::RingRecordsWord * sizeof(std::uint32_t));
            ringBatch = serial;
            recorder.OnComplete([this, held = ring] { drain(held); });
        }
        std::array<std::uint32_t, Abi::HeaderWords> fields{};
        const auto tableAddress = table->DeviceAddress();
        const auto ringAddress = ring->DeviceAddress();
        fields[Abi::HeaderVersion] = Abi::Version;
        fields[Abi::HeaderTableLow] = static_cast<std::uint32_t>(tableAddress);
        fields[Abi::HeaderTableHigh] = static_cast<std::uint32_t>(tableAddress >> 32u);
        fields[Abi::HeaderMask] = keys.Buckets() - 1u;
        fields[Abi::HeaderRingLow] = static_cast<std::uint32_t>(ringAddress);
        fields[Abi::HeaderRingHigh] = static_cast<std::uint32_t>(ringAddress >> 32u);
        fields[Abi::HeaderRingCapacity] = RingCapacity;
        context.Resolved(&DeviceFunctions::cmdUpdateBuffer, "vkCmdUpdateBuffer")(commands, header->Handle(), 0, sizeof(fields), fields.data());
    }
    RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void DeviceImageTable::drain(const std::shared_ptr<Buffer>& drained) {
    std::lock_guard lock(mutex);
    const auto bytes = drained->Bytes();
    const auto decoded = DecodeDeviceImageRing(std::span<const std::uint32_t>(reinterpret_cast<const std::uint32_t*>(bytes.data()), RingWords), RingCapacity);
    totals.misses += decoded.written;
    totals.dropped += decoded.dropped;
    pending.insert(pending.end(), decoded.misses.begin(), decoded.misses.end());
    freeRings.push_back(drained);
}

std::optional<DeviceImageTable::Rejection> DeviceImageTable::FindRejection(std::uint32_t index) const {
    std::lock_guard lock(mutex);
    if (index >= rejections.size()) return std::nullopt;
    return rejections[index];
}

DeviceImageTable::Counts DeviceImageTable::Totals() const {
    std::lock_guard lock(mutex);
    auto counts = totals;
    counts.entries = keys.Entries();
    counts.buckets = keys.Buckets();
    return counts;
}

void DeviceImageTable::report() {
    if (!Profiled()) return;
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (lastReport == 0) {
        lastReport = now;
        reported = totals;
        reportedPresents = Recorder::Presents();
        return;
    }
    if (now - lastReport < 10'000'000'000ll) return;
    const auto presents = Recorder::Presents() - reportedPresents;
    const auto misses = totals.misses - reported.misses;
    std::fprintf(stderr, "[device-images] (10 s): %llu misses (%llu distinct, %llu dropped), %.2f per present over %llu presents, %llu inserted, %llu rejected, %llu refreshed (%llu replaced), %u entries in %u buckets, %llu clears\n",
        static_cast<unsigned long long>(misses), static_cast<unsigned long long>(totals.distinct - reported.distinct), static_cast<unsigned long long>(totals.dropped - reported.dropped),
        presents != 0u ? static_cast<double>(misses) / static_cast<double>(presents) : 0.0, static_cast<unsigned long long>(presents),
        static_cast<unsigned long long>(totals.inserted - reported.inserted), static_cast<unsigned long long>(totals.rejected - reported.rejected), static_cast<unsigned long long>(totals.refreshed - reported.refreshed), static_cast<unsigned long long>(totals.replaced - reported.replaced),
        keys.Entries(), keys.Buckets(), static_cast<unsigned long long>(totals.clears - reported.clears));
    reported = totals;
    reportedPresents = Recorder::Presents();
    lastReport = now;
}

}
