#pragma once

#include "memory/Allocator.h"

#include <atomic>

namespace engine {

// Fixed-size block pool. Free-list of identically sized slots.
// O(1) allocate / free. No fragmentation inside the pool.
// Used for: EntityRecord, Archetype metadata, Chunk headers, command items.
//
// Thread-safe via a Treiber stack (lock-free LIFO free-list). Game threads
// can allocate entity records without taking a mutex — critical when the
// streaming system dumps 10k props on a cell load.
class PoolAllocator {
public:
    PoolAllocator() = default;
    PoolAllocator(void* buffer, usize capacity, usize block_size, usize block_alignment,
                  const char* name = "pool");

    void bind(void* buffer, usize capacity, usize block_size, usize block_alignment,
              const char* name = "pool");

    [[nodiscard]] void* allocate();
    void                deallocate(void* ptr);

    [[nodiscard]] usize block_size() const noexcept { return block_size_; }
    [[nodiscard]] usize capacity_blocks() const noexcept { return block_count_; }
    [[nodiscard]] usize allocated_blocks() const noexcept { return allocated_.load(std::memory_order_relaxed); }

    [[nodiscard]] Allocator as_allocator();

private:
    struct FreeNode {
        FreeNode* next;
    };

    static void* trampoline_allocate(void* self, usize bytes, usize alignment);
    static void  trampoline_deallocate(void* self, void* ptr, usize bytes);

    u8*                      buffer_      = nullptr;
    usize                    buffer_size_ = 0;
    usize                    block_size_  = 0;
    usize                    block_count_ = 0;
    std::atomic<FreeNode*>   free_head_{nullptr};
    std::atomic<usize>       allocated_{0};
    const char*              name_        = "pool";
};

} // namespace engine
