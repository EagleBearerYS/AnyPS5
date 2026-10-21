#include "prx/libc/include/BigAllocations.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include "prx/libc/include/exceptions/Unwind.hpp"
extern "C" {
_Unwind_Reason_Code APS5_VABI _Unwind_Backtrace_nid_postfix(_Unwind_Trace_Fn, void*);
_Unwind_Ptr APS5_VABI _Unwind_GetIP_nid_postfix(_Unwind_Context*);
}
#else
#include <dlfcn.h>
#include <execinfo.h>
#endif

namespace {

constexpr std::size_t Threshold = std::size_t{512} << 10;
constexpr std::size_t Depth = 16;
constexpr std::size_t UnwoundEnough = 3;
constexpr std::size_t ScanBytes = std::size_t{32} << 10;
constexpr std::size_t CallBytes = 7;
constexpr std::size_t TopStacks = 12;
constexpr std::size_t Dumped = 3;
constexpr std::size_t DumpWords = 8;
constexpr std::size_t FilterBuckets = std::size_t{1} << 16;
constexpr std::size_t MiB = std::size_t{1} << 20;
constexpr std::size_t ClassSlack = std::size_t{64} << 10;

bool enabled() {
    static const bool value = std::getenv("APS5_PROFILE_BIG_ALLOCS") != nullptr;
    return value;
}

struct Stack {
    std::array<void*, Depth> frames{};
    std::size_t depth = 0;
    bool scanned = false;

    bool operator==(const Stack& other) const {
        return depth == other.depth && scanned == other.scanned && std::equal(frames.begin(), frames.begin() + static_cast<std::ptrdiff_t>(depth), other.frames.begin());
    }
};

struct StackHash {
    std::size_t operator()(const Stack& stack) const {
        std::uint64_t hash = stack.scanned ? 1099511628211ull : 14695981039346656037ull;
        for (std::size_t index = 0; index < stack.depth; ++index) hash = (hash ^ reinterpret_cast<std::uintptr_t>(stack.frames[index])) * 1099511628211ull;
        return static_cast<std::size_t>(hash);
    }
};

struct Site {
    Stack stack;
    std::uint64_t liveBytes = 0;
    std::uint64_t liveCount = 0;
    std::uint64_t allocated = 0;
    std::uint64_t freed = 0;
    std::array<std::uint64_t, 3> sizes{};
};

struct Block {
    std::size_t site = 0;
    std::size_t bytes = 0;
    std::uint64_t serial = 0;
};

struct Tracker {
    std::mutex mutex;
    std::atomic<std::thread::id> owner{};
    std::unordered_map<Stack, std::size_t, StackHash> siteIndex;
    std::vector<Site> sites;
    std::unordered_map<std::uintptr_t, Block> blocks;
    std::uint64_t serial = 0;
    std::uint64_t liveBytes = 0;
    std::uint64_t liveCount = 0;
    std::uint64_t allocatedBytes = 0;
    std::uint64_t allocatedCount = 0;
    std::uint64_t freedBytes = 0;
    std::uint64_t freedCount = 0;
    std::uint64_t stale = 0;
    std::array<std::atomic<std::uint32_t>, FilterBuckets> filter{};
};

Tracker& tracker() {
    static Tracker* const instance = new Tracker;
    return *instance;
}

class Owned {
public:
    explicit Owned(Tracker& state) : state(state) {
        state.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
    }
    ~Owned() {
        state.owner.store(std::thread::id{}, std::memory_order_relaxed);
    }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;

private:
    Tracker& state;
};

bool reentered(const Tracker& state) {
    return state.owner.load(std::memory_order_relaxed) == std::this_thread::get_id();
}

std::size_t bucket(std::uintptr_t address) {
    return static_cast<std::size_t>(((address >> 4) * 0x9E3779B97F4A7C15ull) >> 48);
}

std::size_t sizeClass(std::size_t bytes) {
    if (bytes > MiB - ClassSlack && bytes <= MiB) return 0;
    if (bytes > 2 * MiB - ClassSlack && bytes <= 2 * MiB) return 1;
    return 2;
}

double mebibytes(std::uint64_t bytes) {
    return static_cast<double>(bytes) / static_cast<double>(MiB);
}

[[gnu::format(printf, 2, 3)]] void append(std::string& text, const char* format, ...) {
    std::array<char, 1024> buffer{};
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
    va_end(arguments);
    if (length > 0) text.append(buffer.data(), std::min(static_cast<std::size_t>(length), buffer.size() - 1));
}

void print(const std::string& text) {
    if (text.empty()) return;
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
}

void dropStale(Tracker& state, const Block& block) {
    auto& site = state.sites[block.site];
    site.liveBytes -= block.bytes;
    --site.liveCount;
    --site.sizes[sizeClass(block.bytes)];
    state.liveBytes -= block.bytes;
    --state.liveCount;
    ++state.stale;
}

thread_local bool capturing = false;

void* const* returnSlot(void* frame, void* returnAddress) {
    const auto slot = static_cast<void* const*>(frame) + 1;
    return *slot == returnAddress ? slot : nullptr;
}

#ifdef _WIN32

struct CodeRange {
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;

