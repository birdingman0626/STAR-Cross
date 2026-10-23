# include "PackedArray.h"
#include <stdexcept>

PackedArray::PackedArray() {
    charArray=NULL;
    bitRecMask=wordCompLength=wordLength=length=lengthByte=0;
};

PackedArray::PackedArray(const PackedArray& other) : PackedArray() {
    *this = other;
}

PackedArray& PackedArray::operator=(const PackedArray& other) {
    if (this == &other) return *this;
    // Releasing an owner here could invalidate the source borrowed view.
    if (storage) throw std::logic_error("Cannot overwrite PackedArray owner with borrowed view");
    bitRecMask=other.bitRecMask; wordCompLength=other.wordCompLength;
    wordLength=other.wordLength; length=other.length; lengthByte=other.lengthByte;
    charArray=other.charArray;
    return *this;
}

PackedArray::PackedArray(PackedArray&& other) noexcept : PackedArray() {
    *this = std::move(other);
}

PackedArray& PackedArray::operator=(PackedArray&& other) noexcept {
    if (this == &other) return *this;
    // A moved borrowed view may alias our allocation. Keep the allocation alive
    // in that case; moving a view does not transfer release authority.
    if (other.storage || !storage) storage=std::move(other.storage);
    bitRecMask=other.bitRecMask; wordCompLength=other.wordCompLength;
    wordLength=other.wordLength; length=other.length; lengthByte=other.lengthByte;
    charArray=other.charArray;
    other.charArray=nullptr;
    other.bitRecMask=other.wordCompLength=other.wordLength=other.length=other.lengthByte=0;
    return *this;
}

void PackedArray::defineBits(uint64 Nbits, uint64 lengthIn){
    // A word must fit in the 64-bit load at every possible record offset.
    if (Nbits == 0 || Nbits > 64 || Nbits + 8-std::gcd(Nbits, static_cast<uint64>(8)) > 64)
        throw std::invalid_argument("PackedArray word crosses its 64-bit storage window");
    if (lengthIn > 0 && lengthIn-1 > (std::numeric_limits<uint64>::max()-8*sizeof(uint64))/Nbits)
        throw std::length_error("PackedArray storage size overflow");
    wordLength=Nbits;
    wordCompLength=sizeof(uint64)*8LLU-wordLength;
    bitRecMask=(~0LLU)>>wordCompLength;
    length=lengthIn;
    lengthByte=length == 0 ? sizeof(uint64) : (length-1)*wordLength/8LLU+sizeof(uint64);
//     lengthByte=((lengthByte+sizeof(uint64)-1LLU)/sizeof(uint64))*sizeof(uint64);
};

void PackedArray::writePacked( uint64 jj, uint64 x) {
   uint64 b=jj*wordLength;
   uint64 B=b/8LLU;
   uint64 S=b%8LLU;

   x = x << S;
   uint64 a1 = loadUintLE(charArray+B); // little-endian byte stream (native load on LE)
   a1 = ( a1 & ~(bitRecMask<<S) ) | x;
   storeUintLE(charArray+B, a1);
};

void PackedArray::pointArray(char* pointerCharIn) {
    charArray=pointerCharIn;
};

void PackedArray::allocateArray() {
    if (wordLength == 0)
        throw std::logic_error("PackedArray must be defined before allocation");
    if (storage)
        throw std::logic_error("PackedArray already owns storage");
    storage.reset(new char[lengthByte]);
    charArray=storage.get();
    memset(charArray+lengthByte-sizeof(uint64),0,sizeof(uint64));//set the last 8 bytes to zero, since some of them may lnever be written
};

void PackedArray::deallocateArray() {
    storage.reset();
    charArray=NULL;
};
