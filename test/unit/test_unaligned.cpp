#include "doctest/doctest.h"
#include "unaligned.h"
#include <cstdint>

TEST_CASE("Unaligned native fields preserve packed layout and copy values") {
    alignas(8) char bytes[32] = {};
    for (size_t offset=0; offset<8; ++offset) {
        storeUnaligned<std::uint64_t>(bytes+offset, 0xDEADBEEFCAFEBABEULL);
        CHECK(loadUnaligned<std::uint64_t>(bytes+offset) == 0xDEADBEEFCAFEBABEULL);
        UnalignedPointer<std::uint32_t> left(reinterpret_cast<std::uint32_t*>(bytes+offset));
        UnalignedPointer<std::uint32_t> right(reinterpret_cast<std::uint32_t*>(bytes+offset+8));
        *left = 42;
        *right = *left;
        *left += 1;
        CHECK(static_cast<std::uint32_t>(*left) == 43);
        CHECK(static_cast<std::uint32_t>(*right) == 42);
    }
    static_assert(sizeof(UnalignedPointer<std::uint64_t>) == sizeof(void*), "views add no record storage");
}
