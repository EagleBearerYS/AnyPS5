#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::AddressReuse;
using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t PointerOffset = 0;
constexpr std::uint32_t StoreOffset = 8;
constexpr std::uint32_t ValueOffset = 16;
constexpr std::uint32_t Sentinel = 0xdeadbeefu;
constexpr std::uint32_t SpreadThreads = 1024;
constexpr std::uint32_t SpreadSpans = 3;
constexpr std::size_t ChunkBytes = std::size_t{1} << 17u;
constexpr std::size_t SpanBytes = SpreadThreads * ChunkBytes;
constexpr double FreeBoundSeconds = 10.0;

alignas(256) constexpr std::array<std::uint32_t, 10> ReaderCode{
    0x7e000280u,
    0xdc348000u, 0x02000000u,
    0xbf8c3f70u,
    0xdc308000u, 0x017d0002u,
    0xbf8c3f70u,
    0xdc708008u, 0x00000100u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 6> WriterCode{
    0x7e000280u, 0x7e040202u, 0x7e060203u,
    0xdc748000u, 0x00000200u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 9> SpreadCode{
    0x34020091u, 0x7e040300u,
    0xdc708000u, 0x00020201u,
    0xdc708000u, 0x00040201u,
    0xdc708000u, 0x00060201u,
    0xbf810000u,
};

constexpr std::size_t OverlayOffset = 16384;
constexpr std::uint32_t OverlayBytes = 16384;
constexpr std::size_t OverlayOutput = 64;

alignas(256) constexpr std::array<std::uint32_t, 8> OverlayCode{
    0x7e000280u,
    0xdc308000u, 0x01000000u,
    0xbf8c3f70u,
    0x4a020281u,
    0xe0700040u, 0x80010100u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 10> AtomicReaderCode{
    0x7e000280u,
    0xdc348000u, 0x02000000u,
    0xbf8c3f70u,
    0xdcc98000u, 0x017d0002u,
    0xbf8c3f70u,
    0xdc708008u, 0x00000100u,
    0xbf810000u,
};

std::uint64_t AddressOf(const void* data) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data));
}

class Pages {
public:
    explicit Pages(std::size_t bytes) : bytes(bytes) {
#ifdef _WIN32
        if (watched) {
            data = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, BlockBytes));
            if (data != nullptr) GuestArena::GuestArenaCommit_nid_postfix(data, bytes, PAGE_READWRITE, bytes);
            if (data != nullptr) std::memset(data, 0, bytes);
        } else {
            data = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        }
#else
        void* mapped = mmap(nullptr, bytes + BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        Require(mapped != MAP_FAILED, "address dispatch reuse: cannot map guest pages");
        const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
        const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
        if (aligned != begin) munmap(mapped, aligned - begin);
        if (aligned + bytes != begin + bytes + BlockBytes) munmap(reinterpret_cast<void*>(aligned + bytes), begin + BlockBytes - aligned);
        data = reinterpret_cast<std::uint8_t*>(aligned);
        if (watched) GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(data, bytes);
#endif
        Require(data != nullptr, "address dispatch reuse: cannot allocate guest pages");
    }

    ~Pages() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), bytes, nullptr, "test release");
        for (const auto offset : registered) GuestAllocations::Mutation().Remove(data + offset);
#ifdef _WIN32
        if (watched) {
            GuestArena::GuestArenaReset_nid_postfix(data, bytes);
            GuestArena::GuestArenaRelease_nid_postfix(data, bytes);
        } else {
            VirtualFree(data, 0, MEM_RELEASE);
        }
#else
        munmap(data, bytes);
        if (watched) GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(data, bytes);
#endif
    }

    Pages(const Pages&) = delete;
    Pages& operator=(const Pages&) = delete;

    void Register(std::size_t offset, std::size_t length, bool writable) {
        GuestAllocations::Mutation().Add(data + offset, length, true, writable, true);
        registered.push_back(offset);
    }

    std::uint64_t Address(std::size_t offset = 0) const { return AddressOf(data + offset); }
    std::uint8_t* Data() { return data; }
    std::size_t Bytes() const { return bytes; }

    void Store(std::size_t offset, std::uint64_t value, std::size_t length) {
        std::memcpy(data + offset, &value, length);
    }

    std::uint64_t Load(std::size_t offset, std::size_t length) const {
        std::uint64_t value = 0;
        std::memcpy(&value, data + offset, length);
        return value;
    }