    bool operator==(const CodeRange& other) const {
        return begin == other.begin && end == other.end;
    }
};

std::atomic<const std::vector<CodeRange>*> codeRanges{nullptr};

std::vector<std::unique_ptr<const std::vector<CodeRange>>>& publishedCodeRanges() {
    static auto* const published = new std::vector<std::unique_ptr<const std::vector<CodeRange>>>;
    return *published;
}

void addCodeRanges(HMODULE module, std::vector<CodeRange>& ranges) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;
        const auto begin = reinterpret_cast<std::uintptr_t>(base) + section->VirtualAddress;
        const auto size = section->Misc.VirtualSize != 0 ? section->Misc.VirtualSize : section->SizeOfRawData;
        if (size != 0) ranges.push_back({begin, begin + size});
    }
}

void publishCodeRanges(const std::vector<HMODULE>& modules) {
    std::vector<CodeRange> ranges;
    for (const auto module : modules) addCodeRanges(module, ranges);
    std::sort(ranges.begin(), ranges.end(), [](const CodeRange& left, const CodeRange& right) { return left.begin < right.begin; });
    const auto* current = codeRanges.load(std::memory_order_acquire);
    if (current != nullptr && *current == ranges) return;
    auto& published = publishedCodeRanges();
    published.push_back(std::make_unique<const std::vector<CodeRange>>(std::move(ranges)));
    codeRanges.store(published.back().get(), std::memory_order_release);
}

std::size_t indirectCallLength(const std::uint8_t* modrm, std::size_t available) {
    const unsigned mod = modrm[0] >> 6u;
    const unsigned rm = modrm[0] & 7u;
    if (mod == 3) return 2;
    std::size_t length = 2;
    if (rm == 4) {
        if (available < 2) return 0;
        ++length;
        if (mod == 0 && (modrm[1] & 7u) == 5) length += 4;
    } else if (mod == 0 && rm == 5) {
        length += 4;
    }
    if (mod == 1) length += 1;
    if (mod == 2) length += 4;
    return length;
}

bool callEndsAt(const std::uint8_t* end) {
    if (end[-5] == 0xE8) return true;
    for (std::size_t length = 2; length <= CallBytes; ++length) {
        const auto* opcode = end - length;
        if (opcode[0] != 0xFF || (opcode[1] >> 3u & 7u) != 2) continue;
        if (indirectCallLength(opcode + 1, length - 1) == length) return true;
    }
    return false;
}

