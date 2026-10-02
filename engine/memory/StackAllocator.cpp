#include "memory/StackAllocator.h"

#include "core/Assert.h"

namespace engine {

StackAllocator::StackAllocator(void* buffer, usize capacity, const char* name) {
    bind(buffer, capacity, name);
}

void StackAllocator::bind(void* buffer, usize capacity, const char* name) {
    ENGINE_ASSERT(buffer != nullptr, "stack allocator needs a backing buffer");
    buffer_   = static_cast<u8*>(buffer);
    capacity_ = capacity;
    offset_   = 0;
    last_ptr_ = nullptr;
    name_     = name;
}

void* StackAllocator::allocate(usize bytes, usize alignment) {
    ENGINE_ASSERT(buffer_ != nullptr, "stack allocator not bound");
    ENGINE_ASSERT(bytes > 0, "zero-sized stack allocation");

    const usize aligned_offset = align_up(offset_, alignment);
    ENGINE_ASSERT(aligned_offset <= capacity_ && bytes <= capacity_ - aligned_offset,
                  "stack allocator exhausted");

    void* ptr = buffer_ + aligned_offset;
    offset_   = aligned_offset + bytes;
    last_ptr_ = ptr;
    return ptr;
}

void StackAllocator::deallocate(void* ptr, usize bytes) {
    ENGINE_ASSERT(ptr == last_ptr_, "stack allocator only frees the most recent allocation");
    ENGINE_ASSERT(offset_ >= bytes, "stack rewind underflow");
    offset_   -= bytes;
    last_ptr_  = nullptr;
}

StackAllocator::Marker StackAllocator::push_marker() const noexcept {
    return Marker{offset_};
}

void StackAllocator::pop_marker(Marker marker) noexcept {
    ENGINE_ASSERT(marker.offset <= offset_, "stack marker is in the future");
    offset_   = marker.offset;
    last_ptr_ = nullptr;
}

void StackAllocator::reset() noexcept {
    offset_   = 0;
    last_ptr_ = nullptr;
}

Allocator StackAllocator::as_allocator() {
    Allocator a{};
    a.self          = this;
    a.allocate_fn   = &StackAllocator::trampoline_allocate;
    a.deallocate_fn = &StackAllocator::trampoline_deallocate;
    a.reset_fn      = &StackAllocator::trampoline_reset;
    a.name          = name_;
    return a;
}

void* StackAllocator::trampoline_allocate(void* self, usize bytes, usize alignment) {
    return static_cast<StackAllocator*>(self)->allocate(bytes, alignment);
}

void StackAllocator::trampoline_deallocate(void* self, void* ptr, usize bytes) {
    static_cast<StackAllocator*>(self)->deallocate(ptr, bytes);
}

void StackAllocator::trampoline_reset(void* self) {
    static_cast<StackAllocator*>(self)->reset();
}

} // namespace engine
