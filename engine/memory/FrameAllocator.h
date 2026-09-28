#pragma once

#include "memory/LinearAllocator.h"

#include <array>

namespace engine {

// Double-buffered per-thread frame arena.
// Frame N allocates from buffer[N & 1]. At the start of frame N+1 that
// buffer is reset. Pointers obtained this frame are INVALID next frame.
//
// Rule: NEVER store a FrameAllocator pointer in a component. Components
// live in archetype chunks (world lifetime). Frame memory is for:
//   - transient query result lists
//   - debug draw commands
//   - one-tick event payloads
class FrameAllocator {
public:
    static constexpr u32 kBufferCount = 2;

    void bind(void* buffer0, usize cap0, void* buffer1, usize cap1);

    void begin_frame(u64 frame_index);

    [[nodiscard]] void* allocate(usize bytes, usize alignment);
    [[nodiscard]] Allocator as_allocator();

    [[nodiscard]] u64   frame_index() const noexcept { return frame_index_; }
    [[nodiscard]] usize used() const noexcept { return arenas_[current_].used(); }

private:
    static void* trampoline_allocate(void* self, usize bytes, usize alignment);
    static void  trampoline_deallocate(void* self, void* ptr, usize bytes);
    static void  trampoline_reset(void* self);

    std::array<LinearAllocator, kBufferCount> arenas_{};
    u32 current_     = 0;
    u64 frame_index_ = 0;
};

} // namespace engine