bool readableCode(DWORD protect) {
    return (protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 && (protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0;
}

bool followsCall(std::uintptr_t address) {
    const auto* ranges = codeRanges.load(std::memory_order_acquire);
    if (ranges == nullptr) return false;
    const auto found = std::upper_bound(ranges->begin(), ranges->end(), address, [](std::uintptr_t value, const CodeRange& range) { return value < range.begin; });
    if (found == ranges->begin()) return false;
    const auto& range = *std::prev(found);
    if (address < range.begin + CallBytes || address > range.end) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(reinterpret_cast<const void*>(address - CallBytes), &region, sizeof(region)) == 0) return false;
    if (region.State != MEM_COMMIT || !readableCode(region.Protect) || reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize < address) return false;
    return callEndsAt(reinterpret_cast<const std::uint8_t*>(address));
}

void scanStack(Stack& stack, void* const* slot, unsigned drop) {
    const auto* thread = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto low = reinterpret_cast<std::uintptr_t>(thread->StackLimit);
    const auto high = reinterpret_cast<std::uintptr_t>(thread->StackBase);
    const auto begin = reinterpret_cast<std::uintptr_t>(slot);
    if (begin < low || begin >= high || begin % sizeof(void*) != 0) return;
    const auto end = std::min(high, begin + ScanBytes);
    stack.scanned = true;
    for (auto at = begin; at + sizeof(void*) <= end && stack.depth < Depth; at += sizeof(void*)) {
        const auto value = *reinterpret_cast<const std::uintptr_t*>(at);
        if (!followsCall(value)) continue;
        if (drop != 0) {
            --drop;
            continue;
        }
        stack.frames[stack.depth++] = reinterpret_cast<void*>(value);
    }
}

struct Walk {
    Stack* stack = nullptr;
    unsigned skip = 0;
};

_Unwind_Reason_Code APS5_VABI traceFrame(_Unwind_Context* context, void* argument) {
    auto& walk = *static_cast<Walk*>(argument);
    const auto address = _Unwind_GetIP_nid_postfix(context);
    if (address == 0) return _URC_END_OF_STACK;
    if (walk.skip != 0) {
        --walk.skip;
        return _URC_NO_REASON;
    }
    walk.stack->frames[walk.stack->depth++] = reinterpret_cast<void*>(address);
    return walk.stack->depth < Depth ? _URC_NO_REASON : _URC_NORMAL_STOP;
}

[[gnu::noinline]] void captureStack(Stack& stack, unsigned skip, void* const* slot) {
    Walk walk{&stack, skip + 2};
    _Unwind_Backtrace_nid_postfix(&traceFrame, &walk);
    if (stack.depth >= UnwoundEnough || slot == nullptr) return;
    Stack scanned;
    scanStack(scanned, slot, skip - 1);
    if (scanned.depth > stack.depth) stack = scanned;
}

#else

[[gnu::noinline]] void captureStack(Stack& stack, unsigned skip, void* const*) {
    std::array<void*, Depth + 4> raw{};
    const auto wanted = static_cast<int>(std::min<std::size_t>(raw.size(), Depth + skip + 2));
    const auto captured = static_cast<std::size_t>(std::max(0, backtrace(raw.data(), wanted)));
    const auto first = std::min<std::size_t>(captured, skip + 2);
    stack.depth = std::min(Depth, captured - first);
    std::copy_n(raw.begin() + static_cast<std::ptrdiff_t>(first), stack.depth, stack.frames.begin());
}

#endif

[[gnu::noinline]] void record(void* block, std::size_t bytes, unsigned skip, void* const* slot) {
    if (block == nullptr || bytes < Threshold || !enabled() || capturing) return;
    auto& state = tracker();
    if (reentered(state)) return;
    Stack stack;
    capturing = true;
    captureStack(stack, skip, slot);
    capturing = false;
    try {
        std::lock_guard lock(state.mutex);
        Owned owned(state);
        auto site = state.siteIndex.find(stack);
        if (site == state.siteIndex.end()) {
            state.sites.push_back(Site{stack});
            site = state.siteIndex.emplace(stack, state.sites.size() - 1).first;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(block);
        const auto [entry, fresh] = state.blocks.try_emplace(address);
        if (!fresh) dropStale(state, entry->second);
        entry->second = Block{site->second, bytes, ++state.serial};
        auto& counts = state.sites[site->second];
        counts.liveBytes += bytes;
        ++counts.liveCount;
        ++counts.allocated;
        ++counts.sizes[sizeClass(bytes)];
        state.liveBytes += bytes;
        ++state.liveCount;
        state.allocatedBytes += bytes;
        ++state.allocatedCount;
        if (fresh) state.filter[bucket(address)].fetch_add(1, std::memory_order_release);
    } catch (...) {
    }
}

std::size_t forget(void* block) noexcept {
    if (block == nullptr || !enabled()) return 0;
    auto& state = tracker();
    const auto address = reinterpret_cast<std::uintptr_t>(block);
    if (state.filter[bucket(address)].load(std::memory_order_acquire) == 0 || reentered(state)) return 0;
    try {
        std::lock_guard lock(state.mutex);
        Owned owned(state);
        const auto found = state.blocks.find(address);
        if (found == state.blocks.end()) return 0;
        const auto bytes = found->second.bytes;
        auto& site = state.sites[found->second.site];
        site.liveBytes -= bytes;
        --site.liveCount;
        ++site.freed;
        --site.sizes[sizeClass(bytes)];
        state.liveBytes -= bytes;
        --state.liveCount;
        state.freedBytes += bytes;
        ++state.freedCount;
        state.blocks.erase(found);
        state.filter[bucket(address)].fetch_sub(1, std::memory_order_release);
        return bytes;
    } catch (...) {
        return 0;
    }
}

#ifdef _WIN32

std::string utf8(const wchar_t* text, int length) {
    std::array<char, MAX_PATH * 3> buffer{};
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, text, length, buffer.data(), static_cast<int>(buffer.size()), nullptr, nullptr);
    return bytes > 0 ? std::string(buffer.data(), static_cast<std::size_t>(bytes)) : std::string("?");
}

std::string baseName(const wchar_t* path, DWORD length) {
    const wchar_t* name = path;
    for (DWORD index = 0; index < length; ++index) {
        if (path[index] == L'\\' || path[index] == L'/') name = path + index + 1;
    }
    return utf8(name, static_cast<int>(path + length - name));
}

#endif

std::string frameName(void* address) {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    std::string text;
#ifdef _WIN32
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) && module != nullptr) {
        std::array<wchar_t, MAX_PATH> path{};
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length != 0 && length < path.size()) {
            text = baseName(path.data(), length);
            append(text, "+0x%llx", static_cast<unsigned long long>(value - reinterpret_cast<std::uintptr_t>(module)));
            return text;
        }
    }
