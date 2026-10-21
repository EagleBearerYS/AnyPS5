#include "prx/libc/include/BigAllocations.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

namespace {

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

}

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
    const auto stack = text.find(" live 2.0 MiB in 2 (+3 -1) sizes 1MiB 2 2MiB 0 other 0 frames ");
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
