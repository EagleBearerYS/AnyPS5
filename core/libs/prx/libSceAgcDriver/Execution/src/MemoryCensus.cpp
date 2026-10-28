#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include "prx/libSceAgcDriver/Execution/include/AsyncLog.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Pipeline.hpp"
#include "prx/libSceAgcDriver/Graphics/include/PipelineLibrary.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestHeap.hpp"
#include "prx/libc/include/MallocManagedSize.hpp"
#include "prx/libkernel/DirectMemory/DirectMemory.hpp"
#include "Recompiler.hpp"
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
#include <unordered_map>
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
#include <unistd.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#endif

namespace AgcDriver::MemoryCensus {
namespace {

constexpr std::size_t OwnerCount = static_cast<std::size_t>(Owner::Count);
constexpr std::array<const char*, OwnerCount> OwnerNames{"untagged", "pool slabs", "heap mirrors", "image mirrors", "sampled textures", "storage images", "render targets", "staging", "BDA tables", "LDS pools", "unit shadows", "presentation", "host imports"};
constexpr std::array<const char*, RegionClassCount> RegionNames{"guest arena", "shared views", "images", "other private"};
constexpr std::array<const char*, PoolKindCount> PoolKindNames{"other", "chain", "dedicated"};
constexpr std::size_t ProbeLimit = 16;
constexpr std::uint64_t ProbeBudget = 1ull << 30;
#ifdef _WIN32
constexpr const char* ProbeMeasureName = "private";
#else
constexpr const char* ProbeMeasureName = "rss";
#endif

thread_local Owner currentOwner = Owner::Untagged;
thread_local PoolKind currentPoolKind = PoolKind::Other;

struct Allocation {
    Owner owner = Owner::Untagged;
    std::uint32_t type = 0;
    std::uint64_t bytes = 0;
};

struct SetRecord {
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    std::uint64_t layoutSerial = 0;
    std::uint32_t variableCount = 0;
    bool variable = false;
};

struct PoolRecord {
    PoolShape shape;
    bool chained = false;
    std::unordered_map<VkDescriptorSet, SetRecord> sets;
};

struct Tally {
    std::mutex mutex;
    PFN_vkGetDeviceProcAddr resolver = nullptr;
    std::map<std::pair<VkDevice, VkDeviceMemory>, Allocation> live;
    std::map<std::pair<VkDevice, VkDescriptorPool>, PoolRecord> pools;
    std::map<std::pair<VkDevice, VkDescriptorSetLayout>, std::uint64_t> layouts;
    std::uint64_t layoutSerial = 0;
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
    PoolRecord record{{currentPoolKind, info->flags, info->maxSets, {}}, info->pNext != nullptr, {}};
    for (std::uint32_t index = 0; index < info->poolSizeCount && info->pPoolSizes != nullptr; ++index) record.shape.sizes.emplace_back(static_cast<std::uint32_t>(info->pPoolSizes[index].type), info->pPoolSizes[index].descriptorCount);
    std::lock_guard lock(state.mutex);
    state.pools[{device, *pool}] = std::move(record);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL resetDescriptorPool(VkDevice device, VkDescriptorPool pool, VkDescriptorPoolResetFlags flags) {
    auto& state = tally();
    const auto result = reinterpret_cast<PFN_vkResetDescriptorPool>(state.resolver(device, "vkResetDescriptorPool"))(device, pool, flags);
    std::lock_guard lock(state.mutex);
    if (const auto found = state.pools.find({device, pool}); found != state.pools.end()) found->second.sets.clear();
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL allocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    auto& state = tally();
    const auto result = reinterpret_cast<PFN_vkAllocateDescriptorSets>(state.resolver(device, "vkAllocateDescriptorSets"))(device, info, sets);
    if (result != VK_SUCCESS || info == nullptr || sets == nullptr || info->pSetLayouts == nullptr) return result;
    const VkDescriptorSetVariableDescriptorCountAllocateInfo* variable = nullptr;
    for (auto* next = static_cast<const VkBaseInStructure*>(info->pNext); next != nullptr; next = next->pNext) {
        if (next->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO) variable = reinterpret_cast<const VkDescriptorSetVariableDescriptorCountAllocateInfo*>(next);
    }
    const bool counted = variable != nullptr && variable->descriptorSetCount == info->descriptorSetCount && variable->pDescriptorCounts != nullptr;
    std::lock_guard lock(state.mutex);
    const auto found = state.pools.find({device, info->descriptorPool});
    if (found == state.pools.end()) return result;
    for (std::uint32_t index = 0; index < info->descriptorSetCount; ++index) {
        const auto layout = state.layouts.find({device, info->pSetLayouts[index]});
        found->second.sets[sets[index]] = {info->pSetLayouts[index], layout != state.layouts.end() ? layout->second : 0u, counted ? variable->pDescriptorCounts[index] : 0u, variable != nullptr};
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL freeDescriptorSets(VkDevice device, VkDescriptorPool pool, std::uint32_t count, const VkDescriptorSet* sets) {
    auto& state = tally();
    const auto result = reinterpret_cast<PFN_vkFreeDescriptorSets>(state.resolver(device, "vkFreeDescriptorSets"))(device, pool, count, sets);
    if (sets == nullptr) return result;
    std::lock_guard lock(state.mutex);
    const auto found = state.pools.find({device, pool});
    if (found == state.pools.end()) return result;
    for (std::uint32_t index = 0; index < count; ++index) found->second.sets.erase(sets[index]);
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL createDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* info, const VkAllocationCallbacks* callbacks, VkDescriptorSetLayout* layout) {
    auto& state = tally();
    const auto result = reinterpret_cast<PFN_vkCreateDescriptorSetLayout>(state.resolver(device, "vkCreateDescriptorSetLayout"))(device, info, callbacks, layout);
    if (result != VK_SUCCESS || layout == nullptr || *layout == VK_NULL_HANDLE) return result;
    std::lock_guard lock(state.mutex);
    state.layouts[{device, *layout}] = ++state.layoutSerial;
    return result;
}

VKAPI_ATTR void VKAPI_CALL destroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout, const VkAllocationCallbacks* callbacks) {
    auto& state = tally();
    if (layout != VK_NULL_HANDLE) {
        std::lock_guard lock(state.mutex);
        state.layouts.erase({device, layout});
    }
    reinterpret_cast<PFN_vkDestroyDescriptorSetLayout>(state.resolver(device, "vkDestroyDescriptorSetLayout"))(device, layout, callbacks);
}

VKAPI_ATTR void VKAPI_CALL destroyDescriptorPool(VkDevice device, VkDescriptorPool pool, const VkAllocationCallbacks* callbacks) {
    auto& state = tally();
    if (pool != VK_NULL_HANDLE) {
        std::lock_guard lock(state.mutex);
        state.pools.erase({device, pool});
    }
    reinterpret_cast<PFN_vkDestroyDescriptorPool>(state.resolver(device, "vkDestroyDescriptorPool"))(device, pool, callbacks);
}

VKAPI_ATTR void VKAPI_CALL destroyDevice(VkDevice device, const VkAllocationCallbacks* callbacks) {
    auto& state = tally();
    const auto real = reinterpret_cast<PFN_vkDestroyDevice>(state.resolver(device, "vkDestroyDevice"));
    {
        std::lock_guard lock(state.mutex);
        for (auto found = state.live.begin(); found != state.live.end();) {
            if (found->first.first != device) {
                ++found;
                continue;
            }
            const auto owner = static_cast<std::size_t>(found->second.owner);
            state.bytes[owner][found->second.type] -= found->second.bytes;
            --state.allocations[owner];
            found = state.live.erase(found);
        }
        std::erase_if(state.pools, [&](const auto& entry) { return entry.first.first == device; });
        std::erase_if(state.layouts, [&](const auto& entry) { return entry.first.first == device; });
        if (state.device == device) state.device = VK_NULL_HANDLE;
    }
    real(device, callbacks);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL resolve(VkDevice device, const char* name) {
    if (name != nullptr && std::strcmp(name, "vkAllocateMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&allocateMemory);
    if (name != nullptr && std::strcmp(name, "vkFreeMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&freeMemory);
    if (name != nullptr && std::strcmp(name, "vkCreateDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&createDescriptorPool);
    if (name != nullptr && std::strcmp(name, "vkDestroyDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&destroyDescriptorPool);
    if (name != nullptr && std::strcmp(name, "vkResetDescriptorPool") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&resetDescriptorPool);
    if (name != nullptr && std::strcmp(name, "vkAllocateDescriptorSets") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&allocateDescriptorSets);
    if (name != nullptr && std::strcmp(name, "vkFreeDescriptorSets") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&freeDescriptorSets);
    if (name != nullptr && std::strcmp(name, "vkCreateDescriptorSetLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&createDescriptorSetLayout);
    if (name != nullptr && std::strcmp(name, "vkDestroyDescriptorSetLayout") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&destroyDescriptorSetLayout);
    if (name != nullptr && std::strcmp(name, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&destroyDevice);
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
    std::uint64_t poolSets = 0;
    std::uint64_t poolDescriptors = 0;
    std::size_t poolCount = 0;
    {
        std::lock_guard lock(state.mutex);
        for (const auto& [key, record] : state.pools) {
            poolSets += record.shape.maxSets;
            poolDescriptors += Descriptors(record.shape);
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
    std::snprintf(text, sizeof(text), "; host imports (pin guest pages, no new commit) %llu allocations %.0f MiB, driver records %llu live %.0f MiB; descriptor pools (the Vulkan driver's own memory, outside these figures) %zu live for %llu sets and %llu descriptors\n", static_cast<unsigned long long>(allocations[static_cast<std::size_t>(Owner::HostImport)]), mib(totals.imports), static_cast<unsigned long long>(imports.live), mib(imports.liveBytes), poolCount, static_cast<unsigned long long>(poolSets), static_cast<unsigned long long>(poolDescriptors));
    owners += text;
    return types + owners;
}

#ifdef _WIN32
std::uint64_t processBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    return K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0 ? counters.PrivateUsage : 0;
}
#else
std::uint64_t processBytes() {
    std::FILE* file = std::fopen("/proc/self/statm", "r");
    if (file == nullptr) return 0;
    unsigned long long size = 0, resident = 0;
    const bool read = std::fscanf(file, "%llu %llu", &size, &resident) == 2;
    std::fclose(file);
    const auto page = sysconf(_SC_PAGESIZE);
    return read && page > 0 ? static_cast<std::uint64_t>(resident) * static_cast<std::uint64_t>(page) : 0;
}
#endif

const char* descriptorTypeName(std::uint32_t type) {
    switch (static_cast<VkDescriptorType>(type)) {
        case VK_DESCRIPTOR_TYPE_SAMPLER: return "sampler";
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return "combined image sampler";
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return "sampled image";
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return "storage image";
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: return "uniform texel buffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return "storage texel buffer";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: return "uniform buffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return "storage buffer";
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC: return "dynamic uniform buffer";
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: return "dynamic storage buffer";
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: return "input attachment";
        default: return "other";
    }
}

std::string shapeText(const PoolShape& shape) {
    std::string text = PoolKindNames[static_cast<std::size_t>(shape.kind)];
    text += " maxSets " + std::to_string(shape.maxSets);
    if ((shape.flags & VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT) != 0) text += " update-after-bind";
    if ((shape.flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT) != 0) text += " free";
    text += " [";
    for (std::size_t index = 0; index < shape.sizes.size(); ++index) text += (index == 0 ? "" : ", ") + std::string(descriptorTypeName(shape.sizes[index].first)) + " " + std::to_string(shape.sizes[index].second);
    return text + "]";
}

std::string poolLines() {
    struct Probe {
        PoolGroup group;
        ProbeMeasure measure;
        std::uint64_t failed = 0;
        bool created = false;
    };
    std::array<std::uint64_t, PoolKindCount> pools{}, maxSets{}, descriptors{}, sets{}, estimate{}, probed{}, skipped{}, skippedPools{}, skippedDescriptors{};
    std::vector<Probe> probes;
    std::uint64_t probeSets = 0, probeFailures = 0, overBudget = 0;
    std::int64_t probeGrowth = 0;
    const auto started = std::chrono::steady_clock::now();
    {
        auto& state = tally();
        std::lock_guard lock(state.mutex);
        std::vector<LivePool> live;
        std::vector<std::pair<VkDevice, const PoolRecord*>> records;
        live.reserve(state.pools.size());
        records.reserve(state.pools.size());
        for (const auto& [key, record] : state.pools) {
            const auto kind = static_cast<std::size_t>(record.shape.kind);
            ++pools[kind];
            maxSets[kind] += record.shape.maxSets;
            descriptors[kind] += Descriptors(record.shape);
            sets[kind] += record.sets.size();
            live.push_back({record.shape, record.sets.size(), !record.chained && state.resolver != nullptr && key.first == state.device});
            records.emplace_back(key.first, &record);
        }
        auto plan = PlanProbes(live, ProbeLimit);
        std::vector<std::pair<VkDevice, VkDescriptorPool>> created;
        const auto probeStart = processBytes();
        for (auto& group : plan.probed) {
            if (Unattributed(processBytes(), {probeStart}) > static_cast<std::int64_t>(ProbeBudget)) {
                ++overBudget;
                plan.skipped.push_back(std::move(group));
                continue;
            }
            const auto [device, record] = records[group.representative];
            const auto create = reinterpret_cast<PFN_vkCreateDescriptorPool>(state.resolver(device, "vkCreateDescriptorPool"));
            const auto allocate = reinterpret_cast<PFN_vkAllocateDescriptorSets>(state.resolver(device, "vkAllocateDescriptorSets"));
            Probe probe{group, {}, 0, false};
            std::vector<VkDescriptorPoolSize> sizes;
            for (const auto& [type, count] : group.shape.sizes) sizes.push_back({static_cast<VkDescriptorType>(type), count});
            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.flags = group.shape.flags;
            info.maxSets = group.shape.maxSets;
            info.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
            info.pPoolSizes = sizes.data();
            const auto before = processBytes();
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (create == nullptr || allocate == nullptr || create(device, &info, nullptr, &pool) != VK_SUCCESS) {
                ++probe.failed;
                ++probeFailures;
                probes.push_back(std::move(probe));
                continue;
            }
            created.emplace_back(device, pool);
            const auto middle = processBytes();
            for (const auto& [set, held] : record->sets) {
                const auto layout = state.layouts.find({device, held.layout});
                VkDescriptorSetVariableDescriptorCountAllocateInfo variable{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
                variable.descriptorSetCount = 1;
                variable.pDescriptorCounts = &held.variableCount;
                VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                allocation.pNext = held.variable ? &variable : nullptr;
                allocation.descriptorPool = pool;
                allocation.descriptorSetCount = 1;
                allocation.pSetLayouts = &held.layout;
                VkDescriptorSet probeSet = VK_NULL_HANDLE;
                if (layout != state.layouts.end() && layout->second == held.layoutSerial && allocate(device, &allocation, &probeSet) == VK_SUCCESS) {
                    ++probe.measure.probedSets;
                } else {
                    ++probe.failed;
                }
            }
            const auto after = processBytes();
            probe.measure.pool = Unattributed(middle, {before});
            probe.measure.sets = Unattributed(after, {middle});
            probe.created = true;
            const auto kind = static_cast<std::size_t>(group.shape.kind);
            estimate[kind] += ProbeEstimate(group, probe.measure);
            ++probed[kind];
            probeSets += probe.measure.probedSets;
            probeFailures += probe.failed;
            probes.push_back(std::move(probe));
        }
        probeGrowth = Unattributed(processBytes(), {probeStart});
        for (const auto& [device, pool] : created) {
            if (const auto destroy = reinterpret_cast<PFN_vkDestroyDescriptorPool>(state.resolver(device, "vkDestroyDescriptorPool"))) destroy(device, pool, nullptr);
        }
        for (const auto& group : plan.skipped) {
            const auto kind = static_cast<std::size_t>(group.shape.kind);
            ++skipped[kind];
            skippedPools[kind] += group.pools;
            skippedDescriptors[kind] += Descriptors(group.shape) * group.pools;
        }
    }
    const auto milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::string line = "[mem-census] descriptor pools (the Vulkan driver's own memory: ";
    line += ProbeMeasureName;
    line += " bytes of one probe pool per create-info, created under the census lock with the sets of its fullest live pool, scaled by live pools and sets):";
    char text[512];
    constexpr std::array<PoolKind, PoolKindCount> PrintOrder{PoolKind::Chain, PoolKind::Dedicated, PoolKind::Other};
    for (const auto printed : PrintOrder) {
        const auto kind = static_cast<std::size_t>(printed);
        std::snprintf(text, sizeof(text), "%s %s %llu live, %llu sets and %llu descriptors requested, %llu sets live, est %.1f MiB (%llu create-infos probed, %llu skipped: %llu pools, %llu descriptors)", printed == PrintOrder.front() ? "" : ";", PoolKindNames[kind], static_cast<unsigned long long>(pools[kind]), static_cast<unsigned long long>(maxSets[kind]), static_cast<unsigned long long>(descriptors[kind]), static_cast<unsigned long long>(sets[kind]), mib(estimate[kind]), static_cast<unsigned long long>(probed[kind]), static_cast<unsigned long long>(skipped[kind]), static_cast<unsigned long long>(skippedPools[kind]), static_cast<unsigned long long>(skippedDescriptors[kind]));
        line += text;
    }
    std::snprintf(text, sizeof(text), "; probe: %zu create-infos (limit %zu, %llu over the %.0f MiB budget), %llu sets allocated, %llu failures, %s %+.1f MiB while the probe pools lived, %.1f ms\n", probes.size(), ProbeLimit, static_cast<unsigned long long>(overBudget), mib(ProbeBudget), static_cast<unsigned long long>(probeSets), static_cast<unsigned long long>(probeFailures), ProbeMeasureName, signedMib(probeGrowth), milliseconds);
    line += text;
    std::string detail = "[mem-census] descriptor pool probes (create-info: live pools and sets; measured pool + sets of the fullest live pool; estimate):";
    for (std::size_t index = 0; index < probes.size(); ++index) {
        const auto& probe = probes[index];
        detail += index == 0 ? " " : "; ";
        detail += shapeText(probe.group.shape);
        if (!probe.created) {
            std::snprintf(text, sizeof(text), ": %llu pools %llu sets, probe pool not created", static_cast<unsigned long long>(probe.group.pools), static_cast<unsigned long long>(probe.group.sets));
        } else {
            std::snprintf(text, sizeof(text), ": %llu pools %llu sets; %+.1f KiB + %llu sets %+.1f KiB (%llu not allocated); est %.1f MiB", static_cast<unsigned long long>(probe.group.pools), static_cast<unsigned long long>(probe.group.sets), static_cast<double>(probe.measure.pool) / 1024.0, static_cast<unsigned long long>(probe.measure.probedSets), static_cast<double>(probe.measure.sets) / 1024.0, static_cast<unsigned long long>(probe.failed), mib(ProbeEstimate(probe.group, probe.measure)));
        }
        detail += text;
    }
    if (probes.empty()) detail += " none";
    return line + detail + "\n";
}

std::string cacheLine(VkDevice device) {
    auto& driver = DriverDetail::Driver::Get();
    const auto draw = driver.DrawCacheSize();
    const auto dispatch = driver.DispatchCacheSize();
    const auto relocations = driver.DrawRelocationFootprint();
    Graphics::ResourceCache::Footprint resources;
    {
        std::lock_guard gpu(GuestMemory::GpuMutex());
        resources = Graphics::SharedResourceCache().Measure();
    }
    const auto sources = ShaderRecompiler::SourceFootprint();
    const auto mirrors = Graphics::MirrorCounters();
    const auto store = Graphics::PipelineStoreTotals();
    const auto libraries = device != VK_NULL_HANDLE ? Graphics::PipelineLibraryCountersOf(device) : Graphics::PipelineLibraryCounters{};
    std::uint64_t parts = 0;
    for (const auto built : libraries.built) parts += built;
    const auto count = [](std::uint64_t value) { return static_cast<unsigned long long>(value); };
    char text[1024];
    std::snprintf(text, sizeof(text), "[mem-census] driver: draw cache %zu entries, %.1f MiB of variants; dispatch cache %zu entries, %.1f MiB of variants; resource cache %zu entries (%.1f MiB of keys and index, %.1f MiB of objects, %llu table elements in %.1f MiB of table structures); heap mirrors %llu, %.0f MiB of device memory; log queue %.1f MiB; pipelines: store %llu cached (bound %llu), library parts %llu, linked %llu, optimized %llu (APS5_NO_GPL drops the parts, links and optimized relinks for monolithic pipelines)", draw.entries, mib(draw.variantBytes), dispatch.entries, mib(dispatch.variantBytes), resources.entries, mib(resources.indexBytes), mib(resources.objectBytes), count(resources.tableElements), mib(resources.tableBytes), count(mirrors.heapMirrors), mib(mirrors.heapBytes), mib(AsyncLog::Get().QueuedBytes()), count(store.cached), count(store.bound), count(parts), count(libraries.linked), count(libraries.optimized));
    std::string line = text;
    std::snprintf(text, sizeof(text), "; never erased: draw relocations %zu entries %.1f MiB, relocation trials %zu entries %.1f MiB, draw aliases %zu entries %.1f MiB; recompiler sources %llu keys, %llu sources (%llu busy, not counted), %.1f MiB of table, %.1f MiB of code, %llu plans %.1f MiB, %llu IR programs %.1f MiB, %llu variants with %.1f MiB of SPIR-V on the heap and %.1f MiB file-backed, %llu memo tables %.1f MiB (memo results %.1f MiB)\n", relocations.relocations, mib(relocations.relocationBytes), relocations.trials, mib(relocations.trialBytes), relocations.aliases, mib(relocations.aliasBytes), count(sources.keys), count(sources.sources), count(sources.busy), mib(sources.tableBytes), mib(sources.codeBytes), count(sources.plans), mib(sources.planBytes), count(sources.programs), mib(sources.programBytes), count(sources.variants), mib(sources.spirvHeapBytes), mib(sources.spirvViewBytes), count(sources.memoTables), mib(sources.memoTableBytes), mib(ShaderRecompiler::ResultMemoBytes()));
    return line + text;
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

std::string buildBlock() {
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
    block += poolLines();
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
    return block;
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

PoolScope::PoolScope(PoolKind kind) {
    if (!Enabled()) return;
    previous = currentPoolKind;
    currentPoolKind = kind;
    active = true;
}

PoolScope::~PoolScope() {
    if (active) currentPoolKind = previous;
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
        ProfilePrint_nid_no_patch("%s", buildBlock().c_str());
    } catch (const std::exception& error) {
        ProfilePrint_nid_no_patch("[mem-census] census failed: %s\n", error.what());
    }
}

std::string Block() {
    return Enabled() ? buildBlock() : std::string{};
}

}
