#pragma once

#include "memory/Allocator.h"

namespace engine {

// Bump / arena allocator. O(1) allocate, O(1) reset of the entire region.
// Individual deallocations are no-ops — lifetime is scoped to the arena.
// Used for: world boot, baked prefab blobs, per-streaming-cell scratch.
class LinearAllocator {
public:
    LinearAllocator() = default;
    LinearAllocator(void* buffer, usize capacity, const char* name = "linear");

    void bind(void* buffer, usize capacity, const char* name = "linear");

    [[nodiscard]] void* allocate(usize bytes, usize alignment);
    void                deallocate(void* ptr, usize bytes) noexcept; // no-op
    void                reset() noexcept;

    [[nodiscard]] usize used() const noexcept { return offset_; }
    [[nodiscard]] usize capacity() const noexcept { return capacity_; }
    [[nodiscard]] usize remaining() const noexcept { return capacity_ - offset_; }

    [[nodiscard]] Allocator as_allocator();

private:
    static void* trampoline_allocate(void* self, usize bytes, usize alignment);
    static void  trampoline_deallocate(void* self, void* ptr, usize bytes);
    static void  trampoline_reset(void* self);

    u8*         buffer_   = nullptr;
    usize       capacity_ = 0;
    usize       offset_   = 0;
    const char* name_     = "linear";
};

} // namespace engine