private:
    std::size_t bytes;
    bool watched = AgcDriver::GuestMemory::WriteWatched();
    std::uint8_t* data = nullptr;
    std::vector<std::size_t> registered;
};

class StderrCapture {
public:
    StderrCapture() {
        std::fflush(stderr);
        file = std::tmpfile();
        Require(file != nullptr, "cannot open a temporary file for stderr");
#ifdef _WIN32
        saved = _dup(_fileno(stderr));
        _dup2(_fileno(file), _fileno(stderr));
#else
        saved = dup(fileno(stderr));
        dup2(fileno(file), fileno(stderr));
#endif
    }

    ~StderrCapture() {
        if (file != nullptr) static_cast<void>(Finish());
    }

    StderrCapture(const StderrCapture&) = delete;
    StderrCapture& operator=(const StderrCapture&) = delete;

    std::string Finish() {
        std::fflush(stderr);
#ifdef _WIN32
        _dup2(saved, _fileno(stderr));
        _close(saved);
#else
        dup2(saved, fileno(stderr));
        close(saved);
#endif
        std::rewind(file);
        std::string text;
        char buffer[4096];
        for (std::size_t read = 0; (read = std::fread(buffer, 1, sizeof(buffer), file)) != 0;) text.append(buffer, read);
        std::fclose(file);
        file = nullptr;
        std::fwrite(text.data(), 1, text.size(), stderr);
        return text;
    }

private:
    std::FILE* file = nullptr;
    int saved = -1;
};

std::uint64_t Builds() {
    const auto counters = AgcDriver::Graphics::BdaResources::TableCacheCounters();
    return counters.hits + counters.misses;
}

std::uint64_t Counted(const AgcDriver::Graphics::AddressReuseStats& stats, AddressReuse outcome) {
    return stats.outcomes[static_cast<std::size_t>(outcome)];
}

std::uint64_t Refusals(const AgcDriver::Graphics::AddressReuseStats& stats) {
    std::uint64_t refusals = 0;
    for (const auto count : stats.outcomes) refusals += count;
    return refusals - Counted(stats, AddressReuse::Reused);
}

std::size_t Occurrences(const std::string& text, const std::string& word) {
    std::size_t found = 0;
    for (auto at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size())) ++found;
    return found;
}

