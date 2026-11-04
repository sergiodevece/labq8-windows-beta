// LABQ8 Windows beta, test support only: a Windows port of
// tests/AllocationGuard.cpp (same API and the same counting), used instead
// of it when the tests are built with MSVC (distribution/windows/
// windows-build-overrides.cmake). The original uses posix_memalign and frees
// aligned blocks with std::free, which is POSIX only.
//
// Here, as in the original, ordinary blocks are plain malloc / free, so
// memory may still pass between this module and the C++ runtime DLL (whose
// own operator new is malloc). Over-aligned blocks (the align_val_t forms,
// only used above __STDCPP_DEFAULT_NEW_ALIGNMENT__) use _aligned_malloc and
// are always released by the matching align_val_t deletes with _aligned_free.
#include "AllocationGuard.h"

#include <cstddef>
#include <cstdlib>
#include <malloc.h>
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

    return alignment != 0 ? _aligned_malloc (size, alignment) : std::malloc (size);
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

void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { _aligned_free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free (p); }
