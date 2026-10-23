#ifndef DEF_SjdbClass
#define DEF_SjdbClass

#include "IncludeDefine.h"
#include <set>

class SjdbClass {
public:
    std::vector <std::string> chr;
    std::vector <uint64> start,end;
    std::vector <char> str;
    std::vector <uint8> priority;

    std::vector<std::set<uint64>> gene;
};

#endif
