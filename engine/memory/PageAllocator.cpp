#include "memory/PageAllocator.h"

#include "core/Assert.h"

#if defined(_WIN32)
#    define NOMINMAX
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#endif

namespace engine {

PageAllocator::~PageAllocator() {
    ENGINE_ASSERT(allocation_count_ == 0,
                  "PageAllocator destroyed with live mappings — leaking virtual memory");
}

void* PageAllocator::allocate_pages(usize bytes, bool huge) {
    ENGINE_ASSERT(bytes > 0, "zero-sized page allocation");
    const usize aligned = align_up(bytes, static_cast<usize>(kOsPageBytes));

#if defined(_WIN32)
    DWORD flags = MEM_RESERVE | MEM_COMMIT;
    if (huge) {
        flags |= MEM_LARGE_PAGES;
    }
    void* ptr = VirtualAlloc(nullptr, aligned, flags, PAGE_READWRITE);
    ENGINE_ASSERT(ptr != nullptr, "VirtualAlloc failed");
#else
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#    ifdef MAP_HUGETLB
    if (huge) {
        flags |= MAP_HUGETLB;
    }
#    else
    (void)huge;
#    endif
    void* ptr = mmap(nullptr, aligned, PROT_READ | PROT_WRITE, flags, -1, 0);
    ENGINE_ASSERT(ptr != MAP_FAILED, "mmap failed");
#endif

    committed_bytes_ += aligned;
    ++allocation_count_;
    return ptr;
}

void PageAllocator::deallocate_pages(void* ptr, usize bytes) {
    if (!ptr) {
        return;
    }
    const usize aligned = align_up(bytes, static_cast<usize>(kOsPageBytes));

#if defined(_WIN32)
    const BOOL ok = VirtualFree(ptr, 0, MEM_RELEASE);
    ENGINE_ASSERT(ok, "VirtualFree failed");
#else
    const int rc = munmap(ptr, aligned);
    ENGINE_ASSERT(rc == 0, "munmap failed");
#endif

    ENGINE_ASSERT(committed_bytes_ >= aligned, "page accounting underflow");
    committed_bytes_ -= aligned;
    ENGINE_ASSERT(allocation_count_ > 0, "page accounting underflow");
    --allocation_count_;
}

Allocator PageAllocator::as_allocator() {
    Allocator a{};
    a.self           = this;
    a.allocate_fn    = &PageAllocator::trampoline_allocate;
    a.deallocate_fn  = &PageAllocator::trampoline_deallocate;
    a.reset_fn       = nullptr;
    a.name           = "page";
    return a;
}

void* PageAllocator::trampoline_allocate(void* self, usize bytes, usize /*alignment*/) {
    return static_cast<PageAllocator*>(self)->allocate_pages(bytes, false);
}

void PageAllocator::trampoline_deallocate(void* self, void* ptr, usize bytes) {
    static_cast<PageAllocator*>(self)->deallocate_pages(ptr, bytes);
}

} // namespace engine
