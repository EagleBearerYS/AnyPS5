#include "prx/libSceAgcDriver/Graphics/include/Residency.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace AgcDriver::Graphics {

namespace {

struct PageSpan {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    bool empty = true;
};

PageSpan PagesOf(std::uint64_t address, std::uint64_t bytes) {
    if (bytes == 0) return {};
    return {address >> ResidencyPageShift, (address + (bytes - 1)) >> ResidencyPageShift, false};
}

constexpr std::uint32_t WordBits = 64;

}

ResidencyToken MergeTokens(const ResidencyToken& first, const ResidencyToken& second) {
    ResidencyToken merged{};
    for (std::size_t stream = 0; stream < ResidencyStreams; ++stream) merged[stream] = std::max(first[stream], second[stream]);
    return merged;
}

bool TokenEmpty(const ResidencyToken& token) {
    return std::all_of(token.begin(), token.end(), [](std::uint64_t value) { return value == 0; });
}

ResidencyMask SubpageMask(std::uint64_t page, std::uint64_t address, std::uint64_t bytes) {
    if (bytes == 0) return 0;
    const auto base = page << ResidencyPageShift;
    const auto first = std::max(address, base);
    const auto last = std::min(address + (bytes - 1), base + (ResidencyPageBytes - 1));
    if (first > last) return 0;
    const auto firstSubpage = static_cast<std::uint32_t>((first - base) >> ResidencySubpageShift);
    const auto lastSubpage = static_cast<std::uint32_t>((last - base) >> ResidencySubpageShift);
    const auto count = lastSubpage - firstSubpage + 1;
    const std::uint32_t bits = count >= ResidencySubpages ? 0xffffu : (1u << count) - 1u;
    return static_cast<ResidencyMask>(bits << firstSubpage);
}

ResidencyManager::ResidencyManager(IResidencyDevice& device, const ResidencyPolicy& policy)
    : device(device), chunkSlots(policy.chunkSlots), hotFaults(policy.hotFaults), hotEpochs(policy.hotEpochs) {
    if (chunkSlots == 0 || chunkSlots > (std::numeric_limits<std::uint32_t>::max() >> ResidencyPageShift))
        throw std::invalid_argument("residency: a chunk holds 1 to 65535 pages, not " + std::to_string(chunkSlots));
    if (hotEpochs == 0 || hotEpochs > 64 || hotFaults == 0 || hotFaults > hotEpochs)
        throw std::invalid_argument("residency: a hot page faults in 1 to " + std::to_string(hotEpochs) + " of the last 1 to 64 epochs, not " + std::to_string(hotFaults) + " of " + std::to_string(hotEpochs));
    SetBudget(policy.budgetBytes);
}

ResidencyManager::~ResidencyManager() {
    std::vector<std::uint64_t> guarded;
    for (auto id = oldest; id != None; id = pages[id].newer) {
        if (pages[id].guarded) guarded.push_back(pages[id].index);
    }
    if (!guarded.empty()) device.Unguard(guarded);
    for (std::uint32_t chunk = 0; chunk < chunks.size(); ++chunk) {
        if (chunks[chunk].live) device.ReleaseChunk(chunk);
    }
}

bool ResidencyManager::settled(const ResidencyToken& token) {
    return TokenEmpty(token) || device.Reached(token);
}

void ResidencyManager::waitFor(const ResidencyToken& token) {
    ++counts.waits;
    device.Wait(token);
}

std::optional<std::uint32_t> ResidencyManager::find(std::uint64_t page) const {
    const auto found = resident.find(page);
    if (found == resident.end()) return std::nullopt;
    return found->second;
}

std::vector<std::uint32_t> ResidencyManager::residentIn(std::uint64_t address, std::uint64_t bytes) const {
    std::vector<std::uint32_t> ids;
    const auto span = PagesOf(address, bytes);
    if (span.empty) return ids;
    if (span.last - span.first >= resident.size()) {
        for (const auto& [page, id] : resident) {
            if (page >= span.first && page <= span.last) ids.push_back(id);
        }
        std::sort(ids.begin(), ids.end(), [this](std::uint32_t left, std::uint32_t right) { return pages[left].index < pages[right].index; });
        return ids;
    }
    for (auto page = span.first; page <= span.last; ++page) {
        if (const auto id = find(page)) ids.push_back(*id);
    }
    return ids;
}

