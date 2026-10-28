#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

namespace
{
thread_local int countingDepth = 0;
thread_local long allocations = 0;

void* allocate (std::size_t size, std::size_t alignment = 0)
{
    if (countingDepth > 0)
        ++allocations;

    if (size == 0)
        size = 1;

    void* p = nullptr;

    if (alignment > alignof (std::max_align_t))
    {
        if (posix_memalign (&p, alignment, size) != 0)
            p = nullptr;
    }
    else
    {
        p = std::malloc (size);
    }

    return p;
}
} // namespace

namespace labq8::test
{
ScopedAllocationCounter::ScopedAllocationCounter() noexcept : startCount (allocations) { ++countingDepth; }
ScopedAllocationCounter::~ScopedAllocationCounter() noexcept { --countingDepth; }
long ScopedAllocationCounter::count() const noexcept { return allocations - startCount; }
} // namespace labq8::test

// Replaceable global allocation functions ([new.delete]); every form routes
// through allocate() / free() so counted and uncounted memory mix safely.
void* operator new (std::size_t n)
{
    if (auto* p = allocate (n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (auto* p = allocate (n)) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n, std::align_val_t a)
{
    if (auto* p = allocate (n, static_cast<std::size_t> (a))) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n, std::align_val_t a)
{
    if (auto* p = allocate (n, static_cast<std::size_t> (a))) return p;
    throw std::bad_alloc();
}
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { return allocate (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return allocate (n); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate (n, static_cast<std::size_t> (a)); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocate (n, static_cast<std::size_t> (a)); }

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free (p); }
