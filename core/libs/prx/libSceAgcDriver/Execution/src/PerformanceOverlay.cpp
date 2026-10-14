#include "prx/libSceAgcDriver/Execution/include/PerformanceOverlay.hpp"
#include "prx/libSceAgcDriver/Execution/include/Presentation.hpp"
#include "prx/libSceAgcDriver/Execution/include/PqToSdr.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/shaders/PerformanceOverlayFrag_spv.h"
#include "prx/libSceAgcDriver/Graphics/shaders/PerformanceOverlayVert_spv.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace AgcDriver {
namespace {

static_assert(sizeof(OverlayRect) == 20, "overlay rectangles are read as one ivec4 and one uint per instance");

struct OverlayParameters {
    std::array<std::array<float, 4>, OverlayColorCount> palette;
    std::array<float, 2> extent;
};

std::atomic<std::uint64_t> toggleCount{0};
std::atomic<std::uint64_t> gpuTotal{0};
std::atomic<std::uint64_t> driverTotal{0};

const OverlaySettings& settings() {
    static const OverlaySettings parsed = ParseOverlaySettings(std::getenv("ANYPS5_OVERLAY"), std::getenv("ANYPS5_OVERLAY_POS"));
    return parsed;
}

std::int32_t panelWidth(std::int32_t scale) {
    return (OverlayMetrics::InnerWidth + 2 * OverlayMetrics::Padding) * scale;
}

std::int32_t panelHeight(const OverlayContent& content, std::int32_t scale) {
    const auto lines = static_cast<std::int32_t>(content.above.size() + content.below.size());
    const auto glyphGap = OverlayMetrics::LinePitch - OverlayMetrics::GlyphHeight;
    return (2 * OverlayMetrics::Padding + lines * OverlayMetrics::LinePitch + OverlayMetrics::GraphHeight + OverlayMetrics::GraphGap - glyphGap) * scale;
}

bool fits(VkExtent2D extent, const OverlayContent& content, std::int32_t scale) {
    const auto margins = 2 * OverlayMetrics::Margin * scale;
    return panelWidth(scale) + margins <= static_cast<std::int32_t>(extent.width) && panelHeight(content, scale) + margins <= static_cast<std::int32_t>(extent.height);
}

float srgbDecode(float value) {
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float pqEncode(float nits) {
    constexpr double m1 = 2610.0 / 16384.0;
    constexpr double m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0;
    constexpr double c2 = 2413.0 / 4096.0 * 32.0;
    constexpr double c3 = 2392.0 / 4096.0 * 32.0;
    const double power = std::pow(std::clamp(static_cast<double>(nits) / 10000.0, 0.0, 1.0), m1);
    return static_cast<float>(std::pow((c1 + c2 * power) / (1.0 + c3 * power), m2));
}

bool srgbFormat(VkFormat format) {
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

}

OverlaySettings ParseOverlaySettings(const char* enabled, const char* position) {
    OverlaySettings parsed;
    const std::string_view visible = enabled != nullptr ? enabled : "";
    if (visible == "1") parsed.visible = true;
    else if (!visible.empty() && visible != "0") throw std::runtime_error("ANYPS5_OVERLAY must be 1 or 0, not '" + std::string(visible) + "'");
    const std::string_view corner = position != nullptr ? position : "";
    if (corner.empty() || corner == "tl") parsed.corner = OverlayCorner::TopLeft;
    else if (corner == "tr") parsed.corner = OverlayCorner::TopRight;
    else if (corner == "bl") parsed.corner = OverlayCorner::BottomLeft;
    else if (corner == "br") parsed.corner = OverlayCorner::BottomRight;
    else throw std::runtime_error("ANYPS5_OVERLAY_POS must be tl, tr, bl or br, not '" + std::string(corner) + "'");
    return parsed;
}

void PerformanceCounters::AddGpuNanoseconds(std::uint64_t nanoseconds) {
    gpuTotal.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void PerformanceCounters::AddDriverNanoseconds(std::uint64_t nanoseconds) {
    driverTotal.fetch_add(nanoseconds, std::memory_order_relaxed);
}

std::uint64_t PerformanceCounters::GpuNanoseconds() {
    return gpuTotal.load(std::memory_order_relaxed);
}

std::uint64_t PerformanceCounters::DriverNanoseconds() {
    return driverTotal.load(std::memory_order_relaxed);
}

bool PerformanceCounters::GpuEnabled() {
    return Graphics::Recorder::GpuTimingEnabled();
}

bool PerformanceCounters::DriverEnabled() {
    static const bool enabled = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return enabled;
}

void OverlayStatistics::AddPresent(Clock::time_point at, std::uint64_t gpuNanoseconds, std::uint64_t driverNanoseconds) {
    if (!window.empty()) {
        Graphics::Require(at >= window.back().at, "presents reached the performance overlay out of order");
        frameTimes[frameNext] = std::chrono::duration<double, std::milli>(at - window.back().at).count();
        frameNext = (frameNext + 1) % FrameHistory;
        frameCount = std::min(frameCount + 1, FrameHistory);
    }
    window.push_back({at, gpuNanoseconds, driverNanoseconds});
    while (at - window.front().at > Window) window.pop_front();
}

void OverlayStatistics::Reset() {
    window.clear();
    frameTimes = {};
    frameCount = 0;
    frameNext = 0;
}

double OverlayStatistics::Fps() const {
    if (window.size() < 2) return 0;
    const auto seconds = std::chrono::duration<double>(window.back().at - window.front().at).count();
    return seconds > 0 ? static_cast<double>(window.size() - 1) / seconds : 0;
}

double OverlayStatistics::LastFrameMs() const {
    return frameCount == 0 ? 0 : frameTimes[(frameNext + FrameHistory - 1) % FrameHistory];
}

std::vector<double> OverlayStatistics::FrameTimes() const {
    std::vector<double> ordered;
    ordered.reserve(frameCount);
    for (std::size_t i = 0; i < frameCount; ++i) ordered.push_back(frameTimes[(frameNext + FrameHistory - frameCount + i) % FrameHistory]);
    return ordered;
}

OverlayStatistics::Range OverlayStatistics::FrameTimeRange() const {
    const auto times = FrameTimes();
    if (times.empty()) return {};
    Range range{times.front(), 0, times.front()};
    double sum = 0;
    for (const auto time : times) {
        range.minimum = std::min(range.minimum, time);
        range.maximum = std::max(range.maximum, time);
        sum += time;
    }
    range.average = sum / static_cast<double>(times.size());
    return range;
}

double OverlayStatistics::perFrameMs(std::uint64_t Sample::*counter) const {
    if (window.size() < 2) return 0;
    const auto first = window.front().*counter;
    const auto last = window.back().*counter;
    return last >= first ? static_cast<double>(last - first) / 1e6 / static_cast<double>(window.size() - 1) : 0;
}

double OverlayStatistics::GpuMsPerFrame() const {
    return perFrameMs(&Sample::gpu);
}

double OverlayStatistics::DriverMsPerFrame() const {
    return perFrameMs(&Sample::driver);
}

const std::array<std::uint8_t, OverlayMetrics::GlyphHeight>& OverlayGlyph(char character) {
    using Glyph = std::array<std::uint8_t, OverlayMetrics::GlyphHeight>;
    static constexpr Glyph space{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    static constexpr std::array<Glyph, 10> digits{{
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
        {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
        {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    }};
    static constexpr Glyph period{0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    static constexpr Glyph colon{0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
    static constexpr Glyph slash{0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00};
    static constexpr Glyph a{0x0e, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11};
    static constexpr Glyph d{0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c};
    static constexpr Glyph e{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static constexpr Glyph f{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    static constexpr Glyph g{0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
    static constexpr Glyph i{0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    static constexpr Glyph m{0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    static constexpr Glyph n{0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11};
    static constexpr Glyph p{0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    static constexpr Glyph r{0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static constexpr Glyph s{0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static constexpr Glyph u{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static constexpr Glyph v{0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    static constexpr Glyph x{0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    if (character >= '0' && character <= '9') return digits[static_cast<std::size_t>(character - '0')];
    switch (character) {
        case ' ': return space;
        case '.': return period;
        case ':': return colon;
        case '/': return slash;
        case 'A': return a;
        case 'D': return d;
        case 'E': return e;
        case 'F': return f;
        case 'G': return g;
        case 'I': return i;
        case 'M': return m;
        case 'N': return n;
        case 'P': return p;
        case 'R': return r;
        case 'S': return s;
        case 'U': return u;
        case 'V': return v;
        case 'X': return x;
        default: throw std::runtime_error(std::string("the performance overlay font has no glyph for '") + character + "'");
    }
}

std::string FormatOverlayValue(double value) {
    const double clamped = std::isfinite(value) ? std::clamp(value, 0.0, 9999.0) : 0.0;
    char text[16];
    std::snprintf(text, sizeof(text), clamped < 99.95 ? "%.1f" : "%.0f", clamped);
    return text;
}

OverlayContent ComposeOverlay(const OverlayStatistics& statistics, bool gpu, bool driver) {
    OverlayContent content;
    content.above.push_back("FPS: " + FormatOverlayValue(statistics.Fps()));
    content.above.push_back("FRAME: " + FormatOverlayValue(statistics.LastFrameMs()) + " MS");
    content.frameTimes = statistics.FrameTimes();
    const auto range = statistics.FrameTimeRange();
    content.below.push_back("MIN/AVG/MAX: " + FormatOverlayValue(range.minimum) + "/" + FormatOverlayValue(range.average) + "/" + FormatOverlayValue(range.maximum));
    if (gpu) content.below.push_back("GPU: " + FormatOverlayValue(statistics.GpuMsPerFrame()) + " MS");
    if (driver) content.below.push_back("DRIVER: " + FormatOverlayValue(statistics.DriverMsPerFrame()) + " MS");
    return content;
}

std::int32_t OverlayScale(VkExtent2D extent, const OverlayContent& content) {
    auto scale = std::max<std::int32_t>(1, static_cast<std::int32_t>((extent.height + 240) / 480));
    while (scale > 1 && !fits(extent, content, scale)) --scale;
    return scale;
}

OverlayLayout LayOutOverlay(const OverlayContent& content, VkExtent2D extent, OverlayCorner corner) {
    Graphics::Require(extent.width != 0 && extent.height != 0, "the performance overlay needs a destination");
    Graphics::Require(content.frameTimes.size() <= OverlayStatistics::FrameHistory, "the performance overlay graph holds at most 120 frames");
    OverlayLayout layout;
    const auto scale = OverlayScale(extent, content);
    layout.scale = scale;
    const auto width = panelWidth(scale);
    const auto height = panelHeight(content, scale);
    const auto margin = OverlayMetrics::Margin * scale;
    const bool right = corner == OverlayCorner::TopRight || corner == OverlayCorner::BottomRight;
    const bool bottom = corner == OverlayCorner::BottomLeft || corner == OverlayCorner::BottomRight;
    const auto left = std::max(0, right ? static_cast<std::int32_t>(extent.width) - margin - width : margin);
    const auto top = std::max(0, bottom ? static_cast<std::int32_t>(extent.height) - margin - height : margin);
    layout.bounds = {left, top, width, height, static_cast<std::uint32_t>(OverlayColor::Background)};
    layout.rects.push_back(layout.bounds);
    const auto text = [&](const std::string& line, std::int32_t row) {
        Graphics::Require(line.size() <= static_cast<std::size_t>(OverlayMetrics::Columns), "a performance overlay line is wider than the panel");
        for (std::size_t column = 0; column < line.size(); ++column) {
            const auto& glyph = OverlayGlyph(line[column]);
            const auto glyphLeft = left + (OverlayMetrics::Padding + static_cast<std::int32_t>(column) * OverlayMetrics::Advance) * scale;
            for (std::int32_t y = 0; y < OverlayMetrics::GlyphHeight; ++y) {
                const auto bits = glyph[static_cast<std::size_t>(y)];
                std::int32_t x = 0;
                while (x < OverlayMetrics::GlyphWidth) {
                    if ((bits & (0x10u >> x)) == 0) {
                        ++x;
                        continue;
                    }
                    auto end = x;
                    while (end < OverlayMetrics::GlyphWidth && (bits & (0x10u >> end)) != 0) ++end;
                    layout.rects.push_back({glyphLeft + x * scale, top + (row + y) * scale, (end - x) * scale, scale, static_cast<std::uint32_t>(OverlayColor::Text)});
                    x = end;
                }
            }
        }
    };
    auto row = OverlayMetrics::Padding;
    for (const auto& line : content.above) {
        text(line, row);
        row += OverlayMetrics::LinePitch;
    }
    const auto graphBottom = top + (row + OverlayMetrics::GraphHeight) * scale;
    if (!content.frameTimes.empty()) {
        double maximum = 0;
        double sum = 0;
        for (const auto time : content.frameTimes) {
            maximum = std::max(maximum, time);
            sum += time;
        }
        const double fullScale = std::max(maximum, 2.0 * sum / static_cast<double>(content.frameTimes.size()));
        const auto first = static_cast<std::int32_t>(OverlayStatistics::FrameHistory - content.frameTimes.size());
        for (std::size_t i = 0; i < content.frameTimes.size() && fullScale > 0; ++i) {
            const auto pixels = std::clamp(static_cast<std::int32_t>(std::lround(content.frameTimes[i] / fullScale * OverlayMetrics::GraphHeight * scale)), 1, OverlayMetrics::GraphHeight * scale);
            layout.rects.push_back({left + (OverlayMetrics::Padding + first + static_cast<std::int32_t>(i)) * scale, graphBottom - pixels, scale, pixels, static_cast<std::uint32_t>(OverlayColor::Graph)});
        }
    }
    row += OverlayMetrics::GraphHeight + OverlayMetrics::GraphGap;
    for (const auto& line : content.below) {
        text(line, row);
        row += OverlayMetrics::LinePitch;
    }
    return layout;
}

std::array<std::array<float, 4>, OverlayColorCount> OverlayPalette(VkFormat format, VkColorSpaceKHR colorSpace) {
    std::array<std::array<float, 4>, OverlayColorCount> palette{{{0.0f, 0.0f, 0.0f, 0.6f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.35f, 0.8f, 0.35f, 1.0f}}};
    if (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
        for (auto& color : palette) {
            const float red = srgbDecode(color[0]);
            const float green = srgbDecode(color[1]);
            const float blue = srgbDecode(color[2]);
            const std::array<float, 3> bt2020{
                0.6274f * red + 0.3293f * green + 0.0433f * blue,
                0.0691f * red + 0.9195f * green + 0.0114f * blue,
                0.0164f * red + 0.0880f * green + 0.8956f * blue};
            for (std::size_t channel = 0; channel < 3; ++channel) color[channel] = pqEncode(bt2020[channel] * PqReferenceWhiteNits);
        }
    } else if (srgbFormat(format)) {
        for (auto& color : palette) {
            for (std::size_t channel = 0; channel < 3; ++channel) color[channel] = srgbDecode(color[channel]);
        }
    }
    return palette;
}

PerformanceOverlay::PerformanceOverlay(const Graphics::Context& context, std::size_t slotCount) : context(context), slots(slotCount) {
    Graphics::Require(slotCount != 0, "the performance overlay needs at least one slot");
    try {
        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OverlayParameters)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
        Graphics::Check(context.Function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(context.device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout performance overlay");
        const auto createModule = context.Function<PFN_vkCreateShaderModule>("vkCreateShaderModule");
        VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        moduleInfo.codeSize = sizeof(PERFORMANCE_OVERLAY_VERT_SPV);
        moduleInfo.pCode = PERFORMANCE_OVERLAY_VERT_SPV;
        Graphics::Check(createModule(context.device, &moduleInfo, nullptr, &vertex), "vkCreateShaderModule performance overlay vertex");
        moduleInfo.codeSize = sizeof(PERFORMANCE_OVERLAY_FRAG_SPV);
        moduleInfo.pCode = PERFORMANCE_OVERLAY_FRAG_SPV;
        Graphics::Check(createModule(context.device, &moduleInfo, nullptr, &fragment), "vkCreateShaderModule performance overlay fragment");
    } catch (...) {
        release();
        throw;
    }
}

PerformanceOverlay::~PerformanceOverlay() {
    release();
}

void PerformanceOverlay::releaseTransient(Slot& slot) noexcept {
    for (const auto& [view, framebuffer] : slot.transient) {
        if (framebuffer) context.Function<PFN_vkDestroyFramebuffer>("vkDestroyFramebuffer")(context.device, framebuffer, nullptr);
        if (view) context.Function<PFN_vkDestroyImageView>("vkDestroyImageView")(context.device, view, nullptr);
    }
    slot.transient.clear();
}

void PerformanceOverlay::Release(std::size_t index) noexcept {
    if (index < slots.size()) releaseTransient(slots[index]);
}

void PerformanceOverlay::release() noexcept {
    for (auto& slot : slots) {
        releaseTransient(slot);
        slot.rects.reset();
        slot.capacity = 0;
    }
    for (const auto& [format, entry] : pipelines) context.Function<PFN_vkDestroyPipeline>("vkDestroyPipeline")(context.device, entry, nullptr);
    pipelines.clear();
    for (const auto& [key, entry] : renderPasses) context.Function<PFN_vkDestroyRenderPass>("vkDestroyRenderPass")(context.device, entry, nullptr);
    renderPasses.clear();
    if (fragment) context.Function<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(context.device, fragment, nullptr);
    if (vertex) context.Function<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(context.device, vertex, nullptr);
    if (pipelineLayout) context.Function<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(context.device, pipelineLayout, nullptr);
    fragment = vertex = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
}

bool PerformanceOverlay::Visible() {
    return settings().visible != ((toggleCount.load(std::memory_order_relaxed) & 1u) != 0);
}

void PerformanceOverlay::Toggle() {
    toggleCount.fetch_add(1, std::memory_order_relaxed);
}

OverlayCorner PerformanceOverlay::Corner() {
    return settings().corner;
}

void PerformanceOverlay::NotePresent(OverlayStatistics::Clock::time_point at) {
    NotePresent(at, PerformanceCounters::GpuEnabled() ? PerformanceCounters::GpuNanoseconds() : 0, PerformanceCounters::DriverEnabled() ? PerformanceCounters::DriverNanoseconds() : 0);
}

void PerformanceOverlay::NotePresent(OverlayStatistics::Clock::time_point at, std::uint64_t gpuNanoseconds, std::uint64_t driverNanoseconds) {
    const auto current = toggleCount.load(std::memory_order_relaxed);
    if (current != toggles) {
        statistics.Reset();
        toggles = current;
    }
    statistics.AddPresent(at, gpuNanoseconds, driverNanoseconds);
}

VkRenderPass PerformanceOverlay::renderPass(VkFormat format, VkImageLayout initialLayout, VkImageLayout finalLayout) {
    const auto key = std::tuple{format, initialLayout, finalLayout};
    if (const auto found = renderPasses.find(key); found != renderPasses.end()) return found->second;
    VkFormatProperties properties{};
    context.formatProperties(context.physical, format, &properties);
    Graphics::Require((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0, "the performance overlay cannot blend into this format");
    VkAttachmentDescription attachment{};
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = initialLayout;
    attachment.finalLayout = finalLayout;
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    const bool transfer = finalLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const VkSubpassDependency dependencies[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, transfer ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, transfer ? VK_ACCESS_TRANSFER_READ_BIT : VkAccessFlags{0}, 0},
    };
    VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    info.attachmentCount = 1;
    info.pAttachments = &attachment;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = 2;
    info.pDependencies = dependencies;
    VkRenderPass created = VK_NULL_HANDLE;
    Graphics::Check(context.Function<PFN_vkCreateRenderPass>("vkCreateRenderPass")(context.device, &info, nullptr, &created), "vkCreateRenderPass performance overlay");
    return renderPasses.emplace(key, created).first->second;
}

VkPipeline PerformanceOverlay::pipeline(VkFormat format, VkRenderPass compatible) {
    if (const auto found = pipelines.find(format); found != pipelines.end()) return found->second;
    VkPipelineShaderStageCreateInfo stages[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";
    const VkVertexInputBindingDescription binding{0, sizeof(OverlayRect), VK_VERTEX_INPUT_RATE_INSTANCE};
    const VkVertexInputAttributeDescription attributes[2] = {{0, 0, VK_FORMAT_R32G32B32A32_SINT, 0}, {1, 0, VK_FORMAT_R32_UINT, 16}};
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.blendEnable = VK_TRUE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterization;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;
    info.renderPass = compatible;
    VkPipeline created = VK_NULL_HANDLE;
    Graphics::Check(context.Function<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &created), "vkCreateGraphicsPipelines performance overlay");
    return pipelines.emplace(format, created).first->second;
}

OverlayRect PerformanceOverlay::Record(VkCommandBuffer commands, std::size_t slot, VkImage destination, VkFormat format, VkColorSpaceKHR colorSpace, VkExtent2D extent, VkImageLayout initialLayout, VkImageLayout finalLayout) {
    return Record(commands, slot, destination, format, colorSpace, extent, initialLayout, finalLayout, ComposeOverlay(statistics, PerformanceCounters::GpuEnabled(), PerformanceCounters::DriverEnabled()), Corner());
}

OverlayRect PerformanceOverlay::Record(VkCommandBuffer commands, std::size_t index, VkImage destination, VkFormat format, VkColorSpaceKHR colorSpace, VkExtent2D extent, VkImageLayout initialLayout, VkImageLayout finalLayout, const OverlayContent& content, OverlayCorner corner) {
    Graphics::Require(index < slots.size(), "performance overlay slot is out of range");
    Graphics::Require(destination != VK_NULL_HANDLE && extent.width != 0 && extent.height != 0, "the performance overlay destination is unavailable");
    auto& slot = slots[index];
    releaseTransient(slot);
    const auto layout = LayOutOverlay(content, extent, corner);
    if (slot.capacity < layout.rects.size()) {
        const auto capacity = std::max<std::size_t>(layout.rects.size(), 1024);
        slot.rects = std::make_unique<Graphics::Buffer>(context, capacity * sizeof(OverlayRect), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        slot.capacity = capacity;
    }
    std::memcpy(slot.rects->Bytes().data(), layout.rects.data(), layout.rects.size() * sizeof(OverlayRect));
    const auto pass = renderPass(format, initialLayout, finalLayout);
    const auto drawing = pipeline(format, pass);
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = destination;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageViewUsageCreateInfo usageInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};
    usageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    viewInfo.pNext = &usageInfo;
    VkImageView view = VK_NULL_HANDLE;
    Graphics::Check(context.Function<PFN_vkCreateImageView>("vkCreateImageView")(context.device, &viewInfo, nullptr, &view), "vkCreateImageView performance overlay");
    VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = pass;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &view;
    framebufferInfo.width = extent.width;
    framebufferInfo.height = extent.height;
    framebufferInfo.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    const auto created = context.Function<PFN_vkCreateFramebuffer>("vkCreateFramebuffer")(context.device, &framebufferInfo, nullptr, &framebuffer);
    slot.transient.emplace_back(view, framebuffer);
    Graphics::Check(created, "vkCreateFramebuffer performance overlay");
    const auto right = std::min<std::int32_t>(layout.bounds.x + layout.bounds.width, static_cast<std::int32_t>(extent.width));
    const auto bottom = std::min<std::int32_t>(layout.bounds.y + layout.bounds.height, static_cast<std::int32_t>(extent.height));
    const VkRect2D area{{layout.bounds.x, layout.bounds.y}, {static_cast<std::uint32_t>(std::max(1, right - layout.bounds.x)), static_cast<std::uint32_t>(std::max(1, bottom - layout.bounds.y))}};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = pass;
    begin.framebuffer = framebuffer;
    begin.renderArea = area;
    context.Function<PFN_vkCmdBeginRenderPass>("vkCmdBeginRenderPass")(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
    context.Function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, drawing);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
    context.Function<PFN_vkCmdSetViewport>("vkCmdSetViewport")(commands, 0, 1, &viewport);
    context.Function<PFN_vkCmdSetScissor>("vkCmdSetScissor")(commands, 0, 1, &area);
    const OverlayParameters parameters{OverlayPalette(format, colorSpace), {static_cast<float>(extent.width), static_cast<float>(extent.height)}};
    context.Function<PFN_vkCmdPushConstants>("vkCmdPushConstants")(commands, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(parameters), &parameters);
    const VkBuffer buffer = slot.rects->Handle();
    const VkDeviceSize offset = 0;
    context.Function<PFN_vkCmdBindVertexBuffers>("vkCmdBindVertexBuffers")(commands, 0, 1, &buffer, &offset);
    context.Function<PFN_vkCmdDraw>("vkCmdDraw")(commands, 4, static_cast<std::uint32_t>(layout.rects.size()), 0, 0);
    context.Function<PFN_vkCmdEndRenderPass>("vkCmdEndRenderPass")(commands);
    return layout.bounds;
}

}

extern "C" void AgcDriverTogglePerformanceOverlay_nid_postfix() {
    AgcDriver::PerformanceOverlay::Toggle();
}
