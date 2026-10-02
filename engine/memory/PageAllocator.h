#pragma once

#include "memory/Allocator.h"

namespace engine {

// OS virtual-memory page source. All higher-level allocators pull committed
// pages from here. Never used for per-object allocations.
//
// Linux  : mmap(MAP_PRIVATE | MAP_ANONYMOUS)
// Windows: VirtualAlloc(MEM_RESERVE | MEM_COMMIT)
//
// Deallocate unmaps. Pages are 4 KiB-aligned; optional huge-page hint for
// streaming world tiles later.
class PageAllocator {
public:
    PageAllocator() = default;
    ~PageAllocator();

    PageAllocator(const PageAllocator&)            = delete;
    PageAllocator& operator=(const PageAllocator&) = delete;

    [[nodiscard]] void* allocate_pages(usize bytes, bool huge = false);
    void                deallocate_pages(void* ptr, usize bytes);

    [[nodiscard]] usize committed_bytes() const noexcept { return committed_bytes_; }
    [[nodiscard]] usize allocation_count() const noexcept { return allocation_count_; }

    [[nodiscard]] Allocator as_allocator();

private:
    static void* trampoline_allocate(void* self, usize bytes, usize alignment);
    static void  trampoline_deallocate(void* self, void* ptr, usize bytes);

    usize committed_bytes_  = 0;
    usize allocation_count_ = 0;
};

} // namespace engine