std::vector<ResidencyManager::Fill> ResidencyManager::withAliases(std::uint64_t address, std::uint64_t bytes) {
    std::vector<Fill> fills;
    if (bytes == 0) return fills;
    auto spans = device.Aliases(address, bytes);
    spans.push_back({address, bytes});
    std::map<std::uint32_t, ResidencyMask> masks;
    for (const auto& range : spans) {
        for (const auto id : residentIn(range.address, range.bytes)) {
            auto& mask = masks[id];
            mask = static_cast<ResidencyMask>(mask | SubpageMask(pages[id].index, range.address, range.bytes));
        }
    }
    for (const auto& [id, mask] : masks) fills.push_back({id, mask});
    std::sort(fills.begin(), fills.end(), [this](const Fill& left, const Fill& right) { return pages[left.id].index < pages[right.id].index; });
    return fills;
}

void ResidencyManager::unlink(std::uint32_t id) {
    auto& page = pages[id];
    if (page.older != None) pages[page.older].newer = page.newer;
    else oldest = page.newer;
    if (page.newer != None) pages[page.newer].older = page.older;
    else newest = page.older;
    page.older = None;
    page.newer = None;
}

void ResidencyManager::linkNewest(std::uint32_t id) {
    auto& page = pages[id];
    page.older = newest;
    page.newer = None;
    if (newest != None) pages[newest].newer = id;
    else oldest = id;
    newest = id;
}

void ResidencyManager::changed(std::uint64_t page) {
    changes.insert(page);
    ++serial;
}

std::uint32_t ResidencyManager::newPage(std::uint64_t page, MirrorSlot slot, const ResidencyImport& import, bool guarded) {
    std::uint32_t id;
    if (!freePages.empty()) {
        id = freePages.back();
        freePages.pop_back();
    } else {
        id = static_cast<std::uint32_t>(pages.size());
        pages.emplace_back();
    }
    auto& record = pages[id];
    record = Page{};
    record.index = page;
    record.slot = slot;
    record.gpuWritable = import.gpuWritable;
    record.guarded = guarded;
    {
        const std::lock_guard lock(residentMutex);
        resident.emplace(page, id);
    }
    linkNewest(id);
    changed(page);
    return id;
}

void ResidencyManager::releasePage(std::uint32_t id) {
    pages[id] = Page{};
    freePages.push_back(id);
}

void ResidencyManager::forget(std::uint32_t id) {
    const auto page = pages[id].index;
    unlink(id);
    {
        const std::lock_guard lock(residentMutex);
        resident.erase(page);
    }
    if (pages[id].dirty != 0) --dirtyPages;
    writers.erase(page);
    if (const auto key = aliasKeys.find(page); key != aliasKeys.end()) {
        canonical.erase(key->second);
        aliasKeys.erase(key);
    }
    changed(page);
    releasePage(id);
}

std::optional<MirrorSlot> ResidencyManager::takeFree() {
    for (std::uint32_t chunk = 0; chunk < chunks.size(); ++chunk) {
        auto& record = chunks[chunk];
        if (!record.live || record.draining || record.freeSlots == 0) continue;
        for (std::uint32_t word = 0; word < record.freeBits.size(); ++word) {
            if (record.freeBits[word] == 0) continue;
            const auto bit = static_cast<std::uint32_t>(std::countr_zero(record.freeBits[word]));
            record.freeBits[word] &= ~(std::uint64_t{1} << bit);
            --record.freeSlots;
            return MirrorSlot{chunk, word * WordBits + bit};
        }
    }
    return std::nullopt;
}

void ResidencyManager::freeSlot(MirrorSlot slot) {
    auto& record = chunks[slot.chunk];
    record.freeBits[slot.slot / WordBits] |= std::uint64_t{1} << (slot.slot % WordBits);
    ++record.freeSlots;
    if (record.draining && record.freeSlots == chunkSlots) releaseChunk(slot.chunk);
}

