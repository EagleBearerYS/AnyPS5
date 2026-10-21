#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace AgcDriver::Graphics {

struct ShaderResourcesTestAccess {
    using Validated = ShaderResources::ValidatedSurface;

    static void Capture(std::span<const TextureLookupRecord> log, std::span<const std::shared_ptr<Texture>> sampled, std::vector<Validated>& validated) {
        ShaderResources::captureValidation(log, sampled, validated);
    }
};

}

namespace {

using AgcDriver::Graphics::DccKeys;
using AgcDriver::Graphics::GuestTextureResource;
using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::ShaderResourcesTestAccess;
using AgcDriver::Graphics::StorageTexture;
using AgcDriver::Graphics::Texture;
using AgcDriver::Graphics::TextureLookupRecord;
using Validated = ShaderResourcesTestAccess::Validated;

class Objects {
public:
    explicit Objects(std::size_t count) : storage(count) {}

    const void* At(std::size_t index) const { return &storage.at(index); }
    std::shared_ptr<Texture> TextureAt(std::size_t index) const { return std::shared_ptr<Texture>(std::shared_ptr<Texture>{}, reinterpret_cast<Texture*>(const_cast<std::byte*>(&storage.at(index)))); }
    const StorageTexture* SourceAt(std::size_t index) const { return reinterpret_cast<const StorageTexture*>(&storage.at(index)); }

private:
    std::vector<std::byte> storage;
};

TextureLookupRecord Record(const void* object, std::uint64_t generation) {
    GuestTextureResource resource{};
    resource.baseAddress = 0x100000000ull + generation * 256u;
    resource.width = static_cast<std::uint32_t>(generation % 4096u) + 1u;
    resource.height = 8;
    return {object, resource, generation * 3u, DccKeys::Uncompressed, generation, nullptr, false};
}

bool Same(const Validated& validated, const TextureLookupRecord& record) {
    return validated.valid && validated.resource.baseAddress == record.resource.baseAddress && validated.resource.width == record.resource.width && validated.resource.height == record.resource.height && validated.bytes == record.bytes && validated.keys == record.keys && validated.generation == record.generation && validated.collected == 0 && validated.source == record.source && validated.depth == record.depth;
}

bool Empty(const Validated& validated) {
    return !validated.valid && validated.resource.baseAddress == 0 && validated.resource.width == 0 && validated.bytes == 0 && validated.keys == DccKeys::Uncompressed && validated.generation == 0 && validated.collected == 0 && validated.source == nullptr && !validated.depth;
}

const TextureLookupRecord* Newest(std::span<const TextureLookupRecord> log, const void* object) {
    const TextureLookupRecord* found = nullptr;
    for (const auto& record : log) {
        if (record.object == object) found = &record;
    }
    return found;
}

std::vector<Validated> Capture(std::span<const TextureLookupRecord> log, std::span<const std::shared_ptr<Texture>> sampled) {
    std::vector<Validated> validated{Validated{}};
    validated.front().generation = 77;
    ShaderResourcesTestAccess::Capture(log, sampled, validated);
    Require(validated.size() == sampled.size(), "validation capture: " + std::to_string(validated.size()) + " records for " + std::to_string(sampled.size()) + " textures");
    return validated;
}

void RequireNewest(std::span<const TextureLookupRecord> log, std::span<const std::shared_ptr<Texture>> sampled, const std::vector<Validated>& validated, std::string_view label) {
    for (std::size_t i = 0; i < sampled.size(); ++i) {
        const auto* newest = Newest(log, sampled[i].get());
        const bool matched = newest != nullptr ? Same(validated[i], *newest) : Empty(validated[i]);
        Require(matched, "validation capture: " + std::string(label) + ": texture " + std::to_string(i) + (newest != nullptr ? " does not hold the newest record of its object, generation " + std::to_string(newest->generation) + ", but generation " + std::to_string(validated[i].generation) : " has no record in the log but holds one of generation " + std::to_string(validated[i].generation)));
    }
}

void PinnedCases() {
    constexpr std::size_t Distinct = 1500;
    const Objects objects(Distinct + 16);
    std::vector<TextureLookupRecord> log;
    log.push_back(Record(objects.At(5), 900001));
    log.push_back(Record(objects.At(9), 900003));
    log.push_back(Record(nullptr, 900004));
    for (std::size_t i = 0; i < Distinct; ++i) log.push_back(Record(objects.At(i), i + 1u));
    log[3 + 3].keys = DccKeys::Mixed;
    log[3 + 3].depth = true;
    log[3 + 3].source = objects.SourceAt(Distinct + 5);
    log.push_back(Record(objects.At(5), 900002));
    log.push_back(Record(objects.At(Distinct - 1), 900005));
    std::vector<std::shared_ptr<Texture>> sampled;
    for (std::size_t i = 0; i < Distinct; ++i) sampled.push_back(objects.TextureAt(i));
    sampled.push_back(objects.TextureAt(5));
    sampled.push_back(objects.TextureAt(Distinct + 1));
    sampled.push_back(nullptr);
    sampled.push_back(objects.TextureAt(9));
    const auto validated = Capture(log, sampled);
    RequireNewest(log, sampled, validated, "a build of 1500 textures past the old 1024 record bound");
    Require(validated[5].generation == 900002 && validated[Distinct].generation == 900002, "validation capture: an object logged before, inside and after the build did not take its newest record at both of its textures");
    Require(validated[9].generation == 10 && validated[Distinct + 3].generation == 10, "validation capture: an object logged again after an older record did not take the newer one");
    Require(validated[Distinct - 1].generation == 900005, "validation capture: the last object did not take the record logged after the build");
    Require(validated[3].valid && validated[3].depth && validated[3].keys == DccKeys::Mixed && validated[3].source == objects.SourceAt(Distinct + 5), "validation capture: a depth record's keys, source and depth flag were not kept");
    Require(Empty(validated[Distinct + 1]), "validation capture: a texture absent from the log got a record");
    Require(validated[Distinct + 2].valid && validated[Distinct + 2].generation == 900004, "validation capture: a null texture did not take the record of a null object");
    std::puts("a build past the old bound takes every object's newest record; absent textures stay empty");

    const std::array<TextureLookupRecord, 3> few{Record(objects.At(2), 11), Record(objects.At(1), 12), Record(objects.At(2), 13)};
    const std::array<std::shared_ptr<Texture>, 5> fewSampled{objects.TextureAt(1), objects.TextureAt(2), objects.TextureAt(3), objects.TextureAt(Distinct - 1), nullptr};
    const auto again = Capture(few, fewSampled);
    RequireNewest(few, fewSampled, again, "a three-record build after a large one on the same thread");
    Require(again[0].generation == 12 && again[1].generation == 13 && Empty(again[2]) && Empty(again[3]) && Empty(again[4]), "validation capture: a three-record build after a large one kept a record of the large build");

    const auto none = Capture({}, fewSampled);
    Require(std::ranges::all_of(none, Empty), "validation capture: an empty log gave a texture a record");
    Require(Capture(few, {}).empty(), "validation capture: a build without textures got records");

    constexpr std::size_t Large = 4096;
    const Objects many(Large);
    std::vector<TextureLookupRecord> largeLog;
    for (std::size_t i = 0; i < Large; ++i) largeLog.push_back(Record(many.At(Large - 1 - i), i + 1u));
    for (std::size_t i = 0; i < Large; i += 7) largeLog.push_back(Record(many.At(i), 1000000u + i));
    std::vector<std::shared_ptr<Texture>> largeSampled;
    for (std::size_t i = 0; i < Large; ++i) largeSampled.push_back(many.TextureAt(i));
    RequireNewest(largeLog, largeSampled, Capture(largeLog, largeSampled), "a build of 4096 textures with every seventh object logged twice");
    RequireNewest(few, fewSampled, Capture(few, fewSampled), "a three-record build after a build of 4096 textures");
    std::puts("captures on one thread keep no record of an earlier capture");
}

void Measure() {
    for (const std::size_t count : {std::size_t{16}, std::size_t{128}, std::size_t{2000}, std::size_t{4096}}) {
        const Objects objects(count);
        std::vector<TextureLookupRecord> log;
        std::vector<std::shared_ptr<Texture>> sampled;
        for (std::size_t i = 0; i < count; ++i) {
            log.push_back(Record(objects.At(i), i + 1u));
            sampled.push_back(objects.TextureAt(i));
        }
        std::vector<Validated> validated;
        for (int warm = 0; warm < 3; ++warm) ShaderResourcesTestAccess::Capture(log, sampled, validated);
        std::array<double, 5> samples{};
        for (auto& sample : samples) {
            std::size_t calls = 0;
            const auto start = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::steady_clock::duration::zero();
            while (calls < 3 || elapsed < std::chrono::milliseconds(50)) {
                ShaderResourcesTestAccess::Capture(log, sampled, validated);
                ++calls;
                elapsed = std::chrono::steady_clock::now() - start;
            }
            sample = std::chrono::duration<double, std::micro>(elapsed).count() / static_cast<double>(calls);
        }
        RequireNewest(log, sampled, validated, "the measured build");
        auto sorted = samples;
        std::ranges::sort(sorted);
        std::printf("capture of %zu textures: %.2f us per capture, median of 5 (samples %.2f %.2f %.2f %.2f %.2f)\n", count, sorted[2], samples[0], samples[1], samples[2], samples[3], samples[4]);
    }
}

}

int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc == 2 ? argv[1] : "";
        Require(argc == 1 || mode == "--measure", "validation capture: invalid test arguments");
        if (mode == "--measure") {
            Measure();
            return 0;
        }
        PinnedCases();
        std::puts("validation capture tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
