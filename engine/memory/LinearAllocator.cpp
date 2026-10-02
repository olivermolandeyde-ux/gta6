#include "memory/LinearAllocator.h"

#include "core/Assert.h"

namespace engine {

LinearAllocator::LinearAllocator(void* buffer, usize capacity, const char* name) {
    bind(buffer, capacity, name);
}

void LinearAllocator::bind(void* buffer, usize capacity, const char* name) {
    ENGINE_ASSERT(buffer != nullptr, "linear allocator needs a backing buffer");
    ENGINE_ASSERT(capacity > 0, "linear allocator capacity must be > 0");
    buffer_   = static_cast<u8*>(buffer);
    capacity_ = capacity;
    offset_   = 0;
    name_     = name;
}

void* LinearAllocator::allocate(usize bytes, usize alignment) {
    ENGINE_ASSERT(buffer_ != nullptr, "linear allocator not bound");
    ENGINE_ASSERT(bytes > 0, "zero-sized linear allocation");
    ENGINE_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0, "alignment power-of-two");

    const usize aligned_offset = align_up(offset_, alignment);
    ENGINE_ASSERT(aligned_offset <= capacity_ && bytes <= capacity_ - aligned_offset,
                  "linear allocator exhausted");

    void* ptr = buffer_ + aligned_offset;
    offset_   = aligned_offset + bytes;
    return ptr;
}

void LinearAllocator::deallocate(void* /*ptr*/, usize /*bytes*/) noexcept {
    // Intentional no-op. Lifetime is the arena.
}

void LinearAllocator::reset() noexcept {
    offset_ = 0;
}

Allocator LinearAllocator::as_allocator() {
    Allocator a{};
    a.self          = this;
    a.allocate_fn   = &LinearAllocator::trampoline_allocate;
    a.deallocate_fn = &LinearAllocator::trampoline_deallocate;
    a.reset_fn      = &LinearAllocator::trampoline_reset;
    a.name          = name_;
    return a;
}

void* LinearAllocator::trampoline_allocate(void* self, usize bytes, usize alignment) {
    return static_cast<LinearAllocator*>(self)->allocate(bytes, alignment);
}

void LinearAllocator::trampoline_deallocate(void* self, void* ptr, usize bytes) {
    static_cast<LinearAllocator*>(self)->deallocate(ptr, bytes);
}

void LinearAllocator::trampoline_reset(void* self) {
    static_cast<LinearAllocator*>(self)->reset();
}

} // namespace engine