void ResidencyManager::retireReached() {
    while (!retiring.empty() && settled(retiring.front().token)) {
        const auto slot = retiring.front().slot;
        retiring.pop_front();
        freeSlot(slot);
    }
}

std::optional<std::uint32_t> ResidencyManager::allocateChunk() {
    if (liveChunks >= maxChunks) return std::nullopt;
    std::uint32_t chunk = 0;
    while (chunk < chunks.size() && chunks[chunk].live) ++chunk;
    const auto address = device.AllocateChunk(chunk, chunkSlots);
    if (chunk == chunks.size()) chunks.emplace_back();
    auto& record = chunks[chunk];
    record.address = address;
    record.freeBits.assign((chunkSlots + WordBits - 1) / WordBits, ~std::uint64_t{0});
    if (chunkSlots % WordBits != 0) record.freeBits.back() = (std::uint64_t{1} << (chunkSlots % WordBits)) - 1;
    record.freeSlots = chunkSlots;
    record.live = true;
    record.draining = false;
    ++liveChunks;
    ++counts.chunksAllocated;
    return chunk;
}

void ResidencyManager::releaseChunk(std::uint32_t chunk) {
    device.ReleaseChunk(chunk);
    auto& record = chunks[chunk];
    record = Chunk{};
    --liveChunks;
    ++counts.chunksReleased;
}

std::optional<std::uint32_t> ResidencyManager::victim(std::uint64_t protectFirst, std::uint64_t protectLast) {
    for (auto id = oldest; id != None; id = pages[id].newer) {
        const auto& page = pages[id];
        if (page.index >= protectFirst && page.index <= protectLast) continue;
        if (!settled(page.pin)) continue;
        return id;
    }
    return std::nullopt;
}

std::optional<MirrorSlot> ResidencyManager::acquireSlot(std::uint64_t protectFirst, std::uint64_t protectLast, WriteBacks& writeBacks) {
    retireReached();
    if (const auto slot = takeFree()) return slot;
    if (allocateChunk()) return takeFree();
    const auto id = victim(protectFirst, protectLast);
    if (!id) return std::nullopt;
    ++counts.evicted;
    leave(*id, writeBacks);
    retireReached();
    return takeFree();
}

void ResidencyManager::leave(std::uint32_t id, WriteBacks& writeBacks) {
    const auto& page = pages[id];
    if (page.guarded) writeBacks.unguard.push_back(page.index);
    if (page.dirty != 0) {
        appendRuns(page.index, page.slot, page.dirty, writeBacks.copies);
        writeBacks.slots.push_back({page.slot, page.pin});
        writeBacks.pages.push_back(page.index);
    } else {
        retiring.push_back({page.slot, MergeTokens(page.pin, device.Newest())});
    }
    forget(id);
}

void ResidencyManager::recordWriteBacks(WriteBacks& writeBacks) {
    if (!writeBacks.unguard.empty()) device.Unguard(writeBacks.unguard);
    if (writeBacks.copies.empty()) {
        writeBacks = WriteBacks{};
        return;
    }
    const auto token = device.WriteBack(writeBacks.copies);
    for (const auto& copy : writeBacks.copies) counts.writtenBackBytes += copy.bytes;
    const auto newestToken = device.Newest();
    for (const auto& entry : writeBacks.slots) retiring.push_back({entry.slot, MergeTokens(MergeTokens(entry.token, token), newestToken)});
    for (const auto page : writeBacks.pages) {
        auto& pending = writingBack[page];
        pending = MergeTokens(pending, token);
    }
    writeBacks = WriteBacks{};
}

void ResidencyManager::appendRuns(std::uint64_t page, MirrorSlot slot, ResidencyMask mask, std::vector<ResidencyCopy>& copies) const {
    std::uint32_t bits = mask;
    while (bits != 0) {
        const auto first = static_cast<std::uint32_t>(std::countr_zero(bits));
        const auto run = static_cast<std::uint32_t>(std::countr_one(bits >> first));
        copies.push_back({page, slot, first << ResidencySubpageShift, run << ResidencySubpageShift});
        bits &= run >= 32 ? 0u : ~(((1u << run) - 1u) << first);
    }
}

