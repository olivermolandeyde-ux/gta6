#include "memory/PoolAllocator.h"

#include "core/Assert.h"

namespace engine {

PoolAllocator::PoolAllocator(void* buffer, usize capacity, usize block_size, usize block_alignment,
                             const char* name) {
    bind(buffer, capacity, block_size, block_alignment, name);
}

void PoolAllocator::bind(void* buffer, usize capacity, usize block_size, usize block_alignment,
                         const char* name) {
    ENGINE_ASSERT(buffer != nullptr, "pool allocator needs a backing buffer");
    ENGINE_ASSERT(block_size >= sizeof(FreeNode), "pool block too small for free-list node");
    ENGINE_ASSERT(block_alignment >= alignof(FreeNode), "pool alignment too small");
    ENGINE_ASSERT((block_alignment & (block_alignment - 1)) == 0, "alignment power-of-two");

    u8*   raw     = static_cast<u8*>(buffer);
    usize address = reinterpret_cast<usize>(raw);
    usize aligned = align_up(address, block_alignment);
    usize waste   = aligned - address;
    ENGINE_ASSERT(capacity > waste, "pool buffer smaller than alignment waste");

    buffer_      = reinterpret_cast<u8*>(aligned);
    buffer_size_ = capacity - waste;
    block_size_  = align_up(block_size, block_alignment);
    block_count_ = buffer_size_ / block_size_;
    name_        = name;
    allocated_.store(0, std::memory_order_relaxed);

    ENGINE_ASSERT(block_count_ > 0, "pool has zero blocks");

    FreeNode* head = nullptr;
    for (usize i = block_count_; i-- > 0;) {
        auto* node = reinterpret_cast<FreeNode*>(buffer_ + i * block_size_);
        node->next = head;
        head       = node;
    }
    free_head_.store(head, std::memory_order_release);
}

void* PoolAllocator::allocate() {
    FreeNode* node = free_head_.load(std::memory_order_acquire);
    while (node) {
        FreeNode* next = node->next;
        if (free_head_.compare_exchange_weak(node, next, std::memory_order_release,
                                             std::memory_order_acquire)) {
            allocated_.fetch_add(1, std::memory_order_relaxed);
            return node;
        }
    }
    ENGINE_PANIC("pool allocator exhausted");
}

void PoolAllocator::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    ENGINE_ASSERT(ptr >= buffer_ && ptr < buffer_ + buffer_size_, "pointer not from this pool");

    auto* node = static_cast<FreeNode*>(ptr);
    FreeNode* head = free_head_.load(std::memory_order_acquire);
    do {
        node->next = head;
    } while (!free_head_.compare_exchange_weak(head, node, std::memory_order_release,
                                               std::memory_order_acquire));
    allocated_.fetch_sub(1, std::memory_order_relaxed);
}

Allocator PoolAllocator::as_allocator() {
    Allocator a{};
    a.self          = this;
    a.allocate_fn   = &PoolAllocator::trampoline_allocate;
    a.deallocate_fn = &PoolAllocator::trampoline_deallocate;
    a.reset_fn      = nullptr;
    a.name          = name_;
    return a;
}

void* PoolAllocator::trampoline_allocate(void* self, usize bytes, usize /*alignment*/) {
    auto* pool = static_cast<PoolAllocator*>(self);
    ENGINE_ASSERT(bytes <= pool->block_size_, "pool allocate larger than block");
    return pool->allocate();
}

void PoolAllocator::trampoline_deallocate(void* self, void* ptr, usize /*bytes*/) {
    static_cast<PoolAllocator*>(self)->deallocate(ptr);
}

} // namespace engine
