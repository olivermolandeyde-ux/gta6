#include "memory/MemorySystem.h"

#include "core/Assert.h"
#include "ecs/Chunk.h"
#include "ecs/Entity.h"

namespace engine {

void MemorySystem::boot(const MemoryBudget& budget) {
    ENGINE_ASSERT(!booted_, "MemorySystem::boot called twice");

    world_bytes_     = budget.world_arena_bytes;
    streaming_bytes_ = budget.streaming_arena_bytes;
    chunk_bytes_     = budget.chunk_pool_bytes;
    record_bytes_    = budget.record_pool_bytes;
    meta_bytes_      = budget.meta_pool_bytes;
    stack_bytes_     = budget.stack_bytes;
    frame_bytes_     = budget.frame_bytes;

    world_pages_     = pages_.allocate_pages(world_bytes_);
    streaming_pages_ = pages_.allocate_pages(streaming_bytes_);
    chunk_pages_     = pages_.allocate_pages(chunk_bytes_);
    record_pages_    = pages_.allocate_pages(record_bytes_);
    meta_pages_      = pages_.allocate_pages(meta_bytes_);
    stack_pages_     = pages_.allocate_pages(stack_bytes_);
    frame0_pages_    = pages_.allocate_pages(frame_bytes_);
    frame1_pages_    = pages_.allocate_pages(frame_bytes_);

    world_arena_.bind(world_pages_, world_bytes_, "world_arena");
    streaming_arena_.bind(streaming_pages_, streaming_bytes_, "streaming_arena");

    // Chunks are exactly 16 KiB, 16 KiB aligned so a chunk never straddles
    // a page-directory boundary the CPU cares about.
    chunk_pool_.bind(chunk_pages_, chunk_bytes_, kChunkBytes, kChunkBytes, "chunk_pool");

    record_pool_.bind(record_pages_, record_bytes_, sizeof(EntityRecord),
                      alignof(EntityRecord), "record_pool");

    // Meta pool holds Archetype objects (variable but bounded). We size a
    // conservative 4 KiB block — enough for signature + column descriptors.
    constexpr usize kMetaBlock = 4096;
    meta_pool_.bind(meta_pages_, meta_bytes_, kMetaBlock, 64, "meta_pool");

    stack_.bind(stack_pages_, stack_bytes_, "engine_stack");
    frame_.bind(frame0_pages_, frame_bytes_, frame1_pages_, frame_bytes_);

    booted_ = true;
}

void MemorySystem::shutdown() {
    if (!booted_) {
        return;
    }
    pages_.deallocate_pages(frame1_pages_, frame_bytes_);
    pages_.deallocate_pages(frame0_pages_, frame_bytes_);
    pages_.deallocate_pages(stack_pages_, stack_bytes_);
    pages_.deallocate_pages(meta_pages_, meta_bytes_);
    pages_.deallocate_pages(record_pages_, record_bytes_);
    pages_.deallocate_pages(chunk_pages_, chunk_bytes_);
    pages_.deallocate_pages(streaming_pages_, streaming_bytes_);
    pages_.deallocate_pages(world_pages_, world_bytes_);
    booted_ = false;
}

void MemorySystem::begin_frame(u64 frame_index) {
    ENGINE_ASSERT(booted_, "MemorySystem not booted");
    frame_.begin_frame(frame_index);
}

} // namespace engine
