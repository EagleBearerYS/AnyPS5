#include "prx/libSceAgcDriver/Graphics/include/MultisampleConsumers.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

namespace AgcDriver::Graphics {
namespace {

using Counts = std::array<unsigned long long, 4>;

struct Counters {
    std::mutex mutex;
    Counts draws{};
    Counts standInDraws{};
    Counts reads{};
    Counts standInReads{};
    Counts perSample{};
    Counts sampleId{};
    Counts sampleMaskIn{};
    Counts maskExport{};
    unsigned long long resolves = 0;
    unsigned long long standInResolves = 0;
    unsigned long long fmaskDecompresses = 0;
    std::map<std::uint64_t, bool> standInWriters;
    std::chrono::steady_clock::time_point printed = std::chrono::steady_clock::now();
};

Counters& counters() {
    static auto* instance = new Counters();
    return *instance;
}

std::size_t slot(std::uint32_t samples) {
    return samples <= 1u ? 0u : samples == 2u ? 1u : samples == 4u ? 2u : 3u;
}

void noteWriter(Counters& counts, std::uint64_t address, bool standIn) {
    if (counts.standInWriters.size() >= 4096u && !counts.standInWriters.contains(address)) counts.standInWriters.erase(counts.standInWriters.begin());
    counts.standInWriters[address] = standIn;
}

bool standInWritten(const Counters& counts, std::uint64_t address) {
    const auto found = counts.standInWriters.find(address);
    return found != counts.standInWriters.end() && found->second;
}

void printDue(Counters& counts) {
    const auto now = std::chrono::steady_clock::now();
    if (now - counts.printed < std::chrono::seconds(10)) return;
    counts.printed = now;
    const auto loads = ShaderRecompiler::TranslatedMultisampleLoads();
    const auto& d = counts.draws;
    const auto& s = counts.standInDraws;
    std::fprintf(stderr, "[ms-consumers] stand-in draws 1x %llu, 2x %llu of %llu, 4x %llu of %llu, 8x %llu of %llu multisampled draws\n", s[0], s[1], d[1], s[2], d[2], s[3], d[3]);
    std::fprintf(stderr, "[ms-consumers] multisampled texture reads 2x %llu (%llu of stand-in images), 4x %llu (%llu), 8x %llu (%llu); translated image_load on MSAA %llu, image_msaa_load (FMASK) %llu\n", counts.reads[1], counts.standInReads[1], counts.reads[2], counts.standInReads[2], counts.reads[3], counts.standInReads[3], static_cast<unsigned long long>(loads.loads), static_cast<unsigned long long>(loads.fmaskLoads));
    std::fprintf(stderr, "[ms-consumers] decoded multisampled draw states 2x/4x/8x: per-sample shading %llu/%llu/%llu, SampleID (ancillary) %llu/%llu/%llu, SampleMaskIn %llu/%llu/%llu, sample mask export %llu/%llu/%llu\n", counts.perSample[1], counts.perSample[2], counts.perSample[3], counts.sampleId[1], counts.sampleId[2], counts.sampleId[3], counts.sampleMaskIn[1], counts.sampleMaskIn[2], counts.sampleMaskIn[3], counts.maskExport[1], counts.maskExport[2], counts.maskExport[3]);
    std::fprintf(stderr, "[ms-consumers] resolves %llu (%llu of stand-in images), FMASK decompresses %llu\n", counts.resolves, counts.standInResolves, counts.fmaskDecompresses);
}

}

bool TraceMultisampleConsumers() {
    static const bool enabled = [] {
        const char* value = std::getenv("APS5_TRACE_MS_CONSUMERS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

void NoteMultisampleDraw(const Context& context, const State& state) {
    if (!TraceMultisampleConsumers()) return;
    const bool standIn = state.sampleLocationGrid && (context.sampleLocationGridSampleCounts & state.samples) == 0u;
    if (state.samples <= 1u && !standIn) return;
    auto& counts = counters();
    std::lock_guard lock(counts.mutex);
    ++counts.draws[slot(state.samples)];
    if (standIn) ++counts.standInDraws[slot(state.samples)];
    for (const auto& color : state.colors) {
        if (color.samples > 1u) noteWriter(counts, color.address, standIn);
    }
    if (state.depth && state.depth->samples > 1u) noteWriter(counts, state.depth->address, standIn);
    printDue(counts);
}

void NoteMultisampleRead(const GuestTextureResource& resource) {
    if (!TraceMultisampleConsumers()) return;
    auto& counts = counters();
    std::lock_guard lock(counts.mutex);
    ++counts.reads[slot(resource.samples)];
    if (standInWritten(counts, resource.baseAddress)) ++counts.standInReads[slot(resource.samples)];
    printDue(counts);
}

void NotePixelSampleUse(const State& state, const ShaderRecompiler::ShaderPixelStageInfo& pixel) {
    if (!TraceMultisampleConsumers() || state.samples <= 1u) return;
    auto& counts = counters();
    std::lock_guard lock(counts.mutex);
    const auto index = slot(state.samples);
    if (pixel.sampleShading) ++counts.perSample[index];
    if (pixel.ancillary) ++counts.sampleId[index];
    if ((pixel.inputAddr & ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::SampleCoverage)) != 0u) ++counts.sampleMaskIn[index];
    if (pixel.sampleMaskExportEnable) ++counts.maskExport[index];
    printDue(counts);
}

void NoteMultisampleResolve(const ColorMetadataPass& pass) {
    if (!TraceMultisampleConsumers()) return;
    auto& counts = counters();
    std::lock_guard lock(counts.mutex);
    if (pass.mode == ColorMetadataPass::Mode::Resolve) {
        ++counts.resolves;
        if (pass.source && standInWritten(counts, pass.source->address)) ++counts.standInResolves;
    } else if (pass.mode == ColorMetadataPass::Mode::FmaskDecompress) {
        ++counts.fmaskDecompresses;
    }
    printDue(counts);
}

}
