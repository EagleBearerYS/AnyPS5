#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "CompiledVariant.hpp"
#include <spirv/unified1/spirv.hpp>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace AgcDriver::DriverDetail {

namespace {

std::uint32_t DeviceImageLookups(std::span<const std::uint32_t> words) {
    std::uint32_t lookup = 0;
    std::uint32_t calls = 0;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> 16u;
        if (count == 0u || count > words.size() - cursor) break;
        const auto op = words[cursor] & 0xffffu;
        if (op == spv::OpName && count > 2u) {
            const auto* text = reinterpret_cast<const char*>(words.data() + cursor + 2u);
            if (std::string_view(text, strnlen(text, (count - 2u) * sizeof(std::uint32_t))) == "device_image_lookup") lookup = words[cursor + 1u];
        }
        if (op == spv::OpFunctionCall && count >= 4u && lookup != 0u && words[cursor + 3u] == lookup) ++calls;
        cursor += count;
    }
    return calls;
}

const char* StageText(ShaderRecompiler::ShaderStage stage) {
    using Stage = ShaderRecompiler::ShaderStage;
    switch (stage) {
        case Stage::Compute: return "compute";
        case Stage::Vertex: return "vertex";
        case Stage::Fragment: return "pixel";
        default: return "other";
    }
}

}

void NoteDeviceImageModule(const ShaderSnapshot& snapshot, std::size_t codeOffset, const ShaderRecompiler::RecompileRequest& request, const ShaderRecompiler::RecompileResult& result) {
    static const bool profile = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    if (!profile || !result.deviceImageTables) return;
    std::uint64_t codeHash = 0xcbf29ce484222325ull;
    for (const auto word : request.shader.code) codeHash = (codeHash ^ word) * 0x100000001b3ull;
    {
        static std::mutex mutex;
        static std::set<std::pair<std::uint64_t, ShaderRecompiler::ShaderStage>> noted;
        std::lock_guard lock(mutex);
        if (!noted.emplace(codeHash, request.shader.stage).second) return;
    }
    std::string modes;
    if (const auto handle = SourceHandleFor(snapshot, codeOffset, request); handle != nullptr && handle->artifact != nullptr) {
        const auto& info = handle->artifact->info.info;
        for (std::size_t image = 0; image < info.images.size(); ++image) {
            if (!info.images[image].deviceTable) continue;
            const auto count = image < info.runtimeImageModes.size() ? info.runtimeImageModes[image].size() : 0u;
            modes += (modes.empty() ? "" : " ") + std::to_string(count);
        }
    }
    const auto words = result.spirv.Words();
    auto off = request;
    off.target.deviceImageTables = false;
    std::size_t offWords = 0;
    std::string failure;
    off.useCache = false;
    try {
        offWords = ShaderRecompiler::Recompile(off).spirv.Words().size();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto lookups = DeviceImageLookups(words);
    if (!failure.empty()) {
        AgcDriver::ProfilePrint_nid_no_patch("[device-images] program code hash %016llx at 0x%llx (%s): %zu SPIR-V words with device tables, the CPU table path's recompile failed (%s); %u device-table accesses, modes per device-table image %s\n", static_cast<unsigned long long>(codeHash), static_cast<unsigned long long>(request.shader.codeAddress), StageText(request.shader.stage), words.size(), failure.c_str(), lookups, modes.empty() ? "unknown" : modes.c_str());
        return;
    }
    const auto grown = static_cast<long long>(words.size()) - static_cast<long long>(offWords);
    AgcDriver::ProfilePrint_nid_no_patch("[device-images] program code hash %016llx at 0x%llx (%s): %zu SPIR-V words with device tables, %zu on the CPU table path (%+lld, %+.0f%%); %u device-table accesses, modes per device-table image %s\n", static_cast<unsigned long long>(codeHash), static_cast<unsigned long long>(request.shader.codeAddress), StageText(request.shader.stage), words.size(), offWords, grown, offWords != 0u ? 100.0 * static_cast<double>(grown) / static_cast<double>(offWords) : 0.0, lookups, modes.empty() ? "unknown" : modes.c_str());
}

}
