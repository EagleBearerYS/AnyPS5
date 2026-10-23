#include "prx/libSceAgcDriver/Graphics/include/RenderScale.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <numeric>
#include <set>
#include <string>
#include <tuple>

namespace AgcDriver::Graphics {

namespace {

std::optional<std::uint64_t> parseDigits(std::string_view text) {
    if (text.empty() || text.size() > 9) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

struct ScaleSetting {
    std::mutex mutex;
    bool resolved = false;
    std::optional<RenderScale> scale;
};

ScaleSetting& Setting() {
    static ScaleSetting setting;
    return setting;
}

std::atomic<std::uint32_t>& VideoOutWidth() {
    static std::atomic<std::uint32_t> width{0};
    return width;
}

std::atomic<std::uint32_t>& VideoOutHeight() {
    static std::atomic<std::uint32_t> height{0};
    return height;
}

std::array<std::atomic<std::uint64_t>, RenderScaleDemotionCount>& DemotionCounts() {
    static std::array<std::atomic<std::uint64_t>, RenderScaleDemotionCount> counts{};
    return counts;
}

std::atomic<std::uint64_t>& ScaledCount() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

std::atomic<std::uint64_t>& ScaledStoreCount() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

struct DemotedSurfaces {
    std::mutex mutex;
    std::set<std::tuple<std::uint64_t, std::uint32_t, std::uint32_t>> surfaces;
};

DemotedSurfaces& Demoted() {
    static DemotedSurfaces demoted;
    return demoted;
}

bool logDue(std::uint64_t count) {
    return count <= 16 || (count & (count - 1)) == 0;
}

}

std::optional<RenderScale> ParseRenderScale(std::string_view text) {
    RenderScale scale;
    if (const auto slash = text.find('/'); slash != std::string_view::npos) {
        const auto numerator = parseDigits(text.substr(0, slash));
        const auto denominator = parseDigits(text.substr(slash + 1));
        if (!numerator || !denominator) return std::nullopt;
        if (*numerator == 0 || *denominator == 0 || *numerator > *denominator || *denominator > 64) return std::nullopt;
        scale = {static_cast<std::uint32_t>(*numerator), static_cast<std::uint32_t>(*denominator)};
    } else {
        const auto point = text.find('.');
        const auto whole = parseDigits(text.substr(0, point));
        if (!whole) return std::nullopt;
        std::uint64_t numerator = *whole;
        std::uint64_t denominator = 1;
        if (point != std::string_view::npos) {
            const auto fraction = text.substr(point + 1);
            const auto digits = parseDigits(fraction);
            if (!digits || fraction.size() > 4) return std::nullopt;
            for (std::size_t i = 0; i < fraction.size(); ++i) denominator *= 10;
            numerator = numerator * denominator + *digits;
        }
        if (numerator == 0 || numerator > denominator) return std::nullopt;
        scale = {static_cast<std::uint32_t>(numerator), static_cast<std::uint32_t>(denominator)};
    }
    const auto divisor = std::gcd(scale.numerator, scale.denominator);
    scale.numerator /= divisor;
    scale.denominator /= divisor;
    return scale;
}

std::optional<RenderScale> ActiveRenderScale() {
    auto& setting = Setting();
    std::lock_guard lock(setting.mutex);
    if (!setting.resolved) {
        setting.resolved = true;
        if (const char* text = std::getenv("APS5_RENDER_SCALE"); text != nullptr && *text != '\0') {
            const auto parsed = ParseRenderScale(text);
            if (!parsed) {
                std::fprintf(stderr, "[render-scale] APS5_RENDER_SCALE=%s is not 0.5, 0.75 or N/D with 0 < N <= D <= 64; rendering at native size\n", text);
            } else if (parsed->numerator != parsed->denominator) {
                setting.scale = parsed;
                std::fprintf(stderr, "[render-scale] color and depth targets at least the video-out extent render at %u/%u of their size\n", parsed->numerator, parsed->denominator);
            }
        }
    }
    return setting.scale;
}

void SetRenderScaleForTesting(std::optional<RenderScale> scale) {
    auto& setting = Setting();
    std::lock_guard lock(setting.mutex);
    setting.resolved = true;
    setting.scale = scale.has_value() && scale->numerator != scale->denominator ? scale : std::nullopt;
}

void NoteVideoOutExtent(std::uint32_t width, std::uint32_t height) {
    auto& storedWidth = VideoOutWidth();
    auto& storedHeight = VideoOutHeight();
    const auto previousWidth = storedWidth.load(std::memory_order_relaxed);
    const auto previousHeight = storedHeight.load(std::memory_order_relaxed);
    if (static_cast<std::uint64_t>(width) * height <= static_cast<std::uint64_t>(previousWidth) * previousHeight) return;
    storedWidth.store(width, std::memory_order_relaxed);
    storedHeight.store(height, std::memory_order_relaxed);
    if (ActiveRenderScale().has_value()) std::fprintf(stderr, "[render-scale] main video-out buffer %ux%u\n", width, height);
}

VkExtent2D VideoOutExtent() {
    return {VideoOutWidth().load(std::memory_order_relaxed), VideoOutHeight().load(std::memory_order_relaxed)};
}

std::uint32_t ScaleLength(std::uint32_t length, RenderScale scale) {
    const auto scaled = (static_cast<std::uint64_t>(length) * scale.numerator + scale.denominator / 2) / scale.denominator;
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(scaled, 1));
}

VkExtent2D ScaleExtent(VkExtent2D extent, RenderScale scale) {
    return {ScaleLength(extent.width, scale), ScaleLength(extent.height, scale)};
}

VkViewport ScaleViewport(const VkViewport& viewport, RenderScale scale) {
    const float factor = static_cast<float>(scale.numerator) / static_cast<float>(scale.denominator);
    VkViewport scaled = viewport;
    scaled.x = viewport.x * factor;
    scaled.y = viewport.y * factor;
    scaled.width = viewport.width * factor;
    scaled.height = viewport.height * factor;
    return scaled;
}

VkRect2D ScaleScissor(const VkRect2D& scissor, RenderScale scale, VkExtent2D bound) {
    const auto floorScaled = [&](std::int64_t value) { return value * scale.numerator / scale.denominator; };
    const auto ceilScaled = [&](std::int64_t value) { return (value * scale.numerator + scale.denominator - 1) / scale.denominator; };
    const std::int64_t left = std::clamp<std::int64_t>(floorScaled(scissor.offset.x), 0, bound.width);
    const std::int64_t top = std::clamp<std::int64_t>(floorScaled(scissor.offset.y), 0, bound.height);
    const std::int64_t right = std::clamp<std::int64_t>(ceilScaled(static_cast<std::int64_t>(scissor.offset.x) + scissor.extent.width), left, bound.width);
    const std::int64_t bottom = std::clamp<std::int64_t>(ceilScaled(static_cast<std::int64_t>(scissor.offset.y) + scissor.extent.height), top, bound.height);
    return {{static_cast<std::int32_t>(left), static_cast<std::int32_t>(top)}, {static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)}};
}

