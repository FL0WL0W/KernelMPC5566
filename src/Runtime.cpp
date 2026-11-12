#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <__verbose_abort>

namespace
{
    [[noreturn]] void Halt()
    {
        asm volatile("wrteei 0" ::: "memory");
        for (;;)
            asm volatile("" ::: "memory");
    }
}

extern "C" [[noreturn]] void abort()
{
    Halt();
}

extern "C" [[noreturn]] void __cxa_pure_virtual()
{
    Halt();
}

// Picolibc's allocator requests space from the linker-reserved heap. The
// kernel has one execution thread and never allocates from interrupt handlers.
extern "C" void* sbrk(std::ptrdiff_t increment)
{
    extern unsigned char __HEAP[], __HEAP_END[];
    static std::uintptr_t current = reinterpret_cast<std::uintptr_t>(__HEAP);
    const auto begin = reinterpret_cast<std::uintptr_t>(__HEAP);
    const auto end = reinterpret_cast<std::uintptr_t>(__HEAP_END);
    if ((increment >= 0 && static_cast<std::uintptr_t>(increment) > end - current) ||
        (increment < 0 && static_cast<std::uintptr_t>(-(increment + 1)) + 1 > current - begin))
    {
        errno = ENOMEM;
        return reinterpret_cast<void*>(-1);
    }
    const auto previous = current;
    current += increment;
    return reinterpret_cast<void*>(previous);
}

// No stderr exists on the ECU. Keep libc++ failure handling terminal and
// avoid retaining formatted I/O for diagnostics.
[[noreturn]] void std::_LIBCPP_ABI_NAMESPACE::__libcpp_verbose_abort(const char*, ...) noexcept
{
    Halt();
}
