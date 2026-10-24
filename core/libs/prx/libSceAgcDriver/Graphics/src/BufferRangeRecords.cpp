#include "prx/libSceAgcDriver/Graphics/include/BufferRangeRecords.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace AgcDriver::Graphics {

namespace {

struct MemoryRecord {
    VkDeviceSize size = 0;
    std::uint32_t type = 0;
    const void* host = nullptr;
    std::uint64_t allocated = 0;
};

struct BufferRecord {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkBufferUsageFlags usage = 0;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    MemoryRecord bound;
    std::uint64_t address = 0;
    std::uint64_t created = 0;
    std::uint64_t memoryFreed = 0;
    std::uint64_t destroyed = 0;
};

struct Records {
    std::mutex mutex;
    std::unordered_map<VkBuffer, BufferRecord> live;
    std::unordered_map<VkDeviceMemory, MemoryRecord> memories;
    std::unordered_multimap<VkDeviceMemory, VkBuffer> bindings;
    std::deque<BufferRecord> freed;
    std::uint64_t dropped = 0;
};

constexpr std::size_t FreedCapacity = std::size_t{1} << 18;
constexpr std::size_t HitLines = 12;

std::atomic<PFN_vkGetDeviceProcAddr> realDeviceProc{nullptr};
std::atomic<PFN_vkCreateBuffer> realCreateBuffer{nullptr};
std::atomic<PFN_vkDestroyBuffer> realDestroyBuffer{nullptr};
std::atomic<PFN_vkAllocateMemory> realAllocateMemory{nullptr};
std::atomic<PFN_vkFreeMemory> realFreeMemory{nullptr};
std::atomic<PFN_vkBindBufferMemory> realBindBufferMemory{nullptr};
std::atomic<PFN_vkBindBufferMemory2> realBindBufferMemory2{nullptr};
std::atomic<PFN_vkBindBufferMemory2> realBindBufferMemory2Khr{nullptr};
std::atomic<PFN_vkGetBufferDeviceAddress> realBufferAddress{nullptr};
std::atomic<PFN_vkGetBufferDeviceAddress> realBufferAddressKhr{nullptr};
std::atomic<PFN_vkGetBufferDeviceAddress> realBufferAddressExt{nullptr};

Records& records() {
    static auto& value = *new Records();
    return value;
}

std::uint64_t Now() {
    static const auto start = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
}

const void* ImportedHost(const VkMemoryAllocateInfo& info) {
    for (auto* next = static_cast<const VkBaseInStructure*>(info.pNext); next != nullptr; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) return reinterpret_cast<const VkImportMemoryHostPointerInfoEXT*>(next)->pHostPointer;
    }
    return nullptr;
}

void Unbind(Records& state, const BufferRecord& record) {
    if (record.memory == VK_NULL_HANDLE) return;
    auto [first, last] = state.bindings.equal_range(record.memory);
    for (auto it = first; it != last; ++it) {
        if (it->second == record.buffer) {
            state.bindings.erase(it);
            return;
        }
    }
}

void Retire(Records& state, const BufferRecord& record) {
    if (record.address == 0) return;
    if (state.freed.size() == FreedCapacity) {
        state.freed.pop_front();
        ++state.dropped;
    }
    state.freed.push_back(record);
}

