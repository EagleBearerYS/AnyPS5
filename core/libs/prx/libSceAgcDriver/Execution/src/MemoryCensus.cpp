#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include "prx/libSceAgcDriver/Execution/include/AsyncLog.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/PipelineLibrary.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestHeap.hpp"
#include "prx/libc/include/MallocManagedSize.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <mimalloc-stats.h>
#else
#include <sys/types.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

namespace AgcDriver::MemoryCensus {
namespace {

constexpr std::size_t OwnerCount = static_cast<std::size_t>(Owner::Count);
constexpr std::array<const char*, OwnerCount> OwnerNames{"untagged", "pool slabs", "heap mirrors", "image mirrors", "sampled textures", "storage images", "render targets", "staging", "BDA tables", "LDS pools", "unit shadows", "presentation", "host imports"};
constexpr std::array<const char*, RegionClassCount> RegionNames{"guest arena", "shared views", "images", "other private"};

thread_local Owner currentOwner = Owner::Untagged;

struct Allocation {
    Owner owner = Owner::Untagged;
    std::uint32_t type = 0;
    std::uint64_t bytes = 0;
};

struct PoolSize {
    std::uint64_t sets = 0;
    std::uint64_t descriptors = 0;
};

struct Tally {
    std::mutex mutex;
    PFN_vkGetDeviceProcAddr resolver = nullptr;
    std::map<std::pair<VkDevice, VkDeviceMemory>, Allocation> live;
    std::map<std::pair<VkDevice, VkDescriptorPool>, PoolSize> pools;
    std::array<std::array<std::uint64_t, VK_MAX_MEMORY_TYPES>, OwnerCount> bytes{};
    std::array<std::uint64_t, OwnerCount> allocations{};
    std::uint64_t unmatchedFrees = 0;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    PFN_vkGetPhysicalDeviceMemoryProperties2 properties2 = nullptr;
    VkPhysicalDeviceMemoryProperties memory{};
};

Tally& tally() {
    static auto* value = new Tally();
    return *value;
}

double mib(std::uint64_t bytes) {
    return static_cast<double>(bytes) / 1048576.0;
}

double signedMib(std::int64_t bytes) {
    return static_cast<double>(bytes) / 1048576.0;
}

bool imported(const VkMemoryAllocateInfo* info) {
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next != nullptr; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT || next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR) return true;
#ifdef _WIN32
        if (next->sType == VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR) return true;
#endif
    }
    return false;
}

VKAPI_ATTR VkResult VKAPI_CALL allocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks* callbacks, VkDeviceMemory* memory) {
    auto& state = tally();
    const auto real = reinterpret_cast<PFN_vkAllocateMemory>(state.resolver(device, "vkAllocateMemory"));
    const auto result = real(device, info, callbacks, memory);
    if (result != VK_SUCCESS || info == nullptr || memory == nullptr || *memory == VK_NULL_HANDLE || info->memoryTypeIndex >= VK_MAX_MEMORY_TYPES) return result;
    const Allocation allocation{imported(info) ? Owner::HostImport : currentOwner, info->memoryTypeIndex, info->allocationSize};
    std::lock_guard lock(state.mutex);
    state.live[{device, *memory}] = allocation;
    state.bytes[static_cast<std::size_t>(allocation.owner)][allocation.type] += allocation.bytes;
    ++state.allocations[static_cast<std::size_t>(allocation.owner)];
    return result;
}

VKAPI_ATTR void VKAPI_CALL freeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* callbacks) {
    auto& state = tally();
    if (memory != VK_NULL_HANDLE) {
        std::lock_guard lock(state.mutex);
        if (const auto found = state.live.find({device, memory}); found != state.live.end()) {
            const auto owner = static_cast<std::size_t>(found->second.owner);
            state.bytes[owner][found->second.type] -= found->second.bytes;
            --state.allocations[owner];
            state.live.erase(found);
        } else {
            ++state.unmatchedFrees;
        }
    }
    reinterpret_cast<PFN_vkFreeMemory>(state.resolver(device, "vkFreeMemory"))(device, memory, callbacks);
}