std::string FaultAddress(std::uint64_t address) {
    char text[64];
    std::snprintf(text, sizeof(text), "address=0x%llx ", static_cast<unsigned long long>(address));
    return text;
}

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, const std::vector<std::uint32_t>& userData, std::uint32_t threads) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{AddressOf(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, AddressOf(code.data()), code, 0, {}},
        {64, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

std::vector<std::uint32_t> Pointers(std::initializer_list<std::uint64_t> pointers) {
    std::vector<std::uint32_t> words;
    for (const auto pointer : pointers) {
        words.push_back(static_cast<std::uint32_t>(pointer));
        words.push_back(static_cast<std::uint32_t>(pointer >> 32u));
    }
    words.resize(8, 0u);
    return words;
}

struct Block {
    explicit Block(std::uint32_t value, bool writable = true) : value(value) {
        Point(pages.Address(ValueOffset));
        pages.Store(ValueOffset, value, sizeof(value));
        Clear();
        pages.Register(0, BlockBytes, writable);
    }

    void Point(std::uint64_t address) { pages.Store(PointerOffset, address, sizeof(address)); }
    void Clear() { pages.Store(StoreOffset, Sentinel, sizeof(Sentinel)); }
    std::uint32_t Stored() const { return static_cast<std::uint32_t>(pages.Load(StoreOffset, sizeof(std::uint32_t))); }
    std::uint64_t Pointer() const { return pages.Load(PointerOffset, sizeof(std::uint64_t)); }

    Pages pages{BlockBytes};
    std::uint32_t value;
};

struct Fixture {
    explicit Fixture(AgcDriver::VulkanDevice& device) : device(device) {
        codeSnapshot = {{{AddressOf(ReaderCode.data()), std::as_bytes(std::span(program))}}};
    }

    const ShaderRecompiler::RecompileResult& Reader(const Block& block) {
        const auto address = block.pages.Address();
        if (compiled == nullptr) {
            compiled = std::make_unique<ShaderRecompiler::RecompileResult>(Compile(device, ReaderCode, Pointers({address}), Threads));
            std::uint64_t pushed = 0;
            Require(compiled->pushConstants.size() >= sizeof(pushed), "address dispatch reuse: the reader keeps no user data in its push constants");
            std::memcpy(&pushed, compiled->pushConstants.data(), sizeof(pushed));
            Require(pushed == address, "address dispatch reuse: the reader's push constants do not begin with its user data pointer");
        }
        auto& result = readers[address];
        if (result == nullptr) {
            result = std::make_unique<ShaderRecompiler::RecompileResult>(*compiled);
            std::memcpy(result->pushConstants.data(), &address, sizeof(address));
        }
        return *result;
    }

    void Dispatch(const Block& block) {
        Dispatch(block, codeSnapshot);
    }

    void Dispatch(const Block& block, std::span<const AgcDriver::Graphics::GuestMemorySnapshot> snapshots) {
        const auto& result = Reader(block);
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        device.Dispatch(result, 1, 1, 1, snapshots, AddressOf(ReaderCode.data()));
    }

    void Wait() {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        device.WaitIdle();
    }

    AgcDriver::VulkanDevice& device;
    std::array<std::uint32_t, ReaderCode.size()> program = ReaderCode;
    std::array<AgcDriver::Graphics::GuestMemorySnapshot, 1> codeSnapshot{};
    std::unique_ptr<ShaderRecompiler::RecompileResult> compiled;
    std::map<std::uint64_t, std::unique_ptr<ShaderRecompiler::RecompileResult>> readers;
    Block first{0x1111u};
    Block second{0x2222u};
    Block third{0x3333u};
    Block readOnly{0x4444u, false};
    Block otherReadOnly{0x5555u, false};
};

bool Imported(Fixture& fixture) {
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    if (AgcDriver::Graphics::HostImportLiveBytes() == 0) return false;
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the first dispatch's store through the BDA table did not reach guest memory");
    return true;
}

void Repeated(Fixture& fixture) {
    constexpr std::uint64_t Repeats = 4;
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    auto before = AgcDriver::Graphics::AddressReuseCounters();
    auto built = Builds();
    for (std::uint64_t dispatch = 0; dispatch < Repeats; ++dispatch) {
        fixture.first.Clear();
        fixture.Dispatch(fixture.first);
        fixture.Wait();
        Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: reused dispatch " + std::to_string(dispatch) + " did not store through the BDA table");
    }
    auto after = AgcDriver::Graphics::AddressReuseCounters();
    const auto reused = Counted(after, AddressReuse::Reused) - Counted(before, AddressReuse::Reused);
    Require(Builds() == built, "address dispatch reuse: " + std::to_string(Builds() - built) + " of " + std::to_string(Repeats) + " repeated address-based dispatches with unchanged inputs built their resources again");
    Require(reused == Repeats && after.proofs - before.proofs == Repeats && Refusals(after) == Refusals(before), "address dispatch reuse: " + std::to_string(Repeats) + " repeated dispatches with unchanged inputs counted " + std::to_string(reused) + " reuses, " + std::to_string(after.proofs - before.proofs) + " proofs and " + std::to_string(Refusals(after) - Refusals(before)) + " refusals");
    before = after;
    built = Builds();
    fixture.first.Clear();
    fixture.second.Clear();
    for (std::uint64_t dispatch = 0; dispatch < Repeats; ++dispatch) fixture.Dispatch(dispatch % 2 == 0 ? fixture.first : fixture.second);
    fixture.Wait();
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(fixture.first.Stored() == fixture.first.value && fixture.second.Stored() == fixture.second.value, "address dispatch reuse: dispatches of one program over two blocks in one batch did not each read their own block");
    Require(Builds() == built && Counted(after, AddressReuse::Reused) - Counted(before, AddressReuse::Reused) == Repeats && Refusals(after) == Refusals(before), "address dispatch reuse: " + std::to_string(Repeats) + " dispatches of one program in one batch were not all reused (" + std::to_string(Counted(after, AddressReuse::Reused) - Counted(before, AddressReuse::Reused)) + " reuses, " + std::to_string(Refusals(after) - Refusals(before)) + " refusals, " + std::to_string(Builds() - built) + " builds)");
}

void ChangedSnapshot(Fixture& fixture) {
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    auto before = AgcDriver::Graphics::AddressReuseCounters();
    auto built = Builds();
    fixture.program.back() ^= 1u;
    fixture.first.Clear();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the dispatch after a changed snapshot word did not store through the BDA table");
    Require(Builds() == built + 1, "address dispatch reuse: a dispatch whose program snapshot changed a word did not build its resources again");
    Require(Counted(after, AddressReuse::Snapshots) == Counted(before, AddressReuse::Snapshots) + 1 && Refusals(after) == Refusals(before) + 1 && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused), "address dispatch reuse: a dispatch whose program snapshot changed a word was not counted as one refusal for a changed snapshot");
    fixture.program = ReaderCode;
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    before = AgcDriver::Graphics::AddressReuseCounters();
    built = Builds();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Builds() == built && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the build after the snapshot was restored was not reused");
}

void OwnFaults(Fixture& fixture) {
    const auto one = fixture.readOnly.pages.Address(StoreOffset);
    const auto two = fixture.otherReadOnly.pages.Address(StoreOffset);
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    fixture.first.Clear();
    auto before = AgcDriver::Graphics::AddressReuseCounters();
    auto built = Builds();
    std::string text;
    {
        StderrCapture log;
        fixture.Dispatch(fixture.first);
        fixture.Dispatch(fixture.readOnly);
        fixture.Wait();
        text = log.Finish();
    }
    auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Builds() == built && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 2, "address dispatch reuse: two dispatches of one program in one batch were not both reused");
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the dispatch that did not fault lost its store when the other dispatch of its batch faulted");
    Require(Occurrences(text, "BDA access failed") == 1 && Occurrences(text, FaultAddress(one)) == 1, "address dispatch reuse: of two reused dispatches in one batch, the one storing into read-only memory did not report exactly its own fault once:\n" + text);
    fixture.first.Clear();
    before = after;
    built = Builds();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(fixture.first.Stored() == fixture.first.value && Builds() == built + 1, "address dispatch reuse: a build whose dispatch faulted served the next dispatch");
    Require(Counted(after, AddressReuse::NotReusable) == Counted(before, AddressReuse::NotReusable) + 1, "address dispatch reuse: the dispatch after a fault was not counted as one refusal of a build that is no longer reusable");
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    before = AgcDriver::Graphics::AddressReuseCounters();
    built = Builds();
    {
        StderrCapture log;
        fixture.Dispatch(fixture.readOnly);
        fixture.Dispatch(fixture.otherReadOnly);
        fixture.Wait();
        text = log.Finish();
    }
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Builds() == built && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 2, "address dispatch reuse: two faulting dispatches of one program in one batch were not both reused");
    Require(Occurrences(text, "BDA access failed") == 2 && Occurrences(text, FaultAddress(one)) == 1 && Occurrences(text, FaultAddress(two)) == 1, "address dispatch reuse: two reused dispatches in one batch storing into different read-only blocks did not each report their own fault:\n" + text);
    fixture.Dispatch(fixture.first);
    fixture.Wait();
}