void ResidencyManager::copyIn(std::span<const Fill> fills) {
    std::vector<ResidencyCopy> copies;
    for (const auto& fill : fills) appendRuns(pages[fill.id].index, pages[fill.id].slot, fill.mask, copies);
    if (copies.empty()) return;
    const auto generation = device.Generation();
    const auto copied = device.CopyIn(copies);
    for (const auto& copy : copies) counts.copiedInBytes += copy.bytes;
    for (const auto& fill : fills) {
        auto& page = pages[fill.id];
        page.pin = MergeTokens(page.pin, copied);
        if ((fill.mask | page.dirty) == ResidencyWholePage) page.generation = generation;
    }
}

void ResidencyManager::waitWriters(std::span<const Fill> fills) {
    ResidencyToken pending{};
    for (const auto& fill : fills) {
        const auto writer = writers.find(pages[fill.id].index);
        if (writer != writers.end() && !settled(writer->second)) pending = MergeTokens(pending, writer->second);
    }
    if (!TokenEmpty(pending)) waitFor(pending);
}

void ResidencyManager::refresh(std::span<const Fill> fills) {
    waitWriters(fills);
    std::vector<Fill> needed;
    for (const auto& fill : fills) {
        const auto& page = pages[fill.id];
        if ((fill.mask & page.dirty) != 0) ++counts.conflicts;
        const auto mask = static_cast<ResidencyMask>(fill.mask & ~page.dirty);
        if (mask == 0) continue;
        needed.push_back({fill.id, mask});
        ++counts.refreshed;
    }
    copyIn(needed);
}

void ResidencyManager::applyDrops() {
    std::vector<ResidencyRange> queue;
    {
        const std::lock_guard lock(queueMutex);
        queue.swap(dropQueue);
    }
    for (const auto& range : queue) {
        for (const auto id : residentIn(range.address, range.bytes)) {
            const auto page = pages[id].index;
            ++counts.dropped;
            retiring.push_back({pages[id].slot, MergeTokens(pages[id].pin, device.Newest())});
            writingBack.erase(page);
            forget(id);
        }
        const auto span = PagesOf(range.address, range.bytes);
        if (span.empty) continue;
        std::erase_if(writingBack, [&](const auto& entry) { return entry.first >= span.first && entry.first <= span.last; });
    }
}

void ResidencyManager::applyQueued() {
    applyDrops();
    std::vector<std::uint64_t> written;
    {
        const std::lock_guard lock(queueMutex);
        written.swap(cpuQueue);
    }
    if (!written.empty()) applyFaults(std::move(written));
}

void ResidencyManager::applyFaults(std::vector<std::uint64_t> written) {
    std::sort(written.begin(), written.end());
    written.erase(std::unique(written.begin(), written.end()), written.end());
    const auto window = hotEpochs >= 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << hotEpochs) - 1;
    std::vector<std::uint32_t> leaving;
    std::vector<Fill> fills;
    for (const auto index : written) {
        const auto id = find(index);
        if (!id) continue;
        auto& page = pages[*id];
        ++counts.faults;
        const auto distance = epoch - page.faultEpoch;
        page.faultHistory = (distance >= 64 ? 0 : page.faultHistory << distance) | 1u;
        page.faultEpoch = epoch;
        if (static_cast<std::uint32_t>(std::popcount(page.faultHistory & window)) >= hotFaults) {
            ++counts.hot;
            hot[index] = {0, epoch};
            leaving.push_back(*id);
            continue;
        }
        if (page.guarded && !device.Guard(index)) {
            ++counts.unguardable;
            page.guarded = false;
            leaving.push_back(*id);
            continue;
        }
        fills.push_back({*id, ResidencyWholePage});
    }
    if (!leaving.empty()) {
        demote(leaving);
        const auto generation = device.Generation();
        for (auto& [index, cooling] : hot) {
            if (cooling.epoch == epoch && cooling.generation == 0) cooling.generation = generation;
        }
    }
    refresh(fills);
}

