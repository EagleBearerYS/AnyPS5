#include "prx/libc/include/BigAllocations.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <chrono>
#include <thread>
#endif

namespace {

#ifndef _WIN32

void* allocate(std::size_t bytes) {
    void* block = std::malloc(bytes != 0 ? bytes : 1);
    if (block != nullptr) BigAllocationsNote_nid_no_patch(block, bytes);
    return block;
}

void* allocateAligned(std::size_t bytes, std::align_val_t alignment) {
    const auto align = static_cast<std::size_t>(alignment);
    const auto rounded = (bytes + align - 1) / align * align;
    void* block = std::aligned_alloc(align, rounded != 0 ? rounded : align);
    if (block != nullptr) BigAllocationsNote_nid_no_patch(block, bytes);
    return block;
}

void release(void* block) noexcept {
    if (block == nullptr) return;
    BigAllocationsForget_nid_no_patch(block);
    std::free(block);
}

[[gnu::noinline]] std::vector<std::vector<std::uint64_t>> AllocateBigVectors() {
    std::vector<std::vector<std::uint64_t>> vectors;
    vectors.reserve(3);
    for (std::uint64_t index = 0; index < 3; ++index) vectors.emplace_back(std::size_t{1} << 17, 0x200000000ull + index * 0x1000);
    return vectors;
}

#endif

}

#ifndef _WIN32

[[gnu::noinline]] void* operator new(std::size_t bytes) {
    if (void* block = allocate(bytes)) return block;
    throw std::bad_alloc();
}

[[gnu::noinline]] void* operator new[](std::size_t bytes) {
    if (void* block = allocate(bytes)) return block;
    throw std::bad_alloc();
}

[[gnu::noinline]] void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate(bytes);
}

[[gnu::noinline]] void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    return allocate(bytes);
}

[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment) {
    if (void* block = allocateAligned(bytes, alignment)) return block;
    throw std::bad_alloc();
}

[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    if (void* block = allocateAligned(bytes, alignment)) return block;
    throw std::bad_alloc();
}

[[gnu::noinline]] void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAligned(bytes, alignment);
}

[[gnu::noinline]] void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return allocateAligned(bytes, alignment);
}

[[gnu::noinline]] void operator delete(void* block) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete(void* block, std::size_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block, std::size_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete(void* block, const std::nothrow_t&) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block, const std::nothrow_t&) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete(void* block, std::align_val_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block, std::align_val_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete(void* block, std::size_t, std::align_val_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block, std::size_t, std::align_val_t) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete(void* block, std::align_val_t, const std::nothrow_t&) noexcept {
    release(block);
}

[[gnu::noinline]] void operator delete[](void* block, std::align_val_t, const std::nothrow_t&) noexcept {
    release(block);
}

#endif

#ifdef _WIN32

[[gnu::noinline]] void* MallocMegabyte() {
    auto* block = static_cast<std::uint8_t*>(std::malloc(std::size_t{1} << 20));
    if (block != nullptr) block[0] = 1;
    return block;
}

[[gnu::noinline]] void* NewMegabyte() {
    auto* block = static_cast<std::uint8_t*>(::operator new(std::size_t{1} << 20));
    block[0] = 2;
    return block;
}

namespace {

bool hooksInstalled() {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const auto text = BigAllocationsReport_nid_no_patch("hooks");
        const auto at = text.find("; hooked ");
        if (at != std::string::npos && text.compare(at, 11, "; hooked 0 ") != 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

std::string moduleName() {
    std::string path(MAX_PATH, '\0');
    const auto length = GetModuleFileNameA(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

const char* framedAs(const std::string& text, const std::string& module, std::uintptr_t function) {
    const auto start = function - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto token = module + "+0x";
    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        auto lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        const auto line = text.substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;
        const auto frames = line.find(" frames ");
        if (line.rfind("[bigalloc] #", 0) != 0 || frames == std::string::npos) continue;
        for (auto at = line.find(token, frames); at != std::string::npos; at = line.find(token, at + 1)) {
            const auto offset = std::strtoull(line.c_str() + at + token.size(), nullptr, 16);
            if (offset > start && offset <= start + 0x100) return line.find(" scanned frames ") != std::string::npos ? "scanned" : "unwound";
        }
    }
    return nullptr;
}

}

int main() {
    if (!BigAllocationsEnabled_nid_no_patch()) {
        std::fputs("big allocation sites: run with APS5_PROFILE_BIG_ALLOCS=1\n", stderr);
        return 1;
    }
    if (!hooksInstalled()) {
        std::fputs("big allocation sites: the heap imports were not hooked within 10 s\n", stderr);
        return 1;
    }
    void* first = MallocMegabyte();
    void* second = NewMegabyte();
    const auto text = BigAllocationsReport_nid_no_patch("test");
    std::fputs(text.c_str(), stdout);
    std::free(first);
    ::operator delete(second);
    const auto module = moduleName();
    const auto* malloced = framedAs(text, module, reinterpret_cast<std::uintptr_t>(&MallocMegabyte));
    const auto* created = framedAs(text, module, reinterpret_cast<std::uintptr_t>(&NewMegabyte));
    if (malloced == nullptr || created == nullptr) {
        std::fprintf(stderr, "big allocation sites: no stack has a frame in %s:\n%s", malloced == nullptr ? "MallocMegabyte" : "NewMegabyte", text.c_str());
        return 1;
    }
    std::printf("big allocation sites tests passed (MallocMegabyte %s, NewMegabyte %s)\n", malloced, created);
    return 0;
}

#else

int main() {
    if (!BigAllocationsEnabled_nid_no_patch()) {
        std::fputs("big allocation sites: run with APS5_PROFILE_BIG_ALLOCS=1\n", stderr);
        return 1;
    }
    BigAllocationsReport_nid_no_patch("start");
    auto vectors = AllocateBigVectors();
    vectors.erase(vectors.begin());
    const auto text = BigAllocationsReport_nid_no_patch("test");
    std::fputs(text.c_str(), stdout);
    const auto stack = text.find(" live 2.0 MiB in 2 (+3 -1) sizes 1MiB 2 2MiB 0 other 0 unwound frames ");
    if (stack == std::string::npos) {
        std::fprintf(stderr, "big allocation sites: no stack holds 2 live 1 MiB blocks of 3 allocated and 1 freed:\n%s", text.c_str());
        return 1;
    }
    if (text.find(" 0000000200002000 0000000200002000 0000000200002000 0000000200002000") == std::string::npos) {
        std::fprintf(stderr, "big allocation sites: the newest live block's first 64 bytes are not the third vector's fill:\n%s", text.c_str());
        return 1;
    }
    std::puts("big allocation sites tests passed");
    return 0;
}

#endif
