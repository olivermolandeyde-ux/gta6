#pragma once

#include "core/Assert.h"
#include "core/Types.h"

#include <new>
#include <utility>

namespace engine {

// Function-pointer allocator. No virtual hierarchy, no RTTI, no vtable
// per-derived-class. Every concrete allocator fills this POD and is passed
// by value (two pointers). Hot-path allocation is a direct indirect call.
struct Allocator {
    using AllocateFn   = void* (*)(void* self, usize bytes, usize alignment);
    using DeallocateFn = void  (*)(void* self, void* ptr, usize bytes);
    using ResetFn      = void  (*)(void* self);

    void*        self        = nullptr;
    AllocateFn   allocate_fn = nullptr;
    DeallocateFn deallocate_fn = nullptr;
    ResetFn      reset_fn    = nullptr;
    const char*  name        = "unnamed";

    [[nodiscard]] void* allocate(usize bytes, usize alignment = alignof(std::max_align_t)) const {
        ENGINE_ASSERT(allocate_fn != nullptr, "allocator missing allocate_fn");
        ENGINE_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0, "alignment must be power of two");
        void* ptr = allocate_fn(self, bytes, alignment);
        ENGINE_ASSERT(ptr != nullptr, "allocation failed (out of engine memory)");
        return ptr;
    }

    void deallocate(void* ptr, usize bytes) const {
        if (!ptr) {
            return;
        }
        ENGINE_ASSERT(deallocate_fn != nullptr, "allocator missing deallocate_fn");
        deallocate_fn(self, ptr, bytes);
    }

    void reset() const {
        if (reset_fn) {
            reset_fn(self);
        }
    }
};

// Placement-new helpers that never touch the CRT heap.
template <typename T, typename... Args>
[[nodiscard]] T* construct_at(Allocator allocator, Args&&... args) {
    void* memory = allocator.allocate(sizeof(T), alignof(T));
    return new (memory) T(static_cast<Args&&>(args)...);
}

template <typename T>
void destroy_at(Allocator allocator, T* object) {
    if (!object) {
        return;
    }
    object->~T();
    allocator.deallocate(object, sizeof(T));
}

template <typename T>
[[nodiscard]] T* allocate_array(Allocator allocator, usize count) {
    if (count == 0) {
        return nullptr;
    }
    void* memory = allocator.allocate(sizeof(T) * count, alignof(T));
    T*    elems  = static_cast<T*>(memory);
    for (usize i = 0; i < count; ++i) {
        new (elems + i) T();
    }
    return elems;
}

template <typename T>
void deallocate_array(Allocator allocator, T* elems, usize count) {
    if (!elems) {
        return;
    }
    for (usize i = 0; i < count; ++i) {
        elems[i].~T();
    }
    allocator.deallocate(elems, sizeof(T) * count);
}

} // namespace engine