#else
    Dl_info info{};
    if (dladdr(address, &info) != 0 && info.dli_fname != nullptr && info.dli_fbase != nullptr) {
        const char* name = std::strrchr(info.dli_fname, '/');
        text = name != nullptr ? name + 1 : info.dli_fname;
        append(text, "+0x%llx", static_cast<unsigned long long>(value - reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
        return text;
    }
#endif
    append(text, "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

#ifdef _WIN32

using MallocFunction = void* (*)(std::size_t);
using CallocFunction = void* (*)(std::size_t, std::size_t);
using ReallocFunction = void* (*)(void*, std::size_t);
using FreeFunction = void (*)(void*);
using AlignedMallocFunction = void* (*)(std::size_t, std::size_t);
using AlignedReallocFunction = void* (*)(void*, std::size_t, std::size_t);
using LoadAFunction = HMODULE(WINAPI*)(LPCSTR);
using LoadWFunction = HMODULE(WINAPI*)(LPCWSTR);
using LoadExAFunction = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
using LoadExWFunction = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);

enum Import : std::size_t {
    Malloc,
    Calloc,
    Realloc,
    Free,
    AlignedMalloc,
    AlignedRealloc,
    AlignedFree,
    LoadA,
    LoadW,
    LoadExA,
    LoadExW,
    Imports
};

constexpr std::array<const char*, Imports> ImportNames{"malloc", "calloc", "realloc", "free", "_aligned_malloc", "_aligned_realloc", "_aligned_free", "LoadLibraryA", "LoadLibraryW", "LoadLibraryExA", "LoadLibraryExW"};
std::array<std::atomic<void*>, Imports> originals{};
std::atomic<std::uint64_t> hookedSlots{0};
std::atomic<std::uint64_t> hookedModules{0};

template<typename TFunction>
TFunction original(Import import) {
    return reinterpret_cast<TFunction>(originals[import].load(std::memory_order_acquire));
}

std::size_t multiplied(std::size_t count, std::size_t size) {
    return size != 0 && count > SIZE_MAX / size ? SIZE_MAX : count * size;
}

void patchModules(bool wait);

void* hookMalloc(std::size_t bytes) {
    void* block = original<MallocFunction>(Malloc)(bytes);
    record(block, bytes, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    return block;
}

void* hookCalloc(std::size_t count, std::size_t size) {
    void* block = original<CallocFunction>(Calloc)(count, size);
    record(block, multiplied(count, size), 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    return block;
}

void* hookRealloc(void* block, std::size_t bytes) {
    const auto previous = forget(block);
    void* moved = original<ReallocFunction>(Realloc)(block, bytes);
    if (moved != nullptr) {
        record(moved, bytes, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    } else if (bytes != 0) {
        record(block, previous, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    }
    return moved;
}

void hookFree(void* block) {
    forget(block);
    original<FreeFunction>(Free)(block);
}

void* hookAlignedMalloc(std::size_t bytes, std::size_t alignment) {
    void* block = original<AlignedMallocFunction>(AlignedMalloc)(bytes, alignment);
    record(block, bytes, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    return block;
}

void* hookAlignedRealloc(void* block, std::size_t bytes, std::size_t alignment) {
    const auto previous = forget(block);
    void* moved = original<AlignedReallocFunction>(AlignedRealloc)(block, bytes, alignment);
    if (moved != nullptr) {
        record(moved, bytes, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    } else if (bytes != 0) {
        record(block, previous, 1, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    }
    return moved;
}

void hookAlignedFree(void* block) {
    forget(block);
    original<FreeFunction>(AlignedFree)(block);
}

HMODULE loaded(HMODULE module) {
    if (module != nullptr) {
        const auto error = GetLastError();
        patchModules(false);
        SetLastError(error);
    }
    return module;
}

HMODULE WINAPI hookLoadA(LPCSTR name) {
    return loaded(original<LoadAFunction>(LoadA)(name));
}

HMODULE WINAPI hookLoadW(LPCWSTR name) {
    return loaded(original<LoadWFunction>(LoadW)(name));
}

HMODULE WINAPI hookLoadExA(LPCSTR name, HANDLE file, DWORD flags) {
    return loaded(original<LoadExAFunction>(LoadExA)(name, file, flags));
}

HMODULE WINAPI hookLoadExW(LPCWSTR name, HANDLE file, DWORD flags) {
    return loaded(original<LoadExWFunction>(LoadExW)(name, file, flags));
}

const std::array<void*, Imports>& hooks() {
    static const std::array<void*, Imports> table{
        reinterpret_cast<void*>(&hookMalloc), reinterpret_cast<void*>(&hookCalloc), reinterpret_cast<void*>(&hookRealloc),
        reinterpret_cast<void*>(&hookFree), reinterpret_cast<void*>(&hookAlignedMalloc), reinterpret_cast<void*>(&hookAlignedRealloc),
        reinterpret_cast<void*>(&hookAlignedFree), reinterpret_cast<void*>(&hookLoadA), reinterpret_cast<void*>(&hookLoadW),
        reinterpret_cast<void*>(&hookLoadExA), reinterpret_cast<void*>(&hookLoadExW)};
    return table;
}

enum class Patched {
    Done,
    Skipped,
    NotReady
};

Patched patchSlot(void** slot, Import import) {
    void* current = std::atomic_ref<void*>(*slot).load(std::memory_order_acquire);
    if (current == hooks()[import]) return Patched::Skipped;
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(current), &owner)) return Patched::NotReady;
    void* expected = nullptr;
    if (!originals[import].compare_exchange_strong(expected, current, std::memory_order_acq_rel) && expected != current) return Patched::Skipped;
    MEMORY_BASIC_INFORMATION page{};
    if (VirtualQuery(slot, &page, sizeof(page)) == 0) return Patched::Skipped;
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    DWORD previous = 0;
    if (!VirtualProtect(slot, sizeof(void*), (page.Protect & executable) != 0 ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &previous)) return Patched::Skipped;
    std::atomic_ref<void*>(*slot).store(hooks()[import], std::memory_order_release);
    VirtualProtect(slot, sizeof(void*), previous, &previous);
    return Patched::Done;
}

struct ImportsPatched {
    std::size_t slots = 0;
    bool ready = true;
};

ImportsPatched patchImports(HMODULE module) {
    auto* base = reinterpret_cast<std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    ImportsPatched patched;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return patched;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return patched;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (directory.VirtualAddress == 0 || directory.Size == 0) return patched;
    for (auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name != 0; ++descriptor) {
        if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            const auto* name = reinterpret_cast<const char*>(byName->Name);
            for (std::size_t import = 0; import < Imports; ++import) {
                if (std::strcmp(name, ImportNames[import]) != 0) continue;
                const auto outcome = patchSlot(reinterpret_cast<void**>(&slots->u1.Function), static_cast<Import>(import));
                if (outcome == Patched::Done) ++patched.slots;
                if (outcome == Patched::NotReady) patched.ready = false;
                break;
            }
        }
    }
    return patched;
}

bool systemModule(const wchar_t* path, DWORD length) {
    std::array<wchar_t, MAX_PATH> windows{};
    const auto windowsLength = GetSystemWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (windowsLength == 0 || windowsLength >= windows.size() || length < windowsLength) return false;
    return CompareStringOrdinal(path, static_cast<int>(windowsLength), windows.data(), static_cast<int>(windowsLength), TRUE) == CSTR_EQUAL;
}

std::mutex& patchMutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_set<std::uintptr_t>& seenModules() {
    static auto* const modules = new std::unordered_set<std::uintptr_t>;
    return *modules;
}

void patchModules(bool wait) {
    std::string names;
    std::size_t slots = 0;
    try {
        std::unique_lock lock(patchMutex(), std::defer_lock);
        if (wait) {
            lock.lock();
        } else if (!lock.try_lock()) {
            return;
        }
        std::vector<HMODULE> modules(512);
        DWORD needed = 0;
        for (;;) {
            const auto capacity = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
            if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(), capacity, &needed)) return;
            if (needed <= capacity) break;
            modules.resize(needed / sizeof(HMODULE) + 64);
        }
        modules.resize(needed / sizeof(HMODULE));
        publishCodeRanges(modules);
        for (const auto module : modules) {
            if (seenModules().contains(reinterpret_cast<std::uintptr_t>(module))) continue;
            std::array<wchar_t, MAX_PATH> path{};
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            if (length == 0 || length >= path.size()) continue;
            if (systemModule(path.data(), length)) {
                seenModules().insert(reinterpret_cast<std::uintptr_t>(module));
                continue;
            }
            const auto patched = patchImports(module);
            if (patched.ready) seenModules().insert(reinterpret_cast<std::uintptr_t>(module));
            if (patched.slots == 0) continue;
            slots += patched.slots;
            hookedModules.fetch_add(1, std::memory_order_relaxed);
            names += ' ';
            names += baseName(path.data(), length);
        }
    } catch (...) {
        return;
    }
    if (slots == 0) return;
    hookedSlots.fetch_add(slots, std::memory_order_relaxed);
    print("[bigalloc] hooked " + std::to_string(slots) + " heap and LoadLibrary import slots in:" + names + "\n");
}

#endif

struct ShownSite {
    Stack stack;
    std::uint64_t liveBytes = 0;
    std::uint64_t liveCount = 0;
    std::uint64_t allocated = 0;
    std::uint64_t freed = 0;
    std::array<std::uint64_t, 3> sizes{};
    std::uintptr_t newest = 0;
    std::size_t newestBytes = 0;
    std::uint64_t newestSerial = 0;
    std::array<std::uint64_t, DumpWords> words{};
};

std::string report(const char* window) {
    if (!enabled()) return {};
    auto& state = tracker();
    if (reentered(state)) return {};
    std::unique_lock lock(state.mutex, std::try_to_lock);
    for (int attempt = 0; !lock.owns_lock() && attempt < 2000; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (lock.try_lock()) break;
    }
    if (!lock.owns_lock()) return std::string("[bigalloc] (") + window + ") the tracker stayed locked for 2 s, no report\n";
    std::string text;
    std::vector<ShownSite> shown;
    {
        Owned owned(state);
        std::vector<std::size_t> order;
        for (std::size_t index = 0; index < state.sites.size(); ++index) {
            const auto& site = state.sites[index];
            if (site.liveCount != 0 || site.allocated != 0 || site.freed != 0) order.push_back(index);
        }
        std::stable_sort(order.begin(), order.end(), [&](std::size_t left, std::size_t right) { return state.sites[left].liveBytes > state.sites[right].liveBytes; });
        if (order.size() > TopStacks) order.resize(TopStacks);
        for (const auto index : order) {
            const auto& site = state.sites[index];
            shown.push_back({site.stack, site.liveBytes, site.liveCount, site.allocated, site.freed, site.sizes});
        }
        const auto dumped = std::min(Dumped, order.size());
        for (const auto& [address, block] : state.blocks) {
            for (std::size_t rank = 0; rank < dumped; ++rank) {
                if (block.site == order[rank] && block.serial > shown[rank].newestSerial) {
                    shown[rank].newest = address;
                    shown[rank].newestSerial = block.serial;
                    shown[rank].newestBytes = block.bytes;
                }
            }
        }
        for (std::size_t rank = 0; rank < dumped; ++rank) {
            if (shown[rank].newest != 0) std::memcpy(shown[rank].words.data(), reinterpret_cast<const void*>(shown[rank].newest), sizeof(shown[rank].words));
        }
        append(text, "[bigalloc] (%s) live %.1f MiB in %llu blocks; since the last line +%llu blocks %.1f MiB allocated, -%llu blocks %.1f MiB freed; %llu stacks, %llu stale",
            window, mebibytes(state.liveBytes), static_cast<unsigned long long>(state.liveCount), static_cast<unsigned long long>(state.allocatedCount), mebibytes(state.allocatedBytes),
            static_cast<unsigned long long>(state.freedCount), mebibytes(state.freedBytes), static_cast<unsigned long long>(state.sites.size()), static_cast<unsigned long long>(state.stale));
        for (auto& site : state.sites) {
            site.allocated = 0;
            site.freed = 0;
        }
        state.allocatedBytes = 0;
        state.allocatedCount = 0;
        state.freedBytes = 0;
        state.freedCount = 0;
    }
    lock.unlock();
#ifdef _WIN32
    append(text, "; hooked %llu import slots in %llu modules", static_cast<unsigned long long>(hookedSlots.load(std::memory_order_relaxed)), static_cast<unsigned long long>(hookedModules.load(std::memory_order_relaxed)));
#endif
    text += '\n';
    for (std::size_t rank = 0; rank < shown.size(); ++rank) {
        const auto& site = shown[rank];
        append(text, "[bigalloc] #%llu live %.1f MiB in %llu (+%llu -%llu) sizes 1MiB %llu 2MiB %llu other %llu %s frames", static_cast<unsigned long long>(rank + 1), mebibytes(site.liveBytes),
            static_cast<unsigned long long>(site.liveCount), static_cast<unsigned long long>(site.allocated), static_cast<unsigned long long>(site.freed),
            static_cast<unsigned long long>(site.sizes[0]), static_cast<unsigned long long>(site.sizes[1]), static_cast<unsigned long long>(site.sizes[2]), site.stack.scanned ? "scanned" : "unwound");
        for (std::size_t frame = 0; frame < site.stack.depth; ++frame) {
            text += ' ';
            text += frameName(site.stack.frames[frame]);
        }
        text += '\n';
        if (site.newest != 0) {
            append(text, "[bigalloc] #%llu newest 0x%llx (%llu bytes):", static_cast<unsigned long long>(rank + 1), static_cast<unsigned long long>(site.newest), static_cast<unsigned long long>(site.newestBytes));
            for (const auto word : site.words) append(text, " %016llx", static_cast<unsigned long long>(word));
            text += '\n';
        }
    }
    return text;
}

void reportEveryTenSeconds() {
#ifdef _WIN32
    patchModules(true);
#endif
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
#ifdef _WIN32
        patchModules(true);
#endif
        print(report("10 s"));
    }
}

void reportAtExit() {
    print(report("exit"));
}

struct Start {
    Start() {
        if (!enabled()) return;
#ifndef _WIN32
        std::array<void*, 1> warm{};
        backtrace(warm.data(), static_cast<int>(warm.size()));
#endif
        tracker();
        std::atexit(&reportAtExit);
        try {
            std::thread(&reportEveryTenSeconds).detach();
        } catch (...) {
            print("[bigalloc] could not start the reporting thread\n");
        }
    }
};

const Start start;

}

bool BigAllocationsEnabled_nid_no_patch() {
    return enabled();
}

void BigAllocationsNote_nid_no_patch(void* block, std::size_t bytes) {
    record(block, bytes, 2, returnSlot(__builtin_frame_address(0), __builtin_return_address(0)));
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

void BigAllocationsForget_nid_no_patch(void* block) {
    forget(block);
}

std::string BigAllocationsReport_nid_no_patch(const char* window) {
    return report(window);
}
