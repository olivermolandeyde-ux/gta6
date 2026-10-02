#pragma once

#include "memory/FrameAllocator.h"
#include "memory/LinearAllocator.h"
#include "memory/PageAllocator.h"
#include "memory/PoolAllocator.h"
#include "memory/StackAllocator.h"

namespace engine {

// Central memory director. Owns OS pages and carves them into the five
// lifetime domains the simulation is allowed to use.
//
// STRICT RULES (enforced by construction, not convention):
//
//  1. No malloc / new / shared_ptr for simulation state. CRT heap is banned
//     on the game thread after MemorySystem::boot().
//  2. Entity component bytes live ONLY in 16 KiB archetype chunks, allocated
//     from the chunk pool. Components are never individually new'd.
//  3. EntityRecord, Archetype, and Chunk header objects live in typed pools.
//  4. FrameAllocator pointers die at the next begin_frame(). Storing them in
//     a component is a memory-safety bug.
//  5. World arena is reset only on a full world tear-down (load screen).
//  6. Streaming arena is reset per unloaded world cell.
//  7. All returned pointers are cache-line or type-aligned. No hidden
//     headers in front of user allocations (pools excepted, they overlay
//     FreeNode on free slots only).
struct MemoryBudget {
    usize world_arena_bytes     = 64ull * 1024ull * 1024ull;  // 64 MiB persistent world
    usize streaming_arena_bytes = 128ull * 1024ull * 1024ull; // 128 MiB streaming cells
    usize chunk_pool_bytes      = 256ull * 1024ull * 1024ull; // 256 MiB of 16 KiB chunks
    usize record_pool_bytes     = 32ull * 1024ull * 1024ull;  // entity records
    usize meta_pool_bytes       = 8ull * 1024ull * 1024ull;   // archetypes / queries
    usize stack_bytes           = 4ull * 1024ull * 1024ull;   // nested scope scratch
    usize frame_bytes           = 16ull * 1024ull * 1024ull;  // per-buffer frame arena
};

class MemorySystem {
public:
    void boot(const MemoryBudget& budget = {});
    void shutdown();
    void begin_frame(u64 frame_index);

    [[nodiscard]] PageAllocator&    pages() noexcept { return pages_; }
    [[nodiscard]] LinearAllocator&  world_arena() noexcept { return world_arena_; }
    [[nodiscard]] LinearAllocator&  streaming_arena() noexcept { return streaming_arena_; }
    [[nodiscard]] PoolAllocator&    chunk_pool() noexcept { return chunk_pool_; }
    [[nodiscard]] PoolAllocator&    record_pool() noexcept { return record_pool_; }
    [[nodiscard]] PoolAllocator&    meta_pool() noexcept { return meta_pool_; }
    [[nodiscard]] StackAllocator&   stack() noexcept { return stack_; }
    [[nodiscard]] FrameAllocator&   frame() noexcept { return frame_; }

    [[nodiscard]] Allocator world_allocator() { return world_arena_.as_allocator(); }
    [[nodiscard]] Allocator streaming_allocator() { return streaming_arena_.as_allocator(); }
    [[nodiscard]] Allocator chunk_allocator() { return chunk_pool_.as_allocator(); }
    [[nodiscard]] Allocator record_allocator() { return record_pool_.as_allocator(); }
    [[nodiscard]] Allocator meta_allocator() { return meta_pool_.as_allocator(); }
    [[nodiscard]] Allocator stack_allocator() { return stack_.as_allocator(); }
    [[nodiscard]] Allocator frame_allocator() { return frame_.as_allocator(); }

    [[nodiscard]] bool is_booted() const noexcept { return booted_; }

private:
    PageAllocator   pages_{};
    LinearAllocator world_arena_{};
    LinearAllocator streaming_arena_{};
    PoolAllocator   chunk_pool_{};
    PoolAllocator   record_pool_{};
    PoolAllocator   meta_pool_{};
    StackAllocator  stack_{};
    FrameAllocator  frame_{};

    void* world_pages_     = nullptr;
    void* streaming_pages_ = nullptr;
    void* chunk_pages_     = nullptr;
    void* record_pages_    = nullptr;
    void* meta_pages_      = nullptr;
    void* stack_pages_     = nullptr;
    void* frame0_pages_    = nullptr;
    void* frame1_pages_    = nullptr;

    usize world_bytes_     = 0;
    usize streaming_bytes_ = 0;
    usize chunk_bytes_     = 0;
    usize record_bytes_    = 0;
    usize meta_bytes_      = 0;
    usize stack_bytes_     = 0;
    usize frame_bytes_     = 0;

    bool booted_ = false;
};

} // namespace engine
