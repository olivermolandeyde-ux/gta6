#include "memory/FrameAllocator.h"

#include "core/Assert.h"

namespace engine {

void FrameAllocator::bind(void* buffer0, usize cap0, void* buffer1, usize cap1) {
    arenas_[0].bind(buffer0, cap0, "frame_0");
    arenas_[1].bind(buffer1, cap1, "frame_1");
    current_     = 0;
    frame_index_ = 0;
}

void FrameAllocator::begin_frame(u64 frame_index) {
    current_     = static_cast<u32>(frame_index & 1ull);
    frame_index_ = frame_index;
    arenas_[current_].reset();
}

void* FrameAllocator::allocate(usize bytes, usize alignment) {
    return arenas_[current_].allocate(bytes, alignment);
}

Allocator FrameAllocator::as_allocator() {
    Allocator a{};
    a.self          = this;
    a.allocate_fn   = &FrameAllocator::trampoline_allocate;
    a.deallocate_fn = &FrameAllocator::trampoline_deallocate;
    a.reset_fn      = &FrameAllocator::trampoline_reset;
    a.name          = "frame";
    return a;
}

void* FrameAllocator::trampoline_allocate(void* self, usize bytes, usize alignment) {
    return static_cast<FrameAllocator*>(self)->allocate(bytes, alignment);
}

void FrameAllocator::trampoline_deallocate(void* /*self*/, void* /*ptr*/, usize /*bytes*/) {
    // Frame memory is reclaimed as a slab at begin_frame.
}

void FrameAllocator::trampoline_reset(void* self) {
    static_cast<FrameAllocator*>(self)->arenas_[static_cast<FrameAllocator*>(self)->current_].reset();
}

} // namespace engine
