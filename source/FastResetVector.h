// FastResetVector.h - O(modified) reset instead of O(N) memset
// From upstream STAR PR #791 by Sebastian Uhrig (suhrig).
//
// Replaces dense array + memset with a tracking approach:
// only elements that were actually written get reset.
// Huge win when the array is sparse (typical case for winBin).

#ifndef FAST_RESET_VECTOR_H
#define FAST_RESET_VECTOR_H

#include <vector>
#include <cstring>

template <typename T>
class FastResetVector {
public:
    FastResetVector() : _size(0), _resetValue(0) {}

    void resize(size_t n, T resetValue = 0) {
        _size = n;
        _resetValue = resetValue;
        _data.assign(n, resetValue);
        _modified.clear();
        _modified.reserve(1024);
    }

    size_t size() const { return _size; }

    T operator[](size_t i) const { return _data[i]; }

    T& set(size_t i) {
        _modified.push_back(i);
        return _data[i];
    }

    // Set value and track modification
    void set(size_t i, T val) {
        _data[i] = val;
        _modified.push_back(i);
    }

    // Reset only modified elements (O(modified) instead of O(N))
    void reset() {
        for (size_t idx : _modified) {
            _data[idx] = _resetValue;
        }
        _modified.clear();
    }

    // Direct pointer access (for legacy code that needs raw pointer)
    T* data() { return _data.data(); }
    const T* data() const { return _data.data(); }

private:
    std::vector<T> _data;
    std::vector<size_t> _modified;
    size_t _size;
    T _resetValue;
};

#endif // FAST_RESET_VECTOR_H
