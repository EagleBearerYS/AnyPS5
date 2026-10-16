#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include <cstdlib>

namespace AgcDriver::DriverDetail {

std::uint32_t Driver::drawUserWord(const DrawProgram& program, std::int32_t sgpr) {
    require(sgpr >= 0 && static_cast<std::uint32_t>(sgpr) >= program.firstUserSgpr, "invalid draw offset SGPR");
    const auto index = static_cast<std::uint32_t>(sgpr) - program.firstUserSgpr;
    require(index < program.userData.size(), "draw offset SGPR exceeds user data");
    return program.userData[index];
}

void Driver::FoldDrawOffsets(const ShaderRecompiler::RecompileResult& result, const DrawProgram& program, Pm4::DrawParameters& parameters) {
    if (result.vertexOffsetSgpr >= 0) {
        const auto offset = drawUserWord(program, result.vertexOffsetSgpr);
        require(offset <= std::numeric_limits<std::uint32_t>::max() - parameters.firstVertex, "draw vertex offset overflow");
        parameters.firstVertex += offset;
    }
    if (result.instanceOffsetSgpr >= 0) parameters.firstInstance = drawUserWord(program, result.instanceOffsetSgpr);
}

namespace {

// Read per draw, so one process can draw the same records both ways.
bool drawParametersOff() {
    return std::getenv("APS5_NO_INDIRECT_DRAW_PARAMETERS") != nullptr;
}

}

// The CP-patched SGPRs an indirect draw's vertex stage reads from the draw's own parameters
// (DrawParameterSgprs), or none when the draw is classified as before: the device lacks
// shaderDrawParameters, the path is not a plain vertex one, the front stage holds no patched SGPR,
// or the draw would still fall back to the CPU for a reason the stage cannot remove.
ShaderRecompiler::DrawParameterSgprs Driver::IndirectDrawParameters(const Graphics::State& graphics, const DrawProgram& frontProgram, const std::shared_ptr<VulkanDevice>& localDevice, const Pm4::DrawParameters& drawParameters) {
    const auto& indirect = *drawParameters.indirect;
    const auto support = localDevice->DrawIndirectSupport();
    const ShaderRecompiler::DrawParameterSgprs sgprs{true, indirect.baseVertexSgpr, indirect.startInstanceSgpr, indirect.drawIndexEnabled ? indirect.drawIndexSgpr : -1};
    if (drawParametersOff() || !support.drawParameters) return {};
    if (graphics.stages.path != Graphics::ShaderPath::Vertex || graphics.rectList || graphics.stages.mesh || graphics.stages.tessellation || frontProgram.binary.stage != ShaderRecompiler::ShaderStage::Vertex) return {};
    if (sgprs.baseVertex < 0 && sgprs.startInstance < 0 && sgprs.drawIndex < 0) return {};
    if (!drawParameters.indexed && indirect.indxOffset != 0) return {};
    if (indirect.countIndirect && (!support.count || (indirect.count > 1 && !support.multi))) return {};
    if (sgprs.drawIndex >= 0 && indirect.count > 1 && !support.multi) return {};
    if (sgprs.startInstance >= 0 && !support.firstInstance) return {};
    return sgprs;
}

// A register-keyed draw entry's key for an indirect draw: the packet fields IndirectDrawParameters
// reads beyond the registers, since its stage differs from a direct draw's when they enable it.
std::uint64_t Driver::IndirectDrawKey(std::uint64_t key, const Pm4::DrawParameters& drawParameters) {
    if (!drawParameters.indirect) return key;
    const auto& indirect = *drawParameters.indirect;
    const auto mix = [&](std::uint64_t value) {
        key ^= value;
        key *= 0x100000001b3ull;
    };
    mix(0x494e44u);
    mix(drawParametersOff());
    for (const std::uint64_t value : {std::uint64_t{indirect.baseVertexLocation}, std::uint64_t{indirect.startInstanceLocation}, std::uint64_t{indirect.drawIndexLocation}, std::uint64_t{indirect.drawIndexEnabled}, std::uint64_t{drawParameters.indexed}, std::uint64_t{indirect.indxOffset != 0}, std::uint64_t{indirect.countIndirect}, std::uint64_t{indirect.count > 1}}) mix(value);
    return key;
}

std::optional<Graphics::IndirectDrawPath> Driver::ClassifyIndirectDraw(const ShaderRecompiler::RecompileResult& result, const Graphics::State& graphics, const DrawProgram& frontProgram, const std::shared_ptr<VulkanDevice>& localDevice, Pm4::DrawParameters& drawParameters, bool traceIndirect) {
    std::optional<Graphics::IndirectDrawPath> indirectCpu;
    auto& indirect = *drawParameters.indirect;
    using Rule = Pm4::DrawParameters::IndirectDraw::Rule;
    using Path = Graphics::IndirectDrawPath;
    const auto support = localDevice->DrawIndirectSupport();
    const auto fail = [&](Path reason) { if (!indirectCpu) indirectCpu = reason; };
    constexpr std::uint32_t NoLocation = 0x280u;
    const bool meshPath = graphics.stages.mesh.has_value() && !graphics.stages.tessellation && !graphics.rectList;
    const bool meshGpuSide = meshPath && drawParameters.indexed && indirect.count == 1 && !indirect.countIndirect && indirect.baseVertexLocation == NoLocation && indirect.startInstanceLocation == NoLocation && (!indirect.drawIndexEnabled || indirect.drawIndexLocation == NoLocation);
    if ((graphics.stages.path != Graphics::ShaderPath::Vertex || graphics.rectList) && !meshGpuSide) fail(Path::NonVertexPath);
    if (indirect.drawIndexEnabled && indirect.drawIndexSgpr >= 0 && !indirect.drawParameters) fail(Path::DrawIndex);
    if (indirect.countIndirect && !support.count) fail(Path::FeatureGap);
    if (indirect.countIndirect && indirect.count > 1 && !support.multi) fail(Path::FeatureGap);
    // DrawIndex counts the draws of one vkCmdDraw*Indirect, not the per-record draws of a device
    // without multiDrawIndirect.
    if (indirect.drawParameters && indirect.drawIndexEnabled && indirect.drawIndexSgpr >= 0 && indirect.count > 1 && !support.multi) fail(Path::FeatureGap);
    const auto dimension = [&](std::int32_t k, std::int32_t p, bool shared, bool conflict, std::uint32_t x, Rule& rule, std::uint32_t& constant) {
        if (indirect.drawParameters) {
            // The stage's index VGPR is the CP's whatever the draw's offset, and it reads the
            // patched SGPR from the draw: the record's dword is drawn in place when the CP writes
            // it to an SGPR, and a dimension it writes nowhere is drawn with offset 0. The index
            // still misses GE_INDX_OFFSET.
            if (x != 0) fail(Path::IndxOffset);
            else if (p >= 0) rule = Rule::InPlace;
            else {
                rule = Rule::Constant;
                constant = 0;
            }
        } else if (p < 0) {
            rule = Rule::Constant;
            const auto offset = k >= 0 ? drawUserWord(frontProgram, k) : 0u;
            require(offset <= std::numeric_limits<std::uint32_t>::max() - x, "draw vertex offset overflow");
            constant = x + offset;
        } else if (conflict) fail(Path::FetchUnknown);
        else if (k != p || shared) fail(Path::NotFolded);
        else if (x != 0) fail(Path::IndxOffset);
        else rule = Rule::InPlace;
    };
    dimension(result.vertexOffsetSgpr, indirect.baseVertexSgpr, result.vertexOffsetShared, result.vertexOffsetConflict, drawParameters.indexed ? 0u : indirect.indxOffset, indirect.vertexRule, indirect.vertexConstant);
    dimension(result.instanceOffsetSgpr, indirect.startInstanceSgpr, result.instanceOffsetShared, result.instanceOffsetConflict, 0u, indirect.instanceRule, indirect.instanceConstant);
    if (!support.firstInstance && (indirect.instanceRule == Rule::InPlace || indirect.instanceConstant != 0)) fail(Path::FeatureGap);
    if ((indirect.vertexRule == Rule::Constant || indirect.instanceRule == Rule::Constant) && indirect.count > 256) fail(Path::FeatureGap);

    static const std::uint64_t vertexCap = [] { const char* text = std::getenv("APS5_INDIRECT_VERTEX_MIB"); return (text ? std::strtoull(text, nullptr, 10) : 64ull) << 20u; }();
    for (const auto& attribute : result.vertexAttributes) {
        const auto stride = (attribute.resource.fields[1] >> 16u) & 0x3fffu;
        const auto extent = stride == 0 ? static_cast<std::uint64_t>(attribute.resource.fields[2]) : static_cast<std::uint64_t>(attribute.resource.fields[2]) * stride;
        if (extent > vertexCap) fail(Path::VertexRange);
    }
    if (traceIndirect) {
        const auto location = [](std::uint32_t value, std::int32_t sgpr) { char text[24]; if (value == 0x280u) std::snprintf(text, sizeof(text), "none"); else std::snprintf(text, sizeof(text), "0x%x(s%d)", value, sgpr); return std::string(text); };
        const auto rule = [](Rule value, std::uint32_t constant) { char text[24]; if (value == Rule::InPlace) std::snprintf(text, sizeof(text), "in-place"); else std::snprintf(text, sizeof(text), "const %u", constant); return std::string(text); };
        std::string decision = indirectCpu ? "cpu (" + std::string(Graphics::IndirectDrawPathName(*indirectCpu)) + ")" : std::string(indirect.drawParameters ? "gpu draw-parameters" : "gpu") + " vertex=" + rule(indirect.vertexRule, indirect.vertexConstant) + " instance=" + rule(indirect.instanceRule, indirect.instanceConstant);
        std::fprintf(stderr, "[draw] indirect 0x%x args 0x%llx stride %u count %u%s locs base=%s inst=%s idx=%s%s analyzer v=%d i=%d shared=%d/%d conflict=%d/%d indx=%u -> %s\n", indirect.opcode, static_cast<unsigned long long>(indirect.arguments), indirect.stride, indirect.count, indirect.countIndirect ? " (indirect)" : "", location(indirect.baseVertexLocation, indirect.baseVertexSgpr).c_str(), location(indirect.startInstanceLocation, indirect.startInstanceSgpr).c_str(), location(indirect.drawIndexLocation, indirect.drawIndexSgpr).c_str(), indirect.drawIndexEnabled ? " (enabled)" : "", result.vertexOffsetSgpr, result.instanceOffsetSgpr, result.vertexOffsetShared ? 1 : 0, result.instanceOffsetShared ? 1 : 0, result.vertexOffsetConflict ? 1 : 0, result.instanceOffsetConflict ? 1 : 0, indirect.indxOffset, decision.c_str());
    }
    return indirectCpu;
}

}
