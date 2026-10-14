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

class PipelineManifest {
public:
    static constexpr int SaveIntervalSeconds = 10;

    PipelineManifest(std::filesystem::path path, std::uint64_t sourceVersion);
    ~PipelineManifest();
    PipelineManifest(const PipelineManifest&) = delete;
    PipelineManifest& operator=(const PipelineManifest&) = delete;

    bool Record(ComputePipelineRecord record);
    std::vector<ComputePipelineRecord> Records(const std::array<std::uint64_t, 2>& program) const;
    std::size_t Size() const;
    std::size_t Loaded() const { return loaded; }
    void Save();

    static std::vector<std::byte> Encode(std::uint64_t sourceVersion, std::span<const ComputePipelineRecord> records);
    static std::optional<std::vector<ComputePipelineRecord>> Decode(std::span<const std::byte> bytes, std::uint64_t sourceVersion);

private:
    void run();

    std::filesystem::path path;
    std::uint64_t sourceVersion;
    mutable std::mutex mutex;
    std::map<std::array<std::uint64_t, 2>, std::vector<ComputePipelineRecord>> records;
    std::size_t count = 0;
    std::size_t loaded = 0;
    bool dirty = false;
    bool stopping = false;
    std::condition_variable wake;
    std::thread saver;
};

}

#endif
