#include "prx/libSceAgcDriver/Execution/include/PipelineManifest.hpp"
#include "ShaderCacheDirectory.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace AgcDriver {

namespace {

constexpr std::uint64_t Magic = 0x314d504335535041ull;
constexpr std::uint32_t FormatVersion = 1;

class Writer {
public:
    explicit Writer(std::vector<std::byte>& out) : out(out) {}

    template <typename T>
    void Value(T value) {
        const auto bytes = std::as_bytes(std::span(&value, 1));
        out.insert(out.end(), bytes.begin(), bytes.end());
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
            for (auto& record : *decoded) {
                records[record.program].push_back(std::move(record));
                ++count;
            }
            loaded = count;
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
    auto& program = records[record.program];
    if (std::ranges::find(program, record) != program.end()) return false;
    program.push_back(std::move(record));
    ++count;
    dirty = true;
    return true;
}

std::vector<ComputePipelineRecord> PipelineManifest::Records(const std::array<std::uint64_t, 2>& program) const {
    std::lock_guard lock(mutex);
    const auto found = records.find(program);
    return found != records.end() ? found->second : std::vector<ComputePipelineRecord>{};
}

std::size_t PipelineManifest::Size() const {
    std::lock_guard lock(mutex);
    return count;
}

void PipelineManifest::Save() {
    if (path.empty()) return;
    std::vector<ComputePipelineRecord> all;
    {
        std::lock_guard lock(mutex);
        if (!dirty) return;
        for (const auto& [program, list] : records) all.insert(all.end(), list.begin(), list.end());
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

std::vector<std::byte> PipelineManifest::Encode(std::uint64_t sourceVersion, std::span<const ComputePipelineRecord> records) {
    std::vector<std::byte> out;
    Writer writer(out);
    writer.Value(Magic);
    writer.Value(FormatVersion);
    writer.Value(sourceVersion);
    writer.Value(static_cast<std::uint32_t>(records.size()));
    for (const auto& record : records) {
        writer.Value(record.program[0]);
        writer.Value(record.program[1]);
        writer.Value(static_cast<std::uint32_t>(record.constants.size()));
        for (const auto& constant : record.constants) {
            writer.Value(constant.id);
            writer.Value(constant.value);
        }
        writer.Value(static_cast<std::uint32_t>(record.bindings.size()));
        for (const auto& [binding, elements] : record.bindings) {
            writer.Value(binding);
            writer.Value(elements);
        }
    }
    return out;
}

std::optional<std::vector<ComputePipelineRecord>> PipelineManifest::Decode(std::span<const std::byte> bytes, std::uint64_t sourceVersion) {
    Reader reader(bytes);
    std::uint64_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t source = 0;
    std::uint32_t size = 0;
    if (!reader.Value(magic) || !reader.Value(version) || !reader.Value(source) || !reader.Value(size)) return std::nullopt;
    if (magic != Magic || version != FormatVersion || source != sourceVersion) return std::nullopt;
    std::vector<ComputePipelineRecord> records;
    for (std::uint32_t index = 0; index < size; ++index) {
        ComputePipelineRecord record;
        std::uint32_t constants = 0;
        if (!reader.Value(record.program[0]) || !reader.Value(record.program[1]) || !reader.Value(constants) || constants > reader.Remaining() / 8u) return std::nullopt;
        record.constants.resize(constants);
        for (auto& constant : record.constants) {
            if (!reader.Value(constant.id) || !reader.Value(constant.value)) return std::nullopt;
        }
        std::uint32_t bindings = 0;
        if (!reader.Value(bindings) || bindings > reader.Remaining() / 8u) return std::nullopt;
        record.bindings.resize(bindings);
        for (auto& [binding, elements] : record.bindings) {
            if (!reader.Value(binding) || !reader.Value(elements)) return std::nullopt;
        }
        records.push_back(std::move(record));
    }
    if (reader.Remaining() != 0) return std::nullopt;
    return records;
}

}