void PendingSrtWrites(Fixture& fixture) {
    const auto writer = Compile(fixture.device, WriterCode, Pointers({fixture.first.pages.Address(PointerOffset), fixture.second.pages.Address(ValueOffset)}), Threads);
    std::array<std::uint64_t, 1> captured{fixture.first.Pointer()};
    const std::array<AgcDriver::Graphics::GuestMemorySnapshot, 2> snapshots{{
        {AddressOf(ReaderCode.data()), std::as_bytes(std::span(fixture.program))},
        {fixture.first.pages.Address(PointerOffset), std::as_bytes(std::span(captured))},
    }};
    fixture.Dispatch(fixture.first, snapshots);
    fixture.Wait();
    fixture.Dispatch(fixture.first, snapshots);
    fixture.Wait();
    fixture.first.Clear();
    {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        fixture.device.Dispatch(writer, 1, 1, 1, {}, AddressOf(WriterCode.data()));
        fixture.device.Dispatch(fixture.Reader(fixture.first), 1, 1, 1, snapshots, AddressOf(ReaderCode.data()));
    }
    fixture.Wait();
    Require(fixture.first.Pointer() == fixture.second.pages.Address(ValueOffset), "address dispatch reuse: the dispatch writing the SRT did not store its pointer");
    Require(fixture.first.Stored() == fixture.second.value, "address dispatch reuse: a reused dispatch after a dispatch rewrote its SRT in the same batch read the old SRT");
    fixture.first.Point(fixture.first.pages.Address(ValueOffset));
    fixture.Dispatch(fixture.first, snapshots);
    fixture.Wait();
    fixture.Dispatch(fixture.first, snapshots);
    fixture.Wait();
    const auto rewritten = fixture.second.pages.Address(ValueOffset);
    const std::array<std::uint32_t, 4> pattern{static_cast<std::uint32_t>(rewritten), static_cast<std::uint32_t>(rewritten >> 32u), 0u, 0u};
    const auto before = AgcDriver::Graphics::AddressReuseCounters();
    const auto built = Builds();
    {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        Require(fixture.device.FillBuffer(fixture.first.pages.Address(PointerOffset), 16, pattern), "address dispatch reuse: the fill over the SRT was not recorded on the GPU");
        fixture.device.Dispatch(fixture.Reader(fixture.first), 1, 1, 1, snapshots, AddressOf(ReaderCode.data()));
    }
    fixture.Wait();
    const auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(fixture.first.Stored() == fixture.second.value, "address dispatch reuse: a dispatch after a recorded fill rewrote its SRT read the old SRT");
    Require(Counted(after, AddressReuse::Snapshots) == Counted(before, AddressReuse::Snapshots) + 1 && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) && Builds() == built + 1, "address dispatch reuse: a dispatch whose captured SRT a pending GPU write changed was not refused for a changed snapshot and rebuilt (" + std::to_string(Counted(after, AddressReuse::Reused) - Counted(before, AddressReuse::Reused)) + " reuses, " + std::to_string(Builds() - built) + " builds)");
    fixture.first.Point(fixture.first.pages.Address(ValueOffset));
    fixture.Dispatch(fixture.first);
    fixture.Wait();
}

