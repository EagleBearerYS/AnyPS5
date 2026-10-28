#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYCENSUS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYCENSUS_HPP

#include <vulkan/vulkan.h>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace AgcDriver::MemoryCensus {

enum class Owner : std::uint8_t { Untagged, PoolSlab, HeapMirror, ImageMirror, SampledTexture, StorageImage, RenderTarget, Staging, BdaTable, Lds, UnitShadow, Presentation, HostImport, Count };

enum class PoolKind : std::uint8_t { Other, Chain, Dedicated, Count };
inline constexpr std::size_t PoolKindCount = static_cast<std::size_t>(PoolKind::Count);

bool Enabled();

class Scope {
public:
    explicit Scope(Owner owner);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Owner previous = Owner::Untagged;
    bool active = false;
};

class PoolScope {
public:
    explicit PoolScope(PoolKind kind);
    ~PoolScope();
    PoolScope(const PoolScope&) = delete;
    PoolScope& operator=(const PoolScope&) = delete;

private:
    PoolKind previous = PoolKind::Other;
    bool active = false;
};

PFN_vkGetDeviceProcAddr Wrap(PFN_vkGetDeviceProcAddr resolver);
void NoteDevice(VkDevice device, VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceMemoryProperties2 properties2, const VkPhysicalDeviceMemoryProperties& memory);
void Tick();
std::string Block();

struct PoolShape {
    PoolKind kind = PoolKind::Other;
    std::uint32_t flags = 0;
    std::uint32_t maxSets = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
    auto operator<=>(const PoolShape&) const = default;
};

std::uint64_t Descriptors(const PoolShape& shape);

struct LivePool {
    PoolShape shape;
    std::uint64_t sets = 0;
    bool probeable = true;
};

struct PoolGroup {
    PoolShape shape;
    std::uint64_t pools = 0;
    std::uint64_t sets = 0;
    std::size_t representative = 0;
    bool probeable = true;
};

struct ProbePlan {
    std::vector<PoolGroup> probed;
    std::vector<PoolGroup> skipped;
};

ProbePlan PlanProbes(std::span<const LivePool> pools, std::size_t limit);

struct ProbeMeasure {
    std::int64_t pool = 0;
    std::int64_t sets = 0;
    std::uint64_t probedSets = 0;
};

std::uint64_t ProbeEstimate(const PoolGroup& group, const ProbeMeasure& measure);

enum class RegionClass : std::uint8_t { Arena, Mapped, Image, Private, Count };
inline constexpr std::size_t RegionClassCount = static_cast<std::size_t>(RegionClass::Count);

struct RegionTotals {
    std::array<std::uint64_t, RegionClassCount> resident{};
    std::array<std::uint64_t, RegionClassCount> swapped{};
    std::uint64_t regions = 0;
};

RegionClass ClassifyMapping(std::uint64_t begin, std::uint64_t end, std::string_view permissions, std::uint64_t inode, std::string_view path, std::uint64_t arenaBegin, std::uint64_t arenaEnd);

struct PrivateMapping {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    std::uint64_t resident = 0;
    std::string permissions;
    std::string name;
};

class SmapsParser {
public:
    SmapsParser(std::uint64_t arenaBegin, std::uint64_t arenaEnd, bool recordPrivate = false) : arenaBegin(arenaBegin), arenaEnd(arenaEnd), recordPrivate(recordPrivate) {}
    void Line(std::string_view line);
    const RegionTotals& Totals() const { return totals; }
    std::vector<PrivateMapping>& PrivateMappings() { return privateMappings; }

private:
    std::uint64_t arenaBegin;
    std::uint64_t arenaEnd;
    bool recordPrivate;
    RegionClass current = RegionClass::Private;
    bool inRegion = false;
    RegionTotals totals;
    std::vector<PrivateMapping> privateMappings;
};

inline constexpr std::uint32_t TagHeapBlock = 1;
inline constexpr std::uint32_t TagDeviceMemory = 2;

class AllocationGroups {
public:
    struct Group {
        std::uint64_t base = 0;
        std::uint64_t end = 0;
        std::uint64_t bytes = 0;
        std::uint32_t regions = 0;
        std::string protections;
        std::string name;
        std::uint32_t tags = 0;
    };
    struct SizeClass {
        std::uint64_t count = 0;
        std::uint64_t bytes = 0;
    };
    static constexpr std::size_t SizeClassCount = 5;
    void Add(std::uint64_t group, std::uint64_t end, std::uint64_t bytes, std::string_view protection, std::string_view name = {});
    void Tag(std::span<const std::uint64_t> addresses, std::uint32_t tag);
    std::array<SizeClass, SizeClassCount> Histogram() const;
    std::vector<Group> Largest(std::size_t count) const;
    std::uint64_t Bytes() const;
    std::size_t Count() const { return groups.size(); }
    std::size_t Tagged(std::uint32_t tag) const;

private:
    std::vector<Group> groups;
};

std::size_t SizeClassOf(std::uint64_t bytes);
std::string GroupsText(const AllocationGroups& groups, std::size_t largest);

struct StatusTotals {
    std::uint64_t rss = 0;
    std::uint64_t anonymous = 0;
    std::uint64_t file = 0;
    std::uint64_t shmem = 0;
    std::uint64_t swap = 0;
};

StatusTotals ParseStatus(std::string_view text);
std::int64_t Unattributed(std::uint64_t total, std::initializer_list<std::uint64_t> named);

}

#endif
