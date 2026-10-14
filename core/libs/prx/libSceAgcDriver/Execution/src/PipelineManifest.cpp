#include "prx/libSceAgcDriver/Execution/include/PipelineManifest.hpp"
#include "ShaderCacheDirectory.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace AgcDriver {

namespace {

constexpr std::uint64_t Magic = 0x314d504335535041ull;
constexpr std::uint32_t FormatVersion = 2;

class Writer {
public:
    explicit Writer(std::vector<std::byte>& out) : out(out) {}

    template <typename T>
    void Value(T value) {
        const auto bytes = std::as_bytes(std::span(&value, 1));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    template <typename T, typename TWrite>
    void List(const std::vector<T>& values, TWrite&& write) {
        Value(static_cast<std::uint32_t>(values.size()));
        for (const auto& value : values) write(value);
    }

    void Constants(const std::vector<ShaderRecompiler::PipelineSpecializationConstant>& constants) {
        List(constants, [&](const ShaderRecompiler::PipelineSpecializationConstant& constant) {
            Value(constant.id);
            Value(constant.value);
        });
    }

private:
    std::vector<std::byte>& out;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes(bytes) {}

    template <typename T>
    bool Value(T& value) {
        if (bytes.size() - offset < sizeof(T)) return false;
        std::memcpy(&value, bytes.data() + offset, sizeof(T));
        offset += sizeof(T);
        return true;
    }

    template <typename T, typename TRead>
    bool List(std::vector<T>& values, std::size_t minimumBytes, TRead&& read) {
        std::uint32_t size = 0;
        if (!Value(size) || size > Remaining() / minimumBytes) return false;
        values.resize(size);
        for (auto& value : values) {
            if (!read(value)) return false;
        }
        return true;
    }

    bool Constants(std::vector<ShaderRecompiler::PipelineSpecializationConstant>& constants) {
        return List(constants, 8u, [&](ShaderRecompiler::PipelineSpecializationConstant& constant) { return Value(constant.id) && Value(constant.value); });
    }

    std::size_t Remaining() const { return bytes.size() - offset; }

private:
    std::span<const std::byte> bytes;
    std::size_t offset = 0;
};

}

PipelineManifest::PipelineManifest(std::filesystem::path path, std::uint64_t sourceVersion) : path(std::move(path)), sourceVersion(sourceVersion) {
    if (this->path.empty()) return;
    std::vector<std::byte> file;
    if (ShaderRecompiler::ReadWholeFile(this->path, file)) {
        if (auto decoded = Decode(file, sourceVersion)) {
            loaded = std::move(*decoded);
            for (const auto& record : loaded.compute) compute[record.program].push_back(record);
            graphics = loaded.graphics;
            count = loaded.compute.size() + loaded.graphics.size();
        } else {
            std::fprintf(stderr, "[pipeline-manifest] ignoring %s: it is not a manifest of this build\n", this->path.string().c_str());
        }
    }
    saver = std::thread([this] { run(); });
}

PipelineManifest::~PipelineManifest() {
    if (saver.joinable()) {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        saver.join();
    }
    Save();
}

bool PipelineManifest::Record(ComputePipelineRecord record) {
    std::lock_guard lock(mutex);
    auto& program = compute[record.program];
    if (std::ranges::find(program, record) != program.end()) return false;
    program.push_back(std::move(record));
    ++count;
    dirty = true;
    return true;
}

bool PipelineManifest::Record(GraphicsPipelineRecord record) {
    std::lock_guard lock(mutex);
    if (std::ranges::find(graphics, record) != graphics.end()) return false;
    graphics.push_back(std::move(record));
    ++count;
    dirty = true;
    return true;
}

std::vector<ComputePipelineRecord> PipelineManifest::Records(const std::array<std::uint64_t, 2>& program) const {
    std::lock_guard lock(mutex);
    const auto found = compute.find(program);
    return found != compute.end() ? found->second : std::vector<ComputePipelineRecord>{};
}

std::vector<GraphicsPipelineRecord> PipelineManifest::GraphicsRecords() const {
    std::lock_guard lock(mutex);
    return graphics;
}

std::size_t PipelineManifest::Size() const {
    std::lock_guard lock(mutex);
    return count;
}

void PipelineManifest::Save() {
    if (path.empty()) return;
    ManifestRecords all;
    {
        std::lock_guard lock(mutex);
        if (!dirty) return;
        for (const auto& [program, list] : compute) all.compute.insert(all.compute.end(), list.begin(), list.end());
        all.graphics = graphics;
        dirty = false;
    }
    const auto bytes = Encode(sourceVersion, all);
    if (!ShaderRecompiler::WriteFileAtomically(path, bytes)) {
        std::fprintf(stderr, "[pipeline-manifest] cannot write %s\n", path.string().c_str());
        std::lock_guard lock(mutex);
        dirty = true;
    }
}

void PipelineManifest::run() {
    std::unique_lock lock(mutex);
    while (!stopping) {
        wake.wait_for(lock, std::chrono::seconds(SaveIntervalSeconds), [&] { return stopping; });
        if (stopping) break;
        lock.unlock();
        Save();
        lock.lock();
    }
}

std::vector<std::byte> PipelineManifest::Encode(std::uint64_t sourceVersion, const ManifestRecords& records) {
    std::vector<std::byte> out;
    Writer writer(out);
    writer.Value(Magic);
    writer.Value(FormatVersion);
    writer.Value(sourceVersion);
    writer.List(records.compute, [&](const ComputePipelineRecord& record) {
        writer.Value(record.program[0]);
        writer.Value(record.program[1]);
        writer.Constants(record.constants);
        writer.List(record.bindings, [&](const std::pair<std::uint32_t, std::uint32_t>& binding) {
            writer.Value(binding.first);
            writer.Value(binding.second);
        });
    });
    writer.List(records.graphics, [&](const GraphicsPipelineRecord& record) {
        writer.List(record.stages, [&](const GraphicsStageRecord& stage) {
            writer.Value(stage.stage);
            writer.Value(stage.pushOffset);
            writer.Value(stage.program[0]);
            writer.Value(stage.program[1]);
            writer.Constants(stage.constants);
            writer.Value(stage.pushBytes);
            writer.List(stage.bindings, [&](const GraphicsBindingShape& binding) {
                writer.Value(binding.kind);
                writer.Value(binding.role);
                writer.Value(binding.descriptorSet);
                writer.Value(binding.binding);
                writer.Value(binding.count);
                writer.Value(binding.written);
            });
        });
        writer.List(record.description, [&](std::byte value) { writer.Value(value); });
    });
    return out;
}

std::optional<ManifestRecords> PipelineManifest::Decode(std::span<const std::byte> bytes, std::uint64_t sourceVersion) {
    Reader reader(bytes);
    std::uint64_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t source = 0;
    if (!reader.Value(magic) || !reader.Value(version) || !reader.Value(source)) return std::nullopt;
    if (magic != Magic || version != FormatVersion || source != sourceVersion) return std::nullopt;
    ManifestRecords records;
    const bool compute = reader.List(records.compute, 24u, [&](ComputePipelineRecord& record) {
        return reader.Value(record.program[0]) && reader.Value(record.program[1]) && reader.Constants(record.constants)
            && reader.List(record.bindings, 8u, [&](std::pair<std::uint32_t, std::uint32_t>& binding) { return reader.Value(binding.first) && reader.Value(binding.second); });
    });
    const bool graphics = compute && reader.List(records.graphics, 8u, [&](GraphicsPipelineRecord& record) {
        return reader.List(record.stages, 36u, [&](GraphicsStageRecord& stage) {
            return reader.Value(stage.stage) && reader.Value(stage.pushOffset) && reader.Value(stage.program[0]) && reader.Value(stage.program[1]) && reader.Constants(stage.constants) && reader.Value(stage.pushBytes)
                && reader.List(stage.bindings, 24u, [&](GraphicsBindingShape& binding) {
                       return reader.Value(binding.kind) && reader.Value(binding.role) && reader.Value(binding.descriptorSet) && reader.Value(binding.binding) && reader.Value(binding.count) && reader.Value(binding.written);
                   });
        }) && reader.List(record.description, 1u, [&](std::byte& value) { return reader.Value(value); });
    });
    if (!graphics || reader.Remaining() != 0) return std::nullopt;
    return records;
}

}