double Free(std::unique_ptr<Pages>& pages) {
    const auto start = std::chrono::steady_clock::now();
    pages.reset();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

void GuestFree(Fixture& fixture) {
    auto extra = std::make_unique<Pages>(BlockBytes);
    extra->Register(0, BlockBytes, true);
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    auto before = AgcDriver::Graphics::AddressReuseCounters();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the dispatch before the guest free was not reused");
    auto seconds = Free(extra);
    Require(seconds < FreeBoundSeconds, "address dispatch reuse: a guest free while a reused dispatch build was cached took " + std::to_string(seconds) + " s");
    fixture.first.Clear();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the dispatch after a guest free did not store through the BDA table");
    extra = std::make_unique<Pages>(BlockBytes);
    extra->Register(0, BlockBytes, true);
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    before = AgcDriver::Graphics::AddressReuseCounters();
    fixture.first.Clear();
    fixture.Dispatch(fixture.first);
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the dispatch in flight during the guest free was not reused");
    seconds = Free(extra);
    Require(seconds < FreeBoundSeconds, "address dispatch reuse: a guest free while a reused dispatch was in flight took " + std::to_string(seconds) + " s");
    fixture.Wait();
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the reused dispatch in flight during a guest free did not store through the BDA table");
    fixture.first.Clear();
    fixture.Dispatch(fixture.first);
    fixture.Wait();
    Require(fixture.first.Stored() == fixture.first.value, "address dispatch reuse: the dispatch after a guest free during a reused dispatch did not store through the BDA table");
}

void UnalignedAtomic(Fixture& fixture) {
    auto& block = fixture.third;
    const auto program = Compile(fixture.device, AtomicReaderCode, Pointers({block.pages.Address()}), Threads);
    const auto dispatch = [&] {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        fixture.device.Dispatch(program, 1, 1, 1, {}, AddressOf(AtomicReaderCode.data()));
    };
    dispatch();
    fixture.Wait();
    dispatch();
    fixture.Wait();
    Require(block.Stored() == block.value, "address dispatch reuse: the atomic reader did not store the word its pointer names");
    block.Point(block.pages.Address(ValueOffset) + 2);
    block.Clear();
    const auto store = block.pages.Address(StoreOffset);
    const auto collected = AgcDriver::GuestMemory::Watched(store, sizeof(std::uint32_t)) ? AgcDriver::GuestMemory::CollectWrites(store, sizeof(std::uint32_t)) : 0;
    const auto before = AgcDriver::Graphics::AddressReuseCounters();
    std::string text;
    {
        StderrCapture log;
        dispatch();
        fixture.Wait();
        text = log.Finish();
    }
    const auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the dispatch whose atomic is unaligned was not reused");
    Require(Occurrences(text, "BDA access failed") == 1, "address dispatch reuse: the reused dispatch's unaligned atomic was not reported once:\n" + text);
    Require(block.Stored() == 0, "address dispatch reuse: the reused dispatch whose atomic is unaligned did not store the atomic's zero");
    if (collected != 0) Require(!AgcDriver::GuestMemory::UnchangedSince(store, sizeof(std::uint32_t), collected), "address dispatch reuse: a reused dispatch that reported an unaligned atomic left its store unstamped");
    block.Point(block.pages.Address(ValueOffset));
    const auto built = Builds();
    dispatch();
    fixture.Wait();
    Require(block.Stored() == block.value && Builds() == built + 1, "address dispatch reuse: a build whose dispatch reported an unaligned atomic served the next dispatch");
}

std::string Hex(std::uint64_t value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
    return text;
}

void OverlayReuse(Fixture& fixture) {
    Pages pages{BlockBytes};
    pages.Register(0, BlockBytes, true);
    const auto element = pages.Address(OverlayOffset);
    const auto counter = element + OverlayOutput;
    std::vector<std::uint32_t> userData{static_cast<std::uint32_t>(counter), static_cast<std::uint32_t>(counter >> 32u), 0u, 0u, static_cast<std::uint32_t>(element), static_cast<std::uint32_t>((element >> 32u) & 0xffffu), OverlayBytes, 0x31016facu};
    const auto program = Compile(fixture.device, OverlayCode, userData, 1);
    const auto dispatch = [&] {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        fixture.device.Dispatch(program, 1, 1, 1, {}, AddressOf(OverlayCode.data()));
    };
    const auto value = [&] { return pages.Load(OverlayOffset + OverlayOutput, sizeof(std::uint32_t)); };
    const auto staged = "[staging] 0x" + Hex(element) + "+0x" + Hex(OverlayBytes) + " ";
    pages.Store(OverlayOffset + OverlayOutput, 0x100u, sizeof(std::uint32_t));
    const auto before = AgcDriver::Graphics::AddressReuseCounters();
    const auto built = Builds();
    std::string text;
    {
        StderrCapture log;
        dispatch();
        fixture.Wait();
        text = log.Finish();
    }
    Require(text.find(staged) != std::string::npos, "address dispatch reuse: the written element of the address-based build was not staged as an overlay of its import:\n" + text);
    Require(value() == 0x101u, "address dispatch reuse: the staged element's first dispatch left 0x" + Hex(value()) + " in guest memory, expected 0x101");
    pages.Store(OverlayOffset + OverlayOutput, 0x200u, sizeof(std::uint32_t));
    dispatch();
    fixture.Wait();
    auto after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Builds() == built + 1 && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the second dispatch of the build with a staged element was not reused (" + std::to_string(Counted(after, AddressReuse::Reused) - Counted(before, AddressReuse::Reused)) + " reuses, " + std::to_string(Builds() - built) + " builds)");
    Require(value() == 0x201u, "address dispatch reuse: the reused dispatch of a staged element left 0x" + Hex(value()) + " in guest memory, expected 0x201 (0x102: its copy-in was not repeated; 0x200: its write was not copied back)");
    dispatch();
    dispatch();
    fixture.Wait();
    after = AgcDriver::Graphics::AddressReuseCounters();
    Require(Builds() == built + 1 && Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 3, "address dispatch reuse: two dispatches in one batch of the build with a staged element were not both reused");
    Require(value() == 0x203u, "address dispatch reuse: two reused dispatches in one batch of a staged element left 0x" + Hex(value()) + " in guest memory, expected 0x203 (0x202: the second copy-in did not follow the first copy-back)");
}

void Overflow(AgcDriver::VulkanDevice& device) {
    Pages spread{SpreadSpans * SpanBytes};
    spread.Register(0, spread.Bytes(), true);
    const auto base = spread.Address();
    const auto program = Compile(device, SpreadCode, Pointers({0, base, base + SpanBytes, base + 2 * SpanBytes}), SpreadThreads);
    const auto dispatch = [&] {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        device.Dispatch(program, 1, 1, 1, {}, AddressOf(SpreadCode.data()));
        device.WaitIdle();
    };
    for (int use = 0; use < 2; ++use) {
        for (std::uint32_t span = 0; span < SpreadSpans; ++span) {
            for (std::uint32_t thread = 0; thread < SpreadThreads; ++thread) spread.Store(span * SpanBytes + thread * ChunkBytes, Sentinel, sizeof(Sentinel));
        }
        const auto collected = AgcDriver::GuestMemory::Watched(base, spread.Bytes()) ? AgcDriver::GuestMemory::CollectWrites(base, spread.Bytes()) : 0;
        const auto before = AgcDriver::Graphics::AddressReuseCounters();
        std::string text;
        {
            StderrCapture log;
            dispatch();
            text = log.Finish();
        }
        const auto after = AgcDriver::Graphics::AddressReuseCounters();
        Require(text.find("failed") == std::string::npos, "address dispatch reuse: a dispatch storing to " + std::to_string(SpreadSpans * SpreadThreads) + " chunks of its written-page record failed:\n" + text);
        for (std::uint32_t span = 0; span < SpreadSpans; ++span) {
            for (std::uint32_t thread = 0; thread < SpreadThreads; ++thread) {
                const auto offset = span * SpanBytes + thread * ChunkBytes;
                Require(spread.Load(offset, sizeof(std::uint32_t)) == thread, "address dispatch reuse: thread " + std::to_string(thread) + " of the overflowing dispatch did not store into span " + std::to_string(span));
                if (collected != 0) Require(!AgcDriver::GuestMemory::UnchangedSince(base + offset, sizeof(std::uint32_t), collected), "address dispatch reuse: a page the overflowing dispatch stored to is not stamped as written");
            }
        }
        if (use == 1) Require(Counted(after, AddressReuse::Reused) == Counted(before, AddressReuse::Reused) + 1, "address dispatch reuse: the build whose written-page record overflowed was not reused");
    }
}

}

