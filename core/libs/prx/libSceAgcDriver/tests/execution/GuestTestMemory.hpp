#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_GUESTTESTMEMORY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_GUESTTESTMEMORY_HPP

#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestHeap.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>

namespace TestGuest {

inline constexpr std::size_t PoolBytes = std::size_t{256} << 20;
#ifdef _WIN32
inline constexpr std::uint32_t PageReadWrite = 0x04u;
#endif

inline std::byte* Allocate(std::size_t bytes, std::size_t alignment) {
    struct Pool {
        std::byte* base = nullptr;
        std::size_t used = 0;
        Pool() {
#ifdef _WIN32
            base = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(PoolBytes, 65536));
            if (base != nullptr) GuestArena::GuestArenaCommit_nid_postfix(base, PoolBytes, PageReadWrite, PoolBytes);
#else
            constexpr std::uintptr_t align = 65536;
            void* mapped = mmap(nullptr, PoolBytes + align, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapped != MAP_FAILED) {
                const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
                const auto aligned = (begin + align - 1) & ~(align - 1);
                if (aligned != begin) munmap(mapped, aligned - begin);
                if (aligned + PoolBytes != begin + PoolBytes + align) munmap(reinterpret_cast<void*>(aligned + PoolBytes), begin + align - aligned);
                base = reinterpret_cast<std::byte*>(aligned);
                GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(base, PoolBytes);
            }
#endif
            if (base == nullptr) throw std::runtime_error("test guest memory: the guest pool cannot be allocated");
            GuestAllocations::Mutation().Add(base, PoolBytes, true, true, true);
        }
    };
    static Pool pool;
    const auto offset = (pool.used + alignment - 1) / alignment * alignment;
    if (offset + bytes > PoolBytes) throw std::runtime_error("test guest memory: the guest pool is exhausted");
    pool.used = offset + bytes;
    return pool.base + offset;
}

template<typename T, std::size_t N>
class Array {
public:
    static constexpr std::size_t ByteSize = sizeof(T) * N;

    Array() : items(reinterpret_cast<T*>(Allocate(ByteSize, 256))) {
        std::uninitialized_value_construct_n(items, N);
    }

    Array(std::initializer_list<T> values) : Array() {
        std::copy(values.begin(), values.end(), items);
    }

    Array(const Array& other) : Array() {
        std::copy(other.begin(), other.end(), items);
    }

    Array& operator=(const Array& other) {
        std::copy(other.begin(), other.end(), items);
        return *this;
    }

    Array& operator=(std::initializer_list<T> values) {
        std::fill(items, items + N, T{});
        std::copy(values.begin(), values.end(), items);
        return *this;
    }

    Array& operator=(const std::array<T, N>& values) {
        std::copy(values.begin(), values.end(), items);
        return *this;
    }

    operator std::array<T, N>() const {
        std::array<T, N> values{};
        std::copy(items, items + N, values.begin());
        return values;
    }

    bool operator==(const Array& other) const { return std::equal(items, items + N, other.items); }
    bool operator==(const std::array<T, N>& other) const { return std::equal(items, items + N, other.begin()); }

    T* data() { return items; }
    const T* data() const { return items; }
    static constexpr std::size_t size() { return N; }
    T& operator[](std::size_t index) { return items[index]; }
    const T& operator[](std::size_t index) const { return items[index]; }
    T* begin() { return items; }
    T* end() { return items + N; }
    const T* begin() const { return items; }
    const T* end() const { return items + N; }
    T& front() { return items[0]; }
    T& back() { return items[N - 1]; }
    void fill(const T& value) { std::fill(items, items + N, value); }

private:
    T* items;
};


template<typename T>
struct Allocator {
    using value_type = T;

    Allocator() = default;
    template<typename U>
    Allocator(const Allocator<U>&) {}

    T* allocate(std::size_t count) {
        void* block = GuestHeap::GuestHeapAlignForGpu_nid_postfix(256, std::max<std::size_t>(count * sizeof(T), 1));
        if (block == nullptr) throw std::bad_alloc();
        return static_cast<T*>(block);
    }

    void deallocate(T* pointer, std::size_t) {
        GuestHeap::GuestHeapFree_nid_postfix(pointer);
    }

    template<typename U>
    bool operator==(const Allocator<U>&) const { return true; }
};

template<typename T>
using Vector = std::vector<T, Allocator<T>>;

}

#endif