bool RenderScaleCoversVideoOut(VkExtent2D extent) {
    if (!ActiveRenderScale().has_value()) return false;
    const auto videoOut = VideoOutExtent();
    return videoOut.width != 0 && videoOut.height != 0 && extent.width >= videoOut.width && extent.height >= videoOut.height;
}

const char* RenderScaleDemotionName(RenderScaleDemotion reason) {
    switch (reason) {
        case RenderScaleDemotion::GuestWrite: return "guest memory written (re-upload)";
        case RenderScaleDemotion::WriteBack: return "results stored to guest memory (CPU readback or write-back)";
        case RenderScaleDemotion::StorageAccess: return "storage-image access from a draw";
        case RenderScaleDemotion::ComputeWrite: return "storage-image access from a dispatch";
        case RenderScaleDemotion::TexelFetch: return "texel fetch with integer coordinates";
        case RenderScaleDemotion::SizeQuery: return "size query";
        case RenderScaleDemotion::Copy: return "copy or resolve at native size";
        case RenderScaleDemotion::DepthAttachment: return "drawn with a depth attachment that does not scale";
        case RenderScaleDemotion::MixedAttachment: return "drawn with an unscaled color attachment";
        case RenderScaleDemotion::FragmentPosition: return "drawn by a pixel shader reading its position";
        case RenderScaleDemotion::Presentation: return "presented through a converting path";
        case RenderScaleDemotion::DepthGuestWrite: return "guest memory written to a depth plane";
        case RenderScaleDemotion::DepthWriteBack: return "depth plane stored to guest memory";
        case RenderScaleDemotion::DepthStorage: return "depth plane accessed as a storage image";
        case RenderScaleDemotion::DepthCopy: return "depth plane copied at native size";
        case RenderScaleDemotion::StencilCopy: return "stencil plane copied for sampling";
        case RenderScaleDemotion::Count: break;
    }
    return "unknown";
}

