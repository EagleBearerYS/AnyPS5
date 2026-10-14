#include "prx/libSceAgcDriver/Execution/include/PipelineManifest.hpp"
#include "ShaderCacheDirectory.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<AgcDriver::ComputePipelineRecord> ComputeRecords() {
    AgcDriver::ComputePipelineRecord first;
    first.program = {0x1111222233334444ull, 0x5555666677778888ull};
    first.constants = {{16777219u, 0u}, {8188u, 0u}};
    first.bindings = {{3u, 1u}, {7u, 1u}};
    AgcDriver::ComputePipelineRecord second = first;
    second.constants[0].value = 2u;
    AgcDriver::ComputePipelineRecord third;
    third.program = {1u, 2u};
    return {first, second, third};
}

std::vector<AgcDriver::GraphicsPipelineRecord> GraphicsRecords() {
    AgcDriver::GraphicsPipelineRecord first;
    first.stages.push_back({1u, 0u, {0xaaaau, 0xbbbbu}, {{4096u + 5u, 2u}, {8188u, 4u}}, 64u, {{1u, 7u, 0u, 3u, 1u, 0u}}});
    first.stages.push_back({5u, 128u, {0xccccu, 0xddddu}, {{8224u, 1u}}, 0u, {{1u, 0u, 1u, 0u, 4u, 1u}, {8u, 2u, 1u, 9u, 1u, 0u}}});
    first.description = {std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x37}};
    auto second = first;
    second.description.back() = std::byte{0x38};
    return {first, second};
}

std::vector<AgcDriver::PreparedProgramRecord> PreparedRecords() {
    AgcDriver::PreparedProgramRecord first{{7u, 8u}, 64u, {0x1111222233334444ull, 0x5555666677778888ull}, "stage 0 code AQID"};
    auto second = first;
    second.codeOffset = 128u;
    second.identity = {3u, 4u};
    AgcDriver::PreparedProgramRecord other{{9u, 9u}, 0u, {5u, 6u}, ""};
    return {first, second, other};
}

AgcDriver::ManifestRecords Records() {
    return {ComputeRecords(), GraphicsRecords(), PreparedRecords()};
}

void Codec() {
    const auto records = Records();
    const auto bytes = AgcDriver::PipelineManifest::Encode(77u, records);
    const auto decoded = AgcDriver::PipelineManifest::Decode(bytes, 77u);
    Require(decoded.has_value() && *decoded == records, "a manifest did not decode to the records it was encoded from");
    Require(!AgcDriver::PipelineManifest::Decode(bytes, 78u).has_value(), "a manifest of another recompiler build was accepted");
    for (std::size_t size = 0; size < bytes.size(); ++size) Require(!AgcDriver::PipelineManifest::Decode(std::span(bytes).first(size), 77u).has_value(), "a manifest cut to " + std::to_string(size) + " bytes was accepted");
    auto longer = bytes;
    longer.push_back(std::byte{0});
    Require(!AgcDriver::PipelineManifest::Decode(longer, 77u).has_value(), "a manifest with trailing bytes was accepted");
    auto corrupt = bytes;
    corrupt[40] = std::byte{0xff};
    corrupt[41] = std::byte{0xff};
    corrupt[42] = std::byte{0xff};
    corrupt[43] = std::byte{0x7f};
    Require(!AgcDriver::PipelineManifest::Decode(corrupt, 77u).has_value(), "a manifest whose constant count exceeds its bytes was accepted");
    auto older = bytes;
    older[8] = std::byte{1};
    Require(!AgcDriver::PipelineManifest::Decode(older, 77u).has_value(), "a manifest of an earlier format was accepted");
}

void File() {
    const auto directory = std::filesystem::temp_directory_path() / ("aps5-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "pipelines.manifest";
    const auto compute = ComputeRecords();
    const auto graphics = GraphicsRecords();
    const auto prepared = PreparedRecords();
    {
        AgcDriver::PipelineManifest manifest(path, 77u);
        Require(manifest.Loaded() == 0u && manifest.Size() == 0u, "a manifest without a file has records");
        for (const auto& record : compute) Require(manifest.Record(record), "a new compute record was refused");
        for (const auto& record : graphics) Require(manifest.Record(record), "a new graphics record was refused");
        for (const auto& record : prepared) Require(manifest.Record(record), "a new record of a program prepared at use was refused");
        Require(!manifest.Record(compute[1]) && !manifest.Record(graphics[1]) && !manifest.Record(prepared[1]), "a record seen before was recorded again");
        Require(manifest.Records(compute[0].program).size() == 2u && manifest.Records(compute[2].program).size() == 1u && manifest.Records({9u, 9u}).empty(), "the records of a program are not the records recorded for it");
        Require(manifest.GraphicsRecords() == graphics, "the graphics records are not the ones recorded");
    }
    {
        AgcDriver::PipelineManifest manifest(path, 77u);
        Require(manifest.Loaded() == 8u && manifest.Records(compute[0].program) == std::vector<AgcDriver::ComputePipelineRecord>{compute[0], compute[1]}, "a manifest did not load the compute records the previous one saved");
        Require(manifest.PreparedPrograms({7u, 8u}) == std::vector<AgcDriver::PreparedProgramRecord>{prepared[0], prepared[1]} && manifest.PreparedPrograms({1u, 1u}).empty(), "a manifest did not load the programs prepared at use the previous one saved");
        Require(manifest.GraphicsRecords() == graphics && manifest.LoadedRecords().graphics == graphics, "a manifest did not load the graphics records the previous one saved");
    }
    {
        AgcDriver::PipelineManifest manifest(path, 78u);
        Require(manifest.Loaded() == 0u && manifest.Records(compute[0].program).empty() && manifest.GraphicsRecords().empty(), "a manifest of another recompiler build was loaded");
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}

}

int main() {
    try {
        Codec();
        File();
        std::puts("pipeline manifest tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
