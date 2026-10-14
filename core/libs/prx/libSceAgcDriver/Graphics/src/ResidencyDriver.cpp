#include "prx/libSceAgcDriver/Graphics/include/ResidencyDriver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <map>
#include <mutex>
#include <set>

namespace AgcDriver::Graphics {

namespace {

std::atomic<ResidencyDriver*> activeDriver{nullptr};

}

ResidencyDriver::ResidencyDriver(const Context& context, Recorder& recorder, IResidencyGuestMemory& guest, const ResidencyPolicy& policy)
    : recorder(recorder), guest(guest), mirrors(context, policy.chunkSlots), manager(*this, policy) {
    activeDriver.store(this);
}

ResidencyDriver::~ResidencyDriver() {
    ResidencyDriver* expected = this;
    activeDriver.compare_exchange_strong(expected, nullptr);
}

ResidencyDriver* ResidencyDriver::Active() {
    return activeDriver.load();
}

void ResidencyDriver::OrderingPoint() {
    orderingPoints.fetch_add(1, std::memory_order_relaxed);
    if (!manager.Advance()) return;
    const std::lock_guard lock(GuestMemory::GpuMutex());
    manager.Apply();
    appliedAtOrderingPoints.fetch_add(1, std::memory_order_relaxed);
}

void ResidencyDriver::BeforeWritingDispatch(std::span<const ResidencyRange> grants, bool writableImports) {
    for (const auto& grant : grants) manager.Demote(grant.address, grant.bytes);
    if (writableImports) manager.DemoteWritable();
}

ResidencyPublication ResidencyDriver::Publish() {
    ResidencyPublication publication;
    const auto changes = manager.TakeChanges();
    publication.serial = manager.Serial();
    publication.changed.reserve(changes.size());
    for (const auto page : changes) publication.changed.push_back({page, manager.Translate(page).value_or(0)});
    return publication;
}

std::vector<Recorder::SectionObject> ResidencyDriver::ChunkObjects(bool written) const {
    std::vector<Recorder::SectionObject> objects;
    for (const auto chunk : manager.Chunks()) objects.push_back({mirrors.Chunk(chunk).get(), written});
    return objects;
}

void ResidencyDriver::KeepChunks() {
    for (const auto chunk : manager.Chunks()) recorder.Keep(mirrors.Chunk(chunk));
}

ResidencyDriverCounts ResidencyDriver::Counts() const {
    return {orderingPoints.load(std::memory_order_relaxed), appliedAtOrderingPoints.load(std::memory_order_relaxed), vanishedCopies};
}

bool ResidencyDriver::Reached(const ResidencyToken& token) {
    return recorder.Reached(token);
}

void ResidencyDriver::Wait(const ResidencyToken& token) {
    recorder.Submit();
    recorder.WaitToken(token);
    recorder.Reap();
    Require(recorder.Reached(token), "residency: a waited token's batches did not finish");
}

ResidencyToken ResidencyDriver::Newest() {
    return recorder.AllToken();
}

ResidencyToken ResidencyDriver::Current() {
    return recorder.CallerToken();
}

ResidencyImport ResidencyDriver::Import(std::uint64_t page) {
    const auto import = guest.ImportOf(page);
    return import ? import->attributes : ResidencyImport{};
}

std::vector<ResidencyRange> ResidencyDriver::Aliases(std::uint64_t address, std::uint64_t bytes) {
    return guest.Aliases(address, bytes);
}

std::uint64_t ResidencyDriver::Generation() {
    return guest.Generation();
}

void ResidencyDriver::ChangedPages(std::uint64_t firstPage, std::span<const std::uint64_t> generations, std::span<std::uint8_t> changed) {
    guest.ChangedPages(firstPage, generations, changed);
}

std::uint64_t ResidencyDriver::AllocateChunk(std::uint32_t chunk, std::uint32_t) {
    return mirrors.Allocate(chunk);
}

void ResidencyDriver::ReleaseChunk(std::uint32_t chunk) {
    mirrors.Release(chunk);
}

ResidencyToken ResidencyDriver::CopyIn(std::span<const ResidencyCopy> copies) {
    return record(copies, true);
}

ResidencyToken ResidencyDriver::WriteBack(std::span<const ResidencyCopy> copies) {
    return record(copies, false);
}

bool ResidencyDriver::Guard(std::uint64_t page) {
    return guest.Guard(page);
}

void ResidencyDriver::Unguard(std::span<const std::uint64_t> pages) {
    guest.Unguard(pages);
}

ResidencyToken ResidencyDriver::record(std::span<const ResidencyCopy> copies, bool intoMirror) {
    std::map<std::uint64_t, ResidencyImportLocation> locations;
    std::vector<ResidencyCopy> located;
    std::set<std::uint32_t> chunks;
    for (const auto& copy : copies) {
        auto found = locations.find(copy.page);
        if (found == locations.end()) {
            const auto import = guest.ImportOf(copy.page);
            found = locations.emplace(copy.page, import ? ResidencyImportLocation{import->buffer, import->offset} : ResidencyImportLocation{}).first;
        }
        if (found->second.buffer == VK_NULL_HANDLE) {
            ++vanishedCopies;
            continue;
        }
        located.push_back(copy);
        chunks.insert(copy.mirror.chunk);
    }
    if (located.empty()) return recorder.CallerToken();
    if (intoMirror) {
        std::vector<Recorder::SectionObject> objects;
        for (const auto chunk : chunks) objects.push_back({mirrors.Chunk(chunk).get(), true});
        recorder.DeclareSection(objects);
    }
    const auto commands = recorder.Commands();
    const ResidencyImportLocator locator = [&](std::uint64_t page) { return locations.at(page); };
    if (intoMirror) mirrors.RecordCopyIn(commands, located, locator);
    else mirrors.RecordWriteBack(commands, located, locator);
    recorder.MarkCovered(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    for (const auto chunk : chunks) recorder.Keep(mirrors.Chunk(chunk));
    return recorder.CallerToken();
}

}