void ResidencyManager::coolHot(std::uint64_t first, std::span<const std::uint8_t> written) {
    const auto generation = device.Generation();
    for (std::size_t k = 0; k < written.size(); ++k) {
        const auto entry = hot.find(first + k);
        if (entry == hot.end()) continue;
        if (written[k] != 0) {
            entry->second = {generation, epoch};
            continue;
        }
        if (epoch - entry->second.epoch < hotEpochs) continue;
        hot.erase(entry);
        ++counts.cooled;
    }
}

void ResidencyManager::Prepare() {
    ++epoch;
    applyQueued();
    retireReached();
}

void ResidencyManager::SetBudget(std::uint64_t bytes) {
    applyQueued();
    const auto chunkBytes = std::uint64_t{chunkSlots} << ResidencyPageShift;
    maxChunks = static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes / chunkBytes, std::numeric_limits<std::uint32_t>::max()));
    std::uint32_t ordinal = 0;
    for (auto& record : chunks) {
        if (!record.live) continue;
        record.draining = ordinal >= maxChunks;
        ++ordinal;
    }
    for (std::uint32_t chunk = 0; chunk < chunks.size(); ++chunk) {
        if (chunks[chunk].live && chunks[chunk].draining && chunks[chunk].freeSlots == chunkSlots) releaseChunk(chunk);
    }
    evictDraining();
}

void ResidencyManager::evictDraining() {
    if (liveChunks <= maxChunks) return;
    std::vector<std::uint32_t> leaving;
    for (auto id = oldest; id != None; id = pages[id].newer) {
        if (chunks[pages[id].slot.chunk].draining && settled(pages[id].pin)) leaving.push_back(id);
    }
    WriteBacks writeBacks;
    for (const auto id : leaving) {
        ++counts.evicted;
        leave(id, writeBacks);
    }
    recordWriteBacks(writeBacks);
    retireReached();
}

void ResidencyManager::Pin(std::uint64_t address, std::uint64_t bytes, ResidencyAccess access, const ResidencyToken& token) {
    for (const auto id : residentIn(address, bytes)) {
        auto& page = pages[id];
        page.pin = MergeTokens(page.pin, token);
        if (access == ResidencyAccess::Write) {
            auto& writer = writers[page.index];
            writer = MergeTokens(writer, token);
        }
    }
}

void ResidencyManager::Use(std::uint64_t address, std::uint64_t bytes, ResidencyAccess access) {
    applyQueued();
    const auto span = PagesOf(address, bytes);
    if (span.empty) return;
    const auto count = span.last - span.first + 1;
    const auto present = residentIn(address, bytes);
    std::vector<std::uint64_t> generations(count, ResidencyUnchecked);
    bool check = !present.empty();
    for (const auto id : present) generations[pages[id].index - span.first] = pages[id].generation;
    if (!hot.empty()) {
        for (std::uint64_t k = 0; k < count; ++k) {
            const auto entry = hot.find(span.first + k);
            if (entry == hot.end()) continue;
            generations[k] = entry->second.generation;
            check = true;
        }
    }
    if (check) {
        std::vector<std::uint8_t> stale(count, 0);
        device.ChangedPages(span.first, generations, stale);
        std::vector<Fill> fills;
        for (const auto id : present) {
            unlink(id);
            linkNewest(id);
            if (stale[pages[id].index - span.first] != 0) fills.push_back({id, ResidencyWholePage});
            stale[pages[id].index - span.first] = 0;
        }
        refresh(fills);
        if (!hot.empty()) coolHot(span.first, stale);
    }
    if (present.size() < count) admit(span.first, span.last);
    Pin(address, bytes, access, device.Current());
}

