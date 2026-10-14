#include "prx/libSceAgcDriver/Graphics/include/ResidencyGuest.hpp"
#include "prx/libSceAgcDriver/Graphics/include/CommittedImports.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <algorithm>
#include <iterator>
#include <mutex>

namespace AgcDriver::Graphics {

std::optional<ResidencyGuestImport> ResidencyGuest::ImportOf(std::uint64_t page) {
    const auto imported = Graphics::ImportOf(device, page);
    if (!imported) return std::nullopt;
    const auto backing = GuestMemory::Backing(page);
    ResidencyGuestImport result;
    result.attributes.whole = imported->bytes == ResidencyPageBytes && backing.uniform;
    result.attributes.cpuWritable = imported->cpuWritable;
    result.attributes.gpuWritable = true;
    result.attributes.aliased = backing.aliased;
    result.attributes.section = backing.section;
    result.attributes.offset = backing.offset;
    result.buffer = imported->buffer;
    result.offset = imported->offset;
    result.address = imported->address;
    return result;
}

std::vector<ResidencyRange> ResidencyGuest::Aliases(std::uint64_t address, std::uint64_t bytes) {
    std::vector<ResidencyRange> ranges;
    for (const auto& [alias, size] : GuestMemory::Aliases(address, static_cast<std::size_t>(bytes))) ranges.push_back({alias, size});
    return ranges;
}

std::uint64_t ResidencyGuest::Generation() {
    return GuestMemory::TrackerGeneration();
}

void ResidencyGuest::ChangedPages(std::uint64_t firstPage, std::span<const std::uint64_t> generations, std::span<std::uint8_t> changed) {
    Require(changed.size() == generations.size(), "residency: changed pages and generations differ in count");
    if (generations.empty()) return;
    const auto address = firstPage << ResidencyPageShift;
    const auto bytes = static_cast<std::size_t>(generations.size() << ResidencyPageShift);
    GuestMemory::CollectWrites(address, bytes);
    static_cast<void>(GuestMemory::ChangedBlocks(address, bytes, generations, changed));
    for (std::size_t index = 0; index < generations.size(); ++index) {
        if (generations[index] == ResidencyUnchecked) changed[index] = GuestMemory::BlockUnchanged;
    }
}

bool ResidencyGuest::Guard(std::uint64_t page) {
    return GuestMemory::GuardWrites(page << ResidencyPageShift, static_cast<std::size_t>(ResidencyPageBytes));
}

void ResidencyGuest::Unguard(std::span<const std::uint64_t> pages) {
    for (const auto page : pages) GuestMemory::UnguardWrites(page << ResidencyPageShift, static_cast<std::size_t>(ResidencyPageBytes));
}

ResidencyPolicy DeviceResidencyPolicy(const Context& context) {
    VkDeviceSize deviceLocal = 0;
    for (std::uint32_t heap = 0; heap < context.memory.memoryHeapCount; ++heap) {
        if ((context.memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) deviceLocal = std::max(deviceLocal, context.memory.memoryHeaps[heap].size);
    }
    Require(deviceLocal != 0, "residency: the device has no device-local heap");
    ResidencyPolicy policy;
    policy.budgetBytes = deviceLocal / 4;
    return policy;
}

namespace {

bool anyResident(std::uint64_t address, std::size_t bytes) {
    auto* driver = ResidencyDriver::Active();
    return driver != nullptr && driver->Manager().AnyResident(address, bytes);
}

void cpuWritten(std::span<const std::uint64_t> pages) {
    if (auto* driver = ResidencyDriver::Active()) driver->Manager().QueueCpuWritten(pages);
}

void dropRange(std::uint64_t address, std::size_t bytes) {
    if (auto* driver = ResidencyDriver::Active()) driver->Manager().DropRange(address, bytes);
}

void writeBack(std::uint64_t address, std::size_t bytes) {
    auto* driver = ResidencyDriver::Active();
    if (driver == nullptr || !driver->Manager().AnyResident(address, bytes)) return;
    const std::lock_guard lock(GuestMemory::GpuMutex());
    driver->Manager().FlushForCpuRead(address, bytes);
}

const GuestMemory::ResidencyHooks residencyHooks{&anyResident, &cpuWritten, &dropRange, &writeBack};

}

void InstallResidencyHooks() {
    GuestMemory::SetResidencyHooks(&residencyHooks);
}

void RemoveResidencyHooks() {
    GuestMemory::SetResidencyHooks(nullptr);
}

void NoteImportWritten(std::uint64_t address, std::uint64_t bytes) {
    if (auto* driver = ResidencyDriver::Active(); driver != nullptr && bytes != 0) driver->Manager().NoteImportWritten(address, bytes);
}

std::vector<ShaderRecompiler::BdaAbi::Range> WithResidentPages(std::span<const ShaderRecompiler::BdaAbi::Range> ranges) {
    std::vector<ShaderRecompiler::BdaAbi::Range> result(ranges.begin(), ranges.end());
    auto* driver = ResidencyDriver::Active();
    if (driver == nullptr) return result;
    auto& manager = driver->Manager();
    std::vector<ShaderRecompiler::BdaAbi::Range> mirrors;
    for (const auto& run : manager.ResidentRanges()) {
        for (auto page = run.address >> ResidencyPageShift; page < (run.address + run.bytes) >> ResidencyPageShift; ++page) {
            const auto mirror = manager.Translate(page);
            if (!mirror) continue;
            mirrors.push_back({page << ResidencyPageShift, (page + 1) << ResidencyPageShift, *mirror, ShaderRecompiler::BdaAbi::Read, 0});
        }
    }
    if (mirrors.empty()) return result;
    std::vector<ShaderRecompiler::BdaAbi::Range> carved;
    carved.reserve(ranges.size() + 2 * mirrors.size());
    auto next = mirrors.begin();
    for (const auto& range : ranges) {
        auto begin = range.begin;
        while (next != mirrors.end() && next->end <= begin) ++next;
        for (auto page = next; page != mirrors.end() && page->begin < range.end; ++page) {
            if (page->begin > begin) carved.push_back({begin, page->begin, range.deviceAddress + (begin - range.begin), range.permissions, range.reserved});
            begin = std::max(begin, page->end);
        }
        if (begin < range.end) carved.push_back({begin, range.end, range.deviceAddress + (begin - range.begin), range.permissions, range.reserved});
    }
    std::vector<ShaderRecompiler::BdaAbi::Range> merged;
    merged.reserve(carved.size() + mirrors.size());
    std::merge(carved.begin(), carved.end(), mirrors.begin(), mirrors.end(), std::back_inserter(merged), [](const auto& left, const auto& right) { return left.begin < right.begin; });
    return merged;
}

}
