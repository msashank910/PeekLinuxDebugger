#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>

namespace util {
    std::string strip0x(const std::string& s);  //change function later
    
    inline bool isPrefix(const std::string& a, const std::string& b) {
        return std::equal(a.begin(), a.end(), b.begin());
    }
    
    std::vector<std::string> splitLine(const std::string &line, char delimiter);

}