void NoteRenderScaled(std::uint64_t address, VkExtent2D native, VkExtent2D scaled) {
    const auto count = ScaledCount().fetch_add(1, std::memory_order_relaxed) + 1;
    if (logDue(count)) std::fprintf(stderr, "[render-scale] 0x%llx %ux%u renders at %ux%u (%llu surfaces scaled)\n", static_cast<unsigned long long>(address), native.width, native.height, scaled.width, scaled.height, static_cast<unsigned long long>(count));
}

void NoteRenderScaleDemotion(RenderScaleDemotion reason, std::uint64_t address, VkExtent2D native) {
    {
        auto& demoted = Demoted();
        std::lock_guard lock(demoted.mutex);
        demoted.surfaces.emplace(address, native.width, native.height);
    }
    const auto index = static_cast<std::size_t>(reason);
    const auto count = DemotionCounts()[index].fetch_add(1, std::memory_order_relaxed) + 1;
    if (logDue(count)) std::fprintf(stderr, "[render-scale] 0x%llx %ux%u back to native size: %s (%llu for this reason)\n", static_cast<unsigned long long>(address), native.width, native.height, RenderScaleDemotionName(reason), static_cast<unsigned long long>(count));
}

bool RenderScaleDemoted(std::uint64_t address, VkExtent2D native) {
    auto& demoted = Demoted();
    std::lock_guard lock(demoted.mutex);
    return demoted.surfaces.contains({address, native.width, native.height});
}

std::array<std::uint64_t, RenderScaleDemotionCount> RenderScaleDemotions() {
    std::array<std::uint64_t, RenderScaleDemotionCount> counts{};
    for (std::size_t i = 0; i < counts.size(); ++i) counts[i] = DemotionCounts()[i].load(std::memory_order_relaxed);
    return counts;
}

std::uint64_t RenderScaledSurfaces() {
    return ScaledCount().load(std::memory_order_relaxed);
}

void NoteRenderScaledStore(std::uint64_t address, VkExtent2D native) {
    const auto count = ScaledStoreCount().fetch_add(1, std::memory_order_relaxed) + 1;
    if (logDue(count)) std::fprintf(stderr, "[render-scale] 0x%llx stored scaled at its native size %ux%u (%llu scaled stores)\n", static_cast<unsigned long long>(address), native.width, native.height, static_cast<unsigned long long>(count));
}

std::uint64_t RenderScaledStores() {
    return ScaledStoreCount().load(std::memory_order_relaxed);
}

bool ReadsFragmentPosition(std::span<const std::uint32_t> spirv) {
    constexpr std::uint32_t OpDecorate = 71;
    constexpr std::uint32_t DecorationBuiltIn = 11;
    constexpr std::uint32_t BuiltInFragCoord = 15;
    for (std::size_t index = 5; index < spirv.size();) {
        const auto words = spirv[index] >> 16u;
        const auto opcode = spirv[index] & 0xffffu;
        if (words == 0) return false;
        if (opcode == OpDecorate && words >= 4 && index + 3 < spirv.size() && spirv[index + 2] == DecorationBuiltIn && spirv[index + 3] == BuiltInFragCoord) return true;
        index += words;
    }
    return false;
}

}

extern "C" void AgcDriverNoteVideoOutExtent_nid_postfix(std::uint32_t width, std::uint32_t height) {
    AgcDriver::Graphics::NoteVideoOutExtent(width, height);
}