int main(int argc, char** argv) {
    if (std::getenv("APS5_NO_ADDRESS_DISPATCH_REUSE") != nullptr) {
        std::puts("skipped, APS5_NO_ADDRESS_DISPATCH_REUSE=1 turns address-based dispatch reuse off");
        return VulkanTestSkipped;
    }
#ifdef _WIN32
    _putenv_s("APS5_TRACE_STAGING", "1");
#else
    setenv("APS5_TRACE_STAGING", "1", 1);
#endif
    const std::string only = argc > 1 ? argv[1] : "";
    const auto runs = [&](const char* name) { return only.empty() || only == name; };
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        {
            Fixture fixture(*device);
            if (!Imported(fixture)) {
                std::puts("skipped, host imports are unavailable: a store through the BDA table needs one");
                return VulkanTestSkipped;
            }
            if (runs("repeated")) Repeated(fixture);
            if (runs("snapshot")) ChangedSnapshot(fixture);
            if (runs("faults")) OwnFaults(fixture);
            if (runs("srt")) PendingSrtWrites(fixture);
            if (runs("free")) GuestFree(fixture);
            if (runs("unaligned")) UnalignedAtomic(fixture);
            if (runs("overlay")) OverlayReuse(fixture);
            fixture.Wait();
        }
        if (runs("overflow")) Overflow(*device);
        std::printf("address dispatch reuse tests passed%s%s\n", only.empty() ? "" : ": ", only.c_str());
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
