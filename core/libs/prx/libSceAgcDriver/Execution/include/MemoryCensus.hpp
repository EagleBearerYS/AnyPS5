#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYCENSUS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_MEMORYCENSUS_HPP

#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace AgcDriver::MemoryCensus {

enum class Owner : std::uint8_t { Untagged, PoolSlab, HeapMirror, ImageMirror, SampledTexture, StorageImage, RenderTarget, Staging, BdaTable, Lds, UnitShadow, Presentation, HostImport, Count };

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

PFN_vkGetDeviceProcAddr Wrap(PFN_vkGetDeviceProcAddr resolver);
void NoteDevice(VkDevice device, VkPhysicalDevice physical, PFN_vkGetPhysicalDeviceMemoryProperties2 properties2, const VkPhysicalDeviceMemoryProperties& memory);
void Tick();

enum class RegionClass : std::uint8_t { Arena, Mapped, Image, Private, Count };
inline constexpr std::size_t RegionClassCount = static_cast<std::size_t>(RegionClass::Count);

struct RegionTotals {
    std::array<std::uint64_t, RegionClassCount> resident{};
    std::array<std::uint64_t, RegionClassCount> swapped{};
    std::uint64_t regions = 0;
};

RegionClass ClassifyMapping(std::uint64_t begin, std::uint64_t end, std::string_view permissions, std::uint64_t inode, std::string_view path, std::uint64_t arenaBegin, std::uint64_t arenaEnd);

class SmapsParser {
public:
    SmapsParser(std::uint64_t arenaBegin, std::uint64_t arenaEnd) : arenaBegin(arenaBegin), arenaEnd(arenaEnd) {}
    void Line(std::string_view line);
    const RegionTotals& Totals() const { return totals; }

private:
    std::uint64_t arenaBegin;
    std::uint64_t arenaEnd;
    RegionClass current = RegionClass::Private;
    bool inRegion = false;
    RegionTotals totals;
};

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
