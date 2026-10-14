#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PIPELINEMANIFEST_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PIPELINEMANIFEST_HPP

#include "PipelineSpecialization.hpp"
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace AgcDriver {

struct ComputePipelineRecord {
    std::array<std::uint64_t, 2> program{};
    std::vector<ShaderRecompiler::PipelineSpecializationConstant> constants;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bindings;

    bool operator==(const ComputePipelineRecord&) const = default;
};

struct GraphicsBindingShape {
    std::uint32_t kind = 0;
    std::uint32_t role = 0;
    std::uint32_t descriptorSet = 0;
    std::uint32_t binding = 0;
    std::uint32_t count = 0;
    std::uint32_t written = 0;

    bool operator==(const GraphicsBindingShape&) const = default;
};

struct GraphicsStageRecord {
    std::uint32_t stage = 0;
    std::uint32_t pushOffset = 0;
    std::array<std::uint64_t, 2> program{};
    std::vector<ShaderRecompiler::PipelineSpecializationConstant> constants;
    std::uint32_t pushBytes = 0;
    std::vector<GraphicsBindingShape> bindings;

    bool operator==(const GraphicsStageRecord&) const = default;
};

struct GraphicsPipelineRecord {
    std::vector<GraphicsStageRecord> stages;
    std::vector<std::byte> description;

    bool operator==(const GraphicsPipelineRecord&) const = default;
};

struct ManifestRecords {
    std::vector<ComputePipelineRecord> compute;
    std::vector<GraphicsPipelineRecord> graphics;

    bool operator==(const ManifestRecords&) const = default;
};

class PipelineManifest {
public:
    static constexpr int SaveIntervalSeconds = 10;

    PipelineManifest(std::filesystem::path path, std::uint64_t sourceVersion);
    ~PipelineManifest();
    PipelineManifest(const PipelineManifest&) = delete;
    PipelineManifest& operator=(const PipelineManifest&) = delete;

    bool Record(ComputePipelineRecord record);
    bool Record(GraphicsPipelineRecord record);
    std::vector<ComputePipelineRecord> Records(const std::array<std::uint64_t, 2>& program) const;
    std::vector<GraphicsPipelineRecord> GraphicsRecords() const;
    std::size_t Size() const;
    std::size_t Loaded() const { return loaded.compute.size() + loaded.graphics.size(); }
    const ManifestRecords& LoadedRecords() const { return loaded; }
    void Save();

    static std::vector<std::byte> Encode(std::uint64_t sourceVersion, const ManifestRecords& records);
    static std::optional<ManifestRecords> Decode(std::span<const std::byte> bytes, std::uint64_t sourceVersion);

private:
    void run();

    std::filesystem::path path;
    std::uint64_t sourceVersion;
    ManifestRecords loaded;
    mutable std::mutex mutex;
    std::map<std::array<std::uint64_t, 2>, std::vector<ComputePipelineRecord>> compute;
    std::vector<GraphicsPipelineRecord> graphics;
    std::size_t count = 0;
    bool dirty = false;
    bool stopping = false;
    std::condition_variable wake;
    std::thread saver;
};

}

#endif
