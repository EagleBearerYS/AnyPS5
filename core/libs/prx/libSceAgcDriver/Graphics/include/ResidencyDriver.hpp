#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENCYDRIVER_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENCYDRIVER_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Residency.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ResidencyMirrors.hpp"
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace AgcDriver::Graphics {

static_assert(ResidencyStreams == MaxRecorderStreams);

struct ResidencyGuestImport {
    ResidencyImport attributes;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceAddress address = 0;
};

class IResidencyGuestMemory {
public:
    virtual ~IResidencyGuestMemory() = default;
    virtual std::optional<ResidencyGuestImport> ImportOf(std::uint64_t page) = 0;
    virtual std::vector<ResidencyRange> Aliases(std::uint64_t address, std::uint64_t bytes) = 0;
    virtual std::uint64_t Generation() = 0;
    virtual void ChangedPages(std::uint64_t firstPage, std::span<const std::uint64_t> generations, std::span<std::uint8_t> changed) = 0;
    virtual bool Guard(std::uint64_t page) = 0;
    virtual void Unguard(std::span<const std::uint64_t> pages) = 0;
};

struct ResidencyPublishedPage {
    std::uint64_t page = 0;
    std::uint64_t mirror = 0;
};

struct ResidencyPublication {
    std::uint64_t serial = 0;
    std::vector<ResidencyPublishedPage> changed;
};

struct ResidencyDriverCounts {
    std::uint64_t orderingPoints = 0;
    std::uint64_t appliedAtOrderingPoints = 0;
    std::uint64_t vanishedCopies = 0;
};

class ResidencyDriver final : public IResidencyDevice {
public:
    ResidencyDriver(const Context& context, Recorder& recorder, IResidencyGuestMemory& guest, const ResidencyPolicy& policy);
    ResidencyDriver(const ResidencyDriver&) = delete;
    ResidencyDriver& operator=(const ResidencyDriver&) = delete;
    ~ResidencyDriver() override;

    static ResidencyDriver* Active();

    ResidencyManager& Manager() { return manager; }
    const ResidencyMirrors& Mirrors() const { return mirrors; }
    void OrderingPoint();
    void BeforeWritingDispatch(std::span<const ResidencyRange> grants, bool writableImports);
    ResidencyPublication Publish();
    std::vector<Recorder::SectionObject> ChunkObjects(bool written) const;
    void KeepChunks();
    ResidencyDriverCounts Counts() const;

    bool Reached(const ResidencyToken& token) override;
    void Wait(const ResidencyToken& token) override;
    ResidencyToken Newest() override;
    ResidencyToken Current() override;
    ResidencyImport Import(std::uint64_t page) override;
    std::vector<ResidencyRange> Aliases(std::uint64_t address, std::uint64_t bytes) override;
    std::uint64_t Generation() override;
    void ChangedPages(std::uint64_t firstPage, std::span<const std::uint64_t> generations, std::span<std::uint8_t> changed) override;
    std::uint64_t AllocateChunk(std::uint32_t chunk, std::uint32_t slots) override;
    void ReleaseChunk(std::uint32_t chunk) override;
    ResidencyToken CopyIn(std::span<const ResidencyCopy> copies) override;
    ResidencyToken WriteBack(std::span<const ResidencyCopy> copies) override;
    bool Guard(std::uint64_t page) override;
    void Unguard(std::span<const std::uint64_t> pages) override;

private:
    ResidencyToken record(std::span<const ResidencyCopy> copies, bool intoMirror);

    Recorder& recorder;
    IResidencyGuestMemory& guest;
    ResidencyMirrors mirrors;
    ResidencyManager manager;
    std::atomic<std::uint64_t> orderingPoints{0};
    std::atomic<std::uint64_t> appliedAtOrderingPoints{0};
    std::uint64_t vanishedCopies = 0;
};

}

#endif
