#pragma once

#include "memory/Allocator.h"

namespace engine {

// LIFO stack allocator. Deallocate MUST be the most recently allocated
// pointer (or a rewind marker). Used for nested scopes: physics island
// solve, one-shot query result buffers, shader permutation compile.
class StackAllocator {
public:
    struct Marker {
        usize offset = 0;
    };

    StackAllocator() = default;
    StackAllocator(void* buffer, usize capacity, const char* name = "stack");

    void bind(void* buffer, usize capacity, const char* name = "stack");

    [[nodiscard]] void*  allocate(usize bytes, usize alignment);
    void                 deallocate(void* ptr, usize bytes);
    [[nodiscard]] Marker push_marker() const noexcept;
    void                 pop_marker(Marker marker) noexcept;
    void                 reset() noexcept;

    [[nodiscard]] Allocator as_allocator();

private:
    static void* trampoline_allocate(void* self, usize bytes, usize alignment);
    static void  trampoline_deallocate(void* self, void* ptr, usize bytes);
    static void  trampoline_reset(void* self);

    u8*         buffer_   = nullptr;
    usize       capacity_ = 0;
    usize       offset_   = 0;
    void*       last_ptr_ = nullptr;
    const char* name_     = "stack";
};

} // namespace engine
