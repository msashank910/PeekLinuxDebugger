#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <string_view>


namespace util {
    std::string_view strip0x(const std::string_view& s);  //change function later
    
    inline bool isPrefix(const std::string& a, const std::string& b) {
        return std::equal(a.begin(), a.end(), b.begin());
    }

    bool validStol(uint64_t& num, std::string_view addr);
    
    std::vector<std::string> splitLine(const std::string &line, char delimiter);

}