void ResidencyManager::admit(std::uint64_t first, std::uint64_t last) {
    WriteBacks writeBacks;
    std::vector<Fill> fills;
    bool exhausted = false;
    for (auto page = first; page <= last; ++page) {
        if (find(page) || hot.contains(page)) continue;
        const auto import = device.Import(page);
        if (!import.whole) {
            ++counts.inadmissible;
            continue;
        }
        const auto key = std::pair{import.section, import.offset};
        if (import.aliased && canonical.contains(key)) {
            ++counts.aliasSkipped;
            continue;
        }
        const auto slot = exhausted ? std::nullopt : acquireSlot(first, last, writeBacks);
        if (!slot) {
            ++counts.deferred;
            exhausted = exhausted || !victim(first, last);
            continue;
        }
        if (import.cpuWritable && !device.Guard(page)) {
            ++counts.unguardable;
            freeSlot(*slot);
            continue;
        }
        fills.push_back({newPage(page, *slot, import, import.cpuWritable), ResidencyWholePage});
        if (import.aliased) {
            canonical.emplace(key, page);
            aliasKeys.emplace(page, key);
        }
        ++counts.admitted;
    }
    recordWriteBacks(writeBacks);
    copyIn(fills);
}

void ResidencyManager::demote(std::span<const std::uint32_t> ids) {
    ResidencyToken pending{};
    for (const auto id : ids) {
        const auto writer = writers.find(pages[id].index);
        if (writer != writers.end() && !settled(writer->second)) pending = MergeTokens(pending, writer->second);
    }
    if (!TokenEmpty(pending)) waitFor(pending);
    WriteBacks writeBacks;
    for (const auto id : ids) {
        ++counts.demoted;
        leave(id, writeBacks);
    }
    recordWriteBacks(writeBacks);
}

void ResidencyManager::Demote(std::uint64_t address, std::uint64_t bytes) {
    applyQueued();
    std::vector<std::uint32_t> ids;
    for (const auto& fill : withAliases(address, bytes)) ids.push_back(fill.id);
    demote(ids);
}

void ResidencyManager::DemoteWritable() {
    applyQueued();
    std::vector<std::uint32_t> ids;
    for (auto id = oldest; id != None; id = pages[id].newer) {
        if (pages[id].gpuWritable) ids.push_back(id);
    }
    demote(ids);
}

void ResidencyManager::DropRange(std::uint64_t address, std::uint64_t bytes) {
    if (bytes == 0) return;
    const std::lock_guard lock(queueMutex);
    dropQueue.push_back({address, bytes});
}

void ResidencyManager::NoteImportWritten(std::uint64_t address, std::uint64_t bytes) {
    applyQueued();
    const auto fills = withAliases(address, bytes);
    waitWriters(fills);
    for (const auto& fill : fills) {
        auto& page = pages[fill.id];
        if (page.dirty == 0) continue;
        page.dirty = static_cast<ResidencyMask>(page.dirty & ~fill.mask);
        if (page.dirty == 0) --dirtyPages;
    }
    refresh(fills);
}

void ResidencyManager::QueueCpuWritten(std::span<const std::uint64_t> written) {
    const std::lock_guard lock(queueMutex);
    cpuQueue.insert(cpuQueue.end(), written.begin(), written.end());
}

void ResidencyManager::NoteGpuWritten(std::uint64_t address, std::uint64_t bytes) {
    for (const auto id : residentIn(address, bytes)) {
        auto& page = pages[id];
        const auto mask = SubpageMask(page.index, address, bytes);
        if (page.dirty == 0 && mask != 0) ++dirtyPages;
        page.dirty = static_cast<ResidencyMask>(page.dirty | mask);
    }
}

void ResidencyManager::FlushForCpuRead(std::uint64_t address, std::uint64_t bytes) {
    applyQueued();
    const auto span = PagesOf(address, bytes);
    if (span.empty) return;
    const auto inSpan = [&](std::uint64_t page) { return page >= span.first && page <= span.last; };
    ResidencyToken pending{};
    for (const auto& [page, token] : writers) {
        if (inSpan(page) && !settled(token)) pending = MergeTokens(pending, token);
    }
    if (!TokenEmpty(pending)) waitFor(pending);
    std::erase_if(writers, [&](const auto& entry) { return inSpan(entry.first) && settled(entry.second); });
    std::vector<ResidencyCopy> copies;
    std::vector<std::uint32_t> written;
    for (const auto id : residentIn(address, bytes)) {
        if (pages[id].dirty == 0) continue;
        appendRuns(pages[id].index, pages[id].slot, pages[id].dirty, copies);
        written.push_back(id);
    }
    ResidencyToken wait{};
    if (!copies.empty()) {
        wait = device.WriteBack(copies);
        for (const auto& copy : copies) counts.writtenBackBytes += copy.bytes;
        for (const auto id : written) {
            auto& page = pages[id];
            page.dirty = 0;
            --dirtyPages;
            page.pin = MergeTokens(page.pin, wait);
        }
    }
    for (const auto& [page, token] : writingBack) {
        if (inSpan(page)) wait = MergeTokens(wait, token);
    }
    if (!settled(wait)) waitFor(wait);
    std::erase_if(writingBack, [&](const auto& entry) { return inSpan(entry.first); });
}

