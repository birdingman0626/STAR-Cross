#ifndef STAR_UNALIGNED_H
#define STAR_UNALIGNED_H
#include <cstring>
#include <type_traits>

// Native-endian fields in private byte buffers; no alignment or aliasing promise.
template<class T> inline T loadUnaligned(const void* address) {
    static_assert(std::is_trivially_copyable<T>::value, "byte field must be trivial");
    T value;
    std::memcpy(&value, address, sizeof(value));
    return value;
}
template<class T> inline void storeUnaligned(void* address, T value) {
    std::memcpy(address, &value, sizeof(value));
}
template<class T> class UnalignedPointer {
    char* address = nullptr;
public:
    class Reference {
        char* address;
    public:
        explicit Reference(char* address) : address(address) {}
        operator T() const { return loadUnaligned<T>(address); }
        Reference& operator=(T value) { storeUnaligned(address, value); return *this; }
        Reference& operator=(const Reference& other) { return *this = static_cast<T>(other); }
        Reference& operator+=(T value) { return *this = static_cast<T>(*this)+value; }
    };
    UnalignedPointer() = default;
    UnalignedPointer(T* address) : address(reinterpret_cast<char*>(address)) {}
    Reference operator*() const { return Reference(address); }
};
#endif