void Bind(Records& state, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    const auto found = state.live.find(buffer);
    if (found == state.live.end()) return;
    Unbind(state, found->second);
    found->second.memory = memory;
    found->second.offset = offset;
    if (const auto bound = state.memories.find(memory); bound != state.memories.end()) found->second.bound = bound->second;
    state.bindings.emplace(memory, buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL createBuffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkBuffer* buffer) {
    const auto result = realCreateBuffer.load(std::memory_order_acquire)(device, info, allocator, buffer);
    if (result == VK_SUCCESS) {
        BufferRecord record;
        record.buffer = *buffer;
        record.size = info->size;
        record.usage = info->usage;
        record.created = Now();
        auto& state = records();
        std::lock_guard lock(state.mutex);
        state.live[*buffer] = record;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL destroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* allocator) {
    if (buffer != VK_NULL_HANDLE) {
        auto& state = records();
        std::lock_guard lock(state.mutex);
        if (const auto found = state.live.find(buffer); found != state.live.end()) {
            found->second.destroyed = Now();
            Unbind(state, found->second);
            Retire(state, found->second);
            state.live.erase(found);
        }
    }
    realDestroyBuffer.load(std::memory_order_acquire)(device, buffer, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* allocator, VkDeviceMemory* memory) {
    const auto result = realAllocateMemory.load(std::memory_order_acquire)(device, info, allocator, memory);
    if (result == VK_SUCCESS) {
        const MemoryRecord record{info->allocationSize, info->memoryTypeIndex, ImportedHost(*info), Now()};
        auto& state = records();
        std::lock_guard lock(state.mutex);
        state.memories[*memory] = record;
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* allocator) {
    if (memory != VK_NULL_HANDLE) {
        auto& state = records();
        std::lock_guard lock(state.mutex);
        state.memories.erase(memory);
        const auto now = Now();
        auto [first, last] = state.bindings.equal_range(memory);
        for (auto it = first; it != last; ++it) {
            if (const auto found = state.live.find(it->second); found != state.live.end() && found->second.memoryFreed == 0) found->second.memoryFreed = now;
        }
    }
    realFreeMemory.load(std::memory_order_acquire)(device, memory, allocator);
}

VKAPI_ATTR VkResult VKAPI_CALL bindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    const auto result = realBindBufferMemory.load(std::memory_order_acquire)(device, buffer, memory, offset);
    if (result == VK_SUCCESS) {
        auto& state = records();
        std::lock_guard lock(state.mutex);
        Bind(state, buffer, memory, offset);
    }
    return result;
}

VkResult BindAll(std::atomic<PFN_vkBindBufferMemory2>& real, VkDevice device, std::uint32_t count, const VkBindBufferMemoryInfo* infos) {
    const auto result = real.load(std::memory_order_acquire)(device, count, infos);
    if (result == VK_SUCCESS) {
        auto& state = records();
        std::lock_guard lock(state.mutex);
        for (std::uint32_t i = 0; i < count; ++i) Bind(state, infos[i].buffer, infos[i].memory, infos[i].memoryOffset);
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL bindBufferMemory2(VkDevice device, std::uint32_t count, const VkBindBufferMemoryInfo* infos) {
    return BindAll(realBindBufferMemory2, device, count, infos);
}

VKAPI_ATTR VkResult VKAPI_CALL bindBufferMemory2Khr(VkDevice device, std::uint32_t count, const VkBindBufferMemoryInfo* infos) {
    return BindAll(realBindBufferMemory2Khr, device, count, infos);
}

VkDeviceAddress Addressed(std::atomic<PFN_vkGetBufferDeviceAddress>& real, VkDevice device, const VkBufferDeviceAddressInfo* info) {
    const auto address = real.load(std::memory_order_acquire)(device, info);
    if (address != 0) {
        auto& state = records();
        std::lock_guard lock(state.mutex);
        if (const auto found = state.live.find(info->buffer); found != state.live.end()) found->second.address = address;
    }
    return address;
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL bufferAddress(VkDevice device, const VkBufferDeviceAddressInfo* info) {
    return Addressed(realBufferAddress, device, info);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL bufferAddressKhr(VkDevice device, const VkBufferDeviceAddressInfo* info) {
    return Addressed(realBufferAddressKhr, device, info);
}

VKAPI_ATTR VkDeviceAddress VKAPI_CALL bufferAddressExt(VkDevice device, const VkBufferDeviceAddressInfo* info) {
    return Addressed(realBufferAddressExt, device, info);
}

template<typename TFunction>
PFN_vkVoidFunction Hook(std::atomic<TFunction>& real, PFN_vkVoidFunction function, PFN_vkVoidFunction hook) {
    real.store(reinterpret_cast<TFunction>(function), std::memory_order_release);
    return hook;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL recordingDeviceProc(VkDevice device, const char* name) {
    const auto function = realDeviceProc.load(std::memory_order_acquire)(device, name);
    if (function == nullptr || device == VK_NULL_HANDLE) return function;
    if (std::strcmp(name, "vkCreateBuffer") == 0) return Hook(realCreateBuffer, function, reinterpret_cast<PFN_vkVoidFunction>(&createBuffer));
    if (std::strcmp(name, "vkDestroyBuffer") == 0) return Hook(realDestroyBuffer, function, reinterpret_cast<PFN_vkVoidFunction>(&destroyBuffer));
    if (std::strcmp(name, "vkAllocateMemory") == 0) return Hook(realAllocateMemory, function, reinterpret_cast<PFN_vkVoidFunction>(&allocateMemory));
    if (std::strcmp(name, "vkFreeMemory") == 0) return Hook(realFreeMemory, function, reinterpret_cast<PFN_vkVoidFunction>(&freeMemory));
    if (std::strcmp(name, "vkBindBufferMemory") == 0) return Hook(realBindBufferMemory, function, reinterpret_cast<PFN_vkVoidFunction>(&bindBufferMemory));
    if (std::strcmp(name, "vkBindBufferMemory2") == 0) return Hook(realBindBufferMemory2, function, reinterpret_cast<PFN_vkVoidFunction>(&bindBufferMemory2));
    if (std::strcmp(name, "vkBindBufferMemory2KHR") == 0) return Hook(realBindBufferMemory2Khr, function, reinterpret_cast<PFN_vkVoidFunction>(&bindBufferMemory2Khr));
    if (std::strcmp(name, "vkGetBufferDeviceAddress") == 0) return Hook(realBufferAddress, function, reinterpret_cast<PFN_vkVoidFunction>(&bufferAddress));
    if (std::strcmp(name, "vkGetBufferDeviceAddressKHR") == 0) return Hook(realBufferAddressKhr, function, reinterpret_cast<PFN_vkVoidFunction>(&bufferAddressKhr));
    if (std::strcmp(name, "vkGetBufferDeviceAddressEXT") == 0) return Hook(realBufferAddressExt, function, reinterpret_cast<PFN_vkVoidFunction>(&bufferAddressExt));
    return function;
}

std::string Describe(std::uint64_t address, const char* kind, const BufferRecord& record) {
    char line[640];
    std::snprintf(line, sizeof(line),
        "[device-fault] 0x%llx: %s buffer 0x%llx at 0x%llx-0x%llx (+0x%llx), usage 0x%x, created %llu ms, destroyed %llu ms, memory 0x%llx+0x%llx (0x%llx bytes, type %u, host %p, allocated %llu ms, freed %llu ms)",
        static_cast<unsigned long long>(address), kind, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(record.buffer)),
        static_cast<unsigned long long>(record.address), static_cast<unsigned long long>(record.address + record.size), static_cast<unsigned long long>(address - record.address),
        static_cast<unsigned>(record.usage), static_cast<unsigned long long>(record.created), static_cast<unsigned long long>(record.destroyed),
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(record.memory)), static_cast<unsigned long long>(record.offset),
        static_cast<unsigned long long>(record.bound.size), static_cast<unsigned>(record.bound.type), record.bound.host,
        static_cast<unsigned long long>(record.bound.allocated), static_cast<unsigned long long>(record.memoryFreed));
    return line;
}

}

PFN_vkGetDeviceProcAddr BufferRangeDeviceProc(PFN_vkGetDeviceProcAddr real) {
    if (real == nullptr || real == &recordingDeviceProc) return real;
    Now();
    realDeviceProc.store(real, std::memory_order_release);
    return &recordingDeviceProc;
}

std::vector<std::string> BufferRangeReport(std::uint64_t address, std::uint64_t precision) {
    std::vector<std::string> lines;
    const auto span = std::max<std::uint64_t>(precision, 1);
    const auto low = address & ~(span - 1);
    const auto high = low + span;
    const auto holds = [&](const BufferRecord& record) { return record.address != 0 && record.address < high && record.address + record.size > low; };
    auto& state = records();
    std::lock_guard lock(state.mutex);
    const BufferRecord* below = nullptr;
    const BufferRecord* above = nullptr;
    std::size_t liveHits = 0;
    for (const auto& [handle, record] : state.live) {
        if (record.address == 0) continue;
        if (holds(record)) {
            if (liveHits++ < HitLines) lines.push_back(Describe(address, record.memoryFreed != 0 ? "live (memory freed)" : "live", record));
        } else if (record.address + record.size <= low) {
            if (below == nullptr || record.address + record.size > below->address + below->size) below = &record;
        } else if (above == nullptr || record.address < above->address) {
            above = &record;
        }
    }
    std::size_t freedHits = 0;
    for (auto it = state.freed.rbegin(); it != state.freed.rend(); ++it) {
        if (!holds(*it)) continue;
        if (freedHits++ < HitLines) lines.push_back(Describe(address, "destroyed", *it));
    }
    if (below != nullptr) lines.push_back(Describe(address, "nearest live below:", *below));
    if (above != nullptr) lines.push_back(Describe(address, "nearest live above:", *above));
    char line[256];
    std::snprintf(line, sizeof(line), "[device-fault] 0x%llx: %zu live and %zu destroyed buffers hold it; %zu live buffers, %zu destroyed kept, %llu dropped, now %llu ms",
        static_cast<unsigned long long>(address), liveHits, freedHits, state.live.size(), state.freed.size(), static_cast<unsigned long long>(state.dropped), static_cast<unsigned long long>(Now()));
    lines.emplace_back(line);
    return lines;
}

namespace {

struct TableMapNote {
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    std::uint32_t offset = 0;
    std::uint32_t base = 0;
    std::array<std::uint32_t, 4> written{};
    const std::uint32_t* live = nullptr;
    std::uint64_t at = 0;
};

struct TableNotes {
    std::mutex mutex;
    std::uint64_t table = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t ring = 0;
    std::uint64_t ringBytes = 0;
    std::uint32_t mask = 0;
    std::uint32_t capacity = 0;
    std::uint64_t headerAt = 0;
    std::uint64_t headers = 0;
    std::array<TableMapNote, 16> maps{};
    std::size_t next = 0;
    std::uint64_t noted = 0;
};

TableNotes& tableNotes() {
    static auto& value = *new TableNotes();
    return value;
}

}

void NoteDeviceTable(std::uint64_t table, std::uint64_t tableBytes, std::uint64_t ring, std::uint64_t ringBytes) {
    auto& notes = tableNotes();
    {
        std::lock_guard lock(notes.mutex);
        notes.table = table;
        notes.tableBytes = tableBytes;
        notes.ring = ring;
        notes.ringBytes = ringBytes;
    }
    std::fprintf(stderr, "[device-images] diagnostic: key table at 0x%llx-0x%llx, miss ring at 0x%llx-0x%llx, %llu ms\n", static_cast<unsigned long long>(table), static_cast<unsigned long long>(table + tableBytes), static_cast<unsigned long long>(ring), static_cast<unsigned long long>(ring + ringBytes), static_cast<unsigned long long>(Now()));
}

void NoteDeviceTableHeader(std::uint32_t mask, std::uint32_t capacity) {
    auto& notes = tableNotes();
    std::lock_guard lock(notes.mutex);
    notes.mask = mask;
    notes.capacity = capacity;
    notes.headerAt = Now();
    ++notes.headers;
}

void NoteTableMap(std::uint64_t identityHigh, std::uint64_t identityLow, std::uint32_t offset, std::uint32_t base, const std::uint32_t* written, const std::uint32_t* live) {
    auto& notes = tableNotes();
    std::lock_guard lock(notes.mutex);
    auto& note = notes.maps[notes.next];
    notes.next = (notes.next + 1u) % notes.maps.size();
    ++notes.noted;
    note.high = identityHigh;
    note.low = identityLow;
    note.offset = offset;
    note.base = base;
    std::copy(written, written + note.written.size(), note.written.begin());
    note.live = live;
    note.at = Now();
}

std::vector<std::string> DeviceTableReport(std::uint64_t address) {
    std::vector<std::string> lines;
    auto& notes = tableNotes();
    std::lock_guard lock(notes.mutex);
    char line[512];
    std::snprintf(line, sizeof(line), "[device-fault] device table: keys 0x%llx-0x%llx, ring 0x%llx-0x%llx, header mask 0x%x capacity %u (%llu uploads, last %llu ms), %llu maps noted",
        static_cast<unsigned long long>(notes.table), static_cast<unsigned long long>(notes.table + notes.tableBytes), static_cast<unsigned long long>(notes.ring), static_cast<unsigned long long>(notes.ring + notes.ringBytes),
        notes.mask, notes.capacity, static_cast<unsigned long long>(notes.headers), static_cast<unsigned long long>(notes.headerAt), static_cast<unsigned long long>(notes.noted));
    lines.emplace_back(line);
    if (notes.table != 0 && address >= notes.table) {
        std::snprintf(line, sizeof(line), "[device-fault] 0x%llx: key table + 0x%llx (word %llu)", static_cast<unsigned long long>(address), static_cast<unsigned long long>(address - notes.table), static_cast<unsigned long long>((address - notes.table) / 4u));
        lines.emplace_back(line);
    }
    if (notes.ring != 0 && address >= notes.ring) {
        std::snprintf(line, sizeof(line), "[device-fault] 0x%llx: miss ring + 0x%llx (word %llu)", static_cast<unsigned long long>(address), static_cast<unsigned long long>(address - notes.ring), static_cast<unsigned long long>((address - notes.ring) / 4u));
        lines.emplace_back(line);
    }
    const auto count = std::min<std::uint64_t>(notes.noted, notes.maps.size());
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto& note = notes.maps[(notes.next + notes.maps.size() - 1u - index) % notes.maps.size()];
        std::array<std::uint32_t, 4> now{};
        if (note.live != nullptr) std::copy(note.live, note.live + now.size(), now.begin());
        std::snprintf(line, sizeof(line), "[device-fault] map %llu: identity %016llx%016llx at heap +0x%x, element base %u, %llu ms: trailer written %08x %08x %08x %08x, now %08x %08x %08x %08x%s",
            static_cast<unsigned long long>(index), static_cast<unsigned long long>(note.high), static_cast<unsigned long long>(note.low), note.offset, note.base, static_cast<unsigned long long>(note.at),
            note.written[0], note.written[1], note.written[2], note.written[3], now[0], now[1], now[2], now[3], note.written == now ? "" : " (changed)");
        lines.emplace_back(line);
    }
    return lines;
}

}