VKAPI_ATTR VkResult VKAPI_CALL createDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* info, const VkAllocationCallbacks* callbacks, VkDescriptorPool* pool) {
    auto& state = tally();
    const auto result = reinterpret_cast<PFN_vkCreateDescriptorPool>(state.resolver(device, "vkCreateDescriptorPool"))(device, info, callbacks, pool);
    if (result != VK_SUCCESS || info == nullptr || pool == nullptr || *pool == VK_NULL_HANDLE) return result;
    PoolSize size{info->maxSets, 0};
    for (std::uint32_t index = 0; index < info->poolSizeCount && info->pPoolSizes != nullptr; ++index) size.descriptors += info->pPoolSizes[index].descriptorCount;
    std::lock_guard lock(state.mutex);
    state.pools[{device, *pool}] = size;
    return result;
}

VKAPI_ATTR void VKAPI_CALL destroyDescriptorPool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks* callbacks) {
    auto& state = tally();
    if (pool != VK_NULL_HANDLE) {
        std::lock_guard lock(state.mutex);
        state.pools.erase({device, pool});
    }
    reinterpret_cast<PFN_vkDestroyDescriptorPool>(state.resolver(device, "vkDestroyDescriptorPool"))(device, pool, callbacks);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name) {
    if (name != nullptr && std::strcmp(name, "vkAllocateMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&allocateMemory);
    if (name != nullptr && std::strcmp(name, "vkFreeMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&freeMemory);
    if (name != nullptr && std::strcmp(name, "vkCreateDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&createDescriptorPool);
    if (name != nullptr && std::strcmp(name, "vkDestroyDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&destroyDescriptorPool);
    return tally().resolver(device, name);
}

struct Walk {
    std::array<std::uint64_t, RegionClassCount> committed{};
    std::array<std::uint64_t, RegionClassCount> resident{};
    std::array<std::uint64_t, RegionClassCount> swapped{};
    std::uint64_t arenaShared = 0;
    std::uint64_t regions = 0;
    bool haveCommitted = false;
    bool haveResident = false;
    double milliseconds = 0.0;
};

#ifdef _WIN32
struct Run {
    std::uint64_t begin;
    std::uint64_t end;
    RegionClass type;
};

Walk walkAddressSpace(std::uint64_t arenaBegin, std::uint64_t arenaEnd) {
    Walk walk;
    walk.haveCommitted = true;
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    std::vector<Run> runs;
    auto address = reinterpret_cast<std::uint64_t>(system.lpMinimumApplicationAddress);
    const auto last = reinterpret_cast<std::uint64_t>(system.lpMaximumApplicationAddress);
    MEMORY_BASIC_INFORMATION region{};
    while (address < last && VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) == sizeof(region)) {
        const auto begin = reinterpret_cast<std::uint64_t>(region.BaseAddress);
        const auto end = begin + region.RegionSize;
        if (end <= address) break;
        ++walk.regions;
        if (region.State == MEM_COMMIT) {
            const auto type = begin < arenaEnd && end > arenaBegin ? RegionClass::Arena : region.Type == MEM_IMAGE ? RegionClass::Image : region.Type == MEM_MAPPED ? RegionClass::Mapped : RegionClass::Private;
            walk.committed[static_cast<std::size_t>(type)] += region.RegionSize;
            if (type == RegionClass::Arena && region.Type == MEM_MAPPED) walk.arenaShared += region.RegionSize;
            if (!runs.empty() && runs.back().type == type && runs.back().end == begin) runs.back().end = end;
            else runs.push_back({begin, end, type});
        }
        address = end;
    }
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (runs.empty() || K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) return walk;
    const std::uint64_t page = system.dwPageSize;
    std::vector<ULONG_PTR> pages(counters.WorkingSetSize / page + 4096u);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (K32QueryWorkingSet(GetCurrentProcess(), pages.data(), static_cast<DWORD>(std::min<std::uint64_t>(pages.size() * sizeof(ULONG_PTR), 0xffffffffu))) != 0) {
            walk.haveResident = true;
            break;
        }
        if (GetLastError() != ERROR_BAD_LENGTH) break;
        pages.resize(static_cast<std::size_t>(pages[0]) + 4096u);
    }
    if (!walk.haveResident) return walk;
    const auto entries = std::min<std::size_t>(static_cast<std::size_t>(pages[0]), pages.size() - 1u);
    for (std::size_t index = 1; index <= entries; ++index) {
        const auto virtualAddress = static_cast<std::uint64_t>(pages[index]) & ~(page - 1u);
        auto run = std::upper_bound(runs.begin(), runs.end(), virtualAddress, [](std::uint64_t value, const Run& candidate) { return value < candidate.begin; });
        if (run == runs.begin()) continue;
        --run;
        if (virtualAddress < run->end) walk.resident[static_cast<std::size_t>(run->type)] += page;
    }
    return walk;
}
#else
Walk walkAddressSpace(std::uint64_t arenaBegin, std::uint64_t arenaEnd) {
    Walk walk;
    std::FILE* file = std::fopen("/proc/self/smaps", "r");
    if (file == nullptr) return walk;
    SmapsParser parser(arenaBegin, arenaEnd);
    char* line = nullptr;
    std::size_t capacity = 0;
    ssize_t length = 0;
    while ((length = getline(&line, &capacity, file)) > 0) parser.Line(std::string_view(line, static_cast<std::size_t>(length)));
    std::free(line);
    std::fclose(file);
    walk.haveResident = true;
    walk.resident = parser.Totals().resident;
    walk.swapped = parser.Totals().swapped;
    walk.regions = parser.Totals().regions;
    return walk;
}

std::string readFile(const char* path) {
    std::string text;
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) return text;
    char buffer[4096];
    std::size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) != 0) text.append(buffer, read);
    std::fclose(file);
    return text;
}
#endif

std::string processLine(std::uint64_t& total) {
    char text[512];
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    const bool process = K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0;
    const bool system = GlobalMemoryStatusEx(&status) != 0;
    total = process ? counters.PrivateUsage : 0;
    std::snprintf(text, sizeof(text), "[mem-census] process: private %.0f MiB, working set %.0f MiB, pagefile usage %.0f MiB; system RAM free %.0f of %.0f MiB, commit %.0f of %.0f MiB\n", mib(total), process ? mib(counters.WorkingSetSize) : 0.0, process ? mib(counters.PagefileUsage) : 0.0, system ? mib(status.ullAvailPhys) : 0.0, system ? mib(status.ullTotalPhys) : 0.0, system ? mib(status.ullTotalPageFile - status.ullAvailPageFile) : 0.0, system ? mib(status.ullTotalPageFile) : 0.0);
#else
    const auto status = ParseStatus(readFile("/proc/self/status"));
    total = status.rss;
    std::snprintf(text, sizeof(text), "[mem-census] process: rss %.0f MiB (anonymous %.0f, file %.0f, shmem %.0f), swap %.0f MiB\n", mib(status.rss), mib(status.anonymous), mib(status.file), mib(status.shmem), mib(status.swap));
#endif
    return text;
}

struct Arena {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::uint64_t direct = 0;
    std::uint64_t flexible = 0;
    std::uint64_t stacks = 0;
    std::uint64_t heapSystem = 0;
    std::uint64_t heapInUse = 0;
};

Arena arenaParts() {
    Arena arena;
    std::uintptr_t base = 0;
    std::size_t bytes = 0;
    GuestArena::GuestArenaRange_nid_postfix(&base, &bytes);
    arena.begin = base;
    arena.end = base + bytes;
    arena.direct = KernelDirectMappedBytes_nid_no_patch();
    arena.flexible = KernelFlexibleMappedBytes_nid_no_patch();
    arena.stacks = KernelGuestStackBytes_nid_no_patch();
    MallocManagedSize managed{};
    managed.size = sizeof(managed);
    if (GuestHeap::GuestHeapStatsFast_nid_postfix(&managed) == 0) {
        arena.heapSystem = managed.currentSystemSize;
        arena.heapInUse = managed.currentInuseSize;
    }
    return arena;
}

std::string addressLine(const Walk& walk, const Arena& arena) {
    const auto at = [](RegionClass type) { return static_cast<std::size_t>(type); };
    std::string line = "[mem-census] address space";
    line += walk.haveCommitted ? " (committed MiB / resident MiB):" : " (resident MiB, +swapped):";
    char text[256];
    for (std::size_t type = 0; type < RegionClassCount; ++type) {
        if (walk.haveCommitted) std::snprintf(text, sizeof(text), "%s %s %.0f / %.0f", type == 0 ? "" : ",", RegionNames[type], mib(walk.committed[type]), mib(walk.resident[type]));
        else std::snprintf(text, sizeof(text), "%s %s %.0f (+%.0f)", type == 0 ? "" : ",", RegionNames[type], mib(walk.resident[type]), mib(walk.swapped[type]));
        line += text;
        if (type == at(RegionClass::Arena)) {
            std::snprintf(text, sizeof(text), " [%.0f in shared views; kernel: direct mapped %.0f, flexible %.0f, guest stacks %.0f; guest heap %.0f system, %.0f in use]", mib(walk.arenaShared), mib(arena.direct), mib(arena.flexible), mib(arena.stacks), mib(arena.heapSystem), mib(arena.heapInUse));
            line += text;
        }
    }
    std::snprintf(text, sizeof(text), "; %llu regions walked in %.1f ms\n", static_cast<unsigned long long>(walk.regions), walk.milliseconds);
    line += text;
    return line;
}

struct DeviceTotals {
    std::uint64_t hostVisible = 0;
    std::uint64_t deviceOnly = 0;
    std::uint64_t imports = 0;
};

std::string deviceLines(DeviceTotals& totals) {
    auto& state = tally();
    std::array<std::array<std::uint64_t, VK_MAX_MEMORY_TYPES>, OwnerCount> bytes{};
    std::array<std::uint64_t, OwnerCount> allocations{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    PFN_vkGetPhysicalDeviceMemoryProperties2 properties2 = nullptr;
    std::uint64_t unmatched = 0;
    std::size_t live = 0;
    PoolSize pools;
    std::size_t poolCount = 0;
    {
        std::lock_guard lock(state.mutex);
        for (const auto& [key, size] : state.pools) {
            pools.sets += size.sets;
            pools.descriptors += size.descriptors;
        }
        poolCount = state.pools.size();
        bytes = state.bytes;
        allocations = state.allocations;
        memory = state.memory;
        physical = state.physical;
        properties2 = state.properties2;
        unmatched = state.unmatchedFrees;
        live = state.live.size();
    }
    const auto hostVisible = [&](std::size_t type) { return type < memory.memoryTypeCount && (memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0; };
    std::string types = "[mem-census] device memory by type (MiB, imports apart):";
    char text[512];
    for (std::size_t type = 0; type < VK_MAX_MEMORY_TYPES; ++type) {
        std::uint64_t sum = 0;
        for (std::size_t owner = 0; owner < OwnerCount; ++owner) {
            if (owner != static_cast<std::size_t>(Owner::HostImport)) sum += bytes[owner][type];
        }
        if (sum == 0) continue;
        const auto flags = type < memory.memoryTypeCount ? memory.memoryTypes[type].propertyFlags : 0u;
        std::snprintf(text, sizeof(text), " type %zu (heap %u%s%s%s) %.1f", type, type < memory.memoryTypeCount ? memory.memoryTypes[type].heapIndex : 0u, (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0 ? " device-local" : "", (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0 ? " host-visible" : "", (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0 ? " cached" : "", mib(sum));
        types += text;
    }
    if (properties2 != nullptr && physical != VK_NULL_HANDLE) {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
        VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, &budget};
        properties2(physical, &properties);
        types += "; VK_EXT_memory_budget usage / budget:";
        for (std::uint32_t heap = 0; heap < properties.memoryProperties.memoryHeapCount; ++heap) {
            std::snprintf(text, sizeof(text), " heap %u%s %.0f / %.0f", heap, (properties.memoryProperties.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0 ? " device-local" : "", mib(budget.heapUsage[heap]), mib(budget.heapBudget[heap]));
            types += text;
        }
    } else {
        types += "; VK_EXT_memory_budget unavailable";
    }
    std::snprintf(text, sizeof(text), "; %zu live allocations, %llu frees of memory allocated before the census\n", live, static_cast<unsigned long long>(unmatched));
    types += text;
    std::string owners = "[mem-census] device memory by owner (MiB host-visible / device-only):";
    for (std::size_t owner = 0; owner < OwnerCount; ++owner) {
        std::uint64_t visible = 0, local = 0;
        for (std::size_t type = 0; type < VK_MAX_MEMORY_TYPES; ++type) (hostVisible(type) ? visible : local) += bytes[owner][type];
        if (owner == static_cast<std::size_t>(Owner::HostImport)) {
            totals.imports = visible + local;
            continue;
        }
        totals.hostVisible += visible;
        totals.deviceOnly += local;
        if (visible == 0 && local == 0) continue;
        std::snprintf(text, sizeof(text), " %s %.1f / %.1f (%llu)", OwnerNames[owner], mib(visible), mib(local), static_cast<unsigned long long>(allocations[owner]));
        owners += text;
    }
    const auto imports = Graphics::HostImportCounters();
    std::snprintf(text, sizeof(text), "; host imports (pin guest pages, no new commit) %llu allocations %.0f MiB, driver records %llu live %.0f MiB; descriptor pools (the Vulkan driver's own memory, outside these figures) %zu live for %llu sets and %llu descriptors\n", static_cast<unsigned long long>(allocations[static_cast<std::size_t>(Owner::HostImport)]), mib(totals.imports), static_cast<unsigned long long>(imports.live), mib(imports.liveBytes), poolCount, static_cast<unsigned long long>(pools.sets), static_cast<unsigned long long>(pools.descriptors));
    owners += text;
    return types + owners;
}

std::string cacheLine(VkDevice device) {
    auto& driver = DriverDetail::Driver::Get();
    const auto draw = driver.DrawCacheSize();
    const auto dispatch = driver.DispatchCacheSize();
    const auto resources = Graphics::SharedResourceCache().Size();
    const auto mirrors = Graphics::MirrorCounters();
    const auto store = Graphics::PipelineStoreTotals();
    const auto libraries = device != VK_NULL_HANDLE ? Graphics::PipelineLibraryCountersOf(device) : Graphics::PipelineLibraryCounters{};
    std::uint64_t parts = 0;
    for (const auto built : libraries.built) parts += built;
    char text[768];
    std::snprintf(text, sizeof(text), "[mem-census] driver: draw cache %zu entries, %.1f MiB of variants; dispatch cache %zu entries, %.1f MiB of variants; resource cache %zu entries; heap mirrors %llu, %.0f MiB of device memory; log queue %.1f MiB; pipelines: store %llu cached (bound %llu), library parts %llu, linked %llu, optimized %llu (APS5_NO_GPL drops the parts, links and optimized relinks for monolithic pipelines)\n", draw.entries, mib(draw.variantBytes), dispatch.entries, mib(dispatch.variantBytes), resources, static_cast<unsigned long long>(mirrors.heapMirrors), mib(mirrors.heapBytes), mib(AsyncLog::Get().QueuedBytes()), static_cast<unsigned long long>(store.cached), static_cast<unsigned long long>(store.bound), static_cast<unsigned long long>(parts), static_cast<unsigned long long>(libraries.linked), static_cast<unsigned long long>(libraries.optimized));
    return text;
}

std::string allocatorLine(std::uint64_t& heap) {
    char text[384];
#ifdef _WIN32
    std::vector<HANDLE> heaps(GetProcessHeaps(0, nullptr) + 16u);
    heaps.resize(GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data()));
    std::uint64_t committed = 0, allocated = 0;
    for (const auto handle : heaps) {
        HEAP_SUMMARY summary{};
        summary.cb = sizeof(summary);
        if (HeapSummary(handle, 0, &summary)) {
            committed += summary.cbCommitted;
            allocated += summary.cbAllocated;
        }
    }
    heap = committed;
    std::uint64_t mimalloc = 0;
    if (const auto module = GetModuleHandleW(L"mimalloc.dll")) {
        if (const auto get = reinterpret_cast<bool (*)(mi_stats_t*)>(reinterpret_cast<void*>(GetProcAddress(module, "mi_stats_get")))) {
            mi_stats_t stats;
            mi_stats_init(&stats);
            if (get(&stats) && stats.committed.current > 0) mimalloc = static_cast<std::uint64_t>(stats.committed.current);
        }
    }
    heap += mimalloc;
    std::snprintf(text, sizeof(text), "[mem-census] allocator: %zu process heaps %.0f MiB committed (%.0f MiB allocated); mimalloc %s %.0f MiB committed\n", heaps.size(), mib(committed), mib(allocated), GetModuleHandleW(L"mimalloc.dll") != nullptr ? "loaded," : "not loaded,", mib(mimalloc));
#elif defined(__GLIBC__)
    const auto info = mallinfo2();
    heap = info.arena + info.hblkhd;
    std::snprintf(text, sizeof(text), "[mem-census] allocator (glibc): arena %.0f MiB, mmapped blocks %.0f MiB, in use %.0f MiB, free %.0f MiB\n", mib(info.arena), mib(info.hblkhd), mib(info.uordblks + info.hblkhd), mib(info.fordblks));
#else
    heap = 0;
    std::snprintf(text, sizeof(text), "[mem-census] allocator: unavailable on this platform\n");
#endif
    return text;
}

void report() {
    const auto started = std::chrono::steady_clock::now();
    std::uint64_t total = 0;
    auto block = processLine(total);
    const auto arena = arenaParts();
    const auto walkStarted = std::chrono::steady_clock::now();
    auto walk = walkAddressSpace(arena.begin, arena.end);
    walk.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - walkStarted).count();
    block += addressLine(walk, arena);
    DeviceTotals device;
    block += deviceLines(device);
    VkDevice handle = VK_NULL_HANDLE;
    {
        std::lock_guard lock(tally().mutex);
        handle = tally().device;
    }
    block += cacheLine(handle);
    std::uint64_t heap = 0;
    block += allocatorLine(heap);
    const auto at = [](RegionClass type) { return static_cast<std::size_t>(type); };
    char text[512];
#ifdef _WIN32
    const auto arenaPrivate = walk.committed[at(RegionClass::Arena)] - std::min(walk.committed[at(RegionClass::Arena)], walk.arenaShared);
    const auto otherPrivate = walk.committed[at(RegionClass::Private)];
    const auto named = Unattributed(otherPrivate, {heap, device.hostVisible});
    std::snprintf(text, sizeof(text), "[mem-census] remainder: private %.0f MiB = guest arena private %.0f + other private %.0f (%+.0f vs the walk); other private = heaps %.0f + host-visible device memory %.0f + unattributed %.0f MiB; census took %.1f ms\n", mib(total), mib(arenaPrivate), mib(otherPrivate), signedMib(Unattributed(total, {arenaPrivate, otherPrivate})), mib(heap), mib(device.hostVisible), signedMib(named), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
#else
    std::uint64_t walked = 0;
    for (const auto resident : walk.resident) walked += resident;
    const auto otherPrivate = walk.resident[at(RegionClass::Private)];
    std::snprintf(text, sizeof(text), "[mem-census] remainder: rss %.0f MiB, walk %.0f MiB (%+.0f); other private %.0f MiB = glibc heap %.0f + unattributed %.0f MiB (device memory %.0f MiB host-visible / %.0f MiB device-only is inside these on a CPU driver); census took %.1f ms\n", mib(total), mib(walked), signedMib(Unattributed(total, {walked})), mib(otherPrivate), mib(heap), signedMib(Unattributed(otherPrivate, {heap})), mib(device.hostVisible), mib(device.deviceOnly), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
#endif
    block += text;
    ProfilePrint_nid_no_patch("%s", block.c_str());
}

}

bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("APS5_MEMORY_CENSUS");
        return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

Scope::Scope(Owner owner) {
    if (!Enabled()) return;
    previous = currentOwner;
    currentOwner = owner;
    active = true;
}

Scope::~Scope() {
    if (active) currentOwner = previous;
}

PFN_vkGetDeviceProcAddr Wrap(PFN_vkGetDeviceProcAddr resolver) {
    if (!Enabled() || resolver == nullptr) return resolver;
    auto& state = tally();
    std::lock_guard lock(state.mutex);
    if (state.resolver == nullptr) state.resolver = resolver;
    return state.resolver == resolver ? &resolve : resolver;
}

void NoteDevice(VkDevice device, VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceMemoryProperties2 properties2, const VkPhysicalDeviceMemoryProperties& memory) {
    if (!Enabled()) return;
    auto& state = tally();
    std::lock_guard lock(state.mutex);
    state.device = device;
    state.physical = physical;
    state.properties2 = properties2;
    state.memory = memory;
}

void Tick() {
    if (!Enabled()) return;
    static std::atomic<std::int64_t> due{0};
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto expected = due.load(std::memory_order_relaxed);
    if (expected == 0) {
        due.compare_exchange_strong(expected, now + 10000);
        return;
    }
    if (now < expected || !due.compare_exchange_strong(expected, now + 10000)) return;
    try {
        report();
    } catch (const std::exception& error) {
        ProfilePrint_nid_no_patch("[mem-census] census failed: %s\n", error.what());
    }
}

}
