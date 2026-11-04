// LABQ8 Windows beta, test support only: a Windows port of
// tests/AllocationGuard.cpp (same API and the same counting), used instead
// of it when the tests are built with MSVC (distribution/windows/
// windows-build-overrides.cmake). The original uses posix_memalign and frees
// aligned blocks with std::free, which is POSIX only: on Windows aligned
// blocks cannot be released with free(). Here every block, aligned or not,
// keeps the pointer malloc returned just before the address handed out, and
// every form of delete releases that pointer.
#include "AllocationGuard.h"

#include <cstddef>
#include <cstdint>
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

    const std::size_t align = alignment > alignof (std::max_align_t) ? alignment : alignof (std::max_align_t);
    void* raw = std::malloc (size + align + sizeof (void*));
    if (raw == nullptr)
        return nullptr;
    auto start = reinterpret_cast<std::uintptr_t> (raw) + sizeof (void*);
    start = (start + align - 1) & ~(static_cast<std::uintptr_t> (align) - 1);
    auto* p = reinterpret_cast<void*> (start);
    static_cast<void**> (p)[-1] = raw;
    return p;
}

void release (void* p) noexcept
{
    if (p != nullptr)
        std::free (static_cast<void**> (p)[-1]);
}
} // namespace

namespace labq8::test
{
ScopedAllocationCounter::ScopedAllocationCounter() noexcept : startCount (allocations) { ++countingDepth; }
ScopedAllocationCounter::~ScopedAllocationCounter() noexcept { --countingDepth; }
long ScopedAllocationCounter::count() const noexcept { return allocations - startCount; }
} // namespace labq8::test

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

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, std::align_val_t) noexcept { release (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { release (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { release (p); }
