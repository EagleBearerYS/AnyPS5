#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RENDERSCALE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RENDERSCALE_HPP

#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace AgcDriver::Graphics {

struct RenderScale {
    std::uint32_t numerator = 1;
    std::uint32_t denominator = 1;
    bool operator==(const RenderScale& other) const = default;
};

enum class RenderScaleDemotion : std::uint8_t {
    GuestWrite,
    WriteBack,
    StorageAccess,
    ComputeWrite,
    TexelFetch,
    SizeQuery,
    Copy,
    DepthAttachment,
    MixedAttachment,
    FragmentPosition,
    Presentation,
    DepthGuestWrite,
    DepthWriteBack,
    DepthStorage,
    DepthCopy,
    StencilCopy,
    Count
};

inline constexpr std::size_t RenderScaleDemotionCount = static_cast<std::size_t>(RenderScaleDemotion::Count);

std::optional<RenderScale> ParseRenderScale(std::string_view text);
std::optional<RenderScale> ActiveRenderScale();
void SetRenderScaleForTesting(std::optional<RenderScale> scale);
void NoteVideoOutExtent(std::uint32_t width, std::uint32_t height);
VkExtent2D VideoOutExtent();
std::uint32_t ScaleLength(std::uint32_t length, RenderScale scale);
VkExtent2D ScaleExtent(VkExtent2D extent, RenderScale scale);
VkViewport ScaleViewport(const VkViewport& viewport, RenderScale scale);
VkRect2D ScaleScissor(const VkRect2D& scissor, RenderScale scale, VkExtent2D bound);
bool RenderScaleCoversVideoOut(VkExtent2D extent);
const char* RenderScaleDemotionName(RenderScaleDemotion reason);
void NoteRenderScaled(std::uint64_t address, VkExtent2D native, VkExtent2D scaled);
void NoteRenderScaleDemotion(RenderScaleDemotion reason, std::uint64_t address, VkExtent2D native);
bool RenderScaleDemoted(std::uint64_t address, VkExtent2D native);
std::array<std::uint64_t, RenderScaleDemotionCount> RenderScaleDemotions();
std::uint64_t RenderScaledSurfaces();
bool ReadsFragmentPosition(std::span<const std::uint32_t> spirv);

}

extern "C" void AgcDriverNoteVideoOutExtent_nid_postfix(std::uint32_t width, std::uint32_t height);

#endif
