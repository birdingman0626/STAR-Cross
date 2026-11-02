#ifndef STAR_UNINIT_ARRAY_H
#define STAR_UNINIT_ARRAY_H
#include <cstddef>
#include <memory>
#include <new>

// Owning replacements for a raw new T[n]: elements are default-initialized, so
// scalars stay uninitialized exactly as before. These are the only array
// new-expressions outside third-party code; GCC 10 and Clang 14 libraries lack
// std::make_unique_for_overwrite, hence the fallback.
template<class T>
std::unique_ptr<T[]> makeUninitArray(std::size_t n)
{
#if defined(__cpp_lib_smart_ptr_for_overwrite)
    return std::make_unique_for_overwrite<T[]>(n);
#else
    return std::unique_ptr<T[]>(new T[n]);
#endif
}

// As makeUninitArray, but returns an empty pointer when allocation fails.
template<class T>
std::unique_ptr<T[]> makeUninitArrayNoThrow(std::size_t n)
{
    return std::unique_ptr<T[]>(new (std::nothrow) T[n]);
}

#endif