void ResidencyManager::Retire() {
    applyQueued();
    retireReached();
    evictDraining();
    std::erase_if(writers, [&](const auto& entry) { return settled(entry.second); });
    std::erase_if(writingBack, [&](const auto& entry) { return settled(entry.second); });
}

std::optional<MirrorSlot> ResidencyManager::Slot(std::uint64_t page) {
    applyDrops();
    const auto id = find(page);
    if (!id) return std::nullopt;
    return pages[*id].slot;
}

std::optional<std::uint64_t> ResidencyManager::Translate(std::uint64_t page) {
    const auto slot = Slot(page);
    if (!slot) return std::nullopt;
    return chunks[slot->chunk].address + (std::uint64_t{slot->slot} << ResidencyPageShift);
}

ResidencyMask ResidencyManager::Dirty(std::uint64_t page) {
    applyDrops();
    const auto id = find(page);
    return id ? pages[*id].dirty : ResidencyMask{0};
}

bool ResidencyManager::AnyResident(std::uint64_t address, std::uint64_t bytes) {
    const auto span = PagesOf(address, bytes);
    if (span.empty) return false;
    const std::lock_guard lock(residentMutex);
    if (span.last - span.first >= resident.size()) {
        return std::any_of(resident.begin(), resident.end(), [&](const auto& entry) { return entry.first >= span.first && entry.first <= span.last; });
    }
    for (auto page = span.first; page <= span.last; ++page) {
        if (resident.contains(page)) return true;
    }
    return false;
}

std::vector<ResidencyRange> ResidencyManager::ResidentRanges() {
    applyDrops();
    if (rangesSerial == serial) return ranges;
    std::vector<std::uint64_t> indices;
    indices.reserve(resident.size());
    for (const auto& entry : resident) indices.push_back(entry.first);
    std::sort(indices.begin(), indices.end());
    ranges.clear();
    for (const auto page : indices) {
        const auto address = page << ResidencyPageShift;
        if (!ranges.empty() && ranges.back().address + ranges.back().bytes == address) ranges.back().bytes += ResidencyPageBytes;
        else ranges.push_back({address, ResidencyPageBytes});
    }
    rangesSerial = serial;
    return ranges;
}

std::vector<std::uint64_t> ResidencyManager::TakeChanges() {
    applyDrops();
    std::vector<std::uint64_t> taken(changes.begin(), changes.end());
    changes.clear();
    std::sort(taken.begin(), taken.end());
    return taken;
}

std::vector<std::uint32_t> ResidencyManager::Chunks() const {
    std::vector<std::uint32_t> live;
    for (std::uint32_t chunk = 0; chunk < chunks.size(); ++chunk) {
        if (chunks[chunk].live) live.push_back(chunk);
    }
    return live;
}

std::uint64_t ResidencyManager::ChunkAddress(std::uint32_t chunk) const {
    if (chunk >= chunks.size() || !chunks[chunk].live) throw std::out_of_range("residency: chunk " + std::to_string(chunk) + " is not live");
    return chunks[chunk].address;
}

ResidencyCounts ResidencyManager::Counts() const {
    auto result = counts;
    result.chunks = liveChunks;
    result.resident = static_cast<std::uint32_t>(resident.size());
    result.dirty = dirtyPages;
    result.retiring = static_cast<std::uint32_t>(retiring.size());
    result.free = 0;
    for (const auto& record : chunks) {
        if (record.live) result.free += record.freeSlots;
    }
    return result;
}

}
