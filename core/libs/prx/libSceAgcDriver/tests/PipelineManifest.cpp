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

std::vector<AgcDriver::ComputePipelineRecord> Records() {
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
}

void File() {
    const auto directory = std::filesystem::temp_directory_path() / ("aps5-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "compute.manifest";
    const auto records = Records();
    {
        AgcDriver::PipelineManifest manifest(path, 77u);
        Require(manifest.Loaded() == 0u && manifest.Size() == 0u, "a manifest without a file has records");
        for (const auto& record : records) Require(manifest.Record(record), "a new record was refused");
        Require(!manifest.Record(records[1]), "a record seen before was recorded again");
        Require(manifest.Records(records[0].program).size() == 2u && manifest.Records(records[2].program).size() == 1u && manifest.Records({9u, 9u}).empty(), "the records of a program are not the records recorded for it");
    }
    {
        AgcDriver::PipelineManifest manifest(path, 77u);
        Require(manifest.Loaded() == 3u && manifest.Records(records[0].program) == std::vector<AgcDriver::ComputePipelineRecord>{records[0], records[1]}, "a manifest did not load the records the previous one saved");
    }
    {
        AgcDriver::PipelineManifest manifest(path, 78u);
        Require(manifest.Loaded() == 0u && manifest.Records(records[0].program).empty(), "a manifest of another recompiler build was loaded");
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
