#pragma once

// Counts C++ heap allocations made by the current thread while a
// ScopedAllocationCounter is alive. Implemented by replacing the global
// operator new/delete in the test binary only (AllocationGuard.cpp).

namespace labq8::test
{
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter() noexcept;
    ~ScopedAllocationCounter() noexcept;

    long count() const noexcept;

    ScopedAllocationCounter (const ScopedAllocationCounter&) = delete;
    ScopedAllocationCounter& operator= (const ScopedAllocationCounter&) = delete;

private:
    long startCount;
};
} // namespace labq8::test
